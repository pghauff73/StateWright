#include "statewright/reasoning/certification.hpp"
#include "statewright/reasoning/context.hpp"

#include "statewright/common/error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>
#include <utility>

namespace {

statewright::contracts::Json load_fixtures() {
  std::ifstream input(STATEWRIGHT_ORACLE_FIXTURES);
  REQUIRE(input.is_open());
  return statewright::contracts::Json::parse(input);
}

statewright::reasoning::ReasoningProblem problem() {
  return statewright::reasoning::create_reasoning_problem(
      "A bounded claim is supported by e1.",
      "Return the supported conclusion.", "snapshot", "boundary", "dimension",
      {"e1"}, 2'000, 1'000);
}

statewright::reasoning::Hypothesis context_hypothesis() {
  statewright::reasoning::HypothesisProposal proposal;
  proposal.hypothesis_id = "h-context";
  proposal.proposition = "Calibration should be confirmed";
  proposal.prior_bp = 5'000;
  proposal.posterior_bp = 5'000;
  proposal.assumptions = {"  confirm   calibration  "};
  return statewright::reasoning::build_hypothesis_set({proposal}, "context", 1)
      .hypotheses.front();
}

statewright::reasoning::ReasoningBudget context_budget() {
  statewright::reasoning::ReasoningBudget budget;
  budget.maximum_candidates = 1;
  budget.candidate_count = 1;
  budget.verifier_count = 1;
  budget.falsifier_count = 0;
  budget.max_context_items = 4;
  budget.operation_costs_bp = {{"REFINE_DIMENSION", 0}};
  return statewright::reasoning::canonicalize_reasoning_budget(
      std::move(budget));
}

} // namespace

TEST_CASE("reasoning context projection matches the frozen oracle") {
  const auto fixture = load_fixtures().at("reasoning_context_case");
  const auto hypothesis = context_hypothesis();
  const auto context = statewright::reasoning::project_reasoning_context(
      problem(), {hypothesis}, context_budget(), std::nullopt, std::nullopt,
      {"collision:z", "collision:a"},
      std::vector<std::string>{"e2", "e1", "e2"});
  const auto operation = statewright::reasoning::choose_reasoning_operation(
      context_budget(), {{"REFINE_DIMENSION", 1'000}});

  REQUIRE(statewright::reasoning::to_json(hypothesis) ==
          fixture.at("hypothesis"));
  REQUIRE(statewright::reasoning::to_json(context) == fixture.at("context"));
  REQUIRE(statewright::reasoning::to_json(operation) ==
          fixture.at("operation"));
}

TEST_CASE("reasoning context is bounded and excludes raw history") {
  auto budget = context_budget();
  budget.max_context_items = 3;
  const auto context = statewright::reasoning::project_reasoning_context(
      problem(), {context_hypothesis()}, budget, std::nullopt, std::nullopt,
      {"collision:a", "collision:b"},
      std::vector<std::string>{"e1", "e2"});
  REQUIRE(context.hypothesis_ids.size() <= 3);
  REQUIRE(context.evidence_ids.size() <= 3);
  REQUIRE(context.collision_ids.size() <= 3);
  const auto payload = statewright::reasoning::to_json(context);
  REQUIRE_FALSE(payload.contains("conversation"));
  REQUIRE_FALSE(payload.contains("history"));
}

TEST_CASE("reasoning context integrity fails closed") {
  auto context = statewright::reasoning::project_reasoning_context(
      problem(), {context_hypothesis()}, context_budget());
  context.problem_hash = "forged";
  REQUIRE_THROWS_AS(
      statewright::reasoning::require_reasoning_context_integrity(context),
      statewright::common::Error);
}

TEST_CASE("reasoning operation preserves IURM and read-only gates") {
  const auto choice = statewright::reasoning::choose_reasoning_operation(
      context_budget(), {{"REFINE_DIMENSION", 1'000}});
  REQUIRE(choice.operation == "REFINE_DIMENSION");
  REQUIRE(choice.requires_iurm);
  REQUIRE(choice.read_only);
  statewright::reasoning::require_reasoning_operation_integrity(choice);
}
