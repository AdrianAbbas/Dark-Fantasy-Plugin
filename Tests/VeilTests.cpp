#include "TestSignals.h"
#include "Dread.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <set>
#include <string>

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

int renderDemos(const juce::String& directory);
int probe(int argc,char** argv);

using namespace veiltest;
using veil::P;

namespace {
struct Suite {
    int passed=0,failed=0;
    void check(bool okay,const std::string& name) {
        if (okay) ++passed;
        else { ++failed; std::cerr<<"FAIL: "<<name<<'\n'; }
    }
    void section(const char* title) { std::cout<<"[test] "<<title<<std::endl; }
    void metric(const std::string& name,double value,const char* unit="") { std::cout<<"[metric] "<<name<<" = "<<value<<unit<<"\n"; }
};
bool near(float a,float b,float relative=1.0e-5f) { return std::abs(a-b)<=relative*std::max({1.0f,std::abs(a),std::abs(b)}); }
bool frameEqual(const veil::Frame& a,const veil::Frame& b) {
    for (std::size_t i=0;i<veil::Count;++i) if (!near(a[i],b[i])) return false;
    return true;
}
constexpr double sr=48000;

//==============================================================================
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
    s.check(p.factoryPresetCount()==16,"16 factory presets (Init + 15 worlds)");
    s.check(p.factoryPresetName(0).startsWith("Init"),"first preset is Init - Clean Altar");
    std::set<std::string> names;
    for (int preset=0;preset<p.factoryPresetCount();++preset) {
        p.loadFactoryPreset(preset);
        s.check(p.currentFactoryPreset()==preset,"preset index loaded");
        s.check(names.insert(p.factoryPresetName(preset).toStdString()).second,"unique factory preset name");
        const auto values=p.currentValues();
        bool valid=true;
        for (std::size_t i=0;i<veil::Count;++i) valid &= std::isfinite(values[i]) && values[i]>=veil::specs[i].min && values[i]<=veil::specs[i].max;
        s.check(valid,"complete in-range factory preset "+std::to_string(preset));
        juce::MemoryBlock data; p.getStateInformation(data);
        VeilAudioProcessor restored; restored.setStateInformation(data.getData(),static_cast<int>(data.getSize()));
        s.check(frameEqual(values,restored.currentValues()),"all controls survive binary state roundtrip "+std::to_string(preset));
    }
    p.loadFactoryPreset(1); p.storeA(); const auto a=p.currentValues();
    p.loadFactoryPreset(15); p.storeB(); const auto b=p.currentValues();
    p.recallA(); s.check(frameEqual(a,p.currentValues()),"A/B slot A recall");
    p.recallB(); s.check(frameEqual(b,p.currentValues()),"A/B slot B recall");
    const auto before=p.currentValues();
    const std::array<unsigned char,16> junk{{0x56,0x45,0x49,0x4c,1,2,3,4}};
    p.setStateInformation(junk.data(),static_cast<int>(junk.size()));
    p.setStateInformation(nullptr,0);
    s.check(frameEqual(before,p.currentValues()),"malformed state does not replace valid state");
    const auto file=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("veil-test",".veilpreset");
    s.check(p.savePreset(file),"user preset file saved");
    p.loadFactoryPreset(3);
    s.check(p.loadPreset(file) && frameEqual(before,p.currentValues()),"user preset file restores all controls");
    file.deleteFile();
}

