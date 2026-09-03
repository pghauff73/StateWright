#include "statewright/reasoning/certification.hpp"
#include "statewright/reasoning/synthesis.hpp"
#include "statewright/reasoning/verification.hpp"

#include "statewright/common/error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <utility>

namespace {

statewright::contracts::Json load_fixtures() {
  std::ifstream input(STATEWRIGHT_ORACLE_FIXTURES);
  REQUIRE(input.is_open());
  return statewright::contracts::Json::parse(input);
}

statewright::reasoning::ReasoningPath winner_path() {
  statewright::reasoning::ReasoningStep step;
  step.step_id = "winner:step";
  step.claim = "e1 supports the bounded claim";
  step.premises = {"problem", "h1"};
  step.evidence_ids = {"e1"};
  step.inference = "deductive";
  step.confidence_bp = 9'000;
  step.falsifier = "A conflicting declared observation defeats the claim.";
  statewright::reasoning::ReasoningPath path;
  path.path_id = "winner";
  path.perspective = "direct";
  path.hypothesis_ids = {"h1"};
  path.steps = {
      statewright::reasoning::canonicalize_reasoning_step(std::move(step))};
  path.conclusion = "The bounded claim is supported.";
  path.estimated_cost_bp = 500;
  path.goal_relevance_bp = 9'000;
  path = statewright::reasoning::canonicalize_reasoning_path(std::move(path));
  path.structure_signature =
      statewright::reasoning::path_structure_signature(path);
  return path;
}

statewright::reasoning::Hypothesis hypothesis() {
  statewright::reasoning::HypothesisProposal proposal;
  proposal.hypothesis_id = "h1";
  proposal.proposition = "The bounded claim is supported";
  proposal.prior_bp = 5'000;
  proposal.posterior_bp = 8'000;
  proposal.supporting_evidence = {"e1"};
  return statewright::reasoning::build_hypothesis_set({proposal}, "problem", 1)
      .hypotheses.front();
}

statewright::contracts::Json all_checks() {
  statewright::contracts::Json checks = statewright::contracts::Json::object();
  for (const auto &name : statewright::reasoning::required_process_checks()) {
    checks[name] = true;
  }
  return checks;
}

statewright::reasoning::VerifierReport winner_verifier(
    const statewright::reasoning::ReasoningPath &winner) {
  return statewright::reasoning::verify_reasoning_path(
      winner, {hypothesis()}, {"e1"},
      {{"steps",
        {{{"step_id", "winner:step"},
          {"checks", all_checks()},
          {"failures", statewright::contracts::Json::array()}}}},
       {"contradictions", statewright::contracts::Json::array()},
       {"missing_assumptions", statewright::contracts::Json::array()}});
}

statewright::reasoning::ReasoningProblem problem() {
  return statewright::reasoning::create_reasoning_problem(
      "A bounded claim is supported by e1.",
      "Return the supported conclusion.", "snapshot", "boundary", "dimension",
      {"e1"}, 2'000, 1'000);
}

statewright::contracts::Json synthesis_payload() {
  return {{"conclusion", "The bounded claim is supported."},
          {"source_path_ids", {"winner"}},
          {"accepted_step_ids", {"winner:step"}},
          {"rejected_step_ids", statewright::contracts::Json::array()},
          {"remaining_uncertainties", {"Residual uncertainty"}},
          {"confidence_bp", 8'500}};
}

} // namespace

TEST_CASE("provider-independent synthesis validation matches frozen oracle") {
  const auto oracle = load_fixtures().at("reasoning_synthesis_case");
  const auto winner = winner_path();
  const auto validated = statewright::reasoning::validate_synthesis_payload(
      problem(), winner, {winner}, winner_verifier(winner), synthesis_payload(),
      false);

  REQUIRE(statewright::reasoning::to_json(validated.path) ==
          oracle.at("path"));
  REQUIRE(statewright::reasoning::to_json(validated.result) ==
          oracle.at("result"));
}

TEST_CASE("synthesis rejects foreign and contradictory step bindings") {
  const auto winner = winner_path();
  auto foreign = synthesis_payload();
  foreign["accepted_step_ids"] = {"foreign:step"};
  REQUIRE_THROWS_AS(
      statewright::reasoning::validate_synthesis_payload(
          problem(), winner, {winner}, winner_verifier(winner), foreign, false),
      statewright::common::Error);

  auto contradictory = synthesis_payload();
  contradictory["rejected_step_ids"] = {"winner:step"};
  REQUIRE_THROWS_AS(
      statewright::reasoning::validate_synthesis_payload(
          problem(), winner, {winner}, winner_verifier(winner), contradictory,
          false),
      statewright::common::Error);
}

TEST_CASE("conclusion-only synthesis falls back on invalid source binding") {
  const auto winner = winner_path();
  const auto result = statewright::reasoning::validate_synthesized_conclusion(
      winner, {winner},
      {{"conclusion", "Invented"}, {"source_path_ids", {"unknown"}}});
  REQUIRE(result.first == winner.conclusion);
  REQUIRE(result.second == std::vector<std::string>{winner.path_id});
}
