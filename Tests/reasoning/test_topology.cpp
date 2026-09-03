#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/reasoning/topology.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

statewright::contracts::Json load_fixtures() {
  std::ifstream input(STATEWRIGHT_ORACLE_FIXTURES);
  REQUIRE(input.is_open());
  return statewright::contracts::Json::parse(input);
}

statewright::reasoning::ReasoningNode premise() {
  statewright::reasoning::ReasoningNodeOptions options;
  options.validated = true;
  return statewright::reasoning::make_reasoning_node(
      "premise:p", "premise", "Validated input", std::move(options));
}

statewright::reasoning::ReasoningNode material_conclusion(
    bool hypothetical = false) {
  statewright::reasoning::ReasoningNodeOptions options;
  options.path_id = "p";
  options.hypothetical = hypothetical;
  options.material = true;
  return statewright::reasoning::make_reasoning_node(
      "conclusion:p", "conclusion", "Grounded result", std::move(options));
}

void require_policy_error(const std::function<void()> &operation,
                          std::string_view expected) {
  bool thrown = false;
  try {
    operation();
  } catch (const statewright::common::Error &error) {
    thrown = true;
    REQUIRE(std::string_view(error.what()) == expected);
  }
  REQUIRE(thrown);
}

} // namespace

TEST_CASE("OIEC-SR topology identities match frozen oracle fixtures") {
  const auto fixture = load_fixtures().at("reasoning_topology_case");
  const auto first = premise();
  statewright::reasoning::ReasoningNodeOptions options;
  options.confidence_bp = 8'000;
  options.path_id = "p";
  options.material = true;
  const auto second = statewright::reasoning::make_reasoning_node(
      "conclusion:p", "conclusion", "Grounded result", std::move(options));
  const auto edge = statewright::reasoning::make_reasoning_edge(
      "premise:p", "conclusion:p", "entails", "formal");
  const auto topology = statewright::reasoning::make_reasoning_topology(
      "problem", {second, first}, {edge});

  REQUIRE(statewright::reasoning::to_json(first) == fixture.at("nodes").at(0));
  REQUIRE(statewright::reasoning::to_json(second) == fixture.at("nodes").at(1));
  REQUIRE(statewright::reasoning::to_json(edge) == fixture.at("edge"));
  REQUIRE(edge.inference_id == fixture.at("inference_id").get<std::string>());
  REQUIRE(statewright::reasoning::reasoning_topology_payload(topology) ==
          fixture.at("payload"));
  REQUIRE(topology.signature == fixture.at("signature").get<std::string>());
}

TEST_CASE("OIEC-SR topology rejects positive cycles") {
  const auto topology = statewright::reasoning::make_reasoning_topology(
      "", {statewright::reasoning::make_reasoning_node("a", "claim", "A"),
           statewright::reasoning::make_reasoning_node("b", "claim", "B")},
      {statewright::reasoning::make_reasoning_edge("a", "b", "supports",
                                                  "deductive"),
       statewright::reasoning::make_reasoning_edge("b", "a", "entails",
                                                  "deductive")});
  require_policy_error(
      [&] { statewright::reasoning::validate_reasoning_topology(topology, {}, {}); },
      "positive reasoning topology contains a cycle");
}

TEST_CASE("OIEC-SR topology enforces the evidence universe") {
  statewright::reasoning::ReasoningNodeOptions options;
  options.evidence_ids = {"e2"};
  const auto evidence = statewright::reasoning::make_reasoning_node(
      "evidence:e2", "evidence", "Unknown evidence", std::move(options));
  const auto hypothesis = statewright::reasoning::make_reasoning_node(
      "hypothesis:h1", "hypothesis", "H1");
  const auto topology = statewright::reasoning::make_reasoning_topology(
      "", {evidence, hypothesis},
      {statewright::reasoning::make_reasoning_edge(
          evidence.node_id, hypothesis.node_id, "supports", "inductive")});
  require_policy_error(
      [&] {
        statewright::reasoning::validate_reasoning_topology(topology, {}, {"e1"});
      },
      "reasoning topology references undeclared evidence");
}

TEST_CASE("OIEC-SR material conclusions require positive grounding") {
  const auto grounded = statewright::reasoning::make_reasoning_topology(
      "", {premise(), material_conclusion()},
      {statewright::reasoning::make_reasoning_edge(
          "premise:p", "conclusion:p", "entails", "deductive")});
  REQUIRE_NOTHROW(
      statewright::reasoning::validate_reasoning_topology(grounded, {}, {}));

  statewright::reasoning::ReasoningNodeOptions options;
  options.evidence_ids = {"e1"};
  const auto evidence = statewright::reasoning::make_reasoning_node(
      "evidence:e1", "evidence", "Observed conflict", std::move(options));
  const auto attacked = statewright::reasoning::make_reasoning_topology(
      "", {evidence, material_conclusion()},
      {statewright::reasoning::make_reasoning_edge(
          "evidence:e1", "conclusion:p", "contradicts", "defeasible")});
  require_policy_error(
      [&] {
        statewright::reasoning::validate_reasoning_topology(attacked, {}, {"e1"});
      },
      "material reasoning conclusion lacks a grounding trace");
}

