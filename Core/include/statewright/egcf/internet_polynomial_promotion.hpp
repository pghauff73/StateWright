#pragma once

#include "statewright/egcf/internet_polynomial_qualification_binding.hpp"
#include "statewright/saa/autonomous_promotion_policy.hpp"

namespace statewright::egcf {

// Reference checks are not an independent policy assessment. The native wrapper
// below additionally validates the stored assessment, policy and source records.
inline void verify_internet_polynomial_promotion_references(
    const InternetAlgorithmCandidate &candidate,
    const saa::AutonomousPromotionAssessment &assessment) {
  if (candidate.status != "POLICY_QUALIFIED" ||
      !candidate.unresolved_assumptions.empty() ||
      candidate.experiment_qualification_ids.size() != 1U ||
      !assessment.promotion_allowed || assessment.human_approval_required ||
      !assessment.blocking_reasons.empty() ||
      assessment.candidate_ref.empty() ||
      assessment.experiment_qualification_ref != candidate.experiment_qualification_ids.front() ||
      assessment.snapshot_ref != candidate.snapshot_id ||
      assessment.retrieval_receipt_ref != candidate.retrieval_receipt_id) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_PROMOTION_REFERENCE_BINDING_MISMATCH");
  }
}

inline void verify_internet_polynomial_promotion_binding(
    const EgcfStore &store, const InternetAlgorithmCandidate &candidate,
    const std::string &assessment_id) {
  const auto reject = [](const char *reason) {
    throw common::Error(common::ErrorCode::invalid_argument, reason);
  };
  if (candidate.promotion_assessment_ids.size() != 1U ||
      candidate.promotion_assessment_ids.front() != assessment_id) {
    reject("POLYNOMIAL_PROMOTION_NOT_BOUND_TO_CANDIDATE");
  }
  const auto record = store.get(assessment_id);
  if (record.object_type != "internet-promotion-assessment") {
    reject("POLYNOMIAL_PROMOTION_ASSESSMENT_TYPE_MISMATCH");
  }
  auto material = record.payload;
  const auto policy_id = material.at("policy_id").get<std::string>();
  material.erase("policy_id");
  material.erase("assessed_at");
  material.erase("source_checked_at");
  const auto assessment = saa::autonomous_promotion_assessment_from_json(material);
  verify_internet_polynomial_promotion_references(candidate, assessment);
  const auto policy_record = store.get(policy_id);
  if (policy_record.object_type != "internet-promotion-policy") {
    reject("POLYNOMIAL_PROMOTION_POLICY_TYPE_MISMATCH");
  }
  const auto policy = saa::autonomous_promotion_policy_from_json(policy_record.payload);
  if (assessment.policy_signature != policy.policy_signature) {
    reject("POLYNOMIAL_PROMOTION_POLICY_BINDING_MISMATCH");
  }
  const auto assessed_record = store.get(assessment.candidate_ref);
  if (assessed_record.object_type != "internet-algorithm-candidate") {
    reject("POLYNOMIAL_PROMOTION_SUBJECT_TYPE_MISMATCH");
  }
  const auto assessed_candidate = internet_algorithm_candidate_from_json(assessed_record.payload);
  verify_internet_polynomial_qualification_binding(
      store, assessed_candidate, assessment.experiment_qualification_ref);
  const auto assessed_source = store.get(assessment.source_policy_assessment_ref);
  const auto candidate_source = store.get(candidate.source_policy_assessment_id);
  if (assessed_source.object_type != "internet-policy-assessment" ||
      candidate_source.object_type != "internet-policy-assessment" ||
      assessed_source.payload.at("snapshot_id").get<std::string>() != candidate.snapshot_id ||
      assessed_source.payload.at("source_policy_id") != candidate_source.payload.at("source_policy_id") ||
      assessed_source.payload.at("status") != "SOURCE_ADMISSIBLE" ||
      !assessed_source.payload.at("blocking_reasons").empty()) {
    reject("POLYNOMIAL_PROMOTION_SOURCE_POLICY_BINDING_MISMATCH");
  }
}

} // namespace statewright::egcf
