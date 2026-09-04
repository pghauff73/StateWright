#include "statewright/egcf/internet_improvement_store.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/sources/policy.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace statewright::egcf {
namespace {

[[noreturn]] void internet_store_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

sources::InternetFetchLease lease_from_payload(const contracts::Json &payload) {
  sources::InternetFetchLease lease;
  lease.schema_version = payload.at("schema_version").get<int>();
  lease.job_id = payload.at("job_id").get<std::string>();
  lease.worker_id = payload.at("worker_id").get<std::string>();
  lease.acquired_at = payload.at("acquired_at").get<std::string>();
  lease.expires_at = payload.at("expires_at").get<std::string>();
  lease.predecessor_lease_id =
      payload.at("predecessor_lease_id").get<std::string>();
  lease.state = payload.at("state").get<std::string>();
  lease.lease_signature = payload.at("lease_signature").get<std::string>();
  return sources::canonical_fetch_lease(std::move(lease));
}

[[nodiscard]] std::string trimmed(std::string value) {
  const auto first = value.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\n\r\f\v");
  return value.substr(first, last - first + 1U);
}

} // namespace

InternetImprovementStore::InternetImprovementStore(EgcfStore &store)
    : store_(store) {}

void InternetImprovementStore::require_type(
    std::string_view object_id, std::string_view object_type) const {
  const auto record = store_.get(object_id);
  if (record.object_type != object_type) {
    internet_store_error("expected " + std::string(object_type) +
                         " reference: " + std::string(object_id));
  }
}

std::string InternetImprovementStore::register_source_policy(
    const sources::InternetSourcePolicy &policy) {
  const auto canonical = sources::canonical_source_policy(policy);
  return store_.register_record(
      {.object_type = "internet-source-policy",
       .payload = sources::to_json(canonical)},
      "internet_source_policy_registered");
}

std::string
InternetImprovementStore::register_watch(const sources::InternetWatch &watch) {
  const auto canonical = sources::canonical_watch(watch);
  require_type(canonical.source_policy_id, "internet-source-policy");
  if (!canonical.supersedes_watch_id.empty()) {
    require_type(canonical.supersedes_watch_id, "internet-watch");
  }
  const std::string id = store_.register_record(
      {.object_type = "internet-watch", .payload = sources::to_json(canonical)},
      "internet_watch_registered");
  if (!canonical.supersedes_watch_id.empty()) {
    static_cast<void>(store_.supersede(
        canonical.supersedes_watch_id, id, "internet watch superseded",
        "statewright-autonomous-source-policy"));
  }
  return id;
}

std::string InternetImprovementStore::register_fetch_job(
    const sources::InternetFetchJob &job) {
  const auto canonical = sources::canonical_fetch_job(job);
  require_type(canonical.watch_id, "internet-watch");
  const auto watch = store_.get(canonical.watch_id).payload;
  if (watch.at("schedule_generation").get<int>() !=
      canonical.expected_watch_generation) {
    internet_store_error("fetch job watch generation is stale");
  }
  return store_.register_record(
      {.object_type = "internet-fetch-job",
       .payload = sources::to_json(canonical)},
      "internet_fetch_job_registered");
}

std::optional<sources::InternetFetchLease>
InternetImprovementStore::latest_lease(std::string_view job_id) {
  std::vector<std::pair<std::string, sources::InternetFetchLease>> leases;
  std::set<std::string> predecessors;
  for (const auto &object : store_.list("internet-fetch-lease")) {
    const auto lease = lease_from_payload(object.payload);
    if (lease.job_id != job_id) {
      continue;
    }
    if (!lease.predecessor_lease_id.empty()) {
      predecessors.insert(lease.predecessor_lease_id);
    }
    leases.emplace_back(object.object_id, lease);
  }
  std::vector<sources::InternetFetchLease> latest;
  for (const auto &[object_id, lease] : leases) {
    if (!predecessors.contains(object_id)) {
      latest.push_back(lease);
    }
  }
  if (latest.size() > 1U) {
    internet_store_error("fetch job has conflicting latest leases");
  }
  return latest.empty() ? std::nullopt
                        : std::optional<sources::InternetFetchLease>(latest.front());
}

