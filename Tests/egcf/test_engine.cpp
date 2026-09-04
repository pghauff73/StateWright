#include "statewright/egcf/engine.hpp"

#include "statewright/common/error.hpp"
#include "statewright/core/file_io.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {

class EngineFixture final {
public:
  EngineFixture() : root(make_root()), workspace(root) {}

  static std::filesystem::path make_root() {
    const auto result =
        std::filesystem::temp_directory_path() /
        ("statewright-engine-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(result / "src");
    std::ofstream(result / "README.md") << "before\n";
    std::ofstream(result / "src/main.cpp") << "return 1;\n";
    return result;
  }

  ~EngineFixture() { std::filesystem::remove_all(root); }

  statewright::core::AuthorityManifest
  authority(std::string ceiling,
            std::vector<std::string> semantic_capabilities,
            bool read_only) const {
    statewright::core::AuthorityManifest result;
    result.task_id = "engine-test";
    result.goal = "Execute a governed workflow";
    result.source_snapshot_hash = workspace.snapshot_hash();
    result.allowed_paths = {"README.md", "src/**"};
    result.forbidden_paths = {".ourd-agent/**"};
    result.read_capabilities = {"filesystem.read"};
    result.semantic_capability_ceiling = std::move(ceiling);
    result.semantic_capabilities = std::move(semantic_capabilities);
    result.operator_name = "Pamela";
    result.read_only = read_only;
    if (!read_only) {
      result.allow_l1_auto_apply = true;
      result.allow_interactive_l2 = true;
    }
    return statewright::core::finalize_authority(std::move(result));
  }

  std::filesystem::path root;
  statewright::core::Workspace workspace;
};

statewright::core::AuthorityManifest
priority_handler_authority(const EngineFixture &fixture) {
  return fixture.authority(
      "C2", {"analysis.experiment", "analysis.reason", "evidence.analyse",
              "filesystem.read", "governance.read", "registry.read",
              "simulation.run", "workflow.plan"},
      true);
}
} // namespace

TEST_CASE("EGCF engine executes and restart-rolls back one exact C3 plan") {
  EngineFixture fixture;
  const auto authority = fixture.authority(
      "C3", {"filesystem.read", "filesystem.write", "process.execute"},
      false);
  std::string plan_id;
  std::string approval_id;
  {
    statewright::egcf::EgcfEngine engine(fixture.root,
                                         STATEWRIGHT_RESOURCE_ROOT, authority);
    const statewright::egcf::WorkflowDefinition workflow = {
        .name = "write-readme",
        .version = 1,
        .parameters = statewright::contracts::Json::object(),
        .nodes = {{.node_id = "write",
                   .command_id = "eon.execute@1",
                   .inputs =
                       {{"changes",
                         {{{"type", "replace"},
                           {"path", "README.md"},
                           {"old", "before"},
                           {"new", "after"}}}}},
                   .depends_on = {},
                   .when = statewright::contracts::Json::object(),
                   .retry_limit = 0,
                   .checkpoint = false}},
        .outputs = statewright::contracts::Json::object(),
        .description = "First governed mutation slice"};
    const auto compiled = engine.compile(workflow);
    const auto plan = engine.create_execution_plan(compiled, true);
    plan_id = plan.object_id();
    REQUIRE(plan.eon_action_ids.size() == 1U);
    REQUIRE(statewright::core::read_text(fixture.root / "README.md") ==
            "before\n");
    REQUIRE_THROWS_AS(engine.execute_plan(plan_id),
                      statewright::common::Error);
    REQUIRE_THROWS_AS(
        engine.execute_plan(
            plan_id, "approval:sha256:" + std::string(64U, 'f')),
        statewright::common::Error);

    approval_id = engine.authorize(plan_id, "Pamela", "workspace owner",
                                   {{"ticket", "SW9"}},
                                   "2030-01-01T00:00:00Z");
    const auto execution = engine.execute_plan(plan_id, approval_id);
    REQUIRE(execution.at("status") == "COMPLETED");
    REQUIRE(execution.at("execution_ids").size() == 1U);
    const auto artifacts = engine.evidence().artifacts(plan_id);
    REQUIRE(artifacts.size() == 3U);
    for (const auto &artifact : artifacts) {
      REQUIRE(artifact.requirement_ids.size() == 1U);
      REQUIRE(artifact.requirement_ids.front().starts_with(
          "evidence-requirement:sha256:"));
    }
    REQUIRE(engine.evidence().coverage(plan_id).at("missing_mandatory").empty());
    const auto verification = engine.verify_plan(plan_id);
    REQUIRE(verification.at("status") == "VERIFIED");
    REQUIRE(verification.at("verification_evidence_id")
                .get<std::string>()
                .starts_with("egcf-evidence:sha256:"));
    REQUIRE(statewright::core::read_text(fixture.root / "README.md") ==
            "after\n");
  }

  statewright::egcf::EgcfEngine recovery(
      fixture.root, STATEWRIGHT_RESOURCE_ROOT, authority, "user", true);
  const auto rollback = recovery.rollback_plan(plan_id);
  REQUIRE(rollback.at("status") == "ROLLED_BACK");
  REQUIRE(rollback.at("transaction_ids").size() == 1U);
  REQUIRE(statewright::core::read_text(fixture.root / "README.md") ==
          "before\n");
  REQUIRE(fixture.workspace.snapshot_hash() == authority.source_snapshot_hash);
}