//==============================================================================
juce::AudioProcessor::BusesLayout layoutOf(const juce::AudioChannelSet& in,const juce::AudioChannelSet& out) {
    juce::AudioProcessor::BusesLayout layout; layout.inputBuses.add(in); layout.outputBuses.add(out); return layout;
}
void transparencyTests(Suite& s) {
    s.section("layouts, latency, bypass / wet-dry 0 / all modules off are exact after latency");
    using Set=juce::AudioChannelSet;
    VeilAudioProcessor probe;
    s.check(probe.isBusesLayoutSupported(layoutOf(Set::mono(),Set::mono())),"mono->mono supported");
    s.check(probe.isBusesLayoutSupported(layoutOf(Set::stereo(),Set::stereo())),"stereo->stereo supported");
    s.check(probe.isBusesLayoutSupported(layoutOf(Set::mono(),Set::stereo())),"mono->stereo supported");
    s.check(!probe.isBusesLayoutSupported(layoutOf(Set::stereo(),Set::mono())),"stereo->mono rejected");
    s.check(!probe.isBusesLayoutSupported(layoutOf(Set::create5point1(),Set::create5point1())),"surround rejected");
    const auto input=sawChord(sr,2.0,1.6,0.6f);
    for (int mode=0;mode<3;++mode) {
        auto p=makeProcessor();
        if (mode==0) set(*p,veil::Bypass,1);
        if (mode==1) set(*p,veil::GlobalMix,0);
        if (mode==2) modulesOff(*p);
        p->prepareToPlay(sr,512);
        const int latency=p->getLatencySamples();
        if (mode==0) { s.metric("reported latency",latency," samples"); s.check(latency>0 && latency<static_cast<int>(sr*0.005),"latency reported and below 5 ms"); }
        const auto r=render(*p,input,sr,512);
        const double residual=residualDb(r.out,r.in,latency,static_cast<std::size_t>(latency),r.out.size());
        const char* names[]={"bypass","wet/dry 0","all modules off"};
        s.metric(std::string(names[mode])+" residual vs delayed input",residual," dB");
        s.check(residual<-100,std::string(names[mode])+" passes the input exactly (after latency)");
    }
    {
        VeilAudioProcessor p; juce::MidiBuffer midi;
        s.check(p.setBusesLayout(layoutOf(Set::mono(),Set::stereo())),"mono->stereo layout applied");
        p.prepareToPlay(sr,512);
        juce::AudioBuffer<float> b(2,512);
        double left=0,right=0;
        for (int block=0;block<200;++block) {
            b.clear();
            for (int n=0;n<512;++n) b.setSample(0,n,.25f*std::sin(static_cast<float>(tau*220.0*(block*512+n)/sr)));
            p.processBlock(b,midi);
            if (block>=100) { left+=b.getRMSLevel(0,0,512); right+=b.getRMSLevel(1,0,512); }
        }
        s.check(left>1e-3 && right>1e-3,"mono->stereo writes both outputs at default settings");
    }
    {
        auto p=makeProcessor();
        juce::AudioBuffer<float> b(2,256); juce::MidiBuffer midi;
        b.clear(); b.setSample(0,0,std::numeric_limits<float>::quiet_NaN()); b.setSample(1,3,std::numeric_limits<float>::infinity());
        for (int i=0;i<20;++i) { p->processBlock(b,midi); b.clear(); }
        bool okay=true; for (int c=0;c<2;++c) for (int n=0;n<256;++n) okay &= std::isfinite(b.getSample(c,n));
        s.check(okay,"NaN and Inf input cannot poison output");
    }
}

//==============================================================================
// The user's headline requirement: defaults obviously transform a bright synth.
void defaultSoundTests(Suite& s) {
    s.section("defaults: obviously darker, big space, ghost layer, level kept, no clipping");
    auto p=makeProcessor();
    const auto input=sawChord(sr,9.0,5.0,0.6f);
    const auto r=render(*p,input,sr,512);
    const double cIn=centroid(r.in,sr,at(sr,2),at(sr,4.5)), cOut=centroid(r.out,sr,at(sr,2),at(sr,4.5));
    s.metric("default centroid in/out",cIn); s.metric("default centroid out",cOut," Hz");
    s.check(cOut<cIn*0.5,"defaults: spectral centroid at least halved (clearly darker)");
    const double sustainIn=meanSquare(r.in,at(sr,2),at(sr,4.5)), sustainOut=meanSquare(r.out,at(sr,2),at(sr,4.5));
    s.metric("default level change",toDb(sustainOut/sustainIn)," dB");
    s.check(std::abs(toDb(sustainOut/sustainIn))<=2.0,"defaults: sustained level within 2 dB of the input");
    const double tail=meanSquare(r.out,at(sr,6.3),at(sr,7.3));
    s.metric("default tail 1-2 s after release",toDb(tail/sustainOut)," dB");
    s.check(toDb(tail/sustainOut)>-30,"defaults: a big audible space (tail 1-2 s after release above -30 dB)");
    s.check(peak(r.out)<=0.97f && finite(r.out),"defaults: no clipping");
    const double residual=residualDb(r.out,r.in,r.latency,at(sr,2),at(sr,4.5));
    s.metric("default difference from dry",residual," dB");
    s.check(residual>-3,"defaults: output is substantially different from the dry signal");
}

