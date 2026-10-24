#include "RitualEngine.h"
#include <algorithm>
#include <cmath>

namespace veil {
namespace {
constexpr double tau = 6.2831853071795864769;
float safeUnit(float v) noexcept { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.5f; }
}

void RitualEngine::prepare(double newRate) {
    sampleRate = std::isfinite(newRate) && newRate >= 8000.0 ? newRate : 48000.0;
    coordinateSlew = 1.0 - std::exp(-1.0 / (0.035 * sampleRate));
    reset();
}
void RitualEngine::reset() {
    phase = 0.0;
    displayX = displayY = 0.5f;
    motionX = motionY = wanderFromX = wanderFromY = 0.0f;
    wanderToX = 0.4f; wanderToY = -0.4f;
    randomState = 0x5645494c;
    controlCounter = 0;
    initialized = false;
}
float RitualEngine::randomSigned() noexcept {
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;
    return static_cast<float>(randomState & 0x00ffffffu) / 8388607.5f - 1.0f;
}
RitualWeights RitualEngine::weights(float xValue, float yValue) noexcept {
    const float x = safeUnit(xValue), y = safeUnit(yValue);
    float spirit = 1.0f - y;
    float ruin = std::max(0.0f, x - 0.5f * spirit);
    float shadow = std::max(0.0f, 1.0f - x - 0.5f * spirit);
    const float sum = shadow + spirit + ruin;
    return {shadow / sum, spirit / sum, ruin / sum};
}
std::array<float, 2> RitualEngine::constrain(float xValue, float yValue) noexcept {
    const auto w = weights(xValue, yValue);
    return {w.ruin + 0.5f * w.spirit, w.shadow + w.ruin};
}
Frame RitualEngine::processBlock(const Frame& manual, float bpm, int samples) noexcept {
    samples = std::max(1, samples);
    const float validBpm = std::isfinite(bpm) ? std::clamp(bpm, 20.0f, 400.0f) : 120.0f;
    const double rate = manual[MotionSync] >= 0.5f ? validBpm / 960.0 : std::clamp(manual[MotionRate], 0.01f, 1.0f);
    // A free-running phase avoids discontinuity when the host starts or stops.
    // Sync follows the host BPM rather than forcibly resetting to bar position.
    phase += rate * samples / sampleRate;
    while (phase >= 1.0) {
        phase -= 1.0;
        wanderFromX = wanderToX; wanderFromY = wanderToY;
        wanderToX = randomSigned(); wanderToY = randomSigned();
    }
    controlCounter -= samples;
    if (controlCounter < 0) {
        controlCounter = 31;
        const int pattern = std::clamp(static_cast<int>(std::lround(manual[MotionPattern])), 0, 2);
        if (pattern == 0) {
            motionX = 0.40f * static_cast<float>(std::cos(tau * phase));
            motionY = 0.34f * static_cast<float>(std::sin(tau * phase));
        } else if (pattern == 1) {
            motionX = 0.48f * static_cast<float>(std::sin(tau * phase));
            motionY = 0.13f * static_cast<float>(std::cos(2.0 * tau * phase));
        } else {
            const float t = static_cast<float>(phase);
            const float curve = t * t * (3.0f - 2.0f * t);
            motionX = 0.44f * (wanderFromX + curve * (wanderToX - wanderFromX));
            motionY = 0.35f * (wanderFromY + curve * (wanderToY - wanderFromY));
        }
    }
    const float motionAmount = safeUnit(manual[MotionOn]) * safeUnit(manual[MotionDepth]);
    const auto position = constrain(manual[RitualX] + motionX * motionAmount,
                                    manual[RitualY] + motionY * motionAmount);
    if (!initialized) { displayX = position[0]; displayY = position[1]; initialized = true; }
    const double slew = samples == 1 ? coordinateSlew : 1.0 - std::pow(1.0 - coordinateSlew, samples);
    displayX += slew * (position[0] - displayX);
    displayY += slew * (position[1] - displayY);
    const auto w = weights(static_cast<float>(displayX), static_cast<float>(displayY));
    const float amount = safeUnit(manual[RitualOn]);
    Frame out = manual;
    // The ritual is an offset layer, never an overwrite of the host's stored values.
    // Off switches, global gain, timing and freeze remain entirely manual.
    const auto add = [&](P p, float delta) {
        out[p] = std::clamp(manual[p] + amount * delta, specs[p].min, specs[p].max);
    };
    const auto scale = [&](P p, float factor) {
        out[p] = std::clamp(manual[p] * (1.0f + amount * (factor - 1.0f)), specs[p].min, specs[p].max);
    };
    scale(Cutoff, 0.35f * w.shadow + 1.03f * w.spirit + 0.68f * w.ruin);
    add(Darkness, 0.34f * w.shadow + 0.08f * w.ruin);
    add(FilterMovement, 0.10f * w.shadow + 0.20f * w.spirit + 0.13f * w.ruin);
    add(Space, 0.12f * w.shadow + 0.22f * w.spirit + 0.05f * w.ruin);
    scale(Decay, 1.35f * w.shadow + 1.55f * w.spirit + 1.08f * w.ruin);
    add(Damping, 0.18f * w.shadow - 0.10f * w.spirit + 0.08f * w.ruin);
    add(Shimmer, 0.30f * w.spirit * (1.0f - manual[Shimmer]));
    add(ReverbWidth, 0.12f * w.spirit - 0.10f * w.shadow);
    scale(ReverbMix, 1.15f * w.shadow + 1.30f * w.spirit + 1.05f * w.ruin);
    add(Blur, 0.08f * w.shadow + 0.36f * w.spirit + 0.08f * w.ruin);
    add(WraithDetune, 5.0f * w.spirit + 8.0f * w.ruin);
    add(Drift, 0.18f * w.spirit + 0.15f * w.ruin);
    add(Ghost, 0.24f * w.spirit);
    scale(WraithMix, 0.70f * w.shadow + 1.70f * w.spirit + 1.15f * w.ruin);
    add(Drive, 2.0f * w.shadow + 10.0f * w.ruin);
    add(Texture, 0.05f * w.shadow + 0.32f * w.ruin);
    add(Age, 0.08f * w.shadow + 0.38f * w.ruin);
    add(Instability, 0.04f * w.spirit + 0.35f * w.ruin);
    scale(BloodMix, 1.10f * w.shadow + 0.70f * w.spirit + 1.45f * w.ruin);
    add(Diffusion, 0.12f * w.shadow + 0.25f * w.spirit + 0.08f * w.ruin);
    add(Spread, 0.18f * w.spirit - 0.08f * w.shadow);
    add(ChoirMovement, 0.25f * w.spirit + 0.12f * w.ruin);
    scale(ChoirBlend, 0.60f * w.shadow + 1.85f * w.spirit + 0.80f * w.ruin);
    return out;
}
}
