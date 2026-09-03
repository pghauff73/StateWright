#include "statewright/egcf/autonomous_promotion.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <limits>
#include <set>
#include <utility>

namespace statewright::egcf {
namespace {

using contracts::Json;

[[noreturn]] void promotion_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

[[nodiscard]] saa::OIECBenchGateAssessment
benchmark_gate_from_json(const Json &value) {
  if (!value.is_object()) {
    promotion_error("persisted benchmark gate must be an object");
  }
  return {.candidate_ref = value.at("candidate_ref").get<std::string>(),
          .profile_signature =
              value.at("profile_signature").get<std::string>(),
          .policy_signature =
              value.at("policy_signature").get<std::string>(),
          .evidence_requirement_coverage_bp =
              value.at("evidence_requirement_coverage_bp").get<int>(),
          .independence_groups =
              value.at("independence_groups")
                  .get<std::vector<std::string>>(),
          .threshold_failures =
              value.at("threshold_failures").get<std::vector<std::string>>(),
          .independent_review = value.at("independent_review").get<bool>(),
          .status = value.at("status").get<std::string>(),
          .canonical_promotion_eligible =
              value.at("canonical_promotion_eligible").get<bool>(),
          .assessment_signature =
              value.at("assessment_signature").get<std::string>()};
}

[[nodiscard]] saa::KnowledgeIntegrityTrajectory
integrity_trajectory_from_json(const Json &value) {
  if (!value.is_object()) {
    promotion_error("persisted integrity trajectory must be an object");
  }
  return {.snapshot_signatures =
              value.at("snapshot_signatures")
                  .get<std::vector<std::string>>(),
          .latest_generation = value.at("latest_generation").get<int>(),
          .status = value.at("status").get<std::string>(),
          .policy_violations =
              value.at("policy_violations")
                  .get<std::vector<std::string>>(),
          .degraded_dimensions =
              value.at("degraded_dimensions")
                  .get<std::vector<std::string>>(),
          .improved_dimensions =
              value.at("improved_dimensions")
                  .get<std::vector<std::string>>(),
          .knowledge_integrity_qualified =
              value.at("knowledge_integrity_qualified").get<bool>(),
          .trajectory_signature =
              value.at("trajectory_signature").get<std::string>()};
}

[[nodiscard]] std::vector<std::string> candidate_primitives(
    const InternetAlgorithmCandidate &candidate) {
  std::set<std::string> result;
  if (!candidate.proposed_saa_ir.contains("nodes") ||
      !candidate.proposed_saa_ir.at("nodes").is_array()) {
    promotion_error("candidate IR lacks nodes");
  }
  for (const auto &node : candidate.proposed_saa_ir.at("nodes")) {
    if (!node.is_object() || !node.contains("primitive") ||
        !node.at("primitive").is_string()) {
      promotion_error("candidate IR node lacks a primitive");
    }
    result.insert(node.at("primitive").get<std::string>());
  }
  return {result.begin(), result.end()};
}

[[nodiscard]] std::vector<std::string> capability_classes(
    const std::vector<std::string> &primitives) {
  std::vector<std::string> result;
  if (std::ranges::find(primitives, "INVOKE") != primitives.end()) {
    result = {"COMMAND_EXECUTION", "PROCESS_EXECUTION"};
  }
  return result;
}

[[nodiscard]] std::string string_value(const Json &value,
                                       std::string_view key) {
  if (!value.contains(key) || !value.at(key).is_string()) {
    promotion_error("persisted promotion input is missing " +
                    std::string(key));
  }
  return value.at(key).get<std::string>();
}

[[nodiscard]] std::chrono::system_clock::time_point
parse_canonical_utc(std::string_view timestamp) {
  if (timestamp.size() != 20U || timestamp[4] != '-' || timestamp[7] != '-' ||
      timestamp[10] != 'T' || timestamp[13] != ':' || timestamp[16] != ':' ||
      timestamp[19] != 'Z') {
    promotion_error("promotion timestamp must use canonical UTC form");
  }
  std::tm parts{};
  const std::string value(timestamp);
  if (strptime(value.c_str(), "%Y-%m-%dT%H:%M:%SZ", &parts) == nullptr) {
    promotion_error("promotion timestamp is invalid");
  }
  const std::time_t seconds = timegm(&parts);
  if (seconds < 0) {
    promotion_error("promotion timestamp is out of range");
  }
  return std::chrono::system_clock::from_time_t(seconds);
}

[[nodiscard]] int source_age_seconds(
    EgcfStore &store, const Json &source_assessment,
    const Json &qualification, std::string_view expected_snapshot_id) {
  const auto receipt = store.get(string_value(source_assessment,
                                               "fetch_receipt_id"));
  if (receipt.object_type != "internet-fetch-receipt" ||
      string_value(receipt.payload, "snapshot_id") != expected_snapshot_id) {
    promotion_error("promotion source receipt binding is invalid");
  }
  const auto lease = store.get(string_value(receipt.payload, "lease_id"));
  if (lease.object_type != "internet-fetch-lease") {
    promotion_error("promotion source lease binding is invalid");
  }
  const auto fetched_at =
      parse_canonical_utc(string_value(lease.payload, "acquired_at"));
  if (!qualification.contains("evidence_ids") ||
      !qualification.at("evidence_ids").is_array() ||
      qualification.at("evidence_ids").empty()) {
    promotion_error("promotion qualification lacks timestamped evidence");
  }
  auto evaluated_at = fetched_at;
  bool found_evaluation = false;
  for (const auto &evidence_id :
       qualification.at("evidence_ids").get<std::vector<std::string>>()) {
    const auto evidence = store.get(evidence_id);
    if (evidence.object_type != "egcf-evidence") {
      promotion_error("promotion qualification evidence binding is invalid");
    }
    const auto created_at =
        parse_canonical_utc(string_value(evidence.payload, "created_at"));
    if (!found_evaluation || created_at > evaluated_at) {
      evaluated_at = created_at;
      found_evaluation = true;
    }
  }
  if (!found_evaluation || evaluated_at < fetched_at) {
    promotion_error("promotion source age is invalid");
  }
  const auto age = std::chrono::duration_cast<std::chrono::seconds>(
                       evaluated_at - fetched_at)
                       .count();
  return age > std::numeric_limits<int>::max()
             ? std::numeric_limits<int>::max()
             : static_cast<int>(age);
}

} // namespace

AutonomousPromotionController::AutonomousPromotionController(EgcfStore &store)
    : store_(store), internet_(store) {}

AutonomousPromotionResult AutonomousPromotionController::assess(
    const InternetAlgorithmCandidate &candidate, std::string policy_id) {
  const auto canonical_candidate =
      canonical_internet_algorithm_candidate(candidate);
  const std::string candidate_id = canonical_candidate.object_id();
  const auto stored_candidate = store_.get(candidate_id);
  if (stored_candidate.object_type != "internet-algorithm-candidate" ||
      canonical_candidate.status != "EXPERIMENT_QUALIFIED") {
    promotion_error(
        "autonomous promotion requires a registered EXPERIMENT_QUALIFIED candidate");
  }
  const auto stored_policy = store_.get(policy_id);
  if (stored_policy.object_type != "internet-promotion-policy") {
    promotion_error("autonomous promotion policy reference has wrong type");
  }
  const auto policy =
      saa::autonomous_promotion_policy_from_json(stored_policy.payload);
  if (canonical_candidate.experiment_qualification_ids.size() != 1U) {
    promotion_error("candidate must bind exactly one active experiment qualification");
  }
  const std::string qualification_id =
      canonical_candidate.experiment_qualification_ids.front();
  const auto qualification = store_.get(qualification_id);
  if (qualification.object_type != "internet-experiment-qualification" ||
      !qualification.payload.at("experiment_qualified").get<bool>() ||
      qualification.payload.at("status").get<std::string>() !=
          "EXPERIMENT_QUALIFIED") {
    promotion_error("candidate experiment qualification is not eligible");
  }
  const auto source_assessment =
      store_.get(canonical_candidate.source_policy_assessment_id);
  if (source_assessment.object_type != "internet-policy-assessment" ||
      string_value(source_assessment.payload, "snapshot_id") !=
          canonical_candidate.snapshot_id) {
    promotion_error("candidate source policy assessment binding is invalid");
  }
  const auto retrieval = store_.get(canonical_candidate.retrieval_receipt_id);
  if (retrieval.object_type != "internet-retrieval-receipt") {
    promotion_error("candidate retrieval receipt binding is invalid");
  }

  bool snapshot_integrity_passed = true;
  try {
    internet_.verify_integrity();
  } catch (const std::exception &) {
    snapshot_integrity_passed = false;
  }
  std::set<std::string> source_groups;
  for (const auto &snapshot_id :
       qualification.payload.at("dataset_snapshot_ids")
           .get<std::vector<std::string>>()) {
    const auto snapshot = store_.get(snapshot_id);
    if (snapshot.object_type != "internet-source-snapshot") {
      promotion_error("experiment qualification dataset is invalid");
    }
    source_groups.insert(string_value(snapshot.payload, "source_group"));
  }
  const auto primitives = candidate_primitives(canonical_candidate);
  const auto benchmark =
      benchmark_gate_from_json(qualification.payload.at("benchmark_gate"));
  const auto integrity = integrity_trajectory_from_json(
      qualification.payload.at("integrity_trajectory"));
  const auto &aggregate = qualification.payload.at("repeated_aggregate");
  if (!aggregate.is_object() ||
      !aggregate.contains("independence_groups")) {
    promotion_error("experiment aggregate is incomplete");
  }
  const std::string domain = string_value(
      store_.get(canonical_candidate.snapshot_id).payload, "source_group");
  const bool semantic_verified =
      !canonical_candidate.semantic_inputs.empty() &&
      !canonical_candidate.semantic_outputs.empty() &&
      canonical_candidate.unresolved_assumptions.empty();
  const std::string mathematical_strength =
      string_value(qualification.payload.at("canonical_candidate_ir"),
                   "canonicalization_strength");

  saa::AutonomousPromotionInputs inputs;
  inputs.candidate_ref = candidate_id;
  inputs.candidate_status = canonical_candidate.status;
  inputs.candidate_class = string_value(retrieval.payload, "novelty_status");
  inputs.candidate_domain = domain;
  inputs.candidate_primitives = primitives;
  inputs.candidate_capability_classes = capability_classes(primitives);
  inputs.source_policy_assessment_ref =
      canonical_candidate.source_policy_assessment_id;
  inputs.snapshot_ref = canonical_candidate.snapshot_id;
  inputs.retrieval_receipt_ref = canonical_candidate.retrieval_receipt_id;
  inputs.experiment_qualification_ref = qualification_id;
  inputs.source_policy_passed =
      string_value(source_assessment.payload, "status") ==
      "SOURCE_ADMISSIBLE";
  inputs.snapshot_integrity_passed = snapshot_integrity_passed;
  inputs.independent_source_groups = static_cast<int>(source_groups.size());
  inputs.semantic_strength = semantic_verified
                                 ? "DETERMINISTIC_SOURCE_BOUND"
                                 : "UNVERIFIED";
  inputs.mathematical_strength = mathematical_strength;
  inputs.existing_knowledge_search_complete =
      retrieval.payload.at("search_complete").get<bool>();
  inputs.experiment_qualified =
      qualification.payload.at("experiment_qualified").get<bool>();
  inputs.independent_experiment_groups = static_cast<int>(
      aggregate.at("independence_groups").size());
  inputs.benchmark_gate = benchmark;
  inputs.integrity_trajectory = integrity;
  inputs.invariants_passed =
      qualification.payload.at("invariants_passed").get<bool>();
  inputs.unresolved_hard_contradictions = 0;
  inputs.successful_falsifiers = 0;
  inputs.source_age_seconds = source_age_seconds(
      store_, source_assessment.payload, qualification.payload,
      canonical_candidate.snapshot_id);
  inputs.probation_plan_valid = policy.probation_window_count > 0 &&
                                policy.minimum_probation_observations > 0 &&
                                policy.minimum_probation_uses > 0 &&
                                policy.maximum_canary_share_bp > 0;
  inputs.demotion_path_valid =
      !policy.automatic_demotion_predicates.empty();

  const auto assessment =
      saa::evaluate_autonomous_promotion(policy, std::move(inputs));
  const std::string assessment_id =
      internet_.register_promotion_assessment(policy_id, assessment);
  auto updated_candidate = canonical_candidate;
  updated_candidate.status = assessment.resulting_state;
  updated_candidate.promotion_assessment_ids.push_back(assessment_id);
  updated_candidate =
      canonical_internet_algorithm_candidate(std::move(updated_candidate));
  const std::string updated_candidate_id =
      internet_.supersede_algorithm_candidate(
          candidate_id, updated_candidate,
          assessment.promotion_allowed ? "autonomous promotion policy passed"
                                       : "autonomous promotion policy blocked");
  AutonomousPromotionResult result{
      .assessment = assessment,
      .updated_candidate = std::move(updated_candidate),
      .policy_id = std::move(policy_id),
      .assessment_id = assessment_id,
      .updated_candidate_id = updated_candidate_id,
      .result_signature = {}};
  auto material = to_json(result);
  material.erase("result_signature");
  result.result_signature = contracts::sha256_json(material);
  return result;
}

contracts::Json to_json(const AutonomousPromotionResult &value) {
  return {{"assessment", saa::to_json(value.assessment)},
          {"assessment_id", value.assessment_id},
          {"policy_id", value.policy_id},
          {"result_signature", value.result_signature},
          {"updated_candidate", to_json(value.updated_candidate)},
          {"updated_candidate_id", value.updated_candidate_id}};
}

} // namespace statewright::egcf
