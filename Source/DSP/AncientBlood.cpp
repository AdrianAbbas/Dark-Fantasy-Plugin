#include "Modules.h"
#include <juce_dsp/juce_dsp.h>

namespace veil {
using namespace dsp;

namespace {
// Soft tape-like curve and a harder asymmetric clip (even harmonics), morphed by Texture.
inline float shape(float x, float texture) noexcept {
    const float soft = std::tanh(x);
    const float hard = x >= 0 ? softLimit(x, 1.0f) : -softLimit(-1.6f * x, 0.6f);
    return soft + texture * (hard - soft);
}
constexpr double wowMaxSeconds = 0.0025;   // centre of the wow delay; part of the reported latency
}

AncientBlood::AncientBlood() = default;
AncientBlood::~AncientBlood() = default;

void AncientBlood::prepare(double sampleRate) {
    sr = sampleRate;
    oversampler = std::make_unique<juce::dsp::Oversampling<float>>(2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    oversampler->initProcessing(static_cast<std::size_t>(maxChunk));
    const int wowCentre = static_cast<int>(std::lround(wowMaxSeconds * sr));
    latency = static_cast<int>(std::lround(oversampler->getLatencyInSamples())) + wowCentre;
    for (auto& d : dryDelay) d.prepare(static_cast<std::size_t>(latency + 8));
    for (auto& w : wow) w.prepare(static_cast<std::size_t>(wowCentre * 2 + 16));
    for (auto& w : wet) w.assign(static_cast<std::size_t>(maxChunk), 0.0f);
    for (auto& d : dc) d.prepare(sr);
    // Loudness compensation: 80 ms RMS windows, gain glides over ~50 ms (updated every 16 samples).
    msCoefficient = timeCoefficient(0.08, sr);
    compensationCoefficient = static_cast<float>(1.0 - std::exp(-16.0 / (0.05 * sr)));
    for (auto* s : { &drive, &texture, &age, &dust, &instability, &mix }) s->setTime(0.03, sr);
    enable.setTime(0.007, sr);   // ~20 ms to settle
    reset();
}

void AncientBlood::reset() {
    if (oversampler) oversampler->reset();
    for (auto& d : dryDelay) d.clear();
    for (auto& w : wow) w.clear();
    for (auto& f : ageFilter) f.clear();
    for (auto& d : dc) d.clear();
    held.fill(0); inMs.fill(0); outMs.fill(0); compensation.fill(1); hiss.fill(0);
    holdPhase = wowPhase = drift = driftTarget = dustEnvelope = crackle = 0;
    driftCountdown = 0;
    random.seed(0x8ad02ec1u);
    drive.value = -1000;   // not primed
}

void AncientBlood::process(float* l, float* r, int n, const Frame& p, const Context&) noexcept {
    drive.set(std::clamp(p[Drive], 0.0f, 48.0f));
    texture.set(clamp01(p[Texture]));
    age.set(clamp01(p[Age]));
    dust.set(clamp01(p[Dust]));
    instability.set(clamp01(p[Instability]));
    mix.set(clamp01(p[BloodMix]));
    enable.set(p[BloodOn] >= 0.5f ? 1.0f : 0.0f);
    if (drive.value < -100) {
        for (auto* s : { &drive, &texture, &age, &dust, &instability, &mix, &enable }) s->snap(s->target);
    }
    float* io[] = { l, r };
    const float wowCentre = static_cast<float>(wowMaxSeconds * sr);
    std::array<std::array<float, maxChunk>, 2> dryAligned{};

    // The aligned dry path always runs, so latency never depends on settings.
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i) {
            const float x = finiteOr(io[ch][i]);
            dryDelay[ch].push(x);
            dryAligned[ch][static_cast<std::size_t>(i)] = dryDelay[ch].readInteger(static_cast<std::size_t>(latency) + 1);
        }

    const bool silentModule = enable.value < 1.0e-5f && enable.target == 0;
    if (silentModule) {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i) { wow[ch].push(finiteOr(io[ch][i])); io[ch][i] = dryAligned[ch][static_cast<std::size_t>(i)]; }
        enable.skip(n);
        return;
    }