std::string InternetImprovementStore::register_fetch_lease(
    const sources::InternetFetchLease &lease) {
  const auto canonical = sources::canonical_fetch_lease(lease);
  require_type(canonical.job_id, "internet-fetch-job");
  const auto latest = latest_lease(canonical.job_id);
  if (!latest && !canonical.predecessor_lease_id.empty()) {
    internet_store_error("first fetch lease cannot declare a predecessor");
  }
  if (!latest && !canonical.active()) {
    internet_store_error("first fetch lease must be active");
  }
  if (latest && canonical.predecessor_lease_id != latest->object_id()) {
    internet_store_error("fetch lease predecessor is not current");
  }
  if (latest && canonical.active() && latest->active() &&
      canonical.acquired_at < latest->expires_at) {
    internet_store_error("active fetch leases cannot overlap");
  }
  if (latest && !canonical.active() &&
      (!latest->active() || canonical.worker_id != latest->worker_id ||
       canonical.acquired_at != latest->acquired_at ||
       canonical.expires_at != latest->expires_at)) {
    internet_store_error("terminal fetch lease must close the current lease");
  }
  return store_.register_record(
      {.object_type = "internet-fetch-lease",
       .payload = sources::to_json(canonical)},
      "internet_fetch_lease_registered");
}

InternetCaptureResult InternetImprovementStore::capture_success(
    std::string job_id, std::string lease_id,
    const sources::FetchResponse &response, std::string source_group) {
  require_type(job_id, "internet-fetch-job");
  require_type(lease_id, "internet-fetch-lease");
  const auto lease = lease_from_payload(store_.get(lease_id).payload);
  const auto latest = latest_lease(job_id);
  if (!lease.active() || lease.job_id != job_id || !latest ||
      latest->object_id() != lease_id) {
    internet_store_error("successful capture requires the active job lease");
  }
  if (response.http_status < 200 || response.http_status >= 300 ||
      response.http_status == 204 || response.body.empty()) {
    internet_store_error("successful capture requires response bytes");
  }
  const std::string content_type =
      sources::normalized_response_content_type(response);
  const std::string artifact_record_id = store_.register_artifact(
      response.body, content_type, {},
      {{"provider_identity", response.provider_identity},
       {"requested_url", response.requested_url},
       {"source_group", source_group}});
  const auto artifact = store_.get(artifact_record_id);
  const std::string artifact_bytes_id =
      "artifact-bytes:sha256:" + artifact.payload.at("sha256").get<std::string>();
  const auto captured_snapshot = sources::make_source_snapshot(
      response, artifact_bytes_id, std::move(source_group));
  const std::string snapshot_id = store_.register_record(
      {.object_type = "internet-source-snapshot",
       .payload = sources::to_json(captured_snapshot)},
      "internet_source_snapshot_registered");
  const auto receipt = sources::make_fetch_receipt(
      std::move(job_id), std::move(lease_id), response, snapshot_id);
  const std::string receipt_id = store_.register_record(
      {.object_type = "internet-fetch-receipt",
       .payload = sources::to_json(receipt)},
      "internet_fetch_receipt_registered");
  return {.artifact_record_id = artifact_record_id,
          .artifact_bytes_id = artifact_bytes_id,
          .snapshot_id = snapshot_id,
          .fetch_receipt_id = receipt_id};
}

std::string InternetImprovementStore::capture_not_modified(
    std::string job_id, std::string lease_id,
    const sources::FetchResponse &response, std::string snapshot_id) {
  require_type(job_id, "internet-fetch-job");
  require_type(lease_id, "internet-fetch-lease");
  require_type(snapshot_id, "internet-source-snapshot");
  const auto lease = lease_from_payload(store_.get(lease_id).payload);
  const auto latest = latest_lease(job_id);
  if (!lease.active() || lease.job_id != job_id || !latest ||
      latest->object_id() != lease_id || response.http_status != 304 ||
      !response.body.empty()) {
    internet_store_error("not-modified capture requires the active job lease");
  }
  const auto receipt = sources::make_not_modified_fetch_receipt(
      std::move(job_id), std::move(lease_id), response, std::move(snapshot_id));
  return store_.register_record(
      {.object_type = "internet-fetch-receipt",
       .payload = sources::to_json(receipt)},
      "internet_fetch_not_modified_registered");
}

