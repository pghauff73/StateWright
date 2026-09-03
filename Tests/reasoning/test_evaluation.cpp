#include "statewright/common/error.hpp"
#include "statewright/reasoning/evaluation.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {

statewright::contracts::Json load_fixtures() {
  std::ifstream input(STATEWRIGHT_ORACLE_FIXTURES);
  REQUIRE(input.is_open());
  return statewright::contracts::Json::parse(input);
}

statewright::reasoning::ReasoningStep direct_step() {
  statewright::reasoning::ReasoningStep step;
  step.step_id = "p1:step";
  step.claim = "Evidence supports the bounded action";
  step.premises = {"problem", "h1"};
  step.evidence_ids = {"e1"};
  step.inference = "deductive";
  step.confidence_bp = 9'000;
  step.assumptions = {"Bounded input"};
  step.falsifier = "A counterexample defeats the claim.";
  return statewright::reasoning::canonicalize_reasoning_step(std::move(step));
}

statewright::reasoning::ReasoningPath direct_path() {
  statewright::reasoning::ReasoningPath path;
  path.path_id = "p1";
  path.perspective = "direct";
  path.hypothesis_ids = {"h1"};
  path.steps = {direct_step()};
  path.conclusion = "Evidence supports bounded action";
  path.estimated_cost_bp = 500;
  path.goal_relevance_bp = 9'000;
  return statewright::reasoning::canonicalize_reasoning_path(std::move(path));
}

statewright::reasoning::ReasoningPath causal_path() {
  statewright::reasoning::ReasoningStep step;
  step.step_id = "p2:step";
  step.claim = "A causal account supports the action";
  step.premises = {"problem", "h1"};
  step.evidence_ids = {"e1"};
  step.inference = "causal";
  step.confidence_bp = 8'500;
  step.falsifier = "Reverse causality defeats the claim.";
  statewright::reasoning::ReasoningPath path;
  path.path_id = "p2";
  path.perspective = "causal";
  path.hypothesis_ids = {"h1"};
  path.steps = {
      statewright::reasoning::canonicalize_reasoning_step(std::move(step))};
  path.conclusion = "Bounded action follows from the causal account";
  path.estimated_cost_bp = 750;
  path.goal_relevance_bp = 8'500;
  return statewright::reasoning::canonicalize_reasoning_path(std::move(path));
}

