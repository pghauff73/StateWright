#include "statewright/common/utf8.hpp"

#include "statewright/common/error.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace statewright::common {
namespace {

[[nodiscard]] bool continuation(unsigned char value) noexcept {
  return (value & 0xC0U) == 0x80U;
}

} // namespace

std::string_view error_code_name(ErrorCode code) noexcept {
  switch (code) {
  case ErrorCode::invalid_argument:
    return "invalid_argument";
  case ErrorCode::invalid_utf8:
    return "invalid_utf8";
  case ErrorCode::json_parse:
    return "json_parse";
  case ErrorCode::json_contract:
    return "json_contract";
  case ErrorCode::invalid_object_type:
    return "invalid_object_type";
  case ErrorCode::invalid_typed_id:
    return "invalid_typed_id";
  case ErrorCode::policy_denied:
    return "policy_denied";
  case ErrorCode::filesystem_failure:
    return "filesystem_failure";
  case ErrorCode::cryptographic_failure:
    return "cryptographic_failure";
  case ErrorCode::internal_failure:
    return "internal_failure";
  }
  return "internal_failure";
}

Error::Error(ErrorCode code, std::string message)
    : std::runtime_error(std::move(message)), code_(code) {}

ErrorCode Error::code() const noexcept { return code_; }

bool is_valid_utf8(std::string_view value) noexcept {
  const auto *bytes = reinterpret_cast<const unsigned char *>(value.data());
  std::size_t index = 0;
  while (index < value.size()) {
    const unsigned char first = bytes[index];
    if (first <= 0x7FU) {
      ++index;
      continue;
    }

    if (first >= 0xC2U && first <= 0xDFU) {
      if (index + 1 >= value.size() || !continuation(bytes[index + 1])) {
        return false;
      }
      index += 2;
      continue;
    }

    if (first >= 0xE0U && first <= 0xEFU) {
      if (index + 2 >= value.size() || !continuation(bytes[index + 1]) ||
          !continuation(bytes[index + 2])) {
        return false;
      }
      const unsigned char second = bytes[index + 1];
      if ((first == 0xE0U && second < 0xA0U) ||
          (first == 0xEDU && second >= 0xA0U)) {
        return false;
      }
      index += 3;
      continue;
    }

    if (first >= 0xF0U && first <= 0xF4U) {
      if (index + 3 >= value.size() || !continuation(bytes[index + 1]) ||
          !continuation(bytes[index + 2]) ||
          !continuation(bytes[index + 3])) {
        return false;
      }
      const unsigned char second = bytes[index + 1];
      if ((first == 0xF0U && second < 0x90U) ||
          (first == 0xF4U && second > 0x8FU)) {
        return false;
      }
      index += 4;
      continue;
    }

    return false;
  }
  return true;
}

void require_valid_utf8(std::string_view value) {
  if (!is_valid_utf8(value)) {
    throw Error(ErrorCode::invalid_utf8, "value is not valid UTF-8");
  }
}

} // namespace statewright::common
