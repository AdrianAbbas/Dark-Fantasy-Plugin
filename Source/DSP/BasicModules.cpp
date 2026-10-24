#include "BasicModules.h"
#include <algorithm>
#include <cmath>

namespace veil {
namespace {
constexpr double pi = 3.14159265358979323846;
float clamp01(float x) noexcept { return std::clamp(x, 0.0f, 1.0f); }
float lerp(float a, float b, float t) noexcept { return a + t * (b - a); }
float safe(float x) noexcept { return std::isfinite(x) ? std::clamp(x, -16.0f, 16.0f) : 0.0f; }
float gain(float db) noexcept { return std::pow(10.0f, db * 0.05f); }
float filterG(float hz, double sr) noexcept {
    return std::tan(static_cast<float>(pi) * std::clamp(hz / static_cast<float>(sr), .00005f, .43f));
}
}
namespace basic {
float StateVariable::low(float x, float g, float k) noexcept {
    const float a = 1.0f / (1.0f + g * (g + k));
    const float v3 = x - s2;
    const float v1 = a * s1 + g * a * v3;
    const float v2 = s2 + g * a * s1 + g * g * a * v3;
    s1 = 2.0f * v1 - s1;
    s2 = 2.0f * v2 - s2;
    return v2;
}
float StateVariable::high(float x, float g, float k) noexcept {
    const float a = 1.0f / (1.0f + g * (g + k));
    const float v3 = x - s2;
    const float v1 = a * s1 + g * a * v3;
    const float v2 = s2 + g * a * s1 + g * g * a * v3;
    s1 = 2.0f * v1 - s1;
    s2 = 2.0f * v2 - s2;
    return x - k * v1 - v2;
}
void Ring::prepare(std::size_t n) { data.assign(std::max(n, std::size_t(8)), 0.0f); cursor = 0; }
void Ring::reset() noexcept { std::fill(data.begin(), data.end(), 0.0f); cursor = 0; }
float Ring::read(float samples) const noexcept {
    if (data.empty()) return 0.0f;
    samples = std::clamp(samples, 1.0f, static_cast<float>(data.size() - 2));
    float position = static_cast<float>(cursor) - samples;
    if (position < 0) position += static_cast<float>(data.size());
    const auto first = static_cast<std::size_t>(position);
    const auto second = first + 1 == data.size() ? 0 : first + 1;
    return lerp(data[first], data[second], position - static_cast<float>(first));
}
void Ring::push(float x) noexcept { if (!data.empty()) { data[cursor] = x; cursor = (cursor + 1 == data.size()) ? 0 : cursor + 1; } }
}

void Abyss::prepare(double sampleRate) { sr = std::clamp(sampleRate, 8000.0, 384000.0); reset(); }
void Abyss::reset() { phase = 0; slopeBlend = 1; for (auto* filters : { &hp, &lp1, &lp2 }) for (auto& f : *filters) f.clear(); }
void Abyss::process(float& left, float& right, const Frame& p, float bpm) noexcept {
    const float rate = p[FilterSync] >= .5f ? std::clamp(bpm, 20.0f, 400.0f) / 240.0f : p[FilterRate];
    phase += std::clamp(rate, .01f, 10.0f) / sr;
    if (phase >= 1) phase -= 1;
    const float darkness = clamp01(p[Darkness]);
    const float modulation = std::sin(static_cast<float>(phase * 2.0 * pi));
    const float frequency = p[Cutoff] * std::exp2(-2.8f * darkness + modulation * clamp01(p[FilterMovement]) * .85f);
    const float g = filterG(frequency, sr), hg = filterG(p[HighPass], sr);
    const float k = 1.0f / (.55f + 2.1f * clamp01(p[Resonance]));
    // Resonant coloration has compensated output, preventing high-Q gain surges.
    const float compensation = 1.0f / (1.0f + .28f * clamp01(p[Resonance]));
    slopeBlend += static_cast<float>(1.0 / (.015 * sr)) * (clamp01(p[Slope]) - slopeBlend);
    float* samples[] = { &left, &right };
    for (int c = 0; c < 2; ++c) {
        const float dry = safe(*samples[c]);
        const float high = hp[c].high(dry, hg, 1.41421356f);
        const float low = lp1[c].low(high, g, k);
        const float steep = lp2[c].low(low, g, 1.41421356f);
        const float wet = lerp(low, steep, slopeBlend) * compensation;
        *samples[c] = lerp(dry, safe(wet), clamp01(p[AbyssOn]));
    }
}

void AncientBlood::prepare(double sampleRate) {
    sr = std::clamp(sampleRate, 8000.0, 384000.0);
    for (auto& ring : wow) ring.prepare(static_cast<std::size_t>(sr * .025) + 8);
    dcCoefficient = static_cast<float>(std::exp(-2.0 * pi * 10.0 / sr));
    // Absolute reconstruction cutoff keeps the color consistent across rates,
    // while the Nyquist-dependent ceiling protects lower-rate processing.
    const double reconstructionHz = std::min(18000.0, sr * .4);
    reconstructionCoefficient = static_cast<float>(1.0 - std::exp(-2.0 * pi * reconstructionHz / (2.0 * sr)));
    reset();
}
void AncientBlood::reset() {
    for (auto& ring : wow) ring.reset();
    previous = reconstruction1 = reconstruction2 = aged = held = dcIn = dcOut = {};
    phase = 0; holdPhase = 0; envelope = randomDrift = driftTarget = 0; driftCountdown = 0; randomState = 0x8ad02ec1u;
}
float AncientBlood::random() noexcept {
    randomState ^= randomState << 13; randomState ^= randomState >> 17; randomState ^= randomState << 5;
    return static_cast<float>(randomState >> 8) * (1.0f / 8388608.0f) - 1.0f;
}
void AncientBlood::process(float& left, float& right, const Frame& p, float) noexcept {
    const float inst = clamp01(p[Instability]), age = clamp01(p[Age]), texture = clamp01(p[Texture]);
    phase += .43 / sr; if (phase >= 1) phase -= 1;
    if (--driftCountdown <= 0) { driftTarget = random(); driftCountdown = static_cast<int>(sr * .27); }
    randomDrift += static_cast<float>(1.0 / (.15 * sr)) * (driftTarget - randomDrift);
    const float offset = 1.0f + inst * static_cast<float>(sr) * (.0015f + .001f * std::sin(static_cast<float>(phase * 2 * pi)) + .0004f * randomDrift);
    const float peak = std::max(std::abs(safe(left)), std::abs(safe(right)));
    envelope += (peak - envelope) * static_cast<float>(peak > envelope ? 1.0 / (.005 * sr) : 1.0 / (.2 * sr));
    const float drive = gain(std::clamp(p[Drive], 0.0f, 24.0f));
    const float driveCompensation = 1.0f / std::sqrt(drive);
    // Two nonlinear evaluations/sample at 2x, followed by a two-pole
    // reconstruction filter. Intentionally dark; not a brickwall oversampler.
    const float reconstruction = reconstructionCoefficient;
    const float ageHz = 17500.0f * std::exp2(-4.0f * age);
    const float ageCoefficient = 1.0f - std::exp(-2.0f * static_cast<float>(pi) * ageHz / static_cast<float>(sr));
    const float bits = 16.0f - 10.0f * age;
    const float levels = std::exp2(bits);
    const float heldSampleRate = 48000.0f / (1.0f + 7.0f * age * age);
    holdPhase += std::min(1.0f, heldSampleRate / static_cast<float>(sr));
    const bool update = holdPhase >= 1; if (update) holdPhase -= 1;
    const float noiseLevel = clamp01(p[Dust]) * .0025f * clamp01(envelope * 20.0f);
    float* samples[] = { &left, &right };
    for (int c = 0; c < 2; ++c) {
        const float dry = safe(*samples[c]);
        wow[c].push(dry);
        const float input = lerp(dry, wow[c].read(offset), inst) * (1.0f + inst * .035f * randomDrift);
        const auto distort = [drive, texture, driveCompensation](float value) noexcept {
            const float z = value * drive;
            const float rounded = std::tanh(z);
            const float coarse = std::clamp(z, -1.0f, 1.0f);
            return lerp(rounded, coarse, texture * .85f) * driveCompensation;
        };
        const float upsampled[] = { .5f * (previous[c] + input), input };
        previous[c] = input;
        float wet = 0;
        for (float x : upsampled) {
            reconstruction1[c] += reconstruction * (distort(x) - reconstruction1[c]);
            reconstruction2[c] += reconstruction * (reconstruction1[c] - reconstruction2[c]);
            wet = reconstruction2[c];
        }
        aged[c] += ageCoefficient * (wet - aged[c]);
        if (update) held[c] = std::round(aged[c] * levels) / levels;
        wet = lerp(aged[c], held[c], age * age) + noiseLevel * random();
        // A fixed 10 Hz DC blocker protects against asymmetric degraded material.
        const float dc = wet - dcIn[c] + dcCoefficient * dcOut[c];
        dcIn[c] = wet; dcOut[c] = dc;
        *samples[c] = lerp(dry, safe(dc), clamp01(p[BloodOn]) * clamp01(p[BloodMix]));
    }
}

void Echoes::prepare(double sampleRate) {
    sr = std::clamp(sampleRate, 8000.0, 384000.0);
    for (auto& ring : delay) ring.prepare(static_cast<std::size_t>(sr * 12.1) + 8);
    for (auto& ring : diffuser) ring.prepare(static_cast<std::size_t>(sr * .06) + 8);
    reset();
}
void Echoes::reset() {
    for (auto& ring : delay) ring.reset();
    for (auto& ring : diffuser) ring.reset();
    damped = hpState = {}; reversePhase = 0; reverseBlend = 0; delaySamples = static_cast<float>(sr * .44);
}
void Echoes::process(float& left, float& right, const Frame& p, float bpm) noexcept {
    const int sync = std::clamp(static_cast<int>(std::lround(p[DelaySync])), 0, 4);
    constexpr float beats[] = { 0, .5f, 1, 1.5f, 2 };
    const float seconds = sync == 0 ? p[DelayTime] * .001f : beats[sync] * 60.0f / std::clamp(bpm, 20.0f, 400.0f);
    const float wanted = std::clamp(seconds, .02f, 6.0f) * static_cast<float>(sr);
    // Slewed read heads deliberately give a tape-like bend when time changes.
    delaySamples += static_cast<float>(1.0 / (.07 * sr)) * (wanted - delaySamples);
    reverseBlend += static_cast<float>(1.0 / (.025 * sr)) * (clamp01(p[Reverse]) - reverseBlend);
    reversePhase += 1.0 / std::max(8.0f, delaySamples); if (reversePhase >= 1) reversePhase -= 1;
    const float spread = clamp01(p[Spread]);
    const float hz = 14000.0f * std::exp2(-4.4f * clamp01(p[DelayDamping]));
    const float coefficient = 1.0f - std::exp(-2.0f * static_cast<float>(pi) * hz / static_cast<float>(sr));
    const float diffusion = clamp01(p[Diffusion]) * .58f;
    const float dry[] = { safe(left), safe(right) };
    float echoes[2]{};
    for (int c = 0; c < 2; ++c) {
        const float time = delaySamples * (1.0f + (c == 1 ? .035f * spread : 0.0f));
        const float forward = delay[c].read(time);
        float reversed = 0;
        // Two half-overlapped Hann grains read backward at original speed.
        // Each grain reads the preceding Time seconds, never a reversed pointer
        // into the current write segment. Window weights sum to unity.
        for (int grain = 0; grain < 2; ++grain) {
            double ph = reversePhase + .5 * grain; if (ph >= 1) ph -= 1;
            const float window = .5f - .5f * std::cos(static_cast<float>(2 * pi * ph));
            reversed += window * delay[c].read(1.0f + 2.0f * time * static_cast<float>(ph));
        }
        const float selected = lerp(forward, reversed, reverseBlend);
        damped[c] += coefficient * (selected - damped[c]);
        // One Schroeder all-pass per channel adds actual temporal diffusion.
        const float tap = diffuser[c].read(static_cast<float>(sr) * (c == 0 ? .0131f : .0177f));
        const float diffused = tap - diffusion * damped[c];
        diffuser[c].push(safe(damped[c] + diffusion * diffused));
        const float diffusedBlend = lerp(damped[c], diffused, clamp01(p[Diffusion]));
        hpState[c] += static_cast<float>(1.0 - std::exp(-2.0 * pi * 45.0 / sr)) * (diffusedBlend - hpState[c]);
        echoes[c] = safe(diffusedBlend - hpState[c]);
    }
    const float feedback = std::clamp(p[Feedback], 0.0f, .9f);
    const float on = clamp01(p[EchoesOn]);
    // Crossfeed is a convex matrix, so its spectral radius is <= 1. Together
    // with bounded all-pass coefficients and feedback < 1 it cannot run away.
    for (int c = 0; c < 2; ++c) {
        const float loop = lerp(echoes[c], echoes[1-c], spread * .8f);
        delay[c].push(std::clamp(dry[c] * on + feedback * loop, -8.0f, 8.0f));
    }
    const float mix = clamp01(p[DelayMix]) * on;
    left = lerp(dry[0], echoes[0], mix); right = lerp(dry[1], echoes[1], mix);
}
}
