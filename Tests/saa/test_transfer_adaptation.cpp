#include "statewright/saa/algorithm_adaptation.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <catch2/catch_test_macros.hpp>

#include <tuple>
#include <utility>

namespace {

using statewright::contracts::Json;

statewright::saa::SemanticConcept transport_concept(
    std::string name, std::string meaning, std::string quantity = "speed") {
  using namespace statewright::saa;
  const std::string evidence_id = "evidence:" + name;
  return make_semantic_concept(
      std::move(name), std::move(meaning), "transport", std::move(quantity),
      {}, LENGTH / TIME, std::nullopt, {evidence_id});
}

statewright::saa::AlgorithmDomainContract domain_contract(
    std::string domain, const statewright::saa::SemanticConcept &semantic_concept,
    char dynamics = '1', char boundary = '2',
    char evidence_signature = '4', char evidence_scope = '5') {
  return {.domain = std::move(domain),
          .input_concepts = {semantic_concept},
          .invariants = {"bounded"},
          .boundary_signatures = {std::string(64U, boundary)},
          .dynamics_signature = std::string(64U, dynamics),
          .evidence_requirements = {"source snapshot"},
          .qualification_evidence_signatures = {
              std::string(64U, evidence_signature)},
          .evidence_scope_signature = std::string(64U, evidence_scope)};
}

std::pair<statewright::saa::UnifiedProblemRequirements,
          statewright::saa::UnifiedRetrievalDecision>
unified_fixture(bool math_selected = false, bool reasoning_selected = false,
                std::string math_gap = {}, std::string reasoning_gap = {}) {
  using namespace statewright::saa;
  UnifiedProblemRequirements requirements;
  requirements.problem_id = "explain";
  requirements.input_concepts = {
      transport_concept("speed", "translational speed")};
  requirements.desired_mathematical_output_count = 1;
  requirements.mathematical_domain = "continuous";
  requirements.reasoning_desired_outputs = {"qualified conclusion"};
  requirements = canonical_unified_requirements(std::move(requirements));

  MathematicalFitAssessment mathematical = {
      .canonical_algorithm_id = "math:one",
      .status = math_selected ? "GOOD_MATHEMATICAL_FIT"
                              : "INELIGIBLE_MATHEMATICAL_FIT",
      .fit_score_bp = math_selected ? 10000 : 6000,
      .semantic_input_fit_bp = 10000,
      .output_shape_fit_bp = 10000,
      .domain_fit_bp = math_gap.empty() ? 10000 : 0,
      .matched_input_meanings = {"translational speed"},
      .unmatched_input_meanings = {},
      .blocking_gaps = math_gap.empty()
                           ? std::vector<std::string>{}
                           : std::vector<std::string>{math_gap},
      .fit_signature = statewright::contracts::sha256_json(
          {{"gap", math_gap}, {"math", math_selected}})};
  ReasoningFitAssessment reasoning = {
      .reasoning_id = "reason:one",
      .canonical_reasoning_signature = std::string(64U, 'a'),
      .status = reasoning_selected ? "GOOD_REASONING_FIT"
                                   : "INELIGIBLE_REASONING_FIT",
      .fit_score_bp = reasoning_selected ? 10000 : 5000,
      .input_fit_bp = 10000,
      .output_fit_bp = 10000,
      .applicability_fit_bp = 10000,
      .invariant_fit_bp = 10000,
      .evidence_fit_bp = reasoning_gap.empty() ? 10000 : 0,
      .termination_fit_bp = 10000,
      .blocking_gaps = reasoning_gap.empty()
                           ? std::vector<std::string>{}
                           : std::vector<std::string>{reasoning_gap},
      .adaptation_gaps = {},
      .fit_signature = statewright::contracts::sha256_json(
          {{"gap", reasoning_gap}, {"reason", reasoning_selected}})};
  ReasoningRetrievalResult reasoning_result = {
      .schema_version = 1,
      .fit_version = "fixture",
      .requirements_signature = std::string(64U, 'b'),
      .candidates = {std::move(reasoning)},
      .selected_reasoning_id = reasoning_selected
                                   ? std::optional<std::string>("reason:one")
                                   : std::nullopt,
      .selected_fit_score_bp = reasoning_selected ? 10000 : 0,
      .search_scope = "fixture",
      .result_signature = std::string(64U, 'c')};
  std::vector<std::string> missing;
  if (!math_selected) {
    missing.push_back("MATHEMATICAL_ALGORITHM");
  }
  if (!reasoning_selected) {
    missing.push_back("REASONING_ALGORITHM");
  }
  UnifiedRetrievalDecision decision = {
      .schema_version = 1,
      .retrieval_version = "fixture",
      .problem_signature = std::string(64U, 'd'),
      .mathematical_candidates = {std::move(mathematical)},
      .selected_mathematical_algorithm_id =
          math_selected ? std::optional<std::string>("math:one")
                        : std::nullopt,
      .reasoning_result = std::move(reasoning_result),
      .selected_reasoning_id = reasoning_selected
                                   ? std::optional<std::string>("reason:one")
                                   : std::nullopt,
      .required_components_satisfied = missing.empty(),
      .missing_components = std::move(missing),
      .status = "fixture",
      .decision_signature = std::string(64U, 'e')};
  return {std::move(requirements), std::move(decision)};
}

} // namespace

