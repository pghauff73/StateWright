#include "statewright/saa/reasoning_composition.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

statewright::saa::CanonicalReasoningAlgorithm composition_left() {
  using namespace statewright::saa;
  return canonicalize_reasoning_algorithm(
      {.name = "falsification-first",
       .inputs = {"problem evidence"},
       .outputs = {"qualified conclusion"},
       .nodes = {{.node_id = "observe",
                  .operator_name = "OBSERVE",
                  .semantic_inputs = {"problem evidence"},
                  .semantic_outputs = {"candidate evidence"},
                  .public_claim_ids = {},
                  .evidence_requirements = {"source snapshot"},
                  .assumptions = {},
                  .falsifiers = {},
                  .description = ""},
                 {.node_id = "test",
                  .operator_name = "FALSIFY",
                  .semantic_inputs = {"candidate evidence"},
                  .semantic_outputs = {"surviving claim"},
                  .public_claim_ids = {},
                  .evidence_requirements = {},
                  .assumptions = {},
                  .falsifiers = {"counterexample exists"},
                  .description = ""},
                 {.node_id = "verify",
                  .operator_name = "VERIFY",
                  .semantic_inputs = {"surviving claim"},
                  .semantic_outputs = {"qualified conclusion"},
                  .public_claim_ids = {},
                  .evidence_requirements = {"independent verification"},
                  .assumptions = {},
                  .falsifiers = {},
                  .description = ""}},
       .edges = {{"observe", "test", "NEXT", ""},
                 {"test", "verify", "NEXT", ""}},
       .invariants = {
           "unverified claims do not become facts without evidence"},
       .termination = {"bounded evidence decision",
                       "claim verified or falsified or budget exhausted", 8},
       .applicability = {"evidence-backed factual reasoning"}});
}

statewright::saa::CanonicalReasoningAlgorithm composition_right(
    std::string input = "qualified conclusion") {
  using namespace statewright::saa;
  return canonicalize_reasoning_algorithm(
      {.name = "qualified conclusion to action",
       .inputs = {input},
       .outputs = {"action decision"},
       .nodes = {{.node_id = "classify",
                  .operator_name = "CLASSIFY",
                  .semantic_inputs = {input},
                  .semantic_outputs = {"candidate action"},
                  .public_claim_ids = {},
                  .evidence_requirements = {},
                  .assumptions = {},
                  .falsifiers = {},
                  .description = ""},
                 {.node_id = "verify",
                  .operator_name = "VERIFY",
                  .semantic_inputs = {"candidate action"},
                  .semantic_outputs = {"action decision"},
                  .public_claim_ids = {},
                  .evidence_requirements = {"decision verification"},
                  .assumptions = {},
                  .falsifiers = {},
                  .description = ""}},
       .edges = {{"classify", "verify", "NEXT", ""}},
       .invariants = {"action follows qualified conclusion"},
       .termination = {"bounded decision",
                       "action verified or budget exhausted", 5},
       .applicability = {"evidence-backed factual reasoning"}});
}

statewright::saa::ReasoningOutcomeQualification qualified(
    const statewright::saa::CanonicalReasoningAlgorithm &algorithm,
    char signature_digit) {
  using namespace statewright::saa;
  return {.schema_version = 1,
          .outcome_version = std::string(reasoning_outcome_version),
          .canonical_reasoning_signature =
              algorithm.canonical_reasoning_signature,
          .outcome_signature = std::string(64U, 'a'),
          .status = "QUALIFIED_REASONING_OUTCOME",
          .evidence_requirement_coverage_bp = 10000,
          .grounded_evidence_ids = {"e"},
          .independence_groups = {"g"},
          .invariant_eligible = true,
          .falsifier_eligible = true,
          .termination_eligible = true,
          .output_contract_eligible = true,
          .canonical_reuse_eligible = true,
          .qualification_signature = std::string(64U, signature_digit),
          .warnings = {}};
}

} // namespace

TEST_CASE("SAA reasoning composition preserves qualified component boundaries") {
  using namespace statewright::saa;
  const auto left = composition_left();
  const auto right = composition_right();
  const auto left_qualification = qualified(left, '1');
  const auto right_qualification = qualified(right, '2');
  const auto assessment = assess_reasoning_composition(
      left, right, &left_qualification, &right_qualification);
  REQUIRE(assessment.status == "SAFE_REASONING_COMPOSITION");
  REQUIRE(assessment.composition_eligible());
  REQUIRE(assessment.assessment_signature ==
          "d241c2241a2a047d8ec84decd949909d62d7c01fce509567c3a124d3e166b26a");
  const auto composition = compose_reasoning_algorithms(
      left, right, left_qualification, right_qualification);
  REQUIRE(composition.interface_semantics ==
          std::vector<std::string>{"qualified conclusion"});
  REQUIRE(composition.composed_algorithm.output_semantics ==
          std::vector<std::string>{"action decision"});
  REQUIRE(composition.composed_algorithm.canonical_reasoning_signature ==
          "8269ac4ea6ff149b410a505827161a6cdb620c793b0f9e99a62362e81aacc351");
  REQUIRE(composition.composition_signature ==
          "9ed4bf2f2cb4bde04e2cb8222a125695ee34d907ee0df78e7dd7bdd32133eb72");
  REQUIRE(composition.qualification_required);
  REQUIRE_FALSE(composition.canonical_reuse_eligible);
}

TEST_CASE("SAA reasoning composition blocks semantic interface mismatch") {
  using namespace statewright::saa;
  const auto left = composition_left();
  const auto right = composition_right("unrelated signal");
  const auto left_qualification = qualified(left, '1');
  const auto right_qualification = qualified(right, '2');
  const auto assessment = assess_reasoning_composition(
      left, right, &left_qualification, &right_qualification);
  REQUIRE(assessment.status == "BLOCKED_REASONING_COMPOSITION");
  REQUIRE_FALSE(assessment.interface_eligible);
  REQUIRE_FALSE(assessment.composition_eligible());
  REQUIRE_THROWS(compose_reasoning_algorithms(
      left, right, left_qualification, right_qualification));
}