TEST_CASE("EGCF engine persists checkpoint pause and resumes after restart") {
  EngineFixture fixture;
  const auto authority = fixture.authority(
      "C1", {"analysis.reason", "filesystem.read", "workflow.plan"}, true);
  std::string plan_id;
  {
    statewright::egcf::EgcfEngine engine(fixture.root,
                                         STATEWRIGHT_RESOURCE_ROOT, authority);
    const statewright::egcf::WorkflowDefinition workflow = {
        .name = "checkpoint-resume",
        .version = 1,
        .parameters = statewright::contracts::Json::object(),
        .nodes = {
            {.node_id = "first",
             .command_id = "hrt.interpret@1",
             .inputs = {{"text", "hello"}},
             .depends_on = {},
             .when = statewright::contracts::Json::object(),
             .retry_limit = 0,
             .checkpoint = true},
            {.node_id = "second",
             .command_id = "hrt.summary@1",
             .inputs = {{"text", {{"$from", "first"},
                                    {"path", {"result", "objective"}}}}},
             .depends_on = {"first"},
             .when = {{"value", {{"$from", "first"},
                                   {"path", {"result", "objective"}}}},
                      {"equals", "hello"}},
             .retry_limit = 0,
             .checkpoint = false}},
        .outputs = statewright::contracts::Json::object(),
        .description = "Restart-safe checkpoint test"};
    const auto compiled = engine.compile(workflow);
    const auto plan = engine.create_execution_plan(compiled, false);
    plan_id = plan.object_id();
    const auto paused = engine.execute_plan(plan_id, {}, true, false);
    REQUIRE(paused.at("status") == "PAUSED");
    REQUIRE(paused.at("checkpoint_node_id") == "first");
    REQUIRE_THROWS_AS(engine.execute_plan(plan_id),
                      statewright::common::Error);
  }
  statewright::egcf::EgcfEngine resumed(
      fixture.root, STATEWRIGHT_RESOURCE_ROOT, authority, "user", true);
  const auto monitored =
      resumed.invoke("workflow.monitor", {{"plan_id", plan_id}});
  REQUIRE(monitored.at("outputs").at(0).at("result").at("executions").size() ==
          2U);
  const auto resume_request =
      resumed.invoke("workflow.resume", {{"plan_id", plan_id}});
  REQUIRE(resume_request.at("outputs").at(0).at("result").at("status") ==
          "RESUME_REQUESTED");
  const auto execution = resumed.execute_plan(plan_id, {}, false, true);
  REQUIRE(execution.at("status") == "COMPLETED");
  REQUIRE(execution.at("outputs").size() == 2U);
  REQUIRE(execution.at("outputs").at(1).at("result").at("summary") ==
          "hello");
  REQUIRE(resumed.verify_plan(plan_id).at("status") == "VERIFIED");
}

