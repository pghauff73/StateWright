#include "statewright/egcf/internet_orchestration_records.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/contracts/typed_id.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace statewright::egcf {
namespace {

using Json = contracts::Json;

[[noreturn]] void orchestration_record_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

void require_nonempty(std::string_view value, std::string_view label) {
  if (value.empty()) {
    orchestration_record_error(std::string(label) + " must not be empty");
  }
}

void canonical_strings(std::vector<std::string> &values,
                       std::string_view label) {
  for (const auto &value : values) {
    require_nonempty(value, label);
  }
  std::ranges::sort(values);
  values.erase(std::unique(values.begin(), values.end()), values.end());
}

template <typename Value>
std::string signature_for(const Value &value, std::string_view key) {
  auto material = to_json(value);
  material.erase(std::string(key));
  return contracts::sha256_json(material);
}

template <typename Value, typename Canonicalizer>
Value parse_and_verify(Value value, const Json &source,
                       Canonicalizer canonicalizer) {
  const auto canonical = canonicalizer(std::move(value));
  if (to_json(canonical) != source) {
    orchestration_record_error("persisted internet orchestration record is invalid");
  }
  return canonical;
}

std::string action_key_for(const InternetDirectedAction &action) {
  const Json material = {
      {"action_algebra_version", internet_directed_action_algebra_version},
      {"dependency_action_keys", action.dependency_action_keys},
      {"expected_generation", action.expected_generation},
      {"expected_status", action.expected_status},
      {"input_ids", action.input_ids},
      {"kind", internet_directed_action_kind_name(action.kind)},
      {"parameters", action.parameters},
      {"policy_ids", action.policy_ids},
      {"protocol_ids", action.protocol_ids},
      {"subject_id", action.subject_id},
      {"subject_type", action.subject_type}};
  return contracts::sha256_json(material);
}

} // namespace

std::string_view
internet_directed_action_kind_name(InternetDirectedActionKind kind) {
  switch (kind) {
  case InternetDirectedActionKind::recover_expired_lease:
    return "RECOVER_EXPIRED_LEASE";
  case InternetDirectedActionKind::schedule_fetch:
    return "SCHEDULE_FETCH";
  case InternetDirectedActionKind::execute_fetch:
    return "EXECUTE_FETCH";
  case InternetDirectedActionKind::assess_source:
    return "ASSESS_SOURCE";
  case InternetDirectedActionKind::extract_snapshot:
    return "EXTRACT_SNAPSHOT";
  case InternetDirectedActionKind::feed_extraction:
    return "FEED_EXTRACTION";
  case InternetDirectedActionKind::reason_candidate:
    return "REASON_CANDIDATE";
  case InternetDirectedActionKind::qualify_candidate:
    return "QUALIFY_CANDIDATE";
  case InternetDirectedActionKind::assess_promotion:
    return "ASSESS_PROMOTION";
  case InternetDirectedActionKind::admit_probation:
    return "ADMIT_PROBATION";
  case InternetDirectedActionKind::select_probation_candidate:
    return "SELECT_PROBATION_CANDIDATE";
  case InternetDirectedActionKind::consume_probation_observation:
    return "CONSUME_PROBATION_OBSERVATION";
  case InternetDirectedActionKind::revalidate_source:
    return "REVALIDATE_SOURCE";
  case InternetDirectedActionKind::verify_integrity:
    return "VERIFY_INTEGRITY";
  }
  orchestration_record_error("unknown internet directed action kind");
}

InternetDirectedActionKind
internet_directed_action_kind_from_name(std::string_view name) {
  static const std::vector<std::pair<std::string_view,
                                     InternetDirectedActionKind>> values = {
      {"RECOVER_EXPIRED_LEASE",
       InternetDirectedActionKind::recover_expired_lease},
      {"SCHEDULE_FETCH", InternetDirectedActionKind::schedule_fetch},
      {"EXECUTE_FETCH", InternetDirectedActionKind::execute_fetch},
      {"ASSESS_SOURCE", InternetDirectedActionKind::assess_source},
      {"EXTRACT_SNAPSHOT", InternetDirectedActionKind::extract_snapshot},
      {"FEED_EXTRACTION", InternetDirectedActionKind::feed_extraction},
      {"REASON_CANDIDATE", InternetDirectedActionKind::reason_candidate},
      {"QUALIFY_CANDIDATE", InternetDirectedActionKind::qualify_candidate},
      {"ASSESS_PROMOTION", InternetDirectedActionKind::assess_promotion},
      {"ADMIT_PROBATION", InternetDirectedActionKind::admit_probation},
      {"SELECT_PROBATION_CANDIDATE",
       InternetDirectedActionKind::select_probation_candidate},
      {"CONSUME_PROBATION_OBSERVATION",
       InternetDirectedActionKind::consume_probation_observation},
      {"REVALIDATE_SOURCE", InternetDirectedActionKind::revalidate_source},
      {"VERIFY_INTEGRITY", InternetDirectedActionKind::verify_integrity}};
  for (const auto &[candidate, kind] : values) {
    if (candidate == name) {
      return kind;
    }
  }
  orchestration_record_error("invalid internet directed action kind");
}

