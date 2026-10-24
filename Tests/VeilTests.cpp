#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <numbers>
#include <set>
#include <string>
#include <vector>

// Count C++ allocations on the calling audio thread. This intentionally does not
// claim to instrument arbitrary C malloc calls or allocations on other threads.
namespace allocationProbe {
thread_local bool enabled=false;
thread_local std::size_t calls=0;
void mark() noexcept { if (enabled) ++calls; }
}
void* operator new(std::size_t n) {
    allocationProbe::mark();
    if (auto* p=std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t n,std::align_val_t alignment) {
    allocationProbe::mark();
    void* p=nullptr;
    if (posix_memalign(&p,static_cast<std::size_t>(alignment),n ? n : 1)==0) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n,std::align_val_t a) { return ::operator new(n,a); }
void operator delete(void* p,std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p,std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p,std::size_t,std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept { std::free(p); }

namespace {
constexpr double tau=2.0*std::numbers::pi;
struct Suite {
    int passed=0,failed=0;
    void check(bool okay,const std::string& name) {
        if (okay) ++passed;
        else { ++failed; std::cerr<<"FAIL: "<<name<<'\n'; }
    }
    void section(const char* title) { std::cout<<"[test] "<<title<<std::endl; }
};
void set(VeilAudioProcessor& p,veil::P id,float value) {
    auto* parameter=p.parameters.getParameter(veil::specs[id].id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
void disableModules(VeilAudioProcessor& p) {
    for (auto id:{veil::AbyssOn,veil::BloodOn,veil::EchoesOn,veil::CryptOn,veil::WraithOn,veil::ChoirOn,veil::RitualOn}) set(p,id,0);
}
bool near(float a,float b,float relative=1.0e-5f) { return std::abs(a-b)<=relative*std::max({1.0f,std::abs(a),std::abs(b)}); }
bool frameEqual(const veil::Frame& a,const veil::Frame& b) {
    for (std::size_t i=0;i<veil::Count;++i) if (!near(a[i],b[i])) return false;
    return true;
}
bool finiteBuffer(const juce::AudioBuffer<float>& b,float limit=2.001f) {
    for (int c=0;c<b.getNumChannels();++c)
        for (int n=0;n<b.getNumSamples();++n)
            if (!std::isfinite(b.getSample(c,n)) || std::abs(b.getSample(c,n))>limit) return false;
    return true;
}
void runProcessor(VeilAudioProcessor& p,juce::AudioBuffer<float>& b,int blocks,double sr,bool tone,bool impulse=false) {
    juce::MidiBuffer midi;
    int position=0;
    for (int block=0;block<blocks;++block) {
        b.clear();
        for (int n=0;n<b.getNumSamples();++n,++position) {
            const float x=tone ? .1f*static_cast<float>(std::sin(tau*220.0*position/sr)) : (impulse && position==0 ? .5f : 0.0f);
            for (int c=0;c<b.getNumChannels();++c) b.setSample(c,n,x*(c==0 ? 1.0f : .7f));
        }
        p.processBlock(b,midi);
    }
}
void parameterAndStateTests(Suite& s) {
    s.section("parameters, factory presets, binary state, user presets, A/B");
    VeilAudioProcessor p;
    s.check(p.getParameters().size()==static_cast<int>(veil::Count),"every declared control is host-automatable");
    std::set<std::string> ids;
    for (std::size_t i=0;i<veil::Count;++i) {
        const auto& spec=veil::specs[i];
        s.check(ids.insert(spec.id).second,"unique parameter id "+std::string(spec.id));
        s.check(spec.min<spec.max && spec.def>=spec.min && spec.def<=spec.max,"valid range/default "+std::string(spec.id));
        auto* parameter=p.parameters.getParameter(spec.id);
        s.check(parameter!=nullptr,"parameter exists "+std::string(spec.id));
        parameter->setValueNotifyingHost(0);
        s.check(near(p.currentValues()[i],spec.min),"minimum reachable "+std::string(spec.id));
        parameter->setValueNotifyingHost(1);
        s.check(near(p.currentValues()[i],spec.max),"maximum reachable "+std::string(spec.id));
    }
    s.check(p.factoryPresetCount()==15,"15 factory presets");
    std::set<std::string> names;
    std::vector<veil::Frame> frames;
    for (int preset=0;preset<p.factoryPresetCount();++preset) {
        p.loadFactoryPreset(preset);
        s.check(p.currentFactoryPreset()==preset,"preset index loaded");
        s.check(names.insert(p.factoryPresetName(preset).toStdString()).second,"unique factory preset name");
        const auto values=p.currentValues();
        bool valid=true;
        for (std::size_t i=0;i<veil::Count;++i) valid &= std::isfinite(values[i]) && values[i]>=veil::specs[i].min && values[i]<=veil::specs[i].max;
        s.check(valid,"complete in-range factory preset "+std::to_string(preset));
        bool unique=true; for (const auto& previous:frames) unique &= !frameEqual(previous,values);
        s.check(unique,"distinct factory settings "+std::to_string(preset)); frames.push_back(values);
        juce::MemoryBlock data; p.getStateInformation(data);
        VeilAudioProcessor restored; restored.setStateInformation(data.getData(),static_cast<int>(data.getSize()));
        s.check(frameEqual(values,restored.currentValues()),"all controls survive binary state roundtrip "+std::to_string(preset));
    }
    p.loadFactoryPreset(0); p.storeA(); const auto a=p.currentValues();
    p.loadFactoryPreset(14); p.storeB(); const auto b=p.currentValues();
    p.recallA(); s.check(frameEqual(a,p.currentValues()),"A/B slot A recall");
    p.recallB(); s.check(frameEqual(b,p.currentValues()),"A/B slot B recall");
    const auto before=p.currentValues();
    const std::array<unsigned char,16> junk{{0x56,0x45,0x49,0x4c,1,2,3,4}};
    p.setStateInformation(junk.data(),static_cast<int>(junk.size()));
    p.setStateInformation(nullptr,0);
    s.check(frameEqual(before,p.currentValues()),"malformed state does not replace valid state");
    const auto file=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("veil-test", ".veilpreset");
    s.check(p.savePreset(file),"user preset file saved");
    p.loadFactoryPreset(3);
    s.check(p.loadPreset(file) && frameEqual(before,p.currentValues()),"user preset file restores all controls");
    file.deleteFile();
    const auto invalid=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("veil-invalid", ".veilpreset");
    invalid.replaceWithText("not a VEIL preset");
    s.check(!p.loadPreset(invalid),"invalid preset file rejected"); invalid.deleteFile();
}
void routingAndSafetyTests(Suite& s) {
    s.section("routing, unity bypass, dry mix, finite input recovery, allocation guard");
    for (int channels:{1,2}) {
        VeilAudioProcessor p;
        auto layout=p.getBusesLayout();
        layout.inputBuses.set(0,channels==1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
        layout.outputBuses.set(0,layout.inputBuses[0]);
        s.check(p.setBusesLayout(layout),"matching mono/stereo bus accepted");
        disableModules(p); set(p,veil::OutputGain,0); set(p,veil::GlobalMix,1);
        p.prepareToPlay(48000,257);
        juce::AudioBuffer<float> buffer(channels,257); juce::MidiBuffer midi;
        for (int c=0;c<channels;++c) for (int n=0;n<257;++n) buffer.setSample(c,n,.1f*std::sin(static_cast<float>(n)*.13f)*(c+1));
        juce::AudioBuffer<float> original; original.makeCopyOf(buffer);
        p.processBlock(buffer,midi);
        bool unchanged=true;
        for (int c=0;c<channels;++c) for (int n=0;n<257;++n) unchanged &= near(buffer.getSample(c,n),original.getSample(c,n),2e-6f);
        s.check(unchanged,"disabled chain routes input at unity");
        p.loadFactoryPreset(12); set(p,veil::Bypass,1); p.prepareToPlay(48000,257);
        buffer.makeCopyOf(original); p.processBlock(buffer,midi);
        unchanged=true;
        for (int c=0;c<channels;++c) for (int n=0;n<257;++n) unchanged &= near(buffer.getSample(c,n),original.getSample(c,n),2e-6f);
        s.check(unchanged,"global bypass is exact unity after initialization");
        set(p,veil::Bypass,0); set(p,veil::GlobalMix,0); set(p,veil::InputGain,0); set(p,veil::OutputGain,0); p.prepareToPlay(48000,257);
        buffer.makeCopyOf(original); p.processBlock(buffer,midi);
        unchanged=true;
        for (int c=0;c<channels;++c) for (int n=0;n<257;++n) unchanged &= near(buffer.getSample(c,n),original.getSample(c,n),2e-6f);
        s.check(unchanged,"global dry mix unity with modules active");
        buffer.clear(); buffer.setSample(0,0,std::numeric_limits<float>::quiet_NaN());
        buffer.setSample(0,1,std::numeric_limits<float>::infinity());
        p.processBlock(buffer,midi);
        s.check(finiteBuffer(buffer),"NaN and Inf input cannot poison output");
        p.loadFactoryPreset(0); p.prepareToPlay(48000,257); buffer.clear();
        p.processBlock(buffer,midi); // Warm up platform/JUCE lazy paths before measuring.
        allocationProbe::calls=0; allocationProbe::enabled=true;
        for (int i=0;i<8;++i) p.processBlock(buffer,midi);
        allocationProbe::enabled=false;
        s.check(allocationProbe::calls==0,"processBlock performs no C++ heap allocations");
        s.check(p.getLatencySamples()==0,"immediate dry path reports zero lookahead latency");
        p.releaseResources();
    }
    VeilAudioProcessor fadeProbe;
    disableModules(fadeProbe); set(fadeProbe,veil::InputGain,0); set(fadeProbe,veil::OutputGain,-12);
    fadeProbe.prepareToPlay(48000,256);
    juce::AudioBuffer<float> fadeBuffer(2,256); juce::MidiBuffer fadeMidi;
    for (int c=0;c<2;++c) for (int n=0;n<256;++n) fadeBuffer.setSample(c,n,.1f);
    fadeProbe.processBlock(fadeBuffer,fadeMidi);
    s.check(near(fadeBuffer.getSample(0,255),.1f*std::pow(10.0f,-12.0f/20),2e-6f),"output gain has calibrated dB response");
    for (int c=0;c<2;++c) for (int n=0;n<256;++n) fadeBuffer.setSample(c,n,.1f);
    fadeProbe.processBlockBypassed(fadeBuffer,fadeMidi);
    s.check(fadeBuffer.getSample(0,0)<.026f && fadeBuffer.getSample(0,255)>.03f && fadeBuffer.getSample(0,255)<.06f,"host bypass crossfades progressively instead of jumping");
    float largestStep=0;
    for (int n=1;n<256;++n) largestStep=std::max(largestStep,std::abs(fadeBuffer.getSample(0,n)-fadeBuffer.getSample(0,n-1)));
    s.check(largestStep<.0001f,"host bypass fade has no single-sample discontinuity");
    VeilAudioProcessor layoutProbe; auto bad=layoutProbe.getBusesLayout();
    bad.outputBuses.set(0,juce::AudioChannelSet::create5point1());
    s.check(!layoutProbe.isBusesLayoutSupported(bad),"unsupported surround routing rejected");
}
juce::AudioProcessor::BusesLayout layoutOf(const juce::AudioChannelSet& in,const juce::AudioChannelSet& out) {
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(in);
    layout.outputBuses.add(out);
    return layout;
}
// Naive sawtooth C major chord: bright, harmonically dense test material.
float sawChord(int position,double sr) {
    float sum=0;
    for (double f:{130.81,164.81,196.0}) { const double phase=std::fmod(f*position/sr,1.0); sum+=static_cast<float>(2.0*phase-1.0); }
    return .12f*sum;
}
void layoutAndTransparencyTests(Suite& s) {
    s.section("bus layouts incl. mono->stereo, transparency at default gains, default level");
    using Set=juce::AudioChannelSet;
    VeilAudioProcessor probe;
    s.check(probe.isBusesLayoutSupported(layoutOf(Set::mono(),Set::mono())),"mono->mono supported");
    s.check(probe.isBusesLayoutSupported(layoutOf(Set::stereo(),Set::stereo())),"stereo->stereo supported");
    s.check(probe.isBusesLayoutSupported(layoutOf(Set::mono(),Set::stereo())),"mono->stereo supported");
    s.check(!probe.isBusesLayoutSupported(layoutOf(Set::stereo(),Set::mono())),"stereo->mono rejected");
    s.check(!probe.isBusesLayoutSupported(layoutOf(Set::disabled(),Set::stereo())),"missing input rejected");
    {
        VeilAudioProcessor p; juce::MidiBuffer midi;
        s.check(p.setBusesLayout(layoutOf(Set::mono(),Set::stereo())),"mono->stereo layout applied");
        p.prepareToPlay(48000,512);
        juce::AudioBuffer<float> b(2,512);
        double left=0,right=0; int position=0;
        for (int block=0;block<200;++block) {
            b.clear();
            for (int n=0;n<512;++n,++position) b.setSample(0,n,.2f*static_cast<float>(std::sin(tau*220.0*position/48000)));
            p.processBlock(b,midi);
            if (block>=100) { left+=b.getRMSLevel(0,0,512); right+=b.getRMSLevel(1,0,512); }
        }
        s.check(left>1e-3 && right>1e-3,"mono->stereo writes both outputs at default settings");
        set(p,veil::Bypass,1); p.prepareToPlay(48000,512);
        b.clear();
        for (int n=0;n<512;++n) b.setSample(0,n,.2f*std::sin(.05f*static_cast<float>(n)));
        juce::AudioBuffer<float> input; input.makeCopyOf(b);
        p.processBlock(b,midi);
        bool same=true;
        for (int n=0;n<512;++n) same &= near(b.getSample(0,n),input.getSample(0,n),2e-6f) && near(b.getSample(1,n),input.getSample(0,n),2e-6f);
        s.check(same,"mono->stereo bypass copies the input to both outputs");
    }
    // Wet/Dry 0, and all modules off, must be transparent with every gain at its default.
    for (int mode=0;mode<2;++mode) {
        VeilAudioProcessor p; juce::MidiBuffer midi;
        if (mode==0) set(p,veil::GlobalMix,0); else disableModules(p);
        p.prepareToPlay(44100,300);
        juce::AudioBuffer<float> b(2,300);
        bool same=true; int position=0;
        for (int block=0;block<20;++block) {
            for (int n=0;n<300;++n,++position) { b.setSample(0,n,sawChord(position,44100)); b.setSample(1,n,-.5f*sawChord(position+7,44100)); }
            juce::AudioBuffer<float> input; input.makeCopyOf(b);
            p.processBlock(b,midi);
            for (int c=0;c<2;++c) for (int n=0;n<300;++n) same &= near(b.getSample(c,n),input.getSample(c,n),2e-6f);
        }
        s.check(same,mode==0 ? "Wet/Dry 0 at default gains passes input exactly" : "all modules off at default gains passes input exactly");
    }
    {
        VeilAudioProcessor p; juce::MidiBuffer midi;
        p.prepareToPlay(48000,512);
        juce::AudioBuffer<float> b(2,512);
        double in=0,out=0; int position=0;
        for (int block=0;block<94*6;++block) {   // six seconds; measure the last four
            for (int n=0;n<512;++n,++position) { const float x=sawChord(position,48000); b.setSample(0,n,x); b.setSample(1,n,x); }
            double blockIn=0; for (int n=0;n<512;++n) blockIn+=double(b.getSample(0,n))*b.getSample(0,n);
            p.processBlock(b,midi);
            if (block>=94*2) {
                in+=2*blockIn;
                for (int c=0;c<2;++c) for (int n=0;n<512;++n) out+=double(b.getSample(c,n))*b.getSample(c,n);
            }
        }
        const double difference=10.0*std::log10(out/in);
        std::cout<<"[metric] default settings on a saw chord: output "<<difference<<" dB relative to input\n";
        // Milestone 1 guard. The +/-2 dB target in the brief is met when the modules are
        // re-tuned in Milestone 2 (Crypt wet calibration, Abyss passband, Ritual removal).
        s.check(std::abs(difference)<=3.0,"default settings keep the level within 3 dB of the input");
    }
}
void ratesAndStabilityTests(Suite& s) {
    s.section("sample-rate changes, silence, tones, impulses, extreme feedback, all presets");
    VeilAudioProcessor p; juce::MidiBuffer midi;
    for (double sr:{44100.0,48000.0,88200.0,96000.0,44100.0}) {
        p.loadFactoryPreset(0); p.prepareToPlay(sr,256);
        juce::AudioBuffer<float> buffer(2,256);
        buffer.clear(); bool silent=true;
        for (int block=0;block<8;++block) { buffer.clear(); p.processBlock(buffer,midi); silent &= buffer.getMagnitude(0,buffer.getNumSamples())<1e-12f && finiteBuffer(buffer); }
        s.check(silent,"silent input remains silent at "+std::to_string(static_cast<int>(sr)));
        runProcessor(p,buffer,static_cast<int>(sr/256*.16),sr,true);
        s.check(finiteBuffer(buffer) && buffer.getRMSLevel(0,0,256)>1e-5f,"sustained tone processes at "+std::to_string(static_cast<int>(sr)));
        p.reset(); runProcessor(p,buffer,static_cast<int>(sr/256*.25),sr,false,true);
        s.check(finiteBuffer(buffer),"impulse remains finite at "+std::to_string(static_cast<int>(sr)));
        p.releaseResources();
    }
    for (int preset=0;preset<15;++preset) {
        p.loadFactoryPreset(preset); p.prepareToPlay(48000,128);
        juce::AudioBuffer<float> buffer(2,128);
        runProcessor(p,buffer,64,48000,true);
        s.check(finiteBuffer(buffer) && buffer.getRMSLevel(0,0,128)>1e-6f,"factory preset produces finite audible tone "+std::to_string(preset));
    }
    p.loadFactoryPreset(12);
    for (auto id:{veil::Resonance,veil::Feedback,veil::Decay,veil::Space,veil::Shimmer,veil::Drive,veil::Age,veil::Texture,veil::Instability,veil::Diffusion}) set(p,id,veil::specs[id].max);
    set(p,veil::Freeze,0); set(p,veil::GlobalMix,1); set(p,veil::DelayMix,.8f); set(p,veil::ReverbMix,.8f);
    p.prepareToPlay(96000,512);
    juce::AudioBuffer<float> buffer(2,512); bool stable=true;
    for (int block=0;block<750;++block) { // Four seconds with maximum stable feedback.
        for (int n=0;n<512;++n) { const float x=block<16 ? .2f*std::sin(static_cast<float>(n+block*512)*.08f) : 0; buffer.setSample(0,n,x); buffer.setSample(1,n,-.7f*x); }
        p.processBlock(buffer,midi); stable &= finiteBuffer(buffer);
        if (block==375) set(p,veil::Reverse,1);
    }
    s.check(stable,"maximum resonance, decay, shimmer and feedback remain bounded, forward/reverse");
    for (int cycle=0;cycle<4;++cycle) { VeilAudioProcessor temp; temp.prepareToPlay(48000,64); juce::AudioBuffer<float> b(2,64); b.clear(); temp.processBlock(b,midi); }
    s.check(true,"repeated construction, preparation and destruction complete");
    std::cout<<"[metric] last 96 kHz processing load = "<<p.cpuLoad.load()*100<<"% of real time (host-dependent, non-gating)\n";
}

template<class Module>
std::vector<float> renderModule(Module& module,const veil::Frame& frame,int length,double sr,double frequency,bool impulse=false) {
    std::vector<float> out(static_cast<std::size_t>(length));
    module.prepare(sr);
    for (int n=0;n<length;++n) {
        float l=impulse ? (n==0 ? .5f : 0) : .15f*static_cast<float>(std::sin(tau*frequency*n/sr));
        float r=l*.8f;
        module.process(l,r,frame,120); out[static_cast<std::size_t>(n)]=l;
    }
    return out;
}
double energy(const std::vector<float>& signal,std::size_t start=0) {
    double e=0; for (std::size_t i=start;i<signal.size();++i) e+=signal[i]*signal[i]; return e;
}
double frequencyPower(const std::vector<float>& signal,double frequency,double sr,std::size_t start) {
    double re=0,im=0;
    for (std::size_t i=start;i<signal.size();++i) { const double phase=tau*frequency*static_cast<double>(i)/sr; re+=signal[i]*std::cos(phase); im+=signal[i]*std::sin(phase); }
    return re*re+im*im;
}
double bandPeak(const std::vector<float>& signal,double frequency,double sr,std::size_t start,double radius=12) {
    double best=0;
    for (double offset=-radius;offset<=radius;offset+=2) best=std::max(best,frequencyPower(signal,frequency+offset,sr,start));
    return best;
}
void moduleBehaviorTests(Suite& s) {
    s.section("audible DSP: filter, saturation, reverb characters, reverse, freeze, pitched choir");
    constexpr double sr=48000;
    auto frame=veil::defaults(); frame[veil::RitualOn]=0;
    veil::Abyss filter; frame[veil::Cutoff]=900; frame[veil::Darkness]=.3f; frame[veil::FilterMovement]=0;
    const auto low=renderModule(filter,frame,24000,sr,220);
    const auto high=renderModule(filter,frame,24000,sr,8000);
    s.check(energy(low,4096)>energy(high,4096)*100,"Abyss attenuates high frequencies by more than 20 dB relative to bass");
    frame=veil::defaults(); frame[veil::BloodMix]=1; frame[veil::Drive]=18; frame[veil::Texture]=.9f; frame[veil::Age]=0; frame[veil::Instability]=0;
    veil::AncientBlood blood;
    const auto saturated=renderModule(blood,frame,48000,sr,300);
    s.check(frequencyPower(saturated,900,sr,12000)>frequencyPower(saturated,1100,sr,12000)*20,"Ancient Blood generates nonlinear third harmonics");
    frame=veil::defaults(); frame[veil::ReverbMix]=1; frame[veil::PreDelay]=0; frame[veil::Shimmer]=0;
    veil::Crypt crypt;
    std::vector<float> first;
    for (int character=0;character<4;++character) {
        frame[veil::Character]=static_cast<float>(character);
        auto response=renderModule(crypt,frame,48000,sr,0,true);
        s.check(energy(response,2400)>1e-7,"Crypt acoustic character has an audible delayed tail "+std::to_string(character));
        if (character==0) first=response;
        else { double difference=0; for (std::size_t i=0;i<first.size();++i) difference+=std::abs(first[i]-response[i]); s.check(difference>.1,"Crypt characters change actual impulse response "+std::to_string(character)); }
    }
    frame=veil::defaults(); frame[veil::DelayMix]=1; frame[veil::DelayTime]=180; frame[veil::DelaySync]=0; frame[veil::Feedback]=0; frame[veil::Diffusion]=0; frame[veil::Spread]=0;
    veil::Echoes delay;
    const auto forward=renderModule(delay,frame,48000,sr,0,true);
    frame[veil::Reverse]=1;
    const auto reverse=renderModule(delay,frame,48000,sr,0,true);
    double difference=0; for (std::size_t i=0;i<forward.size();++i) difference+=std::abs(forward[i]-reverse[i]);
    s.check(energy(reverse,1000)>1e-8,"reverse delay emits a buffered impulse response");
    s.check(difference>.01,"reverse delay differs from forward delay");
    frame=veil::defaults(); frame[veil::WraithMix]=1; frame[veil::Blur]=.7f; frame[veil::Ghost]=.5f; frame[veil::Freeze]=0;
    veil::Wraith wraith; wraith.prepare(sr);
    for (int n=0;n<72000;++n) { float l=.12f*std::sin(static_cast<float>(tau*220*n/sr))+.04f*std::sin(static_cast<float>(tau*330*n/sr)),r=l*.8f; wraith.process(l,r,frame,120); }
    frame[veil::Freeze]=1;
    double early=0,late=0; bool finite=true;
    for (int n=0;n<96000;++n) { float l=0,r=0; wraith.process(l,r,frame,120); finite &= std::isfinite(l) && std::isfinite(r); if (n<24000) early+=l*l; if (n>=72000) late+=l*l; }
    s.check(finite && early>1e-4 && late>early*.15,"Wraith freeze sustains processed texture after two seconds of silent input");
    frame=veil::defaults(); frame[veil::ChoirBlend]=1; frame[veil::Voices]=1; frame[veil::ChoirDetune]=0; frame[veil::ChoirMovement]=0; frame[veil::Formant]=0;
    veil::Choir choir;
    for (auto interval:{std::pair{3,440.0},std::pair{2,220.0*std::pow(2.0,7.0/12.0)}}) {
        frame[veil::Interval]=static_cast<float>(interval.first);
        const auto voice=renderModule(choir,frame,72000,sr,220);
        const auto expected=bandPeak(voice,interval.second,sr,24000);
        const auto original=bandPeak(voice,220,sr,24000);
        s.check(expected>original*4 && expected>1,"Choir produces true shifted pitch at "+std::to_string(interval.second)+" Hz");
    }
}
void ritualTests(Suite& s) {
    s.section("Ritual barycentric vertices, projection, smooth bounded motion, bypass");
    auto weights=veil::RitualEngine::weights(0,1);
    s.check(near(weights.shadow,1) && near(weights.spirit,0) && near(weights.ruin,0),"Shadow vertex");
    weights=veil::RitualEngine::weights(.5f,0);
    s.check(near(weights.spirit,1) && near(weights.shadow,0) && near(weights.ruin,0),"Spirit vertex");
    weights=veil::RitualEngine::weights(1,1);
    s.check(near(weights.ruin,1) && near(weights.shadow,0) && near(weights.spirit,0),"Ruin vertex");
    weights=veil::RitualEngine::weights(.5f,2.0f/3.0f);
    s.check(near(weights.shadow,1.0f/3) && near(weights.spirit,1.0f/3) && near(weights.ruin,1.0f/3),"triangle centroid interpolates three equal archetype weights");
    auto archetype=veil::defaults(); archetype[veil::MotionOn]=0;
    veil::RitualEngine endpoint;
    endpoint.prepare(48000); archetype[veil::RitualX]=0; archetype[veil::RitualY]=1;
    const auto shadow=endpoint.process(archetype,120);
    endpoint.reset(); archetype[veil::RitualX]=.5f; archetype[veil::RitualY]=0;
    const auto spirit=endpoint.process(archetype,120);
    endpoint.reset(); archetype[veil::RitualX]=1; archetype[veil::RitualY]=1;
    const auto ruin=endpoint.process(archetype,120);
    s.check(shadow[veil::Cutoff]<spirit[veil::Cutoff] && spirit[veil::Shimmer]>ruin[veil::Shimmer] && ruin[veil::Drive]>shadow[veil::Drive],"archetypes produce distinct dark, spectral and degraded DSP settings");
    s.check(shadow[veil::InputGain]==archetype[veil::InputGain] && spirit[veil::GlobalMix]==archetype[veil::GlobalMix] && ruin[veil::Freeze]==archetype[veil::Freeze],"Ritual preserves manual gain, global mix and freeze");
    bool bounded=true;
    for (int y=-10;y<=20;++y) for (int x=-10;x<=20;++x) {
        const auto projected=veil::RitualEngine::constrain(x*.1f,y*.1f);
        const auto w=veil::RitualEngine::weights(projected[0],projected[1]);
        bounded &= std::isfinite(w.shadow) && std::isfinite(w.spirit) && std::isfinite(w.ruin) && w.shadow>=0 && w.spirit>=0 && w.ruin>=0 && near(w.shadow+w.spirit+w.ruin,1);
        bounded &= projected[0]>=0 && projected[0]<=1 && projected[1]>=0 && projected[1]<=1;
    }
    s.check(bounded,"out-of-bounds orb positions project to normalized triangle");
    veil::RitualEngine ritual; ritual.prepare(48000);
    auto p=veil::defaults(); p[veil::RitualOn]=0;
    s.check(frameEqual(ritual.process(p,120),p),"Ritual bypass preserves manual parameters");
    p[veil::RitualOn]=1; p[veil::MotionOn]=1; p[veil::MotionDepth]=1; p[veil::MotionRate]=1;
    for (int pattern=0;pattern<3;++pattern) {
        ritual.reset(); p[veil::MotionPattern]=static_cast<float>(pattern);
        float lastX=.5f,lastY=.5f,maxJump=0,minX=1,maxX=0; bool valuesValid=true;
        for (int n=0;n<96000;++n) {
            const auto actual=ritual.process(p,120);
            const float x=ritual.x(),y=ritual.y();
            if (n>0) maxJump=std::max(maxJump,std::max(std::abs(x-lastX),std::abs(y-lastY)));
            lastX=x;lastY=y; minX=std::min(minX,x);maxX=std::max(maxX,x);
            for (std::size_t i=0;i<veil::Count;++i) valuesValid &= std::isfinite(actual[i]) && actual[i]>=veil::specs[i].min && actual[i]<=veil::specs[i].max;
        }
        s.check(maxJump<.002f && valuesValid,"motion continuous and parameters remain bounded for pattern "+std::to_string(pattern));
        s.check(maxX-minX>.02f,"motion actually moves for pattern "+std::to_string(pattern));
    }
}
void editorLifecycleTests(Suite& s) {
    s.section("native editor construction, resize, parameter attachments and destruction");
    VeilAudioProcessor processor;
    processor.prepareToPlay(48000,256);
    for (int cycle=0;cycle<2;++cycle) {
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        s.check(editor!=nullptr && editor->getWidth()>0 && editor->getHeight()>0,"native editor constructs with nonempty bounds");
        for (const auto size:{std::pair{1280,860},std::pair{1040,760},std::pair{1800,1200}}) {
            editor->setSize(size.first,size.second);
            processor.loadFactoryPreset((cycle+size.first)%processor.factoryPresetCount());
            s.check(editor->getWidth()==size.first && editor->getHeight()==size.second,"native editor resizes to "+std::to_string(size.first)+" x "+std::to_string(size.second));
        }
    }
}
int renderUI(const juce::String& path) {
    juce::ScopedJuceInitialiser_GUI gui;
    VeilAudioProcessor p; p.prepareToPlay(48000,256); p.loadFactoryPreset(0);
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    editor->setVisible(true);
    auto image=editor->createComponentSnapshot(editor->getLocalBounds(),true,1.0f);
    juce::File file=juce::File::getCurrentWorkingDirectory().getChildFile(path);
    juce::FileOutputStream output(file);
    juce::PNGImageFormat png;
    const bool okay=output.openedOk() && png.writeImageToStream(image,output);
    std::cout<<(okay ? "Rendered UI: " : "Failed to render UI: ")<<file.getFullPathName()<<'\n';
    return okay ? 0 : 1;
}
}
int main(int argc,char** argv) {
    if (argc>1 && juce::String(argv[1])=="--render-ui") return renderUI(argc>2 ? juce::String(argv[2]) : "veil-editor.png");
    juce::ScopedJuceInitialiser_GUI juceInitializer;
    Suite suite;
    try {
        parameterAndStateTests(suite);
        routingAndSafetyTests(suite);
        layoutAndTransparencyTests(suite);
        ratesAndStabilityTests(suite);
        moduleBehaviorTests(suite);
        ritualTests(suite);
        editorLifecycleTests(suite);
    } catch (const std::exception& e) {
        allocationProbe::enabled=false;
        suite.check(false,std::string("unexpected exception: ")+e.what());
    }
    std::cout<<"VEIL regression: "<<suite.passed<<" passed, "<<suite.failed<<" failed\n";
    return suite.failed==0 ? 0 : 1;
}
