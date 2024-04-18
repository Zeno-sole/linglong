// Thish file is generated by /tools/codegen
// DO NOT EDIT IT.

// clang-format off

//  To parse this JSON data, first install
//
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     TimeOffsets.hpp data = nlohmann::json::parse(jsonString);

#pragma once

#include <optional>
#include <nlohmann/json.hpp>
#include "ocppi/runtime/config/types/helper.hpp"

#include "ocppi/runtime/config/types/BoottimeClass.hpp"

namespace ocppi {
namespace runtime {
namespace config {
namespace types {
using nlohmann::json;

struct TimeOffsets {
std::optional<BoottimeClass> boottime;
std::optional<BoottimeClass> monotonic;
};
}
}
}
}

// clang-format on