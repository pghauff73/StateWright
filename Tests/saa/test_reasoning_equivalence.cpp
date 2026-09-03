#include "statewright/saa/reasoning_equivalence.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

statewright::saa::ReasoningAlgorithmSpec equivalence_spec(
    std::string output = "qualified conclusion") {
  using namespace statewright::saa;
  return {.name = "falsification-first",
          .inputs = {"problem evidence"},
          .outputs = {output},
          .nodes = {{"observe",
                     "OBSERVE",
                     {"problem evidence"},
                     {"candidate evidence"},
                     {},
                     {"source snapshot"},
                     {},
                     {},
                     ""},
                    {"test",
                     "FALSIFY",
                     {"candidate evidence"},
                     {"surviving claim"},
                     {},
                     {},
                     {},
                     {"counterexample exists"},
                     ""},
                    {"verify",
                     "VERIFY",
                     {"surviving claim"},
                     {std::move(output)},
                     {},
                     {"independent verification"},
                     {},
                     {},
                     ""}},
          .edges = {{"observe", "test", "NEXT", ""},
                    {"test", "verify", "NEXT", ""}},
          .invariants = {
              "unverified claims do not become facts without evidence"},
          .termination = {"bounded evidence decision",
                          "claim verified or falsified or budget exhausted", 8},
          .applicability = {"evidence-backed factual reasoning"}};
}

} // namespace

TEST_CASE("SAA reasoning equivalence admits exact reusable identity") {
  const auto algorithm = statewright::saa::canonicalize_reasoning_algorithm(
      equivalence_spec());
  const auto assessment =
      statewright::saa::compare_reasoning_algorithms(algorithm, algorithm);
  REQUIRE(assessment.status == "EXACT_REASONING_ALGORITHM_EQUIVALENCE");
  REQUIRE(assessment.exact_equivalence);
  REQUIRE(assessment.canonical_reuse_eligible);
  REQUIRE(assessment.delta.delta_signature ==
          "4c67b5fae1b2d94dea18275e0457c7f2ed260d3822cbb165ddb69a1e701b95dd");
  REQUIRE(assessment.assessment_signature ==
          "64fa8aae1a8416dad2fbdaff4e4a7fdae4ff3549ba93d7eefee0b3731c6ba54f");
}

TEST_CASE("SAA reasoning equivalence distinguishes semantic near variants") {
  const auto left = statewright::saa::canonicalize_reasoning_algorithm(
      equivalence_spec());
  const auto right = statewright::saa::canonicalize_reasoning_algorithm(
      equivalence_spec("marketing recommendation"));
  const auto assessment =
      statewright::saa::compare_reasoning_algorithms(left, right);
  REQUIRE(assessment.status ==
          "OPERATOR_TOPOLOGY_MATCH_SEMANTIC_DIFFERENCE");
  REQUIRE_FALSE(assessment.canonical_reuse_eligible);
  REQUIRE(assessment.relation_candidates ==
          std::vector<std::string>{"NEAR_VARIANT_OF"});
  REQUIRE(assessment.assessment_signature ==
          "1644f01449be07dbd4b3e1424cbe4c1df796f2c4d31f6ee61bc6fc9323d6dce7");
}
