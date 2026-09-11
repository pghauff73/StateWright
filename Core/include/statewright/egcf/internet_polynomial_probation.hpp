#pragma once

#include "statewright/egcf/internet_polynomial_canonical.hpp"

namespace statewright::egcf {

// Validate a current lifecycle candidate against its original, immutable
// admission. This is not an observation or evidence of successful probation.
[[nodiscard]] inline contracts::Json verify_internet_polynomial_probation_binding(
    EgcfStore &store, const InternetAlgorithmCandidate &candidate,
    const EgcfRecord &admission) {
  const auto fail = [](const char *reason) {
    throw common::Error(common::ErrorCode::invalid_argument, reason);
  };
  if (admission.object_type != "internet-probation-admission" ||
      candidate.probation_admission_ids != std::vector<std::string>{admission.object_id()}) {
    fail("POLYNOMIAL_PROBATION_ADMISSION_REFERENCE_MISMATCH");
  }
  auto material = admission.payload;
  const auto signature = material.at("admission_signature").get<std::string>();
  material.erase("admission_signature");
  if (contracts::sha256_json(material) != signature) {
    fail("POLYNOMIAL_PROBATION_ADMISSION_SIGNATURE_MISMATCH");
  }
  const auto canonical_id = material.at("canonical_algorithm_ref").get<std::string>();
  const auto canonical = read_internet_polynomial_canonical(store, canonical_id);
  const auto origin = internet_algorithm_candidate_from_json(
      store.get(canonical.at("candidate_id").get<std::string>()).payload);
  const auto plan = saa::probation_plan_from_json(material.at("plan"));
  if (candidate.canonical_algorithm_ids != std::vector<std::string>{canonical_id} ||
      candidate.polynomial_form_ids != origin.polynomial_form_ids ||
      candidate.promotion_assessment_ids != origin.promotion_assessment_ids ||
      candidate.experiment_qualification_ids != origin.experiment_qualification_ids ||
      material.value("generation_basis", std::string{}) != "EGCF_OBJECT_COUNT_V1" ||
      material.value("canonical_representation", std::string{}) != internet_polynomial_canonical_version ||
      material.at("canonical_source_ref") != canonical.at("qualified_form_id") ||
      material.at("canonical_store_generation") != canonical.at("native_generation") ||
      plan.candidate_ref != canonical.at("candidate_id").get<std::string>() ||
      plan.promotion_assessment_ref != origin.promotion_assessment_ids.front()) {
    fail("POLYNOMIAL_PROBATION_CANONICAL_BINDING_MISMATCH");
  }
  verify_internet_polynomial_qualification_binding(
      store, candidate, origin.experiment_qualification_ids.front());
  const auto transitions = store.search_text(
      "\"" + candidate.object_id() + "\"", "supersedence", 33);
  if (transitions.size() > 32U) {
    fail("POLYNOMIAL_PROBATION_LINEAGE_BUDGET_EXHAUSTED");
  }
  for (const auto &transition : transitions) {
    if (transition.payload.at("old_id").get<std::string>() == candidate.object_id()) {
      fail("POLYNOMIAL_PROBATION_REQUIRES_CURRENT_CANDIDATE");
    }
  }
  return canonical;
}

} // namespace statewright::egcf
