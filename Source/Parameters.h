#pragma once
#include <array>
#include <cstddef>
namespace veil {
// Stable parameter IDs. Order here is the index order used everywhere (Frame).
enum P : std::size_t {
 InputGain, OutputGain, GlobalMix, Bypass, Dread, AutoLevel,
 AbyssOn, Cutoff, Resonance, HighPass, Slope, Darkness, FilterMovement, FilterRate, FilterSync,
 Tomb, TombNote, TombOctave, TombRing,
 BloodOn, Drive, Texture, Age, Dust, Instability, BloodMix,
 ChoirOn, Voices, Interval, ChoirDetune, Formant, ChoirMovement, ChoirBlend, Drone, DroneHold,
 WraithOn, Blur, WraithDetune, Drift, Ghost, Freeze, WraithMix,
 EchoesOn, DelayTime, DelaySync, Feedback, DelayDamping, Diffusion, Reverse, Descent, Spread, DelayMix,
 CryptOn, Space, Decay, PreDelay, Damping, ReverbLowCut, Shimmer, ReverbWidth, ReverbMix, Character,
 CryptMode, RiseTime, Eternal,
 RitualOn, RitualX, RitualY, MotionOn, MotionRate, MotionDepth, MotionPattern, MotionSync,
 Count
};
using Frame = std::array<float, Count>;
struct ParamSpec { const char* id; const char* name; float min, max, def, skew; const char* unit; const char* choices; };
// Defaults transform a bright synth obviously: darker, a large space, an audible
// ghost layer, at 100 % wet. Ranges run from subtle to dramatic.
inline constexpr std::array<ParamSpec, Count> specs {{
 {"inputGain","Input",-24,24,0,1,"dB",""}, {"outputGain","Output",-24,12,0,1,"dB",""},
 {"globalMix","Wet / Dry",0,1,1,1,"",""}, {"bypass","Bypass",0,1,0,1,"","Off|On"},
 {"dread","Dread",0,1,.5f,1,"",""}, {"autoLevel","Auto Level",0,1,1,1,"","Off|On"},

 {"abyssOn","The Abyss",0,1,1,1,"","Off|On"}, {"cutoff","Cutoff",40,20000,2200,.25f,"Hz",""},
 {"resonance","Resonance",0,1,.3f,1,"",""}, {"highPass","High-Pass",20,2000,40,.35f,"Hz",""},
 {"slope","Slope",0,1,1,1,"","12 dB|24 dB"}, {"darkness","Darkness",0,1,.5f,1,"",""},
 {"filterMovement","Movement",0,1,.25f,1,"",""}, {"filterRate","Movement Rate",.01f,8,.12f,.35f,"Hz",""},
 {"filterSync","Movement Sync",0,5,0,1,"","Free|1/4|1/2|1 bar|2 bars|4 bars"},
 {"tomb","Tomb Resonator",0,1,0,1,"",""}, {"tombNote","Tomb Note",0,11,2,1,"","C|C#|D|D#|E|F|F#|G|G#|A|A#|B"},
 {"tombOctave","Tomb Octave",0,4,1,1,"","1|2|3|4|5"}, {"tombRing","Tomb Ring",0,1,.65f,1,"",""},

 {"bloodOn","Ancient Blood",0,1,1,1,"","Off|On"}, {"drive","Drive",0,36,12,1,"dB",""},
 {"texture","Texture",0,1,.35f,1,"",""}, {"age","Age",0,1,.3f,1,"",""}, {"dust","Dust",0,1,0,1,"",""},
 {"instability","Instability",0,1,.2f,1,"",""}, {"bloodMix","Blood Mix",0,1,.7f,1,"",""},

 {"choirOn","The Choir",0,1,1,1,"","Off|On"}, {"voices","Voice Count",1,4,3,1,"",""},
 {"interval","Interval",0,5,0,1,"","Octave below|Fifth below|Fourth above|Fifth above|Octave above|Octave + fifth"},
 {"choirDetune","Choir Detune",0,50,12,1,"ct",""}, {"formant","Formant",0,1,.4f,1,"",""},
 {"choirMovement","Choir Movement",0,1,.35f,1,"",""}, {"choirBlend","Choir Blend",0,1,.45f,1,"",""},
 {"drone","Drone",0,1,.3f,1,"",""}, {"droneHold","Drone Hold",.5f,30,6,.4f,"s",""},

 {"wraithOn","The Wraith",0,1,1,1,"","Off|On"}, {"blur","Spectral Blur",0,1,.5f,1,"",""},
 {"wraithDetune","Wraith Detune",0,100,15,.6f,"ct",""}, {"drift","Drift",0,1,.4f,1,"",""},
 {"ghost","Ghost",0,1,.45f,1,"",""}, {"freeze","Freeze",0,1,0,1,"","Off|On"}, {"wraithMix","Wraith Mix",0,1,.5f,1,"",""},

 {"echoesOn","The Echoes",0,1,1,1,"","Off|On"}, {"delayTime","Time",20,2000,480,.5f,"ms",""},
 {"delaySync","Time Sync",0,7,0,1,"","Free|1/16|1/8|1/8 dotted|1/4|1/4 dotted|1/2|1 bar"},
 {"feedback","Feedback",0,.98f,.5f,1,"",""}, {"delayDamping","Echo Damping",0,1,.5f,1,"",""},
 {"diffusion","Diffusion",0,1,.35f,1,"",""}, {"reverse","Reverse",0,1,0,1,"","Off|On"},
 {"descent","Descent",0,3,0,1,"","Off|Semitone|Fifth|Octave"},
 {"spread","Stereo Spread",0,1,.7f,1,"",""}, {"delayMix","Echoes Mix",0,1,.4f,1,"",""},

 {"cryptOn","The Crypt",0,1,1,1,"","Off|On"}, {"space","Space",0,1,.75f,1,"",""},
 {"decay","Decay",.3f,30,7,.4f,"s",""}, {"preDelay","Pre-Delay",0,250,30,1,"ms",""},
 {"damping","Damping",0,1,.5f,1,"",""}, {"reverbLowCut","Low Cut",20,1000,120,.5f,"Hz",""},
 {"shimmer","Shimmer",0,1,.15f,1,"",""}, {"reverbWidth","Width",0,1,1,1,"",""},
 {"reverbMix","Crypt Mix",0,1,.5f,1,"",""},
 {"character","Chamber",0,3,1,1,"","Forgotten Chapel|Ancient Cathedral|Underground Crypt|Endless Void"},
 {"cryptMode","Crypt Mode",0,1,0,1,"","Normal|Rising"}, {"riseTime","Rise Time",.2f,4,1.2f,.5f,"s",""},
 {"eternal","Eternal",0,1,0,1,"","Off|On"},

 {"ritualOn","Ritual Engine",0,1,0,1,"","Off|On"}, {"ritualX","Ritual X",0,1,.5f,1,"",""},
 {"ritualY","Ritual Y",0,1,.5f,1,"",""}, {"motionOn","Ritual Motion",0,1,0,1,"","Off|On"},
 {"motionRate","Motion Rate",.01f,1,.04f,.5f,"Hz",""}, {"motionDepth","Motion Depth",0,1,.4f,1,"",""},
 {"motionPattern","Motion Pattern",0,2,0,1,"","Circle|Pendulum|Wander"}, {"motionSync","Motion Sync",0,1,0,1,"","Free|16 beats"}
}};
inline Frame defaults() { Frame f{}; for (std::size_t i=0;i<Count;++i) f[i]=specs[i].def; return f; }
// Discrete parameters (on/off switches, menus, voice count) are never smoothed as numbers.
inline constexpr bool isDiscrete(std::size_t i) { return specs[i].choices[0]!=0 || i==Voices; }
}
