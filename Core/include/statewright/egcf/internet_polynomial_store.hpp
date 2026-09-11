#pragma once

#include "statewright/egcf/internet_polynomial_qualification_binding.hpp"
#include "statewright/egcf/internet_improvement_store.hpp"

namespace statewright::egcf {

inline constexpr std::string_view internet_polynomial_qualified_form_version =
    "internet-polynomial-qualified-form-v1";

// This native record is a qualification-bound input to future canonical
// admission. It cannot be used as a probation or canonical-acceptance receipt.
[[nodiscard]] inline contracts::Json qualified_internet_polynomial_payload(
    const EgcfStore &store, const std::string &candidate_id,
    const std::string &qualification_id, const std::string &recorded_at) {
  if (recorded_at.empty() || recorded_at.size() > 64U) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_QUALIFIED_FORM_REQUIRES_RECORDING_TIME");
  }
  const auto stored_candidate = store.get(candidate_id);
  if (stored_candidate.object_type != "internet-algorithm-candidate") {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_QUALIFIED_FORM_REQUIRES_NATIVE_CANDIDATE");
  }
  const auto candidate = internet_algorithm_candidate_from_json(stored_candidate.payload);
  verify_internet_polynomial_qualification_binding(store, candidate, qualification_id);
  for (const auto &[id, type] : {
           std::pair{candidate.snapshot_id, "internet-source-snapshot"},
           std::pair{candidate.source_fragment_id, "internet-source-fragment"},
           std::pair{candidate.source_policy_assessment_id, "internet-policy-assessment"}}) {
    const auto source = store.get(id);
    if (source.object_type != type) {
      throw common::Error(common::ErrorCode::invalid_argument,
                          "POLYNOMIAL_QUALIFIED_FORM_SOURCE_TYPE_MISMATCH");
    }
    if (source.object_type != "internet-source-snapshot" &&
        source.payload.at("snapshot_id").get<std::string>() != candidate.snapshot_id) {
      throw common::Error(common::ErrorCode::invalid_argument,
                          "POLYNOMIAL_QUALIFIED_FORM_SOURCE_SNAPSHOT_MISMATCH");
    }
  }
  return {{"schema_version", 1},
          {"version", internet_polynomial_qualified_form_version},
          {"candidate_id", candidate_id},
          {"qualification_id", qualification_id},
          {"snapshot_id", candidate.snapshot_id},
          {"source_fragment_id", candidate.source_fragment_id},
          {"source_policy_assessment_id", candidate.source_policy_assessment_id},
          {"form", make_internet_polynomial_form(
                       candidate.proposed_saa_ir, candidate.semantic_inputs.front(),
                       candidate.semantic_outputs.front())},
          {"recorded_at", recorded_at},
          {"lifecycle_claim", "QUALIFICATION_BOUND_NOT_ADMITTED"}};
}

[[nodiscard]] inline std::string register_qualified_internet_polynomial_form(
    EgcfStore &store, const std::string &candidate_id,
    const std::string &qualification_id, const std::string &recorded_at) {
  return store.register_record(
      {.object_type = "internet-polynomial-qualified-form",
       .payload = qualified_internet_polynomial_payload(
           store, candidate_id, qualification_id, recorded_at)},
      "internet_polynomial_qualified_form_registered");
}

[[nodiscard]] inline contracts::Json load_qualified_internet_polynomial_form(
    const EgcfStore &store, const std::string &record_id) {
  const auto record = store.get(record_id);
  if (record.object_type != "internet-polynomial-qualified-form") {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_QUALIFIED_FORM_RECORD_TYPE_MISMATCH");
  }
  const auto &payload = record.payload;
  const auto expected = qualified_internet_polynomial_payload(
      store, payload.at("candidate_id").get<std::string>(),
      payload.at("qualification_id").get<std::string>(),
      payload.at("recorded_at").get<std::string>());
  if (payload != expected) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_QUALIFIED_FORM_BINDING_MISMATCH");
  }
  return expected;
}

struct InternetPolynomialCheckpoint final {
  std::string candidate_id;
  std::string form_id;
};

// Resume with the immutable operation timestamp, not the wall clock at retry.
// Every write has deterministic content; a crash between writes leaves an
// independently addressable prefix that the same operation can safely replay.
[[nodiscard]] inline InternetPolynomialCheckpoint
checkpoint_qualified_internet_polynomial_candidate(
    EgcfStore &store, const std::string &candidate_id,
    const std::string &qualification_id, const std::string &recorded_at) {
  const auto record = store.get(candidate_id);
  if (record.object_type != "internet-algorithm-candidate") {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_CHECKPOINT_REQUIRES_NATIVE_CANDIDATE");
  }
  auto candidate = internet_algorithm_candidate_from_json(record.payload);
  verify_internet_polynomial_qualification_binding(store, candidate, qualification_id);
  if (!candidate.polynomial_form_ids.empty()) {
    if (candidate.polynomial_form_ids.size() != 1U) {
      throw common::Error(common::ErrorCode::invalid_argument,
                          "POLYNOMIAL_CHECKPOINT_REQUIRES_ONE_FORM");
    }
    const auto &form_id = candidate.polynomial_form_ids.front();
    const auto payload = load_qualified_internet_polynomial_form(store, form_id);
    const auto expected = make_internet_polynomial_form(
        candidate.proposed_saa_ir, candidate.semantic_inputs.front(),
        candidate.semantic_outputs.front());
    if (payload.at("qualification_id").get<std::string>() != qualification_id ||
        payload.at("form") != expected) {
      throw common::Error(common::ErrorCode::invalid_argument,
                          "POLYNOMIAL_CHECKPOINT_PROOF_MISMATCH");
    }
    return {candidate_id, form_id};
  }
  if (candidate.status != "EXPERIMENT_QUALIFIED") {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_CHECKPOINT_REQUIRES_QUALIFIED_STAGE");
  }
  const auto form_id = register_qualified_internet_polynomial_form(
      store, candidate_id, qualification_id, recorded_at);
  candidate.polynomial_form_ids = {form_id};
  candidate = canonical_internet_algorithm_candidate(std::move(candidate));
  InternetImprovementStore internet(store);
  const auto next_id = internet.register_algorithm_candidate(candidate);
  static_cast<void>(store.supersede(
      candidate_id, next_id, "qualified polynomial representation checkpoint",
      "statewright-internet-improvement-controller", recorded_at));
  return {next_id, form_id};
}

} // namespace statewright::egcf
