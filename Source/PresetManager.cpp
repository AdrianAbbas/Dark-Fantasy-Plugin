#include "PresetManager.h"
#include <initializer_list>
#include <utility>

namespace veil {
namespace {
// Every preset starts from the complete default frame, so every parameter is set.
FactoryPreset make(const char* name, const char* description, std::initializer_list<std::pair<P, float>> changes) {
    auto values = defaults();
    values[RitualOn] = 0; values[Freeze] = 0; values[Eternal] = 0; values[Bypass] = 0;
    for (const auto& [parameter, value] : changes) values[parameter] = value;
    return { name, description, values };
}
}
const std::array<FactoryPreset, factoryPresetTotal>& factoryPresets() {
    static const std::array<FactoryPreset, factoryPresetTotal> bank {{
        make("Init - Clean Altar", "Near-transparent starting point: every module off.",
             {{AbyssOn,0},{BloodOn,0},{ChoirOn,0},{WraithOn,0},{EchoesOn,0},{CryptOn,0},{Dread,.5f},{GlobalMix,1}}),
        make("Forgotten Kingdom", "A vast, distant hall; sparse dotted echoes and a faint monk choir.",
             {{Cutoff,1900},{Darkness,.5f},{Space,.85f},{Decay,11},{PreDelay,60},{Character,1},{ReverbMix,.6f},
              {DelaySync,5},{Feedback,.45f},{DelayMix,.3f},{Diffusion,.6f},{ChoirBlend,.3f},{Drone,.25f},{Interval,0},
              {WraithMix,.4f},{Blur,.65f},{Drive,8},{BloodMix,.4f}}),
        make("Cathedral at Midnight", "Sixteen-second cathedral, shimmering high choir, very dark stone.",
             {{Cutoff,2600},{Darkness,.65f},{Space,1},{Decay,16},{PreDelay,90},{Damping,.7f},{Shimmer,.3f},{Character,1},
              {ReverbMix,.75f},{Interval,4},{Formant,.95f},{ChoirBlend,.4f},{Voices,4},{DelayMix,.15f},{WraithMix,.3f},{BloodMix,.3f}}),
        make("The Hollow Choir", "Four phantom voices an octave below and a held drone that never quite leaves.",
             {{Cutoff,3200},{Darkness,.4f},{Voices,4},{Interval,0},{ChoirDetune,22},{Formant,.75f},{ChoirMovement,.6f},{ChoirBlend,.85f},
              {Drone,.7f},{DroneHold,14},{Ghost,.6f},{WraithMix,.45f},{Character,0},{Decay,5},{ReverbMix,.45f},{DelayMix,.2f}}),
        make("Abyssal Drone", "Sub-octave drone, frozen-smeared spectra and an endless void tuned to D.",
             {{Cutoff,650},{Darkness,.85f},{Resonance,.45f},{Tomb,.55f},{TombNote,2},{TombOctave,1},{TombRing,.85f},
              {Drone,.9f},{DroneHold,25},{ChoirBlend,.5f},{Interval,0},{Blur,.92f},{WraithMix,.65f},{Ghost,.3f},
              {Character,3},{Decay,24},{ReverbMix,.7f},{Descent,3},{Feedback,.6f},{DelayMix,.3f},{Drive,10}}),
        make("Cursed Forest", "Wobbling, worn tape; reversed echoes creep through a twisted crypt.",
             {{Cutoff,3600},{FilterMovement,.7f},{FilterRate,.3f},{Resonance,.5f},{Drive,18},{Texture,.55f},{Age,.55f},{Dust,.3f},
              {Instability,.75f},{BloodMix,.85f},{Reverse,1},{Diffusion,.8f},{Spread,1},{Feedback,.55f},{DelayMix,.5f},
              {WraithDetune,45},{Drift,.8f},{WraithMix,.45f},{Character,2},{Decay,4},{ReverbMix,.45f},{ChoirBlend,.2f}}),
        make("Fallen Angel", "Bright, shimmering heaven collapsing slowly: octave-up choir and ghosts in an endless void.",
             {{Cutoff,9000},{Darkness,.15f},{Shimmer,.75f},{Character,3},{Decay,14},{Damping,.25f},{ReverbMix,.7f},
              {Interval,4},{Formant,.9f},{ChoirBlend,.55f},{Voices,3},{Ghost,.8f},{WraithMix,.55f},{Blur,.6f},
              {DelayMix,.25f},{Drive,4},{BloodMix,.3f}}),
        make("Ashes of a King", "Burnt, ancient saturation and dust; echoes that sink a semitone at a time.",
             {{Drive,26},{Texture,.65f},{Age,.85f},{Dust,.45f},{Instability,.4f},{BloodMix,.9f},{Cutoff,1500},{Darkness,.6f},
              {DelaySync,2},{Descent,1},{Feedback,.55f},{DelayMix,.4f},{Character,0},{Decay,3},{ReverbMix,.35f},
              {ChoirBlend,.15f},{WraithMix,.25f}}),
        make("Moonlit Ruins", "Open and silvery: wide ping-pong echoes and a short chapel; the lightest world.",
             {{Cutoff,6000},{Darkness,.2f},{FilterMovement,.35f},{DelaySync,3},{Feedback,.6f},{Spread,1},{DelayMix,.5f},{Diffusion,.3f},
              {Character,0},{Decay,5},{ReverbMix,.45f},{Blur,.3f},{WraithMix,.3f},{ChoirBlend,.2f},{Drive,5},{BloodMix,.35f},{Dread,.4f}}),
        make("The Black Citadel", "A low, resonant fortress: tomb resonance in A, a fifth-dropping echo, heavy stone.",
             {{Cutoff,480},{Resonance,.55f},{Darkness,.9f},{Tomb,.45f},{TombNote,9},{TombOctave,0},{TombRing,.7f},
              {Drive,20},{BloodMix,.75f},{Character,2},{Decay,9},{ReverbMix,.65f},{Descent,2},{DelaySync,4},{Feedback,.55f},{DelayMix,.35f},
              {Interval,0},{ChoirBlend,.4f},{Drone,.5f}}),
        make("Ghosts of the Chapel", "Pale octave ghosts and backwards echoes drift through a small stone chapel.",
             {{Ghost,.95f},{Blur,.75f},{WraithDetune,30},{WraithMix,.75f},{Reverse,1},{Feedback,.6f},{DelayMix,.45f},
              {Character,0},{Decay,6},{ReverbMix,.5f},{ChoirOn,0},{Cutoff,4200},{Darkness,.35f}}),
        make("Witch's Lament", "Moving filters and a crying 'oo' choir a fourth above; echoes fall by semitones.",
             {{Cutoff,2800},{Resonance,.6f},{FilterMovement,.6f},{FilterRate,.5f},{Interval,2},{Formant,.05f},{ChoirMovement,.85f},
              {ChoirDetune,35},{ChoirBlend,.65f},{Voices,2},{Descent,1},{DelaySync,3},{Feedback,.5f},{DelayMix,.4f},
              {Instability,.5f},{Character,2},{Decay,5},{ReverbMix,.45f}}),
        make("Eternal Winter", "Icy and bright: a thirty-second void, shimmer, and a frozen haze of detuned spectra.",
             {{Cutoff,12000},{Darkness,.1f},{HighPass,200},{Character,3},{Decay,30},{Damping,.15f},{Shimmer,.5f},{ReverbMix,.8f},
              {Blur,.88f},{WraithDetune,10},{WraithMix,.6f},{ChoirBlend,.2f},{Interval,4},{DelayMix,.2f},{Drive,2},{BloodMix,.2f}}),
        make("The Final Ritual", "Everything at once: full Dread, octave-sinking echoes, rising crypt, roaring drive.",
             {{Dread,.9f},{Drive,24},{Texture,.6f},{BloodMix,.8f},{Voices,4},{ChoirBlend,.6f},{Drone,.6f},{Ghost,.8f},{WraithMix,.6f},
              {Descent,3},{Feedback,.65f},{DelayMix,.45f},{Character,3},{Decay,20},{CryptMode,1},{RiseTime,1.6f},{ReverbMix,.7f},{Cutoff,1600}}),
        make("Ancient Spellbook", "Crackling parchment, a tomb ringing in E and a reverb that swells backwards into each note.",
             {{Age,.9f},{Dust,.55f},{Drive,10},{BloodMix,.7f},{Tomb,.5f},{TombNote,4},{TombOctave,2},{TombRing,.6f},
              {CryptMode,1},{RiseTime,1.3f},{Character,1},{Decay,6},{ReverbMix,.6f},{DelaySync,1},{Diffusion,.6f},{DelayMix,.3f},
              {ChoirBlend,.2f},{WraithMix,.3f}}),
        make("Throne of Shadows", "Slow two-bar filter breathing, a deep choir and a cathedral that never ends.",
             {{Cutoff,950},{Darkness,.75f},{FilterMovement,.4f},{FilterSync,4},{Voices,4},{Interval,0},{ChoirBlend,.6f},{Drone,.45f},
              {Character,1},{Decay,13},{ReverbMix,.65f},{DelaySync,6},{Descent,2},{Feedback,.5f},{DelayMix,.3f},{Drive,14},{BloodMix,.6f}})
    }};
    return bank;
}
}
