#pragma once

#include "statewright/egcf/internet_polynomial_form.hpp"
#include "statewright/egcf/internet_records.hpp"
#include "statewright/egcf/store.hpp"
#include "statewright/saa/algorithm_ir.hpp"

#include <algorithm>

namespace statewright::egcf {

// This is an additional admission precondition, not a replacement for source,
// grounded-protocol, reviewer, promotion-policy or operational evidence gates.
inline void verify_internet_polynomial_qualification_form(
    const contracts::Json &form, const contracts::Json &qualification) {
  const auto validated = read_internet_polynomial_form(form);
  const auto reject = [](const char *reason) {
    throw common::Error(common::ErrorCode::invalid_argument, reason);
  };
  if (!qualification.is_object() ||
      !qualification.value("internal_ir_only", false) ||
      qualification.value("downloaded_code_executed", true) ||
      !qualification.value("identical_frozen_contexts", false) ||
      !qualification.value("invariants_passed", false) ||
      qualification.value("known_failure_retry_blocked", true) ||
      !qualification.value("experiment_qualified", false) ||
      !qualification.value("benchmark_passed", false) ||
      !qualification.value("integrity_passed", false) ||
      !qualification.contains("blocking_reasons") ||
      !qualification.at("blocking_reasons").is_array() ||
      !qualification.at("blocking_reasons").empty()) {
    reject("POLYNOMIAL_ADMISSION_REQUIRES_COMPLETE_QUALIFICATION_PASS");
  }
  const auto canonical_ir = saa::to_json(
      saa::canonicalize_mapping(validated.at("execution_ir")));
  if (!qualification.contains("canonical_candidate_ir") ||
      qualification.at("canonical_candidate_ir") != canonical_ir) {
    reject("POLYNOMIAL_QUALIFICATION_EXECUTION_BINDING_MISMATCH");
  }
  if (!qualification.contains("experiment_design") ||
      !qualification.at("experiment_design").is_object() ||
      !qualification.at("experiment_design").contains("execution_contract") ||
      qualification.at("experiment_design").at("execution_contract") !=
          validated.at("execution_contract")) {
    reject("POLYNOMIAL_QUALIFICATION_CONTRACT_BINDING_MISMATCH");
  }
  if (!qualification.contains("evidence_ids") ||
      !qualification.at("evidence_ids").is_array() ||
      qualification.at("evidence_ids").empty()) {
    reject("POLYNOMIAL_QUALIFICATION_REQUIRES_NATIVE_EVIDENCE");
  }
}

inline void verify_internet_polynomial_qualification_binding(
    const EgcfStore &store, const InternetAlgorithmCandidate &candidate,
    const std::string &qualification_id) {
  const auto reject = [](const char *reason) {
    throw common::Error(common::ErrorCode::invalid_argument, reason);
  };
  if (candidate.semantic_inputs.size() != 1U ||
      candidate.semantic_outputs.size() != 1U ||
      std::ranges::find(candidate.experiment_qualification_ids, qualification_id) ==
          candidate.experiment_qualification_ids.end()) {
    reject("POLYNOMIAL_QUALIFICATION_NOT_BOUND_TO_CANDIDATE");
  }
  const auto record = store.get(qualification_id);
  if (record.object_type != "internet-experiment-qualification") {
    reject("POLYNOMIAL_QUALIFICATION_RECORD_TYPE_MISMATCH");
  }
  const auto form = make_internet_polynomial_form(
      candidate.proposed_saa_ir, candidate.semantic_inputs.front(),
      candidate.semantic_outputs.front());
  verify_internet_polynomial_qualification_form(form, record.payload);
  const auto origin = store.get(record.payload.at("candidate_id").get<std::string>());
  if (origin.object_type != "internet-algorithm-candidate") {
    reject("POLYNOMIAL_QUALIFICATION_ORIGIN_TYPE_MISMATCH");
  }
  const auto source_candidate = internet_algorithm_candidate_from_json(origin.payload);
  // Status and receipt lists legitimately change during immutable supersession;
  // mathematical, semantic and source identity must not change underneath proof.
  if (source_candidate.proposed_saa_ir != candidate.proposed_saa_ir ||
      source_candidate.semantic_inputs != candidate.semantic_inputs ||
      source_candidate.semantic_outputs != candidate.semantic_outputs ||
      source_candidate.units != candidate.units ||
      source_candidate.applicability != candidate.applicability ||
      source_candidate.claimed_invariants != candidate.claimed_invariants ||
      source_candidate.termination_properties != candidate.termination_properties ||
      source_candidate.snapshot_id != candidate.snapshot_id ||
      source_candidate.source_fragment_id != candidate.source_fragment_id ||
      source_candidate.source_policy_assessment_id != candidate.source_policy_assessment_id) {
    reject("POLYNOMIAL_QUALIFICATION_SOURCE_OR_SEMANTIC_BINDING_MISMATCH");
  }
  for (const auto &id : record.payload.at("evidence_ids")) {
    if (store.get(id.get<std::string>()).object_type != "egcf-evidence") {
      reject("POLYNOMIAL_QUALIFICATION_EVIDENCE_TYPE_MISMATCH");
    }
  }
}

} // namespace statewright::egcf
