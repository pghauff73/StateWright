#include "statewright/saa/reasoning_fit.hpp"

#include <catch2/catch_test_macros.hpp>

#include <map>

namespace {

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

statewright::saa::ReasoningTaskRequirements exact_requirements() {
  return {.available_inputs = {"problem evidence"},
          .desired_outputs = {"qualified conclusion"},
          .required_applicability = {
              "evidence-backed factual reasoning"},
          .required_invariants = {
              "unverified claims do not become facts without evidence"},
          .available_evidence_requirements = {
              "source snapshot", "independent verification"},
          .max_steps = 8};
}

class FitCatalog final : public statewright::saa::ReasoningAlgorithmCatalog {
public:
  explicit FitCatalog(statewright::saa::CanonicalReasoningAlgorithm algorithm)
      : algorithm_(std::move(algorithm)) {}

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

private:
  [[nodiscard]] std::string id() const {
    return "canonical-reasoning:sha256:" +
           algorithm_.canonical_reasoning_signature;
  }

  statewright::saa::CanonicalReasoningAlgorithm algorithm_;
};

} // namespace

TEST_CASE("SAA reasoning fit exactly matches frozen weighted contract") {
  using namespace statewright::saa;
  const auto algorithm = fit_algorithm();
  const std::string id = "canonical-reasoning:sha256:" +
                         algorithm.canonical_reasoning_signature;
  const auto assessment =
      evaluate_reasoning_fit(id, algorithm, exact_requirements());
  REQUIRE(assessment.status == "GOOD_REASONING_FIT");
  REQUIRE(assessment.fit_score_bp == 10000);
  REQUIRE(assessment.eligible());
  REQUIRE(assessment.fit_signature ==
          "aef418a717c3c58018aaf79d3cb04da6ee816891e91bd402e6c11ee598efa2a3");

  const FitCatalog catalog(algorithm);
  const auto result =
      retrieve_reasoning_algorithms(catalog, exact_requirements());
  REQUIRE(result.requirements_signature ==
          "40ad94d272b05943f2090e60e21b7b7a230b4dbc09ba8f0679946cd58dd3348c");
  REQUIRE(result.selected_reasoning_id == id);
  REQUIRE(result.selected_fit_score_bp == 10000);
  REQUIRE(result.result_signature ==
          "fec7b5d3c3ca641e5ad79383405b6e5b0fb46c2063648af66588be7451cab854");
}

TEST_CASE("SAA reasoning fit blocks unavailable evidence capability") {
  using namespace statewright::saa;
  const auto algorithm = fit_algorithm();
  auto requirements = exact_requirements();
  requirements.available_inputs.push_back("extra input");
  requirements.required_applicability.clear();
  requirements.required_invariants.clear();
  requirements.available_evidence_requirements = {"source snapshot"};
  const auto assessment = evaluate_reasoning_fit(
      "canonical-reasoning:sha256:" +
          algorithm.canonical_reasoning_signature,
      algorithm, requirements);
  REQUIRE(assessment.status == "INELIGIBLE_REASONING_FIT");
  REQUIRE(assessment.fit_score_bp == 9500);
  REQUIRE_FALSE(assessment.eligible());
  REQUIRE(assessment.fit_signature ==
          "8e1357b70ffb3af31d37a26a1e3d7c42be5aa086048e7311067bd918ac0db061");
}
