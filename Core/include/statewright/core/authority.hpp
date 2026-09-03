#pragma once

#include "statewright/contracts/canonical_json.hpp"
#include "statewright/core/workspace.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace statewright::core {

struct AuthorityManifest final {
  int schema_version = 1;
  std::string task_id = "read-only";
  std::string goal = "Read-only repository inspection";
  std::string source_snapshot_hash;
  std::vector<std::string> allowed_paths{"**"};
  std::vector<std::string> forbidden_paths{".ourd-agent/**"};
  std::vector<std::string> read_capabilities{
      "workspace.list", "workspace.read", "workspace.search", "git.status",
      "git.diff"};
  std::vector<std::string> command_capabilities;
  std::string semantic_capability_ceiling = "C1";
  std::vector<std::string> semantic_capabilities;
  int max_retries_per_action = 1;
  std::string max_automatic_risk = "L0";
  bool allow_l1_auto_apply = false;
  bool allow_interactive_l2 = false;
  bool allow_yolo = false;
  std::vector<std::string> mandatory_tests;
  std::vector<std::string> mandatory_evidence;
  std::string expires_at;
  std::string operator_name = "unconfigured";
  std::string authority_hash;
  bool read_only = true;

  friend bool operator==(const AuthorityManifest &, const AuthorityManifest &) =
      default;
};

[[nodiscard]] contracts::Json to_json(const AuthorityManifest &manifest);
[[nodiscard]] AuthorityManifest authority_from_json(const contracts::Json &value);
[[nodiscard]] contracts::Json authority_payload(const AuthorityManifest &manifest);
[[nodiscard]] AuthorityManifest finalize_authority(AuthorityManifest manifest);
[[nodiscard]] AuthorityManifest read_only_authority(const Workspace &workspace);

void validate_authority(
    const AuthorityManifest &manifest, const Workspace &workspace,
    bool check_snapshot = true,
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

} // namespace statewright::core

