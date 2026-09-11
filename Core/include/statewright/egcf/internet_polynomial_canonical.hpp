#pragma once

#include "statewright/egcf/autonomous_promotion.hpp"
#include "statewright/egcf/internet_polynomial_promotion.hpp"
#include "statewright/egcf/internet_polynomial_store.hpp"

#include <limits>

namespace statewright::egcf {

inline constexpr std::string_view internet_polynomial_canonical_version =
    "internet-full-polynomial-canonical-v1";

struct InternetPolynomialCanonicalAdmission final {
  std::string canonical_id;
  std::string qualified_form_id;
  std::size_t native_generation = 0;
  bool created = false;
};

// Validate the complete representation and its immutable policy/qualification
// bindings. This does not establish current freshness or probation completion.
[[nodiscard]] inline contracts::Json internet_polynomial_canonical_binding(
    const EgcfStore &store, const std::string &candidate_id) {
  const auto record = store.get(candidate_id);
  if (record.object_type != "internet-algorithm-candidate") {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_CANONICAL_REQUIRES_NATIVE_CANDIDATE");
  }
  const auto candidate = internet_algorithm_candidate_from_json(record.payload);
  if (candidate.status != "POLICY_QUALIFIED" ||
      candidate.promotion_assessment_ids.size() != 1U ||
      candidate.polynomial_form_ids.size() != 1U ||
      candidate.experiment_qualification_ids.size() != 1U) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_CANONICAL_REQUIRES_POLICY_AND_FORM_CHECKPOINT");
  }
  verify_internet_polynomial_promotion_binding(
      store, candidate, candidate.promotion_assessment_ids.front());
  verify_internet_polynomial_qualification_binding(
      store, candidate, candidate.experiment_qualification_ids.front());
  const auto form_id = candidate.polynomial_form_ids.front();
  const auto checkpoint = load_qualified_internet_polynomial_form(store, form_id);
  const auto form = make_internet_polynomial_form(
      candidate.proposed_saa_ir, candidate.semantic_inputs.front(),
      candidate.semantic_outputs.front());
  if (checkpoint.at("form") != form ||
      checkpoint.at("qualification_id").get<std::string>() !=
          candidate.experiment_qualification_ids.front()) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_CANONICAL_CHECKPOINT_MISMATCH");
  }
  return {{"schema_version", 1},
          {"canonical_version", internet_polynomial_canonical_version},
          {"candidate_id", candidate_id},
          {"qualified_form_id", form_id},
          {"promotion_assessment_id", candidate.promotion_assessment_ids.front()},
          {"form", form},
          {"generation_basis", "EGCF_OBJECT_COUNT_V1"},
          {"probation_required", true}};
}

[[nodiscard]] inline contracts::Json read_internet_polynomial_canonical(
    const EgcfStore &store, const std::string &canonical_id) {
  const auto record = store.get(canonical_id);
  if (record.object_type != "internet-polynomial-canonical") {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_CANONICAL_RECORD_TYPE_MISMATCH");
  }
  auto binding = record.payload;
  const auto generation = binding.at("native_generation").get<std::size_t>();
  const auto created_at = binding.at("created_at").get<std::string>();
  binding.erase("native_generation");
  binding.erase("created_at");
  if (generation == 0U || created_at.empty() || created_at.size() > 64U ||
      binding != internet_polynomial_canonical_binding(
          store, binding.at("candidate_id").get<std::string>())) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_CANONICAL_BINDING_MISMATCH");
  }
  return record.payload;
}

// Materialize canonical mathematical representation only. The normal probation
// controller must still admit, observe and promote the candidate separately.
[[nodiscard]] inline InternetPolynomialCanonicalAdmission
admit_internet_polynomial_canonical(EgcfStore &store,
                                   const std::string &candidate_id,
                                   const std::string &current_timestamp) {
  auto payload = internet_polynomial_canonical_binding(store, candidate_id);
  const auto candidate = internet_algorithm_candidate_from_json(store.get(candidate_id).payload);
  const auto assessment = store.get(candidate.promotion_assessment_ids.front());
  const auto policy = store.get(assessment.payload.at("policy_id").get<std::string>());
  const auto freshness = internet_source_freshness(store, candidate, current_timestamp);
  if (!assessment.payload.contains("assessed_at") ||
      assessment.payload.at("assessed_at").get<std::string>() > current_timestamp ||
      !freshness.admissible || freshness.age_seconds >
          policy.payload.at("maximum_source_age_seconds").get<int>()) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_CANONICAL_REQUIRES_CURRENT_SOURCE_AND_POLICY");
  }
  const auto matches = store.search_text(
      "\"" + candidate_id + "\"", "internet-polynomial-canonical", 3);
  if (matches.size() > 2U) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_CANONICAL_LOOKUP_BUDGET_EXHAUSTED");
  }
  std::optional<InternetPolynomialCanonicalAdmission> existing;
  for (const auto &match : matches) {
    if (match.payload.at("candidate_id").get<std::string>() != candidate_id) continue;
    const auto value = read_internet_polynomial_canonical(store, match.object_id);
    if (existing) {
      throw common::Error(common::ErrorCode::invalid_argument,
                          "POLYNOMIAL_CANONICAL_AMBIGUOUS_RECORDS");
    }
    existing = InternetPolynomialCanonicalAdmission{
        match.object_id, value.at("qualified_form_id").get<std::string>(),
        value.at("native_generation").get<std::size_t>(), false};
  }
  if (existing) return *existing;
  const auto count = store.projection_checkpoint().object_count;
  if (count == std::numeric_limits<std::size_t>::max()) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_CANONICAL_GENERATION_EXHAUSTED");
  }
  payload["native_generation"] = count + 1U;
  payload["created_at"] = current_timestamp;
  const auto id = store.register_record(
      {.object_type = "internet-polynomial-canonical", .payload = payload},
      "internet_polynomial_canonical_representation_registered");
  return {id, payload.at("qualified_form_id").get<std::string>(), count + 1U, true};
}

} // namespace statewright::egcf
