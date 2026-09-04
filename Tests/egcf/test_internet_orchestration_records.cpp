#include "statewright/egcf/internet_orchestration_records.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("internet orchestration records are canonical and round-trip") {
  using namespace statewright;

  egcf::InternetDirectedAction action;
  action.kind = egcf::InternetDirectedActionKind::schedule_fetch;
  action.subject_id = "internet-watch:sha256:" + std::string(64U, 'a');
  action.subject_type = "internet-watch";
  action.input_ids = {action.subject_id};
  action.parameters = {{"job", {{"schema_version", 1}}}};
  action.not_before = "2026-09-04T00:00:00Z";
  action.deadline = "2026-09-04T00:05:00Z";
  action.priority_bp = 5000;
  action.cost_bp = 100;
  action.risk_bp = 100;
  action.cpu_unit_budget = 1U;
  action = egcf::canonical_internet_directed_action(std::move(action));
  REQUIRE(egcf::internet_directed_action_from_json(egcf::to_json(action))
              .action_key == action.action_key);

  egcf::InternetImprovementPlan plan;
  plan.cycle_key = "2026-09-04T00:00:00Z";
  plan.baseline_event_head = "GENESIS";
  plan.projection_digest = std::string(64U, 'b');
  plan.director_policy = {{"maximum_actions", 1}};
  plan.planned_at = "2026-09-04T00:00:00Z";
  plan.director_version =
      std::string(egcf::internet_orchestration_records_version);
  plan.actions = {action};
  plan = egcf::canonical_internet_improvement_plan(std::move(plan));
  REQUIRE(egcf::internet_improvement_plan_from_json(egcf::to_json(plan))
              .object_id() == plan.object_id());

  egcf::InternetExperimentProtocol protocol;
  protocol.protocol_version = "fixture-v1";
  protocol.applicable_candidate_statuses = {"VALIDATION_READY"};
  protocol.baseline_ref =
      "canonical-algorithm:sha256:" + std::string(64U, 'c');
  protocol.baseline_saa_ir = {{"nodes", contracts::Json::array()}};
  protocol.trial_groups =
      {{{"independence_group", "fixture-a"},
        {"deterministic_seed", 1},
        {"inputs", {1}},
        {"expected_outputs", {1}}}};
  protocol.valid_from = "2026-09-04T00:00:00Z";
  protocol.source_provenance = {{"producer", "fixture"}};
  protocol = egcf::canonical_internet_experiment_protocol(
      std::move(protocol));
  REQUIRE(egcf::internet_experiment_protocol_from_json(
              egcf::to_json(protocol))
              .object_id() == protocol.object_id());
}