statewright::reasoning::VerifierReport accepted_verifier() {
  statewright::reasoning::VerifierReport report;
  report.report_id = "verifier:p1";
  report.path_id = "p1";
  report.step_scores = {{"p1:step", 9'000}};
  report.premise_validity_bp = 9'000;
  report.evidence_support_bp = 9'000;
  report.inference_quality_bp = 9'000;
  report.consistency_bp = 9'500;
  report.completeness_bp = 8'500;
  report.weakest_step_bp = 9'000;
  report.score_bp = 9'000;
  report.verdict = "ACCEPT";
  return statewright::reasoning::canonicalize_verifier_report(std::move(report));
}

statewright::reasoning::FalsifierReport surviving_falsifier() {
  statewright::reasoning::FalsifierReport report;
  report.report_id = "falsifier:p1";
  report.path_id = "p1";
  report.searched_falsifiers = {"counterexample"};
  report.alternative_explanations = {"Alternative cause"};
  report.severity_bp = 6'500;
  report.survival_bp = 8'000;
  report.residual_uncertainty_bp = 1'000;
  report.verdict = "SURVIVES";
  return statewright::reasoning::canonicalize_falsifier_report(std::move(report));
}

} // namespace

TEST_CASE("OIEC-SR evaluation records match frozen oracle fixtures") {
  const auto fixture = load_fixtures().at("reasoning_evaluation_case");
  REQUIRE(statewright::reasoning::to_json(direct_step()) == fixture.at("step"));
  REQUIRE(statewright::reasoning::to_json(direct_path()) == fixture.at("path"));
  REQUIRE(statewright::reasoning::to_json(
              statewright::reasoning::default_score_configuration()) ==
          fixture.at("score_configuration"));
  REQUIRE(statewright::reasoning::to_json(
              statewright::reasoning::default_diversity_configuration()) ==
          fixture.at("diversity_configuration"));
}

TEST_CASE("OIEC-SR diversity measures structure rather than prose order") {
  const auto fixture = load_fixtures().at("reasoning_evaluation_case");
  const auto first = direct_path();
  const auto second = causal_path();
  REQUIRE(statewright::reasoning::path_structure_material(first) ==
          fixture.at("structure_material"));
  REQUIRE(statewright::reasoning::path_structure_signature(first) ==
          fixture.at("structure_signature").get<std::string>());
  const auto diverse =
      statewright::reasoning::bind_diversity_scores({first, second});
  REQUIRE(diverse.size() == 2);
  REQUIRE(statewright::reasoning::to_json(diverse.at(0)) ==
          fixture.at("diverse_paths").at(0));
  REQUIRE(statewright::reasoning::to_json(diverse.at(1)) ==
          fixture.at("diverse_paths").at(1));

  auto prose_variant = first;
  prose_variant.path_id = "p3";
  prose_variant.conclusion = "bounded action supports evidence";
  REQUIRE(statewright::reasoning::is_structural_duplicate(prose_variant,
                                                           {first}));
}

TEST_CASE("OIEC-SR scoring and confidence are deterministic") {
  const auto fixture = load_fixtures().at("reasoning_evaluation_case");
  const auto paths = statewright::reasoning::bind_diversity_scores(
      {direct_path(), causal_path()});
  const auto metrics = statewright::reasoning::score_reasoning_path(
      paths.at(0), accepted_verifier(), surviving_falsifier(), {"e1"});
  REQUIRE(statewright::reasoning::to_json(metrics) == fixture.at("metrics"));

  statewright::reasoning::CandidateSet candidates;
  candidates.problem_id = "problem";
  candidates.paths = paths;
  candidates.verifier_reports = {accepted_verifier()};
  candidates.falsifier_reports = {surviving_falsifier()};
  candidates.metrics = {metrics};
  candidates.selected_path_id = "p1";
  candidates =
      statewright::reasoning::canonicalize_candidate_set(std::move(candidates));
  REQUIRE(statewright::reasoning::conclusion_agreement_bp(candidates) ==
          fixture.at("agreement_bp").get<int>());
  REQUIRE(statewright::reasoning::derive_reasoning_confidence_bp(candidates) ==
          fixture.at("confidence_bp").get<int>());
}

TEST_CASE("OIEC-SR ranking has deterministic evidence tie breakers") {
  const auto first = direct_path();
  auto second = causal_path();
  auto first_metrics = statewright::reasoning::ReasoningMetrics{};
  first_metrics.path_id = first.path_id;
  first_metrics.total_score_bp = 7'000;
  first_metrics = statewright::reasoning::canonicalize_reasoning_metrics(
      std::move(first_metrics));
  auto second_metrics = first_metrics;
  second_metrics.path_id = second.path_id;
  auto first_verifier = accepted_verifier();
  first_verifier.score_bp = 8'000;
  auto second_verifier = first_verifier;
  second_verifier.report_id = "verifier:p2";
  second_verifier.path_id = second.path_id;
  second_verifier.score_bp = 9'000;
  auto first_falsifier = surviving_falsifier();
  auto second_falsifier = first_falsifier;
  second_falsifier.report_id = "falsifier:p2";
  second_falsifier.path_id = second.path_id;
  const auto ranked = statewright::reasoning::rank_reasoning_paths(
      {first, second}, {first_metrics, second_metrics},
      {first_verifier, second_verifier}, {first_falsifier, second_falsifier});
  REQUIRE(ranked.front().path_id == "p2");
}

TEST_CASE("OIEC-SR contradictions preserve identity across resolution") {
  const auto fixture = load_fixtures().at("reasoning_evaluation_case");
  auto verifier = accepted_verifier();
  verifier.contradictions = {"logical conflict"};
  verifier = statewright::reasoning::canonicalize_verifier_report(
      std::move(verifier));
  const auto metrics = statewright::reasoning::score_reasoning_path(
      direct_path(), accepted_verifier(), surviving_falsifier(), {"e1"});
  statewright::reasoning::CandidateSet candidates;
  candidates.paths = {direct_path()};
  candidates.verifier_reports = {verifier};
  candidates.falsifier_reports = {surviving_falsifier()};
  candidates.metrics = {metrics};
  candidates.selected_path_id = "p1";
  const auto records = statewright::reasoning::build_contradiction_records(
      candidates);
  REQUIRE(records.size() == fixture.at("contradictions").size());
  for (std::size_t index = 0; index < records.size(); ++index) {
    REQUIRE(statewright::reasoning::to_json(records.at(index)) ==
            fixture.at("contradictions").at(index));
  }
  const auto resolved = statewright::reasoning::resolve_contradiction(
      records.front(), {"e2"});
  REQUIRE(statewright::reasoning::to_json(resolved) ==
          fixture.at("resolved_contradiction"));
  REQUIRE(resolved.contradiction_id == records.front().contradiction_id);
  REQUIRE(statewright::reasoning::unresolved_critical_contradictions(
              {resolved})
              .empty());
  REQUIRE(statewright::reasoning::cap_confidence_for_contradictions(
              9'000, records) == 4'999);
}

TEST_CASE("OIEC-SR budget derivation is bounded and source-identical") {
  const auto fixture = load_fixtures().at("reasoning_evaluation_case");
  statewright::reasoning::DimensionBudget dimensions;
  dimensions.max_active_relations = 80;
  dimensions.max_active_hypotheses = 8;
  dimensions.max_candidate_actions = 6;
  dimensions.max_decomposition_depth = 5;
  dimensions.max_branch_factor = 4;
  const auto budget = statewright::reasoning::derive_reasoning_budget(
      dimensions, 6'000, 4'000, 2'500, 6, 16);
  REQUIRE(statewright::reasoning::to_json(budget) ==
          fixture.at("derived_budget"));
  REQUIRE(statewright::reasoning::expected_value_of_information_bp(2'000, 750) ==
          fixture.at("voi_bp").get<int>());
  REQUIRE(statewright::reasoning::should_continue_reasoning(budget, 2'000,
                                                            750));
  const auto topology_limits = statewright::reasoning::topology_budget(budget);
  REQUIRE(topology_limits.max_branch_factor == 4);
  REQUIRE(topology_limits.max_topology_nodes == 80);
  REQUIRE(topology_limits.max_topology_edges == 160);
}

TEST_CASE("OIEC-SR evaluation rejects malformed finite state") {
  auto invalid_config = statewright::reasoning::ScoreConfiguration{};
  invalid_config.verifier_weight = 31;
  REQUIRE_THROWS_AS(statewright::reasoning::make_score_configuration(
                        std::move(invalid_config)),
                    statewright::common::Error);
  auto invalid_path = direct_path();
  invalid_path.steps.push_back(invalid_path.steps.front());
  REQUIRE_THROWS_AS(statewright::reasoning::canonicalize_reasoning_path(
                        std::move(invalid_path)),
                    statewright::common::Error);
  statewright::reasoning::DimensionBudget dimensions;
  REQUIRE_THROWS_AS(statewright::reasoning::derive_reasoning_budget(
                        dimensions, 0, 0, 0, 1, 5),
                    statewright::common::Error);
}
