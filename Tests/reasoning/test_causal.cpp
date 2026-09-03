#include "statewright/reasoning/causal.hpp"

#include "statewright/common/error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

namespace {

statewright::contracts::Json load_fixtures() {
  std::ifstream input(STATEWRIGHT_ORACLE_FIXTURES);
  REQUIRE(input.is_open());
  return statewright::contracts::Json::parse(input);
}

} // namespace

TEST_CASE("causal assessment accepts evidenced temporally ordered intervention") {
  statewright::reasoning::CausalEdge edge_value;
  edge_value.edge_id = "edge:temperature-yield";
  edge_value.source_id = "temperature";
  edge_value.target_id = "yield";
  edge_value.relation = "causes";
  edge_value.evidence_ids = {"experiment:1"};
  edge_value.temporal_ordered = true;
  const auto edge =
      statewright::reasoning::make_causal_edge(std::move(edge_value));
  statewright::reasoning::Intervention intervention_value;
  intervention_value.intervention_id = "intervention:temperature";
  intervention_value.variable_id = "temperature";
  intervention_value.assigned_value = "350 K";
  const auto intervention =
      statewright::reasoning::make_intervention(std::move(intervention_value));
  const auto assessment = statewright::reasoning::assess_causal_claim(
      "temperature increases yield", "temperature", "yield", {edge},
      intervention, {}, {}, 8'500);

  REQUIRE(assessment.confidence_bp == 8'500);
  REQUIRE(assessment.blockers.empty());
  REQUIRE(assessment.evidence_ids ==
          std::vector<std::string>{"experiment:1"});
  REQUIRE(assessment.intervention_supported);
  statewright::reasoning::require_causal_assessment_integrity(assessment);
}

TEST_CASE("causal assessment caps correlation and unresolved confounding") {
  statewright::reasoning::CausalEdge edge_value;
  edge_value.edge_id = "edge:temperature-yield";
  edge_value.source_id = "temperature";
  edge_value.target_id = "yield";
  edge_value.relation = "correlates";
  edge_value.evidence_ids = {"observation:1"};
  const auto edge =
      statewright::reasoning::make_causal_edge(std::move(edge_value));
  const auto assessment = statewright::reasoning::assess_causal_claim(
      "temperature increases yield", "temperature", "yield", {edge},
      std::nullopt, {"feedstock"}, {"operator experience"}, 9'000);

  REQUIRE(assessment.confidence_bp == 3'000);
  REQUIRE_FALSE(assessment.intervention_supported);
  REQUIRE(assessment.blockers.size() == 3);
  REQUIRE(assessment.blockers.front() ==
          "alternative explanations remain unresolved: operator experience");
}

TEST_CASE("causal records fail closed on invalid types and tampering") {
  statewright::reasoning::CausalNode invalid_node;
  invalid_node.node_id = "x";
  invalid_node.variable = "x";
  invalid_node.kind = "imaginary";
  REQUIRE_THROWS_AS(
      statewright::reasoning::make_causal_node(std::move(invalid_node)),
      statewright::common::Error);

  statewright::reasoning::Counterfactual counterfactual_value;
  counterfactual_value.counterfactual_id = "cf:1";
  counterfactual_value.intervention_id = "do:x";
  counterfactual_value.outcome_variable_id = "y";
  counterfactual_value.predicted_value = "higher";
  counterfactual_value.assumptions = {"stable mechanism"};
  auto counterfactual = statewright::reasoning::make_counterfactual(
      std::move(counterfactual_value));
  counterfactual.predicted_value = "lower";
  REQUIRE_THROWS_AS(
      statewright::reasoning::require_counterfactual_integrity(counterfactual),
      statewright::common::Error);
}

TEST_CASE("causal assessment matches the frozen oracle") {
  const auto fixture = load_fixtures().at("reasoning_causal_case");
  statewright::reasoning::CausalEdge edge_value;
  edge_value.edge_id = "edge:temperature-yield";
  edge_value.source_id = "temperature";
  edge_value.target_id = "yield";
  edge_value.relation = "causes";
  edge_value.evidence_ids = {"experiment:1"};
  edge_value.temporal_ordered = true;
  const auto edge =
      statewright::reasoning::make_causal_edge(std::move(edge_value));
  statewright::reasoning::Intervention intervention_value;
  intervention_value.intervention_id = "intervention:temperature";
  intervention_value.variable_id = "temperature";
  intervention_value.assigned_value = "350 K";
  const auto intervention =
      statewright::reasoning::make_intervention(std::move(intervention_value));
  const auto assessment = statewright::reasoning::assess_causal_claim(
      "temperature increases yield", "temperature", "yield", {edge},
      intervention, {}, {}, 8'500);

  REQUIRE(statewright::reasoning::to_json(edge) == fixture.at("edge"));
  REQUIRE(statewright::reasoning::to_json(intervention) ==
          fixture.at("intervention"));
  REQUIRE(statewright::reasoning::to_json(assessment) ==
          fixture.at("assessment"));
}
