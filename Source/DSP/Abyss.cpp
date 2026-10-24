#include "Modules.h"

namespace veil {
using namespace dsp;

namespace {
constexpr double syncBeats[] = { 0, 1, 2, 4, 8, 16 };   // Free, 1/4, 1/2, 1 bar, 2 bars, 4 bars
}

void Abyss::prepare(double sampleRate) {
    sr = sampleRate;
    for (auto& channel : combs)
        for (auto& comb : channel) comb.prepare(static_cast<std::size_t>(sr / 30.0) + 16);   // lowest note C1 = 32.7 Hz
    for (auto* s : { &logCutoff, &resonance, &highPass, &darkness, &movement, &slopeBlend, &tombAmount, &tombFeedback })
        s->setTime(0.03, sr);
    tombPeriod.setTime(0.06, sr);
    reset();
}

void Abyss::reset() {
    phase = 0;
    counter = 0;
    compensation = 1;
    for (auto* filters : { &hp, &lp1, &lp2 }) for (auto& f : *filters) f.clear();
    for (auto& f : shelf) f.clear();
    for (auto& f : warmth) f.clear();
    for (auto& channel : combs) for (auto& comb : channel) comb.clear();
    for (auto& channel : combDamp) channel.fill(0);
    logCutoff.value = -1;   // marks "not primed": the first chunk snaps to its targets
}

void Abyss::process(float* l, float* r, int n, const Frame& p, const Context& c) noexcept {
    const auto sync = static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(p[FilterSync])), 0, 5));
    const double rate = sync == 0 ? p[FilterRate] : 1.0 / beatsToSeconds(syncBeats[sync], c.bpm);
    const float tombHz = static_cast<float>(noteFrequency(std::clamp(static_cast<int>(std::lround(p[TombNote])), 0, 11),
                                                          1 + std::clamp(static_cast<int>(std::lround(p[TombOctave])), 0, 4)));
    logCutoff.set(std::log2(std::clamp(p[Cutoff], 20.0f, 22000.0f)));
    resonance.set(clamp01(p[Resonance]));
    highPass.set(std::clamp(p[HighPass], 10.0f, 4000.0f));
    darkness.set(clamp01(p[Darkness]));
    movement.set(clamp01(p[FilterMovement]));
    slopeBlend.set(p[Slope] >= 0.5f ? 1.0f : 0.0f);
    tombAmount.set(clamp01(p[Tomb]));
    // Ring time: feedback 0.80 (short knock) .. 0.996 (long stone ring).
    tombFeedback.set(0.80f + 0.196f * clamp01(p[TombRing]));
    tombPeriod.set(static_cast<float>(sr) / tombHz);
    if (logCutoff.value < 0) {
        for (auto* s : { &logCutoff, &resonance, &highPass, &darkness, &movement, &slopeBlend, &tombAmount, &tombFeedback, &tombPeriod })
            s->snap(s->target);
        counter = 0;
    }

    // Darkness: high-shelf cut and low-mid warmth, updated per chunk.
    const float dark = darkness.skip(0);
    for (auto& f : shelf) { const auto z1 = f.z1, z2 = f.z2; f.highShelf(sr, 2600.0, -20.0 * dark); f.z1 = z1; f.z2 = z2; }
    for (auto& f : warmth) { const auto z1 = f.z1, z2 = f.z2; f.peaking(sr, 230.0, 0.8, 5.0 * dark); f.z1 = z1; f.z2 = z2; }
    tombDampCoefficient = poleForHz(9000.0 - 5500.0 * clamp01(p[TombRing]), sr);
    const float loopFilterDelay = (1.0f - tombDampCoefficient) / tombDampCoefficient;

    float* channels[] = { l, r };
    for (int i = 0; i < n; ++i) {
        logCutoff.next(); resonance.next(); highPass.next(); darkness.next(); movement.next(); slopeBlend.next();
        tombAmount.next(); tombFeedback.next(); tombPeriod.next();
        phase += rate / sr;
        if (phase >= 1) phase -= 1;
        if (counter-- <= 0) {
            counter = 15;
            // Darkness lowers the cutoff up to 2.2 octaves; Movement sweeps up to +-2.5 octaves.
            const double lfo = std::sin(2.0 * pi * phase);
            const double hz = std::exp2(logCutoff.value - 2.2 * darkness.value + 2.5 * movement.value * lfo);
            const float q = 0.5f + 11.5f * resonance.value * resonance.value;
            compensation = 1.0f / (1.0f + 0.08f * std::max(0.0f, q - 0.707f));
            for (int ch = 0; ch < 2; ++ch) {
                lp1[ch].set(hz, sr, 1.0f / q);
                lp2[ch].set(hz, sr, 1.41421356f);
                hp[ch].set(highPass.value, sr, 1.41421356f);
            }
        }
        const float tomb = tombAmount.value;
        for (int ch = 0; ch < 2; ++ch) {
            float x = finiteOr(channels[ch][i]);
            x = hp[ch].highPass(x);
            const float first = lp1[ch].low(x);
            const float second = lp2[ch].low(first);
            float y = softLimit(lerp(first, second, slopeBlend.value) * compensation, 2.0f);
            y = warmth[ch].process(shelf[ch].process(y));
            if (tomb > 0.0001f) {
                // Two tuned feedback combs (fundamental and a slightly sharp octave) with
                // damping in the loop: pitched, stone-like ringing.
                float wet = 0;
                const float feedback = tombFeedback.value;
                for (int k = 0; k < 2; ++k) {
                    const float period = (k == 0 ? tombPeriod.value : tombPeriod.value * 0.4985f) - loopFilterDelay;
                    const float tap = combs[ch][k].read(std::max(2.0f, period));
                    combDamp[ch][k] += tombDampCoefficient * (tap - combDamp[ch][k]);
                    combs[ch][k].push(softLimit(y * (1.0f - feedback) * 2.0f + combDamp[ch][k] * feedback, 4.0f));
                    wet += combDamp[ch][k] * (k == 0 ? 1.0f : 0.6f);
                }
                y = y * (1.0f - 0.7f * tomb) + wet * 1.6f * tomb;
            }
            channels[ch][i] = finiteOr(y);
        }
    }
}
}
