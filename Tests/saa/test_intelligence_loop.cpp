#include "statewright/saa/intelligence_loop.hpp"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

namespace {

using namespace statewright::saa;

RetrieveFirstReceipt receipt() {
  return {.schema_version = 1,
          .policy_version = "saa-retrieve-first-policy-v1",
          .status = "ADAPT_OR_FILL_CONFIRMED_GAP",
          .retrieval_attempted = true,
          .required_search_completed = true,
          .new_algorithm_generation_allowed = true,
          .adaptation_allowed = true,
          .generation_scope = {"MATHEMATICAL_ALGORITHM"},
          .selected_mathematical_algorithm_id =
              "canonical-algorithm:sha256:" + std::string(64, '1'),
          .selected_reasoning_id = std::nullopt,
          .retrieval_decision_signature = std::string(64, '2'),
          .explanation_signature = std::string(64, '3'),
          .fit_gap_dimensions = {"DYNAMICS_CONTRACT"},
          .guidance = {"adapt only verified delta"},
          .receipt_signature = std::string(64, '4')};
}

RetrievalExplanation explanation() {
  return {.schema_version = 1,
          .explanation_version = "fixture",
          .decision_signature = std::string(64, '2'),
          .status = "EXPLAINED_PARTIAL_FIT_WITH_DELTA",
          .selected_reasons = {},
          .rejected_reasons = {},
          .counterfactual_changes = {},
          .fit_gap_dimensions = {"DYNAMICS_CONTRACT"},
          .explanation_signature = std::string(64, '5')};
}

ControlledAdaptationPlan plan() {
  return {.schema_version = 1,
          .adaptation_version = "fixture",
          .source_explanation_signature = std::string(64, '5'),
          .steps = {},
          .one_dimension_per_step = true,
          .qualification_required = true,
          .canonical_reuse_eligible = false,
          .plan_signature = std::string(64, '6')};
}

MultiStepEvolutionAssessment evolution(bool qualified = true) {
  return {.schema_version = 1,
          .evolution_version = "fixture",
          .plan_signature = std::string(64, '6'),
          .final_candidate_ref =
              "adapted-candidate:sha256:" + std::string(64, '7'),
          .qualification_signatures = {},
          .qualified_step_count = qualified ? 2 : 1,
          .total_step_count = 2,
          .invariant_preservation_complete = qualified,
          .status = qualified ? "MULTISTEP_EVOLUTION_QUALIFIED"
                              : "MULTISTEP_EVOLUTION_BLOCKED",
          .evolution_qualified = qualified,
          .blocking_steps = qualified
                                ? std::vector<std::string>{}
                                : std::vector<std::string>{
                                      "step-2:EVOLUTION_STEP_INVARIANT_"
                                      "VIOLATION"},
          .assessment_signature = std::string(64, qualified ? '8' : '9')};
}

RepeatedExperimentAggregate aggregate(bool qualified = true) {
  return {.schema_version = 1,
          .aggregation_version = "fixture",
          .design_signature = std::string(64, 'a'),
          .result_signatures = {},
          .grounded_evidence_ids = {},
          .independence_groups = qualified
                                     ? std::vector<std::string>{"a", "b"}
                                     : std::vector<std::string>{"a"},
          .experiment_count = 2,
          .minimum_required_experiments = 2,
          .minimum_required_independence_groups = 2,
          .metric_evidence = {},
          .status = qualified
                        ? "SUSTAINED_CANDIDATE_IMPROVEMENT_QUALIFIED"
                        : "REPEATED_EVIDENCE_INDEPENDENCE_INSUFFICIENT",
          .sustained_improvement_qualified = qualified,
          .qualification_required_before_canonical_reuse = true,
          .aggregate_signature = std::string(64, qualified ? 'b' : 'c')};
}

RetrieveFirstReceipt post_receipt(char selected = 'd') {
  auto value = receipt();
  value.status = "REUSE_QUALIFIED_KNOWN_SOLUTION";
  value.new_algorithm_generation_allowed = false;
  value.adaptation_allowed = false;
  value.generation_scope.clear();
  value.selected_mathematical_algorithm_id =
      "canonical-algorithm:sha256:" + std::string(64, selected);
  value.retrieval_decision_signature = std::string(64, 'e');
  value.receipt_signature = std::string(64, 'f');
  return value;
}

} // namespace

TEST_CASE("SAA intelligence loop preserves early gate order") {
  auto incomplete = receipt();
  incomplete.retrieval_attempted = false;
  incomplete.required_search_completed = false;
  incomplete.status = "RETRIEVAL_INFRASTRUCTURE_MISSING";
  incomplete.generation_scope.clear();
  incomplete.selected_mathematical_algorithm_id = std::nullopt;
  incomplete.receipt_signature = std::string(64, '1');
  const auto blocked = evaluate_intelligence_improvement_loop(incomplete);
  REQUIRE(blocked.status == "LOOP_BLOCKED_RETRIEVAL_INCOMPLETE");
  REQUIRE(blocked.decision_signature ==
          "0c91a7fb68beb26d23f3c8da6b7248949b886803e1a3da195ffc5f3ea99f5ae4");

  auto reuse = receipt();
  reuse.status = "REUSE_QUALIFIED_KNOWN_SOLUTION";
  reuse.generation_scope.clear();
  reuse.receipt_signature = std::string(64, '0');
  const auto reused = evaluate_intelligence_improvement_loop(reuse);
  REQUIRE(reused.status == "KNOWN_SOLUTION_REUSE_COMPLETE");
  REQUIRE(reused.terminal);
  REQUIRE(reused.decision_signature ==
          "04527a1abe74bb5f4c4beade66a8967973fd66f37d38d852971f1a1d4a460dd1");

  const auto explain = evaluate_intelligence_improvement_loop(receipt());
  REQUIRE(explain.status ==
          "LOOP_REQUIRES_DETERMINISTIC_FIT_EXPLANATION");
  REQUIRE(explain.decision_signature ==
          "f677dc2de9eb9241344ccdb7567c45a39be2d86e57b2960641684cd30b77b75e");
}

