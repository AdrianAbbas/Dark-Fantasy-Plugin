#pragma once
#include "Common.h"

namespace veil {
// Keeps the processed (wet) level close to the input level. It adapts only while
// the input is sustaining; during releases it holds its gain so reverb and echo
// tails decay naturally instead of being pulled up. Range -18 dB .. +12 dB.
struct LevelMatcher {
    float referenceSlow = 0, referenceFast = 0, outputSlow = 0, gain = 1, target = 1;
    float slow = 0, fast = 0, glide = 0;
    int counter = 0;
    void prepare(double sampleRate) {
        slow = dsp::timeCoefficient(1.0, sampleRate);
        fast = dsp::timeCoefficient(0.08, sampleRate);
        glide = dsp::timeCoefficient(0.3, sampleRate);
        reset();
    }
    void reset() noexcept { referenceSlow = referenceFast = outputSlow = 0; gain = target = 1; counter = 0; }
    void process(const float* refL, const float* refR, float* l, float* r, int n, bool enabled) noexcept {
        for (int i = 0; i < n; ++i) {
            const float referencePower = 0.5f * (refL[i] * refL[i] + refR[i] * refR[i]);
            const float outputPower = 0.5f * (l[i] * l[i] + r[i] * r[i]);
            referenceSlow += slow * (referencePower - referenceSlow);
            referenceFast += fast * (referencePower - referenceFast);
            outputSlow += slow * (outputPower - outputSlow);
            if (--counter <= 0) {
                counter = 16;
                if (!enabled) target = 1;
                else if (referenceSlow > 1.0e-7f && referenceFast > 0.3f * referenceSlow && outputSlow > 1.0e-12f)
                    target = std::clamp(std::sqrt(referenceSlow / outputSlow), 0.125f, 4.0f);
            }
            gain += glide * (target - gain);
            if (target == 1.0f && std::abs(gain - 1.0f) < 1.0e-6f) gain = 1.0f;
            l[i] *= gain; r[i] *= gain;
        }
    }
};

// Sample-peak safety limiter: instant attack (no overshoot, no look-ahead latency),
// 80 ms release. Transparent while the signal stays below the ceiling.
struct SafetyLimiter {
    float gain = 1, release = 0.001f, ceiling = 0.966f;   // -0.3 dBFS
    void prepare(double sampleRate) { release = dsp::timeCoefficient(0.08, sampleRate); gain = 1; }
    void reset() noexcept { gain = 1; }
    void process(float& l, float& r) noexcept {
        const float peak = std::max(std::abs(l), std::abs(r));
        const float needed = peak > ceiling ? ceiling / peak : 1.0f;
        if (needed < gain) gain = needed;
        else { gain += release * (1.0f - gain); if (gain > 0.999999f) gain = 1.0f; }
        l *= gain; r *= gain;
    }
};
}
