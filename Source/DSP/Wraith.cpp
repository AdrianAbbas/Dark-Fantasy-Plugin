#include "Modules.h"
#include <juce_dsp/juce_dsp.h>

namespace veil {
struct WraithFft {
    juce::dsp::FFT fft;
    explicit WraithFft(int order) : fft(order) {}
};

using namespace dsp;

namespace {
inline float wrapPhase(float x) noexcept {
    constexpr float p = static_cast<float>(pi), tp = 2.0f * static_cast<float>(pi);
    x = std::fmod(x + p, tp);
    if (x < 0) x += tp;
    return x - p;
}
}

Wraith::Wraith() = default;
Wraith::~Wraith() = default;

void Wraith::prepare(double sampleRate) {
    sr = sampleRate;
    // About 43 ms frames at any rate: 2048 at 44.1/48 kHz, 4096 at 88.2/96, 8192 above.
    order = sr <= 50000 ? 11 : (sr <= 100000 ? 12 : 13);
    size = 1 << order;
    hop = size / 4;
    fft = std::make_unique<WraithFft>(order);
    const auto bins = static_cast<std::size_t>(size / 2 + 1);
    for (auto& c : channels) {
        c.inFifo.assign(static_cast<std::size_t>(size), 0.0f);
        c.outAccum.assign(static_cast<std::size_t>(size), 0.0f);
        c.frame.assign(static_cast<std::size_t>(size) * 2, 0.0f);
        c.window.resize(static_cast<std::size_t>(size));
        for (int i = 0; i < size; ++i)
            c.window[static_cast<std::size_t>(i)] = 0.5f - 0.5f * std::cos(twoPi * static_cast<float>(i) / static_cast<float>(size));
        for (auto* v : { &c.lastPhase, &c.sumPhase, &c.ghostPhase, &c.smoothMag, &c.frozenMag, &c.mag, &c.freq, &c.synthMag, &c.synthFreq, &c.blurred })
            v->assign(bins, 0.0f);
        c.ghostRing.assign(bins * static_cast<std::size_t>(ghostFrames), 0.0f);
    }
    // Measure the FFT engine's round-trip gain instead of assuming a convention.
    auto& probe = channels[0].frame;
    std::fill(probe.begin(), probe.end(), 0.0f);
    probe[3] = 1.0f;
    fft->fft.performRealOnlyForwardTransform(probe.data(), true);
    fft->fft.performRealOnlyInverseTransform(probe.data());
    const float roundTrip = std::abs(probe[3]) > 1.0e-9f ? probe[3] : 1.0f;
    // Hann analysis x Hann synthesis at 4x overlap sums to 1.5.
    scale = 1.0f / (roundTrip * 1.5f);
    mix.setTime(0.04, sr);
    reset();
}

void Wraith::reset() {
    for (auto& c : channels) {
        std::fill(c.inFifo.begin(), c.inFifo.end(), 0.0f);
        std::fill(c.outAccum.begin(), c.outAccum.end(), 0.0f);
        for (auto* v : { &c.lastPhase, &c.sumPhase, &c.ghostPhase, &c.smoothMag, &c.frozenMag, &c.mag, &c.freq, &c.synthMag, &c.synthFreq, &c.blurred })
            std::fill(v->begin(), v->end(), 0.0f);
        std::fill(c.ghostRing.begin(), c.ghostRing.end(), 0.0f);
        c.ghostWrite = 0;
        c.captured = false;
    }
    fifoPos = hopCounter = 0;
    freezeAmount = 0;
    driftValue = driftTarget = 0;
    framesSinceDrift = 0;
    random.seed(0x77726169u);
    mix.value = -1;   // not primed
}

void Wraith::processFrame(Channel& c, int channelIndex) noexcept {
    const int bins = size / 2 + 1;
    const float expected = twoPi * static_cast<float>(hop) / static_cast<float>(size);   // phase advance per bin per hop
    auto& f = c.frame;
    for (int i = 0; i < size; ++i)
        f[static_cast<std::size_t>(i)] = c.inFifo[static_cast<std::size_t>((fifoPos + i) % size)] * c.window[static_cast<std::size_t>(i)];
    std::fill(f.begin() + size, f.end(), 0.0f);
    fft->fft.performRealOnlyForwardTransform(f.data(), true);

    // Analysis: magnitude and true frequency (in bins) from the phase advance.
    for (int k = 0; k < bins; ++k) {
        const auto K = static_cast<std::size_t>(k);
        const float re = f[2 * K], im = f[2 * K + 1];
        const float phase = std::atan2(im, re);
        const float deviation = wrapPhase(phase - c.lastPhase[K] - static_cast<float>(k) * expected);
        c.lastPhase[K] = phase;
        c.mag[K] = std::sqrt(re * re + im * im);
        c.freq[K] = static_cast<float>(k) + deviation / expected;
    }
    // Blur: temporal smoothing plus a 5-tap smear across neighbouring bins.
    const float temporal = blur * 0.94f, spectral = blur;
    for (int k = 0; k < bins; ++k) {
        const auto K = static_cast<std::size_t>(k);
        c.smoothMag[K] = temporal * c.smoothMag[K] + (1.0f - temporal) * c.mag[K];
    }
    for (int k = 0; k < bins; ++k) {
        auto at = [&](int j) { return c.smoothMag[static_cast<std::size_t>(std::clamp(j, 0, bins - 1))]; };
        c.blurred[static_cast<std::size_t>(k)] = at(k) * (1.0f - 0.6f * spectral) + 0.2f * spectral * (at(k - 1) + at(k + 1)) + 0.1f * spectral * (at(k - 2) + at(k + 2));
    }
    // Freeze: take the spectrum once per engagement and crossfade towards it.
    if (freezeTarget > 0.5f && !c.captured) { std::copy(c.blurred.begin(), c.blurred.end(), c.frozenMag.begin()); c.captured = true; }
    if (freezeTarget < 0.5f && freezeAmount <= 0.0f) c.captured = false;
    const float fz = freezeAmount;
    for (int k = 0; k < bins; ++k) {
        const auto K = static_cast<std::size_t>(k);
        c.blurred[K] = c.blurred[K] * (1.0f - fz) + c.frozenMag[K] * fz;
    }
    // Ghost ring keeps recent frames; the ghost reads an older one.
    std::copy(c.blurred.begin(), c.blurred.end(), c.ghostRing.begin() + static_cast<std::ptrdiff_t>(c.ghostWrite) * bins);

    // Main synthesis, pitch-shifted by `ratio` (phase-vocoder bin remapping).
    std::fill(c.synthMag.begin(), c.synthMag.end(), 0.0f);
    std::fill(c.synthFreq.begin(), c.synthFreq.end(), 0.0f);
    for (int k = 0; k < bins; ++k) {
        const int j = static_cast<int>(static_cast<float>(k) * ratio + 0.5f);
        if (j >= bins) break;
        const auto J = static_cast<std::size_t>(j), K = static_cast<std::size_t>(k);
        c.synthMag[J] += c.blurred[K];
        c.synthFreq[J] = c.freq[K] * ratio;
    }
    for (int j = 0; j < bins; ++j) {
        const auto J = static_cast<std::size_t>(j);
        float advance = c.synthFreq[J] * expected;
        if (fz > 0.0f) advance = advance + fz * (static_cast<float>(j) * ratio * expected - advance) + fz * 0.9f * random.bipolar();
        c.sumPhase[J] = wrapPhase(c.sumPhase[J] + advance);
        f[2 * J] = c.synthMag[J] * std::cos(c.sumPhase[J]);
        f[2 * J + 1] = c.synthMag[J] * std::sin(c.sumPhase[J]);
    }
    // Ghost: an older frame, an octave up, with loose phases (a pale apparition).
    if (ghost > 0.001f) {
        const int delayFrames = 6 + static_cast<int>(ghost * static_cast<float>(ghostFrames - 8));
        const int slot = (c.ghostWrite - delayFrames + ghostFrames * 2) % ghostFrames;
        const float* past = c.ghostRing.data() + static_cast<std::ptrdiff_t>(slot) * bins;
        // Up to twice the source level at Ghost = 100 %: the ghost can overtake the original.
        const float level = ghost * 1.9f;
        for (int k = 1; k < bins; ++k) {
            const int j = static_cast<int>(static_cast<float>(k) * 2.004f * ratio + 0.5f);
            if (j >= bins) break;
            const auto J = static_cast<std::size_t>(j);
            c.ghostPhase[J] = wrapPhase(c.ghostPhase[J] + static_cast<float>(j) * expected + 0.3f * random.bipolar());
            const float a = past[k] * level;
            f[2 * J] += a * std::cos(c.ghostPhase[J]);
            f[2 * J + 1] += a * std::sin(c.ghostPhase[J]);
        }
    }
    c.ghostWrite = (c.ghostWrite + 1) % ghostFrames;
    f[1] = 0;
    f[static_cast<std::size_t>(2 * (bins - 1) + 1)] = 0;
    fft->fft.performRealOnlyInverseTransform(f.data());
    for (int i = 0; i < size; ++i) {
        auto& out = c.outAccum[static_cast<std::size_t>((fifoPos + i) % size)];
        out += f[static_cast<std::size_t>(i)] * c.window[static_cast<std::size_t>(i)] * scale;
    }
    (void) channelIndex;
}

void Wraith::process(float* l, float* r, int n, const Frame& p, const Context&) noexcept {
    mix.set(clamp01(p[WraithMix]));
    if (mix.value < 0) {
        mix.snap(mix.target);
        blur = clamp01(p[Blur]); ghost = clamp01(p[Ghost]);
    }
    blurTarget = clamp01(p[Blur]);
    ghostTarget = clamp01(p[Ghost]);
    detuneCents = std::clamp(p[WraithDetune], 0.0f, 200.0f);
    drift = clamp01(p[Drift]);
    freezeTarget = p[Freeze] >= 0.5f ? 1.0f : 0.0f;
    const float frameSeconds = static_cast<float>(hop / sr);
    float* io[] = { l, r };
    for (int i = 0; i < n; ++i) {
        const float m = mix.next();
        float wet[2];
        for (int ch = 0; ch < 2; ++ch) {
            auto& c = channels[static_cast<std::size_t>(ch)];
            c.inFifo[static_cast<std::size_t>(fifoPos)] = finiteOr(io[ch][i]);
            wet[ch] = c.outAccum[static_cast<std::size_t>(fifoPos)];
            c.outAccum[static_cast<std::size_t>(fifoPos)] = 0;
        }
        fifoPos = (fifoPos + 1) % size;
        if (++hopCounter >= hop) {
            hopCounter = 0;
            // Per-frame control: glide blur/ghost, slow random drift of the detune.
            blur += 0.25f * (blurTarget - blur);
            ghost += 0.25f * (ghostTarget - ghost);
            if (++framesSinceDrift >= nextDrift) {
                framesSinceDrift = 0;
                nextDrift = 8 + static_cast<int>(random.unit() * 16.0f);
                driftTarget = random.bipolar();
            }
            driftValue += 0.08f * (driftTarget - driftValue);
            const float cents = detuneCents * (1.0f - 0.5f * drift) + detuneCents * drift * driftValue;
            ratio = std::exp2(cents / 1200.0f);
            const float step = frameSeconds / 0.1f;   // ~100 ms freeze fades
            freezeAmount = freezeTarget > freezeAmount ? std::min(freezeTarget, freezeAmount + step)
                                                       : std::max(freezeTarget, freezeAmount - step);
            processFrame(channels[0], 0);
            processFrame(channels[1], 1);
        }
        float dryGain, wetGain;
        equalPowerGains(m, dryGain, wetGain);
        for (int ch = 0; ch < 2; ++ch) io[ch][i] = finiteOr(io[ch][i] * dryGain + softLimit(wet[ch], 2.0f) * wetGain);
    }
}
}
