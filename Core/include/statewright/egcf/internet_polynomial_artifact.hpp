#pragma once

#include "statewright/egcf/internet_polynomial_form.hpp"
#include "statewright/egcf/store.hpp"

#include <span>
#include <vector>

namespace statewright::egcf {

[[nodiscard]] inline contracts::Json internet_polynomial_artifact_provenance(
    const contracts::Json &form) {
  return {{"kind", "INTERNET_FULL_POLYNOMIAL_REPRESENTATION_V1"},
          {"representation_signature", form.at("representation_signature")},
          {"execution_ir_sha256", form.at("execution_ir_sha256")},
          {"stage", "REPRESENTATION_ONLY"},
          {"qualification_claim", "NONE"}};
}

// A native artifact, deliberately not an egcf-evidence or canonical admission.
// Reuse the original recorded_at value when resuming the same registration.
[[nodiscard]] inline std::string register_internet_polynomial_form_artifact(
    EgcfStore &store, const contracts::Json &form,
    std::vector<std::string> source_ids, std::string recorded_at) {
  const auto validated = read_internet_polynomial_form(form);
  if (recorded_at.empty() || recorded_at.size() > 64U) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_ARTIFACT_REQUIRES_RECORDING_TIME");
  }
  for (const auto &source_id : source_ids) {
    static_cast<void>(store.get(source_id));
  }
  const auto bytes = contracts::canonical_json(validated);
  return store.register_artifact(
      std::as_bytes(std::span(bytes.data(), bytes.size())),
      "application/vnd.statewright.polynomial-form+json", std::move(source_ids),
      internet_polynomial_artifact_provenance(validated), std::move(recorded_at));
}

[[nodiscard]] inline contracts::Json load_internet_polynomial_form_artifact(
    const EgcfStore &store, const std::string &artifact_id) {
  const auto record = store.get(artifact_id);
  if (record.object_type != "artifact" ||
      record.payload.at("media_type") !=
          "application/vnd.statewright.polynomial-form+json" ||
      record.payload.at("size").get<std::size_t>() > 131072U) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "INVALID_POLYNOMIAL_ARTIFACT_TYPE_OR_SIZE");
  }
  const auto bytes = store.artifacts().get(
      "artifact-bytes:sha256:" + record.payload.at("sha256").get<std::string>());
  const std::string serialized(reinterpret_cast<const char *>(bytes.data()),
                               bytes.size());
  const auto form = read_internet_polynomial_form(contracts::parse_json(serialized));
  if (bytes.size() != record.payload.at("size").get<std::size_t>() ||
      contracts::sha256_json(form) != record.payload.at("sha256").get<std::string>() ||
      record.payload.at("provenance") !=
          internet_polynomial_artifact_provenance(form)) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_ARTIFACT_BINDING_MISMATCH");
  }
  for (const auto &source_id : record.payload.at("source_ids")) {
    static_cast<void>(store.get(source_id.get<std::string>()));
  }
  return form;
}

// Bind the representation to the actual immutable candidate and its source
// records. This checks structural provenance, not source admissibility or review.
[[nodiscard]] inline std::string register_internet_candidate_polynomial_form(
    EgcfStore &store, const std::string &candidate_id, std::string recorded_at) {
  const auto candidate = store.get(candidate_id);
  if (candidate.object_type != "internet-algorithm-candidate") {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_FORM_REQUIRES_NATIVE_CANDIDATE");
  }
  const auto &payload = candidate.payload;
  if (payload.at("semantic_inputs").size() != 1U ||
      payload.at("semantic_outputs").size() != 1U) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_FORM_REQUIRES_SCALAR_SEMANTICS");
  }
  const auto form = make_internet_polynomial_form(
      payload.at("proposed_saa_ir"),
      payload.at("semantic_inputs").at(0).get<std::string>(),
      payload.at("semantic_outputs").at(0).get<std::string>());
  std::vector<std::string> sources{candidate_id};
  for (const auto &[field, type] : {
           std::pair{"snapshot_id", "internet-source-snapshot"},
           std::pair{"source_fragment_id", "internet-source-fragment"},
           std::pair{"source_policy_assessment_id", "internet-policy-assessment"}}) {
    const auto id = payload.at(field).get<std::string>();
    if (store.get(id).object_type != type) {
      throw common::Error(common::ErrorCode::invalid_argument,
                          "POLYNOMIAL_FORM_SOURCE_RECORD_TYPE_MISMATCH");
    }
    sources.push_back(id);
  }
  return register_internet_polynomial_form_artifact(
      store, form, std::move(sources), std::move(recorded_at));
}

} // namespace statewright::egcf
