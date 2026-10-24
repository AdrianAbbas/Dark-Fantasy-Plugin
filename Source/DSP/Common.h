#pragma once
// Small real-time-safe DSP building blocks shared by the modules. All storage
// is allocated in prepare(); processing never allocates, locks or logs.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace veil::dsp {
inline constexpr double pi = 3.14159265358979323846;
inline constexpr float twoPi = 6.283185307179586f;

inline float clamp01(float x) noexcept { return std::clamp(x, 0.0f, 1.0f); }
inline float lerp(float a, float b, float t) noexcept { return a + t * (b - a); }
inline float dbToGain(float db) noexcept { return std::pow(10.0f, db * 0.05f); }
inline float finiteOr(float x, float fallback = 0.0f) noexcept { return std::isfinite(x) ? x : fallback; }
// One-pole coefficient for a time constant in seconds.
inline float timeCoefficient(double seconds, double sampleRate) noexcept {
    return seconds <= 0 ? 1.0f : static_cast<float>(1.0 - std::exp(-1.0 / (seconds * sampleRate)));
}
inline float poleForHz(double hz, double sampleRate) noexcept {
    return static_cast<float>(1.0 - std::exp(-2.0 * pi * std::clamp(hz, 1.0, sampleRate * 0.49) / sampleRate));
}
// Smooth saturation that is linear below the knee and approaches the ceiling.
inline float softLimit(float x, float ceiling = 0.98f) noexcept {
    const float a = std::abs(x), knee = ceiling * 0.8f;
    if (a <= knee) return x;
    const float range = ceiling - knee;
    return std::copysign(knee + range * std::tanh((a - knee) / range), x);
}
// Equal-power blend for layers that are uncorrelated with the dry signal.
inline void equalPowerGains(float mix, float& dryGain, float& wetGain) noexcept {
    mix = clamp01(mix);
    dryGain = std::sqrt(1.0f - mix);
    wetGain = std::sqrt(mix);
}

// Exponential smoother towards a target.
struct Smoothed {
    float value = 0, target = 0, coefficient = 1;
    void setTime(double seconds, double sampleRate) noexcept { coefficient = timeCoefficient(seconds, sampleRate); }
    void snap(float v) noexcept { value = target = v; }
    void set(float t) noexcept { target = t; }
    float next() noexcept { value += coefficient * (target - value); return value; }
    // Advance n samples at once (for control-rate updates).
    float skip(int n) noexcept {
        value = target + (value - target) * std::pow(1.0f - coefficient, static_cast<float>(n));
        return value;
    }
};

// Linear ramp of a gain from its current value towards a target over a fixed time.
struct Ramp {
    float value = 0, step = 0;
    void prepare(double seconds, double sampleRate) noexcept { step = static_cast<float>(1.0 / std::max(1.0, seconds * sampleRate)); }
    float next(float target) noexcept {
        if (value < target) value = std::min(target, value + step);
        else if (value > target) value = std::max(target, value - step);
        return value;
    }
};