TEST_CASE("EGCF engine records immutable failure and rollback outcome") {
  EngineFixture fixture;
  const auto authority = fixture.authority(
      "C1", {"filesystem.read", "workflow.plan"}, true);
  statewright::egcf::EgcfEngine engine(fixture.root,
                                       STATEWRIGHT_RESOURCE_ROOT, authority);
  const statewright::egcf::WorkflowDefinition workflow = {
      .name = "runtime-reference-failure",
      .version = 1,
      .parameters = statewright::contracts::Json::object(),
      .nodes = {{.node_id = "interpret",
                 .command_id = "hrt.interpret@1",
                 .inputs = {{"text", "hello"}},
                 .depends_on = {},
                 .when = {{"value", {{"$from", "interpret"},
                                      {"path", {"result", "missing"}}}},
                          {"truthy", true}},
                 .retry_limit = 0,
                 .checkpoint = false}},
      .outputs = statewright::contracts::Json::object(),
      .description = "Force deterministic execution failure"};
  REQUIRE_THROWS_AS(engine.compile(workflow), statewright::common::Error);

  const statewright::egcf::WorkflowDefinition failing = {
      .name = "missing-workflow-object",
      .version = 1,
      .parameters = statewright::contracts::Json::object(),
      .nodes = {{.node_id = "compile",
                 .command_id = "workflow.compile@1",
                 .inputs = {{"workflow_definition_id",
                             "workflow-definition:sha256:" +
                                 std::string(64U, 'f')}},
                 .depends_on = {},
                 .when = statewright::contracts::Json::object(),
                 .retry_limit = 0,
                 .checkpoint = false}},
      .outputs = statewright::contracts::Json::object(),
      .description = "Force a missing canonical object"};
  const auto plan = engine.create_execution_plan(engine.compile(failing), false);
  REQUIRE_THROWS_AS(engine.execute_plan(plan.object_id()),
                    statewright::common::Error);
  const auto failures = engine.store().list("failure");
  REQUIRE(failures.size() == 1U);
  const auto failure =
      statewright::egcf::failure_from_json(failures.front().payload);
  REQUIRE(failure.subject_id == plan.object_id());
  REQUIRE(failure.status == "FAILED");
  REQUIRE(engine.store().list("rollback").size() == 1U);
}

TEST_CASE("EGCF engine binds simulation and read-only execution to plans") {
  EngineFixture fixture;
  const auto authority = fixture.authority(
      "C2", {"filesystem.read", "simulation.run"}, true);
  statewright::egcf::EgcfEngine engine(fixture.root, STATEWRIGHT_RESOURCE_ROOT,
                                       authority);

  const statewright::egcf::WorkflowDefinition migration = {
      .name = "simulate-migration",
      .version = 1,
      .parameters = statewright::contracts::Json::object(),
      .nodes = {{.node_id = "simulate",
                 .command_id = "simulate.migration@1",
                 .inputs =
                     {{"before", {{"version", 1}}},
                      {"operations",
                       {{{"operation", "set"},
                         {"key", "version"},
                         {"value", 2}}}}},
                 .depends_on = {},
                 .when = statewright::contracts::Json::object(),
                 .retry_limit = 0,
                 .checkpoint = false}},
      .outputs = statewright::contracts::Json::object(),
      .description = "Simulate a dictionary migration"};
  statewright::egcf::CommandContext simulation_context;
  simulation_context.simulate = true;
  const auto simulation_compiled = engine.compile(migration, simulation_context);
  const auto simulation_plan =
      engine.create_execution_plan(simulation_compiled, false);
  const auto simulation = engine.execute_plan(simulation_plan.object_id());
  REQUIRE(simulation.at("status") == "SIMULATED");
  REQUIRE(simulation.at("plan_id") == simulation_plan.object_id());
  REQUIRE(simulation.at("outputs").at(0).at("result").at("after").at(
              "version") == 2);

  const statewright::egcf::WorkflowDefinition metrics = {
      .name = "repository-metrics",
      .version = 1,
      .parameters = statewright::contracts::Json::object(),
      .nodes = {{.node_id = "metrics",
                 .command_id = "repo.metrics@1",
                 .inputs = statewright::contracts::Json::object(),
                 .depends_on = {},
                 .when = statewright::contracts::Json::object(),
                 .retry_limit = 0,
                 .checkpoint = false}},
      .outputs = statewright::contracts::Json::object(),
      .description = "Measure the repository"};
  const auto metrics_compiled = engine.compile(metrics);
  const auto metrics_plan = engine.create_execution_plan(metrics_compiled, false);
  const auto measured = engine.execute_plan(metrics_plan.object_id());
  REQUIRE(measured.at("status") == "COMPLETED");
  REQUIRE(measured.at("outputs").at(0).at("result").at("files") == 2);
  REQUIRE(fixture.workspace.snapshot_hash() == authority.source_snapshot_hash);
}

