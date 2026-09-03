#include "statewright/saa/autonomous_promotion_policy.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

statewright::saa::AutonomousPromotionPolicy policy() {
  using namespace statewright::saa;
  AutonomousPromotionPolicy value;
  value.domain_scopes = {"*"};
  value.allowed_candidate_classes = {"NOVEL_CANDIDATE"};
  value.prohibited_primitives = {"INVOKE"};
  value.prohibited_capability_classes = {
      "COMMAND_EXECUTION", "FILESYSTEM_MUTATION", "NETWORK_IO",
      "PROCESS_EXECUTION"};
  value.automatic_demotion_predicates = {
      "BENCHMARK_REGRESSION", "INVARIANT_FAILURE",
      "KNOWLEDGE_INTEGRITY_REGRESSION", "SOURCE_RETRACTION"};
  return canonical_autonomous_promotion_policy(std::move(value));
}

statewright::saa::AutonomousPromotionInputs inputs() {
  using namespace statewright::saa;
  AutonomousPromotionInputs value;
  value.candidate_ref =
      "internet-algorithm-candidate:sha256:" + std::string(64U, 'a');
  value.candidate_status = "EXPERIMENT_QUALIFIED";
  value.candidate_class = "NOVEL_CANDIDATE";
  value.candidate_domain = "example.com";
  value.candidate_primitives = {"IDENTITY"};
  value.source_policy_assessment_ref =
      "internet-policy-assessment:sha256:" + std::string(64U, 'b');
  value.snapshot_ref =
      "internet-source-snapshot:sha256:" + std::string(64U, 'c');
  value.retrieval_receipt_ref =
      "internet-retrieval-receipt:sha256:" + std::string(64U, 'd');
  value.experiment_qualification_ref =
      "internet-experiment-qualification:sha256:" + std::string(64U, 'e');
  value.source_policy_passed = true;
  value.snapshot_integrity_passed = true;
  value.independent_source_groups = 1;
  value.semantic_strength = "DETERMINISTIC_SOURCE_BOUND";
  value.mathematical_strength = "EXACT_STRUCTURAL";
  value.existing_knowledge_search_complete = true;
  value.experiment_qualified = true;
  value.independent_experiment_groups = 2;
  value.benchmark_gate =
      {.candidate_ref = value.candidate_ref,
       .profile_signature = std::string(64U, '1'),
       .policy_signature = std::string(64U, '2'),
       .evidence_requirement_coverage_bp = 10000,
       .independence_groups = {"a", "b"},
       .threshold_failures = {},
       .independent_review = true,
       .status = "OIEC_BENCH_PROMOTION_GATE_PASSED",
       .canonical_promotion_eligible = true,
       .assessment_signature = std::string(64U, '3')};
  value.integrity_trajectory =
      {.snapshot_signatures = {std::string(64U, '4')},
       .latest_generation = 1,
       .status = "KNOWLEDGE_INTEGRITY_QUALIFIED_STABLE",
       .policy_violations = {},
       .degraded_dimensions = {},
       .improved_dimensions = {},
       .knowledge_integrity_qualified = true,
       .trajectory_signature = std::string(64U, '5')};
  value.invariants_passed = true;
  value.probation_plan_valid = true;
  value.demotion_path_valid = true;
  return value;
}

} // namespace

TEST_CASE("autonomous promotion policy is deterministic and approval free") {
  using namespace statewright::saa;
  const auto first = evaluate_autonomous_promotion(policy(), inputs());
  const auto second = evaluate_autonomous_promotion(policy(), inputs());
  REQUIRE(first.promotion_allowed);
  REQUIRE(first.resulting_state == "POLICY_QUALIFIED");
  REQUIRE_FALSE(first.human_approval_required);
  REQUIRE(first.blocking_reasons.empty());
  REQUIRE(first.decision_signature == second.decision_signature);
  REQUIRE(to_json(first) == to_json(second));
}

TEST_CASE("autonomous promotion policy blocks every failed hard predicate") {
  using namespace statewright::saa;
  auto value = inputs();
  value.invariants_passed = false;
  const auto assessment = evaluate_autonomous_promotion(policy(), value);
  REQUIRE_FALSE(assessment.promotion_allowed);
  REQUIRE(assessment.resulting_state == "EXPERIMENT_QUALIFIED");
  REQUIRE(assessment.blocking_reasons ==
          std::vector<std::string>{"INVARIANTS"});
  REQUIRE_FALSE(assessment.human_approval_required);
}

TEST_CASE("autonomous promotion policy rejects prohibited capabilities") {
  using namespace statewright::saa;
  auto value = inputs();
  value.candidate_primitives = {"IDENTITY", "INVOKE"};
  value.candidate_capability_classes = {"COMMAND_EXECUTION"};
  const auto assessment = evaluate_autonomous_promotion(policy(), value);
  REQUIRE_FALSE(assessment.promotion_allowed);
  REQUIRE(assessment.blocking_reasons == std::vector<std::string>{"SCOPE"});
}
