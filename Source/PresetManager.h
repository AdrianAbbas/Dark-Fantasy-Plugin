#pragma once
#include "Parameters.h"

namespace veil {
struct FactoryPreset { const char* name; Frame values; };
const std::array<FactoryPreset, 15>& factoryPresets();
}