//==============================================================================
// Every module alone, at its defaults, must make an obvious, measurable change.
void moduleAloneTests(Suite& s) {
    s.section("each module alone at defaults makes an obvious change");
    {   // Abyss: much darker.
        auto p=makeProcessor(); onlyModule(*p,veil::AbyssOn); p->prepareToPlay(sr,512);
        const auto r=render(*p,sawChord(sr,4,3.5,0.5f),sr);
        const double ratio=centroid(r.out,sr,at(sr,1),at(sr,3))/centroid(r.in,sr,at(sr,1),at(sr,3));
        s.metric("Abyss centroid ratio",ratio);
        s.check(ratio<0.45,"Abyss alone: spectral centroid falls below 45 % of the input");
    }
    {   // Ancient Blood: strong added harmonics on a pure tone.
        auto p=makeProcessor(); onlyModule(*p,veil::BloodOn); p->prepareToPlay(sr,512);
        const auto r=render(*p,sine(sr,3,220,0.3f,3),sr);
        const auto power=powerSpectrum(r.out,at(sr,1),at(sr,2.8));
        const double fundamental=bandPower(power,sr,220,15);
        double harmonics=0; for (int h=2;h<=7;++h) harmonics+=bandPower(power,sr,220.0*h,15);
        s.metric("Blood harmonics vs fundamental",toDb(harmonics/fundamental)," dB");
        s.check(toDb(harmonics/fundamental)>-20,"Blood alone: harmonics within 20 dB of the fundamental");
    }
    {   // Choir: real energy an octave below (default interval).
        auto p=makeProcessor(); onlyModule(*p,veil::ChoirOn); p->prepareToPlay(sr,512);
        const auto r=render(*p,sine(sr,3,220,0.3f,3),sr);
        const auto power=powerSpectrum(r.out,at(sr,1),at(sr,2.8));
        const double shifted=bandPower(power,sr,110,12), original=bandPower(power,sr,220,12);
        s.metric("Choir octave-below vs original",toDb(shifted/original)," dB");
        s.check(toDb(shifted/original)>-10,"Choir alone: octave-below voices within 10 dB of the original");
    }
    {   // Wraith: an audible octave-up ghost and a changed signal.
        auto p=makeProcessor(); onlyModule(*p,veil::WraithOn); p->prepareToPlay(sr,512);
        const auto r=render(*p,sine(sr,4,440,0.3f,4),sr);
        const auto power=powerSpectrum(r.out,at(sr,1.5),at(sr,3.8));
        const double ghost=bandPower(power,sr,880,20), original=bandPower(power,sr,440,20);
        s.metric("Wraith ghost (octave up) vs original",toDb(ghost/original)," dB");
        s.check(toDb(ghost/original)>-16,"Wraith alone: octave ghost within 16 dB of the original");
        const double residual=residualDb(r.out,r.in,r.latency,at(sr,1.5),at(sr,3.8));
        s.metric("Wraith difference from dry",residual," dB");
        s.check(residual>-6,"Wraith alone: output clearly differs from the input");
    }
    {   // Echoes: loud first repeat and several repeats.
        auto p=makeProcessor(); onlyModule(*p,veil::EchoesOn); p->prepareToPlay(sr,512);
        const auto r=render(*p,impulse(sr,4,0.8f),sr);
        const auto env=envelopeDb(r.out,sr,0);
        const double impulseDb=toDb(meanSquare(r.out,0,at(sr,0.01)));
        int repeats=0; double first=-200;
        for (int k=1;k<=6;++k) {
            const auto c=static_cast<std::size_t>(k*48);   // 480 ms per repeat, 10 ms bins
            double best=-200; for (std::size_t j=c-2;j<=c+2 && j<env.size();++j) best=std::max(best,env[j]);
            if (k==1) first=best-impulseDb;
            if (best>impulseDb-45) ++repeats;
        }
        s.metric("Echoes first repeat vs impulse",first," dB"); s.metric("Echoes repeats above -45 dB",repeats);
        s.check(first>-14,"Echoes alone: first repeat within 14 dB of the impulse");
        s.check(repeats>=4,"Echoes alone: at least four audible repeats");
    }
    {   // Crypt: long tail.
        auto p=makeProcessor(); onlyModule(*p,veil::CryptOn); p->prepareToPlay(sr,512);
        const auto r=render(*p,noise(sr,14,0.3f,0.08),sr);
        const double t60=decaySeconds(r.out,sr,at(sr,0.08));
        const double tail=meanSquare(r.out,at(sr,1.0),at(sr,1.5)), burst=meanSquare(r.in,0,at(sr,0.08));
        s.metric("Crypt decay time",t60," s"); s.metric("Crypt tail at 1 s vs burst",toDb(tail/burst)," dB");
        s.check(t60>4.0,"Crypt alone: decay time above 4 s");
        s.check(toDb(tail/burst)>-30,"Crypt alone: tail one second later within 30 dB of the burst");
    }
}

