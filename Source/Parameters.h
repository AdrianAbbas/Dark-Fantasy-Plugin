#pragma once
#include <array>
#include <cstddef>
namespace veil {
enum P : std::size_t {
 InputGain, OutputGain, GlobalMix, Bypass,
 AbyssOn, Cutoff, Resonance, HighPass, Slope, Darkness, FilterMovement, FilterRate, FilterSync,
 CryptOn, Space, Decay, PreDelay, Damping, ReverbLowCut, Shimmer, ReverbWidth, ReverbMix, Character,
 WraithOn, Blur, WraithDetune, Drift, Ghost, Freeze, WraithMix,
 BloodOn, Drive, Texture, Age, Dust, Instability, BloodMix,
 EchoesOn, DelayTime, DelaySync, Feedback, DelayDamping, Diffusion, Reverse, Spread, DelayMix,
 ChoirOn, Voices, Interval, ChoirDetune, Formant, ChoirMovement, ChoirBlend,
 RitualOn, RitualX, RitualY, MotionOn, MotionRate, MotionDepth, MotionPattern, MotionSync,
 Count
};
using Frame = std::array<float, Count>;
struct ParamSpec { const char* id; const char* name; float min, max, def, skew; const char* unit; const char* choices; };
inline constexpr std::array<ParamSpec, Count> specs {{
 {"inputGain","Input",-24,24,0,1,"dB",""}, {"outputGain","Output",-24,12,-3,1,"dB",""}, {"globalMix","Wet / Dry",0,1,.65f,1,"",""}, {"bypass","Bypass",0,1,0,1,"","Off|On"},
 {"abyssOn","The Abyss",0,1,1,1,"","Off|On"}, {"cutoff","Cutoff",80,18000,6500,.3f,"Hz",""}, {"resonance","Resonance",.1f,1,.25f,1,"",""}, {"highPass","High-Pass",20,800,35,.4f,"Hz",""}, {"slope","Slope",0,1,1,1,"","12 dB|24 dB"}, {"darkness","Darkness",0,1,.3f,1,"",""}, {"filterMovement","Movement",0,1,.12f,1,"",""}, {"filterRate","Movement Rate",.02f,4,.15f,.5f,"Hz",""}, {"filterSync","Movement Sync",0,1,0,1,"","Free|4 beats"},
 {"cryptOn","The Crypt",0,1,1,1,"","Off|On"}, {"space","Space",0,1,.65f,1,"",""}, {"decay","Decay",.5f,20,5,.5f,"s",""}, {"preDelay","Pre-Delay",0,200,35,1,"ms",""}, {"damping","Damping",0,1,.65f,1,"",""}, {"reverbLowCut","Low Cut",30,1000,180,.5f,"Hz",""}, {"shimmer","Shimmer",0,1,.08f,1,"",""}, {"reverbWidth","Width",0,1,.9f,1,"",""}, {"reverbMix","Crypt Mix",0,1,.32f,1,"",""}, {"character","Chamber",0,3,1,1,"","Forgotten Chapel|Ancient Cathedral|Underground Crypt|Endless Void"},
 {"wraithOn","The Wraith",0,1,1,1,"","Off|On"}, {"blur","Spectral Blur",0,1,.2f,1,"",""}, {"wraithDetune","Detune",0,30,4,1,"ct",""}, {"drift","Drift",0,1,.2f,1,"",""}, {"ghost","Ghost",0,1,.2f,1,"",""}, {"freeze","Freeze",0,1,0,1,"","Off|On"}, {"wraithMix","Wraith Mix",0,1,.16f,1,"",""},
 {"bloodOn","Ancient Blood",0,1,1,1,"","Off|On"}, {"drive","Drive",0,24,3,1,"dB",""}, {"texture","Texture",0,1,.18f,1,"",""}, {"age","Age",0,1,.12f,1,"",""}, {"dust","Dust",0,1,0,1,"",""}, {"instability","Instability",0,1,.08f,1,"",""}, {"bloodMix","Blood Mix",0,1,.28f,1,"",""},
 {"echoesOn","The Echoes",0,1,1,1,"","Off|On"}, {"delayTime","Time",20,2000,440,.5f,"ms",""}, {"delaySync","Time Sync",0,4,0,1,"","Free|1/8|1/4|Dotted 1/4|1/2"}, {"feedback","Feedback",0,.9f,.38f,1,"",""}, {"delayDamping","Damping",0,1,.6f,1,"",""}, {"diffusion","Diffusion",0,1,.25f,1,"",""}, {"reverse","Reverse",0,1,0,1,"","Off|On"}, {"spread","Stereo Spread",0,1,.6f,1,"",""}, {"delayMix","Echoes Mix",0,1,.2f,1,"",""},
 {"choirOn","The Choir",0,1,1,1,"","Off|On"}, {"voices","Voice Count",1,4,3,1,"",""}, {"interval","Interval",0,4,2,1,"","Octave below|Fifth below|Fifth above|Octave above|Unison"}, {"choirDetune","Detune",0,30,7,1,"ct",""}, {"formant","Formant Character",0,1,.45f,1,"",""}, {"choirMovement","Movement",0,1,.25f,1,"",""}, {"choirBlend","Choir Blend",0,1,.13f,1,"",""},
 {"ritualOn","Ritual Engine",0,1,1,1,"","Off|On"}, {"ritualX","Ritual X",0,1,.5f,1,"",""}, {"ritualY","Ritual Y",0,1,.5f,1,"",""}, {"motionOn","Ritual Motion",0,1,0,1,"","Off|On"}, {"motionRate","Motion Rate",.01f,1,.04f,.5f,"Hz",""}, {"motionDepth","Motion Depth",0,1,.4f,1,"",""}, {"motionPattern","Motion Pattern",0,2,0,1,"","Circle|Pendulum|Wander"}, {"motionSync","Motion Sync",0,1,0,1,"","Free|16 beats"}
}};
inline Frame defaults() { Frame f{}; for (std::size_t i=0;i<Count;++i) f[i]=specs[i].def; return f; }
}