std::string InternetImprovementStore::capture_failure(
    std::string job_id, std::string lease_id, std::string requested_url,
    std::string provider_identity, std::string reason) {
  require_type(job_id, "internet-fetch-job");
  require_type(lease_id, "internet-fetch-lease");
  const auto lease = lease_from_payload(store_.get(lease_id).payload);
  const auto latest = latest_lease(job_id);
  if (!lease.active() || lease.job_id != job_id || !latest ||
      latest->object_id() != lease_id) {
    internet_store_error("failed capture requires the active job lease");
  }
  const auto receipt = sources::make_failed_fetch_receipt(
      std::move(job_id), std::move(lease_id), std::move(requested_url),
      std::move(provider_identity), std::move(reason));
  return store_.register_record(
      {.object_type = "internet-fetch-receipt",
       .payload = sources::to_json(receipt)},
      "internet_fetch_failure_registered");
}

std::string InternetImprovementStore::register_policy_assessment(
    const sources::InternetPolicyAssessment &assessment) {
  const auto canonical = sources::canonical_policy_assessment(assessment);
  require_type(canonical.snapshot_id, "internet-source-snapshot");
  require_type(canonical.fetch_receipt_id, "internet-fetch-receipt");
  require_type(canonical.source_policy_id, "internet-source-policy");
  return store_.register_record(
      {.object_type = "internet-policy-assessment",
       .payload = sources::to_json(canonical)},
      "internet_policy_assessment_registered");
}

std::string InternetImprovementStore::register_source_fragment(
    const sources::InternetSourceFragment &fragment) {
  const auto canonical = sources::canonical_source_fragment(fragment);
  require_type(canonical.snapshot_id, "internet-source-snapshot");
  return store_.register_record(
      {.object_type = "internet-source-fragment",
       .payload = sources::to_json(canonical)},
      "internet_source_fragment_registered");
}

std::string InternetImprovementStore::register_extraction_receipt(
    const sources::InternetExtractionReceipt &receipt) {
  const auto canonical = sources::canonical_extraction_receipt(receipt);
  require_type(canonical.snapshot_id, "internet-source-snapshot");
  for (const auto &fragment_id : canonical.fragment_ids) {
    require_type(fragment_id, "internet-source-fragment");
  }
  return store_.register_record(
      {.object_type = "internet-extraction-receipt",
       .payload = sources::to_json(canonical)},
      "internet_extraction_receipt_registered");
}

std::string InternetImprovementStore::register_extraction(
    const sources::InternetExtractionResult &extraction) {
  for (const auto &fragment : extraction.fragments) {
    static_cast<void>(register_source_fragment(fragment));
  }
  return register_extraction_receipt(extraction.receipt);
}

std::string InternetImprovementStore::register_retrieval_receipt(
    const InternetKnowledgeSearchReceipt &receipt) {
  const auto canonical = canonical_knowledge_search_receipt(receipt);
  require_type(canonical.snapshot_id, "internet-source-snapshot");
  require_type(canonical.source_fragment_id, "internet-source-fragment");
  require_type(canonical.brain_feed_batch_id, "brain-feed-batch");
  return store_.register_record(
      {.object_type = "internet-retrieval-receipt",
       .payload = to_json(canonical)},
      "internet_retrieval_receipt_registered");
}

std::string InternetImprovementStore::register_algorithm_candidate(
    const InternetAlgorithmCandidate &candidate) {
  const auto canonical = canonical_internet_algorithm_candidate(candidate);
  require_type(canonical.snapshot_id, "internet-source-snapshot");
  require_type(canonical.source_fragment_id, "internet-source-fragment");
  require_type(canonical.source_policy_assessment_id,
               "internet-policy-assessment");
  require_type(canonical.retrieval_receipt_id, "internet-retrieval-receipt");
  return store_.register_record(
      {.object_type = "internet-algorithm-candidate",
       .payload = to_json(canonical)},
      "internet_algorithm_candidate_registered");
}

