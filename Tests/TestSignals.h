#pragma once
// Test signals, offline rendering through the real processBlock path, and simple
// audio measurements. Shared by the regression tests and the demo renderer.
#include "PluginProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <memory>
#include <numbers>
#include <utility>
#include <vector>

namespace veiltest {
constexpr double tau = 2.0 * std::numbers::pi;
using Settings = std::initializer_list<std::pair<veil::P, float>>;

inline void set(VeilAudioProcessor& p, veil::P id, float value) {
    auto* parameter = p.parameters.getParameter(veil::specs[id].id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
inline void apply(VeilAudioProcessor& p, Settings settings) { for (const auto& [id, value] : settings) set(p, id, value); }
inline constexpr veil::P moduleSwitches[] = { veil::AbyssOn, veil::BloodOn, veil::ChoirOn, veil::WraithOn, veil::EchoesOn, veil::CryptOn };
inline void modulesOff(VeilAudioProcessor& p) { for (auto id : moduleSwitches) set(p, id, 0); }
inline void onlyModule(VeilAudioProcessor& p, veil::P on) { modulesOff(p); set(p, on, 1); }

struct Signal {
    std::vector<float> l, r;
    std::size_t size() const { return l.size(); }
};
inline Signal silence(double sr, double seconds) {
    const auto n = static_cast<std::size_t>(sr * seconds);
    return { std::vector<float>(n, 0.0f), std::vector<float>(n, 0.0f) };
}
inline float polyBlep(double t, double dt) {
    if (t < dt) { t /= dt; return static_cast<float>(t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return static_cast<float>(t * t + t + t + 1.0); }
    return 0.0f;
}
// Bright band-limited sawtooth chord (C minor 7 plus octave), held then released.
inline Signal sawChord(double sr, double seconds, double hold = 3.5, float level = 0.5f) {
    auto s = silence(sr, seconds);
    const double freqs[] = { 130.81, 155.56, 196.0, 233.08, 261.63 };
    double phase[5] = { 0, .21, .43, .67, .89 };
    for (std::size_t n = 0; n < s.size(); ++n) {
        const double t = static_cast<double>(n) / sr;
        const double env = std::min(1.0, t / 0.02) * (t < hold ? 1.0 : std::max(0.0, 1.0 - (t - hold) / 0.3));
        float sum = 0;
        for (int k = 0; k < 5; ++k) {
            const double dt = freqs[k] * (1.0 + 0.0011 * k) / sr;
            phase[k] += dt;
            if (phase[k] >= 1) phase[k] -= 1;
            sum += static_cast<float>(2.0 * phase[k] - 1.0) - polyBlep(phase[k], dt);
        }
        const float v = static_cast<float>(env) * sum * level / 3.0f;
        s.l[n] = v; s.r[n] = v * 0.97f;
    }
    return s;
}
// Short piano-like phrase: inharmonic partials with faster decay for higher ones.
inline Signal pianoPhrase(double sr, double seconds, float level = 0.55f) {
    auto s = silence(sr, seconds);
    const int notes[] = { 57, 60, 64, 69, 67, 64, 60, 62, 57 };
    const double start[] = { 0, .45, .9, 1.35, 1.8, 2.25, 2.7, 3.15, 3.6 };
    for (int k = 0; k < 9; ++k) {
        const double f0 = 440.0 * std::pow(2.0, (notes[k] - 69) / 12.0);
        const auto begin = static_cast<std::size_t>(start[k] * sr);
        if (begin >= s.size()) break;   // phrase longer than the signal
        const auto length = std::min(s.size() - begin, static_cast<std::size_t>(2.6 * sr));
        for (std::size_t n = 0; n < length; ++n) {
            const double t = static_cast<double>(n) / sr;
            double v = 0;
            for (int h = 1; h <= 10; ++h) {
                const double f = f0 * h * std::sqrt(1.0 + 0.00035 * h * h);
                if (f > sr * 0.45) break;
                v += std::sin(tau * f * t) * std::exp(-t / (1.7 / (1.0 + 0.55 * h))) / std::pow(h, 1.1);
            }
            v *= std::min(1.0, t / 0.003) * (k == 8 ? 1.0 : std::exp(-std::max(0.0, t - 1.6) / 0.15));
            s.l[begin + n] += static_cast<float>(v * level * 0.42);
            s.r[begin + n] += static_cast<float>(v * level * 0.40);
        }
    }
    return s;
}
inline Signal sine(double sr, double seconds, double hz, float amplitude, double on) {
    auto s = silence(sr, seconds);
    for (std::size_t n = 0; n < s.size(); ++n) {
        const double t = static_cast<double>(n) / sr;
        if (t >= on) break;
        const double env = std::min(1.0, t / 0.005) * std::min(1.0, (on - t) / 0.005);
        s.l[n] = s.r[n] = static_cast<float>(amplitude * env * std::sin(tau * hz * t));
    }
    return s;
}
inline Signal noise(double sr, double seconds, float amplitude, double on, std::uint32_t seed = 1234) {
    auto s = silence(sr, seconds);
    veil::dsp::Random random; random.seed(seed);
    for (std::size_t n = 0; n < s.size() && static_cast<double>(n) < on * sr; ++n) {
        s.l[n] = amplitude * random.bipolar();
        s.r[n] = amplitude * random.bipolar();
    }
    return s;
}
inline Signal impulse(double sr, double seconds, float amplitude) {
    auto s = silence(sr, seconds);
    s.l[0] = s.r[0] = amplitude;
    return s;
}

struct Rendered { Signal in, out; double sr = 48000; int latency = 0; };
// Renders through processBlock in blocks of `block` samples (block <= 0: varying sizes).
inline Rendered render(VeilAudioProcessor& p, const Signal& input, double sr, int block = 512,
                       const std::function<void(std::size_t)>& beforeBlock = {}) {
    Rendered result;
    result.in = input; result.sr = sr; result.latency = p.getLatencySamples();
    result.out = silence(sr, 0);
    result.out.l.assign(input.size(), 0.0f); result.out.r.assign(input.size(), 0.0f);
    const int varying[] = { 1, 7, 64, 333, 512, 4096, 13, 1024 };
    juce::AudioBuffer<float> buffer(2, 4096);
    juce::MidiBuffer midi;
    std::size_t position = 0, index = 0;
    while (position < input.size()) {
        const int want = block > 0 ? block : varying[index % 8];
        const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(want), input.size() - position));
        if (beforeBlock) beforeBlock(position);
        buffer.setSize(2, n, false, false, true);
        std::copy_n(input.l.begin() + static_cast<std::ptrdiff_t>(position), n, buffer.getWritePointer(0));
        std::copy_n(input.r.begin() + static_cast<std::ptrdiff_t>(position), n, buffer.getWritePointer(1));
        p.processBlock(buffer, midi);
        std::copy_n(buffer.getReadPointer(0), n, result.out.l.begin() + static_cast<std::ptrdiff_t>(position));
        std::copy_n(buffer.getReadPointer(1), n, result.out.r.begin() + static_cast<std::ptrdiff_t>(position));
        position += static_cast<std::size_t>(n);
        ++index;
    }
    return result;
}
inline std::unique_ptr<VeilAudioProcessor> makeProcessor(Settings settings = {}, double sr = 48000, int block = 512) {
    auto p = std::make_unique<VeilAudioProcessor>();
    apply(*p, settings);
    p->prepareToPlay(sr, block);
    return p;
}

// --- measurements -----------------------------------------------------------
inline std::size_t at(double sr, double seconds) { return static_cast<std::size_t>(std::max(0.0, seconds) * sr); }
inline double meanSquare(const Signal& s, std::size_t a, std::size_t b) {
    b = std::min(b, s.size());
    double e = 0;
    for (std::size_t n = a; n < b; ++n) e += 0.5 * (double(s.l[n]) * s.l[n] + double(s.r[n]) * s.r[n]);
    return b > a ? e / static_cast<double>(b - a) : 0.0;
}
inline double toDb(double power) { return 10.0 * std::log10(std::max(power, 1.0e-20)); }
inline float peak(const Signal& s) {
    float m = 0;
    for (std::size_t n = 0; n < s.size(); ++n) m = std::max({ m, std::abs(s.l[n]), std::abs(s.r[n]) });
    return m;
}
inline bool finite(const Signal& s) {
    for (std::size_t n = 0; n < s.size(); ++n) if (!std::isfinite(s.l[n]) || !std::isfinite(s.r[n])) return false;
    return true;
}
// Average power spectrum (mono sum) of [a, b) with 4096-point Hann frames.
inline std::vector<double> powerSpectrum(const Signal& s, std::size_t a, std::size_t b) {
    constexpr int order = 12, size = 1 << order;
    juce::dsp::FFT fft(order);
    std::vector<float> frame(size * 2);
    std::vector<double> power(size / 2 + 1, 0.0);
    int frames = 0;
    for (std::size_t start = a; start + size <= std::min(b, s.size()); start += size / 2) {
        for (int i = 0; i < size; ++i) {
            const float w = 0.5f - 0.5f * std::cos(static_cast<float>(tau) * static_cast<float>(i) / size);
            frame[static_cast<std::size_t>(i)] = 0.5f * (s.l[start + static_cast<std::size_t>(i)] + s.r[start + static_cast<std::size_t>(i)]) * w;
        }
        std::fill(frame.begin() + size, frame.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform(frame.data(), true);
        for (int k = 0; k <= size / 2; ++k) power[static_cast<std::size_t>(k)] += double(frame[static_cast<std::size_t>(k)]) * frame[static_cast<std::size_t>(k)];
        ++frames;
    }
    for (auto& v : power) v /= std::max(1, frames);
    return power;
}
inline double binHz(double sr) { return sr / 4096.0; }
inline double centroid(const Signal& s, double sr, std::size_t a, std::size_t b) {
    const auto power = powerSpectrum(s, a, b);
    double num = 0, den = 0;
    for (std::size_t k = 1; k < power.size(); ++k) { num += power[k] * double(k) * binHz(sr); den += power[k]; }
    return den > 0 ? num / den : 0.0;
}
// Power in a band around `hz` (+- width Hz), from the averaged spectrum.
inline double bandPower(const std::vector<double>& power, double sr, double hz, double width) {
    double e = 0;
    const auto lo = static_cast<std::size_t>(std::max(1.0, (hz - width) / binHz(sr)));
    const auto hi = std::min(power.size() - 1, static_cast<std::size_t>((hz + width) / binHz(sr)) + 1);
    for (std::size_t k = lo; k <= hi; ++k) e += power[k];
    return e;
}
// Energy of output minus delayed input, relative to the input, in dB.
inline double residualDb(const Signal& out, const Signal& in, int delay, std::size_t a, std::size_t b) {
    double diff = 0, ref = 0;
    b = std::min(b, out.size());
    for (std::size_t n = std::max<std::size_t>(a, static_cast<std::size_t>(delay)); n < b; ++n) {
        const auto m = n - static_cast<std::size_t>(delay);
        const double dl = out.l[n] - in.l[m], dr = out.r[n] - in.r[m];
        diff += dl * dl + dr * dr;
        ref += double(in.l[m]) * in.l[m] + double(in.r[m]) * in.r[m];
    }
    return toDb(diff / std::max(ref, 1.0e-30));
}
// Difference between two renders relative to the second, in dB.
inline double differenceDb(const Signal& a, const Signal& b) {
    double diff = 0, ref = 0;
    for (std::size_t n = 0; n < std::min(a.size(), b.size()); ++n) {
        const double dl = a.l[n] - b.l[n], dr = a.r[n] - b.r[n];
        diff += dl * dl + dr * dr;
        ref += double(b.l[n]) * b.l[n] + double(b.r[n]) * b.r[n];
    }
    return toDb(diff / std::max(ref, 1.0e-30));
}
// Envelope in dB (10 ms RMS windows) starting at sample a.
inline std::vector<double> envelopeDb(const Signal& s, double sr, std::size_t a) {
    std::vector<double> env;
    const auto window = at(sr, 0.01);
    for (std::size_t start = a; start + window <= s.size(); start += window) env.push_back(toDb(meanSquare(s, start, start + window)));
    return env;
}
// Reverb decay time: T30 extrapolated to 60 dB, from the envelope after `a`.
inline double decaySeconds(const Signal& s, double sr, std::size_t a) {
    const auto env = envelopeDb(s, sr, a);
    if (env.empty()) return 0;
    const auto top = std::max_element(env.begin(), env.end());
    const double peakDb = *top;
    double t5 = -1, t35 = -1;
    for (auto it = top; it != env.end(); ++it) {
        const double t = 0.01 * static_cast<double>(it - env.begin());
        if (t5 < 0 && *it < peakDb - 5) t5 = t;
        if (t35 < 0 && *it < peakDb - 35) { t35 = t; break; }
    }
    if (t5 < 0) return 0;
    if (t35 < 0) return 99.0;   // never fell 35 dB within the render
    return 2.0 * (t35 - t5);
}
// One-third-octave band levels (dB), 40 Hz .. 16 kHz, for comparing timbres.
inline std::vector<double> bandLevels(const Signal& s, double sr, std::size_t a, std::size_t b) {
    const auto power = powerSpectrum(s, a, b);
    std::vector<double> levels;
    for (double f = 40; f < 16000; f *= std::pow(2.0, 1.0 / 3.0)) levels.push_back(toDb(bandPower(power, sr, f, f * 0.115)));
    return levels;
}
}
