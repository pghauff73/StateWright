#include "statewright/egcf/approval.hpp"

#include "statewright/common/error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

class ApprovalFixture final {
public:
  ApprovalFixture()
      : root(make_root()), workspace(root), store(root, STATEWRIGHT_RESOURCE_ROOT),
        approvals(store, workspace) {}

  static std::filesystem::path make_root() {
    const auto result =
        std::filesystem::temp_directory_path() /
        ("statewright-approval-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(result);
    std::ofstream(result / "README.md") << "before\n";
    return result;
  }

  ~ApprovalFixture() { std::filesystem::remove_all(root); }

  statewright::egcf::ExecutionPlan plan(std::string graph_hash) const {
    return {.compiled_workflow_id =
                "compiled-workflow:sha256:" + std::string(64U, '1'),
            .graph_hash = std::move(graph_hash),
            .source_snapshot_hash = workspace.snapshot_hash(),
            .node_order = {"read"},
            .eon_action_ids = {},
            .algorithm_digests = {std::string(64U, '2')},
            .capability_grant_id =
                "capability-grant:sha256:" + std::string(64U, '3'),
            .evidence_ids = {},
            .budget = {},
            .rollback_graph = statewright::contracts::Json::object(),
            .approval_policy = "human",
            .expires_at = "2030-01-01T00:00:00Z",
            .created_at = "2026-09-02T00:00:00Z"};
  }

  std::string register_plan(const statewright::egcf::ExecutionPlan &plan) {
    return store.register_record(
        {.object_type = "execution-plan",
         .payload = statewright::egcf::to_json(plan)},
        "egcf_execution_planned");
  }

  std::filesystem::path root;
  statewright::core::Workspace workspace;
  statewright::egcf::EgcfStore store;
  statewright::egcf::ApprovalManager approvals;
};

} // namespace

TEST_CASE("EGCF approval binds one human authorization to one exact plan") {
  ApprovalFixture fixture;
  const auto plan = fixture.plan(std::string(64U, 'a'));
  const auto plan_id = fixture.register_plan(plan);
  REQUIRE(plan_id == plan.object_id());

  const auto approval_id = fixture.approvals.authorize(
      plan_id, "Pamela", "workspace owner", {{"ticket", "SW9"}},
      "2030-01-01T00:00:00Z", true, 1);
  const auto approval = fixture.approvals.validate(plan, approval_id);
  REQUIRE(approval.plan_id == plan_id);
  REQUIRE(approval.approver == "Pamela");
  REQUIRE(approval.use_limit == 1);
  REQUIRE(approval.human);

  const auto other_plan = fixture.plan(std::string(64U, 'b'));
  REQUIRE_THROWS_AS(fixture.approvals.validate(other_plan, approval_id),
                    statewright::common::Error);

  const statewright::egcf::ExecutionRecord execution = {
      .plan_id = plan_id,
      .node_id = "__workflow__",
      .algorithm_id = {},
      .executor = "egcf",
      .inputs_hash = plan.graph_hash,
      .output = {{"success", true}},
      .status = "COMPLETED",
      .usage = statewright::contracts::Json::object(),
      .evidence_ids = {},
      .started_at = "2026-09-02T00:00:00Z",
      .completed_at = "2026-09-02T00:00:01Z",
      .simulated = false};
  REQUIRE(fixture.store
              .register_record({.object_type = "execution",
                                .payload = statewright::egcf::to_json(execution)},
                               "egcf_execution_completed")
              .starts_with("execution:sha256:"));
  REQUIRE_THROWS_AS(fixture.approvals.validate(plan, approval_id),
                    statewright::common::Error);
}

TEST_CASE("EGCF approval fails closed on non-human approval and source drift") {
  ApprovalFixture fixture;
  const auto plan = fixture.plan(std::string(64U, 'c'));
  const auto plan_id = fixture.register_plan(plan);

  REQUIRE_THROWS_AS(
      fixture.approvals.authorize(plan_id, "model", "self",
                                  statewright::contracts::Json::object(), {},
                                  false),
      statewright::common::Error);

  const auto approval_id = fixture.approvals.authorize(
      plan_id, "Pamela", "workspace owner",
      statewright::contracts::Json::object(), "2030-01-01T00:00:00Z");
  std::ofstream(fixture.root / "README.md") << "after\n";
  REQUIRE_THROWS_AS(fixture.approvals.validate(plan, approval_id),
                    statewright::common::Error);

  REQUIRE_THROWS_AS(fixture.approvals.authorize(
                        plan_id, "Pamela", "workspace owner"),
                    statewright::common::Error);
}
