#include "statewright/core/authority.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <ctime>
#include <regex>
#include <set>
#include <string>

namespace statewright::core {
namespace {

const std::set<std::string, std::less<>> authority_fields = {
    "allow_interactive_l2",       "allow_l1_auto_apply",
    "allow_yolo",                 "allowed_paths",
    "authority_hash",             "command_capabilities",
    "expires_at",                 "forbidden_paths",
    "goal",                       "mandatory_evidence",
    "mandatory_tests",            "max_automatic_risk",
    "max_retries_per_action",     "operator",
    "read_capabilities",          "read_only",
    "schema_version",             "semantic_capabilities",
    "semantic_capability_ceiling", "source_snapshot_hash",
    "task_id",
};

[[nodiscard]] bool path_pattern_invalid(const std::string &pattern) {
  return pattern.empty() || pattern.front() == '/' ||
         pattern.find('\0') != std::string::npos;
}

[[nodiscard]] std::chrono::system_clock::time_point
parse_iso8601(std::string_view value) {
  static const std::regex pattern(
      R"(^([0-9]{4})-([0-9]{2})-([0-9]{2})T([0-9]{2}):([0-9]{2}):([0-9]{2})(?:\.([0-9]{1,9}))?(Z|[+-][0-9]{2}:[0-9]{2})?$)");
  std::smatch match;
  const std::string text(value);
  if (!std::regex_match(text, match, pattern)) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "authority expires_at must be ISO-8601");
  }

  std::tm time{};
  time.tm_year = std::stoi(match[1].str()) - 1900;
  time.tm_mon = std::stoi(match[2].str()) - 1;
  time.tm_mday = std::stoi(match[3].str());
  time.tm_hour = std::stoi(match[4].str());
  time.tm_min = std::stoi(match[5].str());
  time.tm_sec = std::stoi(match[6].str());
  const std::time_t epoch = timegm(&time);
  if (epoch == static_cast<std::time_t>(-1)) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "authority expires_at is outside supported range");
  }
  auto result = std::chrono::system_clock::from_time_t(epoch);

  if (match[7].matched) {
    std::string fraction = match[7].str();
    fraction.append(9U - fraction.size(), '0');
    result += std::chrono::nanoseconds(std::stoll(fraction));
  }
  if (match[8].matched && match[8].str() != "Z") {
    const std::string offset = match[8].str();
    const int sign = offset.front() == '+' ? 1 : -1;
    const int hours = std::stoi(offset.substr(1, 2));
    const int minutes = std::stoi(offset.substr(4, 2));
    result -= sign * (std::chrono::hours(hours) + std::chrono::minutes(minutes));
  }
  return result;
}

template <typename T>
[[nodiscard]] T value_or(const contracts::Json &value, const char *name,
                         T fallback) {
  const auto iterator = value.find(name);
  return iterator == value.end() ? std::move(fallback) : iterator->get<T>();
}

} // namespace

contracts::Json to_json(const AuthorityManifest &manifest) {
  return {
      {"allow_interactive_l2", manifest.allow_interactive_l2},
      {"allow_l1_auto_apply", manifest.allow_l1_auto_apply},
      {"allow_yolo", manifest.allow_yolo},
      {"allowed_paths", manifest.allowed_paths},
      {"authority_hash", manifest.authority_hash},
      {"command_capabilities", manifest.command_capabilities},
      {"expires_at", manifest.expires_at},
      {"forbidden_paths", manifest.forbidden_paths},
      {"goal", manifest.goal},
      {"mandatory_evidence", manifest.mandatory_evidence},
      {"mandatory_tests", manifest.mandatory_tests},
      {"max_automatic_risk", manifest.max_automatic_risk},
      {"max_retries_per_action", manifest.max_retries_per_action},
      {"operator", manifest.operator_name},
      {"read_capabilities", manifest.read_capabilities},
      {"read_only", manifest.read_only},
      {"schema_version", manifest.schema_version},
      {"semantic_capabilities", manifest.semantic_capabilities},
      {"semantic_capability_ceiling", manifest.semantic_capability_ceiling},
      {"source_snapshot_hash", manifest.source_snapshot_hash},
      {"task_id", manifest.task_id},
  };
}

