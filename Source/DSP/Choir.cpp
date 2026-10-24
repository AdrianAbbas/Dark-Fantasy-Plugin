#include "Modules.h"

namespace veil {
using namespace dsp;

namespace {
constexpr float intervalSemitones[] = { -12, -7, 5, 7, 12, 19 };
constexpr float voiceDetune[] = { -1.0f, 1.0f, -0.45f, 0.55f };
constexpr float voicePan[] = { -0.75f, 0.75f, -0.3f, 0.35f };
// Vowel formants (F1, F2, F3) for "oo", "oh", "ah"; Formant morphs across them.
constexpr float vowels[3][3] = { { 300, 870, 2240 }, { 570, 840, 2410 }, { 730, 1090, 2440 } };
constexpr float formantGain[3] = { 1.0f, 0.55f, 0.32f };
constexpr double droneGrainSeconds = 0.26, droneCaptureSeconds = 2.0;
void retune(Biquad& f, double sr, double hz, double q) noexcept {
    const auto z1 = f.z1, z2 = f.z2;
    f.bandPass(sr, hz, q);
    f.z1 = z1; f.z2 = z2;
}
}

void Choir::prepare(double sampleRate) {
    sr = sampleRate;
    for (std::size_t v = 0; v < voices.size(); ++v) voices[v].prepare(sr, 0.055 + 0.013 * static_cast<double>(v));
    droneShifter.prepare(sr, 0.09);
    capture.assign(static_cast<std::size_t>(droneCaptureSeconds * sr), 0.0f);
    for (auto* s : { &blend, &detune, &movement, &droneLevel, &formant }) s->setTime(0.04, sr);
    reset();
}

void Choir::reset() {
    for (std::size_t v = 0; v < voices.size(); ++v) { voices[v].clear(); voices[v].setPhase(0.23 * static_cast<double>(v)); }
    droneShifter.clear();
    std::fill(capture.begin(), capture.end(), 0.0f);
    captureWrite = captureFrozenAt = 0;
    voiceGain.fill(0);
    for (auto& ch : formants) for (auto& f : ch) f.clear();
    for (auto& f : choirTone) f.clear();
    for (auto& f : droneTone) f.clear();
    inputLowpass.clear();
    for (std::size_t g = 0; g < grains.size(); ++g) grains[g] = { 0, static_cast<float>(g) / static_cast<float>(grains.size()), 1, 0, 1 };
    inputEnvelope = holdEnvelope = droneLevelTrack = 0;
    captureActive = false;
    random.seed(0x63686f72u);
    lfoPhase = 0;
    counter = 0;
    ratios.fill(1);
    formantCached = -1;
    blend.value = -1;   // not primed
}

void Choir::updateFormants(float value) noexcept {
    formantCached = value;
    const float position = clamp01(value) * 2.0f;
    const int a = std::min(1, static_cast<int>(position));
    const float t = position - static_cast<float>(a);
    for (int k = 0; k < 3; ++k) {
        const double hz = lerp(vowels[a][k], vowels[a + 1][k], t);
        for (auto& ch : formants) retune(ch[static_cast<std::size_t>(k)], sr, hz, 5.0 + 2.0 * k);
    }
    for (auto& f : choirTone) { const auto z1 = f.z1, z2 = f.z2; f.highShelf(sr, 5000.0, -9.0); f.z1 = z1; f.z2 = z2; }
    for (auto& f : droneTone) { const auto z1 = f.z1, z2 = f.z2; f.highShelf(sr, 1400.0, -14.0); f.z1 = z1; f.z2 = z2; }
}

void Choir::process(float* l, float* r, int n, const Frame& p, const Context&) noexcept {
    const auto interval = static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(p[Interval])), 0, 5));
    const int voiceCount = std::clamp(static_cast<int>(std::lround(p[Voices])), 1, 4);
    blend.set(clamp01(p[ChoirBlend]));
    detune.set(std::clamp(p[ChoirDetune], 0.0f, 100.0f));
    movement.set(clamp01(p[ChoirMovement]));
    droneLevel.set(clamp01(p[Drone]));
    formant.set(clamp01(p[Formant]));
    if (blend.value < 0) for (auto* s : { &blend, &detune, &movement, &droneLevel, &formant }) s->snap(s->target);
    if (std::abs(formant.value - formantCached) > 0.004f) updateFormants(formant.value);
    // Pitching up needs band-limiting first.
    const float largestRatio = std::exp2(std::max(0.0f, intervalSemitones[interval]) / 12.0f + 0.05f);
    inputLowpass.set(std::min(9000.0, sr * 0.42 / largestRatio), sr, 1.2f);
    // Drone hold: the held layer falls 60 dB over Drone Hold seconds once notes end.
    const float holdDecay = static_cast<float>(std::exp(std::log(0.001) / (std::max(0.5f, p[DroneHold]) * sr)));
    const float attack = timeCoefficient(0.005, sr), release = timeCoefficient(0.12, sr), rise = timeCoefficient(0.06, sr);
    const float grainLength = static_cast<float>(droneGrainSeconds * sr);
    const auto captureSize = capture.size();

    for (int i = 0; i < n; ++i) {
        blend.next(); detune.next(); movement.next(); droneLevel.next(); formant.next();
        lfoPhase += (0.07 + 0.45 * movement.value) / sr;
        if (lfoPhase >= 1) lfoPhase -= 1;
        if (counter-- <= 0) {
            counter = 15;
            for (std::size_t v = 0; v < 4; ++v) {
                const double wobble = std::sin(2 * pi * lfoPhase + 1.7 * static_cast<double>(v)) * 0.8 + std::sin(2 * pi * 0.37 * lfoPhase + static_cast<double>(v)) * 0.5;
                const double cents = voiceDetune[v] * detune.value + movement.value * (detune.value * 0.6 + 8.0) * wobble;
                ratios[v] = static_cast<float>(std::exp2((intervalSemitones[interval] + cents / 100.0) / 12.0));
            }
        }
        const float x = 0.5f * (finiteOr(l[i]) + finiteOr(r[i]));
        const float band = inputLowpass.low(x);
        float wetL = 0, wetR = 0, active = 0;
        for (std::size_t v = 0; v < 4; ++v) {
            const float target = static_cast<int>(v) < voiceCount ? 1.0f : 0.0f;
            voiceGain[v] += 0.002f * (target - voiceGain[v]);
            if (voiceGain[v] < 0.0005f && target == 0) continue;
            const float y = voices[v].process(band, ratios[v]) * voiceGain[v];
            const float angle = (voicePan[v] + 1.0f) * 0.25f * static_cast<float>(pi);
            wetL += y * std::cos(angle); wetR += y * std::sin(angle);
            active += voiceGain[v];
        }
        const float norm = 1.4f / std::sqrt(std::max(1.0f, active));
        wetL *= norm; wetR *= norm;
        float voiced[2] = { wetL, wetR };
        for (int ch = 0; ch < 2; ++ch) {
            float f = 0;
            for (std::size_t k = 0; k < 3; ++k) f += formants[static_cast<std::size_t>(ch)][k].process(voiced[ch]) * formantGain[k];
            voiced[ch] = choirTone[static_cast<std::size_t>(ch)].process(voiced[ch] * 0.35f + f * 1.6f);
        }

        // Drone: capture the sub-octave while playing; afterwards grains keep playing
        // the captured material and the layer fades over Drone Hold.
        const float level = std::abs(x);
        inputEnvelope += (level > inputEnvelope ? attack : release) * (level - inputEnvelope);
        const bool playing = inputEnvelope > 0.003f;
        const float sub = droneShifter.process(band, 0.5f);
        float drone = 0;
        if (droneLevel.value > 0.0001f) {
            if (playing) {
                capture[captureWrite] = sub;
                captureWrite = (captureWrite + 1) % captureSize;
                captureFrozenAt = captureWrite;
                holdEnvelope += rise * (1.0f - holdEnvelope);
            } else {
                holdEnvelope *= holdDecay;
            }
            for (auto& g : grains) {
                g.age += 1.0f / grainLength;
                if (g.age >= 1.0f) {
                    g.age -= 1.0f;
                    const double back = (0.3 + 0.65 * random.unit()) * droneCaptureSeconds * sr * 0.5 + grainLength;
                    g.position = static_cast<double>(captureFrozenAt) + static_cast<double>(captureSize) - back;
                    g.rate = 1.0f + 0.004f * random.bipolar();
                    g.pan = 0.6f * random.bipolar();
                }
                const auto index = static_cast<std::size_t>(g.position) % captureSize;
                const float w = 0.5f - 0.5f * std::cos(twoPi * g.age);
                drone += capture[index] * w;
                g.position += g.rate;
            }
            drone *= 0.55f * holdEnvelope;
        }
        for (int ch = 0; ch < 2; ++ch) {
            float* io = ch == 0 ? l : r;
            const float droneOut = droneTone[static_cast<std::size_t>(ch)].process(drone) * 1.8f;
            io[i] = finiteOr(io[i] + voiced[ch] * blend.value + droneOut * droneLevel.value);
        }
    }
}
}
