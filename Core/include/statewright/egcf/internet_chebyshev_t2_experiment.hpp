#pragma once

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/egcf/evidence.hpp"
#include "statewright/egcf/internet_context_resolution.hpp"
#include "statewright/egcf/internet_improvement_store.hpp"
#include "statewright/egcf/internet_polynomial_operation.hpp"
#include "statewright/egcf/internet_polynomial_candidate.hpp"
#include "statewright/egcf/internet_polynomial.hpp"
#include "statewright/saa/algorithm_ir.hpp"

#include <gmpxx.h>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <map>
#include <set>
#include <optional>
#include <string>
#include <vector>

namespace statewright::egcf {

using Json = contracts::Json;

inline constexpr std::string_view chebyshev_t2_experiment_version =
    "exact-rational-chebyshev-t2-v1";

struct ChebyshevT2GroupResult final {
  std::string name;
  int deterministic_seed = 0;
  std::vector<mpq_class> inputs;
  std::vector<mpq_class> expected_outputs;
};

inline void chebyshev_t2_require(bool condition, const char *reason) {
  if (!condition) {
    throw common::Error(common::ErrorCode::invalid_argument, reason);
  }
}

[[nodiscard]] inline mpq_class chebyshev_t2_reference_output(const mpq_class &input) {
  return 2 * input * input - 1;
}

[[nodiscard]] inline std::vector<ChebyshevT2GroupResult>
chebyshev_t2_fixture_groups() {
  return {
      {"boundary", 101,
       {mpq_class(-1), mpq_class(-1, 2), mpq_class(0), mpq_class(1, 2), mpq_class(1)},
       {chebyshev_t2_reference_output(mpq_class(-1)),
        chebyshev_t2_reference_output(mpq_class(-1, 2)),
        chebyshev_t2_reference_output(mpq_class(0)),
        chebyshev_t2_reference_output(mpq_class(1, 2)),
        chebyshev_t2_reference_output(mpq_class(1))}},
      {"near_boundary", 107,
       {mpq_class(-999, 1000), mpq_class(-997, 1000),
        mpq_class(997, 1000), mpq_class(999, 1000)},
       {chebyshev_t2_reference_output(mpq_class(-999, 1000)),
        chebyshev_t2_reference_output(mpq_class(-997, 1000)),
        chebyshev_t2_reference_output(mpq_class(997, 1000)),
        chebyshev_t2_reference_output(mpq_class(999, 1000))}},
      {"small_rationals", 113,
       {mpq_class(-1, 3), mpq_class(-2, 5), mpq_class(2, 5),
        mpq_class(1, 3)},
       {chebyshev_t2_reference_output(mpq_class(-1, 3)),
        chebyshev_t2_reference_output(mpq_class(-2, 5)),
        chebyshev_t2_reference_output(mpq_class(2, 5)),
        chebyshev_t2_reference_output(mpq_class(1, 3))}},
      {"larger_denominators", 127,
       {mpq_class(-123, 997), mpq_class(-256, 997), mpq_class(256, 997),
        mpq_class(789, 997)},
       {chebyshev_t2_reference_output(mpq_class(-123, 997)),
        chebyshev_t2_reference_output(mpq_class(-256, 997)),
        chebyshev_t2_reference_output(mpq_class(256, 997)),
        chebyshev_t2_reference_output(mpq_class(789, 997))}},
  };
}

[[nodiscard]] inline bool is_chebyshev_t2_candidate(
    const InternetAlgorithmCandidate &candidate) {
  if (candidate.status != "VALIDATION_READY" ||
      candidate.semantic_inputs != std::vector<std::string>{"x"} ||
      candidate.semantic_outputs != std::vector<std::string>{"y"} ||
      candidate.context_resolution_ids.empty() ||
      !candidate.unresolved_assumptions.empty()) {
    return false;
  }
  if (!candidate.applicability.contains("translation") ||
      candidate.applicability.at("translation").at("translator_version")
          .get<std::string>() != "fungrim-chebyshev-quadratic-candidate-v1") {
    return false;
  }
  const auto program = internet_exact_polynomial_program(candidate.proposed_saa_ir);
  if (program.direct_power_sum || program.coefficients.size() != 3U ||
      program.coefficients.front() != mpq_class(-1) ||
      program.coefficients.at(1) != mpq_class(0) ||
      program.coefficients.at(2) != mpq_class(2)) {
    return false;
  }
  return true;
}

[[nodiscard]] inline InternetContextResolution
resolve_complete_chebyshev_t2_context(EgcfStore &store,
                                     const InternetAlgorithmCandidate &candidate) {
  for (const auto &context_id : candidate.context_resolution_ids) {
    const auto record = store.get(context_id);
    if (record.object_type != "internet-context-resolution") {
      continue;
    }
    const auto resolution = internet_context_resolution_from_json(record.payload);
    if (resolution.candidate_id == candidate.object_id() &&
        resolution.operation == "ChebyshevT(2,x)" &&
        resolution.status == "CONTEXT_RESOLUTION_COMPLETE") {
      return resolution;
    }
  }
  throw common::Error(common::ErrorCode::invalid_argument,
                      "CHEBYSHEV_T2_CONTEXT_RESOLUTION_MISSING");
}

[[nodiscard]] inline Json chebyshev_t2_machine_gates(
    bool context_bound, bool bounded_domain, bool invariant_inputs,
    bool exact_match, const std::vector<std::string> &blocking_reasons) {
  Json gates = Json::array();
  gates.push_back({{"gate", "CONTEXT_SIGNATURE_PRESENT"},
                   {"status", context_bound ? "PASS" : "FAIL"},
                   {"reason",
                    context_bound ? "context signature is available"
                                 : "context signature missing"}});
  gates.push_back({{"gate", "BOUNDED_DOMAIN"},
                   {"status", bounded_domain ? "PASS" : "FAIL"},
                   {"reason",
                    bounded_domain ? "sample points within x∈[-1,1]"
                                  : "sample points violate declared domain"}});
  gates.push_back({{"gate", "OUTPUT_BOUND_INVARIANTS"},
                   {"status", invariant_inputs ? "PASS" : "FAIL"},
                   {"reason",
                    invariant_inputs ? "all sample points distinct"
                                    : "non-distinct inputs or unsupported fixture"}});
  gates.push_back({{"gate", "EXACT_MATCH"},
                   {"status", exact_match ? "PASS" : "FAIL"},
                   {"reason",
                    exact_match ? "candidate equals recurrence baseline for all points"
                               : "one or more fixture mismatches"},
                   {"blocking_reasons", blocking_reasons}});
  return gates;
}

[[nodiscard]] inline Json chebyshev_t2_trial_groups_for_design(
    const std::vector<ChebyshevT2GroupResult> &groups) {
  Json result = Json::array();
  for (const auto &group : groups) {
    Json inputs = Json::array();
    Json expected = Json::array();
    for (const auto &value : group.inputs) {
      inputs.push_back(value.get_str());
    }
    for (const auto &value : group.expected_outputs) {
      expected.push_back(value.get_str());
    }
    const auto signature = contracts::sha256_json({
        {"inputs", inputs}, {"expected_outputs", expected},
        {"name", group.name}, {"deterministic_seed", group.deterministic_seed}});
    result.push_back({
        {"name", group.name},
        {"deterministic_seed", group.deterministic_seed},
        {"inputs", std::move(inputs)},
        {"expected_outputs", std::move(expected)},
        {"group_signature", signature},
    });
  }
  return result;
}

[[nodiscard]] inline Json chebyshev_t2_group_signature_inputs(
    const ChebyshevT2GroupResult &group) {
  Json inputs = Json::array();
  Json expected = Json::array();
  for (const auto &value : group.inputs) {
    inputs.push_back(value.get_str());
  }
  for (const auto &value : group.expected_outputs) {
    expected.push_back(value.get_str());
  }
  return {{"name", group.name},
          {"deterministic_seed", group.deterministic_seed},
          {"inputs", std::move(inputs)},
          {"expected_outputs", std::move(expected)}};
}

[[nodiscard]] inline Json qualify_fungrim_chebyshev_t2_exact_rational_candidate(
    EgcfStore &store, const std::string &candidate_id,
    const std::string &recorded_at, bool inspect_only = false) {
  // These fixtures share author, arithmetic and execution infrastructure. They
  // cannot supply protocol review, benchmark scores or longitudinal evidence.
  chebyshev_t2_require(inspect_only,
                       "POLYNOMIAL_VERSIONED_GROUNDED_PROTOCOL_REQUIRED");
  chebyshev_t2_require(!candidate_id.empty(),
                       "CHEBYSHEV_T2_CANDIDATE_ID_REQUIRED");
  const auto candidate_record = store.get(candidate_id);
  chebyshev_t2_require(candidate_record.object_type == "internet-algorithm-candidate",
                       "CHEBYSHEV_T2_CANDIDATE_TYPE_REQUIRED");
  const auto candidate = internet_algorithm_candidate_from_json(candidate_record.payload);
  chebyshev_t2_require(candidate.object_id() == candidate_record.object_id() &&
                           is_chebyshev_t2_candidate(candidate),
                       "CHEBYSHEV_T2_CANDIDATE_NOT_READY");
  const auto snapshot_record = store.get(candidate.snapshot_id);
  chebyshev_t2_require(snapshot_record.object_type == "internet-source-snapshot",
                       "CHEBYSHEV_T2_SNAPSHOT_REQUIRED");
  const auto source_snapshot_hash = snapshot_record.payload.at("body_sha256").get<std::string>();
  const auto context = resolve_complete_chebyshev_t2_context(store, candidate);
  const auto candidate_contract = internet_polynomial_contract(
      internet_exact_polynomial_program(candidate.proposed_saa_ir));
  auto groups = chebyshev_t2_fixture_groups();
  chebyshev_t2_require(!groups.empty(), "CHEBYSHEV_T2_GROUPS_MISSING");

  std::set<mpq_class> unique_inputs;
  Json per_group_results = Json::array();
  std::size_t total = 0U;
  bool exact_match = true;
  std::size_t match_count = 0U;
  std::size_t mismatch_count = 0U;

  for (auto &group : groups) {
    std::size_t group_match_count = 0U;
    std::size_t group_mismatch_count = 0U;
    Json sample_records = Json::array();
    for (std::size_t index = 0; index < group.inputs.size(); ++index) {
      auto input = group.inputs[index];
      auto expected = group.expected_outputs[index];
      chebyshev_t2_require(input >= mpq_class(-1) && input <= mpq_class(1),
                           "CHEBYSHEV_T2_INPUT_OUTSIDE_DOMAIN");
      const bool distinct = unique_inputs.insert(input).second;
      chebyshev_t2_require(distinct, "CHEBYSHEV_T2_DUPLICATE_INPUT_FIXTURE");
      auto candidate_start = std::chrono::steady_clock::now();
      const auto candidate_value =
          internet_execute_exact_polynomial(
              internet_exact_polynomial_program(candidate.proposed_saa_ir), input);
      auto candidate_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::steady_clock::now() - candidate_start)
                                   .count();
      auto baseline_start = std::chrono::steady_clock::now();
      const auto baseline_value = chebyshev_t2_reference_output(input);
      auto baseline_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::steady_clock::now() - baseline_start)
                                  .count();
      const bool equality = (candidate_value == baseline_value && candidate_value == expected);
      exact_match = exact_match && equality;
      if (equality) {
        ++match_count;
        ++group_match_count;
      } else {
        ++mismatch_count;
        ++group_mismatch_count;
      }
      ++total;
      sample_records.push_back({
          {"input", input.get_str()},
          {"expected", expected.get_str()},
          {"candidate", candidate_value.get_str()},
          {"baseline", baseline_value.get_str()},
          {"equal", equality},
          {"candidate_elapsed_nanoseconds", candidate_elapsed},
          {"baseline_elapsed_nanoseconds", baseline_elapsed},
      });
    }
    const auto group_signature = contracts::sha256_json({
        {"name", group.name},
        {"deterministic_seed", group.deterministic_seed},
        {"group_signature_inputs",
         chebyshev_t2_group_signature_inputs(group)}});
    per_group_results.push_back({
        {"name", group.name},
        {"deterministic_seed", group.deterministic_seed},
        {"group_signature", group_signature},
        {"samples", std::move(sample_records)},
        {"pass_count", group_match_count},
        {"fail_count", group_mismatch_count},
    });
  }

  const auto bounded_domain = true;
  const bool invariant_inputs = true;
  std::vector<std::string> blocking_reasons;
  if (!exact_match) {
    blocking_reasons.push_back("EXPERIMENTALLY_SAMPLED_MISMATCH");
  }
  const auto gates = chebyshev_t2_machine_gates(
      !context.context_signature.empty(), bounded_domain, invariant_inputs,
      exact_match, blocking_reasons);

  Json group_design_inputs = chebyshev_t2_trial_groups_for_design(groups);

  Json design_content = {
      {"kind", "CHEBYSHEV_T2_EXPERIMENT_GROUPS_V1"},
      {"family", chebyshev_t2_experiment_version},
      {"operation", "ChebyshevT(2,x)"},
      {"candidate_id", candidate.object_id()},
      {"snapshot_id", candidate.snapshot_id},
      {"context_signature", context.context_signature},
      {"candidate_contract", candidate_contract},
      {"recorded_at", recorded_at},
      {"qualification_claim", "NONE"},
      {"groups", group_design_inputs},
      {"independence_groups", Json::array({"boundary", "near_boundary", "small_rationals", "larger_denominators"})},
  };

  return {{"candidate_id", candidate.object_id()},
          {"status", "INSPECT_ONLY_VALIDATION"},
          {"candidate_status", candidate.status},
          {"source_snapshot_hash", source_snapshot_hash},
          {"context_signature", context.context_signature},
          {"design", design_content},
          {"runs", per_group_results},
          {"gates", gates},
          {"blocking_reasons", blocking_reasons},
          {"total_samples", total},
          {"match_count", match_count},
          {"mismatch_count", mismatch_count},
          {"qualification_claim", "NONE"},
          {"independence_status", "NOT_ESTABLISHED"},
          {"admission", false}};
}

} // namespace statewright::egcf
