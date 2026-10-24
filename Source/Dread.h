#pragma once
#include "Parameters.h"
#include <algorithm>
#include <cmath>

namespace veil {
// DREAD is one macro over several modules. At 50 % it changes nothing: the panel
// settings are heard exactly. Towards 0 % everything lightens (brighter, smaller,
// drier, less wet overall); towards 100 % every module is pushed to full
// transformation. Offsets apply on top of the stored settings, never replacing them;
// a module mix the user set to zero stays zero.
inline void applyDread(Frame& p, float dread) noexcept {
    const float u = std::clamp((dread - 0.5f) * 2.0f, -1.0f, 1.0f);
    if (u == 0.0f) return;
    const auto clampTo = [&](P id) { p[id] = std::clamp(p[id], specs[id].min, specs[id].max); };
    const auto scale = [&](P id, float factor) { p[id] *= factor; clampTo(id); };
    const auto add = [&](P id, float delta) { p[id] += delta; clampTo(id); };
    scale(Cutoff, std::exp2(-1.8f * u));
    add(Darkness, 0.35f * u);
    if (u > 0) add(Resonance, 0.15f * u);
    add(Drive, 10.0f * u);
    scale(BloodMix, 1.0f + 0.5f * u);
    add(Age, 0.2f * u);
    scale(ChoirBlend, 1.0f + 0.8f * u);
    scale(Drone, 1.0f + 1.0f * u);
    scale(WraithMix, 1.0f + 0.7f * u);
    add(Blur, 0.3f * u);
    add(Ghost, 0.3f * u);
    add(Feedback, 0.2f * u);
    scale(DelayMix, 1.0f + 0.5f * u);
    scale(ReverbMix, 1.0f + 0.6f * u);
    scale(Decay, std::exp2(1.4f * u));
    add(Space, 0.2f * u);
    if (u < 0) scale(GlobalMix, 1.0f + 0.55f * u);
}
}