TEST_CASE("EGCF engine executes IEPS and assurance priority handlers") {
  EngineFixture fixture;
  statewright::egcf::EgcfEngine engine(fixture.root, STATEWRIGHT_RESOURCE_ROOT,
                                       priority_handler_authority(fixture));

  const auto generated = engine.invoke(
      "ieps.generate",
      {{"subject_id", "priority"},
       {"requirements",
        {{{"name", "deterministic result"},
          {"category", "test"},
          {"oracle", "unit"},
          {"independence_group", "unit"}}}}});
  const auto requirement_id = generated.at("outputs")
                                  .at(0)
                                  .at("result")
                                  .at("requirement_ids")
                                  .at(0)
                                  .get<std::string>();
  static_cast<void>(engine.evidence().collect(
      {.subject_id = "priority",
       .content = {{"passed", true}},
       .category = "test",
       .producer = "deterministic-unit",
       .method = "unit",
       .source_snapshot_hash = fixture.workspace.snapshot_hash(),
       .oracle = "unit",
       .requirement_ids = {requirement_id},
       .success = true,
       .independence_group = "unit"}));

  const auto qualified =
      engine.invoke("ieps.qualify", {{"subject_id", "priority"}});
  REQUIRE(qualified.at("outputs").at(0).at("result").at("qualified") == true);
  REQUIRE(engine.invoke("evidence.confidence",
                        {{"subject_id", "priority"}})
              .at("ok") == true);
  const auto assurance = engine.invoke(
      "assurance.generate",
      {{"subject_id", "priority"},
       {"capability_facts", {{"authorized", true}}},
       {"approval_facts", {{"satisfied", false}}},
       {"rollback_argument", {{"required", false}, {"covered", true}}}});
  REQUIRE(assurance.at("outputs").at(0).at("result").at("approval_facts").at(
              "satisfied") == false);
}

TEST_CASE("EGCF engine executes algorithm registry priority handlers") {
  EngineFixture fixture;
  statewright::egcf::EgcfEngine engine(fixture.root, STATEWRIGHT_RESOURCE_ROOT,
                                       priority_handler_authority(fixture));

  REQUIRE(engine.algorithms().algorithms().size() == 181U);
  const auto searched =
      engine.invoke("algorithm.search", {{"command_id", "repo.metrics"}});
  REQUIRE(searched.at("outputs").at(0).at("result").at("algorithms").size() ==
          1U);
  const auto selected =
      engine.invoke("algorithm.select", {{"command_id", "repo.metrics"}});
  REQUIRE(selected.at("ok") == true);
  const auto selection_id = selected.at("outputs")
                                .at(0)
                                .at("result")
                                .at("selection_id")
                                .get<std::string>();
  REQUIRE(engine.invoke("algorithm.explain", {{"selection_id", selection_id}})
              .at("outputs")
              .at(0)
              .at("result")
              .at("selection_id") == selection_id);
  REQUIRE(engine
              .invoke("algorithm.compare",
                      {{"algorithm_ids", {"builtin.repo.metrics@1"}}})
              .at("outputs")
              .at(0)
              .at("result")
              .at("algorithms")
              .at(0)
              .at("qualification_count") == 1U);
}