std::string InternetImprovementPlan::object_id() const {
  return contracts::typed_id("internet-improvement-plan", to_json(*this));
}

std::string InternetImprovementRun::object_id() const {
  return contracts::typed_id("internet-improvement-run", to_json(*this));
}

std::string InternetImprovementRunEvent::object_id() const {
  return contracts::typed_id("internet-improvement-run-event", to_json(*this));
}

bool InternetImprovementActionLease::active() const noexcept {
  return state == "ACTIVE";
}

std::string InternetImprovementActionLease::object_id() const {
  return contracts::typed_id("internet-improvement-action-lease",
                             to_json(*this));
}

std::string InternetImprovementActionReceipt::object_id() const {
  return contracts::typed_id("internet-improvement-action-receipt",
                             to_json(*this));
}

std::string InternetExperimentProtocol::object_id() const {
  return contracts::typed_id("internet-experiment-protocol", to_json(*this));
}

std::string InternetSourceAssessmentInput::object_id() const {
  return contracts::typed_id("internet-source-assessment-input",
                             to_json(*this));
}

std::string InternetProbationObservationInput::object_id() const {
  return contracts::typed_id("internet-probation-observation-input",
                             to_json(*this));
}

InternetDirectedAction
canonical_internet_directed_action(InternetDirectedAction action) {
  require_nonempty(action.subject_id, "directed action subject ID");
  require_nonempty(action.subject_type, "directed action subject type");
  require_nonempty(action.not_before, "directed action not-before time");
  require_nonempty(action.deadline, "directed action deadline");
  if (action.expected_generation < 0 || action.priority_bp < 0 ||
      action.cost_bp < 0 || action.risk_bp < 0 || action.retry_ceiling < 0 ||
      !action.parameters.is_object()) {
    orchestration_record_error("directed action numeric bounds are invalid");
  }
  canonical_strings(action.input_ids, "directed action input ID");
  canonical_strings(action.policy_ids, "directed action policy ID");
  canonical_strings(action.protocol_ids, "directed action protocol ID");
  canonical_strings(action.dependency_action_keys,
                    "directed action dependency key");
  canonical_strings(action.blocked_reasons, "directed action blocked reason");
  action.action_key = action_key_for(action);
  action.action_signature = signature_for(action, "action_signature");
  return action;
}

InternetImprovementPlan
canonical_internet_improvement_plan(InternetImprovementPlan plan) {
  require_nonempty(plan.cycle_key, "internet improvement cycle key");
  require_nonempty(plan.baseline_event_head,
                   "internet improvement baseline event head");
  require_nonempty(plan.projection_digest,
                   "internet improvement projection digest");
  require_nonempty(plan.planned_at, "internet improvement planned time");
  require_nonempty(plan.director_version, "internet improvement director version");
  if (!plan.director_policy.is_object() || plan.allocated_provider_calls < 0 ||
      plan.allocated_cost_bp < 0 || plan.allocated_risk_bp < 0) {
    orchestration_record_error("internet improvement plan fields are invalid");
  }
  std::set<std::string> action_keys;
  for (auto &action : plan.actions) {
    action = canonical_internet_directed_action(std::move(action));
    if (!action.eligible() || !action_keys.insert(action.action_key).second) {
      orchestration_record_error("internet improvement plan action is invalid");
    }
  }
  for (auto &action : plan.deferred_actions) {
    action = canonical_internet_directed_action(std::move(action));
    if (action.eligible() || !action_keys.insert(action.action_key).second) {
      orchestration_record_error("internet improvement deferred action is invalid");
    }
  }
  plan.plan_signature = signature_for(plan, "plan_signature");
  return plan;
}