TEST_CASE("OIEC-SR assumption-only material stays hypothetical") {
  const auto assumption = statewright::reasoning::make_reasoning_node(
      "assumption:a", "assumption", "Assume A");
  const auto edge = statewright::reasoning::make_reasoning_edge(
      "assumption:a", "conclusion:p", "requires", "constraint");
  const auto asserted = statewright::reasoning::make_reasoning_topology(
      "", {assumption, material_conclusion()}, {edge});
  require_policy_error(
      [&] { statewright::reasoning::validate_reasoning_topology(asserted, {}, {}); },
      "assumption-only conclusion must remain hypothetical");

  const auto hypothetical = statewright::reasoning::make_reasoning_topology(
      "", {assumption, material_conclusion(true)}, {edge});
  REQUIRE_NOTHROW(statewright::reasoning::validate_reasoning_topology(
      hypothetical, {}, {}));
}

TEST_CASE("OIEC-SR counterexamples bind to attacked reasoning") {
  const auto hypothesis = statewright::reasoning::make_reasoning_node(
      "hypothesis:h1", "hypothesis", "H1");
  const auto counterexample = statewright::reasoning::make_reasoning_node(
      "counterexample:c1", "counterexample", "Not H1");
  const auto topology = statewright::reasoning::make_reasoning_topology(
      "", {hypothesis, counterexample},
      {statewright::reasoning::make_reasoning_edge(
          counterexample.node_id, hypothesis.node_id, "falsifies",
          "defeasible")});
  REQUIRE_NOTHROW(
      statewright::reasoning::validate_reasoning_topology(topology, {}, {}));
}

TEST_CASE("OIEC-SR topology rejects unconnected reasoning branches") {
  const auto topology = statewright::reasoning::make_reasoning_topology(
      "", {premise(), material_conclusion(),
           statewright::reasoning::make_reasoning_node(
               "claim:orphan", "claim", "Disconnected claim")},
      {statewright::reasoning::make_reasoning_edge(
          "premise:p", "conclusion:p", "entails", "deductive")});
  require_policy_error(
      [&] { statewright::reasoning::validate_reasoning_topology(topology, {}, {}); },
      "reasoning topology contains an unconnected branch");
}

TEST_CASE("OIEC-SR topology signatures ignore input ordering") {
  const auto edge = statewright::reasoning::make_reasoning_edge(
      "premise:p", "conclusion:p", "entails", "deductive");
  const auto first = statewright::reasoning::make_reasoning_topology(
      "p", {premise(), material_conclusion()}, {edge});
  const auto second = statewright::reasoning::make_reasoning_topology(
      "p", {material_conclusion(), premise()}, {edge});
  REQUIRE(first.signature == second.signature);
  REQUIRE(statewright::reasoning::reasoning_topology_payload(first) ==
          statewright::reasoning::reasoning_topology_payload(second));
}

TEST_CASE("OIEC-SR topology verifies inference content addresses") {
  const auto edge = statewright::reasoning::make_reasoning_edge(
      "a", "b", "supports", "inductive");
  REQUIRE(edge.inference_id == statewright::reasoning::inference_identity(
                                   "a", "b", "supports", "inductive"));
  auto forged = edge;
  forged.inference_id = "inference:forged";
  const auto topology = statewright::reasoning::make_reasoning_topology(
      "", {statewright::reasoning::make_reasoning_node("a", "claim", "A"),
           statewright::reasoning::make_reasoning_node(
               "b", "hypothesis", "B")},
      {forged});
  require_policy_error(
      [&] { statewright::reasoning::validate_reasoning_topology(topology, {}, {}); },
      "reasoning topology inference identity mismatch");
}

TEST_CASE("OIEC-SR topology applies branch and size budgets") {
  const auto hypothesis = statewright::reasoning::make_reasoning_node(
      "hypothesis:h", "hypothesis", "H");
  const auto first = statewright::reasoning::make_reasoning_node(
      "claim:a", "claim", "A");
  const auto second = statewright::reasoning::make_reasoning_node(
      "claim:b", "claim", "B");
  const auto topology = statewright::reasoning::make_reasoning_topology(
      "", {hypothesis, first, second},
      {statewright::reasoning::make_reasoning_edge(
           hypothesis.node_id, first.node_id, "predicts", "deductive"),
       statewright::reasoning::make_reasoning_edge(
           hypothesis.node_id, second.node_id, "predicts", "deductive")});
  auto branch_budget = statewright::reasoning::TopologyBudget{};
  branch_budget.max_branch_factor = 1;
  require_policy_error(
      [&] {
        statewright::reasoning::validate_reasoning_topology(topology,
                                                             branch_budget, {});
      },
      "reasoning topology branch factor exceeded");
  auto size_budget = statewright::reasoning::TopologyBudget{};
  size_budget.max_topology_nodes = 2;
  require_policy_error(
      [&] {
        statewright::reasoning::validate_reasoning_topology(topology,
                                                             size_budget, {});
      },
      "reasoning topology node budget exceeded");
}
