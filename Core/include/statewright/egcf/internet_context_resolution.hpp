#pragma once

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/egcf/internet_improvement_store.hpp"
#include "statewright/egcf/internet_polynomial_bounds.hpp"
#include "statewright/egcf/internet_polynomial_candidate.hpp"
#include "statewright/egcf/internet_reasoning.hpp"

#include <algorithm>
#include <optional>
#include <string>

namespace statewright::egcf {

inline constexpr std::string_view internet_context_resolution_version =
    "statewright-internet-context-resolution-v1";

struct InternetContextResolutionResult final {
  InternetContextResolution resolution;
  InternetAlgorithmCandidate updated_candidate;
  std::string resolution_id;
  std::string updated_candidate_id;
};

inline void internet_context_resolution_require(bool condition,
                                                const char *reason) {
  if (!condition) {
    throw common::Error(common::ErrorCode::invalid_argument, reason);
  }
}

[[nodiscard]] inline std::string source_url_from_snapshot(
    const contracts::Json &snapshot) {
  for (const auto &key : {"final_url", "requested_url", "canonical_url"}) {
    if (snapshot.contains(key) && snapshot.at(key).is_string() &&
        !snapshot.at(key).get<std::string>().empty()) {
      return snapshot.at(key).get<std::string>();
    }
  }
  return "source-url-unrecorded";
}

[[nodiscard]] inline InternetContextResolution
make_fungrim_chebyshev_quadratic_context_resolution(
    const InternetAlgorithmCandidate &candidate,
    const sources::InternetSourceFragment &fragment,
    const contracts::Json &snapshot,
    std::size_t reasoning_limit_bytes = internet_reasoning_maximum_context_bytes) {
  verify_fungrim_quadratic_candidate_translation(candidate, fragment);
  const auto context = inspect_fungrim_chebyshev_quadratic_context(
      sources::to_json(fragment));
  internet_context_resolution_require(context.has_value(),
      "CONTEXT_RESOLUTION_SOURCE_CONTEXT_UNSUPPORTED");
  const auto bounds = derive_internet_polynomial_bounds(candidate.proposed_saa_ir);
  InternetContextResolution resolution;
  resolution.candidate_id = candidate.object_id();
  resolution.operation = "ChebyshevT(2,x)";
  resolution.source_url = source_url_from_snapshot(snapshot);
  resolution.retrieval_receipt_id = candidate.retrieval_receipt_id;
  resolution.snapshot_id = candidate.snapshot_id;
  resolution.source_fragment_id = candidate.source_fragment_id;
  resolution.source_policy_assessment_id = candidate.source_policy_assessment_id;
  resolution.source_hash = snapshot.at("body_sha256").get<std::string>();
  resolution.source_bundle = contracts::Json::array({
      {{"purpose", "operation_definition"},
       {"source_url", resolution.source_url},
       {"snapshot_id", candidate.snapshot_id},
       {"source_fragment_id", candidate.source_fragment_id},
       {"source_hash", resolution.source_hash},
       {"section_identifier", "Fungrim entry 85e42e"},
       {"byte_start", context->at("entry_byte_start")},
       {"byte_end", context->at("entry_byte_end")},
       {"excerpt_kind", "selected_formula"},
       {"excerpt_text", context->at("source_expression")},
       {"excerpt_sha256", contracts::sha256_json(context->at("source_expression"))}},
      {{"purpose", "source_assumptions"},
       {"section_identifier", "Fungrim entry 85e42e Assumptions"},
       {"excerpt_text", context->at("source_assumptions")},
       {"excerpt_sha256", contracts::sha256_json(context->at("source_assumptions"))}},
      {{"purpose", "translated_contract"},
       {"semantic_inputs", candidate.semantic_inputs},
       {"semantic_outputs", candidate.semantic_outputs},
       {"units", candidate.units},
       {"scope", candidate.applicability.at("translation").at("scope_restrictions")},
       {"execution_contract",
        candidate.applicability.at("translation").at("execution_contract")},
       {"candidate_ir_sha256", contracts::sha256_json(candidate.proposed_saa_ir)}},
      {{"purpose", "domain_branch_error_bounds"},
       {"declared_candidate_domain", "exact rational x in [-1,1]"},
       {"branch_conditions", contracts::Json::array()},
       {"error_bound", bounds.at("absolute_arithmetic_error_on_success")},
       {"output_minimum", bounds.at("output_minimum")},
       {"output_maximum", bounds.at("output_maximum")},
       {"bounds_status", bounds.at("status")},
       {"bounds_sha256", contracts::sha256_json(bounds)}}});
  resolution.resolved_items = {"branch_conditions_absent_for_polynomial",
                               "dependency_closure",
                               "domain",
                               "error_bound",
                               "input_output_contract",
                               "operation_definition",
                               "source_formula",
                               "source_hashes"};
  resolution.dependency_relationships = contracts::Json::array({
      {{"from", "ChebyshevT(2,x)"}, {"to", "selected_formula"},
       {"relationship", "defined_by_source_table_row"}},
      {{"from", "selected_formula"}, {"to", "native_horner_polynomial_ir"},
       {"relationship", "translated_by_supported_fixed_quadratic_adapter"}},
      {{"from", "native_horner_polynomial_ir"}, {"to", "exact_rational_bounds"},
       {"relationship", "qualified_by_native_exact_arithmetic_derivation"}}});
  resolution.bundle_bytes =
      contracts::canonical_json(resolution.source_bundle).size();
  resolution.reasoning_limit_bytes = reasoning_limit_bytes;
  resolution.resolver_version = std::string(internet_context_resolution_version);
  const bool fits = resolution.bundle_bytes <= reasoning_limit_bytes;
  resolution.status = fits ? "CONTEXT_RESOLUTION_COMPLETE"
                           : "CONTEXT_RESOLUTION_INCOMPLETE";
  resolution.mathematical_context_review_status =
      fits ? "MATHEMATICAL_CONTEXT_REVIEW_PASSED"
           : "MATHEMATICAL_CONTEXT_REVIEW_INSUFFICIENT";
  resolution.domain_branch_error_bound_status =
      fits ? "DOMAIN_BRANCH_AND_ERROR_BOUNDS_QUALIFIED"
           : "DOMAIN_BRANCH_AND_ERROR_BOUNDS_INSUFFICIENT";
  if (!fits) {
    resolution.missing_items = {"REASONING_CONTEXT_CAPACITY_UNSUPPORTED"};
  }
  return canonical_internet_context_resolution(std::move(resolution));
}

[[nodiscard]] inline std::optional<InternetContextResolution>
existing_context_resolution(EgcfStore &store, const std::string &candidate_id) {
  const auto matches = store.search_text("\"" + candidate_id + "\"",
      "internet-context-resolution", 33U);
  internet_context_resolution_require(matches.size() <= 32U,
      "CONTEXT_RESOLUTION_LOOKUP_BUDGET_EXHAUSTED");
  std::optional<InternetContextResolution> result;
  for (const auto &record : matches) {
    const auto resolution = internet_context_resolution_from_json(record.payload);
    if (resolution.candidate_id != candidate_id) continue;
    if (result.has_value()) {
      internet_context_resolution_require(
          to_json(*result) == to_json(resolution),
          "CONTEXT_RESOLUTION_CONFLICTING_RECEIPTS");
    } else {
      result = resolution;
    }
  }
  return result;
}

[[nodiscard]] inline InternetAlgorithmCandidate
candidate_after_context_resolution(InternetAlgorithmCandidate candidate,
                                   const InternetContextResolution &resolution) {
  if (resolution.status != "CONTEXT_RESOLUTION_COMPLETE") {
    return canonical_internet_algorithm_candidate(std::move(candidate));
  }
  const auto remove = [](std::vector<std::string> &values,
                         const std::string &value) {
    values.erase(std::remove(values.begin(), values.end(), value),
                 values.end());
  };
  remove(candidate.unresolved_assumptions,
         "REASONING_CONTEXT_CAPACITY_UNSUPPORTED");
  remove(candidate.unresolved_assumptions,
         "MATHEMATICAL_CONTEXT_REVIEW_REQUIRED");
  remove(candidate.unresolved_assumptions,
         "DOMAIN_BRANCH_AND_ERROR_BOUNDS_NOT_QUALIFIED");
  candidate.context_resolution_ids.push_back(resolution.object_id());
  candidate.applicability["context_resolution"] = {
      {"context_resolution_id", resolution.object_id()},
      {"context_signature", resolution.context_signature},
      {"resolver_version", resolution.resolver_version},
      {"status", resolution.status},
      {"qualification_claim",
       "CONTEXT_ONLY_NOT_EXPERIMENT_OR_ADMISSION_EVIDENCE"}};
  if (candidate.unresolved_assumptions.empty() &&
      candidate.status == "QUARANTINED") {
    candidate.status = "VALIDATION_READY";
  }
  return canonical_internet_algorithm_candidate(std::move(candidate));
}

[[nodiscard]] inline InternetContextResolutionResult
resolve_fungrim_chebyshev_quadratic_candidate_context(
    EgcfStore &store, const std::string &candidate_id,
    std::size_t reasoning_limit_bytes = internet_reasoning_maximum_context_bytes,
    bool inspect_only = false) {
  const auto record = store.get(candidate_id);
  internet_context_resolution_require(
      record.object_type == "internet-algorithm-candidate",
      "CONTEXT_RESOLUTION_CANDIDATE_REQUIRED");
  const auto candidate = internet_algorithm_candidate_from_json(record.payload);
  const auto fragment_record = store.get(candidate.source_fragment_id);
  const auto snapshot_record = store.get(candidate.snapshot_id);
  internet_context_resolution_require(
      fragment_record.object_type == "internet-source-fragment" &&
          snapshot_record.object_type == "internet-source-snapshot",
      "CONTEXT_RESOLUTION_SOURCE_RECORDS_REQUIRED");
  const auto fragment =
      sources::internet_source_fragment_from_json(fragment_record.payload);
  auto resolution = make_fungrim_chebyshev_quadratic_context_resolution(
      candidate, fragment, snapshot_record.payload, reasoning_limit_bytes);
  if (const auto existing = existing_context_resolution(store, candidate_id)) {
    resolution = *existing;
  }
  const std::string resolution_id = resolution.object_id();
  auto updated = candidate_after_context_resolution(candidate, resolution);
  if (!inspect_only) {
    InternetImprovementStore internet(store);
    if (!store.objects().contains(resolution_id)) {
      static_cast<void>(internet.register_context_resolution(resolution));
    }
    const auto updated_id = updated.object_id();
    if (!store.objects().contains(updated_id)) {
      static_cast<void>(internet.register_algorithm_candidate(updated));
      static_cast<void>(store.supersede(
          candidate_id, updated_id, "internet context resolved",
          "statewright-internet-context-resolution"));
    }
  }
  return {resolution, updated, resolution_id, updated.object_id()};
}

[[nodiscard]] inline contracts::Json to_json(
    const InternetContextResolutionResult &value) {
  return {{"context_resolution", to_json(value.resolution)},
          {"context_resolution_id", value.resolution_id},
          {"updated_candidate", to_json(value.updated_candidate)},
          {"updated_candidate_id", value.updated_candidate_id},
          {"qualification_claim",
           "CONTEXT_ONLY_NOT_EXPERIMENT_OR_ADMISSION_EVIDENCE"},
          {"admission", false}};
}

} // namespace statewright::egcf
