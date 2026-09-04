#include "statewright/egcf/internet_improvement_orchestrator.hpp"

#include "statewright/common/error.hpp"
#include "statewright/egcf/autonomous_promotion.hpp"
#include "statewright/egcf/internet_experiment.hpp"
#include "statewright/egcf/internet_feed.hpp"
#include "statewright/egcf/internet_improvement_store.hpp"
#include "statewright/egcf/internet_probation.hpp"
#include "statewright/egcf/internet_reasoning.hpp"
#include "statewright/egcf/internet_source_coordinator.hpp"
#include "statewright/sources/scheduler.hpp"

#include <algorithm>
#include <exception>
#include <optional>
#include <set>
#include <utility>

namespace statewright::egcf {
namespace {

using Json = contracts::Json;

[[noreturn]] void orchestrator_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

void require_plan_request(const InternetImprovementRunRequest &request) {
  if (request.cycle_key.empty() || request.current_timestamp.empty()) {
    orchestrator_error("internet improvement plan request is incomplete");
  }
}

void require_run_request(const InternetImprovementRunRequest &request) {
  require_plan_request(request);
  if (request.worker_id.empty() ||
      request.action_lease_expires_at.empty() ||
      request.fetch_lease_expires_at.empty() ||
      request.action_lease_expires_at <= request.current_timestamp ||
      request.fetch_lease_expires_at <= request.current_timestamp) {
    orchestrator_error("internet improvement run request is incomplete");
  }
}

std::string status_value(const Json &payload) {
  return payload.value("status", std::string{});
}

int generation_value(std::string_view object_type, const Json &payload) {
  if (object_type == "internet-watch") {
    return payload.value("schedule_generation", 0);
  }
  if (object_type == "internet-fetch-job") {
    return payload.value("expected_watch_generation", 0);
  }
  return payload.value("generation", 0);
}

Json expected_preconditions(const InternetDirectedAction &action) {
  return {{"deadline", action.deadline},
          {"expected_generation", action.expected_generation},
          {"expected_status", action.expected_status},
          {"input_ids", action.input_ids},
          {"policy_ids", action.policy_ids},
          {"protocol_ids", action.protocol_ids},
          {"subject_id", action.subject_id},
          {"subject_type", action.subject_type}};
}

Json observed_preconditions(EgcfStore &store,
                            const InternetDirectedAction &action,
                            std::string_view current_timestamp,
                            bool &matches) {
  matches = current_timestamp >= action.not_before &&
            current_timestamp <= action.deadline;
  Json observed = {{"current_timestamp", current_timestamp},
                   {"event_head", store.event_head()},
                   {"inputs_present", true},
                   {"subject_present", false}};
  try {
    const auto subject = store.get(action.subject_id);
    observed["subject_present"] = true;
    observed["subject_type"] = subject.object_type;
    observed["subject_status"] = status_value(subject.payload);
    observed["subject_generation"] =
        generation_value(subject.object_type, subject.payload);
    matches = matches && subject.object_type == action.subject_type;
    if (!action.expected_status.empty()) {
      matches =
          matches && status_value(subject.payload) == action.expected_status;
    }
    if (action.expected_generation != 0) {
      matches = matches && generation_value(subject.object_type,
                                             subject.payload) ==
                                action.expected_generation;
    }
  } catch (const std::exception &) {
    matches = false;
  }
  for (const auto &id : action.input_ids) {
    try {
      static_cast<void>(store.get(id));
    } catch (const std::exception &) {
      observed["inputs_present"] = false;
      matches = false;
    }
  }
  for (const auto &id : action.policy_ids) {
    try {
      static_cast<void>(store.get(id));
    } catch (const std::exception &) {
      matches = false;
    }
  }
  for (const auto &id : action.protocol_ids) {
    try {
      static_cast<void>(store.get(id));
    } catch (const std::exception &) {
      matches = false;
    }
  }
  observed["matches"] = matches;
  return observed;
}

InternetImprovementRunEvent make_event(
    std::string run_id, std::string event_type, std::string occurred_at,
    std::string action_key = {}, Json details = Json::object()) {
  InternetImprovementRunEvent event;
  event.run_id = std::move(run_id);
  event.event_type = std::move(event_type);
  event.action_key = std::move(action_key);
  event.occurred_at = std::move(occurred_at);
  event.details = std::move(details);
  return canonical_internet_improvement_run_event(std::move(event));
}

InternetImprovementActionLease close_action_lease(
    const InternetImprovementActionLease &lease, std::string state) {
  if (!lease.active()) {
    orchestrator_error("only an active improvement action lease can be closed");
  }
  InternetImprovementActionLease closed = lease;
  closed.predecessor_lease_id = lease.object_id();
  closed.state = std::move(state);
  closed.lease_signature.clear();
  return canonical_internet_improvement_action_lease(std::move(closed));
}

InternetImprovementActionLease acquire_action_lease(
    InternetImprovementStore &internet, const InternetDirectedAction &action,
    std::string run_id, const InternetImprovementRunRequest &request) {
  InternetImprovementActionLease lease;
  lease.action_key = action.action_key;
  lease.run_id = std::move(run_id);
  lease.worker_id = request.worker_id;
  lease.acquired_at = request.current_timestamp;
  lease.expires_at = request.action_lease_expires_at;
  if (const auto latest = internet.latest_action_lease(action.action_key)) {
    if (latest->active()) {
      if (latest->expires_at > request.current_timestamp) {
        orchestrator_error("internet improvement action is already leased");
      }
      const auto expired = close_action_lease(*latest, "EXPIRED");
      lease.predecessor_lease_id = internet.register_improvement_action_lease(
          expired);
      lease.attempt_number = latest->attempt_number + 1;
    } else {
      lease.predecessor_lease_id = latest->object_id();
      lease.attempt_number = latest->attempt_number + 1;
    }
  }
  return canonical_internet_improvement_action_lease(std::move(lease));
}

sources::InternetExtractionResult extraction_from_store(
    EgcfStore &store, std::string_view receipt_id) {
  const auto stored = store.get(receipt_id);
  if (stored.object_type != "internet-extraction-receipt") {
    orchestrator_error("action extraction input has invalid type");
  }
  sources::InternetExtractionResult result;
  result.receipt = sources::internet_extraction_receipt_from_json(stored.payload);
  for (const auto &fragment_id : result.receipt.fragment_ids) {
    const auto fragment = store.get(fragment_id);
    if (fragment.object_type != "internet-source-fragment") {
      orchestrator_error("action extraction fragment has invalid type");
    }
    result.fragments.push_back(
        sources::internet_source_fragment_from_json(fragment.payload));
  }
  return result;
}

InternetAlgorithmCandidate candidate_from_store(EgcfStore &store,
                                                 std::string_view candidate_id) {
  const auto record = store.get(candidate_id);
  if (record.object_type != "internet-algorithm-candidate") {
    orchestrator_error("action candidate has invalid type");
  }
  return internet_algorithm_candidate_from_json(record.payload);
}

bool run_is_terminal(InternetImprovementStore &internet,
                     std::string_view run_id) {
  for (const auto &record :
       internet.list("internet-improvement-run-event")) {
    const auto event =
        internet_improvement_run_event_from_json(record.payload);
    if (event.run_id == run_id &&
        (event.event_type == "COMPLETED" ||
         event.event_type == "ABANDONED")) {
      return true;
    }
  }
  return false;
}

std::optional<std::vector<std::string>> existing_outputs_for_action(
    EgcfStore &store, InternetImprovementStore &internet,
    const InternetDirectedAction &action) {
  if (action.kind == InternetDirectedActionKind::schedule_fetch) {
    const auto job = sources::internet_fetch_job_from_json(
        action.parameters.at("job"));
    try {
      if (store.get(job.object_id()).object_type == "internet-fetch-job") {
        return std::vector<std::string>{job.object_id()};
      }
    } catch (const std::exception &) {
    }
    return std::nullopt;
  }
  if (action.kind == InternetDirectedActionKind::execute_fetch) {
    for (const auto &record : internet.list("internet-fetch-receipt")) {
      const auto receipt =
          sources::internet_fetch_receipt_from_json(record.payload);
      if (receipt.job_id == action.subject_id && receipt.successful()) {
        std::vector<std::string> outputs = {record.object_id};
        if (!receipt.snapshot_id.empty()) {
          outputs.push_back(receipt.snapshot_id);
        }
        return outputs;
      }
    }
    return std::nullopt;
  }
  if (action.kind == InternetDirectedActionKind::assess_source) {
    std::optional<InternetSourceAssessmentInput> expected_input;
    for (const auto &input_id : action.input_ids) {
      const auto input_record = store.get(input_id);
      if (input_record.object_type == "internet-source-assessment-input") {
        expected_input =
            internet_source_assessment_input_from_json(input_record.payload);
        break;
      }
    }
    if (!expected_input || expected_input->snapshot_id != action.subject_id ||
        std::find(action.policy_ids.begin(), action.policy_ids.end(),
                  expected_input->source_policy_id) == action.policy_ids.end()) {
      return std::nullopt;
    }
    for (const auto &record : internet.list("internet-policy-assessment")) {
      const auto assessment =
          sources::internet_policy_assessment_from_json(record.payload);
      if (assessment.snapshot_id == expected_input->snapshot_id &&
          assessment.fetch_receipt_id == expected_input->fetch_receipt_id &&
          assessment.source_policy_id == expected_input->source_policy_id) {
        return std::vector<std::string>{record.object_id};
      }
    }
    return std::nullopt;
  }
  if (action.kind == InternetDirectedActionKind::extract_snapshot) {
    for (const auto &record : internet.list("internet-extraction-receipt")) {
      const auto extraction =
          sources::internet_extraction_receipt_from_json(record.payload);
      if (extraction.snapshot_id == action.subject_id) {
        std::vector<std::string> outputs = {record.object_id};
        outputs.insert(outputs.end(), extraction.fragment_ids.begin(),
                       extraction.fragment_ids.end());
        return outputs;
      }
    }
  }
  return std::nullopt;
}

mpq_class exact_rational(std::string_view value) {
  try {
    mpq_class result{std::string(value)};
    result.canonicalize();
    return result;
  } catch (const std::exception &) {
    orchestrator_error("experiment protocol rational is invalid");
  }
}

std::vector<mpq_class> exact_rationals(const Json &value) {
  if (!value.is_array()) {
    orchestrator_error("experiment protocol trial values must be arrays");
  }
  std::vector<mpq_class> result;
  result.reserve(value.size());
  for (const auto &entry : value) {
    if (entry.is_string()) {
      result.push_back(exact_rational(entry.get<std::string>()));
    } else if (entry.is_number_integer()) {
      result.emplace_back(entry.get<long>());
    } else if (entry.is_number_unsigned()) {
      result.emplace_back(entry.get<unsigned long>());
    } else {
      orchestrator_error("experiment protocol trial value is not exact");
    }
  }
  return result;
}

saa::OIECBenchGatePolicy benchmark_policy_from_json(const Json &value) {
  saa::OIECBenchGatePolicy result;
  if (value.contains("minimum_track_scores")) {
    for (const auto &[name, score] :
         value.at("minimum_track_scores").items()) {
      result.minimum_track_scores.emplace_back(name, score.get<int>());
    }
  }
  result.minimum_independence_groups = value.value(
      "minimum_independence_groups", result.minimum_independence_groups);
  return result;
}

saa::KnowledgeIntegrityPolicy integrity_policy_from_json(const Json &value) {
  saa::KnowledgeIntegrityPolicy result;
  result.max_contradiction_rate_bp = value.value(
      "max_contradiction_rate_bp", result.max_contradiction_rate_bp);
  result.max_semantic_drift_rate_bp = value.value(
      "max_semantic_drift_rate_bp", result.max_semantic_drift_rate_bp);
  result.max_false_admission_rate_bp = value.value(
      "max_false_admission_rate_bp", result.max_false_admission_rate_bp);
  result.max_corrected_error_recurrence_rate_bp = value.value(
      "max_corrected_error_recurrence_rate_bp",
      result.max_corrected_error_recurrence_rate_bp);
  result.min_retrieval_precision_bp = value.value(
      "min_retrieval_precision_bp", result.min_retrieval_precision_bp);
  result.min_equivalent_failure_avoidance_bp = value.value(
      "min_equivalent_failure_avoidance_bp",
      result.min_equivalent_failure_avoidance_bp);
  return result;
}

saa::KnowledgeIntegritySnapshot integrity_snapshot_from_json(
    const Json &value) {
  return saa::make_integrity_snapshot(
      value.at("generation").get<int>(),
      value.at("canonical_knowledge_count").get<int>(),
      value.value("semantic_contradictions", 0),
      value.value("semantic_drift_events", 0),
      value.value("false_canonical_admissions", 0),
      value.value("corrected_error_opportunities", 0),
      value.value("corrected_error_recurrences", 0),
      value.value("retrieval_queries", 0),
      value.value("retrieval_correct_selections", 0),
      value.value("equivalent_failure_opportunities", 0),
      value.value("equivalent_failure_retries", 0));
}

InternetExperimentRequest experiment_request_from_protocol(
    const InternetExperimentProtocol &protocol, std::string recorded_at) {
  InternetExperimentRequest request;
  request.baseline_ref = protocol.baseline_ref;
  request.baseline_saa_ir = protocol.baseline_saa_ir;
  request.dataset_snapshot_ids = protocol.dataset_snapshot_ids;
  for (const auto &value : protocol.trial_groups) {
    InternetScalarTrialGroup group;
    group.independence_group =
        value.at("independence_group").get<std::string>();
    group.baseline_context_signature =
        value.value("baseline_context_signature", std::string{});
    group.candidate_context_signature =
        value.value("candidate_context_signature", std::string{});
    group.deterministic_seed = value.at("deterministic_seed").get<int>();
    group.inputs = exact_rationals(value.at("inputs"));
    group.expected_outputs = exact_rationals(value.at("expected_outputs"));
    request.trial_groups.push_back(std::move(group));
  }
  request.context_signature = internet_experiment_context_signature(
      request.dataset_snapshot_ids, request.trial_groups);
  for (auto &group : request.trial_groups) {
    if (group.baseline_context_signature.empty()) {
      group.baseline_context_signature = request.context_signature;
    }
    if (group.candidate_context_signature.empty()) {
      group.candidate_context_signature = request.context_signature;
    }
  }
  request.minimum_material_effect =
      exact_rational(protocol.minimum_material_effect);
  request.minimum_output = exact_rational(protocol.minimum_output);
  request.maximum_output = exact_rational(protocol.maximum_output);
  request.minimum_trials_per_group = protocol.minimum_trials_per_group;
  request.minimum_experiments = protocol.minimum_experiments;
  request.minimum_independence_groups = protocol.minimum_independence_groups;
  request.maximum_total_trials = protocol.maximum_total_trials;
  for (const auto &[name, score] : protocol.benchmark_track_scores.items()) {
    request.benchmark_track_scores.emplace_back(name, score.get<int>());
  }
  request.benchmark_policy =
      benchmark_policy_from_json(protocol.benchmark_policy);
  for (const auto &value : protocol.integrity_snapshots) {
    request.integrity_snapshots.push_back(
        integrity_snapshot_from_json(value));
  }
  request.integrity_policy =
      integrity_policy_from_json(protocol.integrity_policy);
  request.independent_review = protocol.independent_review;
  request.recorded_at = std::move(recorded_at);
  return request;
}

InternetProbationObservationRequest probation_request_from_input(
    const InternetProbationObservationInput &input) {
  const auto &signals = input.regression_signals;
  return {.query_signature = input.query_signature,
          .context_signature = input.context_signature,
          .observed_at = input.observed_at,
          .window_index = input.window_index,
          .candidate_correct = input.candidate_correct,
          .baseline_correct = input.baseline_correct,
          .invariant_passed = input.invariant_passed,
          .benchmark_passed = input.benchmark_passed,
          .integrity_passed = input.integrity_passed,
          .source_valid = input.source_valid,
          .reproduction_passed = input.reproduction_passed,
          .evidence_ids = input.evidence_ids,
          .regression_signals =
              {.semantic_contradiction =
                   signals.value("semantic_contradiction", false),
               .falsifier_succeeded =
                   signals.value("falsifier_succeeded", false),
               .corrected_error_recurrence =
                   signals.value("corrected_error_recurrence", false),
               .equivalent_failure_retry_regression = signals.value(
                   "equivalent_failure_retry_regression", false),
               .independence_passed =
                   signals.value("independence_passed", true),
               .evidence_fresh = signals.value("evidence_fresh", true),
               .projection_integrity_passed = signals.value(
                   "projection_integrity_passed", true)}};
}

std::vector<std::string> execute_action(
    EgcfStore &store, InternetImprovementStore &internet,
    InternetSourceCoordinator &source,
    const InternetDirectedAction &action,
    const InternetImprovementRunRequest &request,
    sources::HttpFetchProvider *fetch_provider,
    providers::ReasoningProvider *reasoning_provider,
    const std::string &reasoning_provider_identity,
    const std::string &model_identity) {
  switch (action.kind) {
  case InternetDirectedActionKind::recover_expired_lease: {
    const auto record = store.get(action.subject_id);
    const auto lease = sources::internet_fetch_lease_from_json(record.payload);
    if (!lease.active() || lease.expires_at > request.current_timestamp) {
      orchestrator_error("fetch lease is not expired and active");
    }
    return {internet.register_fetch_lease(
        sources::close_fetch_lease(lease, "EXPIRED"))};
  }
  case InternetDirectedActionKind::schedule_fetch: {
    const auto job = sources::internet_fetch_job_from_json(
        action.parameters.at("job"));
    return {internet.register_fetch_job(job)};
  }
  case InternetDirectedActionKind::execute_fetch: {
    if (fetch_provider == nullptr) {
      orchestrator_error("fetch action requires an HTTP provider");
    }
    const auto latest = internet.latest_lease(action.subject_id);
    std::string predecessor;
    int attempt_number = 1;
    if (latest) {
      if (latest->active()) {
        orchestrator_error("fetch job already has an active lease");
      }
      predecessor = latest->object_id();
      for (const auto &record : internet.list("internet-fetch-lease")) {
        const auto lease =
            sources::internet_fetch_lease_from_json(record.payload);
        if (lease.job_id == action.subject_id && lease.state == "ACTIVE") {
          ++attempt_number;
        }
      }
    }
    const auto lease = sources::acquire_fetch_lease(
        action.subject_id, request.worker_id, request.current_timestamp,
        request.fetch_lease_expires_at, predecessor);
    const std::string lease_id = internet.register_fetch_lease(lease);
    const auto result = source.execute_fetch(
        action.subject_id, lease_id, request.current_timestamp,
        *fetch_provider, request.prior_snapshot_id);
    std::vector<std::string> outputs = {
        lease_id, result.fetch_receipt_id, result.closed_lease_id};
    if (!result.snapshot_id.empty()) {
      outputs.push_back(result.snapshot_id);
    }
    if (!result.artifact_record_id.empty()) {
      outputs.push_back(result.artifact_record_id);
    }
    if (!result.artifact_bytes_id.empty()) {
      outputs.push_back(result.artifact_bytes_id);
    }
    if (!result.source_assessment_input_id.empty()) {
      outputs.push_back(result.source_assessment_input_id);
    }
    static_cast<void>(attempt_number);
    return outputs;
  }
  case InternetDirectedActionKind::assess_source: {
    for (const auto &input_id : action.input_ids) {
      const auto record = store.get(input_id);
      if (record.object_type == "internet-source-assessment-input") {
        const auto input =
            internet_source_assessment_input_from_json(record.payload);
        const auto result = source.assess(
            input.snapshot_id, input.fetch_receipt_id, input.source_policy_id,
            input.robots_allowed, input.license_classification);
        return {result.assessment_id};
      }
    }
    orchestrator_error("source assessment action lacks bound evidence input");
  }
  case InternetDirectedActionKind::extract_snapshot: {
    const auto result = source.extract(action.subject_id);
    std::vector<std::string> outputs = {result.extraction_receipt_id};
    outputs.insert(outputs.end(), result.extraction.receipt.fragment_ids.begin(),
                   result.extraction.receipt.fragment_ids.end());
    return outputs;
  }
  case InternetDirectedActionKind::feed_extraction: {
    std::optional<sources::InternetPolicyAssessment> assessment;
    for (const auto &input_id : action.input_ids) {
      const auto record = store.get(input_id);
      if (record.object_type == "internet-policy-assessment") {
        assessment =
            sources::internet_policy_assessment_from_json(record.payload);
      }
    }
    if (!assessment) {
      orchestrator_error("feed action lacks an admissible assessment");
    }
    InternetFeedCoordinator coordinator(store);
    const auto result = coordinator.process(
        *assessment, extraction_from_store(store, action.subject_id),
        request.source_label, request.strict_feed);
    std::vector<std::string> outputs;
    for (const auto &receipt : result.retrieval_receipts) {
      outputs.push_back(receipt.object_id());
    }
    for (const auto &candidate : result.candidates) {
      outputs.push_back(candidate.object_id());
    }
    return outputs;
  }
  case InternetDirectedActionKind::reason_candidate: {
    auto candidate = candidate_from_store(store, action.subject_id);
    std::vector<sources::InternetSourceFragment> fragments;
    for (const auto &input_id : action.input_ids) {
      const auto record = store.get(input_id);
      if (record.object_type == "internet-source-fragment") {
        fragments.push_back(
            sources::internet_source_fragment_from_json(record.payload));
      }
    }
    InternetReasoningCoordinator coordinator(store);
    const auto result = coordinator.analyze(
        candidate, fragments, reasoning_provider,
        reasoning_provider_identity, model_identity);
    return {result.analysis_id, result.updated_candidate_id};
  }
  case InternetDirectedActionKind::qualify_candidate: {
    if (action.protocol_ids.size() != 1U) {
      orchestrator_error("qualification action requires one protocol");
    }
    const auto protocol_record = store.get(action.protocol_ids.front());
    const auto protocol =
        internet_experiment_protocol_from_json(protocol_record.payload);
    InternetExperimentCoordinator coordinator(store);
    const auto result = coordinator.qualify(
        candidate_from_store(store, action.subject_id),
        experiment_request_from_protocol(protocol, request.current_timestamp));
    return {result.qualification_id, result.updated_candidate_id,
            result.benchmark_gate_ref, result.integrity_trajectory_ref,
            result.improvement_schedule_ref};
  }
  case InternetDirectedActionKind::assess_promotion: {
    if (action.policy_ids.size() != 1U) {
      orchestrator_error("promotion action requires one policy");
    }
    AutonomousPromotionController controller(store);
    const auto result = controller.assess(
        candidate_from_store(store, action.subject_id),
        action.policy_ids.front());
    return {result.assessment_id, result.updated_candidate_id};
  }
  case InternetDirectedActionKind::admit_probation: {
    InternetProbationController controller(store);
    const auto result =
        controller.admit(candidate_from_store(store, action.subject_id));
    return {result.admission_id, result.updated_candidate_id,
            result.canonical_admission.canonical_id};
  }
  case InternetDirectedActionKind::select_probation_candidate: {
    InternetProbationController controller(store);
    const auto result = controller.select(
        candidate_from_store(store, action.subject_id),
        action.parameters.at("query_signature").get<std::string>());
    return {result.selected_canonical_ref};
  }
  case InternetDirectedActionKind::consume_probation_observation: {
    const auto input_id =
        action.parameters.at("observation_input_id").get<std::string>();
    const auto input = internet_probation_observation_input_from_json(
        store.get(input_id).payload);
    InternetProbationController controller(store);
    const auto result = controller.observe(
        candidate_from_store(store, action.subject_id),
        probation_request_from_input(input));
    std::vector<std::string> outputs = {result.observation_id,
                                        result.updated_candidate_id};
    if (!result.promotion_decision_id.empty()) {
      outputs.push_back(result.promotion_decision_id);
    }
    if (!result.demotion_decision_id.empty()) {
      outputs.push_back(result.demotion_decision_id);
    }
    return outputs;
  }
  case InternetDirectedActionKind::revalidate_source:
    orchestrator_error("source revalidation requires a fresh assessment input");
  case InternetDirectedActionKind::verify_integrity:
    internet.verify_integrity();
    return {};
  }
  orchestrator_error("unsupported internet directed action");
}

Json stored_objects(const std::vector<StoredObject> &records) {
  Json result = Json::array();
  for (const auto &record : records) {
    result.push_back(egcf::to_json(record));
  }
  return result;
}

} // namespace

InternetImprovementOrchestrator::InternetImprovementOrchestrator(
    EgcfStore &store, sources::HttpFetchProvider *fetch_provider,
    providers::ReasoningProvider *reasoning_provider,
    std::string reasoning_provider_identity, std::string model_identity)
    : store_(store), fetch_provider_(fetch_provider),
      reasoning_provider_(reasoning_provider),
      reasoning_provider_identity_(std::move(reasoning_provider_identity)),
      model_identity_(std::move(model_identity)) {}

InternetImprovementPlan InternetImprovementOrchestrator::plan(
    const InternetImprovementRunRequest &request) {
  require_plan_request(request);
  InternetImprovementStateReader reader(store_);
  InternetImprovementDirector director;
  return director.plan(reader.read(request.current_timestamp, request.cycle_key),
                       request.policy);
}

InternetImprovementRunResult InternetImprovementOrchestrator::run_once(
    const InternetImprovementRunRequest &request) {
  return run({}, request);
}

InternetImprovementRunResult InternetImprovementOrchestrator::resume(
    std::string prior_run_id, const InternetImprovementRunRequest &request) {
  const auto prior = store_.get(prior_run_id);
  if (prior.object_type != "internet-improvement-run") {
    orchestrator_error("resume target is not an internet improvement run");
  }
  require_run_request(request);
  InternetImprovementStore internet(store_);
  if (run_is_terminal(internet, prior_run_id)) {
    orchestrator_error("resume target is already terminal");
  }
  const auto prior_run = internet_improvement_run_from_json(prior.payload);
  const auto plan_record = store_.get(prior_run.plan_id);
  const auto prior_plan =
      internet_improvement_plan_from_json(plan_record.payload);
  if (!prior_plan.actions.empty()) {
    const auto &action = prior_plan.actions.front();
    if (const auto existing = internet.terminal_action_receipt(
            action.action_key)) {
      static_cast<void>(internet.register_improvement_run_event(make_event(
          prior_run_id, "ACTION_SKIPPED", request.current_timestamp,
          action.action_key,
          {{"disposition", "RECONCILED"},
           {"receipt_id", existing->object_id()}})));
      static_cast<void>(internet.register_improvement_run_event(make_event(
          prior_run_id, "COMPLETED", request.current_timestamp)));
      return {.plan = prior_plan,
              .plan_id = prior_run.plan_id,
              .run_id = prior_run_id,
              .action_key = action.action_key,
              .action_lease_id = existing->lease_id,
              .action_receipt_id = existing->object_id(),
              .output_ids = existing->output_ids,
              .status = "RECONCILED",
              .diagnostic = {}};
    }
    if (const auto outputs =
            existing_outputs_for_action(store_, internet, action)) {
      InternetImprovementRunResult result;
      result.plan = prior_plan;
      result.plan_id = prior_run.plan_id;
      result.action_key = action.action_key;
      result.output_ids = *outputs;
      InternetImprovementActionLease active_lease;
      const auto latest = internet.latest_action_lease(action.action_key);
      if (latest && latest->active() &&
          latest->expires_at > request.current_timestamp) {
        if (latest->run_id != prior_run_id) {
          orchestrator_error("resume action lease belongs to another run");
        }
        if (latest->worker_id != request.worker_id) {
          orchestrator_error("resume action lease belongs to another worker");
        }
        result.run_id = prior_run_id;
        active_lease = *latest;
      } else {
        InternetImprovementRun resumed_run;
        resumed_run.plan_id = prior_run.plan_id;
        resumed_run.worker_id = request.worker_id;
        resumed_run.started_at = request.current_timestamp;
        resumed_run.resume_of_run_id = prior_run_id;
        resumed_run.requested_budgets = to_json(request.policy);
        resumed_run =
            canonical_internet_improvement_run(std::move(resumed_run));
        result.run_id = internet.register_improvement_run(resumed_run);
        static_cast<void>(internet.register_improvement_run_event(make_event(
            result.run_id, "STARTED", request.current_timestamp, {},
            {{"plan_id", result.plan_id},
             {"resume_of_run_id", prior_run_id}})));
        active_lease = acquire_action_lease(internet, action, result.run_id,
                                             request);
        result.action_lease_id =
            internet.register_improvement_action_lease(active_lease);
      }
      if (result.action_lease_id.empty()) {
        result.action_lease_id = active_lease.object_id();
      }
      InternetImprovementActionReceipt receipt;
      receipt.action_key = action.action_key;
      receipt.plan_id = result.plan_id;
      receipt.run_id = result.run_id;
      receipt.lease_id = result.action_lease_id;
      receipt.expected_preconditions = expected_preconditions(action);
      receipt.observed_preconditions =
          {{"domain_effect_present", true},
           {"event_head", store_.event_head()}};
      receipt.input_ids = action.input_ids;
      receipt.output_ids = result.output_ids;
      receipt.executor_version =
          std::string(internet_improvement_orchestrator_version);
      receipt.provider_identity = "reconciled-durable-state";
      receipt.model_identity = "none";
      receipt.started_at = active_lease.acquired_at;
      receipt.completed_at = request.current_timestamp;
      receipt.terminal_state = "COMPLETED";
      receipt.disposition = "RECONCILED";
      receipt.diagnostic =
          "durable lifecycle output existed before terminal action receipt";
      receipt =
          canonical_internet_improvement_action_receipt(std::move(receipt));
      result.action_receipt_id =
          internet.register_improvement_action_receipt(receipt);
      static_cast<void>(internet.register_improvement_action_lease(
          close_action_lease(active_lease, "COMPLETED")));
      static_cast<void>(internet.register_improvement_run_event(make_event(
          result.run_id, "ACTION_SKIPPED", request.current_timestamp,
          action.action_key,
          {{"disposition", "RECONCILED"},
           {"receipt_id", result.action_receipt_id}})));
      static_cast<void>(internet.register_improvement_run_event(make_event(
          result.run_id, "COMPLETED", request.current_timestamp)));
      result.status = "RECONCILED";
      result.diagnostic = receipt.diagnostic;
      return result;
    }
  }
  return run(std::move(prior_run_id), request);
}

InternetImprovementRunResult InternetImprovementOrchestrator::run(
    std::string resume_of_run_id,
    const InternetImprovementRunRequest &request) {
  require_run_request(request);
  InternetImprovementStore internet(store_);
  InternetSourceCoordinator source(store_);
  InternetImprovementRunResult result;
  result.plan = plan(request);
  result.plan_id = internet.register_improvement_plan(result.plan);

  InternetImprovementRun run_record;
  run_record.plan_id = result.plan_id;
  run_record.worker_id = request.worker_id;
  run_record.started_at = request.current_timestamp;
  run_record.resume_of_run_id = std::move(resume_of_run_id);
  run_record.requested_budgets = to_json(request.policy);
  run_record = canonical_internet_improvement_run(std::move(run_record));
  result.run_id = internet.register_improvement_run(run_record);
  static_cast<void>(internet.register_improvement_run_event(make_event(
      result.run_id, "STARTED", request.current_timestamp, {},
      {{"plan_id", result.plan_id}})));

  if (result.plan.actions.empty()) {
    result.status = "NO_ELIGIBLE_WORK";
    static_cast<void>(internet.register_improvement_run_event(make_event(
        result.run_id, "NO_ELIGIBLE_WORK", request.current_timestamp)));
    static_cast<void>(internet.register_improvement_run_event(make_event(
        result.run_id, "COMPLETED", request.current_timestamp)));
    return result;
  }

  const auto &action = result.plan.actions.front();
  result.action_key = action.action_key;
  if (const auto existing = internet.terminal_action_receipt(action.action_key)) {
    result.action_receipt_id = existing->object_id();
    result.output_ids = existing->output_ids;
    result.status = "RECONCILED";
    static_cast<void>(internet.register_improvement_run_event(make_event(
        result.run_id, "ACTION_SKIPPED", request.current_timestamp,
        action.action_key,
        {{"disposition", "RECONCILED"},
         {"receipt_id", result.action_receipt_id}})));
    static_cast<void>(internet.register_improvement_run_event(make_event(
        result.run_id, "COMPLETED", request.current_timestamp)));
    return result;
  }

  const auto lease = acquire_action_lease(internet, action, result.run_id,
                                           request);
  result.action_lease_id = internet.register_improvement_action_lease(lease);
  static_cast<void>(internet.register_improvement_run_event(make_event(
      result.run_id, "ACTION_LEASED", request.current_timestamp,
      action.action_key, {{"lease_id", result.action_lease_id}})));

  bool preconditions_match = false;
  const Json observed = observed_preconditions(
      store_, action, request.current_timestamp, preconditions_match);
  InternetImprovementActionReceipt receipt;
  receipt.action_key = action.action_key;
  receipt.plan_id = result.plan_id;
  receipt.run_id = result.run_id;
  receipt.lease_id = result.action_lease_id;
  receipt.expected_preconditions = expected_preconditions(action);
  receipt.observed_preconditions = observed;
  receipt.input_ids = action.input_ids;
  receipt.executor_version =
      std::string(internet_improvement_orchestrator_version);
  receipt.provider_identity =
      action.kind == InternetDirectedActionKind::execute_fetch
          ? "provider-bound-by-fetch-receipt"
          : action.kind == InternetDirectedActionKind::reason_candidate
                ? reasoning_provider_identity_
                : "deterministic-core";
  receipt.model_identity =
      action.kind == InternetDirectedActionKind::reason_candidate
          ? model_identity_
          : "none";
  receipt.started_at = request.current_timestamp;
  receipt.completed_at = request.current_timestamp;

  std::string lease_terminal_state = "COMPLETED";
  std::string event_type = "ACTION_COMPLETED";
  if (!preconditions_match) {
    receipt.terminal_state = "STALE";
    receipt.error_code = "STALE_PRECONDITION";
    receipt.diagnostic = "directed action preconditions changed before lease execution";
    receipt.disposition = "STALE";
    result.status = "STALE";
    result.diagnostic = receipt.diagnostic;
    event_type = "ACTION_SKIPPED";
  } else {
    try {
      receipt.output_ids = execute_action(
          store_, internet, source, action, request, fetch_provider_,
          reasoning_provider_, reasoning_provider_identity_, model_identity_);
      result.output_ids = receipt.output_ids;
      receipt.terminal_state = "COMPLETED";
      receipt.disposition = "EXECUTED";
      result.status = "COMPLETED";
    } catch (const common::Error &error) {
      receipt.terminal_state = "FAILED";
      receipt.error_code = std::string(common::error_code_name(error.code()));
      receipt.diagnostic = error.what();
      receipt.disposition = "EXECUTED";
      result.status = "FAILED";
      result.diagnostic = error.what();
      lease_terminal_state = "ABANDONED";
      event_type = "ACTION_FAILED";
    } catch (const std::exception &error) {
      receipt.terminal_state = "FAILED";
      receipt.error_code = "internal_failure";
      receipt.diagnostic = error.what();
      receipt.disposition = "EXECUTED";
      result.status = "FAILED";
      result.diagnostic = error.what();
      lease_terminal_state = "ABANDONED";
      event_type = "ACTION_FAILED";
    }
  }
  receipt = canonical_internet_improvement_action_receipt(std::move(receipt));
  result.action_receipt_id =
      internet.register_improvement_action_receipt(receipt);
  static_cast<void>(internet.register_improvement_action_lease(
      close_action_lease(lease, lease_terminal_state)));
  static_cast<void>(internet.register_improvement_run_event(make_event(
      result.run_id, event_type, request.current_timestamp, action.action_key,
      {{"receipt_id", result.action_receipt_id},
       {"status", result.status}})));
  static_cast<void>(internet.register_improvement_run_event(make_event(
      result.run_id, result.status == "FAILED" ? "ABANDONED" : "COMPLETED",
      request.current_timestamp)));
  return result;
}

Json InternetImprovementOrchestrator::run_status(
    std::string_view run_id, std::string_view worker_id,
    bool nonterminal_only) const {
  InternetImprovementStore internet(store_);
  if (run_id.empty() && worker_id.empty() && !nonterminal_only) {
    return {{"action_leases",
             stored_objects(
                 internet.list("internet-improvement-action-lease"))},
            {"action_receipts",
             stored_objects(
                 internet.list("internet-improvement-action-receipt"))},
            {"plans",
             stored_objects(internet.list("internet-improvement-plan"))},
            {"run_events",
             stored_objects(
                 internet.list("internet-improvement-run-event"))},
            {"runs",
             stored_objects(internet.list("internet-improvement-run"))}};
  }
  const auto runs = internet.list("internet-improvement-run");
  const auto run_events = internet.list("internet-improvement-run-event");
  const auto action_leases =
      internet.list("internet-improvement-action-lease");
  const auto action_receipts =
      internet.list("internet-improvement-action-receipt");
  const auto plans = internet.list("internet-improvement-plan");
  std::set<std::string> terminal_run_ids;
  for (const auto &record : run_events) {
    const auto event =
        internet_improvement_run_event_from_json(record.payload);
    if (event.event_type == "COMPLETED" || event.event_type == "ABANDONED") {
      terminal_run_ids.insert(event.run_id);
    }
  }
  std::set<std::string> resumed_run_ids;
  for (const auto &record : runs) {
    const auto run = internet_improvement_run_from_json(record.payload);
    if (!run.resume_of_run_id.empty()) {
      resumed_run_ids.insert(run.resume_of_run_id);
    }
  }
  std::set<std::string> selected_run_ids;
  std::set<std::string> selected_plan_ids;
  std::vector<StoredObject> selected_runs;
  for (const auto &record : runs) {
    const auto run = internet_improvement_run_from_json(record.payload);
    if ((!run_id.empty() && record.object_id != run_id) ||
        (!worker_id.empty() && run.worker_id != worker_id) ||
        (nonterminal_only &&
         (terminal_run_ids.contains(record.object_id) ||
          resumed_run_ids.contains(record.object_id)))) {
      continue;
    }
    selected_runs.push_back(record);
    selected_run_ids.insert(record.object_id);
    selected_plan_ids.insert(run.plan_id);
  }
  auto records_for_selected_runs = [&](const std::vector<StoredObject> &records) {
    std::vector<StoredObject> selected;
    for (const auto &record : records) {
      if (selected_run_ids.contains(
              record.payload.at("run_id").get<std::string>())) {
        selected.push_back(record);
      }
    }
    return selected;
  };
  std::vector<StoredObject> selected_plans;
  for (const auto &record : plans) {
    if (selected_plan_ids.contains(record.object_id)) {
      selected_plans.push_back(record);
    }
  }
  Json result = {
      {"action_leases",
       stored_objects(records_for_selected_runs(action_leases))},
      {"action_receipts",
       stored_objects(records_for_selected_runs(action_receipts))},
      {"plans", stored_objects(selected_plans)},
      {"run_events",
       stored_objects(records_for_selected_runs(run_events))},
      {"runs", stored_objects(selected_runs)}};
  if (!run_id.empty()) {
    const auto record = store_.get(run_id);
    if (record.object_type != "internet-improvement-run") {
      orchestrator_error("run status target has invalid type");
    }
    result["selected_run"] = {{"object_id", record.object_id()},
                              {"object_type", record.object_type},
                              {"payload", record.payload}};
  }
  return result;
}

Json InternetImprovementOrchestrator::explain_action(
    std::string_view action_key) const {
  InternetImprovementStore internet(store_);
  for (const auto &plan_record : internet.list("internet-improvement-plan")) {
    const auto plan = internet_improvement_plan_from_json(plan_record.payload);
    for (const auto &action : plan.actions) {
      if (action.action_key == action_key) {
        return {{"action", to_json(action)},
                {"eligible", true},
                {"latest_lease",
                 internet.latest_action_lease(action_key)
                     ? to_json(*internet.latest_action_lease(action_key))
                     : Json(nullptr)},
                {"terminal_receipt",
                 internet.terminal_action_receipt(action_key)
                     ? to_json(*internet.terminal_action_receipt(action_key))
                     : Json(nullptr)}};
      }
    }
    for (const auto &action : plan.deferred_actions) {
      if (action.action_key == action_key) {
        return {{"action", to_json(action)},
                {"eligible", false},
                {"blocking_reasons", action.blocked_reasons}};
      }
    }
  }
  orchestrator_error("internet improvement action was not found");
}

Json to_json(const InternetImprovementRunRequest &value) {
  return {{"action_lease_expires_at", value.action_lease_expires_at},
          {"current_timestamp", value.current_timestamp},
          {"cycle_key", value.cycle_key},
          {"fetch_lease_expires_at", value.fetch_lease_expires_at},
          {"policy", to_json(value.policy)},
          {"prior_snapshot_id", value.prior_snapshot_id},
          {"source_label", value.source_label},
          {"strict_feed", value.strict_feed},
          {"worker_id", value.worker_id}};
}

Json to_json(const InternetImprovementRunResult &value) {
  return {{"action_key", value.action_key},
          {"action_lease_id", value.action_lease_id},
          {"action_receipt_id", value.action_receipt_id},
          {"diagnostic", value.diagnostic},
          {"output_ids", value.output_ids},
          {"plan", to_json(value.plan)},
          {"plan_id", value.plan_id},
          {"run_id", value.run_id},
          {"status", value.status}};
}

} // namespace statewright::egcf
