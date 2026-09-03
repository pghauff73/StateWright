#include "statewright/reasoning/certification.hpp"
#include "statewright/reasoning/search.hpp"
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

struct SearchFixture final {
  statewright::reasoning::ReasoningProblem problem;
  statewright::reasoning::ReasoningPath path;
  statewright::reasoning::VerifierReport verifier;
  statewright::reasoning::FalsifierReport falsifier;
  statewright::reasoning::SynthesisResult synthesis;
};

SearchFixture search_fixture() {
  SearchFixture fixture;
  fixture.problem = statewright::reasoning::create_reasoning_problem(
      "A bounded claim is supported by e1.",
      "Return the supported conclusion.", "snapshot", "boundary", "dimension",
      {"e1"}, 2'000, 1'000);
  fixture.path = winner_path();
  fixture.verifier = statewright::reasoning::verify_reasoning_path(
      fixture.path, {hypothesis()}, {"e1"},
      {{"steps",
        {{{"step_id", "winner:step"},
          {"checks", all_checks()},
          {"failures", statewright::contracts::Json::array()}}}},
       {"contradictions", statewright::contracts::Json::array()},
       {"missing_assumptions", statewright::contracts::Json::array()}});
  fixture.falsifier = statewright::reasoning::falsify_reasoning_path(
      fixture.path,
      {{"searched_falsifiers", {"declared conflict"}},
       {"survival_bp", 9'000}},
      std::vector<std::string>{"e1"});
  fixture.synthesis = statewright::reasoning::fallback_to_verified_winner(
      fixture.path, fixture.verifier);
  return fixture;
}

} // namespace

TEST_CASE("reasoning search bypass reports match the frozen oracle") {
  const auto oracle = load_fixtures().at("reasoning_search_case");
  const auto path = winner_path();
  REQUIRE(statewright::reasoning::to_json(
              statewright::reasoning::make_rejected_falsifier(path)) ==
          oracle.at("rejected_falsifier"));
  REQUIRE(statewright::reasoning::to_json(
              statewright::reasoning::make_bypass_verifier(path, {"e1"})) ==
          oracle.at("bypass_verifier"));
  REQUIRE(statewright::reasoning::to_json(
              statewright::reasoning::make_bypass_falsifier(path)) ==
          oracle.at("bypass_falsifier"));
}

TEST_CASE("standard ablation profiles preserve frozen order and signatures") {
  const auto oracle = load_fixtures().at("reasoning_search_case");
  const auto configurations =
      statewright::reasoning::standard_ablation_configurations();
  statewright::contracts::Json payload = statewright::contracts::Json::array();
  statewright::contracts::Json pipelines = statewright::contracts::Json::array();
  for (const auto &configuration : configurations) {
    payload.push_back(statewright::reasoning::to_json(configuration));
    pipelines.push_back(
        statewright::reasoning::ablation_pipeline(configuration));
  }
  REQUIRE(payload == oracle.at("standard_ablations"));
  REQUIRE(pipelines == oracle.at("ablation_pipelines"));
}

TEST_CASE("deterministic candidate assembly matches the frozen oracle") {
  const auto oracle = load_fixtures().at("reasoning_search_case");
  const auto fixture = search_fixture();
  const auto candidates = statewright::reasoning::assemble_reasoning_candidates(
      fixture.problem, {fixture.path}, {fixture.verifier}, {fixture.falsifier},
      {"e1"}, statewright::reasoning::make_ablation_configuration(),
      fixture.synthesis);
  REQUIRE(statewright::reasoning::to_json(candidates) ==
          oracle.at("assembled_candidate_set"));
}

TEST_CASE("verifier ablation cannot admit undeclared evidence") {
  const auto path = winner_path();
  REQUIRE_THROWS_AS(statewright::reasoning::make_bypass_verifier(path, {}),
                    statewright::common::Error);
}
