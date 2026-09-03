#include "statewright/saa/probation.hpp"

#include "statewright/contracts/hash.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

namespace {

statewright::saa::AutonomousPromotionAssessment eligible_assessment() {
  statewright::saa::AutonomousPromotionAssessment value;
  value.policy_signature = std::string(64U, 'a');
  value.candidate_ref = "internet-algorithm-candidate:sha256:" +
                        std::string(64U, 'b');
  value.probation_window_count = 2;
  value.minimum_probation_observations = 2;
  value.minimum_probation_uses = 2;
  value.maximum_canary_share_bp = 5000;
  value.automatic_demotion_predicates = {
      "BENCHMARK_REGRESSION", "INTEGRITY_FAILURE", "INVARIANT_FAILURE",
      "REPRODUCTION_FAILURE", "RETRIEVAL_PRECISION_REGRESSION",
      "SOURCE_RETRACTION"};
  value.human_approval_required = false;
  value.resulting_state = "POLICY_QUALIFIED";
  value.promotion_allowed = true;
  return value;
}

statewright::saa::ProbationPlan plan() {
  return statewright::saa::make_probation_plan(
      eligible_assessment(),
      "internet-promotion-policy:sha256:" + std::string(64U, 'c'),
      "internet-promotion-assessment:sha256:" + std::string(64U, 'd'),
      "internet-algorithm-candidate:sha256:" + std::string(64U, '9'),
      "canonical-algorithm:sha256:" + std::string(64U, 'e'));
}

std::string selected_query(const statewright::saa::ProbationPlan &value,
                           int start) {
  for (int index = start; index < start + 100000; ++index) {
    const auto query = statewright::contracts::sha256_json({{"query", index}});
    if (statewright::saa::probation_canary_selected(value, query)) {
      return query;
    }
  }
  FAIL("could not find deterministic probation canary query");
  return {};
}

statewright::saa::ProbationObservation observation(
    const statewright::saa::ProbationPlan &value, int window,
    std::string query, bool candidate_correct = true,
    bool baseline_correct = true, bool invariant_passed = true,
    bool benchmark_passed = true, bool integrity_passed = true,
    bool source_valid = true, bool reproduction_passed = true) {
  return statewright::saa::make_probation_observation(
      value, value.previous_preferred_canonical_ref, std::move(query),
      statewright::contracts::sha256_json({{"window", window}}),
      "2026-09-03T00:00:00Z", window, candidate_correct, baseline_correct,
      invariant_passed, benchmark_passed, integrity_passed, source_valid,
      reproduction_passed,
      {"egcf-evidence:sha256:" + std::string(64U, 'f')});
}

} // namespace

TEST_CASE("autonomous probation canary selection is deterministic") {
  const auto value = plan();
  const auto query = statewright::contracts::sha256_json({{"query", 17}});
  REQUIRE(statewright::saa::probation_canary_selected(value, query) ==
          statewright::saa::probation_canary_selected(value, query));
  REQUIRE(statewright::saa::probation_plan_from_json(
              statewright::saa::to_json(value))
              .plan_signature == value.plan_signature);
}

TEST_CASE("autonomous probation continues before bounded evidence completes") {
  const auto value = plan();
  const auto result = statewright::saa::assess_probation(
      value, {observation(value, 0, selected_query(value, 0))});
  REQUIRE(result.status == "PROBATION_CONTINUES");
  REQUIRE_FALSE(result.promotion_ready);
  REQUIRE_FALSE(result.demotion_required);
  REQUIRE(statewright::saa::probation_assessment_from_json(
              statewright::saa::to_json(result))
              .assessment_signature == result.assessment_signature);
}

TEST_CASE("autonomous probation promotes after all windows and uses") {
  const auto value = plan();
  const auto result = statewright::saa::assess_probation(
      value, {observation(value, 0, selected_query(value, 0)),
              observation(value, 1, selected_query(value, 100000))});
  REQUIRE(result.status == "PROBATION_PROMOTION_READY");
  REQUIRE(result.promotion_ready);
  REQUIRE_FALSE(result.demotion_required);
  REQUIRE(result.candidate_use_count == 2);
  const auto decision = statewright::saa::make_automatic_promotion_decision(
      value, result,
      "canonical-algorithm:sha256:" + std::string(64U, '1'));
  REQUIRE(decision.status == "CANONICAL_PROMOTED");
  REQUIRE_FALSE(decision.human_approval_required);
}

TEST_CASE("autonomous probation demotes on configured regression") {
  const auto value = plan();
  const auto result = statewright::saa::assess_probation(
      value, {observation(value, 0, selected_query(value, 0), false, true)});
  REQUIRE(result.status == "PROBATION_DEMOTION_REQUIRED");
  REQUIRE(result.demotion_required);
  REQUIRE_FALSE(result.promotion_ready);
  REQUIRE(result.regression_reasons ==
          std::vector<std::string>{"RETRIEVAL_PRECISION_REGRESSION"});
  const auto decision = statewright::saa::make_automatic_demotion_decision(
      value, result,
      "canonical-algorithm:sha256:" + std::string(64U, '2'),
      value.previous_preferred_canonical_ref,
      "failure-occurrence:sha256:" + std::string(64U, '3'),
      "improvement-schedule:sha256:" + std::string(64U, '4'));
  REQUIRE(decision.status == "CANONICAL_DEMOTED");
  REQUIRE_FALSE(decision.human_approval_required);
  REQUIRE(decision.restored_canonical_algorithm_ref ==
          value.previous_preferred_canonical_ref);
}