InternetImprovementRun
canonical_internet_improvement_run(InternetImprovementRun run) {
  require_nonempty(run.plan_id, "internet improvement run plan ID");
  require_nonempty(run.worker_id, "internet improvement run worker ID");
  require_nonempty(run.started_at, "internet improvement run start time");
  if (!run.requested_budgets.is_object()) {
    orchestration_record_error("internet improvement run budgets must be an object");
  }
  run.run_signature = signature_for(run, "run_signature");
  return run;
}

InternetImprovementRunEvent
canonical_internet_improvement_run_event(InternetImprovementRunEvent event) {
  require_nonempty(event.run_id, "internet improvement run-event run ID");
  require_nonempty(event.event_type, "internet improvement run-event type");
  require_nonempty(event.occurred_at,
                   "internet improvement run-event occurrence time");
  static const std::set<std::string> event_types = {
      "STARTED",          "ACTION_LEASED",   "ACTION_COMPLETED",
      "ACTION_SKIPPED",   "ACTION_FAILED",   "REPLANNED",
      "BUDGET_EXHAUSTED", "NO_ELIGIBLE_WORK", "COMPLETED",
      "ABANDONED"};
  if (!event_types.contains(event.event_type) || !event.details.is_object()) {
    orchestration_record_error("internet improvement run event is invalid");
  }
  event.event_signature = signature_for(event, "event_signature");
  return event;
}

InternetImprovementActionLease canonical_internet_improvement_action_lease(
    InternetImprovementActionLease lease) {
  require_nonempty(lease.action_key, "internet improvement lease action key");
  require_nonempty(lease.run_id, "internet improvement lease run ID");
  require_nonempty(lease.worker_id, "internet improvement lease worker ID");
  require_nonempty(lease.acquired_at, "internet improvement lease acquisition");
  require_nonempty(lease.expires_at, "internet improvement lease expiry");
  static const std::set<std::string> states = {"ACTIVE", "COMPLETED", "EXPIRED",
                                               "ABANDONED"};
  if (!states.contains(lease.state) || lease.attempt_number <= 0) {
    orchestration_record_error("internet improvement action lease is invalid");
  }
  lease.lease_signature = signature_for(lease, "lease_signature");
  return lease;
}

InternetImprovementActionReceipt
canonical_internet_improvement_action_receipt(
    InternetImprovementActionReceipt receipt) {
  require_nonempty(receipt.action_key, "internet improvement receipt action key");
  require_nonempty(receipt.plan_id, "internet improvement receipt plan ID");
  require_nonempty(receipt.run_id, "internet improvement receipt run ID");
  require_nonempty(receipt.lease_id, "internet improvement receipt lease ID");
  require_nonempty(receipt.executor_version,
                   "internet improvement receipt executor version");
  require_nonempty(receipt.started_at,
                   "internet improvement receipt start time");
  require_nonempty(receipt.completed_at,
                   "internet improvement receipt completion time");
  require_nonempty(receipt.terminal_state,
                   "internet improvement receipt terminal state");
  require_nonempty(receipt.disposition,
                   "internet improvement receipt disposition");
  static const std::set<std::string> terminal_states = {
      "COMPLETED", "SKIPPED", "FAILED", "STALE"};
  static const std::set<std::string> dispositions = {
      "EXECUTED", "RECONCILED", "STALE", "SKIPPED"};
  if (!terminal_states.contains(receipt.terminal_state) ||
      !dispositions.contains(receipt.disposition) ||
      !receipt.expected_preconditions.is_object() ||
      !receipt.observed_preconditions.is_object()) {
    orchestration_record_error("internet improvement action receipt is invalid");
  }
  canonical_strings(receipt.input_ids, "internet improvement receipt input ID");
  canonical_strings(receipt.output_ids,
                    "internet improvement receipt output ID");
  receipt.result_signature = contracts::sha256_json(
      {{"action_key", receipt.action_key},
       {"disposition", receipt.disposition},
       {"error_code", receipt.error_code},
       {"output_ids", receipt.output_ids},
       {"terminal_state", receipt.terminal_state}});
  receipt.receipt_signature = signature_for(receipt, "receipt_signature");
  return receipt;
}

