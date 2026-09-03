#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <string>
#include <string_view>

namespace statewright::contracts {

struct TypedIdParts final {
  std::string object_type;
  std::string digest;

  friend bool operator==(const TypedIdParts &, const TypedIdParts &) = default;
};

[[nodiscard]] std::string normalize_object_type(std::string_view object_type);
[[nodiscard]] std::string typed_id(std::string_view object_type,
                                   const Json &payload);
[[nodiscard]] TypedIdParts parse_typed_id(std::string_view object_id);

} // namespace statewright::contracts

