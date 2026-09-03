#include "statewright/saa/reasoning_algebra.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

statewright::saa::ReasoningAlgorithmSpec reasoning_base_spec(
    bool renamed = false,
    std::string output_meaning = "qualified conclusion",
    std::string description = "first") {
  using namespace statewright::saa;
  const std::array<std::string, 3> ids =
      renamed ? std::array<std::string, 3>{"n3", "n1", "n2"}
              : std::array<std::string, 3>{"observe", "test", "verify"};
  std::vector<ReasoningNodeSpec> nodes = {
      {.node_id = ids[0],
       .operator_name = "OBSERVE",
       .semantic_inputs = {"problem evidence"},
       .semantic_outputs = {"candidate evidence"},
       .public_claim_ids = {},
       .evidence_requirements = {"source snapshot"},
       .assumptions = {},
       .falsifiers = {},
       .description = std::move(description)},
      {.node_id = ids[1],
       .operator_name = "FALSIFY",
       .semantic_inputs = {"candidate evidence"},
       .semantic_outputs = {"surviving claim"},
       .public_claim_ids = {},
       .evidence_requirements = {},
       .assumptions = {},
       .falsifiers = {"counterexample exists"},
       .description = ""},
      {.node_id = ids[2],
       .operator_name = "VERIFY",
       .semantic_inputs = {"surviving claim"},
       .semantic_outputs = {output_meaning},
       .public_claim_ids = {},
       .evidence_requirements = {"independent verification"},
       .assumptions = {},
       .falsifiers = {},
       .description = ""}};
  if (renamed) {
    std::reverse(nodes.begin(), nodes.end());
  }
  return {.name = "falsification-first",
          .inputs = {"problem evidence"},
          .outputs = {std::move(output_meaning)},
          .nodes = std::move(nodes),
          .edges = {{ids[0], ids[1], "NEXT", ""},
                    {ids[1], ids[2], "NEXT", ""}},
          .invariants = {
              "unverified claims do not become facts without evidence"},
          .termination = {"bounded evidence decision",
                          "claim verified or falsified or budget exhausted", 8},
          .applicability = {"evidence-backed factual reasoning"}};
}

} // namespace

TEST_CASE("SAA canonical reasoning ignores names order and display prose") {
  const auto left = statewright::saa::canonicalize_reasoning_algorithm(
      reasoning_base_spec(false, "qualified conclusion", "visible one"));
  const auto right = statewright::saa::canonicalize_reasoning_algorithm(
      reasoning_base_spec(true, "qualified conclusion", "different prose"));
  REQUIRE(left.canonicalization_strength ==
          "EXACT_BOUNDED_GRAPH_CANONICALIZATION");
  REQUIRE(left.canonical_reasoning_signature ==
          "3646a849a19655a002a86a33c0c11b04b961ddc76d3af2bc2959963f9db676c7");
  REQUIRE(left.canonical_reasoning_signature ==
          right.canonical_reasoning_signature);
  REQUIRE(left.topology_signature ==
          "a5e93018536e189986cbd5d6a7ee1a951694106964bdef8a8bb5745e9f2c0934");
  REQUIRE(left.canonical_permutations_evaluated == 2U);
  REQUIRE(left.public_artifact_only);
}

TEST_CASE("SAA canonical reasoning semantics remain identity bearing") {
  const auto left = statewright::saa::canonicalize_reasoning_algorithm(
      reasoning_base_spec());
  const auto right = statewright::saa::canonicalize_reasoning_algorithm(
      reasoning_base_spec(false, "marketing recommendation"));
  REQUIRE(left.topology_signature == right.topology_signature);
  REQUIRE(left.semantic_signature != right.semantic_signature);
  REQUIRE(right.canonical_reasoning_signature ==
          "290c3119110f025367bef857d591443d0de72dd6af55d9200c6c8773681537b7");
}

TEST_CASE("SAA canonical reasoning bounds symmetric graphs conservatively") {
  using namespace statewright::saa;
  std::vector<ReasoningNodeSpec> nodes;
  for (int index = 0; index < 8; ++index) {
    nodes.push_back({.node_id = "n" + std::to_string(index),
                     .operator_name = "OBSERVE",
                     .semantic_inputs = {},
                     .semantic_outputs = {},
                     .public_claim_ids = {},
                     .evidence_requirements = {},
                     .assumptions = {},
                     .falsifiers = {},
                     .description = ""});
  }
  const auto canonical = canonicalize_reasoning_algorithm(
      {.name = "symmetric",
       .inputs = {"evidence"},
       .outputs = {"observations"},
       .nodes = std::move(nodes),
       .edges = {},
       .invariants = {},
       .termination = {"bounded", "all observations collected", 8},
       .applicability = {}});
  REQUIRE(canonical.canonicalization_strength ==
          "CONSERVATIVE_RENAMING_BOUND");
  REQUIRE(canonical.canonical_reasoning_signature ==
          "b1c0b16de03efa2749378fe68247771be7ac80202aca4bf08428730326949005");
  REQUIRE(canonical.warnings.size() == 2U);
}
