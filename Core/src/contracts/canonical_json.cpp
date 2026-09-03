#include "statewright/contracts/canonical_json.hpp"

#include "statewright/common/error.hpp"
#include "statewright/common/utf8.hpp"

#include <cmath>
#include <string>

namespace statewright::contracts {
namespace {

void require_finite_numbers(const Json &value) {
  if (value.is_number_float() && !std::isfinite(value.get<double>())) {
    throw common::Error(common::ErrorCode::json_contract,
                        "canonical JSON rejects non-finite numbers");
  }
  if (value.is_array()) {
    for (const auto &item : value) {
      require_finite_numbers(item);
    }
  } else if (value.is_object()) {
    for (const auto &[key, item] : value.items()) {
      common::require_valid_utf8(key);
      require_finite_numbers(item);
    }
  } else if (value.is_string()) {
    common::require_valid_utf8(value.get_ref<const std::string &>());
  }
}

} // namespace

Json parse_json(std::string_view text) {
  common::require_valid_utf8(text);
  try {
    Json value = Json::parse(text.begin(), text.end(), nullptr, true, true);
    require_finite_numbers(value);
    return value;
  } catch (const common::Error &) {
    throw;
  } catch (const nlohmann::json::exception &error) {
    throw common::Error(common::ErrorCode::json_parse,
                        std::string("invalid JSON: ") + error.what());
  }
}

std::string canonical_json(const Json &value) {
  require_finite_numbers(value);
  try {
    return value.dump(-1, ' ', false, Json::error_handler_t::strict);
  } catch (const nlohmann::json::exception &error) {
    throw common::Error(common::ErrorCode::json_contract,
                        std::string("cannot serialize canonical JSON: ") +
                            error.what());
  }
}

std::string canonicalize_json_text(std::string_view text) {
  return canonical_json(parse_json(text));
}

} // namespace statewright::contracts

