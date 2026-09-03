#include "statewright/reasoning/certification.hpp"
#include "statewright/reasoning/hypotheses.hpp"
#include "statewright/reasoning/synthesis.hpp"
#include "statewright/reasoning/topology.hpp"
#include "statewright/reasoning/verification.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace {

statewright::reasoning::ReasoningProblem problem() {
  return statewright::reasoning::create_reasoning_problem(
      "Evidence e1 supports the bounded conclusion.",
      "Return the supported conclusion.", "snapshot", "boundary", "dimension",
      {"e1"}, 2'000, 1'000);
}

statewright::reasoning::Hypothesis hypothesis() {
  statewright::reasoning::HypothesisProposal proposal;
  proposal.hypothesis_id = "h1";
  proposal.proposition = "The bounded conclusion is supported";
  proposal.prior_bp = 5'000;
  proposal.posterior_bp = 8'000;
  proposal.supporting_evidence = {"e1"};
  return statewright::reasoning::build_hypothesis_set({proposal}, problem().problem_id, 1)
      .hypotheses.front();
}

statewright::reasoning::ReasoningPath winner() {
  statewright::reasoning::ReasoningStep step;
  step.step_id = "winner:step";
  step.claim = "e1 supports the bounded conclusion";
  step.premises = {"problem", "h1"};
  step.evidence_ids = {"e1"};
  step.inference = "deductive";
  step.confidence_bp = 9'000;
  step.falsifier = "A conflicting declared observation defeats the claim.";
  statewright::reasoning::ReasoningPath result;
  result.path_id = "winner";
  result.perspective = "direct";
  result.hypothesis_ids = {"h1"};
  result.steps = {
      statewright::reasoning::canonicalize_reasoning_step(std::move(step))};
  result.conclusion = "The bounded conclusion is supported.";
  result.estimated_cost_bp = 500;
  result.goal_relevance_bp = 9'000;
  result = statewright::reasoning::canonicalize_reasoning_path(std::move(result));
  result.structure_signature =
      statewright::reasoning::path_structure_signature(result);
  return result;
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
           {{{"step_id", "winner:step"},
             {"checks", checks()},
             {"failures", statewright::contracts::Json::array()}}}},
          {"contradictions", statewright::contracts::Json::array()},
          {"missing_assumptions", statewright::contracts::Json::array()}};
}

statewright::reasoning::VerifierReport winner_verifier() {
  return statewright::reasoning::verify_reasoning_path(
      winner(), {hypothesis()}, {"e1"}, verifier_payload());
}

statewright::reasoning::ReasoningBudget budget() {
  statewright::reasoning::ReasoningBudget value;
  value.maximum_candidates = 2;
  value.candidate_count = 1;
  value.max_generation_attempts = 2;
  value.verifier_count = 1;
  value.falsifier_count = 0;
  value.max_provider_calls = 4;
  value.max_verifier_passes = 1;
  return statewright::reasoning::canonicalize_reasoning_budget(std::move(value));
}

statewright::contracts::Json synthesis_payload() {
  return {{"conclusion", "The bounded conclusion is supported."},
          {"source_path_ids", {"winner"}},
          {"accepted_step_ids", {"winner:step"}},
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

} // namespace

TEST_CASE("verified synthesis requires an independent verifier response") {
  RecordingProvider provider(
      {text_response(synthesis_payload()), text_response(verifier_payload())});
  const auto selected = winner();
  const auto report = winner_verifier();
  const auto result = statewright::reasoning::synthesize_verified_result(
      provider, problem(), {hypothesis()}, selected, {selected}, {report},
      {"e1"}, budget(), true, 1);

  REQUIRE(result.winning_path_id == "winner");
  REQUIRE(result.synthesized_path_id != "winner");
  REQUIRE(result.verified);
  REQUIRE_FALSE(result.fallback_used);
  REQUIRE_FALSE(result.verifier_report_id.empty());
  REQUIRE(result.confidence_bp == 8'500);
  REQUIRE(provider.requests.size() == 2);
  REQUIRE(provider.requests.back().at("instructions").get<std::string>().find(
              "process verifier") != std::string::npos);
}

TEST_CASE("synthesis verification ablation remains explicit and unverified") {
  RecordingProvider provider({text_response(synthesis_payload())});
  const auto selected = winner();
  const auto result = statewright::reasoning::synthesize_verified_result(
      provider, problem(), {hypothesis()}, selected, {selected},
      {winner_verifier()}, {"e1"}, budget(), false, 1);

  REQUIRE_FALSE(result.verified);
  REQUIRE_FALSE(result.fallback_used);
  REQUIRE(result.verifier_report_id.empty());
  REQUIRE(result.failure_reasons ==
          std::vector<std::string>{
              "synthesis verification disabled by qualification ablation"});
  REQUIRE(provider.requests.size() == 1);
}

TEST_CASE("invalid synthesis source binding falls back to verified winner") {
  auto invalid = synthesis_payload();
  invalid["source_path_ids"] = {"unknown"};
  RecordingProvider provider({text_response(invalid)});
  const auto selected = winner();
  const auto report = winner_verifier();
  const auto result = statewright::reasoning::synthesize_verified_result(
      provider, problem(), {hypothesis()}, selected, {selected}, {report},
      {"e1"}, budget(), true, 1);

  REQUIRE(result.synthesized_path_id == "winner");
  REQUIRE(result.merged_conclusion == selected.conclusion);
  REQUIRE(result.verified);
  REQUIRE(result.fallback_used);
  REQUIRE_FALSE(result.failure_reasons.empty());
  REQUIRE(provider.requests.size() == 1);
}

TEST_CASE("conclusion-only synthesis rejects foreign source paths") {
  auto invalid = synthesis_payload();
  invalid["source_path_ids"] = {"unknown"};
  RecordingProvider provider({text_response(invalid)});
  const auto selected = winner();
  const auto result = statewright::reasoning::synthesize_conclusion(
      provider, problem(), selected, {selected}, budget());
  REQUIRE(result.first == selected.conclusion);
  REQUIRE(result.second == std::vector<std::string>{"winner"});
}
