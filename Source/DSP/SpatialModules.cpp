#include "SpatialModules.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace veil {
namespace {
constexpr double pi=std::numbers::pi;
float safe(float v) noexcept { return std::isfinite(v) ? v : 0.0f; }
float pole(double hz, double sr) noexcept { return static_cast<float>(1.0-std::exp(-2.0*pi*hz/sr)); }
float mix(float a,float b,float m) noexcept { return a+(b-a)*std::clamp(m,0.0f,1.0f); }
// Equal-power blend for wet layers that are uncorrelated with the dry signal
// (reverb, granular, pitch-shifted). A linear crossfade loses up to 3 dB there.
// Exact at m = 0 (dry) and m = 1 (wet).
float blend(float dry,float wet,float m) noexcept {
    m=std::clamp(m,0.0f,1.0f);
    return dry*std::sqrt(1.0f-m)+wet*std::sqrt(m);
}
float bounded(float x) noexcept { return x / (1.0f + std::abs(x)*.02f); }
}
namespace spatial {
void StereoRing::prepare(std::size_t length) {
    for(auto& channel:data) channel.assign(std::max<std::size_t>(length,8),0.0f);
    position=0; seamPosition=0; seamStrength=0;
}
void StereoRing::clear() noexcept {
    for(auto& channel:data) std::fill(channel.begin(),channel.end(),0.0f);
    position=0; seamPosition=0; seamStrength=0;
}
void StereoRing::write(float l,float r) noexcept {
    data[0][position]=safe(l); data[1][position]=safe(r);
}
void StereoRing::advance() noexcept {
    if(++position==data[0].size()) position=0;
}
float StereoRing::read(int channel,double delay) const noexcept {
    const auto size=data[0].size();
    if(size<2) return 0;
    double location=static_cast<double>(position)-delay;
    // Taps never exceed the allocated length, so one wrap normally suffices.
    while(location<0) location+=static_cast<double>(size);
    while(location>=static_cast<double>(size)) location-=static_cast<double>(size);
    const auto a=static_cast<std::size_t>(location);
    const auto b=(a+1==size)?0:a+1;
    const auto fraction=static_cast<float>(location-static_cast<double>(a));
    float result=data[static_cast<std::size_t>(channel)][a]*(1-fraction)+data[static_cast<std::size_t>(channel)][b]*fraction;
    if(seamStrength>0.00001f) {
        double distance=std::abs(location-static_cast<double>(seamPosition));
        distance=std::min(distance,static_cast<double>(size)-distance);
        const float splice=static_cast<float>(std::clamp(distance/seamLength,0.0,1.0));
        // A 3 ms soft splice removes the retained capture's discontinuity.
        // Staggered grain heads keep this from gating the whole frozen layer.
        result*=1-seamStrength*(1-splice);
    }
    return result;
}
std::array<float,2> PitchTap::read(const StereoRing& ring,double ratio,double window,double minimumDelay) noexcept {
    window=std::max(window,32.0);
    phase+=(1.0-ratio)/window;
    phase-=std::floor(phase);
    const double other=phase<.5?phase+.5:phase-.5;
    // Complementary Hann windows. Both heads run at the requested pitch ratio.
    const auto weight=static_cast<float>(.5-.5*std::cos(2*pi*phase));
    const double d0=minimumDelay+phase*window, d1=minimumDelay+other*window;
    return {{ring.read(0,d0)*weight+ring.read(0,d1)*(1-weight),
             ring.read(1,d0)*weight+ring.read(1,d1)*(1-weight)}};
}
void Resonator::set(double sampleRate,double frequency,double q) noexcept {
    const double omega=2*pi*std::clamp(frequency,50.0,sampleRate*.4)/sampleRate;
    const double alpha=std::sin(omega)/(2*q), a0=1+alpha;
    b0=static_cast<float>(alpha/a0); b1=0; b2=-b0;
    a1=static_cast<float>(-2*std::cos(omega)/a0); a2=static_cast<float>((1-alpha)/a0);
}
float Resonator::process(float x,int channel) noexcept {
    const auto c=static_cast<std::size_t>(channel);
    const float y=b0*x+z1[c];
    z1[c]=b1*x-a1*y+z2[c]; z2[c]=b2*x-a2*y;
    return y;
}
}

