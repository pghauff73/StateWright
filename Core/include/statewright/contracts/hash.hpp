#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <span>
#include <filesystem>
#include <string>
#include <string_view>

namespace statewright::contracts {

[[nodiscard]] std::string sha256_bytes(std::span<const std::byte> bytes);
[[nodiscard]] std::string sha256_text(std::string_view text);
[[nodiscard]] std::string sha256_json(const Json &value);
[[nodiscard]] std::string sha256_file(const std::filesystem::path &path);

} // namespace statewright::contracts