TEST_CASE("SAA transfer exactly preserves a qualified domain contract") {
  using namespace statewright::saa;
  const auto speed = transport_concept("speed", "translational speed");
  REQUIRE(speed.concept_signature ==
          "0a0585b51736e66d6c3ee611a64618547becebf39fba6a41439ed77090e0da41");
  const auto assessment = assess_algorithm_transfer(
      "canonical-algorithm:fixture", domain_contract("road", speed),
      domain_contract("rail", speed));
  REQUIRE(assessment.status == "EXACT_TRANSFER_CONTRACT_MATCH");
  REQUIRE(assessment.transfer_without_requalification);
  REQUIRE_FALSE(assessment.adaptation_required);
  REQUIRE(assessment.assessment_signature ==
          "f29f6848a4f5c702675845ed9f92ff172f97b118edb6528e53e55ea3388a1f93");

  const auto explanation = explain_algorithm_transfer(assessment);
  REQUIRE(explanation.status == "EXPLAINED_EXACT_TRANSFER");
  REQUIRE(explanation.fit_gap_dimensions.empty());
  REQUIRE(explanation.explanation_signature ==
          "c9f50d1c7044034d57f109e632c99a1f2aa055f44487a110d48d65ee0593f597");
}

TEST_CASE("SAA transfer isolates dynamics and evidence requalification") {
  using namespace statewright::saa;
  const auto speed = transport_concept("speed", "translational speed");
  const auto dynamics = assess_algorithm_transfer(
      "canonical-algorithm:fixture", domain_contract("road", speed, '1'),
      domain_contract("air", speed, '3'));
  REQUIRE(dynamics.status == "TRANSFER_REQUIRES_DOMAIN_REQUALIFICATION");
  REQUIRE(dynamics.adaptation_gaps ==
          std::vector<std::string>{"DYNAMICS_CONTRACT"});
  REQUIRE(dynamics.assessment_signature ==
          "454d5cf4a1b065659ecd6064aed01c831600a9cf68768cdcfa2b6227f40b9adf");
  const auto dynamics_explanation = explain_algorithm_transfer(dynamics);
  REQUIRE(dynamics_explanation.fit_gap_dimensions ==
          std::vector<std::string>{"DYNAMICS_CONTRACT"});
  REQUIRE(dynamics_explanation.explanation_signature ==
          "7f296f2d67be7912afb6a00f4dd5664b4d5daebf0661e26a22b2715d3cb981a7");

  const auto evidence = assess_algorithm_transfer(
      "canonical-algorithm:fixture", domain_contract("road", speed),
      domain_contract("rail", speed, '1', '2', '4', '6'));
  REQUIRE(evidence.adaptation_gaps ==
          std::vector<std::string>{"EVIDENCE_CONTRACT"});
  REQUIRE(evidence.assessment_signature ==
          "269a0972484ea4aa673f38acf8b4cba5cb2ba439d8c4249713c349b86b6db8d0");
}

TEST_CASE("SAA transfer blocks an unresolved semantic mismatch") {
  using namespace statewright::saa;
  const auto speed =
      transport_concept("speed", "translational speed", "speed");
  const auto flow = transport_concept("flow", "volumetric transport proxy",
                                      "flow proxy");
  REQUIRE(flow.concept_signature ==
          "8f848c0d5c4a96a3d2024a27fe5ab063d95f19ab3b83ac8be6cb4cc51caf3623");
  const auto assessment = assess_algorithm_transfer(
      "canonical-algorithm:fixture", domain_contract("road", speed),
      domain_contract("process", flow));
  REQUIRE(assessment.status == "TRANSFER_BLOCKED_SEMANTIC_MISMATCH");
  REQUIRE(assessment.blocking_gaps ==
          std::vector<std::string>{
              "semantic contracts are not exactly equivalent"});
  REQUIRE(assessment.assessment_signature ==
          "a7ad04d149faa134058e56d9a7393c70e31e95f114e698e7d2ea1a9b7ed5b9e8");
  const auto explanation = explain_algorithm_transfer(assessment);
  REQUIRE(explanation.status == "EXPLAINED_BLOCKED_TRANSFER");
  REQUIRE(explanation.fit_gap_dimensions ==
          std::vector<std::string>{"SEMANTIC_TRANSFER_CONTRACT"});
  REQUIRE(explanation.explanation_signature ==
          "f1b25dfaf181a95ede2c9ea235bb53d7dd704a402bb72930f2441353c66b683b");
  REQUIRE_THROWS_AS(build_controlled_adaptation_plan(explanation),
                    statewright::common::Error);
}

