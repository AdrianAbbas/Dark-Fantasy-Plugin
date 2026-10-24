#include "Modules.h"

namespace veil {
using namespace dsp;

namespace {
constexpr double syncBeats[] = { 0, 0.25, 0.5, 0.75, 1, 1.5, 2, 4 };   // Free, 1/16 .. 1 bar
constexpr float descentSemitones[] = { 0, 1, 7, 12 };                   // Off, semitone, fifth, octave
constexpr float diffuserMs[2][3] = { { 4.7f, 7.9f, 11.3f }, { 5.3f, 8.6f, 12.1f } };
constexpr double maxDelaySeconds = 6.0;
}

void Echoes::prepare(double sampleRate) {
    sr = sampleRate;
    for (auto& line : lines) line.prepare(static_cast<std::size_t>((maxDelaySeconds * 1.15 + 0.2) * sr));
    for (int ch = 0; ch < 2; ++ch) {
        diffuserDelay[static_cast<std::size_t>(ch)] = 0;
        for (std::size_t k = 0; k < 3; ++k) {
            const auto length = static_cast<std::size_t>(std::lround(diffuserMs[ch][k] * 0.001 * sr));
            diffuserSamples[static_cast<std::size_t>(ch)][k] = length;
            diffusers[static_cast<std::size_t>(ch)][k].prepare(length + 2);
            diffuserDelay[static_cast<std::size_t>(ch)] += static_cast<float>(length);
        }
    }
    for (auto& d : descent) d.prepare(sr, 0.05);
    for (auto* s : { &feedback, &mix, &damping, &diffusion, &spread, &reverseBlend, &descentBlend }) s->setTime(0.03, sr);
    reset();
}

void Echoes::reset() {
    for (auto& line : lines) line.clear();
    for (auto& channel : diffusers) for (auto& d : channel) d.clear();
    for (auto& d : descent) d.clear();
    damp.fill(0); hpState.fill(0);
    tapA = tapB = static_cast<float>(0.48 * sr);
    crossfade = 1;
    reverseLength = tapB;
    reversePhase = 0;
    feedback.value = -1;   // not primed
}

void Echoes::process(float* l, float* r, int n, const Frame& p, const Context& c) noexcept {
    const auto sync = static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(p[DelaySync])), 0, 7));
    const double seconds = sync == 0 ? p[DelayTime] * 0.001 : beatsToSeconds(syncBeats[sync], c.bpm);
    const auto descentIndex = static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(p[Descent])), 0, 3));
    if (descentIndex > 0) descentRatio = std::exp2(-descentSemitones[descentIndex] / 12.0f);
    descentBlend.set(descentIndex > 0 ? 1.0f : 0.0f);
    feedback.set(std::clamp(p[Feedback], 0.0f, 0.98f));
    mix.set(clamp01(p[DelayMix]));
    damping.set(clamp01(p[DelayDamping]));
    diffusion.set(clamp01(p[Diffusion]));
    spread.set(clamp01(p[Spread]));
    reverseBlend.set(p[Reverse] >= 0.5f ? 1.0f : 0.0f);
    // The descent shifter sits in the loop; its delay comes out of the tap so the
    // repeat timing stays on the beat.
    const float shifterDelay = descentBlend.target * descent[0].latency();
    const float wanted = std::max(static_cast<float>(0.005 * sr),
                                  static_cast<float>(std::clamp(seconds, 0.01, maxDelaySeconds) * sr) - shifterDelay);
    if (feedback.value < 0) {
        for (auto* s : { &feedback, &mix, &damping, &diffusion, &spread, &reverseBlend, &descentBlend }) s->snap(s->target);
        tapA = tapB = reverseLength = wanted;
        crossfade = 1;
    }
    // Time changes crossfade between two taps (no pitch bend, no zipper).
    if (std::abs(wanted - tapB) > 0.5f && crossfade >= 1.0f) { tapA = tapB; tapB = wanted; crossfade = 0; }
    const float crossfadeStep = static_cast<float>(1.0 / (0.06 * sr));
    const float lowpass = poleForHz(18000.0 * std::exp2(-5.0 * damping.target), sr);
    const float highpass = poleForHz(45.0, sr);

    float* io[] = { l, r };
    for (int i = 0; i < n; ++i) {
        const float fb = feedback.next(), m = mix.next(), dif = diffusion.next(), spr = spread.next();
        const float rev = reverseBlend.next(), dsc = descentBlend.next();
        damping.next();
        crossfade = std::min(1.0f, crossfade + crossfadeStep);
        if (crossfade >= 1.0f) reverseLength += 0.0005f * (tapB - reverseLength);
        reversePhase += 1.0 / std::max(64.0f, reverseLength);
        if (reversePhase >= 1) reversePhase -= 1;
        float dry[2], echo[2];
        for (int ch = 0; ch < 2; ++ch) {
            dry[ch] = finiteOr(io[ch][i]);
            auto& line = lines[static_cast<std::size_t>(ch)];
            const float stretch = ch == 1 ? 1.0f + 0.11f * spr : 1.0f;
            // The diffusers always delay by their full length, so it comes out of the tap.
            const float offset = diffuserDelay[static_cast<std::size_t>(ch)];
            float s = lerp(line.read(std::max(1.0f, tapA * stretch - offset)), line.read(std::max(1.0f, tapB * stretch - offset)), crossfade);
            if (rev > 0.0001f) {
                // Two half-overlapped Hann heads play the last segment backwards.
                float reversed = 0;
                for (int h = 0; h < 2; ++h) {
                    double ph = reversePhase + 0.5 * h;
                    if (ph >= 1) ph -= 1;
                    const float w = 0.5f - 0.5f * std::cos(twoPi * static_cast<float>(ph));
                    reversed += w * line.read(1.0f + 2.0f * reverseLength * stretch * static_cast<float>(ph));
                }
                s = lerp(s, reversed, rev);
            }
            auto& d = damp[static_cast<std::size_t>(ch)];
            d += lowpass * (s - d);
            auto& h = hpState[static_cast<std::size_t>(ch)];
            h += highpass * (d - h);
            s = d - h;
            // Three Schroeder all-passes: energy-preserving smear of each repeat
            // (at Diffusion 0 they are plain delays, so there is no comb colouring).
            const float g = 0.72f * dif;
            for (std::size_t k = 0; k < 3; ++k) {
                auto& ap = diffusers[static_cast<std::size_t>(ch)][k];
                const float delayed = ap.readInteger(diffuserSamples[static_cast<std::size_t>(ch)][k]);
                const float out = -g * s + delayed;
                ap.push(s + g * out);
                s = out;
            }
            const float shifted = descent[static_cast<std::size_t>(ch)].process(s, descentRatio);
            echo[ch] = lerp(s, shifted, dsc);
        }
        float dryGain, wetGain;
        equalPowerGains(m, dryGain, wetGain);
        for (int ch = 0; ch < 2; ++ch) {
            // Ping-pong crossfeed is a convex mix, so the loop gain stays <= feedback < 1;
            // the soft limiter bounds anything left over.
            const float loop = lerp(echo[ch], echo[1 - ch], spr * 0.75f);
            lines[static_cast<std::size_t>(ch)].push(softLimit(dry[ch] + fb * loop, 1.6f));
            io[ch][i] = finiteOr(dry[ch] * dryGain + echo[ch] * wetGain * 1.15f);
        }
    }
}
}
