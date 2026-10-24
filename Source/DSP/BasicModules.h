#pragma once
#include "../Parameters.h"
#include <array>
#include <cstdint>
#include <vector>

namespace veil {
// All storage is allocated by prepare(). process() is allocation-free.
namespace basic {
struct StateVariable {
    float s1 = 0.0f, s2 = 0.0f;
    float low(float x, float g, float k) noexcept;
    float high(float x, float g, float k) noexcept;
    void clear() noexcept { s1 = s2 = 0.0f; }
};
struct Ring {
    std::vector<float> data;
    std::size_t cursor = 0;
    void prepare(std::size_t n);
    void reset() noexcept;
    float read(float delay) const noexcept;
    void push(float value) noexcept;
};
}

class Abyss {
public:
    void prepare(double sampleRate);
    void reset();
    void process(float& left, float& right, const Frame&, float bpm) noexcept;
private:
    double sr = 48000.0, phase = 0.0;
    float slopeBlend = 1.0f;
    std::array<basic::StateVariable, 2> hp, lp1, lp2;
};

class AncientBlood {
public:
    void prepare(double sampleRate);
    void reset();
    void process(float& left, float& right, const Frame&, float bpm) noexcept;
private:
    double sr = 48000.0, phase = 0.0;
    std::array<basic::Ring, 2> wow;
    std::array<float, 2> previous{}, reconstruction1{}, reconstruction2{}, aged{}, held{}, dcIn{}, dcOut{};
    float holdPhase = 0, envelope = 0, randomDrift = 0, driftTarget = 0;
    float dcCoefficient = .9987f, reconstructionCoefficient = .69f;
    int driftCountdown = 0;
    std::uint32_t randomState = 0x8ad02ec1u;
    float random() noexcept;
};

class Echoes {
public:
    void prepare(double sampleRate);
    void reset();
    void process(float& left, float& right, const Frame&, float bpm) noexcept;
private:
    double sr = 48000.0, reversePhase = 0.0;
    std::array<basic::Ring, 2> delay, diffuser;
    std::array<float, 2> damped{}, hpState{};
    float delaySamples = 21120.0f, reverseBlend = 0.0f;
};
}
