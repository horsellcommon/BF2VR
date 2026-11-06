#pragma once
#include <optional>
#include <string_view>
#include <sstream>
#include <cmath>
namespace BF2VR {
inline std::optional<float> ParseWorldUnitsPerMeter(std::string_view text) {
    std::istringstream input{std::string(text)}; float value=0; char extra=0;
    if(!(input>>value) || (input>>extra) || !std::isfinite(value) || value<.5f || value>2.f) return std::nullopt;
    return value;
}
}
