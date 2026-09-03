#include "statewright/saa/reasoning_semantics.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("SAA reasoning state rejects coupled atomic independence") {
  using namespace statewright::saa;
  const ReasoningStateModel state = {
      .dimensions = {{"evidence", "evidence", "evidence strength", "ATOMIC",
                      "UNVERIFIED_CONCEPT", {}, true},
                     {"consistency", "consistency",
                      "cross-source consistency", "ATOMIC",
                      "UNVERIFIED_CONCEPT", {}, true},
                     {"confidence", "confidence", "confidence", "ATOMIC",
                      "UNVERIFIED_CONCEPT", {}, true}},
      .dependencies = {{"evidence", "confidence", "CONTRIBUTES_TO", {}},
                       {"consistency", "confidence", "CONTRIBUTES_TO", {}}}};
  const auto assessment = assess_reasoning_state_semantics(state);
  REQUIRE(assessment.status ==
          "REASONING_STATE_SEMANTIC_MISREPRESENTATION");
  REQUIRE_FALSE(assessment.canonical_reasoning_state_eligible);
  REQUIRE(assessment.issues.size() == 2U);
  REQUIRE(assessment.issues[0].issue_kind ==
          "ATOMIC_DIMENSION_COUPLES_MULTIPLE_MEANINGS");
  REQUIRE(assessment.issues[1].issue_kind ==
          "DECLARED_INDEPENDENCE_CONTRADICTED_BY_DEPENDENCY");
  REQUIRE(assessment.state_signature ==
          "6f246d35321b7ecc57943cdd0a03327d6c2d3829455b4497e04cebf0b8b35e9c");
  REQUIRE(assessment.assessment_signature ==
          "b6fe8a4ff83c13a2016fd30c6dfb8d6cfe95405fce2fcbc79e607aa9573950d1");
  const auto directives =
      propagate_reasoning_semantic_issues(assessment.issues);
  REQUIRE(directives.size() == 14U);
}

TEST_CASE("SAA grounded composite reasoning state is coherent") {
  using namespace statewright::saa;
  const ReasoningStateModel state = {
      .dimensions =
          {{"evidence", "evidence", "evidence strength", "ATOMIC",
            "UNVERIFIED_CONCEPT", {}, true},
           {"consistency", "consistency", "cross-source consistency",
            "ATOMIC", "UNVERIFIED_CONCEPT", {}, true},
           {"support_quality", "support quality",
            "joint evidence support quality", "COMPOSITE",
            "SEMANTICALLY_RESOLVED", {"evidence:semantic-resolution"},
            false}},
      .dependencies = {
          {"evidence", "support_quality", "CONTRIBUTES_TO", {}},
          {"consistency", "support_quality", "CONTRIBUTES_TO", {}}}};
  const auto assessment = assess_reasoning_state_semantics(state);
  REQUIRE(assessment.status == "REASONING_STATE_SEMANTICALLY_COHERENT");
  REQUIRE(assessment.canonical_reasoning_state_eligible);
  REQUIRE(assessment.issues.empty());
  REQUIRE(assessment.state_signature ==
          "26a58680b5ad76e8ac892342a1d7ede6925962c2194e0d3e634e257e66f9f9d8");
  REQUIRE(assessment.assessment_signature ==
          "645750a1b03d796f11d2ab5d587be768775e00a45fc3b4a5e2de4a1e659cf27c");
}

TEST_CASE("SAA reasoning state detects labels and ungrounded facts") {
  using namespace statewright::saa;
  const auto collision = assess_reasoning_state_semantics(
      {.dimensions = {{"risk_a", "risk", "probability of failure", "ATOMIC",
                       "UNVERIFIED_CONCEPT", {}, true},
                      {"risk_b", "risk", "financial exposure", "ATOMIC",
                       "UNVERIFIED_CONCEPT", {}, true}},
       .dependencies = {}});
  REQUIRE(collision.issues.front().issue_kind == "SEMANTIC_LABEL_COLLISION");
  REQUIRE(collision.assessment_signature ==
          "5a8963256e67c806308669097480ca5051749b4413622393bb342a1ac2c49774");

  const auto ungrounded = assess_reasoning_state_semantics(
      {.dimensions = {{"fact", "fact", "supplier is approved", "ATOMIC",
                       "VERIFIED_FACT", {}, true}},
       .dependencies = {}});
  REQUIRE(ungrounded.issues.front().issue_kind ==
          "UNGROUNDED_REASONING_STATE");
  REQUIRE(ungrounded.assessment_signature ==
          "14aadab13bc4b0397f3b5987fe938d96c9bac2b9756b2327bf5a8c229c72bda3");
}