InternetExperimentProtocol
canonical_internet_experiment_protocol(InternetExperimentProtocol protocol) {
  require_nonempty(protocol.protocol_version,
                   "internet experiment protocol version");
  require_nonempty(protocol.baseline_ref,
                   "internet experiment protocol baseline reference");
  require_nonempty(protocol.valid_from,
                   "internet experiment protocol valid-from time");
  if (!protocol.baseline_saa_ir.is_object() || !protocol.trial_groups.is_array() ||
      !protocol.benchmark_track_scores.is_object() ||
      !protocol.benchmark_policy.is_object() ||
      !protocol.integrity_snapshots.is_array() ||
      !protocol.integrity_policy.is_object() ||
      !protocol.source_provenance.is_object() ||
      protocol.minimum_trials_per_group <= 0 ||
      protocol.minimum_experiments <= 0 ||
      protocol.minimum_independence_groups <= 0 ||
      protocol.maximum_total_trials <= 0) {
    orchestration_record_error("internet experiment protocol is invalid");
  }
  canonical_strings(protocol.applicable_candidate_statuses,
                    "applicable candidate status");
  canonical_strings(protocol.applicable_primitives, "applicable primitive");
  canonical_strings(protocol.applicable_domains, "applicable domain");
  canonical_strings(protocol.dataset_snapshot_ids, "dataset snapshot ID");
  if (protocol.applicable_candidate_statuses.empty() ||
      protocol.trial_groups.empty()) {
    orchestration_record_error("internet experiment protocol is incomplete");
  }
  protocol.protocol_signature = signature_for(protocol, "protocol_signature");
  return protocol;
}

InternetSourceAssessmentInput
canonical_internet_source_assessment_input(InternetSourceAssessmentInput input) {
  require_nonempty(input.snapshot_id, "source assessment input snapshot ID");
  require_nonempty(input.fetch_receipt_id,
                   "source assessment input fetch receipt ID");
  require_nonempty(input.source_policy_id,
                   "source assessment input policy ID");
  require_nonempty(input.license_classification,
                   "source assessment input license classification");
  require_nonempty(input.producer_identity,
                   "source assessment input producer identity");
  if (input.evidence_ids.empty() || !input.provenance.is_object()) {
    orchestration_record_error("source assessment input is invalid");
  }
  canonical_strings(input.evidence_ids, "source assessment evidence ID");
  input.assessment_input_signature =
      signature_for(input, "assessment_input_signature");
  return input;
}

InternetProbationObservationInput
canonical_internet_probation_observation_input(
    InternetProbationObservationInput input) {
  require_nonempty(input.candidate_id, "probation input candidate ID");
  require_nonempty(input.admission_id, "probation input admission ID");
  require_nonempty(input.query_signature, "probation input query signature");
  require_nonempty(input.context_signature, "probation input context signature");
  require_nonempty(input.observed_at, "probation input observation time");
  require_nonempty(input.producer_identity, "probation input producer identity");
  if (input.window_index < 0 || input.evidence_ids.empty() ||
      !input.regression_signals.is_object() || !input.provenance.is_object()) {
    orchestration_record_error("probation observation input is invalid");
  }
  canonical_strings(input.evidence_ids, "probation observation evidence ID");
  input.observation_input_signature =
      signature_for(input, "observation_input_signature");
  return input;
}

InternetDirectedAction
internet_directed_action_from_json(const Json &value) {
  return parse_and_verify(
      InternetDirectedAction{
       .schema_version = value.at("schema_version").get<int>(),
       .kind = internet_directed_action_kind_from_name(
           value.at("kind").get<std::string>()),
       .subject_id = value.at("subject_id").get<std::string>(),
       .subject_type = value.at("subject_type").get<std::string>(),
       .expected_status = value.at("expected_status").get<std::string>(),
       .expected_generation = value.at("expected_generation").get<int>(),
       .input_ids = value.at("input_ids").get<std::vector<std::string>>(),
       .policy_ids = value.at("policy_ids").get<std::vector<std::string>>(),
       .protocol_ids = value.at("protocol_ids").get<std::vector<std::string>>(),
       .dependency_action_keys =
           value.at("dependency_action_keys").get<std::vector<std::string>>(),
       .parameters = value.at("parameters"),
       .not_before = value.at("not_before").get<std::string>(),
       .deadline = value.at("deadline").get<std::string>(),
       .priority_bp = value.at("priority_bp").get<int>(),
       .cost_bp = value.at("cost_bp").get<int>(),
       .risk_bp = value.at("risk_bp").get<int>(),
       .response_byte_budget = value.at("response_byte_budget").get<std::size_t>(),
       .cpu_unit_budget = value.at("cpu_unit_budget").get<std::size_t>(),
       .retry_ceiling = value.at("retry_ceiling").get<int>(),
       .blocked_reasons =
           value.at("blocked_reasons").get<std::vector<std::string>>(),
       .action_key = value.at("action_key").get<std::string>(),
       .action_signature = value.at("action_signature").get<std::string>()},
      value, canonical_internet_directed_action);
}

