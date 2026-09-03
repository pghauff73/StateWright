#include "statewright/saa/unified_retrieval.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <utility>

namespace {

using statewright::contracts::Json;

statewright::saa::SemanticConcept problem_concept() {
  return statewright::saa::make_semantic_concept(
      "problem evidence", "problem evidence", "general reasoning", "evidence",
      {}, std::nullopt, std::nullopt, {"evidence:fixture"});
}

statewright::saa::UnifiedProblemRequirements math_only_requirements() {
  statewright::saa::UnifiedProblemRequirements requirements;
  requirements.problem_id = "math-only";
  requirements.input_concepts = {problem_concept()};
  requirements.desired_mathematical_output_count = 1;
  requirements.mathematical_domain = "continuous";
  requirements.require_reasoning_algorithm = false;
  return requirements;
}

Json mathematical_item(char digest_character, std::string meaning,
                       int output_count, std::string domain) {
  return {{"canonical_id",
           "canonical-algorithm:sha256:" +
               std::string(64U, digest_character)},
          {"payload",
           {{"domain", std::move(domain)},
            {"inputs",
             Json::array(
                 {{{"canonical_meaning", std::move(meaning)}}})},
            {"output_count", output_count}}}};
}

class MathematicalCatalog final
    : public statewright::saa::MathematicalAlgorithmCatalog {
public:
  explicit MathematicalCatalog(std::vector<Json> items)
      : items_(std::move(items)) {}

  std::vector<Json> list_mathematical_algorithms() override { return items_; }

private:
  std::vector<Json> items_;
};

statewright::saa::CanonicalReasoningAlgorithm fit_algorithm() {
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

class ReasoningCatalog final
    : public statewright::saa::ReasoningAlgorithmCatalog {
public:
  ReasoningCatalog() : algorithm_(fit_algorithm()) {}

  std::vector<std::string> list_reasoning_ids() const override {
    return {id()};
  }

  statewright::saa::CanonicalReasoningAlgorithm
  load_reasoning_algorithm(std::string_view reasoning_id) const override {
    if (reasoning_id != id()) {
      throw std::runtime_error("unknown test reasoning ID");
    }
    return algorithm_;
  }

  [[nodiscard]] std::string id() const {
    return "canonical-reasoning:sha256:" +
           algorithm_.canonical_reasoning_signature;
  }

private:
  statewright::saa::CanonicalReasoningAlgorithm algorithm_;
};

class Equivalence final : public statewright::saa::SemanticMeaningEquivalence {
public:
  bool meanings_equivalent(std::string_view left,
                           std::string_view right) override {
    return (left == "road speed" && right == "translational velocity") ||
           (left == "translational velocity" && right == "road speed");
  }
};

} // namespace

TEST_CASE("SAA unified mathematical fit matches frozen contract") {
  using namespace statewright::saa;
  const auto semantic_concept = problem_concept();
  REQUIRE(semantic_concept.concept_signature ==
          "88c0b71a38a23981267fc6371cb7fb2c8c1d0128477ae7e1c6f42696cb5b9c57");

  const auto good = evaluate_mathematical_fit(
      mathematical_item('a', "problem evidence", 1, "continuous"),
      math_only_requirements());
  REQUIRE(good.status == "GOOD_MATHEMATICAL_FIT");
  REQUIRE(good.fit_score_bp == 10000);
  REQUIRE(good.eligible());
  REQUIRE(good.fit_signature ==
          "6a3e8b94f52c38b8dcf6db5ce7e4f05f00275348cad5f67ea3c81b7049fa2331");

  const auto bad = evaluate_mathematical_fit(
      mathematical_item('b', "different", 2, "discrete"),
      math_only_requirements());
  REQUIRE(bad.status == "INELIGIBLE_MATHEMATICAL_FIT");
  REQUIRE_FALSE(bad.eligible());
  REQUIRE(bad.blocking_gaps ==
          std::vector<std::string>{
              "unmatched representative input meanings: different",
              "mathematical output count 2 != required 1",
              "mathematical domain discrete != required continuous"});
  REQUIRE(bad.fit_signature ==
          "58fdfb359f55a2618a3bc8879dc3b7225293d421e94a6a3b99b885e6f974430e");
}

TEST_CASE("SAA unified retrieval selects the exact mathematical fit") {
  using namespace statewright::saa;
  MathematicalCatalog catalog(
      {mathematical_item('a', "problem evidence", 1, "continuous")});
  const auto decision = retrieve_unified_solution(
      &catalog, nullptr, math_only_requirements());
  REQUIRE(decision.problem_signature ==
          "a7d6347170fdfcba24530daa7b9603500217fc4f8de74497d1a66ef4d8af9f10");
  REQUIRE(decision.mathematical_candidates.size() == 1U);
  REQUIRE(decision.mathematical_candidates.front().eligible());
  REQUIRE(decision.selected_mathematical_algorithm_id ==
          "canonical-algorithm:sha256:" + std::string(64U, 'a'));
  REQUIRE(decision.required_components_satisfied);
  REQUIRE(decision.status == "QUALIFIED_KNOWN_SOLUTION_PAIR_FOUND");
  REQUIRE(decision.decision_signature ==
          "dae6ce381cb93e227ef7e4f0fe4fd87e7363c3030e35fa52e6a158e5d91dfc1a");
}

TEST_CASE("SAA unified retrieval uses qualified semantic equivalence") {
  using namespace statewright::saa;
  const auto road_speed = make_semantic_concept(
      "road speed", "road speed", "vehicle dynamics", "velocity", {},
      std::nullopt, std::nullopt, {"evidence:road-speed"});
  UnifiedProblemRequirements requirements;
  requirements.problem_id = "aligned-speed";
  requirements.input_concepts = {road_speed};
  requirements.desired_mathematical_output_count = 1;
  requirements.mathematical_domain = "continuous";
  requirements.require_reasoning_algorithm = false;
  MathematicalCatalog catalog(
      {mathematical_item('c', "translational velocity", 1, "continuous")});
  Equivalence ontology;

  const auto without_ontology =
      retrieve_unified_solution(&catalog, nullptr, requirements);
  REQUIRE_FALSE(without_ontology.selected_mathematical_algorithm_id);

  const auto with_ontology =
      retrieve_unified_solution(&catalog, nullptr, requirements, &ontology);
  REQUIRE(with_ontology.selected_mathematical_algorithm_id ==
          "canonical-algorithm:sha256:" + std::string(64U, 'c'));
  REQUIRE(with_ontology.required_components_satisfied);
}

TEST_CASE("SAA unified retrieval selects a complete known pair") {
  using namespace statewright::saa;
  MathematicalCatalog mathematical_catalog(
      {mathematical_item('a', "problem evidence", 1, "continuous")});
  const ReasoningCatalog reasoning_catalog;
  UnifiedProblemRequirements requirements;
  requirements.problem_id = "known-pair";
  requirements.input_concepts = {problem_concept()};
  requirements.desired_mathematical_output_count = 1;
  requirements.mathematical_domain = "continuous";
  requirements.reasoning_desired_outputs = {"qualified conclusion"};
  requirements.reasoning_applicability = {
      "evidence-backed factual reasoning"};
  requirements.required_invariants = {
      "unverified claims do not become facts without evidence"};
  requirements.available_evidence_requirements = {
      "source snapshot", "independent verification"};
  requirements.max_reasoning_steps = 8;

  const auto decision = retrieve_unified_solution(
      &mathematical_catalog, &reasoning_catalog, requirements);
  REQUIRE(decision.problem_signature ==
          "058d865c23c6d65616c991dbcb6118a81e811da067e6ea5678f56f5edd9caa9c");
  REQUIRE(decision.selected_mathematical_algorithm_id ==
          "canonical-algorithm:sha256:" + std::string(64U, 'a'));
  REQUIRE(decision.selected_reasoning_id == reasoning_catalog.id());
  REQUIRE(decision.reasoning_result);
  REQUIRE(decision.reasoning_result->result_signature ==
          "fec7b5d3c3ca641e5ad79383405b6e5b0fb46c2063648af66588be7451cab854");
  REQUIRE(decision.required_components_satisfied);
  REQUIRE(decision.status == "QUALIFIED_KNOWN_SOLUTION_PAIR_FOUND");
  REQUIRE(decision.decision_signature ==
          "697419a88dba4a3eb4e9b5b0a6476551ea7d63b70afb7a4220e15f47c8294daa");
}

TEST_CASE("SAA unified retrieval reports missing required stores") {
  using namespace statewright::saa;
  UnifiedProblemRequirements requirements;
  requirements.problem_id = "none";
  requirements.input_concepts = {problem_concept()};
  const auto decision =
      retrieve_unified_solution(nullptr, nullptr, requirements);
  REQUIRE(decision.problem_signature ==
          "19a3c3b8593f4a3ad0fbe8400ff7d049df5591c978aa61db219680271c68229e");
  REQUIRE(decision.missing_components ==
          std::vector<std::string>{"MATHEMATICAL_ALGORITHM",
                                   "REASONING_ALGORITHM"});
  REQUIRE_FALSE(decision.required_components_satisfied);
  REQUIRE(decision.status == "NO_QUALIFIED_KNOWN_SOLUTION_FIT");
  REQUIRE(decision.decision_signature ==
          "39e0c5ca344275bf6476002ed835f2e0f80f2fd4615c7c29a2f8ba44c60925fc");
}
