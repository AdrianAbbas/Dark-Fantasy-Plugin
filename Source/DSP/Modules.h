#pragma once
// The six VEIL modules. Each processes a stereo chunk in place:
//   process(left, right, numSamples, params, context)
// Parameters arrive once per chunk (already including DREAD offsets) and are
// smoothed inside the module. Chunks never exceed maxChunk samples.
#include "../Parameters.h"
#include "Common.h"
#include <array>
#include <memory>
#include <vector>

namespace juce::dsp { template <typename> class Oversampling; }

namespace veil {
struct Context {
    double sampleRate = 48000, bpm = 120;
};
inline constexpr int maxChunk = 64;

//==============================================================================
// I. THE ABYSS: resonant low-pass (12/24 dB), high-pass, Darkness (cutoff, high
// shelf cut, low-mid warmth), slow Movement, and the Tomb Resonator.
class Abyss {
public:
    void prepare(double sampleRate);
    void reset();
    void process(float* l, float* r, int n, const Frame&, const Context&) noexcept;
private:
    double sr = 48000, phase = 0;
    std::array<dsp::Svf, 2> hp, lp1, lp2;
    std::array<dsp::Biquad, 2> shelf, warmth;
    dsp::Smoothed logCutoff, resonance, highPass, darkness, movement, slopeBlend, tombAmount;
    // Tomb: two tuned feedback combs per channel with damping in the loop.
    std::array<std::array<dsp::DelayLine, 2>, 2> combs;
    std::array<std::array<float, 2>, 2> combDamp{};
    dsp::Smoothed tombPeriod, tombFeedback;
    float tombDampCoefficient = 0.5f, compensation = 1;
    int counter = 0;
};

//==============================================================================
// II. ANCIENT BLOOD: oversampled saturation with loudness compensation, Age,
// Dust, Instability. It is the only module in the main path with latency; its
// own dry path is delayed to match and it never stops (off = aligned dry).
class AncientBlood {
public:
    AncientBlood();
    ~AncientBlood();
    void prepare(double sampleRate);
    void reset();
    int latencySamples() const noexcept { return latency; }
    void process(float* l, float* r, int n, const Frame&, const Context&) noexcept;
private:
    double sr = 48000;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int latency = 0;
    std::array<dsp::DelayLine, 2> dryDelay, wow;
    std::array<std::vector<float>, 2> wet;
    dsp::Smoothed drive, texture, age, dust, instability, mix, enable;
    std::array<dsp::Biquad, 2> ageFilter;
    std::array<float, 2> held{}, inMs{}, outMs{}, compensation{1, 1};
    float msCoefficient = 0.0003f, compensationCoefficient = 0.05f;
    std::array<dsp::DcBlocker, 2> dc;
    float holdPhase = 0, wowPhase = 0, drift = 0, driftTarget = 0, dustEnvelope = 0, crackle = 0;
    std::array<float, 2> hiss{};
    int driftCountdown = 0;
    dsp::Random random;
};

//==============================================================================
// III. THE CHOIR: 1-4 granular pitch-shifted voices with detune, movement and a
// vowel formant bank, plus Drone: a sub-octave layer that holds after notes end.
class Choir {
public:
    void prepare(double sampleRate);
    void reset();
    void process(float* l, float* r, int n, const Frame&, const Context&) noexcept;
private:
    double sr = 48000, lfoPhase = 0;
    std::array<dsp::GranularShifter, 4> voices;
    std::array<float, 4> voiceGain{}, ratios{ { 1, 1, 1, 1 } };
    std::array<std::array<dsp::Biquad, 3>, 2> formants;
    std::array<dsp::Biquad, 2> choirTone;
    dsp::Smoothed blend, detune, movement, droneLevel, formant;
    dsp::Svf inputLowpass;
    // Drone: sub-octave shifter feeding a capture buffer that grains play from.
    dsp::GranularShifter droneShifter;
    std::vector<float> capture;
    std::size_t captureWrite = 0, captureFrozenAt = 0;
    struct Grain { double position = 0; float age = 1, length = 1, pan = 0, rate = 1; };
    std::array<Grain, 4> grains;
    float inputEnvelope = 0, holdEnvelope = 0, droneLevelTrack = 0;
    bool captureActive = false;
    std::array<dsp::Biquad, 2> droneTone;
    dsp::Random random;
    int counter = 0;
    float formantCached = -1;
    void updateFormants(float value) noexcept;
};

//==============================================================================
// IV. THE WRAITH: STFT phase vocoder (Hann, 4x overlap). Blur smears magnitudes
// over time and frequency, Detune/Drift shift pitch slightly, Ghost adds a delayed
// octave-up spectral copy, Freeze resynthesises a held spectrum with evolving
// phases. Wet-only layer: its STFT delay is part of the effect.
struct WraithFft;   // wraps juce::dsp::FFT, defined in Wraith.cpp
class Wraith {
public:
    Wraith();
    ~Wraith();
    void prepare(double sampleRate);
    void reset();
    int fftSize() const noexcept { return size; }
    void process(float* l, float* r, int n, const Frame&, const Context&) noexcept;
private:
    struct Channel {
        std::vector<float> inFifo, outAccum, frame, window;
        std::vector<float> lastPhase, sumPhase, ghostPhase, smoothMag, frozenMag, mag, freq, synthMag, synthFreq;
        std::vector<float> ghostRing, blurred;   // ghost: past frames of magnitudes
        int ghostWrite = 0;
        bool captured = false;                   // freeze spectrum taken
    };
    void processFrame(Channel&, int channel) noexcept;
    double sr = 48000;
    int size = 2048, hop = 512, order = 11, ghostFrames = 48, fifoPos = 0, hopCounter = 0;
    std::unique_ptr<WraithFft> fft;
    float scale = 1;
    std::array<Channel, 2> channels;
    dsp::Smoothed mix;
    float blur = .5f, detuneCents = 15, drift = .4f, ghost = .45f, freezeAmount = 0, freezeTarget = 0;
    float driftValue = 0, driftTarget = 0, ratio = 1, blurTarget = .5f, ghostTarget = .45f;
    int framesSinceDrift = 0, nextDrift = 12;
    dsp::Random random;
};

//==============================================================================
// V. THE ECHOES: crossfaded time changes, damping, diffusion, Reverse, Descent
// (each repeat drops in pitch), stereo spread, limited feedback.
class Echoes {
public:
    void prepare(double sampleRate);
    void reset();
    void process(float* l, float* r, int n, const Frame&, const Context&) noexcept;
private:
    double sr = 48000, reversePhase = 0;
    std::array<dsp::DelayLine, 2> lines;
    std::array<std::array<dsp::DelayLine, 3>, 2> diffusers;
    std::array<std::array<std::size_t, 3>, 2> diffuserSamples{};
    std::array<float, 2> diffuserDelay{};   // total all-pass delay per channel, taken out of the tap
    std::array<dsp::GranularShifter, 2> descent;
    std::array<float, 2> damp{}, hpState{};
    float tapA = 20000, tapB = 20000, crossfade = 1;   // two taps for click-free time changes
    float reverseLength = 20000;
    dsp::Smoothed feedback, mix, damping, diffusion, spread, reverseBlend, descentBlend;
    float descentRatio = 1;
};

//==============================================================================
// VI. THE CRYPT: 8-line Householder FDN with input diffusion, early reflections,
// modulated lines, in-loop damping and shimmer, four characters that differ in
// structure, Rising (reverse-swell) mode and Eternal hold.
class Crypt {
public:
    void prepare(double sampleRate);
    void reset();
    void process(float* l, float* r, int n, const Frame&, const Context&) noexcept;
private:
    static constexpr int lines = 8;
    double sr = 48000;
    std::array<dsp::DelayLine, lines> delay;
    std::array<float, lines> length{}, targetLength{}, gain{}, lowpass{}, lfoPhase{}, lfoRate{};
    std::array<dsp::DelayLine, 8> diffusers;
    std::array<float, 8> diffuserLength{};
    dsp::DelayLine preDelayL, preDelayR, early;
    std::array<dsp::Biquad, 2> inputTone;
    std::array<float, 2> lowCut{};
    dsp::GranularShifter shimmerShifter;
    dsp::Smoothed mix, width, shimmer, eternal, inputGain, dampingHz, preDelaySamples;
    std::array<dsp::DelayLine, 2> rising;   // reverse-swell buffers of the wet signal
    double risePhase = 0;
    float riseLength = 48000;
    dsp::Smoothed risingBlend;
    int character = 1, diffuserCount = 6, counter = 0;
    float modulationDepth = 20, injection = 0.3f;
};
}