std::string InternetImprovementStore::register_reasoning_analysis(
    const InternetReasoningAnalysis &analysis) {
  const auto canonical = canonical_internet_reasoning_analysis(analysis);
  require_type(canonical.candidate_id, "internet-algorithm-candidate");
  for (const auto &snapshot_id : canonical.snapshot_ids) {
    require_type(snapshot_id, "internet-source-snapshot");
  }
  for (const auto &fragment_id : canonical.source_fragment_ids) {
    require_type(fragment_id, "internet-source-fragment");
  }
  return store_.register_record(
      {.object_type = "internet-reasoning-analysis",
       .payload = to_json(canonical)},
      "internet_reasoning_analysis_registered");
}

std::string InternetImprovementStore::register_experiment_qualification(
    const InternetExperimentQualification &qualification) {
  const auto canonical =
      canonical_internet_experiment_qualification(qualification);
  require_type(canonical.candidate_id, "internet-algorithm-candidate");
  for (const auto &snapshot_id : canonical.dataset_snapshot_ids) {
    require_type(snapshot_id, "internet-source-snapshot");
  }
  for (const auto &evidence_id : canonical.evidence_ids) {
    require_type(evidence_id, "egcf-evidence");
  }
  return store_.register_record(
      {.object_type = "internet-experiment-qualification",
       .payload = to_json(canonical)},
      "internet_experiment_qualification_registered");
}

std::string InternetImprovementStore::register_promotion_policy(
    const saa::AutonomousPromotionPolicy &policy) {
  const auto canonical = saa::canonical_autonomous_promotion_policy(policy);
  return store_.register_record(
      {.object_type = "internet-promotion-policy",
       .payload = saa::to_json(canonical)},
      "internet_promotion_policy_registered");
}

std::string InternetImprovementStore::register_promotion_assessment(
    std::string policy_id,
    const saa::AutonomousPromotionAssessment &assessment) {
  require_type(policy_id, "internet-promotion-policy");
  require_type(assessment.candidate_ref, "internet-algorithm-candidate");
  require_type(assessment.source_policy_assessment_ref,
               "internet-policy-assessment");
  require_type(assessment.snapshot_ref, "internet-source-snapshot");
  require_type(assessment.retrieval_receipt_ref,
               "internet-retrieval-receipt");
  require_type(assessment.experiment_qualification_ref,
               "internet-experiment-qualification");
  const auto stored_policy = store_.get(policy_id);
  if (stored_policy.payload.at("policy_signature").get<std::string>() !=
      assessment.policy_signature || assessment.human_approval_required) {
    internet_store_error("promotion assessment policy binding is invalid");
  }
  auto payload = saa::to_json(assessment);
  payload["policy_id"] = std::move(policy_id);
  return store_.register_record(
      {.object_type = "internet-promotion-assessment",
       .payload = std::move(payload)},
      "internet_promotion_assessment_registered");
}

std::string InternetImprovementStore::register_probation_admission(
    const saa::ProbationPlan &plan, std::string canonical_algorithm_ref,
    std::string canonical_source_ref, std::string baseline_ref,
    int canonical_store_generation, std::string admission_status) {
  const auto canonical_plan =
      saa::probation_plan_from_json(saa::to_json(plan));
  require_type(canonical_plan.policy_ref, "internet-promotion-policy");
  require_type(canonical_plan.promotion_assessment_ref,
               "internet-promotion-assessment");
  require_type(canonical_plan.candidate_ref, "internet-algorithm-candidate");
  canonical_algorithm_ref = trimmed(std::move(canonical_algorithm_ref));
  canonical_source_ref = trimmed(std::move(canonical_source_ref));
  baseline_ref = trimmed(std::move(baseline_ref));
  admission_status = trimmed(std::move(admission_status));
  if (canonical_algorithm_ref.empty() || canonical_source_ref.empty() ||
      baseline_ref.empty() || canonical_store_generation < 1 ||
      admission_status != "PROBATIONARY_CANONICAL") {
    internet_store_error("probation admission is invalid");
  }
  contracts::Json payload =
      {{"admission_status", admission_status},
       {"baseline_ref", baseline_ref},
       {"canonical_algorithm_ref", canonical_algorithm_ref},
       {"canonical_source_ref", canonical_source_ref},
       {"canonical_store_generation", canonical_store_generation},
       {"plan", saa::to_json(canonical_plan)},
       {"schema_version", 1}};
  payload["admission_signature"] = contracts::sha256_json(payload);
  return store_.register_record(
      {.object_type = "internet-probation-admission",
       .payload = std::move(payload)},
      "internet_probation_admission_registered");
}

