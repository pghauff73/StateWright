#include "statewright/contracts/typed_id.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace statewright::contracts {
namespace {

[[nodiscard]] bool ascii_space(unsigned char value) noexcept {
  return value == ' ' || value == '\t' || value == '\n' || value == '\r' ||
         value == '\f' || value == '\v';
}

} // namespace

std::string normalize_object_type(std::string_view object_type) {
  std::size_t first = 0;
  while (first < object_type.size() &&
         ascii_space(static_cast<unsigned char>(object_type[first]))) {
    ++first;
  }
  std::size_t last = object_type.size();
  while (last > first &&
         ascii_space(static_cast<unsigned char>(object_type[last - 1]))) {
    --last;
  }

  std::string normalized(object_type.substr(first, last - first));
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char value) {
                   if (value == '_') {
                     return '-';
                   }
                   return static_cast<char>(std::tolower(value));
                 });
  if (normalized.empty() || normalized.find(':') != std::string::npos) {
    throw common::Error(common::ErrorCode::invalid_object_type,
                        "invalid typed object type");
  }
  return normalized;
}

std::string typed_id(std::string_view object_type, const Json &payload) {
  const std::string normalized = normalize_object_type(object_type);
  const Json material = {{"object_type", normalized}, {"payload", payload}};
  return normalized + ":sha256:" + sha256_json(material);
}

TypedIdParts parse_typed_id(std::string_view object_id) {
  const std::size_t first = object_id.find(':');
  const std::size_t second = first == std::string_view::npos
                                 ? std::string_view::npos
                                 : object_id.find(':', first + 1);
  if (first == std::string_view::npos || second == std::string_view::npos ||
      object_id.find(':', second + 1) != std::string_view::npos ||
      object_id.substr(first + 1, second - first - 1) != "sha256" ||
      object_id.size() - second - 1 != 64U) {
    throw common::Error(common::ErrorCode::invalid_typed_id,
                        "invalid typed object ID");
  }
  return TypedIdParts{std::string(object_id.substr(0, first)),
                      std::string(object_id.substr(second + 1))};
}

} // namespace statewright::contracts

