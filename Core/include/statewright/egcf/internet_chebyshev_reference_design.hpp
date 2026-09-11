#pragma once

#include "statewright/egcf/internet_chebyshev_integer_reference.hpp"
#include "statewright/contracts/hash.hpp"

#include <set>

namespace statewright::egcf {

[[nodiscard]] inline contracts::Json build_chebyshev_reference_design(
    const std::string &proposal_id, const std::string &proposal_hash,
    const contracts::Json &proposal, const contracts::Json &groups) {
  const auto require = [](bool condition, const char *reason) {
    if (!condition) throw common::Error(common::ErrorCode::invalid_argument, reason);
  };
  require(!proposal_id.empty() && proposal_hash.size() == 64U,
          "CHEBYSHEV_REFERENCE_PROPOSAL_BINDING_REQUIRED");
  const auto &source = proposal.at("source_context");
  require(source.at("operation") == "ChebyshevT" && source.at("degree").is_number_integer() &&
      source.at("degree") >= 0 && source.at("degree") <= 16,
      "CHEBYSHEV_REFERENCE_SOURCE_OPERATION_UNSUPPORTED");
  const int degree = source.at("degree").get<int>();
  require(proposal.at("candidate_ir_sha256") == contracts::sha256_json(proposal.at("proposed_saa_ir")),
          "CHEBYSHEV_REFERENCE_PROPOSAL_IR_HASH_MISMATCH");
  require(groups.is_array() && groups.size() >= 2U && groups.size() <= 8U,
          "CHEBYSHEV_REFERENCE_GROUP_BOUNDS_INVALID");
  std::set<std::string> names;
  std::set<std::string> distinct_inputs;
  contracts::Json frozen = contracts::Json::array();
  std::size_t total = 0;
  for (const auto &group : groups) {
    require(group.is_object() && group.size() == 2U && group.contains("name") &&
        group.at("name").is_string() && group.contains("cases"),
        "CHEBYSHEV_REFERENCE_GROUP_ENCODING_INVALID");
    const auto name = group.at("name").get<std::string>();
    require(!name.empty() && name.size() <= 64U &&
        name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos &&
        names.insert(name).second, "CHEBYSHEV_REFERENCE_GROUP_NAME_INVALID");
    const auto &cases = group.at("cases");
    require(cases.is_array() && !cases.empty() && cases.size() <= 32U && total + cases.size() <= 128U,
            "CHEBYSHEV_REFERENCE_CASE_BOUNDS_INVALID");
    contracts::Json inputs = contracts::Json::array();
    contracts::Json expected = contracts::Json::array();
    for (const auto &item : cases) {
      require(item.is_object() && item.size() == 2U && item.contains("numerator") &&
          item.contains("denominator") && item.at("numerator").is_number_integer() &&
          item.at("denominator").is_number_integer() && item.at("numerator") >= -8 &&
          item.at("numerator") <= 8 && item.at("denominator") >= 1 && item.at("denominator") <= 8,
          "CHEBYSHEV_REFERENCE_CASE_ENCODING_INVALID");
      const auto numerator = item.at("numerator").get<std::int64_t>();
      const auto denominator = item.at("denominator").get<std::int64_t>();
      // Degree one normalizes p/q using the reference's own integer arithmetic.
      const auto input = internet_chebyshev_integer_reference(1, numerator, denominator);
      require(distinct_inputs.insert(input).second, "CHEBYSHEV_REFERENCE_DUPLICATE_INPUT");
      inputs.push_back(input);
      expected.push_back(internet_chebyshev_integer_reference(degree, numerator, denominator));
    }
    total += cases.size();
    frozen.push_back({{"name", name}, {"inputs", inputs}, {"expected_outputs", expected}});
  }
  return contracts::Json{
      {"kind", "CHEBYSHEV_INTEGER_REFERENCE_DESIGN_V1"},
      {"reference_version", internet_chebyshev_integer_reference_version},
      {"proposal_evidence_id", proposal_id}, {"proposal_evidence_sha256", proposal_hash},
      {"candidate_ir_sha256", proposal.at("candidate_ir_sha256")},
      {"source_context_sha256", contracts::sha256_json(source)},
      {"degree", degree}, {"groups", frozen}, {"case_count", total},
      {"independence_status", "NOT_ESTABLISHED"},
      {"shared_dependencies", contracts::Json::array({"author", "compiler", "host"})},
      {"limitations", contracts::Json::array({
          "Disjoint inputs are partitions, not proof of independent experiment groups",
          "Expected values use integer recurrence, not candidate coefficients or Horner execution",
          "Reference-source lineage and mathematical correctness still require review",
          "Not a registered qualification protocol, benchmark measurement or acceptance evidence"})},
      {"candidate_evaluated", false}, {"qualification_claim", "NONE"}};
}

} // namespace statewright::egcf
