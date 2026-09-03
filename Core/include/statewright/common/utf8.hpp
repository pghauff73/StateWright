#pragma once

#include <string_view>

namespace statewright::common {

[[nodiscard]] bool is_valid_utf8(std::string_view value) noexcept;
void require_valid_utf8(std::string_view value);

} // namespace statewright::common

