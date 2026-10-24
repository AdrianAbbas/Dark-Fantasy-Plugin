#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Dread.h"
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
    slotA=slotB=currentValues();
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
    rate=std::isfinite(sr) ? juce::jlimit(8000.0,384000.0,sr) : 48000.0;
    context.sampleRate=rate;
    abyss.prepare(rate); blood.prepare(rate); choir.prepare(rate);
    wraith.prepare(rate); echoes.prepare(rate); crypt.prepare(rate); ritual.prepare(rate);
    autoLevel.prepare(rate); limiter.prepare(rate);
    // Ancient Blood's oversampling (and its wow centre) is the only latency in the
    // main path. The dry path is delayed to match and the total is reported.
    latency=blood.latencySamples();
    for (auto& d : dryDelay) d.prepare(static_cast<std::size_t>(latency+8));
    for (auto* s : { &inputGain, &outputGain, &globalMix }) s->setTime(0.02,rate);
    bypassRamp.prepare(0.025,rate);
    for (auto& slot : slots) slot.gain.prepare(0.02,rate);
    setLatencySamples(latency);
    reset();
}
void VeilAudioProcessor::reset() {
    abyss.reset(); blood.reset(); choir.reset(); wraith.reset(); echoes.reset(); crypt.reset(); ritual.reset();
    autoLevel.reset(); limiter.reset();
    for (auto& d : dryDelay) d.clear();
    const auto v=currentValues();
    inputGain.snap(veil::dsp::dbToGain(v[veil::InputGain]));
    outputGain.snap(veil::dsp::dbToGain(v[veil::OutputGain]));
    globalMix.snap(v[veil::GlobalMix]);
    bypassRamp.value=v[veil::Bypass]>=.5f ? 1.0f : 0.0f;
    const veil::P enables[]={veil::AbyssOn,veil::ChoirOn,veil::WraithOn,veil::EchoesOn,veil::CryptOn};
    for (std::size_t i=0;i<slots.size();++i) { slots[i].gain.value=v[enables[i]]>=.5f ? 1.0f : 0.0f; slots[i].idle=slots[i].gain.value==0; }
    inputLevel=0; outputLevel=0;
}
bool VeilAudioProcessor::isBusesLayoutSupported(const BusesLayout& layout) const {
    // mono->mono, stereo->stereo and mono->stereo (Logic offers it on mono tracks).
    const auto in=layout.getMainInputChannelSet(), out=layout.getMainOutputChannelSet();
    const auto monoOrStereo=[](const juce::AudioChannelSet& set) {
        return set==juce::AudioChannelSet::mono() || set==juce::AudioChannelSet::stereo();
    };
    return monoOrStereo(in) && monoOrStereo(out) && in.size()<=out.size();
}
void VeilAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) { process(buffer,false); }
void VeilAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) { process(buffer,true); }
template <class Module>
void VeilAudioProcessor::runSlot(Module& module,Slot& slot,bool enabled,float* l,float* r,int n,const veil::Frame& p) noexcept {
    const float target=enabled ? 1.0f : 0.0f;
    if (slot.gain.value==0 && !enabled) { slot.idle=true; return; }
    if (slot.idle) { module.reset(); slot.idle=false; }
    auto& preL=pre[0]; auto& preR=pre[1];
    std::copy(l,l+n,preL.begin()); std::copy(r,r+n,preR.begin());
    module.process(l,r,n,p,context);
    for (int i=0;i<n;++i) {
        const float g=slot.gain.next(target);
        const auto k=static_cast<std::size_t>(i);
        l[i]=preL[k]+(l[i]-preL[k])*g;
        r[i]=preR[k]+(r[i]-preR[k])*g;
    }
}
void VeilAudioProcessor::process(juce::AudioBuffer<float>& buffer,bool forcedBypass) {
    juce::ScopedNoDenormals noDenormals;
    const auto begin=juce::Time::getHighResolutionTicks();
    auto p=currentValues();
    if (forcedBypass) p[veil::Bypass]=1;
    double bpm=120;
    if (auto* ph=getPlayHead()) if (auto pos=ph->getPosition()) if (auto tempo=pos->getBpm())
        if (std::isfinite(*tempo)) bpm=juce::jlimit(20.0,400.0,*tempo);
    context.bpm=bpm;
    // Inputs and outputs are counted separately: with mono->stereo the right output
    // carries no input and must still be written.
    const int numSamples=buffer.getNumSamples();
    const int bufferChannels=buffer.getNumChannels();
    const int inputs=juce::jmin(bufferChannels,getTotalNumInputChannels());
    const int outputs=juce::jmin(bufferChannels,getTotalNumOutputChannels());
    for (int ch=inputs;ch<bufferChannels;++ch) buffer.clear(ch,0,numSamples);
    if (inputs<1 || outputs<1 || numSamples<=0) return;
    auto* left=buffer.getWritePointer(0);
    auto* right=outputs>1 ? buffer.getWritePointer(1) : nullptr;
    const bool stereoInput=inputs>1;
    if (p[veil::RitualOn]>=.5f) p=ritual.processBlock(p,static_cast<float>(bpm),numSamples);
    veil::applyDread(p,p[veil::Dread]);
    inputGain.set(veil::dsp::dbToGain(p[veil::InputGain]));
    outputGain.set(veil::dsp::dbToGain(p[veil::OutputGain]));
    globalMix.set(p[veil::GlobalMix]);
    const float bypassTarget=p[veil::Bypass]>=.5f ? 1.0f : 0.0f;
    const bool autoLevelOn=p[veil::AutoLevel]>=.5f;
    float inPeak=0,outPeak=0;
    for (int start=0;start<numSamples;start+=veil::maxChunk) {
        const int n=juce::jmin(veil::maxChunk,numSamples-start);
        float* l=work[0].data(); float* r=work[1].data();
        for (int i=0;i<n;++i) {
            const auto k=static_cast<std::size_t>(i);
            const float rawL=left[start+i], rawR=stereoInput ? right[start+i] : rawL;
            const float x0=std::isfinite(rawL) ? juce::jlimit(-32.0f,32.0f,rawL) : 0.0f;
            const float x1=std::isfinite(rawR) ? juce::jlimit(-32.0f,32.0f,rawR) : 0.0f;
            inPeak=juce::jmax(inPeak,std::abs(x0),std::abs(x1));
            dryDelay[0].push(x0); dryDelay[1].push(x1);
            dryAligned[0][k]=dryDelay[0].readInteger(static_cast<std::size_t>(latency)+1);
            dryAligned[1][k]=dryDelay[1].readInteger(static_cast<std::size_t>(latency)+1);
            const float g=inputGain.next();
            l[i]=x0*g; r[i]=x1*g;
        }
        // Fixed order: tone and saturation, then pitch and spectral layers, then
        // time effects, with the reverb last.
        runSlot(abyss,slots[0],p[veil::AbyssOn]>=.5f,l,r,n,p);
        blood.process(l,r,n,p,context);
        runSlot(choir,slots[1],p[veil::ChoirOn]>=.5f,l,r,n,p);
        runSlot(wraith,slots[2],p[veil::WraithOn]>=.5f,l,r,n,p);
        runSlot(echoes,slots[3],p[veil::EchoesOn]>=.5f,l,r,n,p);
        runSlot(crypt,slots[4],p[veil::CryptOn]>=.5f,l,r,n,p);
        autoLevel.process(dryAligned[0].data(),dryAligned[1].data(),l,r,n,autoLevelOn);
        for (int i=0;i<n;++i) {
            const auto k=static_cast<std::size_t>(i);
            const float mix=globalMix.next(), og=outputGain.next(), bypass=bypassRamp.next(bypassTarget);
            const float dl=dryAligned[0][k], dr=dryAligned[1][k];
            float ol=(dl+(l[i]-dl)*mix)*og, orr=(dr+(r[i]-dr)*mix)*og;
            limiter.process(ol,orr);
            ol+=bypass*(dl-ol); orr+=bypass*(dr-orr);
            if (!std::isfinite(ol)) ol=0;
            if (!std::isfinite(orr)) orr=0;
            if (right) { left[start+i]=ol; right[start+i]=orr; }
            else left[start+i]=.5f*(ol+orr);
            outPeak=juce::jmax(outPeak,std::abs(ol),std::abs(orr));
        }
    }
    inputLevel.store(juce::jmax(inPeak,inputLevel.load()*.85f),std::memory_order_relaxed);
    outputLevel.store(juce::jmax(outPeak,outputLevel.load()*.85f),std::memory_order_relaxed);
    ritualDisplayX.store(ritual.x(),std::memory_order_relaxed);
    ritualDisplayY.store(ritual.y(),std::memory_order_relaxed);
    const auto seconds=juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks()-begin);
    cpuLoad.store(.9f*cpuLoad.load()+.1f*static_cast<float>(seconds*rate/numSamples),std::memory_order_relaxed);
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
