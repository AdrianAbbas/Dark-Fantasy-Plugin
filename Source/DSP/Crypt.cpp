#include "Modules.h"

namespace veil {
using namespace dsp;

namespace {
constexpr float baseSeconds[8] = { 0.0297f, 0.0371f, 0.0411f, 0.0437f, 0.0533f, 0.0613f, 0.0719f, 0.0827f };
constexpr float diffuserMs[8] = { 4.3f, 5.9f, 7.7f, 10.1f, 12.7f, 16.3f, 21.1f, 27.3f };
constexpr float lineSign[8] = { 1, -1, 1, -1, -1, 1, -1, 1 };
// The four chambers differ in structure, not just values: line lengths, number of
// input diffusers, early-reflection pattern, modulation, damping and low-mid body.
struct Chamber {
    float scale, modulationMs, dampingScale, decayScale, injection; int diffusers;
    float earlyMs[6], earlyGain[6]; float bodyDb;
};
constexpr Chamber chambers[4] = {
    // Forgotten Chapel: short lines, bright close-spaced early reflections, quick build.
    { 0.55f, 0.25f, 1.35f, 1.0f, 0.34f, 2, { 7, 11, 17, 23, 31, 41 }, { .55f, .45f, .40f, .33f, .27f, .20f }, 0 },
    // Ancient Cathedral: long lines, six diffusers (slow dense build), darker tail.
    { 1.50f, 0.50f, 0.70f, 1.0f, 0.30f, 6, { 19, 31, 47, 61, 79, 97 }, { .32f, .28f, .25f, .22f, .18f, .15f }, 0 },
    // Underground Crypt: medium lines, heavy damping, very close reflections, low-mid body.
    { 0.85f, 0.15f, 0.42f, 0.85f, 0.32f, 3, { 3, 5, 8, 11, 14, 18 }, { .60f, .50f, .45f, .40f, .33f, .28f }, 7 },
    // Endless Void: very long lines, all diffusers, deep modulation, doubled decay.
    { 2.10f, 2.20f, 0.90f, 2.0f, 0.28f, 8, { 37, 53, 71, 89, 113, 139 }, { .18f, .16f, .14f, .12f, .10f, .08f }, 0 },
};
constexpr double maxScale = 2.1 * 1.55;   // largest chamber at Space = 1
}

void Crypt::prepare(double sampleRate) {
    sr = sampleRate;
    for (int i = 0; i < lines; ++i) {
        delay[static_cast<std::size_t>(i)].prepare(static_cast<std::size_t>((baseSeconds[i] * maxScale + 0.006) * sr) + 64);   // + modulation swing
        lfoRate[static_cast<std::size_t>(i)] = 0.07f + 0.031f * static_cast<float>(i);
    }
    for (std::size_t k = 0; k < diffusers.size(); ++k) {
        diffusers[k].prepare(static_cast<std::size_t>(diffuserMs[k] * 0.001 * sr) + 8);
        diffuserLength[k] = static_cast<float>(diffuserMs[k] * 0.001 * sr);
    }
    preDelayL.prepare(static_cast<std::size_t>(0.26 * sr));
    preDelayR.prepare(static_cast<std::size_t>(0.26 * sr));
    early.prepare(static_cast<std::size_t>(0.25 * sr));
    shimmerShifter.prepare(sr, 0.08);
    for (auto& b : rising) b.prepare(static_cast<std::size_t>(8.5 * sr));
    for (auto* s : { &mix, &width, &shimmer, &inputGain, &dampingHz, &risingBlend }) s->setTime(0.04, sr);
    eternal.setTime(0.25, sr);
    preDelaySamples.setTime(0.08, sr);
    reset();
}

void Crypt::reset() {
    for (auto& d : delay) d.clear();
    for (auto& d : diffusers) d.clear();
    preDelayL.clear(); preDelayR.clear(); early.clear();
    shimmerShifter.clear();
    for (auto& b : rising) b.clear();
    for (auto& f : inputTone) f.clear();
    lowpass.fill(0); lowCut.fill(0); lfoPhase.fill(0);
    for (int i = 0; i < lines; ++i) lfoPhase[static_cast<std::size_t>(i)] = 0.125f * static_cast<float>(i);
    risePhase = 0;
    counter = 0;
    mix.value = -1;   // not primed
}

void Crypt::process(float* l, float* r, int n, const Frame& p, const Context&) noexcept {
    character = std::clamp(static_cast<int>(std::lround(p[Character])), 0, 3);
    const auto& chamber = chambers[character];
    diffuserCount = chamber.diffusers;
    modulationDepth = static_cast<float>(chamber.modulationMs * 0.001 * sr);
    const float space = clamp01(p[Space]);
    const float scale = chamber.scale * (0.45f + 1.1f * space);
    // Whole-sample lengths: with modulation faded out (Eternal) the loop reads exact
    // samples, so nothing is lost to interpolation and the tail can hold forever.
    for (int i = 0; i < lines; ++i) targetLength[static_cast<std::size_t>(i)] = std::round(static_cast<float>(baseSeconds[i] * scale * sr));
    mix.set(clamp01(p[ReverbMix]));
    width.set(clamp01(p[ReverbWidth]));
    shimmer.set(clamp01(p[Shimmer]));
    eternal.set(p[Eternal] >= 0.5f ? 1.0f : 0.0f);
    inputGain.set(p[Eternal] >= 0.5f ? 0.0f : 1.0f);   // Eternal holds the tail: no new input
    dampingHz.set(std::clamp((17000.0f - 15500.0f * std::pow(clamp01(p[Damping]), 0.7f)) * chamber.dampingScale, 300.0f, static_cast<float>(sr * 0.45)));
    preDelaySamples.set(static_cast<float>(std::clamp(p[PreDelay], 0.0f, 250.0f) * 0.001 * sr));
    risingBlend.set(p[CryptMode] >= 0.5f ? 1.0f : 0.0f);
    const float riseTarget = static_cast<float>(std::clamp(p[RiseTime], 0.1f, 4.0f) * sr);
    if (mix.value < 0) {
        for (auto* s : { &mix, &width, &shimmer, &eternal, &inputGain, &dampingHz, &preDelaySamples, &risingBlend }) s->snap(s->target);
        length = targetLength;
        riseLength = riseTarget;
    }
    // Per-chunk loop gains (T60) and damping; Eternal pushes both to "hold".
    const float e = eternal.skip(0);
    const float t60 = std::clamp(p[Decay] * chamber.decayScale, 0.2f, 60.0f);
    float meanGain = 0;
    for (int i = 0; i < lines; ++i) {
        const auto I = static_cast<std::size_t>(i);
        const float decaying = static_cast<float>(std::pow(10.0, -3.0 * length[I] / (sr * t60)));
        gain[I] = std::min(0.99999f, lerp(decaying, 0.999995f, e));
        meanGain += decaying / lines;
    }
    // Inject less into long, dense tails so the level stays usable across Decay.
    injection = chamber.injection * 1.7f * std::pow(std::max(0.02f, 1.0f - meanGain * meanGain), 0.3f);
    const float dampCoefficient = lerp(poleForHz(dampingHz.skip(0), sr), 1.0f, e);
    const float lowCutCoefficient = poleForHz(std::clamp(p[ReverbLowCut], 10.0f, 2000.0f), sr);
    for (auto& f : inputTone) { const auto z1 = f.z1, z2 = f.z2; f.peaking(sr, 210.0, 0.9, chamber.bodyDb); f.z1 = z1; f.z2 = z2; }
    const float earlyScale = static_cast<float>((0.7 + 0.6 * space) * 0.001 * sr);

    for (int i = 0; i < n; ++i) {
        const float m = mix.next(), w = width.next(), shim = shimmer.next(), in = inputGain.next(), rise = risingBlend.next();
        const float hold = eternal.next(); dampingHz.next();
        // Eternal also fades out the two things that would slowly eat a held tail:
        // delay modulation (interpolation loss) and shimmer (it replaces loop energy).
        const float depth = modulationDepth * (1.0f - hold);
        const float pre = preDelaySamples.next();
        riseLength += 0.0002f * (riseTarget - riseLength);
        const float dryL = finiteOr(l[i]), dryR = finiteOr(r[i]);
        // Input: low cut, chamber body, pre-delay.
        float xl = dryL, xr = dryR;
        lowCut[0] += lowCutCoefficient * (xl - lowCut[0]); xl -= lowCut[0];
        lowCut[1] += lowCutCoefficient * (xr - lowCut[1]); xr -= lowCut[1];
        xl = inputTone[0].process(xl); xr = inputTone[1].process(xr);
        preDelayL.push(xl); preDelayR.push(xr);
        const float pl = preDelayL.read(1.0f + pre), pr = preDelayR.read(1.0f + pre);
        const float mid = 0.5f * (pl + pr), side = 0.5f * (pl - pr);
        // Early reflections (character pattern).
        early.push(mid);
        float er = 0;
        for (int k = 0; k < 6; ++k) er += chamber.earlyGain[k] * early.read(chamber.earlyMs[k] * earlyScale);
        // Input diffusion (Schroeder all-passes); more stages = slower, denser build.
        float d = mid;
        for (int k = 0; k < diffuserCount; ++k) {
            auto& ap = diffusers[static_cast<std::size_t>(k)];
            const float delayed = ap.read(diffuserLength[static_cast<std::size_t>(k)]);
            const float out = -0.62f * d + delayed;
            ap.push(d + 0.62f * out);
            d = out;
        }
        // Feedback delay network.
        std::array<float, lines> o{};
        float sum = 0;
        for (int k = 0; k < lines; ++k) {
            const auto K = static_cast<std::size_t>(k);
            lfoPhase[K] += lfoRate[K] / static_cast<float>(sr);
            if (lfoPhase[K] >= 1) lfoPhase[K] -= 1;
            length[K] += 0.0004f * (targetLength[K] - length[K]);
            if (std::abs(targetLength[K] - length[K]) < 0.001f) length[K] = targetLength[K];
            const float tap = delay[K].read(length[K] + depth * (1.0f + std::sin(twoPi * lfoPhase[K])));
            lowpass[K] += dampCoefficient * (tap - lowpass[K]);
            o[K] = lowpass[K];
            sum += o[K];
        }
        // Shimmer: an octave-up copy blended into the loop (convex, limited).
        const float shimmerAmount = 0.35f * shim * (1.0f - hold);
        const float shimmered = shimmerAmount > 0.0001f ? softLimit(shimmerShifter.process(0.5f * (o[0] + o[3]), 2.0f), 1.0f) : 0.0f;
        for (int k = 0; k < lines; ++k) {
            const auto K = static_cast<std::size_t>(k);
            float y = o[K] - 0.25f * sum;   // Householder reflection, orthogonal
            y = (1.0f - shimmerAmount) * y + shimmerAmount * shimmered * lineSign[k] * 0.7f;
            const float inject = injection * in * (d * lineSign[k] + side * ((k & 1) ? -0.6f : 0.6f));
            delay[K].push(softLimit(gain[K] * y + inject, 3.0f));
        }
        float wetL = 0.5f * (o[0] - o[2] + o[4] - o[6]) + 0.25f * (o[1] + o[5]) + er * 0.55f;
        float wetR = 0.5f * (o[1] - o[3] + o[5] - o[7]) + 0.25f * (o[2] + o[6]) + er * 0.5f;
        const float wm = 0.5f * (wetL + wetR), ws = 0.5f * (wetL - wetR) * w;
        wetL = wm + ws; wetR = wm - ws;
        // Rising: play the reverb backwards in overlapping segments, so it swells up.
        rising[0].push(wetL); rising[1].push(wetR);
        if (rise > 0.0001f) {
            risePhase += 1.0 / std::max(256.0f, riseLength);
            if (risePhase >= 1) risePhase -= 1;
            float revL = 0, revR = 0;
            for (int h = 0; h < 2; ++h) {
                double ph = risePhase + 0.5 * h;
                if (ph >= 1) ph -= 1;
                const float wnd = 0.5f - 0.5f * std::cos(twoPi * static_cast<float>(ph));
                const float at = 1.0f + 2.0f * riseLength * static_cast<float>(ph);
                revL += wnd * rising[0].read(at); revR += wnd * rising[1].read(at);
            }
            wetL = lerp(wetL, revL * 1.3f, rise);
            wetR = lerp(wetR, revR * 1.3f, rise);
        }
        float dryGain, wetGain;
        equalPowerGains(m, dryGain, wetGain);
        l[i] = finiteOr(dryL * dryGain + wetL * wetGain);
        r[i] = finiteOr(dryR * dryGain + wetR * wetGain);
    }
}
}