AuthorityManifest authority_from_json(const contracts::Json &value) {
  if (!value.is_object()) {
    throw common::Error(common::ErrorCode::json_contract,
                        "authority manifest must be a JSON object");
  }
  for (const auto &[name, unused] : value.items()) {
    static_cast<void>(unused);
    if (!authority_fields.contains(name)) {
      throw common::Error(common::ErrorCode::json_contract,
                          "unknown authority manifest field: " + name);
    }
  }

  try {
    AuthorityManifest manifest;
    manifest.schema_version = value_or<int>(value, "schema_version", 1);
    manifest.task_id = value_or<std::string>(value, "task_id", "read-only");
    manifest.goal = value_or<std::string>(value, "goal",
                                          "Read-only repository inspection");
    manifest.source_snapshot_hash =
        value_or<std::string>(value, "source_snapshot_hash", "");
    manifest.allowed_paths = value_or<std::vector<std::string>>(
        value, "allowed_paths", {"**"});
    manifest.forbidden_paths = value_or<std::vector<std::string>>(
        value, "forbidden_paths", {".ourd-agent/**"});
    manifest.read_capabilities = value_or<std::vector<std::string>>(
        value, "read_capabilities",
        {"workspace.list", "workspace.read", "workspace.search", "git.status",
         "git.diff"});
    manifest.command_capabilities = value_or<std::vector<std::string>>(
        value, "command_capabilities", {});
    manifest.semantic_capability_ceiling =
        value_or<std::string>(value, "semantic_capability_ceiling", "C1");
    manifest.semantic_capabilities = value_or<std::vector<std::string>>(
        value, "semantic_capabilities", {});
    manifest.max_retries_per_action =
        value_or<int>(value, "max_retries_per_action", 1);
    manifest.max_automatic_risk =
        value_or<std::string>(value, "max_automatic_risk", "L0");
    manifest.allow_l1_auto_apply =
        value_or<bool>(value, "allow_l1_auto_apply", false);
    manifest.allow_interactive_l2 =
        value_or<bool>(value, "allow_interactive_l2", false);
    manifest.allow_yolo = value_or<bool>(value, "allow_yolo", false);
    manifest.mandatory_tests = value_or<std::vector<std::string>>(
        value, "mandatory_tests", {});
    manifest.mandatory_evidence = value_or<std::vector<std::string>>(
        value, "mandatory_evidence", {});
    manifest.expires_at = value_or<std::string>(value, "expires_at", "");
    manifest.operator_name =
        value_or<std::string>(value, "operator", "unconfigured");
    manifest.authority_hash =
        value_or<std::string>(value, "authority_hash", "");
    manifest.read_only = value_or<bool>(value, "read_only", true);
    return manifest;
  } catch (const nlohmann::json::exception &error) {
    throw common::Error(common::ErrorCode::json_contract,
                        std::string("invalid authority manifest field: ") +
                            error.what());
  }
}

contracts::Json authority_payload(const AuthorityManifest &manifest) {
  auto payload = to_json(manifest);
  payload.erase("authority_hash");
  return payload;
}

AuthorityManifest finalize_authority(AuthorityManifest manifest) {
  manifest.authority_hash = contracts::sha256_json(authority_payload(manifest));
  return manifest;
}

AuthorityManifest read_only_authority(const Workspace &workspace) {
  AuthorityManifest manifest;
  manifest.source_snapshot_hash = workspace.snapshot_hash();
  manifest.read_only = true;
  return finalize_authority(std::move(manifest));
}

void validate_authority(const AuthorityManifest &manifest,
                        const Workspace &workspace, bool check_snapshot,
                        std::chrono::system_clock::time_point now) {
  if (manifest.schema_version != 1) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "unsupported authority schema");
  }
  if (manifest.max_automatic_risk != "L0" &&
      manifest.max_automatic_risk != "L1" &&
      manifest.max_automatic_risk != "L2") {
    throw common::Error(common::ErrorCode::policy_denied,
                        "max_automatic_risk must be L0, L1, or L2");
  }
  if (manifest.semantic_capability_ceiling.size() != 2U ||
      manifest.semantic_capability_ceiling.front() != 'C' ||
      manifest.semantic_capability_ceiling.back() < '0' ||
      manifest.semantic_capability_ceiling.back() > '5') {
    throw common::Error(common::ErrorCode::policy_denied,
                        "semantic_capability_ceiling must be C0 through C5");
  }
  if (manifest.max_retries_per_action < 0 ||
      manifest.max_retries_per_action > 10) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "max_retries_per_action must be between 0 and 10");
  }
  if (manifest.task_id.empty() || manifest.goal.empty()) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "authority task_id and goal are required");
  }
  if (manifest.allowed_paths.empty()) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "authority allowed_paths cannot be empty");
  }
  for (const auto &pattern : manifest.allowed_paths) {
    if (path_pattern_invalid(pattern)) {
      throw common::Error(common::ErrorCode::policy_denied,
                          "invalid authority path pattern");
    }
  }
  for (const auto &pattern : manifest.forbidden_paths) {
    if (path_pattern_invalid(pattern)) {
      throw common::Error(common::ErrorCode::policy_denied,
                          "invalid authority path pattern");
    }
  }
  if (!manifest.expires_at.empty() && parse_iso8601(manifest.expires_at) <= now) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "authority manifest has expired");
  }
  if (check_snapshot && !manifest.source_snapshot_hash.empty() &&
      manifest.source_snapshot_hash != workspace.snapshot_hash()) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "authority source snapshot mismatch");
  }
  if (!manifest.read_only && manifest.source_snapshot_hash.empty()) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "mutation authority requires source_snapshot_hash");
  }
  if (manifest.read_only &&
      (manifest.allow_l1_auto_apply || manifest.allow_interactive_l2 ||
       manifest.allow_yolo || !manifest.command_capabilities.empty())) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "read-only authority cannot grant mutation capabilities");
  }
  if (manifest.read_only &&
      (manifest.semantic_capability_ceiling == "C3" ||
       manifest.semantic_capability_ceiling == "C4" ||
       manifest.semantic_capability_ceiling == "C5")) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "read-only authority cannot grant C3-C5 capabilities");
  }
}

} // namespace statewright::core

