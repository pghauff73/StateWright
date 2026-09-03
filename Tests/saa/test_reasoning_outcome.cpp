#include "statewright/saa/reasoning_outcome.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>

namespace {

statewright::saa::CanonicalReasoningAlgorithm outcome_algorithm() {
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

statewright::saa::ReasoningEvidenceResolver outcome_evidence() {
  using statewright::saa::ReasoningGroundingEvidence;
  return [](std::string_view id)
             -> std::optional<ReasoningGroundingEvidence> {
    if (id == "ev:source") {
      return ReasoningGroundingEvidence{
          "egcf-evidence", true, false, "deterministic-source-reader",
          "source-snapshot", {"source snapshot"}, "source"};
    }
    if (id == "ev:verify") {
      return ReasoningGroundingEvidence{
          "egcf-evidence", true, false, "human-reviewer", "review",
          {"independent verification"}, "verification"};
    }
    return std::nullopt;
  };
}

} // namespace

TEST_CASE("SAA reasoning outcome qualifies grounded exact execution") {
  using namespace statewright::saa;
  const auto algorithm = outcome_algorithm();
  REQUIRE(reasoning_evidence_requirements(algorithm) ==
          std::vector<std::string>{"independent verification",
                                   "source snapshot"});
  REQUIRE(reasoning_falsifiers(algorithm) ==
          std::vector<std::string>{"counterexample exists"});
  const auto outcome = make_reasoning_execution_outcome(
      algorithm, "run-1", {"qualified conclusion"},
      {"ev:verify", "ev:source"},
      {{"unverified claims do not become facts without evidence", true}},
      {{"counterexample exists", "SURVIVED"}}, true, 3, true, true);
  REQUIRE(outcome.outcome_signature ==
          "8f24cf98cbd101ac31c8f83d47bc8048a89ed3e6d898e5ddb6ae2c6de3c2864a");
  const auto qualification =
      qualify_reasoning_outcome(outcome_evidence(), algorithm, outcome);
  REQUIRE(qualification.status == "QUALIFIED_REASONING_OUTCOME");
  REQUIRE(qualification.evidence_requirement_coverage_bp == 10000);
  REQUIRE(qualification.independence_groups ==
          std::vector<std::string>{"source", "verification"});
  REQUIRE(qualification.canonical_reuse_eligible);
  REQUIRE(qualification.qualification_signature ==
          "4998c326eedff44ca809fe545d0788c5bc12344114636b6378cd0cfb8c0ad66b");
}

TEST_CASE("SAA reasoning outcome rejects triggered falsifier") {
  using namespace statewright::saa;
  const auto algorithm = outcome_algorithm();
  const auto outcome = make_reasoning_execution_outcome(
      algorithm, "bad-run", {"qualified conclusion"},
      {"ev:source", "ev:verify"},
      {{"unverified claims do not become facts without evidence", true}},
      {{"counterexample exists", "TRIGGERED"}}, true, 3, true, true);
  REQUIRE(outcome.outcome_signature ==
          "5b66580d6bc6631e066025b052ce729b75acaad7aca4010c83eee0c825ebea00");
  const auto qualification =
      qualify_reasoning_outcome(outcome_evidence(), algorithm, outcome);
  REQUIRE(qualification.status == "UNQUALIFIED_REASONING_FALSIFIER");
  REQUIRE_FALSE(qualification.canonical_reuse_eligible);
  REQUIRE(qualification.qualification_signature ==
          "383d07efdc766f7159c1bdf04f5e315c17d2650329377c1ae0ce9b918e54ad2c");
}

TEST_CASE("SAA reasoning outcome enforces bounded execution inputs") {
  using namespace statewright::saa;
  const auto algorithm = outcome_algorithm();
  REQUIRE_THROWS(make_reasoning_execution_outcome(
      algorithm, "", {}, {}, {}, {}, false, 0, false, false));
  REQUIRE_THROWS(make_reasoning_execution_outcome(
      algorithm, "overflow", {}, {}, {}, {}, false, 9, false, false));
  REQUIRE_THROWS(make_reasoning_execution_outcome(
      algorithm, "duplicate", {}, {}, {{"same", true}, {" SAME ", false}},
      {}, false, 0, false, false));
}