    // 1. Instability and Age wow: a modulated delay around a fixed centre, plus slow level drift.
    const float inst = instability.target, ageAmount = age.target;
    const float depth = wowCentre * std::min(0.95f, inst * 0.9f + ageAmount * 0.25f);
    for (int i = 0; i < n; ++i) {
        wowPhase += static_cast<float>((0.45 + 5.5 * inst * inst) / sr);
        if (wowPhase >= 1) wowPhase -= 1;
        if (--driftCountdown <= 0) { driftTarget = random.bipolar(); driftCountdown = static_cast<int>(sr * 0.23); }
        drift += static_cast<float>(1.0 / (0.12 * sr)) * (driftTarget - drift);
        const float offset = wowCentre + depth * (0.7f * std::sin(twoPi * wowPhase) + 0.3f * drift);
        const float level = 1.0f + inst * 0.25f * drift;
        for (int ch = 0; ch < 2; ++ch) {
            wow[ch].push(finiteOr(io[ch][i]));
            wet[ch][static_cast<std::size_t>(i)] = wow[ch].read(std::max(1.0f, offset)) * level;
        }
    }
    // Loudness reference before drive (for compensation).
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i) { const float v = wet[ch][static_cast<std::size_t>(i)]; inMs[ch] += msCoefficient * (v * v - inMs[ch]); }

    // 2. Oversampled nonlinearity.
    float* pointers[] = { wet[0].data(), wet[1].data() };
    juce::dsp::AudioBlock<float> block(pointers, 2, static_cast<std::size_t>(n));
    auto up = oversampler->processSamplesUp(block);
    const float driveGain = dbToGain(drive.target), tex = texture.target;
    for (std::size_t ch = 0; ch < up.getNumChannels(); ++ch) {
        auto* d = up.getChannelPointer(ch);
        for (std::size_t i = 0; i < up.getNumSamples(); ++i) d[i] = shape(d[i] * driveGain, tex);
    }
    oversampler->processSamplesDown(block);

    // 3. Loudness compensation, Age (bandwidth, bits, sample rate), Dust; mix with aligned dry.
    const float ageCubed = ageAmount * ageAmount * ageAmount;
    const double ageHz = 20000.0 * std::exp2(-5.0 * ageAmount);
    {
        // Two-pole low-pass for Age (RBJ, Q 0.707).
        const double w = 2 * pi * std::min(ageHz, sr * 0.45) / sr, alpha = std::sin(w) / (2 * 0.7071), cs = std::cos(w);
        for (auto& f : ageFilter) { const auto z1 = f.z1, z2 = f.z2; f.setNormalised((1 - cs) / 2, 1 - cs, (1 - cs) / 2, 1 + alpha, -2 * cs, 1 - alpha); f.z1 = z1; f.z2 = z2; }
    }
    const float bits = 16.0f - 12.0f * std::pow(ageAmount, 1.5f);
    const float levels = std::exp2(bits), bitMix = ageAmount * ageAmount;
    const float holdIncrement = static_cast<float>(1.0 / (1.0 + 15.0 * ageCubed));
    const float dustAmount = dust.target;
    for (int i = 0; i < n; ++i) {
        const float m = mix.next(), e = enable.next();
        drive.next(); texture.next(); age.next(); dust.next(); instability.next();
        holdPhase += holdIncrement;
        const bool update = holdPhase >= 1.0f;
        if (update) holdPhase -= 1.0f;
        const float inputPeak = std::max(std::abs(dryAligned[0][static_cast<std::size_t>(i)]), std::abs(dryAligned[1][static_cast<std::size_t>(i)]));
        dustEnvelope += (inputPeak > dustEnvelope ? 0.01f : 0.00002f) * (inputPeak - dustEnvelope);
        float crackleSample = 0;
        if (dustAmount > 0.0001f && random.unit() < dustAmount * dustAmount * 0.0025f)
            crackle = (random.unit() * 0.5f + 0.15f) * dustAmount * (random.bipolar() > 0 ? 1.0f : -1.0f);
        crackleSample = crackle; crackle *= 0.55f;
        for (int ch = 0; ch < 2; ++ch) {
            const auto idx = static_cast<std::size_t>(i);
            float y = dc[ch].process(wet[ch][idx]);
            outMs[ch] += msCoefficient * (y * y - outMs[ch]);
            if ((i & 15) == 0) {
                const float target = outMs[ch] > 1.0e-12f ? std::clamp(std::sqrt(inMs[ch] / outMs[ch]), 0.05f, 2.0f) : 1.0f;
                compensation[ch] += compensationCoefficient * (target - compensation[ch]);
            }
            y *= compensation[ch];
            y = ageFilter[ch].process(y);
            if (update) held[ch] = std::round(y * levels) / levels;
            y += bitMix * (held[ch] - y);
            if (dustAmount > 0.0001f) {
                hiss[ch] += 0.35f * (random.bipolar() - hiss[ch]);
                y += (hiss[ch] * 0.012f * dustAmount + crackleSample) * std::min(1.0f, dustEnvelope * 30.0f);
            }
            const float dry = dryAligned[ch][idx];
            io[ch][i] = finiteOr(dry + (softLimit(y, 1.5f) - dry) * m * e);
        }
    }
}
}