InternetImprovementPlan
internet_improvement_plan_from_json(const Json &value) {
  std::vector<InternetDirectedAction> actions;
  for (const auto &item : value.at("actions")) {
    actions.push_back(internet_directed_action_from_json(item));
  }
  std::vector<InternetDirectedAction> deferred;
  for (const auto &item : value.at("deferred_actions")) {
    deferred.push_back(internet_directed_action_from_json(item));
  }
  return parse_and_verify(
      InternetImprovementPlan{
       .schema_version = value.at("schema_version").get<int>(),
       .cycle_key = value.at("cycle_key").get<std::string>(),
       .baseline_event_head =
           value.at("baseline_event_head").get<std::string>(),
       .projection_digest = value.at("projection_digest").get<std::string>(),
       .director_policy = value.at("director_policy"),
       .planned_at = value.at("planned_at").get<std::string>(),
       .director_version = value.at("director_version").get<std::string>(),
       .actions = std::move(actions),
       .deferred_actions = std::move(deferred),
       .allocated_response_bytes =
           value.at("allocated_response_bytes").get<std::size_t>(),
       .allocated_cpu_units =
           value.at("allocated_cpu_units").get<std::size_t>(),
       .allocated_provider_calls =
           value.at("allocated_provider_calls").get<int>(),
       .allocated_cost_bp = value.at("allocated_cost_bp").get<int>(),
       .allocated_risk_bp = value.at("allocated_risk_bp").get<int>(),
       .plan_signature = value.at("plan_signature").get<std::string>()},
      value, canonical_internet_improvement_plan);
}

InternetImprovementRun internet_improvement_run_from_json(const Json &value) {
  return parse_and_verify(
      InternetImprovementRun{
       .schema_version = value.at("schema_version").get<int>(),
       .plan_id = value.at("plan_id").get<std::string>(),
       .worker_id = value.at("worker_id").get<std::string>(),
       .started_at = value.at("started_at").get<std::string>(),
       .resume_of_run_id = value.at("resume_of_run_id").get<std::string>(),
       .requested_budgets = value.at("requested_budgets"),
       .run_signature = value.at("run_signature").get<std::string>()},
      value, canonical_internet_improvement_run);
}

InternetImprovementRunEvent
internet_improvement_run_event_from_json(const Json &value) {
  return parse_and_verify(
      InternetImprovementRunEvent{
       .schema_version = value.at("schema_version").get<int>(),
       .run_id = value.at("run_id").get<std::string>(),
       .event_type = value.at("event_type").get<std::string>(),
       .action_key = value.at("action_key").get<std::string>(),
       .occurred_at = value.at("occurred_at").get<std::string>(),
       .details = value.at("details"),
       .event_signature = value.at("event_signature").get<std::string>()},
      value, canonical_internet_improvement_run_event);
}

InternetImprovementActionLease
internet_improvement_action_lease_from_json(const Json &value) {
  return parse_and_verify(
      InternetImprovementActionLease{
       .schema_version = value.at("schema_version").get<int>(),
       .action_key = value.at("action_key").get<std::string>(),
       .run_id = value.at("run_id").get<std::string>(),
       .worker_id = value.at("worker_id").get<std::string>(),
       .acquired_at = value.at("acquired_at").get<std::string>(),
       .expires_at = value.at("expires_at").get<std::string>(),
       .predecessor_lease_id =
           value.at("predecessor_lease_id").get<std::string>(),
       .attempt_number = value.at("attempt_number").get<int>(),
       .state = value.at("state").get<std::string>(),
       .lease_signature = value.at("lease_signature").get<std::string>()},
      value, canonical_internet_improvement_action_lease);
}

