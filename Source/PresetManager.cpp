#include "PresetManager.h"
#include <initializer_list>
#include <utility>

namespace veil {
namespace {
FactoryPreset make(const char* name, std::initializer_list<std::pair<P, float>> changes) {
    auto values = defaults();
    // All programs initialize every parameter, including hidden choices and motion.
    values[Freeze] = 0.0f;
    for (const auto& [parameter, value] : changes) values[parameter] = value;
    return {name, values};
}
}
const std::array<FactoryPreset, 15>& factoryPresets() {
    static const std::array<FactoryPreset, 15> bank {{
        make("Forgotten Kingdom", {{Cutoff,4200},{Darkness,.42f},{Space,.72f},{Decay,7.0f},{Character,1},{ReverbMix,.36f},{DelayTime,610},{Feedback,.36f},{ChoirBlend,.14f},{RitualX,.30f},{RitualY,.73f},{MotionOn,1},{MotionRate,.025f},{MotionDepth,.24f}}),
        make("Cathedral at Midnight", {{Cutoff,3600},{Darkness,.48f},{Space,.97f},{Decay,14.5f},{PreDelay,72},{Damping,.78f},{ReverbLowCut,240},{ReverbMix,.62f},{Character,1},{WraithMix,.10f},{BloodMix,.12f},{DelayMix,.09f},{ChoirBlend,.06f},{RitualX,.22f},{RitualY,.88f},{GlobalMix,.80f}}),
        make("The Hollow Choir", {{Cutoff,9600},{Darkness,.10f},{Space,.66f},{Decay,6.8f},{Damping,.42f},{ReverbMix,.29f},{Blur,.46f},{Ghost,.54f},{WraithMix,.38f},{Drive,1},{BloodMix,.08f},{DelayMix,.10f},{Voices,4},{Interval,2},{ChoirDetune,12},{Formant,.72f},{ChoirBlend,.62f},{RitualX,.50f},{RitualY,.10f},{GlobalMix,.76f}}),
        make("Abyssal Drone", {{Cutoff,1250},{HighPass,26},{Darkness,.72f},{FilterMovement,.55f},{FilterRate,.035f},{Space,.88f},{Decay,16},{Character,3},{PreDelay,100},{ReverbMix,.50f},{Blur,.68f},{Ghost,.48f},{WraithMix,.37f},{Interval,0},{ChoirBlend,.26f},{DelayTime,1200},{Feedback,.64f},{DelayMix,.25f},{RitualX,.15f},{RitualY,.85f},{MotionOn,1},{MotionPattern,2},{MotionRate,.013f},{MotionDepth,.58f},{GlobalMix,.84f}}),
        make("Cursed Forest", {{Cutoff,5600},{FilterMovement,.52f},{FilterRate,.21f},{Space,.51f},{Decay,4.2f},{Character,2},{Blur,.34f},{WraithDetune,18},{Drift,.72f},{Ghost,.40f},{WraithMix,.29f},{Drive,6},{Texture,.38f},{Age,.32f},{Instability,.65f},{BloodMix,.48f},{DelayTime,370},{Reverse,1},{Diffusion,.53f},{DelayMix,.26f},{RitualX,.74f},{RitualY,.79f},{MotionOn,1},{MotionPattern,2},{MotionRate,.067f},{MotionDepth,.42f}}),
        make("Fallen Angel", {{Cutoff,11000},{Darkness,.08f},{Space,.84f},{Decay,10},{Damping,.30f},{Shimmer,.58f},{ReverbMix,.49f},{Character,3},{Blur,.48f},{Ghost,.49f},{WraithMix,.28f},{BloodMix,.10f},{DelaySync,3},{DelayMix,.18f},{Interval,3},{Voices,3},{Formant,.62f},{ChoirBlend,.36f},{RitualX,.50f},{RitualY,.02f},{GlobalMix,.80f}}),
        make("Ashes of a King", {{Cutoff,3200},{Darkness,.52f},{Space,.58f},{Decay,5.9f},{Damping,.84f},{ReverbMix,.35f},{Character,0},{WraithMix,.12f},{Drive,9},{Texture,.46f},{Age,.72f},{Dust,.09f},{Instability,.36f},{BloodMix,.63f},{DelayTime,680},{DelayDamping,.86f},{DelayMix,.19f},{ChoirBlend,.07f},{RitualX,.83f},{RitualY,.90f}}),
        make("Moonlit Ruins", {{Cutoff,8500},{Darkness,.16f},{Space,.42f},{Decay,3.6f},{PreDelay,28},{Damping,.47f},{ReverbMix,.24f},{Character,0},{Blur,.20f},{Ghost,.19f},{WraithMix,.12f},{Drive,1},{Age,.05f},{BloodMix,.15f},{DelaySync,1},{Feedback,.30f},{DelayMix,.24f},{Interval,2},{Voices,2},{ChoirBlend,.12f},{RitualX,.42f},{RitualY,.42f},{GlobalMix,.52f}}),
        make("The Black Citadel", {{Cutoff,1050},{HighPass,28},{Resonance,.53f},{Darkness,.82f},{Space,.94f},{Decay,11},{PreDelay,95},{Damping,.91f},{ReverbLowCut,80},{Character,2},{ReverbMix,.53f},{WraithMix,.15f},{Drive,7},{Texture,.28f},{BloodMix,.40f},{DelayTime,950},{Feedback,.48f},{DelayDamping,.89f},{DelayMix,.25f},{Interval,0},{ChoirBlend,.28f},{RitualX,.03f},{RitualY,.98f},{GlobalMix,.83f}}),
        make("Ghosts of the Chapel", {{Cutoff,6500},{Space,.68f},{Decay,6.4f},{Character,0},{ReverbMix,.36f},{Blur,.62f},{WraithDetune,14},{Drift,.66f},{Ghost,.77f},{WraithMix,.55f},{BloodMix,.10f},{DelayTime,820},{Feedback,.61f},{Diffusion,.72f},{Reverse,1},{DelayMix,.37f},{Voices,4},{ChoirBlend,.22f},{RitualX,.48f},{RitualY,.14f},{GlobalMix,.77f}}),
        make("Witch's Lament", {{Cutoff,5800},{Resonance,.43f},{FilterMovement,.38f},{FilterRate,.40f},{Space,.60f},{Decay,5.3f},{Character,2},{ReverbMix,.31f},{WraithDetune,22},{Drift,.60f},{Ghost,.43f},{WraithMix,.29f},{Drive,5},{Instability,.34f},{BloodMix,.25f},{DelaySync,3},{Feedback,.45f},{DelayMix,.32f},{Interval,1},{Voices,2},{Formant,.83f},{ChoirBlend,.39f},{RitualX,.62f},{RitualY,.40f}}),
        make("Eternal Winter", {{Cutoff,14000},{Darkness,.04f},{HighPass,140},{Space,.90f},{Decay,12},{Damping,.19f},{Shimmer,.36f},{ReverbMix,.47f},{Character,3},{Blur,.76f},{WraithDetune,9},{Drift,.42f},{Ghost,.55f},{WraithMix,.42f},{BloodMix,.06f},{DelayTime,1090},{DelayDamping,.25f},{Diffusion,.65f},{DelayMix,.20f},{Interval,3},{ChoirDetune,17},{Formant,.22f},{ChoirBlend,.27f},{RitualX,.50f},{RitualY,.02f},{MotionOn,1},{MotionRate,.02f},{MotionDepth,.30f},{GlobalMix,.83f}}),
        make("The Final Ritual", {{Cutoff,5000},{Darkness,.35f},{Space,.88f},{Decay,9.0f},{Character,3},{ReverbMix,.46f},{Shimmer,.24f},{Blur,.56f},{Ghost,.65f},{WraithMix,.40f},{Drive,11},{Texture,.53f},{Age,.44f},{Instability,.52f},{BloodMix,.44f},{DelaySync,4},{Feedback,.66f},{Diffusion,.74f},{Reverse,1},{DelayMix,.36f},{Voices,4},{ChoirBlend,.36f},{RitualX,.5f},{RitualY,.63f},{MotionOn,1},{MotionPattern,0},{MotionRate,.055f},{MotionDepth,.95f},{GlobalMix,.88f},{OutputGain,-6}}),
        make("Ancient Spellbook", {{Cutoff,4600},{Darkness,.40f},{Space,.31f},{Decay,2.7f},{Character,2},{ReverbMix,.21f},{WraithMix,.08f},{Drive,8},{Texture,.58f},{Age,.82f},{Dust,.16f},{Instability,.46f},{BloodMix,.74f},{DelayTime,245},{DelayDamping,.83f},{Diffusion,.18f},{DelayMix,.25f},{ChoirBlend,.04f},{RitualX,.95f},{RitualY,.98f},{GlobalMix,.70f}}),
        make("Throne of Shadows", {{Cutoff,2450},{Resonance,.36f},{Darkness,.64f},{FilterMovement,.26f},{Space,.89f},{Decay,9.5f},{PreDelay,56},{Damping,.72f},{Character,1},{ReverbMix,.45f},{Blur,.36f},{Ghost,.32f},{WraithMix,.25f},{Drive,5},{Age,.24f},{BloodMix,.35f},{DelayTime,740},{Feedback,.49f},{Diffusion,.52f},{DelayMix,.25f},{Interval,0},{Voices,4},{Formant,.35f},{ChoirBlend,.27f},{RitualX,.16f},{RitualY,.87f},{MotionOn,1},{MotionPattern,1},{MotionRate,.024f},{MotionDepth,.30f},{GlobalMix,.80f}})
    }};
    return bank;
}
}
