#pragma once

#include "statewright/egcf/internet_polynomial_protocol_freeze.hpp"
#include "statewright/egcf/internet_feed.hpp"
#include "statewright/sources/records.hpp"

namespace statewright::egcf {

// Register a source-verified frozen design, not an approved experiment.
// Measurements, oracle independence and review remain qualification gates.
[[nodiscard]] inline std::string register_frozen_internet_polynomial_protocol(
    EgcfStore &store, InternetExperimentProtocol protocol,
    const std::string &freeze_id) {
  protocol.source_provenance.at("grounded")["freeze_evidence_id"] = freeze_id;
  verify_internet_polynomial_protocol_freeze(store, protocol, freeze_id);
  const auto &ground = protocol.source_provenance.at("grounded");
  const auto candidate_record = store.get(ground.at("candidate_id").get<std::string>());
  const auto candidate = internet_algorithm_candidate_from_json(candidate_record.payload);
  const auto fragment_record = store.get(candidate.source_fragment_id);
  const auto assessment_record = store.get(candidate.source_policy_assessment_id);
  const auto baseline_record = store.get(protocol.baseline_ref);
  if (fragment_record.object_type != "internet-source-fragment" ||
      assessment_record.object_type != "internet-policy-assessment" ||
      baseline_record.object_type != "egcf-evidence") {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_PROTOCOL_NATIVE_SOURCE_OR_BASELINE_REQUIRED");
  }
  const auto assessment = sources::internet_policy_assessment_from_json(assessment_record.payload);
  if (!assessment.admissible() || assessment.snapshot_id != candidate.snapshot_id) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_PROTOCOL_SOURCE_ASSESSMENT_MISMATCH");
  }
  const auto fragment = sources::internet_source_fragment_from_json(fragment_record.payload);
  verify_internet_candidate_translation(candidate, fragment);
  const auto program = internet_exact_polynomial_program(candidate.proposed_saa_ir);
  const auto &claim = ground.at("claim");
  if (ground.value("review_mode", std::string{}) == "AUTOMATED_CSS_V1" ||
      ground.at("execution_contract_sha256") != contracts::sha256_json(internet_polynomial_contract(program)) ||
      claim.at("inputs") != candidate.semantic_inputs || claim.at("outputs") != candidate.semantic_outputs ||
      claim.at("units") != candidate.units || claim.at("domain").get<std::string>().empty() ||
      claim.at("exclusions").empty()) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_PROTOCOL_CONTRACT_OR_CLAIM_MISMATCH");
  }
  if (protocol.supersedes_protocol_id.empty()) {
    return InternetImprovementStore(store).register_experiment_protocol(protocol);
  }
  // Existing native supersedence semantics remain authoritative, including
  // their separate event history. No historical record is rewritten here.
  return InternetImprovementStore(store).register_experiment_protocol(protocol);
}

} // namespace statewright::egcf