std::string InternetImprovementStore::register_probation_observation(
    std::string admission_id,
    const saa::ProbationObservation &observation) {
  require_type(admission_id, "internet-probation-admission");
  const auto canonical =
      saa::probation_observation_from_json(saa::to_json(observation));
  for (const auto &evidence_id : canonical.evidence_ids) {
    require_type(evidence_id, "egcf-evidence");
  }
  const auto admission = store_.get(admission_id);
  if (admission.payload.at("plan").at("plan_signature").get<std::string>() !=
      canonical.plan_signature) {
    internet_store_error("probation observation plan binding is invalid");
  }
  auto payload = saa::to_json(canonical);
  payload["admission_id"] = std::move(admission_id);
  return store_.register_record(
      {.object_type = "internet-probation-observation",
       .payload = std::move(payload)},
      "internet_probation_observation_registered");
}

std::string InternetImprovementStore::register_promotion_decision(
    std::string admission_id,
    const saa::AutomaticPromotionDecision &decision) {
  require_type(admission_id, "internet-probation-admission");
  const auto canonical = saa::automatic_promotion_decision_from_json(
      saa::to_json(decision));
  const auto admission = store_.get(admission_id);
  if (admission.payload.at("plan").at("plan_signature").get<std::string>() !=
          canonical.plan_signature ||
      admission.payload.at("canonical_algorithm_ref").get<std::string>() !=
          canonical.canonical_algorithm_ref) {
    internet_store_error("automatic promotion decision binding is invalid");
  }
  auto payload = saa::to_json(canonical);
  payload["admission_id"] = std::move(admission_id);
  return store_.register_record(
      {.object_type = "internet-promotion-decision",
       .payload = std::move(payload)},
      "internet_promotion_decision_registered");
}

std::string InternetImprovementStore::register_demotion_decision(
    std::string admission_id,
    const saa::AutomaticDemotionDecision &decision) {
  require_type(admission_id, "internet-probation-admission");
  const auto canonical = saa::automatic_demotion_decision_from_json(
      saa::to_json(decision));
  const auto admission = store_.get(admission_id);
  if (admission.payload.at("plan").at("plan_signature").get<std::string>() !=
          canonical.plan_signature ||
      admission.payload.at("canonical_algorithm_ref").get<std::string>() !=
          canonical.demoted_canonical_algorithm_ref) {
    internet_store_error("automatic demotion decision binding is invalid");
  }
  auto payload = saa::to_json(canonical);
  payload["admission_id"] = std::move(admission_id);
  return store_.register_record(
      {.object_type = "internet-demotion-decision",
       .payload = std::move(payload)},
      "internet_demotion_decision_registered");
}

std::string InternetImprovementStore::register_improvement_plan(
    const InternetImprovementPlan &plan) {
  const auto canonical = canonical_internet_improvement_plan(plan);
  return store_.register_record(
      {.object_type = "internet-improvement-plan",
       .payload = to_json(canonical)},
      "internet_improvement_plan_registered");
}

std::string InternetImprovementStore::register_improvement_run(
    const InternetImprovementRun &run) {
  const auto canonical = canonical_internet_improvement_run(run);
  require_type(canonical.plan_id, "internet-improvement-plan");
  if (!canonical.resume_of_run_id.empty()) {
    require_type(canonical.resume_of_run_id, "internet-improvement-run");
  }
  return store_.register_record(
      {.object_type = "internet-improvement-run",
       .payload = to_json(canonical)},
      "internet_improvement_run_registered");
}

std::string InternetImprovementStore::register_improvement_run_event(
    const InternetImprovementRunEvent &event) {
  const auto canonical = canonical_internet_improvement_run_event(event);
  require_type(canonical.run_id, "internet-improvement-run");
  return store_.register_record(
      {.object_type = "internet-improvement-run-event",
       .payload = to_json(canonical)},
      "internet_improvement_run_event_registered");
}

