#include "statewright/sources/policy.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/contracts/typed_id.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace statewright::sources {
namespace {

[[noreturn]] void policy_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied, std::move(message));
}

template <typename Value> void canonical_values(std::vector<Value> &values) {
  std::ranges::sort(values);
  values.erase(std::unique(values.begin(), values.end()), values.end());
}

} // namespace

std::string InternetSourcePolicy::object_id() const {
  return contracts::typed_id("internet-source-policy", to_json(*this));
}

InternetSourcePolicy canonical_source_policy(InternetSourcePolicy policy) {
  canonical_values(policy.allowed_schemes);
  canonical_values(policy.allowed_ports);
  canonical_values(policy.accepted_mime_types);
  static const std::set<std::string> supported_schemes = {"http", "https"};
  if (policy.policy_version != internet_source_policy_version ||
      policy.allowed_schemes.empty() || policy.allowed_ports.empty() ||
      policy.accepted_mime_types.empty() || policy.maximum_redirects < 0 ||
      policy.maximum_header_bytes == 0U ||
      policy.maximum_response_bytes == 0U ||
      policy.maximum_decompressed_bytes < policy.maximum_response_bytes ||
      policy.connect_timeout_seconds <= 0 ||
      policy.request_timeout_seconds < policy.connect_timeout_seconds ||
      policy.user_agent.empty()) {
    policy_error("internet source policy limits are invalid");
  }
  for (const auto &scheme : policy.allowed_schemes) {
    if (!supported_schemes.contains(scheme)) {
      policy_error("unsupported internet source scheme: " + scheme);
    }
  }
  for (const int port : policy.allowed_ports) {
    if (port <= 0 || port > 65535) {
      policy_error("internet source policy port is invalid");
    }
  }
  contracts::Json material = to_json(policy);
  material.erase("policy_signature");
  policy.policy_signature = contracts::sha256_json(material);
  return policy;
}

contracts::Json to_json(const InternetSourcePolicy &policy) {
  return {{"accepted_mime_types", policy.accepted_mime_types},
          {"allow_loopback_for_tests", policy.allow_loopback_for_tests},
          {"allowed_ports", policy.allowed_ports},
          {"allowed_schemes", policy.allowed_schemes},
          {"connect_timeout_seconds", policy.connect_timeout_seconds},
          {"maximum_decompressed_bytes", policy.maximum_decompressed_bytes},
          {"maximum_header_bytes", policy.maximum_header_bytes},
          {"maximum_redirects", policy.maximum_redirects},
          {"maximum_response_bytes", policy.maximum_response_bytes},
          {"policy_signature", policy.policy_signature},
          {"policy_version", policy.policy_version},
          {"request_timeout_seconds", policy.request_timeout_seconds},
          {"require_known_license", policy.require_known_license},
          {"require_robots_permission", policy.require_robots_permission},
          {"require_tls_verification", policy.require_tls_verification},
          {"schema_version", policy.schema_version},
          {"user_agent", policy.user_agent}};
}

InternetSourcePolicy source_policy_from_json(const contracts::Json &value) {
  if (!value.is_object()) {
    policy_error("internet source policy must be an object");
  }
  InternetSourcePolicy policy;
  policy.schema_version = value.value("schema_version", 1);
  policy.policy_version = value.value(
      "policy_version", std::string(internet_source_policy_version));
  policy.allowed_schemes =
      value.value("allowed_schemes", std::vector<std::string>{"https"});
  policy.allowed_ports = value.value("allowed_ports", std::vector<int>{443});
  policy.accepted_mime_types = value.value(
      "accepted_mime_types",
      std::vector<std::string>{"text/plain", "text/html", "application/json"});
  policy.maximum_redirects = value.value("maximum_redirects", 5);
  policy.maximum_header_bytes =
      value.value("maximum_header_bytes", 64U * 1024U);
  policy.maximum_response_bytes =
      value.value("maximum_response_bytes", 8U * 1024U * 1024U);
  policy.maximum_decompressed_bytes =
      value.value("maximum_decompressed_bytes", 32U * 1024U * 1024U);
  policy.connect_timeout_seconds = value.value("connect_timeout_seconds", 10);
  policy.request_timeout_seconds = value.value("request_timeout_seconds", 30);
  policy.require_tls_verification = value.value("require_tls_verification", true);
  policy.allow_loopback_for_tests = value.value("allow_loopback_for_tests", false);
  policy.require_robots_permission = value.value("require_robots_permission", true);
  policy.require_known_license = value.value("require_known_license", false);
  policy.user_agent = value.value("user_agent", "StateWright-SAA/0.1");
  return canonical_source_policy(std::move(policy));
}

} // namespace statewright::sources
