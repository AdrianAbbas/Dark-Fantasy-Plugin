#pragma once
#include "../Parameters.h"
#include <array>
#include <vector>
#include <cstddef>

namespace veil {
namespace spatial {
// All storage is created by prepare(); these interpolation/tap helpers are RT safe.
struct StereoRing {
    std::array<std::vector<float>, 2> data;
    std::size_t position = 0;
    // Optional soft splice for a held rolling capture; zero for live buffers.
    std::size_t seamPosition = 0;
    float seamStrength = 0;
    double seamLength = 64;
    void prepare(std::size_t length);
    void clear() noexcept;
    void write(float left, float right) noexcept;
    void advance() noexcept;
    float read(int channel, double delay) const noexcept;
};
struct PitchTap {
    double phase = 0.0;
    void reset(double initialPhase = 0.0) noexcept { phase = initialPhase; }
    std::array<float, 2> read(const StereoRing&, double ratio, double window, double minimumDelay) noexcept;
};
struct Resonator {
    float b0=0, b1=0, b2=0, a1=0, a2=0;
    std::array<float,2> z1{}, z2{};
    void set(double sampleRate, double frequency, double q) noexcept;
    float process(float input, int channel) noexcept;
    void reset() noexcept { z1.fill(0); z2.fill(0); }
};
}

class Crypt {
public:
    void prepare(double sampleRate);
    void reset();
    void process(float& left, float& right, const Frame&, float bpm) noexcept;
private:
    double sr = 48000.0;
    spatial::StereoRing preDelay;
    spatial::StereoRing shimmerBuffer;
    spatial::PitchTap shimmerTap;
    std::array<std::vector<float>, 8> delayLines;
    std::array<std::size_t, 8> positions{};
    std::array<float, 8> lowpass{}, feedback{}, delaySamples{}, targetDelays{};
    std::array<float, 2> highpassState{}, shimmerLP{};
    float dampingCoefficient=.2f, highpassCoefficient=.01f, predelaySamples=0;
    float shimmerAmount=0, width=1, characterDark=1, shimmerCoefficient=.5f;
    int controlCounter=0;
};

class Wraith {
public:
    void prepare(double sampleRate);
    void reset();
    void process(float& left, float& right, const Frame&, float bpm) noexcept;
private:
    double sr=48000, modulationPhase=0;
    spatial::StereoRing capture;
    spatial::PitchTap mainTap, ghostTap;
    std::array<float,2> blur1{}, blur2{}, wetSmooth{};
    float blurCoefficient=.2f, wetCoefficient=.2f;
    double mainRatio=1, ghostRatio=1.5, window=4800, targetWindow=4800;
    int controlCounter=0;
    bool held=false;
};

class Choir {
public:
    void prepare(double sampleRate);
    void reset();
    void process(float& left, float& right, const Frame&, float bpm) noexcept;
private:
    double sr=48000, modulationPhase=0;
    spatial::StereoRing buffer;
    std::array<spatial::PitchTap,4> taps;
    std::array<double,4> ratios{{1,1,1,1}};
    std::array<float,4> voiceGains{{1,1,1,0}};
    std::array<float,2> inputLP{};
    spatial::Resonator vowel1, vowel2;
    float inputCoefficient=.5f, formantAmount=.45f;
    int controlCounter=0;
};
}