//==============================================================================
void featureTests(Suite& s) {
    s.section("Descent, Rising, Eternal, Tomb Resonator, Drone, Freeze");
    {   // Descent (octave): each repeat an octave lower.
        auto p=makeProcessor({{veil::Descent,3},{veil::DelayMix,1},{veil::Feedback,.8f},{veil::DelayDamping,0},{veil::Diffusion,0},{veil::Spread,0},{veil::DelayTime,400}});
        onlyModule(*p,veil::EchoesOn); p->prepareToPlay(sr,512);
        const auto r=render(*p,sine(sr,2.0,880,0.4f,0.15),sr);
        bool falling=true; std::string found;
        for (int k=1;k<=3;++k) {
            const auto a=at(sr,0.4*k+0.03), b=at(sr,0.4*k+0.13);
            const double expected=880.0/std::pow(2.0,k);
            const auto power=powerSpectrum(r.out,a>2048?a-2048:0,b+2048);
            const double target=bandPower(power,sr,expected,expected*0.08), above=bandPower(power,sr,expected*2,expected*0.16);
            found+=std::to_string(toDb(target/above))+" ";
            falling &= target>above;
        }
        s.metric("Descent repeat energy at expected vs previous octave (dB, repeats 1-3)",0); std::cout<<"         "<<found<<"\n";
        s.check(falling,"Descent: repeats 1-3 each sit an octave lower");
    }
    {   // Rising: the reverb swells instead of decaying.
        auto measure=[&](float mode) {
            auto p=makeProcessor({{veil::CryptMode,mode},{veil::ReverbMix,1},{veil::RiseTime,1.0f},{veil::PreDelay,0}});
            onlyModule(*p,veil::CryptOn); p->prepareToPlay(sr,512);
            const auto r=render(*p,noise(sr,3,0.3f,0.05),sr);
            return meanSquare(r.out,at(sr,0.7),at(sr,1.1))/meanSquare(r.out,at(sr,0.06),at(sr,0.4));
        };
        const double normal=measure(0), rising=measure(1);
        s.metric("Rising late/early energy (normal, rising)",toDb(normal)); std::cout<<"         rising "<<toDb(rising)<<" dB\n";
        s.check(toDb(rising)>toDb(normal)+10,"Rising: late energy grows at least 10 dB relative to normal mode");
    }
    {   // Eternal: holds the tail.
        auto p=makeProcessor({{veil::ReverbMix,1},{veil::Decay,2}});
        onlyModule(*p,veil::CryptOn); p->prepareToPlay(sr,512);
        const auto input=noise(sr,13,0.3f,1.0);
        const auto r=render(*p,input,sr,512,[&](std::size_t position) { if (position>=at(sr,0.9)) set(*p,veil::Eternal,1); });
        const double early=meanSquare(r.out,at(sr,2),at(sr,3)), late=meanSquare(r.out,at(sr,11),at(sr,12));
        s.metric("Eternal level change 2 s -> 11 s",toDb(late/early)," dB");
        s.check(toDb(late/early)>-6 && late>1e-6,"Eternal: tail holds within 6 dB for ten seconds");
    }
    {   // Tomb resonator: noise rings at the tuned note (A3 = 220 Hz).
        auto measure=[&](float amount) {
            auto p=makeProcessor({{veil::Tomb,amount},{veil::TombNote,9},{veil::TombOctave,2},{veil::Cutoff,20000},{veil::Darkness,0},{veil::FilterMovement,0}});
            onlyModule(*p,veil::AbyssOn); p->prepareToPlay(sr,512);
            const auto r=render(*p,noise(sr,3,0.2f,3),sr);
            const auto power=powerSpectrum(r.out,at(sr,0.5),at(sr,2.8));
            return bandPower(power,sr,220,4)/bandPower(power,sr,311,4);
        };
        const double off=measure(0), on=measure(1);
        s.metric("Tomb 220 Hz vs 311 Hz (off)",toDb(off)," dB"); std::cout<<"         on "<<toDb(on)<<" dB\n";
        s.check(toDb(on)-toDb(off)>10,"Tomb Resonator: tuned note rises at least 10 dB above an untuned frequency");
    }
    {   // Drone: sub-octave holds after the note ends.
        auto p=makeProcessor({{veil::Drone,1},{veil::ChoirBlend,0},{veil::DroneHold,8}});
        onlyModule(*p,veil::ChoirOn); p->prepareToPlay(sr,512);
        const auto r=render(*p,sine(sr,5,220,0.3f,1.5),sr);
        const double during=bandPower(powerSpectrum(r.out,at(sr,0.6),at(sr,1.4)),sr,110,12);
        const double after=bandPower(powerSpectrum(r.out,at(sr,2.3),at(sr,3.4)),sr,110,12);
        s.metric("Drone sub-octave 1-2 s after the note vs during",toDb(after/during)," dB");
        s.check(toDb(after/during)>-20,"Drone: sub-octave layer holds after the note ends (within 20 dB)");
    }
    {   // Freeze: keeps sounding after the input stops.
        auto p=makeProcessor({{veil::WraithMix,1}});
        onlyModule(*p,veil::WraithOn); p->prepareToPlay(sr,512);
        const auto r=render(*p,sine(sr,4.5,330,0.3f,1.5),sr,512,[&](std::size_t position) { if (position>=at(sr,1.2)) set(*p,veil::Freeze,1); });
        const double during=meanSquare(r.out,at(sr,0.8),at(sr,1.2)), after=meanSquare(r.out,at(sr,3.0),at(sr,4.0));
        s.metric("Freeze level 1.5-2.5 s after input stops",toDb(after/during)," dB");
        s.check(toDb(after/during)>-12,"Freeze: frozen spectrum keeps sounding after the input stops");
    }
}

