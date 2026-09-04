#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::egcf {

inline constexpr std::string_view internet_orchestration_records_version =
    "statewright-internet-orchestration-records-v1";
inline constexpr std::string_view internet_directed_action_algebra_version =
    "statewright-internet-directed-action-algebra-v1";

enum class InternetDirectedActionKind {
  recover_expired_lease,
  schedule_fetch,
  execute_fetch,
  assess_source,
  extract_snapshot,
  feed_extraction,
  reason_candidate,
  qualify_candidate,
  assess_promotion,
  admit_probation,
  select_probation_candidate,
  consume_probation_observation,
  revalidate_source,
  verify_integrity
};

[[nodiscard]] std::string_view
internet_directed_action_kind_name(InternetDirectedActionKind kind);
[[nodiscard]] InternetDirectedActionKind
internet_directed_action_kind_from_name(std::string_view name);

struct InternetDirectedAction final {
  int schema_version = 1;
  InternetDirectedActionKind kind = InternetDirectedActionKind::verify_integrity;
  std::string subject_id;
  std::string subject_type;
  std::string expected_status;
  int expected_generation = 0;
  std::vector<std::string> input_ids;
  std::vector<std::string> policy_ids;
  std::vector<std::string> protocol_ids;
  std::vector<std::string> dependency_action_keys;
  contracts::Json parameters = contracts::Json::object();
  std::string not_before;
  std::string deadline;
  int priority_bp = 0;
  int cost_bp = 0;
  int risk_bp = 0;
  std::size_t response_byte_budget = 0U;
  std::size_t cpu_unit_budget = 0U;
  int retry_ceiling = 0;
  std::vector<std::string> blocked_reasons;
  std::string action_key;
  std::string action_signature;

  [[nodiscard]] bool eligible() const noexcept {
    return blocked_reasons.empty();
  }
};

struct InternetImprovementPlan final {
  int schema_version = 1;
  std::string cycle_key;
  std::string baseline_event_head;
  std::string projection_digest;
  contracts::Json director_policy = contracts::Json::object();
  std::string planned_at;
  std::string director_version;
  std::vector<InternetDirectedAction> actions;
  std::vector<InternetDirectedAction> deferred_actions;
  std::size_t allocated_response_bytes = 0U;
  std::size_t allocated_cpu_units = 0U;
  int allocated_provider_calls = 0;
  int allocated_cost_bp = 0;
  int allocated_risk_bp = 0;
  std::string plan_signature;

  [[nodiscard]] std::string object_id() const;
};

struct InternetImprovementRun final {
  int schema_version = 1;
  std::string plan_id;
  std::string worker_id;
  std::string started_at;
  std::string resume_of_run_id;
  contracts::Json requested_budgets = contracts::Json::object();
  std::string run_signature;

  [[nodiscard]] std::string object_id() const;
};

struct InternetImprovementRunEvent final {
  int schema_version = 1;
  std::string run_id;
  std::string event_type;
  std::string action_key;
  std::string occurred_at;
  contracts::Json details = contracts::Json::object();
  std::string event_signature;

  [[nodiscard]] std::string object_id() const;
};

struct InternetImprovementActionLease final {
  int schema_version = 1;
  std::string action_key;
  std::string run_id;
  std::string worker_id;
  std::string acquired_at;
  std::string expires_at;
  std::string predecessor_lease_id;
  int attempt_number = 1;
  std::string state = "ACTIVE";
  std::string lease_signature;

  [[nodiscard]] bool active() const noexcept;
  [[nodiscard]] std::string object_id() const;
};

struct InternetImprovementActionReceipt final {
  int schema_version = 1;
  std::string action_key;
  std::string plan_id;
  std::string run_id;
  std::string lease_id;
  contracts::Json expected_preconditions = contracts::Json::object();
  contracts::Json observed_preconditions = contracts::Json::object();
  std::vector<std::string> input_ids;
  std::vector<std::string> output_ids;
  std::string executor_version;
  std::string provider_identity;
  std::string model_identity;
  std::string started_at;
  std::string completed_at;
  std::string terminal_state;
  std::string error_code;
  std::string diagnostic;
  std::string disposition;
  std::string result_signature;
  std::string receipt_signature;

  [[nodiscard]] std::string object_id() const;
};

struct InternetExperimentProtocol final {
  int schema_version = 1;
  std::string protocol_version;
  std::vector<std::string> applicable_candidate_statuses;
  std::vector<std::string> applicable_primitives;
  std::vector<std::string> applicable_domains;
  std::string baseline_ref;
  contracts::Json baseline_saa_ir = contracts::Json::object();
  std::vector<std::string> dataset_snapshot_ids;
  contracts::Json trial_groups = contracts::Json::array();
  std::string minimum_material_effect = "0";
  std::string minimum_output = "-1000000";
  std::string maximum_output = "1000000";
  int minimum_trials_per_group = 1;
  int minimum_experiments = 2;
  int minimum_independence_groups = 2;
  int maximum_total_trials = 10000;
  contracts::Json benchmark_track_scores = contracts::Json::object();
  contracts::Json benchmark_policy = contracts::Json::object();
  contracts::Json integrity_snapshots = contracts::Json::array();
  contracts::Json integrity_policy = contracts::Json::object();
  bool independent_review = true;
  std::string valid_from;
  std::string valid_until;
  std::string supersedes_protocol_id;
  contracts::Json source_provenance = contracts::Json::object();
  std::string protocol_signature;