// Deterministic xorshift generator.
struct Random {
    std::uint32_t state = 0x5645494cu;
    void seed(std::uint32_t s) noexcept { state = s ? s : 0x5645494cu; }
    std::uint32_t nextInt() noexcept { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
    float bipolar() noexcept { return static_cast<float>(nextInt() >> 8) * (1.0f / 8388608.0f) - 1.0f; }
    float unit() noexcept { return static_cast<float>(nextInt() >> 8) * (1.0f / 16777216.0f); }
};

// Topology-preserving-transform state variable filter (Zavalishin). Unconditionally
// stable for any cutoff and damping k > 0.
struct Svf {
    float g = 0, k = 1.4142f, a1 = 0, a2 = 0, a3 = 0;
    float s1 = 0, s2 = 0;
    void set(double hz, double sampleRate, float damping) noexcept {
        g = static_cast<float>(std::tan(pi * std::clamp(hz, 5.0, sampleRate * 0.45) / sampleRate));
        k = std::max(0.02f, damping);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void clear() noexcept { s1 = s2 = 0; }
    // Returns low-pass; band and high available through out parameters.
    float process(float x, float& band, float& high) noexcept {
        const float v3 = x - s2;
        const float v1 = a1 * s1 + a2 * v3;
        const float v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0f * v1 - s1; s2 = 2.0f * v2 - s2;
        band = v1; high = x - k * v1 - v2;
        return v2;
    }
    float low(float x) noexcept { float b, h; return process(x, b, h); }
    float highPass(float x) noexcept { float b, h; process(x, b, h); return h; }
    float bandPass(float x) noexcept { float b, h; process(x, b, h); return b; }
};

// RBJ biquad, transposed direct form II.
struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void clear() noexcept { z1 = z2 = 0; }
    float process(float x) noexcept {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y;
        return y;
    }
    void setNormalised(double B0, double B1, double B2, double A0, double A1, double A2) noexcept {
        b0 = static_cast<float>(B0 / A0); b1 = static_cast<float>(B1 / A0); b2 = static_cast<float>(B2 / A0);
        a1 = static_cast<float>(A1 / A0); a2 = static_cast<float>(A2 / A0);
    }
    void peaking(double sr, double hz, double q, double db) noexcept {
        const double A = std::pow(10.0, db / 40.0), w = 2 * pi * std::clamp(hz, 10.0, sr * 0.45) / sr, alpha = std::sin(w) / (2 * q);
        setNormalised(1 + alpha * A, -2 * std::cos(w), 1 - alpha * A, 1 + alpha / A, -2 * std::cos(w), 1 - alpha / A);
    }
    void highShelf(double sr, double hz, double db) noexcept {
        const double A = std::pow(10.0, db / 40.0), w = 2 * pi * std::clamp(hz, 10.0, sr * 0.45) / sr;
        const double c = std::cos(w), alpha = std::sin(w) / 2 * std::sqrt(2.0), sa = 2 * std::sqrt(A) * alpha;
        setNormalised(A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
                      (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
    }
    void bandPass(double sr, double hz, double q) noexcept {   // 0 dB peak gain
        const double w = 2 * pi * std::clamp(hz, 10.0, sr * 0.45) / sr, alpha = std::sin(w) / (2 * q);
        setNormalised(alpha, 0, -alpha, 1 + alpha, -2 * std::cos(w), 1 - alpha);
    }
};

// Fractional delay line with linear interpolation. Capacity is fixed at prepare().
struct DelayLine {
    std::vector<float> data;
    std::size_t mask = 0, write = 0;
    void prepare(std::size_t minimumLength) {
        std::size_t size = 16;
        while (size < minimumLength + 4) size <<= 1;
        data.assign(size, 0.0f); mask = size - 1; write = 0;
    }
    void clear() noexcept { std::fill(data.begin(), data.end(), 0.0f); write = 0; }
    std::size_t capacity() const noexcept { return data.size(); }
    void push(float x) noexcept { data[write] = x; write = (write + 1) & mask; }
    // delay in samples, >= 1 (1 = the most recently pushed sample).
    float read(float delay) const noexcept {
        delay = std::clamp(delay, 1.0f, static_cast<float>(data.size() - 3));
        const float position = static_cast<float>(write) - delay + static_cast<float>(data.size());
        const auto index = static_cast<std::size_t>(position);
        const float fraction = position - static_cast<float>(index);
        const float a = data[index & mask], b = data[(index + 1) & mask];
        return a + fraction * (b - a);
    }
    float readInteger(std::size_t delay) const noexcept { return data[(write + data.size() - delay) & mask]; }
};

// Granular pitch shifter: two read heads with complementary Hann windows sweep a
// short delay window at the requested ratio. Latency is about window / 2.
struct GranularShifter {
    DelayLine line;
    double phase = 0;
    float window = 2400;
    void prepare(double sampleRate, double windowSeconds) {
        window = static_cast<float>(windowSeconds * sampleRate);
        line.prepare(static_cast<std::size_t>(window * 2 + 64));
        phase = 0;
    }
    void clear() noexcept { line.clear(); }
    void setPhase(double p) noexcept { phase = p - std::floor(p); }
    float latency() const noexcept { return window * 0.5f + 1.0f; }
    float process(float x, float ratio) noexcept {
        line.push(x);
        phase += (1.0 - static_cast<double>(ratio)) / window;
        phase -= std::floor(phase);
        const double other = phase < 0.5 ? phase + 0.5 : phase - 0.5;
        const float w = 0.5f - 0.5f * std::cos(twoPi * static_cast<float>(phase));
        return line.read(1.0f + static_cast<float>(phase) * window) * w
             + line.read(1.0f + static_cast<float>(other) * window) * (1.0f - w);
    }
};

// Removes DC from asymmetric processing.
struct DcBlocker {
    float x1 = 0, y1 = 0, r = 0.999f;
    void prepare(double sampleRate) noexcept { r = static_cast<float>(std::exp(-2.0 * pi * 12.0 / sampleRate)); }
    void clear() noexcept { x1 = y1 = 0; }
    float process(float x) noexcept { const float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
};

// Equal-tempered note frequency: note 0..11 = C..B, octave as in scientific pitch.
inline double noteFrequency(int note, int octave) noexcept {
    const int midi = (octave + 1) * 12 + note;
    return 440.0 * std::pow(2.0, (midi - 69) / 12.0);
}
// Tempo-synced beat lengths.
inline double beatsToSeconds(double beats, double bpm) noexcept { return beats * 60.0 / std::clamp(bpm, 20.0, 400.0); }
}
