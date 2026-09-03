#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/reasoning/certification.hpp"
#include "statewright/reasoning/verification.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <fstream>
#include <string>
#include <utility>

namespace {

statewright::contracts::Json load_fixtures() {
  std::ifstream input(STATEWRIGHT_ORACLE_FIXTURES);
  REQUIRE(input.is_open());
  return statewright::contracts::Json::parse(input);
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

statewright::reasoning::ReasoningPath winning_path() {
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

statewright::contracts::Json all_checks() {
  statewright::contracts::Json checks = statewright::contracts::Json::object();
  for (const auto &name : statewright::reasoning::required_process_checks()) {
    checks[name] = true;
  }
  return checks;
}

struct CertificationFixture final {
  statewright::reasoning::ReasoningProblem problem;
  statewright::reasoning::Hypothesis hypothesis;
  statewright::reasoning::ReasoningPath path;
  statewright::reasoning::VerifierReport verifier;
  statewright::reasoning::FalsifierReport falsifier;
  statewright::reasoning::ReasoningMetrics metrics;
  statewright::reasoning::SynthesisResult synthesis;
  statewright::reasoning::CandidateSet candidates;
  statewright::reasoning::ReasoningTopology topology;
  statewright::reasoning::ReasoningBudget budget;
};

CertificationFixture make_fixture() {
  CertificationFixture fixture;
  fixture.problem = statewright::reasoning::create_reasoning_problem(
      "A bounded claim is supported by e1.",
      "Return the supported conclusion.", "snapshot", "boundary", "dimension",
      {"e1"}, 2'000, 1'000);
  fixture.hypothesis = hypothesis();
  fixture.path = winning_path();
  fixture.verifier = statewright::reasoning::verify_reasoning_path(
      fixture.path, {fixture.hypothesis}, {"e1"},
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
  fixture.metrics = statewright::reasoning::score_reasoning_path(
      fixture.path, fixture.verifier, fixture.falsifier, {"e1"});
  fixture.synthesis = statewright::reasoning::fallback_to_verified_winner(
      fixture.path, fixture.verifier);
  fixture.candidates.problem_id = fixture.problem.problem_id;
  fixture.candidates.paths = {fixture.path};
  fixture.candidates.verifier_reports = {fixture.verifier};
  fixture.candidates.falsifier_reports = {fixture.falsifier};
  fixture.candidates.metrics = {fixture.metrics};
  fixture.candidates.selected_path_id = "winner";
  fixture.candidates.surviving_path_ids = {"winner"};
  fixture.candidates.synthesis = fixture.synthesis;
  fixture.candidates.score_config_id =
      statewright::reasoning::default_score_configuration().config_id;
  fixture.candidates.score_config_hash =
      statewright::reasoning::default_score_configuration().signature;
  fixture.candidates.diversity_config_hash =
      statewright::contracts::sha256_json(
          {{"configuration",
            statewright::reasoning::default_diversity_configuration().signature},
           {"filter_enabled", true}});
  const auto ablation =
      statewright::reasoning::make_ablation_configuration();
  fixture.candidates.ablation_id = ablation.ablation_id;
  fixture.candidates.ablation_config_hash = ablation.signature;
  fixture.candidates =
      statewright::reasoning::sign_candidate_set(std::move(fixture.candidates));
  const auto premise = statewright::reasoning::make_reasoning_node(
      "premise:certificate", "premise", fixture.problem.statement,
      [] {
        statewright::reasoning::ReasoningNodeOptions options;
        options.validated = true;
        return options;
      }());
  statewright::reasoning::ReasoningNodeOptions conclusion_options;
  conclusion_options.path_id = "winner";
  conclusion_options.material = true;
  const auto conclusion = statewright::reasoning::make_reasoning_node(
      "conclusion:winner", "conclusion", fixture.path.conclusion,
      std::move(conclusion_options));
  fixture.topology = statewright::reasoning::make_reasoning_topology(
      fixture.problem.problem_id, {premise, conclusion},
      {statewright::reasoning::make_reasoning_edge(
          premise.node_id, conclusion.node_id, "entails", "deductive")});
  statewright::reasoning::DimensionBudget dimensions;
  dimensions.max_candidate_actions = 4;
  fixture.budget = statewright::reasoning::derive_reasoning_budget(
      dimensions, fixture.problem.uncertainty_bp, fixture.problem.difficulty_bp,
      0, 4, 16);
  return fixture;
}

} // namespace

TEST_CASE("OIEC-SR certification matches frozen oracle identities") {
  const auto oracle = load_fixtures().at("reasoning_certification_case");
  const auto fixture = make_fixture();
  REQUIRE(statewright::reasoning::to_json(fixture.problem) ==
          oracle.at("problem"));
  REQUIRE(statewright::reasoning::to_json(
              statewright::reasoning::make_ablation_configuration()) ==
          oracle.at("ablation"));
  REQUIRE(statewright::reasoning::to_json(fixture.synthesis) ==
          oracle.at("synthesis"));
  REQUIRE(statewright::reasoning::to_json(fixture.candidates) ==
          oracle.at("candidate_set"));
  REQUIRE(statewright::reasoning::to_json(fixture.topology) ==
          oracle.at("topology"));
  REQUIRE(statewright::reasoning::to_json(fixture.budget) ==
          oracle.at("budget"));
  REQUIRE(statewright::reasoning::to_json(fixture.hypothesis) ==
          oracle.at("hypothesis"));

  const auto certificate = statewright::reasoning::certify_reasoning(
      fixture.problem, {fixture.hypothesis}, fixture.budget,
      fixture.candidates, fixture.topology);
  REQUIRE(statewright::reasoning::to_json(certificate) ==
          oracle.at("certificate"));
  REQUIRE(certificate.decision == "ACCEPT");
  REQUIRE(certificate.terminal_state == "SOLUTION");
  REQUIRE_NOTHROW(
      statewright::reasoning::require_reasoning_certificate_integrity(
          certificate));
}

TEST_CASE("OIEC-SR topology assembly matches the frozen oracle") {
  const auto fixture = make_fixture();
  const auto topology = statewright::reasoning::build_reasoning_topology(
      fixture.problem, {fixture.hypothesis}, fixture.candidates);
  REQUIRE(statewright::reasoning::to_json(topology) ==
          load_fixtures()
              .at("reasoning_certification_case")
              .at("assembled_topology"));
  statewright::reasoning::TopologyBudget topology_budget;
  topology_budget.max_topology_nodes =
      static_cast<std::size_t>(fixture.budget.max_topology_nodes);
  topology_budget.max_topology_edges =
      static_cast<std::size_t>(fixture.budget.max_topology_edges);
  topology_budget.max_branch_factor =
      static_cast<std::size_t>(fixture.budget.max_branch_factor);
  REQUIRE_NOTHROW(statewright::reasoning::validate_reasoning_topology(
      topology, topology_budget, fixture.problem.evidence_ids));
}

TEST_CASE("OIEC-SR operation choice is deterministic and signed") {
  const auto oracle = load_fixtures().at("reasoning_certification_case");
  const auto fixture = make_fixture();
  const auto choice = statewright::reasoning::choose_reasoning_operation(
      fixture.budget,
      {{"VERIFY_AGAIN", 2'000}, {"REFINE_DIMENSION", 2'000}});
  REQUIRE(statewright::reasoning::to_json(choice) ==
          oracle.at("operation_choice"));
  const auto repeated = statewright::reasoning::choose_reasoning_operation(
      fixture.budget,
      {{"REFINE_DIMENSION", 2'000}, {"VERIFY_AGAIN", 2'000}});
  REQUIRE(repeated.signature == choice.signature);
}

TEST_CASE("OIEC-SR certificate integrity fails closed on tampering") {
  auto fixture = make_fixture();
  auto certificate = statewright::reasoning::certify_reasoning(
      fixture.problem, {fixture.hypothesis}, fixture.budget,
      fixture.candidates, fixture.topology);
  certificate.derived_confidence_bp -= 1;
  REQUIRE_THROWS_AS(
      statewright::reasoning::require_reasoning_certificate_integrity(
          certificate),
      statewright::common::Error);

  fixture.candidates.score_config_hash = "forged";
  REQUIRE_THROWS_AS(statewright::reasoning::certify_reasoning(
                        fixture.problem, {fixture.hypothesis}, fixture.budget,
                        fixture.candidates, fixture.topology),
                    statewright::common::Error);
}

TEST_CASE("OIEC-SR repeated acceptance requires measurable progress") {
  const auto fixture = make_fixture();
  const auto first = statewright::reasoning::certify_reasoning(
      fixture.problem, {fixture.hypothesis}, fixture.budget,
      fixture.candidates, fixture.topology);
  const auto repeated = statewright::reasoning::certify_reasoning(
      fixture.problem, {fixture.hypothesis}, fixture.budget,
      fixture.candidates, fixture.topology, {}, first);
  REQUIRE(repeated.decision == "STOP_NO_VALUE");
  REQUIRE(repeated.terminal_state == "EPISTEMIC_STOP");
  REQUIRE(repeated.reasons ==
          std::vector<std::string>{"no_reasoning_progress"});
}
