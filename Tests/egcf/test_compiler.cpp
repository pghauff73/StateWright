#include "statewright/egcf/compiler.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

class CompilerFixture final {
public:
  CompilerFixture()
      : root(make_root()),
        workspace(root), store(root, STATEWRIGHT_RESOURCE_ROOT),
        commands(STATEWRIGHT_RESOURCE_ROOT), algorithms(commands) {}

  static std::filesystem::path make_root() {
    const auto result =
        std::filesystem::temp_directory_path() /
        ("statewright-compiler-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(result / "src");
    std::ofstream(result / "README.md") << "value = 1\n";
    std::ofstream(result / "src/parser.cpp") << "int parse();\n";
    return result;
  }

  ~CompilerFixture() { std::filesystem::remove_all(root); }

  statewright::core::AuthorityManifest authority(
      std::string level, std::vector<std::string> capabilities) const {
    auto result = statewright::core::read_only_authority(workspace);
    result.task_id = "compiler-test";
    result.goal = "Compile governed workflows";
    result.allowed_paths = {"README.md", "src/**"};
    result.semantic_capability_ceiling = std::move(level);
    result.semantic_capabilities = std::move(capabilities);
    if (result.semantic_capability_ceiling == "C3") {
      result.read_only = false;
      result.allow_l1_auto_apply = true;
      result.allow_interactive_l2 = true;
    }
    result = statewright::core::finalize_authority(std::move(result));
    statewright::core::validate_authority(result, workspace);
    return result;
  }

  void qualify(std::string_view command_id) {
    const auto &command = commands.resolve_exact(command_id);
    const auto capabilities = command.capability_query.value(
        "facets", std::vector<std::string>{});
    const auto level = command.capability_query.value("level", "C0");
    const statewright::egcf::AlgorithmDefinition algorithm = {
        .name = "builtin." + command.namespace_name + "." + command.name,
        .version = 1,
        .implementation_kind =
            level == "C3" ? "eon" : "builtin",
        .implementation_ref = "builtin:" + command.command_id(),
        .implementation_digest =
            statewright::contracts::sha256_text(command.command_id()),
        .command_ids = {command.command_id()},
        .input_schema = command.input_schema,
        .output_schema = command.output_schema,
        .applicability = statewright::contracts::Json::object(),
        .capability_requirements = capabilities,
        .capability_level = level,
        .risk_floor = command.risk_policy,
        .rollback_class = command.rollback_policy,
        .invariants = command.invariants,
        .evidence_requirements = command.evidence_requirements,
        .qualification_policy = {{"contextual", true}},
        .owner = level == "C3" ? "egcf-core" : "statewright-test",
        .provenance = {{"fixture", true}},
        .status = "QUALIFIED",
        .known_failures = {}};
    if (level == "C3") {
      static_cast<void>(algorithms.register_core_algorithm(algorithm));
    } else {
      static_cast<void>(algorithms.register_algorithm(algorithm));
    }
    static_cast<void>(algorithms.register_qualification(
        {.algorithm_id = algorithm.algorithm_id(),
         .algorithm_digest = algorithm.implementation_digest,
         .context = statewright::contracts::Json::object(),
         .context_hash = {},
         .evidence_ids = {"evidence:sha256:" + std::string(64U, '1')},
         .tests = {{{"name", "contract"}, {"success", true}}},
         .benchmarks = statewright::contracts::Json::array(),
         .known_failures = {},
         .status = "QUALIFIED",
         .qualified_by = "deterministic-compiler-test",
         .created_at = "2026-09-02T00:00:00Z",
         .expires_at = "2030-01-01T00:00:00Z"}));
  }

  std::filesystem::path root;
  statewright::core::Workspace workspace;
  statewright::egcf::EgcfStore store;
  statewright::egcf::CommandRegistry commands;
  statewright::egcf::AlgorithmRegistry algorithms;
};

} // namespace

TEST_CASE("EGCF compiler graph hash ignores receipt timestamps") {
  CompilerFixture fixture;
  fixture.qualify("repo.metrics@1");
  const auto authority = fixture.authority("C0", {"filesystem.read"});
  statewright::egcf::WorkflowCompiler compiler(
      fixture.store, fixture.workspace, fixture.commands, fixture.algorithms,
      authority);
  const statewright::egcf::WorkflowDefinition workflow = {
      .name = "stable",
      .version = 1,
      .parameters = statewright::contracts::Json::object(),
      .nodes = {{.node_id = "read",
                 .command_id = "repo.metrics@1",
                 .inputs = statewright::contracts::Json::object(),
                 .depends_on = {},
                 .when = statewright::contracts::Json::object(),
                 .retry_limit = 0,
                 .checkpoint = false}},
      .outputs = {{"result", {{"$from", "read"}}}},
      .description = {}};
  const auto first = compiler.compile(workflow);
  const auto second = compiler.compile(workflow);
  REQUIRE(first.graph_hash == second.graph_hash);
  REQUIRE(first.object_id().starts_with("compiled-workflow:sha256:"));
}