void Crypt::prepare(double sampleRate) {
    sr=std::clamp(sampleRate,8000.0,384000.0);
    preDelay.prepare(static_cast<std::size_t>(sr*.21)+8);
    shimmerBuffer.prepare(static_cast<std::size_t>(sr*.16)+8);
    shimmerCoefficient=pole(std::min(7000.0,sr*.19),sr);
    for(auto& line:delayLines) line.assign(static_cast<std::size_t>(sr*.46)+8,0);
    reset();
}
void Crypt::reset() {
    preDelay.clear(); shimmerBuffer.clear(); shimmerTap.reset();
    for(auto& line:delayLines) std::fill(line.begin(),line.end(),0);
    positions.fill(0); lowpass.fill(0); highpassState.fill(0); shimmerLP.fill(0);
    feedback.fill(.8f); delaySamples.fill(static_cast<float>(sr*.1)); targetDelays=delaySamples;
    controlCounter=0; predelaySamples=0;
}
void Crypt::process(float& l,float& r,const Frame& p,float) noexcept {
    if(delayLines[0].empty()) return;
    if(controlCounter--<=0) {
        controlCounter=31;
        constexpr std::array<double,8> times{{.0311,.0379,.0437,.0533,.0613,.0719,.0833,.0977}};
        constexpr std::array<double,4> size{{.75,1.65,.93,2.25}};
        constexpr std::array<float,4> dark{{.85f,1.0f,.55f,1.15f}};
        const auto character=static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(p[Character])),0,3));
        characterDark=dark[character];
        const double scale=size[character]*(.5+1.25*p[Space]);
        for(std::size_t i=0;i<8;++i) {
            targetDelays[i]=static_cast<float>(sr*times[i]*scale);
            feedback[i]=static_cast<float>(std::pow(10.0,-3.0*targetDelays[i]/(sr*std::max(.5f,p[Decay]))));
            feedback[i]=std::min(feedback[i],.9985f);
        }
        const auto dampHz=std::clamp((15000.0-14200.0*p[Damping])*characterDark,350.0,sr*.38);
        dampingCoefficient=pole(dampHz,sr);
        highpassCoefficient=pole(p[ReverbLowCut],sr);
        shimmerAmount=p[Shimmer]*.4f; width=p[ReverbWidth];
    }
    const float dryL=l,dryR=r;
    highpassState[0]+=highpassCoefficient*(l-highpassState[0]);
    highpassState[1]+=highpassCoefficient*(r-highpassState[1]);
    float inL=l-highpassState[0], inR=r-highpassState[1];
    shimmerLP[0]+=shimmerCoefficient*(inL-shimmerLP[0]);
    shimmerLP[1]+=shimmerCoefficient*(inR-shimmerLP[1]);
    shimmerBuffer.write(shimmerLP[0],shimmerLP[1]);
    const auto octave=shimmerTap.read(shimmerBuffer,2.0,sr*.08,sr*.006);
    shimmerBuffer.advance();
    // Feed-forward octave generation leaves the FDN's feedback norm below unity.
    inL=(inL+octave[0]*shimmerAmount)/(1+shimmerAmount*.3f);
    inR=(inR+octave[1]*shimmerAmount)/(1+shimmerAmount*.3f);
    preDelay.write(inL,inR);
    const float targetPre=static_cast<float>(sr*.001*p[PreDelay]);
    predelaySamples+=.0004f*(targetPre-predelaySamples);
    inL=preDelay.read(0,predelaySamples); inR=preDelay.read(1,predelaySamples);
    preDelay.advance();
    std::array<float,8> output{};
    float sum=0;
    for(std::size_t i=0;i<8;++i) {
        delaySamples[i]+=.0003f*(targetDelays[i]-delaySamples[i]);
        const auto n=delayLines[i].size();
        double pos=static_cast<double>(positions[i])-delaySamples[i];
        if(pos<0) pos+=static_cast<double>(n);
        const auto a=static_cast<std::size_t>(pos), b=(a+1)%n;
        const auto fraction=static_cast<float>(pos-static_cast<double>(a));
        const float tap=delayLines[i][a]*(1-fraction)+delayLines[i][b]*fraction;
        lowpass[i]+=dampingCoefficient*(tap-lowpass[i]);
        output[i]=lowpass[i]; sum+=output[i];
    }
    // Householder reflection (2/N * sum - x) is an orthogonal scattering matrix.
    // Per-line T60 gains and the damping filters are all contractive.
    for(std::size_t i=0;i<8;++i) {
        const float injection=((i&1)?inR:inL)*((i&2)?-.27f:.27f);
        const float value=injection+(.25f*sum-output[i])*feedback[i];
        delayLines[i][positions[i]]=safe(bounded(value));
        if(++positions[i]==delayLines[i].size()) positions[i]=0;
    }
    float wetL=(output[0]+output[2]-output[4]+output[6])*.45f;
    float wetR=(output[1]-output[3]+output[5]+output[7])*.45f;
    const float mid=(wetL+wetR)*.5f, side=(wetL-wetR)*.5f*width;
    wetL=mid+side; wetR=mid-side;
    const float amount=p[CryptOn]*p[ReverbMix];
    l=blend(dryL,wetL,amount); r=blend(dryR,wetR,amount);
}