//==============================================================================
// Each control's full range must make a measurable difference.
void rangeTests(Suite& s) {
    s.section("every control: minimum vs maximum makes a clear difference");
    const auto input=pianoPhrase(sr,2.6);
    for (std::size_t i=0;i<veil::Count;++i) {
        const auto id=static_cast<P>(i);
        if (id==veil::Bypass || id>=veil::RitualOn) continue;   // Ritual is off by default and replaced in Milestone 3
        auto renderAt=[&](float value) {
            auto p=makeProcessor({{id,value}});
            if (id==veil::TombNote || id==veil::TombOctave || id==veil::TombRing) set(*p,veil::Tomb,.7f);
            if (id==veil::RiseTime) set(*p,veil::CryptMode,1);
            if (id==veil::FilterRate) set(*p,veil::FilterMovement,.8f);
            if (id==veil::DelayTime || id==veil::DelaySync) set(*p,veil::DelayMix,.8f);
            if (id==veil::AutoLevel) set(*p,veil::InputGain,-12);
            p->prepareToPlay(sr,512);
            return render(*p,input,sr).out;
        };
        const auto low=renderAt(veil::specs[i].min), high=renderAt(veil::specs[i].max);
        const double difference=differenceDb(high,low);
        s.check(difference>-25,std::string("control changes the sound across its range: ")+veil::specs[i].name+" ("+std::to_string(difference)+" dB)");
    }
}

//==============================================================================
void dreadTests(Suite& s) {
    s.section("DREAD macro");
    auto frame=veil::defaults();
    auto copy=frame; veil::applyDread(copy,.5f);
    s.check(frameEqual(frame,copy),"DREAD 50 % leaves every setting exactly as set");
    const auto input=sawChord(sr,8,4,0.6f);
    auto measure=[&](float dread,double& centroidOut,double& tail,double& level) {
        auto p=makeProcessor({{veil::Dread,dread}});
        const auto r=render(*p,input,sr);
        centroidOut=centroid(r.out,sr,at(sr,1.5),at(sr,3.8));
        tail=meanSquare(r.out,at(sr,5),at(sr,6))/meanSquare(r.out,at(sr,2),at(sr,3.8));
        level=toDb(meanSquare(r.out,at(sr,2),at(sr,3.8))/meanSquare(r.in,at(sr,2),at(sr,3.8)));
        s.check(peak(r.out)<=0.97f && finite(r.out),"DREAD "+std::to_string(dread)+": no clipping");
    };
    double c0,t0,l0,c1,t1,l1;
    measure(0,c0,t0,l0); measure(1,c1,t1,l1);
    s.metric("DREAD 0 centroid",c0," Hz"); s.metric("DREAD 100 centroid",c1," Hz");
    s.metric("DREAD 0 tail",toDb(t0)," dB"); s.metric("DREAD 100 tail",toDb(t1)," dB");
    s.check(c1<c0*0.5,"DREAD 100 % is at least twice as dark as 0 %");
    s.check(toDb(t1)>toDb(t0)+8,"DREAD 100 % has a much longer, louder tail than 0 %");
    s.check(std::abs(l0)<=3 && std::abs(l1)<=3,"DREAD extremes keep the level within 3 dB");
}

