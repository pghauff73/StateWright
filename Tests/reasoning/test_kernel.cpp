#include "statewright/reasoning/generator.hpp"
#include "statewright/reasoning/kernel.hpp"
#include "statewright/reasoning/verification.hpp"

#include "statewright/common/error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace {

statewright::reasoning::ReasoningProblem problem() {
  return statewright::reasoning::SuperReasoningKernel::create_problem(
      "Evidence e1 supports the bounded conclusion.",
      "Return the supported conclusion.", "snapshot", "boundary", "dimension",
      {"e1"}, 2'000, 1'000);
}

statewright::reasoning::HypothesisSet hypothesis_state() {
  statewright::reasoning::HypothesisProposal proposal;
  proposal.hypothesis_id = "h1";
  proposal.proposition = "The bounded conclusion is supported";
  proposal.prior_bp = 5'000;
  proposal.posterior_bp = 8'000;
  proposal.supporting_evidence = {"e1"};
  return statewright::reasoning::SuperReasoningKernel::build_hypothesis_state(
      {proposal}, problem().problem_id, 1);
}

statewright::contracts::Json candidate_payload() {
  return {{"conclusion", "The bounded conclusion is supported."},
          {"hypothesis_ids", {"h1"}},
          {"provider_confidence_bp", 10'000},
          {"estimated_cost_bp", 500},
          {"goal_relevance_bp", 9'000},
          {"risk_bp", 500},
          {"steps",
           {{{"step_id", "candidate:step"},
             {"claim", "Evidence supports the bounded conclusion"},
             {"premises", {"problem", "h1"}},
             {"evidence_ids", {"e1"}},
             {"inference", "deductive"},
             {"confidence_bp", 10'000},
             {"assumptions", statewright::contracts::Json::array()},
             {"falsifier", "A declared contradiction defeats the claim."}}}}};
}

statewright::contracts::Json checks() {
  auto result = statewright::contracts::Json::object();
  for (const auto &name : statewright::reasoning::required_process_checks()) {
    result[name] = true;
  }
  return result;
}

statewright::contracts::Json verifier_payload() {
  return {{"steps",
           {{{"step_id", "candidate:step"},
             {"checks", checks()},
             {"failures", statewright::contracts::Json::array()}}}},
          {"contradictions", statewright::contracts::Json::array()},
          {"missing_assumptions", statewright::contracts::Json::array()}};
}

statewright::contracts::Json falsifier_payload() {
  return {{"searched_falsifiers", {"declared contradiction"}},
          {"survival_bp", 9'000}};
}

statewright::contracts::Json synthesis_payload(const std::string &path_id) {
  return {{"conclusion", "The bounded conclusion is supported."},
          {"source_path_ids", {path_id}},
          {"accepted_step_ids", {"candidate:step"}},
          {"rejected_step_ids", statewright::contracts::Json::array()},
          {"remaining_uncertainties", statewright::contracts::Json::array()},
          {"confidence_bp", 8'500}};
}

statewright::contracts::Json text_response(const statewright::contracts::Json &payload) {
  return {{"output_text", statewright::contracts::canonical_json(payload)}};
}

class RecordingProvider final : public statewright::providers::ReasoningProvider {
public:
  explicit RecordingProvider(
      std::vector<statewright::contracts::Json> supplied_responses)
      : responses(std::move(supplied_responses)) {}

  [[nodiscard]] statewright::contracts::Json create_response(
      const statewright::contracts::Json &request) override {
    requests.push_back(request);
    return responses.at(next_response++);
  }

  std::vector<statewright::contracts::Json> responses;
  std::vector<statewright::contracts::Json> requests;
  std::size_t next_response = 0;
};

statewright::reasoning::SuperReasoningKernel kernel() {
  statewright::reasoning::SuperReasoningKernelOptions options;
  options.max_candidates = 1;
  options.max_provider_calls = 8;
  return statewright::reasoning::SuperReasoningKernel(std::move(options));
}

statewright::reasoning::DimensionBudget dimensions() {
  statewright::reasoning::DimensionBudget value;
  value.max_candidate_actions = 1;
  return value;
}

} // namespace

TEST_CASE("SuperReasoningKernel derives the frozen bounded budget") {
  statewright::reasoning::SuperReasoningKernelOptions options;
  options.max_candidates = 4;
  options.max_provider_calls = 16;
  statewright::reasoning::SuperReasoningKernel test_kernel(std::move(options));
  statewright::reasoning::DimensionBudget dimension_budget;
  dimension_budget.max_candidate_actions = 4;
  const auto derived = test_kernel.derive_budget(dimension_budget, problem());
  REQUIRE(derived.maximum_candidates == 4);
  REQUIRE(derived.candidate_count == 2);
  REQUIRE(derived.verifier_count == 2);
  REQUIRE(derived.falsifier_count == 2);
  REQUIRE_FALSE(derived.signature.empty());
}

TEST_CASE("SuperReasoningKernel completes a verified bounded run") {
  const auto test_kernel = kernel();
  const auto test_budget = test_kernel.derive_budget(dimensions(), problem());
  const auto proposed = candidate_payload();
  const auto expected_path = statewright::reasoning::parse_reasoning_path(
      proposed, problem(), hypothesis_state().hypotheses, "direct", test_budget);
  RecordingProvider provider(
      {text_response(proposed), text_response(verifier_payload()),
       text_response(falsifier_payload()),
       text_response(synthesis_payload(expected_path.path_id)),
       text_response(verifier_payload())});

  const auto result = test_kernel.run(provider, problem(), hypothesis_state(),
                                      dimensions(), {"e1"});
  REQUIRE(result.candidates.selected_path_id == expected_path.path_id);
  REQUIRE(result.candidates.synthesis.has_value());
  REQUIRE(result.candidates.synthesis->verified);
  REQUIRE(result.certificate.decision == "ACCEPT");
  REQUIRE(result.certificate.terminal_state == "SOLUTION");
  REQUIRE(result.hypotheses.size() == 1);
  REQUIRE(result.hypotheses.front().status == "SUPPORTED");
  REQUIRE(provider.requests.size() == 5);
  statewright::reasoning::require_problem_integrity(problem());
  statewright::reasoning::require_candidate_integrity(result.candidates);
  statewright::reasoning::require_reasoning_topology_integrity(result.topology);
  statewright::reasoning::require_reasoning_certificate_integrity(
      result.certificate);
}

TEST_CASE("SuperReasoningKernel rejects unavailable problem evidence") {
  RecordingProvider provider({});
  REQUIRE_THROWS_AS(
      kernel().run(provider, problem(), hypothesis_state(), dimensions(), {}),
      statewright::common::Error);
  REQUIRE(provider.requests.empty());
}