TEST_CASE("EGCF compiler requires references to follow dependencies") {
  CompilerFixture fixture;
  fixture.qualify("hrt.interpret@1");
  fixture.qualify("hrt.summary@1");
  const auto authority = fixture.authority("C1", {"analysis.reason"});
  statewright::egcf::WorkflowCompiler compiler(
      fixture.store, fixture.workspace, fixture.commands, fixture.algorithms,
      authority);
  const statewright::egcf::WorkflowDefinition workflow = {
      .name = "bad-reference",
      .version = 1,
      .parameters = statewright::contracts::Json::object(),
      .nodes =
          {{.node_id = "first",
            .command_id = "hrt.interpret@1",
            .inputs = {{"text", "hello"}},
            .depends_on = {},
            .when = statewright::contracts::Json::object(),
            .retry_limit = 0,
            .checkpoint = false},
           {.node_id = "second",
            .command_id = "hrt.summary@1",
            .inputs = {{"text",
                        {{"$from", "first"},
                         {"path", {"result", "objective"}}}}},
            .depends_on = {},
            .when = statewright::contracts::Json::object(),
            .retry_limit = 0,
            .checkpoint = false}},
      .outputs = statewright::contracts::Json::object(),
      .description = {}};
  REQUIRE_THROWS_AS(compiler.compile(workflow), statewright::common::Error);
}

TEST_CASE("EGCF compiler rejects cycles and parallel mutation conflicts") {
  CompilerFixture fixture;
  fixture.qualify("hrt.summary@1");
  fixture.qualify("eon.execute@1");
  const auto authority = fixture.authority(
      "C3", {"analysis.reason", "filesystem.write", "process.execute"});
  statewright::egcf::WorkflowCompiler compiler(
      fixture.store, fixture.workspace, fixture.commands, fixture.algorithms,
      authority);
  const statewright::egcf::WorkflowDefinition cyclic = {
      .name = "cycle",
      .version = 1,
      .parameters = statewright::contracts::Json::object(),
      .nodes =
          {{.node_id = "a",
            .command_id = "hrt.summary@1",
            .inputs = statewright::contracts::Json::object(),
            .depends_on = {"b"},
            .when = statewright::contracts::Json::object(),
            .retry_limit = 0,
            .checkpoint = false},
           {.node_id = "b",
            .command_id = "hrt.summary@1",
            .inputs = statewright::contracts::Json::object(),
            .depends_on = {"a"},
            .when = statewright::contracts::Json::object(),
            .retry_limit = 0,
            .checkpoint = false}},
      .outputs = statewright::contracts::Json::object(),
      .description = {}};
  REQUIRE_THROWS_AS(compiler.compile(cyclic), statewright::common::Error);

  const statewright::egcf::WorkflowDefinition conflicting = {
      .name = "conflict",
      .version = 1,
      .parameters = statewright::contracts::Json::object(),
      .nodes =
          {{.node_id = "left",
            .command_id = "eon.execute@1",
            .inputs = {{"changes",
                        {{{"type", "write"},
                          {"path", "README.md"},
                          {"content", "left"}}}}},
            .depends_on = {},
            .when = statewright::contracts::Json::object(),
            .retry_limit = 0,
            .checkpoint = false},
           {.node_id = "right",
            .command_id = "eon.execute@1",
            .inputs = {{"changes",
                        {{{"type", "write"},
                          {"path", "README.md"},
                          {"content", "right"}}}}},
            .depends_on = {},
            .when = statewright::contracts::Json::object(),
            .retry_limit = 0,
            .checkpoint = false}},
      .outputs = statewright::contracts::Json::object(),
      .description = {}};
  statewright::egcf::CommandContext strict;
  strict.strict = true;
  REQUIRE_THROWS_AS(compiler.compile(conflicting, strict),
                    statewright::common::Error);
}

TEST_CASE("EGCF compiler cannot broaden authority for mutation") {
  CompilerFixture fixture;
  fixture.qualify("eon.execute@1");
  const auto authority = fixture.authority("C1", {"analysis.reason"});
  statewright::egcf::WorkflowCompiler compiler(
      fixture.store, fixture.workspace, fixture.commands, fixture.algorithms,
      authority);
  const statewright::egcf::WorkflowDefinition workflow = {
      .name = "mutation",
      .version = 1,
      .parameters = statewright::contracts::Json::object(),
      .nodes = {{.node_id = "write",
                 .command_id = "eon.execute@1",
                 .inputs = {{"changes",
                             {{{"type", "write"},
                               {"path", "README.md"},
                               {"content", "changed"}}}}},
                 .depends_on = {},
                 .when = statewright::contracts::Json::object(),
                 .retry_limit = 0,
                 .checkpoint = false}},
      .outputs = statewright::contracts::Json::object(),
      .description = {}};
  REQUIRE_THROWS_AS(compiler.compile(workflow), statewright::common::Error);
}