InternetImprovementActionReceipt
internet_improvement_action_receipt_from_json(const Json &value) {
  return parse_and_verify(
      InternetImprovementActionReceipt{
       .schema_version = value.at("schema_version").get<int>(),
       .action_key = value.at("action_key").get<std::string>(),
       .plan_id = value.at("plan_id").get<std::string>(),
       .run_id = value.at("run_id").get<std::string>(),
       .lease_id = value.at("lease_id").get<std::string>(),
       .expected_preconditions = value.at("expected_preconditions"),
       .observed_preconditions = value.at("observed_preconditions"),
       .input_ids = value.at("input_ids").get<std::vector<std::string>>(),
       .output_ids = value.at("output_ids").get<std::vector<std::string>>(),
       .executor_version = value.at("executor_version").get<std::string>(),
       .provider_identity = value.at("provider_identity").get<std::string>(),
       .model_identity = value.at("model_identity").get<std::string>(),
       .started_at = value.at("started_at").get<std::string>(),
       .completed_at = value.at("completed_at").get<std::string>(),
       .terminal_state = value.at("terminal_state").get<std::string>(),
       .error_code = value.at("error_code").get<std::string>(),
       .diagnostic = value.at("diagnostic").get<std::string>(),
       .disposition = value.at("disposition").get<std::string>(),
       .result_signature = value.at("result_signature").get<std::string>(),
       .receipt_signature = value.at("receipt_signature").get<std::string>()},
      value, canonical_internet_improvement_action_receipt);
}

InternetExperimentProtocol
internet_experiment_protocol_from_json(const Json &value) {
  return parse_and_verify(
      InternetExperimentProtocol{
       .schema_version = value.at("schema_version").get<int>(),
       .protocol_version = value.at("protocol_version").get<std::string>(),
       .applicable_candidate_statuses =
           value.at("applicable_candidate_statuses")
               .get<std::vector<std::string>>(),
       .applicable_primitives =
           value.at("applicable_primitives").get<std::vector<std::string>>(),
       .applicable_domains =
           value.at("applicable_domains").get<std::vector<std::string>>(),
       .baseline_ref = value.at("baseline_ref").get<std::string>(),
       .baseline_saa_ir = value.at("baseline_saa_ir"),
       .dataset_snapshot_ids =
           value.at("dataset_snapshot_ids").get<std::vector<std::string>>(),
       .trial_groups = value.at("trial_groups"),
       .minimum_material_effect =
           value.at("minimum_material_effect").get<std::string>(),
       .minimum_output = value.at("minimum_output").get<std::string>(),
       .maximum_output = value.at("maximum_output").get<std::string>(),
       .minimum_trials_per_group =
           value.at("minimum_trials_per_group").get<int>(),
       .minimum_experiments = value.at("minimum_experiments").get<int>(),
       .minimum_independence_groups =
           value.at("minimum_independence_groups").get<int>(),
       .maximum_total_trials = value.at("maximum_total_trials").get<int>(),
       .benchmark_track_scores = value.at("benchmark_track_scores"),
       .benchmark_policy = value.at("benchmark_policy"),
       .integrity_snapshots = value.at("integrity_snapshots"),
       .integrity_policy = value.at("integrity_policy"),
       .independent_review = value.at("independent_review").get<bool>(),
       .valid_from = value.at("valid_from").get<std::string>(),
       .valid_until = value.at("valid_until").get<std::string>(),
       .supersedes_protocol_id =
           value.at("supersedes_protocol_id").get<std::string>(),
       .source_provenance = value.at("source_provenance"),
       .protocol_signature = value.at("protocol_signature").get<std::string>()},
      value, canonical_internet_experiment_protocol);
}

InternetSourceAssessmentInput
internet_source_assessment_input_from_json(const Json &value) {
  return parse_and_verify(
      InternetSourceAssessmentInput{
          .schema_version = value.at("schema_version").get<int>(),
          .snapshot_id = value.at("snapshot_id").get<std::string>(),
          .fetch_receipt_id =
              value.at("fetch_receipt_id").get<std::string>(),
          .source_policy_id =
              value.at("source_policy_id").get<std::string>(),
          .robots_allowed = value.at("robots_allowed").get<bool>(),
          .license_classification =
              value.at("license_classification").get<std::string>(),
          .evidence_ids =
              value.at("evidence_ids").get<std::vector<std::string>>(),
          .producer_identity =
              value.at("producer_identity").get<std::string>(),
          .provenance = value.at("provenance"),
          .assessment_input_signature =
              value.at("assessment_input_signature").get<std::string>()},
      value, canonical_internet_source_assessment_input);
}