TEST_CASE("SAA retrieval explanation records complete counterfactual gaps") {
  using namespace statewright::saa;
  auto [requirements, decision] = unified_fixture(
      false, false, "mathematical domain discrete != required continuous",
      "evidence capability unavailable: independent verification");
  const auto explanation = explain_unified_retrieval(decision, requirements);
  REQUIRE(explanation.status == "EXPLAINED_CONFIRMED_RETRIEVAL_GAP");
  REQUIRE(explanation.fit_gap_dimensions ==
          std::vector<std::string>{"MATHEMATICAL_DOMAIN",
                                   "MISSING_MATHEMATICAL_ALGORITHM",
                                   "MISSING_REASONING_ALGORITHM",
                                   "REASONING_EVIDENCE_CAPABILITY"});
  REQUIRE(explanation.counterfactual_changes.size() == 4U);
  REQUIRE(explanation.explanation_signature ==
          "173b861230beaa6a3e2e8b90e9a613d6facac17e70106a82c9863370dd2c3f78");

  const auto plan = build_controlled_adaptation_plan(explanation);
  REQUIRE(plan.one_dimension_per_step);
  REQUIRE(plan.qualification_required);
  REQUIRE_FALSE(plan.canonical_reuse_eligible);
  REQUIRE(plan.steps.size() == 4U);
  REQUIRE(plan.plan_signature ==
          "9f4d127d9c6242d2d5b505c7d8bb1427ed2c86d9c4302819b012317b6fa8f72f");
}

TEST_CASE("SAA retrieval explanation records a complete known solution") {
  using namespace statewright::saa;
  auto [requirements, decision] = unified_fixture(true, true);
  const auto explanation = explain_unified_retrieval(decision, requirements);
  REQUIRE(explanation.status == "EXPLAINED_COMPLETE_KNOWN_SOLUTION");
  REQUIRE(explanation.selected_reasons.size() == 2U);
  REQUIRE(explanation.fit_gap_dimensions.empty());
  REQUIRE(explanation.explanation_signature ==
          "123262cdad33b11620fba686576eeace19e285e082a49b9fa8fc264441fa1132");
  const auto plan = build_controlled_adaptation_plan(explanation);
  REQUIRE(plan.steps.empty());
  REQUIRE_FALSE(plan.qualification_required);
  REQUIRE(plan.plan_signature ==
          "9ce3babfc0ae2b270fe99ea30851230fe93e28652be4d3e1456a1c51e2af1ec1");
}

TEST_CASE("SAA adaptation changes exactly one declared dimension") {
  using namespace statewright::saa;
  const auto speed = transport_concept("speed", "translational speed");
  const auto assessment = assess_algorithm_transfer(
      "canonical-algorithm:fixture", domain_contract("road", speed, '1'),
      domain_contract("air", speed, '3'));
  const auto explanation = explain_algorithm_transfer(assessment);
  const auto plan = build_controlled_adaptation_plan(
      explanation, "canonical-algorithm:fixture");
  REQUIRE(plan.steps.size() == 1U);
  REQUIRE(plan.steps.front().dimension == "DYNAMICS_CONTRACT");
  REQUIRE(plan.steps.front().step_signature ==
          "5a6e25f13a77120dec2568cdceb54b4dd89f4fc7f34f6a97095f882d81056a71");
  REQUIRE(plan.plan_signature ==
          "a4ee5d53367d5346767c299a4300fdb3f5f934132cd7aca276fcf1fb65361f66");

  const auto candidate = create_adapted_candidate(
      plan.steps.front(),
      {{"dimension", "DYNAMICS_CONTRACT"}, {"target_domain", "air"}});
  REQUIRE(candidate.epistemic_status ==
          "UNQUALIFIED_ADAPTED_ALGORITHM_CANDIDATE");
  REQUIRE(candidate.qualification_required);
  REQUIRE_FALSE(candidate.canonical_reuse_eligible);
  REQUIRE(candidate.candidate_signature ==
          "30cf0765ab7dd1e161f303c75dcc98c450417434961402935ad20ee11a8b0e23");

  REQUIRE_THROWS_AS(
      create_adapted_candidate(
          plan.steps.front(),
          {{"dimension", "DYNAMICS_CONTRACT"},
           {"also_changes", Json::array({"MATHEMATICAL_DOMAIN"})}}),
      statewright::common::Error);
}