std::optional<InternetImprovementActionLease>
InternetImprovementStore::latest_action_lease(std::string_view action_key) {
  std::vector<std::pair<std::string, InternetImprovementActionLease>> leases;
  std::set<std::string> predecessors;
  for (const auto &object : list("internet-improvement-action-lease")) {
    const auto lease =
        internet_improvement_action_lease_from_json(object.payload);
    if (lease.action_key != action_key) {
      continue;
    }
    if (!lease.predecessor_lease_id.empty()) {
      predecessors.insert(lease.predecessor_lease_id);
    }
    leases.emplace_back(object.object_id, lease);
  }
  std::vector<InternetImprovementActionLease> latest;
  for (const auto &[object_id, lease] : leases) {
    if (!predecessors.contains(object_id)) {
      latest.push_back(lease);
    }
  }
  if (latest.size() > 1U) {
    internet_store_error("improvement action has conflicting latest leases");
  }
  return latest.empty()
             ? std::nullopt
             : std::optional<InternetImprovementActionLease>(latest.front());
}

std::string InternetImprovementStore::register_improvement_action_lease(
    const InternetImprovementActionLease &lease) {
  const auto canonical = canonical_internet_improvement_action_lease(lease);
  require_type(canonical.run_id, "internet-improvement-run");
  const auto latest = latest_action_lease(canonical.action_key);
  if (!latest && !canonical.predecessor_lease_id.empty()) {
    internet_store_error("first improvement action lease cannot declare a predecessor");
  }
  if (!latest && !canonical.active()) {
    internet_store_error("first improvement action lease must be active");
  }
  if (latest && canonical.predecessor_lease_id != latest->object_id()) {
    internet_store_error("improvement action lease predecessor is not current");
  }
  if (latest && canonical.active() && latest->active() &&
      canonical.acquired_at < latest->expires_at) {
    internet_store_error("active improvement action leases cannot overlap");
  }
  if (latest && !canonical.active() &&
      (!latest->active() || canonical.worker_id != latest->worker_id ||
       canonical.run_id != latest->run_id ||
       canonical.attempt_number != latest->attempt_number ||
       canonical.acquired_at != latest->acquired_at ||
       canonical.expires_at != latest->expires_at)) {
    internet_store_error(
        "terminal improvement action lease must close the current lease");
  }
  return store_.register_record(
      {.object_type = "internet-improvement-action-lease",
       .payload = to_json(canonical)},
      "internet_improvement_action_lease_registered");
}

std::optional<InternetImprovementActionReceipt>
InternetImprovementStore::terminal_action_receipt(std::string_view action_key) {
  std::optional<InternetImprovementActionReceipt> result;
  for (const auto &object : list("internet-improvement-action-receipt")) {
    const auto receipt =
        internet_improvement_action_receipt_from_json(object.payload);
    if (receipt.action_key != action_key) {
      continue;
    }
    if (result && result->object_id() != object.object_id) {
      internet_store_error(
          "improvement action has conflicting terminal receipts");
    }
    result = receipt;
  }
  return result;
}

std::string InternetImprovementStore::register_improvement_action_receipt(
    const InternetImprovementActionReceipt &receipt) {
  const auto canonical = canonical_internet_improvement_action_receipt(receipt);
  require_type(canonical.plan_id, "internet-improvement-plan");
  require_type(canonical.run_id, "internet-improvement-run");
  require_type(canonical.lease_id, "internet-improvement-action-lease");
  const auto latest = latest_action_lease(canonical.action_key);
  if (!latest || latest->object_id() != canonical.lease_id ||
      !latest->active() || latest->run_id != canonical.run_id) {
    internet_store_error(
        "improvement action receipt requires the current active lease");
  }
  const auto existing = terminal_action_receipt(canonical.action_key);
  if (existing && existing->object_id() != canonical.object_id()) {
    internet_store_error("improvement action already has a terminal receipt");
  }
  return store_.register_record(
      {.object_type = "internet-improvement-action-receipt",
       .payload = to_json(canonical)},
      "internet_improvement_action_receipt_registered");
}

std::string InternetImprovementStore::register_experiment_protocol(
    const InternetExperimentProtocol &protocol) {
  const auto canonical = canonical_internet_experiment_protocol(protocol);
  if (!canonical.supersedes_protocol_id.empty()) {
    require_type(canonical.supersedes_protocol_id,
                 "internet-experiment-protocol");
  }
  const std::string id = store_.register_record(
      {.object_type = "internet-experiment-protocol",
       .payload = to_json(canonical)},
      "internet_experiment_protocol_registered");
  if (!canonical.supersedes_protocol_id.empty()) {
    static_cast<void>(store_.supersede(
        canonical.supersedes_protocol_id, id,
        "internet experiment protocol superseded",
        "statewright-internet-improvement-director"));
  }
  return id;
}