TEST_CASE("SAA intelligence loop gates adaptation and experiment") {
  const auto source_receipt = receipt();
  const auto fit = explanation();
  const auto adaptation = plan();
  const auto qualified_evolution = evolution();
  const auto blocked_evolution = evolution(false);

  const auto needs_plan = evaluate_intelligence_improvement_loop(
      source_receipt, &fit);
  REQUIRE(needs_plan.decision_signature ==
          "2c491ceb7b831892db20becece8c49b079e129cf42a021b8dffd3e522acaa793");
  const auto needs_evolution = evaluate_intelligence_improvement_loop(
      source_receipt, &fit, &adaptation, nullptr, nullptr,
      "adapted-candidate:sha256:" + std::string(64, '7'));
  REQUIRE(needs_evolution.decision_signature ==
          "2400ddaa5e6d6a169c6f1a75a4c14e2503acb2722c7b124b0043c4fa4a08cb23");
  const auto evolution_blocked = evaluate_intelligence_improvement_loop(
      source_receipt, &fit, &adaptation, &blocked_evolution);
  REQUIRE(evolution_blocked.status ==
          "LOOP_BLOCKED_EVOLUTION_NOT_QUALIFIED");
  REQUIRE(evolution_blocked.decision_signature ==
          "37c9f33a0ca09f69004ec5175211301b29c5f590e0e5989985151a352eeec393");
  const auto needs_experiment = evaluate_intelligence_improvement_loop(
      source_receipt, &fit, &adaptation, &qualified_evolution);
  REQUIRE(needs_experiment.decision_signature ==
          "a527e643ffeb40d649622769c9f23045bc991e4816122622e21016df6eb76fb3");
  const auto weak_aggregate = aggregate(false);
  const auto experiment_blocked = evaluate_intelligence_improvement_loop(
      source_receipt, &fit, &adaptation, &qualified_evolution,
      &weak_aggregate);
  REQUIRE(experiment_blocked.decision_signature ==
          "bdc9ee11f1dc729d69330ac604750ca4f6dd8f1b2a6e0868ecf982d1b4f37d6b");
}

TEST_CASE("SAA intelligence loop proves post-promotion closure") {
  const auto source_receipt = receipt();
  const auto fit = explanation();
  const auto adaptation = plan();
  const auto qualified_evolution = evolution();
  const auto evidence = aggregate();
  const std::string candidate =
      "adapted-candidate:sha256:" + std::string(64, '7');
  const std::string promotion =
      "adaptation-promotion:sha256:" + std::string(64, 'c');
  const std::string canonical =
      "canonical-algorithm:sha256:" + std::string(64, 'd');

  const auto needs_promotion = evaluate_intelligence_improvement_loop(
      source_receipt, &fit, &adaptation, &qualified_evolution, &evidence,
      candidate);
  REQUIRE(needs_promotion.decision_signature ==
          "11dddcd06314d193660462ca850a3267fba9170be7eb5315f061f1cf9f44251e");
  const auto needs_post = evaluate_intelligence_improvement_loop(
      source_receipt, &fit, &adaptation, &qualified_evolution, &evidence,
      candidate, promotion, canonical, "MATHEMATICAL_ALGORITHM");
  REQUIRE(needs_post.decision_signature ==
          "18ab769c2477e6c74f62d352a9fe30d0cdfb2de1a1b5040b9839e3e3d920837a");

  const auto post = post_receipt();
  const auto closed = evaluate_intelligence_improvement_loop(
      source_receipt, &fit, &adaptation, &qualified_evolution, &evidence,
      candidate, promotion, canonical, "MATHEMATICAL_ALGORITHM", &post);
  REQUIRE(closed.status == "CLOSED_LOOP_IMPROVEMENT_VERIFIED");
  REQUIRE(closed.terminal);
  REQUIRE(closed.decision_signature ==
          "311d95067b408686dafdf9bae33aa612ca2e92544386a400e869cddf31de0233");

  const auto wrong = post_receipt('0');
  const auto mismatch = evaluate_intelligence_improvement_loop(
      source_receipt, &fit, &adaptation, &qualified_evolution, &evidence,
      candidate, promotion, canonical, "MATHEMATICAL_ALGORITHM", &wrong);
  REQUIRE(mismatch.status ==
          "LOOP_POST_PROMOTION_RETRIEVAL_DID_NOT_SELECT_PROMOTED_KNOWLEDGE");
  REQUIRE(mismatch.decision_signature ==
          "03d7b8677c204bc0c89ae61048164d5f7538d6fc2b186269332a178c4c165363");
}