TEST_CASE("EGCF engine executes governance and experiment priority handlers") {
  EngineFixture fixture;
  statewright::egcf::EgcfEngine engine(fixture.root, STATEWRIGHT_RESOURCE_ROOT,
                                       priority_handler_authority(fixture));

  const auto generic = engine.invoke("repo.history");
  REQUIRE(generic.at("outputs").at(0).at("result").at("status") ==
          "READ_ONLY_RESULT");
  REQUIRE(generic.at("outputs").at(0).at("read_only") == true);
  REQUIRE(engine.invoke("invariant.discover",
                        {{"statements",
                          {"unrelated files remain unchanged"}}})
              .at("ok") == true);
  REQUIRE(engine.invoke("decision.conflicts").at("ok") == true);
  const auto design = engine.invoke(
      "experiment.covering",
      {{"parameters",
        {{"mode", {"strict", "lenient"}}, {"parser", {"a", "b"}}}}});
  REQUIRE(design.at("outputs").at(0).at("result").at("runs") == 4);
}

TEST_CASE("EGCF engine executes simulation and classification handlers") {
  EngineFixture fixture;
  statewright::egcf::EgcfEngine engine(fixture.root, STATEWRIGHT_RESOURCE_ROOT,
                                       priority_handler_authority(fixture));

  statewright::egcf::CommandContext simulation_context;
  simulation_context.simulate = true;
  REQUIRE(engine
              .invoke("simulate.migration",
                      {{"before", {{"version", 1}}},
                       {"operations",
                        {{{"operation", "set"},
                          {"key", "version"},
                          {"value", 2}}}}},
                      simulation_context)
              .at("status") == "SIMULATED");
  REQUIRE(engine.invoke("cfel.classify",
                        {{"expected", "pass"},
                         {"observed", "failed test exception"}})
              .at("outputs")
              .at(0)
              .at("result")
              .at("categories")
              .at(0) == "execution");
}

TEST_CASE("EGCF engine executes workflow and reasoning priority handlers") {
  EngineFixture fixture;
  statewright::egcf::EgcfEngine engine(fixture.root, STATEWRIGHT_RESOURCE_ROOT,
                                       priority_handler_authority(fixture));

  REQUIRE(engine
              .invoke("workflow.compile",
                      {{"name", "nested"},
                       {"nodes",
                        {{{"node_id", "inspect"},
                          {"command_id", "repo.metrics@1"},
                          {"inputs", statewright::contracts::Json::object()}}}},
                       {"outputs", statewright::contracts::Json::object()}})
              .at("ok") == true);
  const auto hypotheses = engine.invoke(
      "hrt.claims@1",
      {{"assumptions", {"fixture evidence remains bounded"}},
       {"hypotheses", {"parser defect", "fixture defect"}},
       {"text", "Explain the regression."}});
  REQUIRE(hypotheses.at("outputs").at(0).at("result").at("status") ==
          "ADVISORY_PROPOSAL");
  REQUIRE_FALSE(hypotheses.at("outputs")
                    .at(0)
                    .at("result")
                    .at("authoritative")
                    .get<bool>());
  REQUIRE(hypotheses.at("outputs")
              .at(0)
              .at("result")
              .at("evidence_requirement_ids")
              .size() == 2U);
}

TEST_CASE("EGCF engine produces stable replay identities") {
  EngineFixture fixture;
  statewright::egcf::EgcfEngine engine(fixture.root, STATEWRIGHT_RESOURCE_ROOT,
                                       priority_handler_authority(fixture));

  const auto metrics = engine.invoke("repo.metrics");
  const auto replay = engine.replay(
      metrics.at("execution_plan_id").get<std::string>(),
      statewright::egcf::CommandContext{});
  REQUIRE(replay.at("same_graph") == true);
  REQUIRE(replay.at("same_snapshot") == true);
  REQUIRE(replay.at("replayed_plan_id") != replay.at("historical_plan_id"));
}