std::string InternetImprovementStore::register_source_assessment_input(
    const InternetSourceAssessmentInput &input) {
  const auto canonical = canonical_internet_source_assessment_input(input);
  require_type(canonical.snapshot_id, "internet-source-snapshot");
  require_type(canonical.fetch_receipt_id, "internet-fetch-receipt");
  require_type(canonical.source_policy_id, "internet-source-policy");
  return store_.register_record(
      {.object_type = "internet-source-assessment-input",
       .payload = to_json(canonical)},
      "internet_source_assessment_input_registered");
}

std::string InternetImprovementStore::register_probation_observation_input(
    const InternetProbationObservationInput &input) {
  const auto canonical = canonical_internet_probation_observation_input(input);
  require_type(canonical.candidate_id, "internet-algorithm-candidate");
  require_type(canonical.admission_id, "internet-probation-admission");
  return store_.register_record(
      {.object_type = "internet-probation-observation-input",
       .payload = to_json(canonical)},
      "internet_probation_observation_input_registered");
}

std::string InternetImprovementStore::supersede_algorithm_candidate(
    std::string old_candidate_id,
    const InternetAlgorithmCandidate &replacement, std::string reason) {
  require_type(old_candidate_id, "internet-algorithm-candidate");
  const std::string replacement_id = register_algorithm_candidate(replacement);
  if (replacement_id == old_candidate_id) {
    internet_store_error("candidate successor must change immutable content");
  }
  static_cast<void>(store_.supersede(
      std::move(old_candidate_id), replacement_id, std::move(reason),
      "statewright-internet-improvement-controller"));
  return replacement_id;
}

std::string InternetImprovementStore::migrate_algorithm_candidate(
    std::string candidate_id) {
  require_type(candidate_id, "internet-algorithm-candidate");
  const auto candidate = internet_algorithm_candidate_from_json(
      store_.get(candidate_id).payload);
  if (candidate.object_id() == candidate_id) {
    return candidate_id;
  }
  return supersede_algorithm_candidate(
      std::move(candidate_id), candidate,
      "internet candidate lineage schema migration");
}

std::vector<StoredObject>
InternetImprovementStore::list(std::string_view object_type) {
  if (!object_type.starts_with("internet-")) {
    internet_store_error("internet store list requires an internet object type");
  }
  return store_.list(std::string(object_type));
}

std::vector<std::string> InternetImprovementStore::active_watch_ids() {
  return store_.active_ids("internet-watch");
}

std::vector<std::string> InternetImprovementStore::active_candidate_ids() {
  return store_.active_ids("internet-algorithm-candidate");
}

std::vector<std::string>
InternetImprovementStore::active_experiment_protocol_ids() {
  return store_.active_ids("internet-experiment-protocol");
}

std::vector<std::byte> InternetImprovementStore::snapshot_bytes(
    std::string_view snapshot_id) const {
  require_type(snapshot_id, "internet-source-snapshot");
  const auto snapshot = store_.get(snapshot_id);
  return store_.artifacts().get(
      snapshot.payload.at("artifact_id").get<std::string>());
}

void InternetImprovementStore::verify_integrity() {
  static_cast<void>(store_.event_head());
  store_.validate_projection();
  for (const auto &snapshot : store_.list("internet-source-snapshot")) {
    const auto bytes = store_.artifacts().get(
        snapshot.payload.at("artifact_id").get<std::string>());
    if (contracts::sha256_bytes(bytes) !=
        snapshot.payload.at("body_sha256").get<std::string>()) {
      internet_store_error("internet snapshot body hash mismatch");
    }
  }
}

void InternetImprovementStore::rebuild_projection() {
  store_.rebuild_projection();
}

contracts::Json to_json(const InternetCaptureResult &value) {
  return {{"artifact_bytes_id", value.artifact_bytes_id},
          {"artifact_record_id", value.artifact_record_id},
          {"fetch_receipt_id", value.fetch_receipt_id},
          {"snapshot_id", value.snapshot_id}};
}

} // namespace statewright::egcf
