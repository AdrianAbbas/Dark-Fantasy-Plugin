#pragma once
#include "Parameters.h"

namespace veil {
struct FactoryPreset { const char* name; const char* description; Frame values; };
inline constexpr std::size_t factoryPresetTotal = 16;
const std::array<FactoryPreset, factoryPresetTotal>& factoryPresets();
}
