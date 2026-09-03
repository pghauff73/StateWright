#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace statewright::contracts {

using Json = nlohmann::json;

[[nodiscard]] Json parse_json(std::string_view text);
[[nodiscard]] std::string canonical_json(const Json &value);
[[nodiscard]] std::string canonicalize_json_text(std::string_view text);

} // namespace statewright::contracts

