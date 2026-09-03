#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace statewright::common {

enum class ErrorCode {
  invalid_argument,
  invalid_utf8,
  json_parse,
  json_contract,
  invalid_object_type,
  invalid_typed_id,
  policy_denied,
  filesystem_failure,
  cryptographic_failure,
  internal_failure,
};

[[nodiscard]] std::string_view error_code_name(ErrorCode code) noexcept;

class Error final : public std::runtime_error {
public:
  Error(ErrorCode code, std::string message);

  [[nodiscard]] ErrorCode code() const noexcept;

private:
  ErrorCode code_;
};

} // namespace statewright::common