//==============================================================================
void gainStagingTests(Suite& s) {
    s.section("gain staging: level close to input under heavy processing, never clipping");
    veil::dsp::Random random; random.seed(77);
    const auto input=sawChord(sr,5,4,0.85f);
    double worst=0;
    for (int trial=0;trial<24;++trial) {
        auto p=makeProcessor();
        for (std::size_t i=0;i<veil::Count;++i) {
            const auto id=static_cast<P>(i);
            if (id==veil::Bypass || id==veil::InputGain || id==veil::OutputGain || id==veil::GlobalMix || id==veil::AutoLevel || id==veil::Freeze || id==veil::Eternal || id>=veil::RitualOn) continue;
            const auto& spec=veil::specs[i];
            set(*p,id,spec.min+random.unit()*(spec.max-spec.min));
        }
        if (trial<6) { set(*p,veil::Drive,36); set(*p,veil::Feedback,.98f); set(*p,veil::Resonance,1); set(*p,veil::Dread,1); }
        p->prepareToPlay(sr,512);
        const auto r=render(*p,input,sr);
        const double change=toDb(meanSquare(r.out,at(sr,2),at(sr,3.9))/meanSquare(r.in,at(sr,2),at(sr,3.9)));
        worst=std::max(worst,std::abs(change));
        s.check(peak(r.out)<=0.97f && finite(r.out),"random/heavy settings "+std::to_string(trial)+": no clipping");
        s.check(std::abs(change)<=3.5,"random/heavy settings "+std::to_string(trial)+": level within 3.5 dB ("+std::to_string(change)+" dB)");
        if (std::abs(change)>3.5) { std::cout<<"         settings:"; const auto v=p->currentValues(); for (std::size_t i=0;i<veil::RitualOn;++i) std::cout<<' '<<veil::specs[i].id<<'='<<v[i]; std::cout<<"\n         peak in "<<peak(r.in)<<" out "<<peak(r.out)<<"\n"; }
    }
    s.metric("largest level change across random settings",worst," dB");
}

//==============================================================================
void presetTests(Suite& s) {
    s.section("factory presets: dramatic, distinct, level-matched, no clipping");
    const auto input=sawChord(sr,7,4,0.6f);
    std::vector<std::vector<double>> fingerprints;
    VeilAudioProcessor names;
    for (int preset=0;preset<16;++preset) {
        auto p=makeProcessor(); p->loadFactoryPreset(preset); p->prepareToPlay(sr,512);
        const auto r=render(*p,input,sr);
        const double change=toDb(meanSquare(r.out,at(sr,2),at(sr,3.9))/meanSquare(r.in,at(sr,2),at(sr,3.9)));
        const std::string name=names.factoryPresetName(preset).toStdString();
        s.check(finite(r.out) && peak(r.out)<=0.97f,"preset "+name+": finite, no clipping");
        s.check(std::abs(change)<=3,"preset "+name+": level within 3 dB ("+std::to_string(change)+" dB)");
        if (preset>0) s.check(residualDb(r.out,r.in,r.latency,at(sr,2),at(sr,3.9))>-3,"preset "+name+": clearly transformed");
        auto f=bandLevels(r.out,sr,at(sr,2),at(sr,3.9));
        const double sustained=meanSquare(r.out,at(sr,2),at(sr,3.9));
        f.push_back(3.0*toDb(meanSquare(r.out,at(sr,4.6),at(sr,5.4))/sustained));   // tail weight
        f.push_back(3.0*toDb(meanSquare(r.out,at(sr,5.8),at(sr,6.8))/sustained));
        fingerprints.push_back(f);
    }
    double closest=1e9; int a=0,b=0;
    for (std::size_t i=0;i<fingerprints.size();++i) for (std::size_t j=i+1;j<fingerprints.size();++j) {
        // Compare spectral shape (mean-removed) and tails.
        const auto& x=fingerprints[i]; const auto& y=fingerprints[j];
        const std::size_t bands=x.size()-2;
        double meanDiff=0; for (std::size_t k=0;k<bands;++k) meanDiff+=(x[k]-y[k])/bands;
        double d=0; for (std::size_t k=0;k<bands;++k) d+=std::pow(x[k]-y[k]-meanDiff,2);
        d=std::sqrt(d/bands)+std::abs(x[bands]-y[bands])/3+std::abs(x[bands+1]-y[bands+1])/3;
        if (d<closest) { closest=d; a=static_cast<int>(i); b=static_cast<int>(j); }
    }
    s.metric("closest pair of presets (distance)",closest);
    std::cout<<"         between "<<names.factoryPresetName(a)<<" and "<<names.factoryPresetName(b)<<"\n";
    s.check(closest>2.0,"no two factory presets are nearly identical");
}

