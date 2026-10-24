#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

juce::AudioProcessorValueTreeState::ParameterLayout VeilAudioProcessor::makeLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    for (const auto& s : veil::specs) {
        juce::ParameterID id { s.id, 1 };
        if (juce::String(s.choices).isNotEmpty()) {
            auto choices = juce::StringArray::fromTokens(s.choices, "|", "");
            if (s.max == 1 && (choices[0] == "Off"))
                result.add(std::make_unique<juce::AudioParameterBool>(id,s.name,s.def > .5f));
            else
                result.add(std::make_unique<juce::AudioParameterChoice>(id,s.name,choices,static_cast<int>(s.def)));
        } else if (juce::String(s.id) == "voices") {
            result.add(std::make_unique<juce::AudioParameterInt>(id,s.name,1,4,static_cast<int>(s.def)));
        } else {
            juce::NormalisableRange<float> range(s.min,s.max,0.0f,s.skew);
            auto attrs = juce::AudioParameterFloatAttributes().withLabel(s.unit);
            result.add(std::make_unique<juce::AudioParameterFloat>(id,s.name,range,s.def,attrs));
        }
    }
    return result;
}
VeilAudioProcessor::VeilAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true)
                                     .withOutput("Output",juce::AudioChannelSet::stereo(),true)),
      parameters(*this,nullptr,"VEIL_STATE",makeLayout()) {
    for (std::size_t i=0;i<veil::Count;++i) raw[i]=parameters.getRawParameterValue(veil::specs[i].id);
    smoothed=slotA=slotB=currentValues();
    for (const auto& s : veil::specs) parameters.addParameterListener(s.id,this);
}
VeilAudioProcessor::~VeilAudioProcessor() {
    for (const auto& s : veil::specs) parameters.removeParameterListener(s.id,this);
}
veil::Frame VeilAudioProcessor::currentValues() const noexcept {
    veil::Frame values{};
    for (std::size_t i=0;i<veil::Count;++i) {
        const auto v=raw[i]->load(std::memory_order_relaxed);
        values[i]=std::isfinite(v) ? juce::jlimit(veil::specs[i].min,veil::specs[i].max,v) : veil::specs[i].def;
    }
    return values;
}
void VeilAudioProcessor::prepareToPlay(double sr,int) {
    rate=std::isfinite(sr) ? juce::jlimit(8000.0,192000.0,sr) : 48000.0;
    smoothing=1.0f-std::exp(-1.0f/static_cast<float>(rate*.025));
    abyss.prepare(rate); crypt.prepare(rate); wraith.prepare(rate);
    blood.prepare(rate); echoes.prepare(rate); choir.prepare(rate); ritual.prepare(rate);
    smoothed=currentValues();
    setLatencySamples(0); // Immediate dry path; delayed granular layers are intentional effects.
}
void VeilAudioProcessor::reset() {
    abyss.reset(); crypt.reset(); wraith.reset(); blood.reset(); echoes.reset(); choir.reset(); ritual.reset();
    smoothed=currentValues();
    inputLevel=0; outputLevel=0;
}
bool VeilAudioProcessor::isBusesLayoutSupported(const BusesLayout& layout) const {
    const auto in=layout.getMainInputChannelSet(), out=layout.getMainOutputChannelSet();
    return in==out && (in==juce::AudioChannelSet::mono() || in==juce::AudioChannelSet::stereo());
}
void VeilAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) { process(buffer,false); }
void VeilAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) { process(buffer,true); }
void VeilAudioProcessor::process(juce::AudioBuffer<float>& buffer,bool forcedBypass) {
    juce::ScopedNoDenormals noDenormals;
    const auto begin=juce::Time::getHighResolutionTicks();
    auto target=currentValues();
    if (forcedBypass) target[veil::Bypass]=1;
    float bpm=120;
    if (auto* ph=getPlayHead()) if (auto pos=ph->getPosition()) if (auto tempo=pos->getBpm())
        if (std::isfinite(*tempo)) bpm=static_cast<float>(juce::jlimit(20.0,400.0,*tempo));
    const int channels=juce::jmin(buffer.getNumChannels(),getTotalNumInputChannels());
    for (int ch=channels;ch<buffer.getNumChannels();++ch) buffer.clear(ch,0,buffer.getNumSamples());
    if (channels<1) return;
    float inPeak=0,outPeak=0;
    auto* left=buffer.getWritePointer(0);
    auto* right=channels>1 ? buffer.getWritePointer(1) : nullptr;
    for (int n=0;n<buffer.getNumSamples();++n) {
        for (std::size_t i=0;i<veil::Count;++i) {
            // Choices are discrete. Enable/bypass parameters fade over 25 ms.
            bool choice=veil::specs[i].choices[0]!=0 && veil::specs[i].max>1;
            if (i==veil::Slope || i==veil::FilterSync || i==veil::MotionSync || i==veil::Freeze || i==veil::Reverse || i==veil::Voices) choice=true;
            smoothed[i]=choice ? target[i] : smoothed[i]+smoothing*(target[i]-smoothed[i]);
        }
        const auto p=ritual.process(smoothed,bpm);
        const float dryL=std::isfinite(left[n]) ? juce::jlimit(-32.0f,32.0f,left[n]) : 0;
        const float dryR=right ? (std::isfinite(right[n]) ? juce::jlimit(-32.0f,32.0f,right[n]) : 0) : dryL;
        inPeak=juce::jmax(inPeak,std::abs(dryL),std::abs(dryR));
        const float inGain=std::pow(10.0f,p[veil::InputGain]*.05f);
        float l=dryL*inGain, r=dryR*inGain;
        abyss.process(l,r,p,bpm); blood.process(l,r,p,bpm); echoes.process(l,r,p,bpm);
        crypt.process(l,r,p,bpm); wraith.process(l,r,p,bpm); choir.process(l,r,p,bpm);
        const float outGain=std::pow(10.0f,p[veil::OutputGain]*.05f);
        l=(dryL*inGain+(l-dryL*inGain)*p[veil::GlobalMix])*outGain;
        r=(dryR*inGain+(r-dryR*inGain)*p[veil::GlobalMix])*outGain;
        // Transparent below -0.17 dBFS; a soft safety knee bounds runaway sources.
        auto safe=[](float x) noexcept {
            if (!std::isfinite(x)) return 0.0f;
            const float a=std::abs(x);
            return a<=.98f ? x : std::copysign(.98f+1.02f*std::tanh((a-.98f)/1.02f),x);
        };
        l=safe(l); r=safe(r);
        const float bypass=smoothed[veil::Bypass];
        l+=bypass*(dryL-l); r+=bypass*(dryR-r);
        left[n]=right ? l : .5f*(l+r);
        if (right) right[n]=r;
        outPeak=juce::jmax(outPeak,std::abs(l),std::abs(r));
    }
    inputLevel.store(juce::jmax(inPeak,inputLevel.load()*.85f),std::memory_order_relaxed);
    outputLevel.store(juce::jmax(outPeak,outputLevel.load()*.85f),std::memory_order_relaxed);
    ritualDisplayX.store(ritual.x(),std::memory_order_relaxed);
    ritualDisplayY.store(ritual.y(),std::memory_order_relaxed);
    if (buffer.getNumSamples()>0) {
        auto seconds=juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks()-begin);
        cpuLoad.store(.9f*cpuLoad.load()+.1f*static_cast<float>(seconds*rate/buffer.getNumSamples()),std::memory_order_relaxed);
    }
}
juce::AudioProcessorParameter* VeilAudioProcessor::getBypassParameter() const { return parameters.getParameter("bypass"); }
juce::AudioProcessorEditor* VeilAudioProcessor::createEditor() { return new VeilAudioProcessorEditor(*this); }
int VeilAudioProcessor::factoryPresetCount() const { return static_cast<int>(veil::factoryPresets().size()); }
juce::String VeilAudioProcessor::factoryPresetName(int i) const { return i>=0 && i<factoryPresetCount() ? veil::factoryPresets()[static_cast<std::size_t>(i)].name : "Custom"; }
int VeilAudioProcessor::getNumPrograms() { return factoryPresetCount(); }
int VeilAudioProcessor::getCurrentProgram() { return juce::jmax(0,currentPreset.load()); }
const juce::String VeilAudioProcessor::getProgramName(int i) { return factoryPresetName(i); }
void VeilAudioProcessor::setCurrentProgram(int i) { loadFactoryPreset(i); }
void VeilAudioProcessor::applyValues(const veil::Frame& frame) {
    for (std::size_t i=0;i<veil::Count;++i) if (auto* parameter=parameters.getParameter(veil::specs[i].id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(juce::jlimit(veil::specs[i].min,veil::specs[i].max,frame[i])));
}
void VeilAudioProcessor::loadFactoryPreset(int i) {
    if (i<0 || i>=factoryPresetCount()) return;
    applyValues(veil::factoryPresets()[static_cast<std::size_t>(i)].values);
    currentPreset.store(i);
}
void VeilAudioProcessor::storeA() { slotA=currentValues(); }
void VeilAudioProcessor::storeB() { slotB=currentValues(); }
void VeilAudioProcessor::recallA() { applyValues(slotA); currentPreset=-1; }
void VeilAudioProcessor::recallB() { applyValues(slotB); currentPreset=-1; }
void VeilAudioProcessor::getStateInformation(juce::MemoryBlock& data) {
    auto state=parameters.copyState();
    state.setProperty("version",1,nullptr);
    if (auto xml=state.createXml()) copyXmlToBinary(*xml,data);
}
bool VeilAudioProcessor::restore(const void* data,int size) {
    if (size<=0 || size>1024*1024 || data==nullptr) return false;
    auto xml=getXmlFromBinary(data,size);
    if (!xml || !xml->hasTagName("VEIL_STATE")) return false;
    auto incoming=juce::ValueTree::fromXml(*xml);
    auto values=veil::defaults();
    for (const auto& child:incoming) {
        const auto id=child.getProperty("id").toString();
        for (std::size_t i=0;i<veil::Count;++i) if (id==veil::specs[i].id) {
            float value=static_cast<float>(child.getProperty("value",veil::specs[i].def));
            if (!std::isfinite(value)) return false;
            values[i]=juce::jlimit(veil::specs[i].min,veil::specs[i].max,value);
        }
    }
    applyValues(values); currentPreset=-1;
    return true;
}
void VeilAudioProcessor::setStateInformation(const void* data,int size) { restore(data,size); }
bool VeilAudioProcessor::savePreset(const juce::File& file) {
    juce::MemoryBlock data; getStateInformation(data);
    return file.replaceWithData(data.getData(),data.getSize());
}
bool VeilAudioProcessor::loadPreset(const juce::File& file) {
    if (file.getSize()>1024*1024) return false;
    juce::MemoryBlock data;
    return file.loadFileAsData(data) && restore(data.getData(),static_cast<int>(data.getSize()));
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VeilAudioProcessor(); }
