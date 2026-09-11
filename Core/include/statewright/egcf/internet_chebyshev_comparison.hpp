#pragma once

#include "statewright/egcf/internet_chebyshev_reference_design.hpp"
#include "statewright/egcf/internet_polynomial.hpp"

namespace statewright::egcf {

// Validate the complete frozen design before executing any candidate operation.
// Integer recurrence supplies expected values; GMP Horner supplies actual values.
inline void verify_chebyshev_reference_design(
    const std::string &proposal_id, const std::string &proposal_hash,
    const contracts::Json &proposal, const contracts::Json &design) {
  const auto require = [](bool condition, const char *reason) {
    if (!condition) throw common::Error(common::ErrorCode::invalid_argument, reason);
  };
  const auto &groups = design.at("groups");
  require(groups.is_array() && groups.size() >= 2U && groups.size() <= 8U,
          "CHEBYSHEV_COMPARISON_GROUP_BOUNDS_INVALID");
  contracts::Json reconstructed = contracts::Json::array();
  std::size_t total = 0;
  for (const auto &group : groups) {
    const auto &inputs = group.at("inputs");
    require(inputs.is_array() && !inputs.empty() && inputs.size() <= 32U &&
        total + inputs.size() <= 128U, "CHEBYSHEV_COMPARISON_CASE_BOUNDS_INVALID");
    contracts::Json cases = contracts::Json::array();
    for (const auto &input : inputs) {
      require(input.is_string() && input.get_ref<const std::string &>().size() <= 8U,
              "CHEBYSHEV_COMPARISON_INPUT_ENCODING_INVALID");
      bool found = false;
      // Enumerate the small declared reference domain rather than parsing
      // arbitrary-size rational strings supplied by a stored object.
      for (std::int64_t q = 1; q <= 8 && !found; ++q) {
        for (std::int64_t p = -q; p <= q; ++p) {
          if (internet_chebyshev_integer_reference(1, p, q) ==
              input.get_ref<const std::string &>()) {
            cases.push_back({{"numerator", p}, {"denominator", q}});
            found = true;
            break;
          }
        }
      }
      require(found, "CHEBYSHEV_COMPARISON_INPUT_OUTSIDE_REFERENCE_DOMAIN");
    }
    total += inputs.size();
    reconstructed.push_back({{"name", group.at("name")}, {"cases", cases}});
  }
  require(design == build_chebyshev_reference_design(
      proposal_id, proposal_hash, proposal, reconstructed),
      "CHEBYSHEV_COMPARISON_FROZEN_DESIGN_MISMATCH");
}

[[nodiscard]] inline contracts::Json compare_chebyshev_reference_design(
    const std::string &proposal_id, const std::string &proposal_hash,
    const contracts::Json &proposal, const contracts::Json &design) {
  verify_chebyshev_reference_design(proposal_id, proposal_hash, proposal, design);
  const auto program = internet_exact_polynomial_program(proposal.at("proposed_saa_ir"));
  if (program.direct_power_sum) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "CHEBYSHEV_COMPARISON_HORNER_CANDIDATE_REQUIRED");
  }
  const auto &groups = design.at("groups");
  contracts::Json results = contracts::Json::array();
  std::size_t mismatches = 0;
  for (const auto &group : groups) {
    contracts::Json cases = contracts::Json::array();
    const auto &inputs = group.at("inputs");
    for (std::size_t i = 0; i < inputs.size(); ++i) {
      const auto input = inputs.at(i).get<std::string>();
      const auto actual = internet_execute_exact_polynomial(program, mpq_class(input)).get_str();
      const auto &expected = group.at("expected_outputs").at(i);
      const bool matches = expected == actual;
      if (!matches) ++mismatches;
      cases.push_back({{"input", input}, {"expected", expected},
          {"actual", actual}, {"matches", matches}});
    }
    results.push_back({{"name", group.at("name")}, {"cases", cases}});
  }
  return {{"kind", "CHEBYSHEV_REFERENCE_COMPARISON_V1"},
      {"design_sha256", contracts::sha256_json(design)},
      {"proposal_evidence_id", proposal_id}, {"proposal_evidence_sha256", proposal_hash},
      {"candidate_ir_sha256", proposal.at("candidate_ir_sha256")},
      {"groups", results}, {"case_count", design.at("case_count")}, {"mismatch_count", mismatches},
      {"status", mismatches == 0U ? "SAMPLED_MATCH" : "SAMPLED_MISMATCH"},
      {"candidate_evaluated", true}, {"independence_status", "NOT_ESTABLISHED"},
      {"qualification_claim", "NONE"},
      {"limitations", contracts::Json::array({
          "Finite pointwise comparisons do not establish a uniform error bound",
          "Shared author, compiler and host; no independent-review verdict",
          "Not performance, longitudinal integrity or acceptance evidence"})}};
}

} // namespace statewright::egcf
