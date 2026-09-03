#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::sources {

inline constexpr std::string_view internet_source_policy_version =
    "statewright-internet-source-policy-v1";

struct InternetSourcePolicy final {
  int schema_version = 1;
  std::string policy_version = std::string(internet_source_policy_version);
  std::vector<std::string> allowed_schemes{"https"};
  std::vector<int> allowed_ports{443};
  std::vector<std::string> accepted_mime_types{"text/plain", "text/html",
                                                "application/json"};
  int maximum_redirects = 5;
  std::size_t maximum_header_bytes = 64U * 1024U;
  std::size_t maximum_response_bytes = 8U * 1024U * 1024U;
  std::size_t maximum_decompressed_bytes = 32U * 1024U * 1024U;
  int connect_timeout_seconds = 10;
  int request_timeout_seconds = 30;
  bool require_tls_verification = true;
  bool allow_loopback_for_tests = false;
  bool require_robots_permission = true;
  bool require_known_license = false;
  std::string user_agent = "StateWright-SAA/0.1";
  std::string policy_signature;

  [[nodiscard]] std::string object_id() const;
};

[[nodiscard]] InternetSourcePolicy
canonical_source_policy(InternetSourcePolicy policy);
[[nodiscard]] contracts::Json to_json(const InternetSourcePolicy &policy);
[[nodiscard]] InternetSourcePolicy
source_policy_from_json(const contracts::Json &value);

} // namespace statewright::sources