InternetProbationObservationInput
internet_probation_observation_input_from_json(const Json &value) {
  return parse_and_verify(
      InternetProbationObservationInput{
       .schema_version = value.at("schema_version").get<int>(),
       .candidate_id = value.at("candidate_id").get<std::string>(),
       .admission_id = value.at("admission_id").get<std::string>(),
       .query_signature = value.at("query_signature").get<std::string>(),
       .context_signature = value.at("context_signature").get<std::string>(),
       .observed_at = value.at("observed_at").get<std::string>(),
       .window_index = value.at("window_index").get<int>(),
       .candidate_correct = value.at("candidate_correct").get<bool>(),
       .baseline_correct = value.at("baseline_correct").get<bool>(),
       .invariant_passed = value.at("invariant_passed").get<bool>(),
       .benchmark_passed = value.at("benchmark_passed").get<bool>(),
       .integrity_passed = value.at("integrity_passed").get<bool>(),
       .source_valid = value.at("source_valid").get<bool>(),
       .reproduction_passed = value.at("reproduction_passed").get<bool>(),
       .evidence_ids = value.at("evidence_ids").get<std::vector<std::string>>(),
       .regression_signals = value.at("regression_signals"),
       .producer_identity = value.at("producer_identity").get<std::string>(),
       .provenance = value.at("provenance"),
       .observation_input_signature =
           value.at("observation_input_signature").get<std::string>()},
      value, canonical_internet_probation_observation_input);
}

Json to_json(const InternetDirectedAction &value) {
  return {{"action_key", value.action_key},
          {"action_signature", value.action_signature},
          {"blocked_reasons", value.blocked_reasons},
          {"cost_bp", value.cost_bp},
          {"cpu_unit_budget", value.cpu_unit_budget},
          {"deadline", value.deadline},
          {"dependency_action_keys", value.dependency_action_keys},
          {"expected_generation", value.expected_generation},
          {"expected_status", value.expected_status},
          {"input_ids", value.input_ids},
          {"kind", internet_directed_action_kind_name(value.kind)},
          {"not_before", value.not_before},
          {"parameters", value.parameters},
          {"policy_ids", value.policy_ids},
          {"priority_bp", value.priority_bp},
          {"protocol_ids", value.protocol_ids},
          {"response_byte_budget", value.response_byte_budget},
          {"retry_ceiling", value.retry_ceiling},
          {"risk_bp", value.risk_bp},
          {"schema_version", value.schema_version},
          {"subject_id", value.subject_id},
          {"subject_type", value.subject_type}};
}

Json to_json(const InternetImprovementPlan &value) {
  Json actions = Json::array();
  for (const auto &action : value.actions) {
    actions.push_back(to_json(action));
  }
  Json deferred = Json::array();
  for (const auto &action : value.deferred_actions) {
    deferred.push_back(to_json(action));
  }
  return {{"actions", std::move(actions)},
          {"allocated_cost_bp", value.allocated_cost_bp},
          {"allocated_cpu_units", value.allocated_cpu_units},
          {"allocated_provider_calls", value.allocated_provider_calls},
          {"allocated_response_bytes", value.allocated_response_bytes},
          {"allocated_risk_bp", value.allocated_risk_bp},
          {"baseline_event_head", value.baseline_event_head},
          {"cycle_key", value.cycle_key},
          {"deferred_actions", std::move(deferred)},
          {"director_policy", value.director_policy},
          {"director_version", value.director_version},
          {"plan_signature", value.plan_signature},
          {"planned_at", value.planned_at},
          {"projection_digest", value.projection_digest},
          {"schema_version", value.schema_version}};
}

Json to_json(const InternetImprovementRun &value) {
  return {{"plan_id", value.plan_id},
          {"requested_budgets", value.requested_budgets},
          {"resume_of_run_id", value.resume_of_run_id},
          {"run_signature", value.run_signature},
          {"schema_version", value.schema_version},
          {"started_at", value.started_at},
          {"worker_id", value.worker_id}};
}

Json to_json(const InternetImprovementRunEvent &value) {
  return {{"action_key", value.action_key},
          {"details", value.details},
          {"event_signature", value.event_signature},
          {"event_type", value.event_type},
          {"occurred_at", value.occurred_at},
          {"run_id", value.run_id},
          {"schema_version", value.schema_version}};
}