  [[nodiscard]] std::string object_id() const;
};

struct InternetSourceAssessmentInput final {
  int schema_version = 1;
  std::string snapshot_id;
  std::string fetch_receipt_id;
  std::string source_policy_id;
  bool robots_allowed = false;
  std::string license_classification;
  std::vector<std::string> evidence_ids;
  std::string producer_identity;
  contracts::Json provenance = contracts::Json::object();
  std::string assessment_input_signature;

  [[nodiscard]] std::string object_id() const;
};

struct InternetProbationObservationInput final {
  int schema_version = 1;
  std::string candidate_id;
  std::string admission_id;
  std::string query_signature;
  std::string context_signature;
  std::string observed_at;
  int window_index = 0;
  bool candidate_correct = false;
  bool baseline_correct = false;
  bool invariant_passed = false;
  bool benchmark_passed = false;
  bool integrity_passed = false;
  bool source_valid = false;
  bool reproduction_passed = false;
  std::vector<std::string> evidence_ids;
  contracts::Json regression_signals = contracts::Json::object();
  std::string producer_identity;
  contracts::Json provenance = contracts::Json::object();
  std::string observation_input_signature;

  [[nodiscard]] std::string object_id() const;
};

[[nodiscard]] InternetDirectedAction
canonical_internet_directed_action(InternetDirectedAction action);
[[nodiscard]] InternetImprovementPlan
canonical_internet_improvement_plan(InternetImprovementPlan plan);
[[nodiscard]] InternetImprovementRun
canonical_internet_improvement_run(InternetImprovementRun run);
[[nodiscard]] InternetImprovementRunEvent
canonical_internet_improvement_run_event(InternetImprovementRunEvent event);
[[nodiscard]] InternetImprovementActionLease
canonical_internet_improvement_action_lease(
    InternetImprovementActionLease lease);
[[nodiscard]] InternetImprovementActionReceipt
canonical_internet_improvement_action_receipt(
    InternetImprovementActionReceipt receipt);
[[nodiscard]] InternetExperimentProtocol
canonical_internet_experiment_protocol(InternetExperimentProtocol protocol);
[[nodiscard]] InternetSourceAssessmentInput
canonical_internet_source_assessment_input(InternetSourceAssessmentInput input);
[[nodiscard]] InternetProbationObservationInput
canonical_internet_probation_observation_input(
    InternetProbationObservationInput input);

[[nodiscard]] InternetDirectedAction
internet_directed_action_from_json(const contracts::Json &value);
[[nodiscard]] InternetImprovementPlan
internet_improvement_plan_from_json(const contracts::Json &value);
[[nodiscard]] InternetImprovementRun
internet_improvement_run_from_json(const contracts::Json &value);
[[nodiscard]] InternetImprovementRunEvent
internet_improvement_run_event_from_json(const contracts::Json &value);
[[nodiscard]] InternetImprovementActionLease
internet_improvement_action_lease_from_json(const contracts::Json &value);
[[nodiscard]] InternetImprovementActionReceipt
internet_improvement_action_receipt_from_json(const contracts::Json &value);
[[nodiscard]] InternetExperimentProtocol
internet_experiment_protocol_from_json(const contracts::Json &value);
[[nodiscard]] InternetSourceAssessmentInput
internet_source_assessment_input_from_json(const contracts::Json &value);
[[nodiscard]] InternetProbationObservationInput
internet_probation_observation_input_from_json(const contracts::Json &value);

[[nodiscard]] contracts::Json to_json(const InternetDirectedAction &value);
[[nodiscard]] contracts::Json to_json(const InternetImprovementPlan &value);
[[nodiscard]] contracts::Json to_json(const InternetImprovementRun &value);
[[nodiscard]] contracts::Json to_json(const InternetImprovementRunEvent &value);
[[nodiscard]] contracts::Json
to_json(const InternetImprovementActionLease &value);
[[nodiscard]] contracts::Json
to_json(const InternetImprovementActionReceipt &value);
[[nodiscard]] contracts::Json to_json(const InternetExperimentProtocol &value);
[[nodiscard]] contracts::Json
to_json(const InternetSourceAssessmentInput &value);
[[nodiscard]] contracts::Json
to_json(const InternetProbationObservationInput &value);

} // namespace statewright::egcf
