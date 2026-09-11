#pragma once

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include "statewright/egcf/evidence.hpp"
#include "statewright/egcf/internet_improvement_store.hpp"
#include "statewright/egcf/internet_polynomial.hpp"

namespace statewright::egcf {

// Freeze design, not outcomes. Expected values and provenance are retained
// verbatim for independent review; freezing does not establish their truth.
[[nodiscard]] inline contracts::Json internet_polynomial_protocol_design(
    const EgcfStore &store, const InternetExperimentProtocol &proposed) {
  const auto protocol = canonical_internet_experiment_protocol(proposed);
  if (protocol.protocol_version != internet_polynomial_protocol_version) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_FREEZE_REQUIRES_SEPARATE_PROTOCOL_FAMILY");
  }
  const auto &grounded = protocol.source_provenance.at("grounded");
  const auto candidate_id = grounded.at("candidate_id").get<std::string>();
  const auto stored = store.get(candidate_id);
  if (stored.object_type != "internet-algorithm-candidate") {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_FREEZE_REQUIRES_NATIVE_CANDIDATE");
  }
  const auto candidate = internet_algorithm_candidate_from_json(stored.payload);
  const auto program = internet_exact_polynomial_program(candidate.proposed_saa_ir);
  const auto snapshot = store.get(candidate.snapshot_id);
  if (program.direct_power_sum || snapshot.object_type != "internet-source-snapshot" ||
      grounded.at("candidate_ir_sha256") != contracts::sha256_json(candidate.proposed_saa_ir) ||
      grounded.at("source_fragment_id") != candidate.source_fragment_id ||
      grounded.at("source_body_sha256") != snapshot.payload.at("body_sha256")) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_FREEZE_SOURCE_BINDING_MISMATCH");
  }
  auto material = to_json(protocol);
  material.erase("protocol_signature");
  material.erase("benchmark_track_scores");
  material.erase("integrity_snapshots");
  auto &design_grounded = material.at("source_provenance").at("grounded");
  design_grounded.erase("measurement_evidence_id");
  design_grounded.erase("reference_comparison_evidence_id");
  design_grounded.erase("review_evidence_ids");
  design_grounded.erase("freeze_evidence_id");
  contracts::Json result = {
      {"kind", "POLYNOMIAL_PROTOCOL_DESIGN_FREEZE_V1"},
      {"candidate_id", candidate_id},
      {"snapshot_id", candidate.snapshot_id},
      {"protocol_design", material},
      {"execution_contract", internet_polynomial_contract(program)},
      {"qualification_claim", "NONE"}};
  if (result.dump().size() > 262144U) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_PROTOCOL_FREEZE_BUDGET_EXCEEDED");
  }
  return result;
}

[[nodiscard]] inline std::string freeze_internet_polynomial_protocol(
    EgcfStore &store, const InternetExperimentProtocol &proposed) {
  const auto design = internet_polynomial_protocol_design(store, proposed);
  EvidenceInput input;
  input.subject_id = design.at("candidate_id");
  input.content = design;
  input.category = "POLYNOMIAL_PROTOCOL_DESIGN";
  input.producer = "internet-polynomial-protocol-freeze-v1";
  input.method = "IMMUTABLE_PROTOCOL_DESIGN_BEFORE_EXPERIMENTS";
  input.source_snapshot_hash = proposed.source_provenance.at("grounded").at("source_body_sha256");
  input.independence_group = "native-polynomial-design-freeze";
  input.limitations = {
      "Design only; expected results and claimed independence require review",
      "Not source admissibility, measurement evidence or qualification",
      "Outcome fields are excluded; policies, thresholds, trials and source are frozen"};
  return EvidenceManager(store).collect(std::move(input));
}

inline void verify_internet_polynomial_protocol_freeze(
    const EgcfStore &store, const InternetExperimentProtocol &proposed,
    const std::string &freeze_id) {
  const auto record = store.get(freeze_id);
  if (record.object_type != "egcf-evidence") {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_PROTOCOL_FREEZE_REQUIRES_NATIVE_EVIDENCE");
  }
  const auto evidence = evidence_artifact_from_json(record.payload);
  const auto expected = internet_polynomial_protocol_design(store, proposed);
  if (evidence.simulated || evidence.category != "POLYNOMIAL_PROTOCOL_DESIGN" ||
      evidence.content != expected || evidence.sha256 != contracts::sha256_json(expected) ||
      evidence.subject_id != expected.at("candidate_id").get<std::string>()) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_PROTOCOL_FROZEN_DESIGN_CHANGED");
  }
}

} // namespace statewright::egcf