void Wraith::prepare(double sampleRate) {
    sr=std::clamp(sampleRate,8000.0,384000.0);
    capture.prepare(static_cast<std::size_t>(sr*1.25)+8);
    reset();
}
void Wraith::reset() {
    capture.clear(); mainTap.reset(.1); ghostTap.reset(.6);
    blur1.fill(0); blur2.fill(0); wetSmooth.fill(0);
    modulationPhase=0; controlCounter=0; window=targetWindow=sr*.12;
    held=false; capture.seamLength=sr*.003;
}
void Wraith::process(float& l,float& r,const Frame& p,float) noexcept {
    if(capture.data[0].empty()) return;
    const float dryL=l,dryR=r;
    if(controlCounter--<=0) {
        controlCounter=31;
        blurCoefficient=pole(10000.0-9100.0*p[Blur],sr);
        wetCoefficient=pole(12000.0-10600.0*p[Blur],sr);
        const double movement=std::sin(modulationPhase)*p[Drift];
        mainRatio=std::pow(2.0,(p[WraithDetune]*(.2+.8*movement))/1200.0);
        ghostRatio=std::pow(2.0,(7.0+movement*.18)/12.0);
        targetWindow=sr*(.055+.17*p[Blur]);
    }
    modulationPhase+=2*pi*(.035+.22*p[Drift])/sr;
    if(modulationPhase>2*pi) modulationPhase-=2*pi;
    blur1[0]+=blurCoefficient*(l-blur1[0]); blur1[1]+=blurCoefficient*(r-blur1[1]);
    blur2[0]+=blurCoefficient*(blur1[0]-blur2[0]); blur2[1]+=blurCoefficient*(blur1[1]-blur2[1]);
    // A 1.25-second processed capture is retained during freeze; the virtual
    // record head still advances so playback traverses the entire frozen texture.
    const bool freezeRequested=p[Freeze]>=.5f;
    if(freezeRequested&&!held) capture.seamPosition=capture.position;
    held=freezeRequested;
    capture.seamStrength+=.002f*((held?1.0f:0.0f)-capture.seamStrength);
    if(!held) capture.write(blur2[0],blur2[1]);
    window+=.0001*(targetWindow-window);
    const auto main=mainTap.read(capture,mainRatio,window,sr*.035);
    const auto ghost=ghostTap.read(capture,ghostRatio,window*.83,sr*(.16+.27*p[Ghost]));
    capture.advance();
    const float ghostLevel=p[Ghost]*.6f;
    const float wetL=(main[0]+ghost[1]*ghostLevel)/(1+ghostLevel*.5f);
    const float wetR=(main[1]+ghost[0]*ghostLevel)/(1+ghostLevel*.5f);
    wetSmooth[0]+=wetCoefficient*(wetL-wetSmooth[0]);
    wetSmooth[1]+=wetCoefficient*(wetR-wetSmooth[1]);
    const float amount=p[WraithOn]*p[WraithMix];
    l=blend(dryL,wetSmooth[0],amount); r=blend(dryR,wetSmooth[1],amount);
}