//==============================================================================
void stabilityTests(Suite& s) {
    s.section("rates, block sizes, silence, extremes, fuzzing, allocation, CPU");
    for (double rate:{44100.0,48000.0,88200.0,96000.0,192000.0}) {
        for (int block:{1,32,64,512,4096,0}) {
            if (rate>100000 && block==1) continue;   // covered at lower rates; keeps the run short
            auto p=makeProcessor({},rate,block>0 ? block : 512);
            const auto r=render(*p,sawChord(rate,1.2,1.0,0.5f),rate,block);
            const bool okay=finite(r.out) && peak(r.out)<=0.97f && meanSquare(r.out,at(rate,0.6),at(rate,1.0))>1e-5;
            s.check(okay,"audible and finite at "+std::to_string(static_cast<int>(rate))+" Hz, block "+(block>0 ? std::to_string(block) : std::string("varying")));
        }
    }
    {
        auto p=makeProcessor();
        const auto r=render(*p,silence(sr,2),sr);
        s.check(peak(r.out)==0.0f,"silence in, silence out at defaults (fresh instance)");
    }
    {
        auto p=makeProcessor();
        for (std::size_t i=0;i<veil::Count;++i) if (static_cast<P>(i)!=veil::Bypass && static_cast<P>(i)<veil::RitualOn) set(*p,static_cast<P>(i),veil::specs[i].max);
        p->prepareToPlay(sr,512);
        const auto r=render(*p,sawChord(sr,6,3,0.9f),sr);
        s.check(finite(r.out) && peak(r.out)<=0.97f,"every control at maximum: finite and bounded");
    }
    {
        veil::dsp::Random random; random.seed(4242);
        auto p=makeProcessor();
        const auto input=pianoPhrase(sr,40);
        const auto r=render(*p,input,sr,256,[&](std::size_t) {
            const auto id=static_cast<P>(static_cast<std::size_t>(random.nextInt()%veil::RitualOn));
            if (id==veil::Bypass) return;
            set(*p,id,veil::specs[id].min+random.unit()*(veil::specs[id].max-veil::specs[id].min));
        });
        s.check(finite(r.out) && peak(r.out)<=0.97f,"7500 blocks of random parameter changes: finite and bounded");
    }
    for (int scenario=0;scenario<2;++scenario) {
        auto p=makeProcessor(scenario==0 ? Settings{} : Settings{{veil::Descent,3},{veil::Reverse,1},{veil::CryptMode,1},{veil::Tomb,.8f},{veil::Drone,1},{veil::Freeze,1},{veil::Dust,.7f},{veil::Eternal,1}});
        juce::AudioBuffer<float> b(2,512); juce::MidiBuffer midi;
        for (int i=0;i<40;++i) { for (int n=0;n<512;++n) b.setSample(0,n,.3f*std::sin(.03f*static_cast<float>(i*512+n))), b.setSample(1,n,b.getSample(0,n)); p->processBlock(b,midi); }
        allocationProbe::calls=0; allocationProbe::enabled=true;
        for (int i=0;i<40;++i) { for (int n=0;n<512;++n) b.setSample(0,n,.3f*std::sin(.03f*static_cast<float>(i*512+n))), b.setSample(1,n,b.getSample(0,n)); p->processBlock(b,midi); }
        allocationProbe::enabled=false;
        s.check(allocationProbe::calls==0,std::string("processBlock performs no heap allocations (")+(scenario==0 ? "defaults" : "every feature on")+")");
    }
    for (double rate:{48000.0,96000.0}) {
        auto p=makeProcessor({},rate,512);
        const auto input=sawChord(rate,6,5,0.5f);
        const auto start=juce::Time::getHighResolutionTicks();
        render(*p,input,rate);
        const double seconds=juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks()-start);
        s.metric("CPU at "+std::to_string(static_cast<int>(rate))+" Hz (fraction of real time, one core, this machine)",seconds/6.0*100,"%");
    }
    for (int cycle=0;cycle<4;++cycle) { VeilAudioProcessor temp; temp.prepareToPlay(48000,64); temp.releaseResources(); temp.prepareToPlay(96000,1024); }
    s.check(true,"repeated construction, preparation and destruction complete");
}

