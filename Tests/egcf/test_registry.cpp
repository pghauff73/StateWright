#include "statewright/common/error.hpp"
#include "statewright/egcf/registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

namespace {

statewright::contracts::Json load_fixtures() {
  std::ifstream input(STATEWRIGHT_ORACLE_FIXTURES);
  REQUIRE(input.is_open());
  return statewright::contracts::Json::parse(input);
}

} // namespace

TEST_CASE("EGCF command resources are fully manifest verified") {
  const statewright::egcf::CommandRegistry registry(STATEWRIGHT_RESOURCE_ROOT);
  REQUIRE(registry.resource_receipt().verified_files == 45U);
  REQUIRE(registry.definitions().size() == 188U);
}

TEST_CASE("EGCF command definitions match the frozen Python oracle") {
  const statewright::egcf::CommandRegistry registry(STATEWRIGHT_RESOURCE_ROOT);
  const auto &definition = registry.resolve_exact("algorithm.search@1");
  const auto fixture = load_fixtures().at("egcf_command_case");
  REQUIRE(statewright::egcf::to_json(definition) == fixture.at("definition"));
  REQUIRE(definition.object_id() == fixture.at("object_id").get<std::string>());
  REQUIRE(registry.describe("algorithm.search") == fixture.at("description"));
}

TEST_CASE("EGCF execution resolution forbids floating versions") {
  const statewright::egcf::CommandRegistry registry(STATEWRIGHT_RESOURCE_ROOT);
  REQUIRE_THROWS_AS(registry.resolve_exact("algorithm.search"),
                    statewright::common::Error);
  REQUIRE(registry.resolve_for_discovery("algorithm.search").command_id() ==
          "algorithm.search@1");
  REQUIRE_THROWS_AS(registry.resolve_exact("algorithm.search@2"),
                    statewright::common::Error);
}

TEST_CASE("EGCF algorithm selection is deterministic and explainable") {
  const statewright::egcf::CommandRegistry commands(STATEWRIGHT_RESOURCE_ROOT);
  statewright::egcf::AlgorithmRegistry algorithms(commands);
  const auto &command = commands.resolve_exact("algorithm.search@1");
  statewright::egcf::AlgorithmDefinition strong{
      .name = "builtin.algorithm.search",
      .version = 1,
      .implementation_kind = "builtin",
      .implementation_ref = "builtin:algorithm.search",
      .implementation_digest = std::string(64U, 'a'),
      .command_ids = {command.command_id()},
      .input_schema = command.input_schema,
      .output_schema = command.output_schema,
      .applicability = {{"platform", "linux"}, {"resource_cost", 1}},
      .capability_requirements = {"registry.read"},
      .capability_level = "C0",
      .risk_floor = "L0",
      .rollback_class = "exact",
      .invariants = command.invariants,
      .evidence_requirements = command.evidence_requirements,
      .qualification_policy = {{"contextual", true},
                               {"tests_required", true}},
      .owner = "statewright",
      .provenance = {{"catalog", "commands-v1"}},
      .status = "QUALIFIED",
      .known_failures = {}};
  const auto algorithm_object_id = algorithms.register_algorithm(strong);
  const auto qualification_object_id = algorithms.register_qualification(
      {.algorithm_id = strong.algorithm_id(),
       .algorithm_digest = strong.implementation_digest,
       .context = {{"platform", "linux"}},
       .context_hash = {},
       .evidence_ids = {"evidence:sha256:" + std::string(64U, '1')},
       .tests = {{{"name", "contract"}, {"success", true}}},
       .benchmarks = statewright::contracts::Json::array(),
       .known_failures = {},
       .status = "QUALIFIED",
       .qualified_by = "deterministic-test",
       .created_at = "2026-09-02T00:00:00Z",
       .expires_at = "2030-01-01T00:00:00Z"});
  REQUIRE(algorithm_object_id.starts_with("algorithm-definition:sha256:"));
  REQUIRE(qualification_object_id.starts_with("qualification:sha256:"));

  const statewright::egcf::SelectionEngine selector(algorithms);
  const auto decision = selector.select(
      command.command_id(), {{"platform", "linux"}, {"resource_cost", 1}}, "C0",
      {"registry.read"}, command.invariants, "2026-09-02T00:00:00Z");
  REQUIRE(decision.selected_algorithm_id == strong.algorithm_id());
  REQUIRE(decision.candidates.size() == 1U);
  REQUIRE(decision.excluded.empty());
  REQUIRE_FALSE(decision.evidence_ids.empty());
}

TEST_CASE("EGCF selection records complete exclusion reasons") {
  const statewright::egcf::CommandRegistry commands(STATEWRIGHT_RESOURCE_ROOT);
  statewright::egcf::AlgorithmRegistry algorithms(commands);
  const auto &command = commands.resolve_exact("eon.execute@1");
  statewright::egcf::AlgorithmDefinition proposed{
      .name = "reference.unsafe",
      .version = 1,
      .implementation_kind = "reference",
      .implementation_ref = "reference:reviewed-source",
      .implementation_digest = std::string(64U, 'b'),
      .command_ids = {command.command_id()},
      .input_schema = command.input_schema,
      .output_schema = command.output_schema,
      .applicability = {{"platform", "linux"}},
      .capability_requirements = {"filesystem.write"},
      .capability_level = "C3",
      .risk_floor = "L2",
      .rollback_class = "irreversible",
      .invariants = command.invariants,
      .evidence_requirements = command.evidence_requirements,
      .qualification_policy = {{"tests_required", true}},
      .owner = "external",
      .provenance = {{"source", "review"}},
      .status = "PROPOSED",
      .known_failures = {"not qualified"}};
  const auto proposed_object_id = algorithms.register_algorithm(proposed);
  REQUIRE(proposed_object_id.starts_with("algorithm-definition:sha256:"));
  const statewright::egcf::SelectionEngine selector(algorithms);
  REQUIRE_THROWS_AS(
      selector.select(command.command_id(), {{"platform", "linux"}}, "C0",
                      {}, {}, "2026-09-02T00:00:00Z"),
      statewright::common::Error);
}