Json to_json(const InternetImprovementActionLease &value) {
  return {{"acquired_at", value.acquired_at},
          {"action_key", value.action_key},
          {"attempt_number", value.attempt_number},
          {"expires_at", value.expires_at},
          {"lease_signature", value.lease_signature},
          {"predecessor_lease_id", value.predecessor_lease_id},
          {"run_id", value.run_id},
          {"schema_version", value.schema_version},
          {"state", value.state},
          {"worker_id", value.worker_id}};
}

Json to_json(const InternetImprovementActionReceipt &value) {
  return {{"action_key", value.action_key},
          {"completed_at", value.completed_at},
          {"diagnostic", value.diagnostic},
          {"disposition", value.disposition},
          {"error_code", value.error_code},
          {"executor_version", value.executor_version},
          {"expected_preconditions", value.expected_preconditions},
          {"input_ids", value.input_ids},
          {"lease_id", value.lease_id},
          {"model_identity", value.model_identity},
          {"observed_preconditions", value.observed_preconditions},
          {"output_ids", value.output_ids},
          {"plan_id", value.plan_id},
          {"provider_identity", value.provider_identity},
          {"receipt_signature", value.receipt_signature},
          {"result_signature", value.result_signature},
          {"run_id", value.run_id},
          {"schema_version", value.schema_version},
          {"started_at", value.started_at},
          {"terminal_state", value.terminal_state}};
}

Json to_json(const InternetExperimentProtocol &value) {
  return {{"applicable_candidate_statuses",
           value.applicable_candidate_statuses},
          {"applicable_domains", value.applicable_domains},
          {"applicable_primitives", value.applicable_primitives},
          {"baseline_ref", value.baseline_ref},
          {"baseline_saa_ir", value.baseline_saa_ir},
          {"benchmark_policy", value.benchmark_policy},
          {"benchmark_track_scores", value.benchmark_track_scores},
          {"dataset_snapshot_ids", value.dataset_snapshot_ids},
          {"independent_review", value.independent_review},
          {"integrity_policy", value.integrity_policy},
          {"integrity_snapshots", value.integrity_snapshots},
          {"maximum_output", value.maximum_output},
          {"maximum_total_trials", value.maximum_total_trials},
          {"minimum_experiments", value.minimum_experiments},
          {"minimum_independence_groups",
           value.minimum_independence_groups},
          {"minimum_material_effect", value.minimum_material_effect},
          {"minimum_output", value.minimum_output},
          {"minimum_trials_per_group", value.minimum_trials_per_group},
          {"protocol_signature", value.protocol_signature},
          {"protocol_version", value.protocol_version},
          {"schema_version", value.schema_version},
          {"source_provenance", value.source_provenance},
          {"supersedes_protocol_id", value.supersedes_protocol_id},
          {"trial_groups", value.trial_groups},
          {"valid_from", value.valid_from},
          {"valid_until", value.valid_until}};
}

Json to_json(const InternetSourceAssessmentInput &value) {
  return {{"assessment_input_signature", value.assessment_input_signature},
          {"evidence_ids", value.evidence_ids},
          {"fetch_receipt_id", value.fetch_receipt_id},
          {"license_classification", value.license_classification},
          {"producer_identity", value.producer_identity},
          {"provenance", value.provenance},
          {"robots_allowed", value.robots_allowed},
          {"schema_version", value.schema_version},
          {"snapshot_id", value.snapshot_id},
          {"source_policy_id", value.source_policy_id}};
}

Json to_json(const InternetProbationObservationInput &value) {
  return {{"admission_id", value.admission_id},
          {"baseline_correct", value.baseline_correct},
          {"benchmark_passed", value.benchmark_passed},
          {"candidate_correct", value.candidate_correct},
          {"candidate_id", value.candidate_id},
          {"context_signature", value.context_signature},
          {"evidence_ids", value.evidence_ids},
          {"integrity_passed", value.integrity_passed},
          {"invariant_passed", value.invariant_passed},
          {"observation_input_signature", value.observation_input_signature},
          {"observed_at", value.observed_at},
          {"producer_identity", value.producer_identity},
          {"provenance", value.provenance},
          {"query_signature", value.query_signature},
          {"regression_signals", value.regression_signals},
          {"reproduction_passed", value.reproduction_passed},
          {"schema_version", value.schema_version},
          {"source_valid", value.source_valid},
          {"window_index", value.window_index}};
}

} // namespace statewright::egcf