void Choir::prepare(double sampleRate) {
    sr=std::clamp(sampleRate,8000.0,384000.0);
    buffer.prepare(static_cast<std::size_t>(sr*.2)+8);
    reset();
}
void Choir::reset() {
    buffer.clear();
    for(std::size_t i=0;i<4;++i) taps[i].reset(static_cast<double>(i)*.217);
    inputLP.fill(0); vowel1.reset(); vowel2.reset();
    ratios.fill(1); voiceGains={{1,1,1,0}}; modulationPhase=0; controlCounter=0;
}
void Choir::process(float& l,float& r,const Frame& p,float) noexcept {
    if(buffer.data[0].empty()) return;
    if(controlCounter--<=0) {
        controlCounter=31;
        constexpr std::array<double,5> semitones{{-12,-7,7,12,0}};
        const auto interval=static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(p[Interval])),0,4));
        constexpr std::array<double,4> detunes{{-1,.37,1,-.43}};
        double largest=1;
        for(std::size_t i=0;i<4;++i) {
            const double drift=std::sin(modulationPhase+static_cast<double>(i)*1.63)*p[ChoirMovement]*5;
            const double cents=detunes[i]*p[ChoirDetune]+drift;
            ratios[i]=std::pow(2.0,semitones[interval]/12.0+cents/1200.0);
            largest=std::max(largest,ratios[i]);
        }
        inputCoefficient=pole(std::min(9500.0,sr*.42/largest),sr);
        formantAmount=p[Formant];
        vowel1.set(sr,380.0+formantAmount*570.0,2.2);
        vowel2.set(sr,1000.0+formantAmount*1700.0,3.2);
    }
    modulationPhase+=2*pi*(.07+.23*p[ChoirMovement])/sr;
    if(modulationPhase>2*pi) modulationPhase-=2*pi;
    const float dryL=l,dryR=r;
    inputLP[0]+=inputCoefficient*(l-inputLP[0]); inputLP[1]+=inputCoefficient*(r-inputLP[1]);
    buffer.write(inputLP[0],inputLP[1]);
    float wetL=0,wetR=0,total=0;
    const float requested=std::clamp(p[Voices],1.0f,4.0f);
    for(std::size_t i=0;i<4;++i) {
        const float target=std::clamp(requested-static_cast<float>(i),0.0f,1.0f);
        voiceGains[i]+=.001f*(target-voiceGains[i]);
        const auto voice=taps[i].read(buffer,ratios[i],sr*(.065+.011*static_cast<double>(i)),sr*.009);
        const float pan=(static_cast<float>(i)/3.0f-.5f)*.65f;
        wetL+=voice[0]*voiceGains[i]*(1-pan); wetR+=voice[1]*voiceGains[i]*(1+pan);
        total+=voiceGains[i];
    }
    buffer.advance();
    wetL/=std::max(1.0f,total); wetR/=std::max(1.0f,total);
    // Broad stable vocal-band resonators colour real pitch-shifted voices;
    // the formant control is timbral, not a promise of vocal resynthesis.
    const float vowelL=vowel1.process(wetL,0)*.7f+vowel2.process(wetL,0)*.5f;
    const float vowelR=vowel1.process(wetR,1)*.7f+vowel2.process(wetR,1)*.5f;
    wetL=mix(wetL,vowelL,.35f+formantAmount*.5f);
    wetR=mix(wetR,vowelR,.35f+formantAmount*.5f);
    const float amount=p[ChoirOn]*p[ChoirBlend];
    l=blend(dryL,wetL,amount); r=blend(dryR,wetR,amount);
}
}
