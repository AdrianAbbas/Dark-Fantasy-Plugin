#pragma once
#include "Parameters.h"
#include <cstdint>

namespace veil {
struct RitualWeights { float shadow, spirit, ruin; };

// The triangle is normalized in editor coordinates: Spirit is above Shadow / Ruin.
// No memory allocation, synchronization, or parameter-tree access on this path.
class RitualEngine {
public:
    void prepare(double sampleRate);
    void reset();
    Frame process(const Frame& manual, float bpm) noexcept { return processBlock(manual, bpm, 1); }
    // Advances the motion by `samples` at once (called once per processing block).
    Frame processBlock(const Frame& manual, float bpm, int samples) noexcept;
    float x() const noexcept { return static_cast<float>(displayX); }
    float y() const noexcept { return static_cast<float>(displayY); }
    static RitualWeights weights(float x, float y) noexcept;
    static std::array<float, 2> constrain(float x, float y) noexcept;
private:
    float randomSigned() noexcept;
    double sampleRate = 48000.0, phase = 0.0;
    double coordinateSlew = 0.001, displayX = 0.5, displayY = 0.5;
    float motionX = 0.0f, motionY = 0.0f;
    float wanderFromX = 0.0f, wanderFromY = 0.0f;
    float wanderToX = 0.4f, wanderToY = -0.4f;
    std::uint32_t randomState = 0x5645494c;
    int controlCounter = 0;
    bool initialized = false;
};
}