//==============================================================================
void ritualTests(Suite& s) {
    s.section("Ritual engine (retained until Milestone 3, off by default)");
    auto weights=veil::RitualEngine::weights(0,1);
    s.check(near(weights.shadow,1) && near(weights.spirit,0) && near(weights.ruin,0),"Shadow vertex");
    weights=veil::RitualEngine::weights(.5f,0);
    s.check(near(weights.spirit,1),"Spirit vertex");
    veil::RitualEngine ritual; ritual.prepare(48000);
    auto p=veil::defaults(); p[veil::RitualOn]=0;
    s.check(frameEqual(ritual.process(p,120),p),"Ritual off preserves manual parameters");
    s.check(veil::defaults()[veil::RitualOn]==0,"Ritual is off by default");
    p[veil::RitualOn]=1; p[veil::MotionOn]=1; p[veil::MotionDepth]=1; p[veil::MotionRate]=1;
    bool valuesValid=true; float lastX=.5f,maxJump=0;
    for (int n=0;n<1500;++n) {
        const auto actual=ritual.processBlock(p,120,64);
        if (n>0) maxJump=std::max(maxJump,std::abs(ritual.x()-lastX));
        lastX=ritual.x();
        for (std::size_t i=0;i<veil::Count;++i) valuesValid &= std::isfinite(actual[i]) && actual[i]>=veil::specs[i].min && actual[i]<=veil::specs[i].max;
    }
    s.check(valuesValid && maxJump<.05f,"block-rate motion stays bounded and continuous");
}

void editorLifecycleTests(Suite& s) {
    s.section("editor construction, resize, destruction, with audio running");
    auto processor=makeProcessor();
    for (int cycle=0;cycle<2;++cycle) {
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor->createEditor());
        s.check(editor!=nullptr && editor->getWidth()>0 && editor->getHeight()>0,"editor constructs with nonempty bounds");
        for (const auto size:{std::pair{1280,860},std::pair{1040,760},std::pair{1800,1200}}) {
            editor->setSize(size.first,size.second);
            processor->loadFactoryPreset((cycle+size.first)%processor->factoryPresetCount());
            render(*processor,sawChord(48000,0.1,0.1,0.3f),48000);
            s.check(editor->getWidth()==size.first && editor->getHeight()==size.second,"editor resizes to "+std::to_string(size.first)+" x "+std::to_string(size.second));
        }
    }
}

int renderUI(const juce::String& path) {
    juce::ScopedJuceInitialiser_GUI gui;
    VeilAudioProcessor p; p.prepareToPlay(48000,256); p.loadFactoryPreset(1);
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
    if (argc>1 && juce::String(argv[1])=="--probe") return probe(argc,argv);
    if (argc>1 && juce::String(argv[1])=="--render-demos") return renderDemos(argc>2 ? juce::String(argv[2]) : "VEIL-audio-demos");
    juce::ScopedJuceInitialiser_GUI juceInitializer;
    const juce::String only=argc>2 && juce::String(argv[1])=="--only" ? juce::String(argv[2]) : juce::String();
    // --gating: correctness groups that must pass (state, transparency, stability, ritual, editor).
    // --sound-targets: the Milestone 2 sound targets, still being tuned (see docs/DEVLOG.md).
    const juce::String mode=argc>1 ? juce::String(argv[1]) : juce::String();
    const std::set<std::string> gating{"state","transparency","stability","ritual","editor"};
    Suite suite;
    const std::pair<const char*,void(*)(Suite&)> groups[]={
        {"state",parameterAndStateTests},{"transparency",transparencyTests},{"defaults",defaultSoundTests},
        {"modules",moduleAloneTests},{"features",featureTests},{"dread",dreadTests},{"gain",gainStagingTests},
        {"presets",presetTests},{"ranges",rangeTests},{"stability",stabilityTests},{"ritual",ritualTests},{"editor",editorLifecycleTests}};
    try {
        for (const auto& [name,run]:groups) {
            const bool isGating=gating.count(name)>0;
            if (mode=="--gating" && !isGating) continue;
            if (mode=="--sound-targets" && isGating) continue;
            if (only.isEmpty() || only==name) run(suite);
        }
    } catch (const std::exception& e) {
        allocationProbe::enabled=false;
        suite.check(false,std::string("unexpected exception: ")+e.what());
    }
    std::cout<<"VEIL regression: "<<suite.passed<<" passed, "<<suite.failed<<" failed\n";
    return suite.failed==0 ? 0 : 1;
}
