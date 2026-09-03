#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::egcf {

struct CommandDefinition final {
  std::string namespace_name;
  std::string name;
  int version = 1;
  std::vector<std::string> intent_kinds;
  contracts::Json input_schema = contracts::Json::object();
  contracts::Json output_schema = contracts::Json::object();
  std::vector<std::string> preconditions;
  std::vector<std::string> postconditions;
  std::vector<std::string> invariants;
  std::vector<std::string> evidence_requirements;
  contracts::Json capability_query = contracts::Json::object();
  contracts::Json algorithm_query = contracts::Json::object();
  std::string risk_policy;
  std::string rollback_policy;
  contracts::Json budget_policy = contracts::Json::object();
  std::string approval_policy;
  contracts::Json lifecycle_policy = contracts::Json::object();
  std::string description;
  std::vector<std::string> aliases;

  [[nodiscard]] std::string command_id() const;
  [[nodiscard]] std::string object_id() const;
};

struct ResourceReceipt final {
  std::filesystem::path resource_root;
  std::string manifest_hash;
  std::size_t verified_files = 0;
  std::vector<std::string> verified_paths;
};

struct AlgorithmDefinition final {
  std::string name;
  int version = 1;
  std::string implementation_kind;
  std::string implementation_ref;
  std::string implementation_digest;
  std::vector<std::string> command_ids;
  contracts::Json input_schema = contracts::Json::object();
  contracts::Json output_schema = contracts::Json::object();
  contracts::Json applicability = contracts::Json::object();
  std::vector<std::string> capability_requirements;
  std::string capability_level;
  std::string risk_floor;
  std::string rollback_class;
  std::vector<std::string> invariants;
  std::vector<std::string> evidence_requirements;
  contracts::Json qualification_policy = contracts::Json::object();
  std::string owner;
  contracts::Json provenance = contracts::Json::object();
  std::string status = "PROPOSED";
  std::vector<std::string> known_failures;

  [[nodiscard]] std::string algorithm_id() const;
  [[nodiscard]] std::string object_id() const;
};

struct QualificationRecord final {
  std::string algorithm_id;
  std::string algorithm_digest;
  contracts::Json context = contracts::Json::object();
  std::string context_hash;
  std::vector<std::string> evidence_ids;
  contracts::Json tests = contracts::Json::array();
  contracts::Json benchmarks = contracts::Json::array();
  std::vector<std::string> known_failures;
  std::string status;
  std::string qualified_by;
  std::string created_at;
  std::string expires_at;

  [[nodiscard]] std::string object_id() const;
};

struct SelectionDecision final {
  std::string command_id;
  std::string context_hash;
  contracts::Json candidates = contracts::Json::array();
  contracts::Json excluded = contracts::Json::array();
  std::string selected_algorithm_id;
  std::string selected_algorithm_digest;
  std::vector<std::string> ranking;
  std::string tie_break;
  std::vector<std::string> evidence_ids;
  std::string created_at;
  contracts::Json score_components = contracts::Json::object();

  [[nodiscard]] std::string object_id() const;
};

[[nodiscard]] contracts::Json to_json(const CommandDefinition &definition);
[[nodiscard]] contracts::Json to_json(const ResourceReceipt &receipt);
[[nodiscard]] contracts::Json to_json(const AlgorithmDefinition &definition);
[[nodiscard]] contracts::Json to_json(const QualificationRecord &record);
[[nodiscard]] contracts::Json to_json(const SelectionDecision &decision);

[[nodiscard]] ResourceReceipt
verify_resource_manifest(const std::filesystem::path &resource_root);

class CommandRegistry final {
public:
  explicit CommandRegistry(std::filesystem::path resource_root);

  [[nodiscard]] const ResourceReceipt &resource_receipt() const noexcept;
  [[nodiscard]] const std::vector<CommandDefinition> &definitions() const noexcept;
  [[nodiscard]] const CommandDefinition &resolve_exact(
      std::string_view identifier) const;
  [[nodiscard]] const CommandDefinition &resolve_for_discovery(
      std::string_view identifier) const;
  [[nodiscard]] contracts::Json describe(std::string_view identifier) const;

private:
  ResourceReceipt resource_receipt_;
  std::vector<CommandDefinition> definitions_;
  std::map<std::string, std::size_t> exact_index_;
  std::map<std::string, std::string> aliases_;
};

class AlgorithmRegistry final {
public:
  explicit AlgorithmRegistry(const CommandRegistry &commands);

  [[nodiscard]] std::string register_algorithm(AlgorithmDefinition definition);
  [[nodiscard]] std::string
  register_core_algorithm(AlgorithmDefinition definition);
  [[nodiscard]] std::string
  register_qualification(QualificationRecord qualification);
  [[nodiscard]] const std::vector<AlgorithmDefinition> &algorithms() const noexcept;
  [[nodiscard]] std::vector<AlgorithmDefinition>
  search(std::string_view exact_command_id) const;
  [[nodiscard]] const AlgorithmDefinition &resolve_exact(
      std::string_view algorithm_id) const;
  [[nodiscard]] std::vector<QualificationRecord>
  qualifications(const AlgorithmDefinition &algorithm) const;

private:
  [[nodiscard]] std::string
  register_algorithm_impl(AlgorithmDefinition definition, bool trusted_core);

  const CommandRegistry &commands_;
  std::vector<AlgorithmDefinition> algorithms_;
  std::vector<QualificationRecord> qualifications_;
  std::map<std::string, std::size_t> object_index_;
  std::map<std::string, std::size_t> qualification_index_;
};

class SelectionEngine final {
public:
  explicit SelectionEngine(const AlgorithmRegistry &algorithms);

  [[nodiscard]] SelectionDecision select(
      std::string exact_command_id, contracts::Json context,
      std::string capability_ceiling,
      std::vector<std::string> allowed_capabilities,
      std::vector<std::string> invariant_names, std::string decision_time) const;

private:
  const AlgorithmRegistry &algorithms_;
};

} // namespace statewright::egcf
