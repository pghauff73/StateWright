#include "statewright/reasoning/benchmark.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/canonical_json.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path benchmark_root() {
  return std::filesystem::path(STATEWRIGHT_RESOURCE_ROOT) / "benchmarks" /
         "reasoning";
}

[[nodiscard]] statewright::contracts::Json
read_json(const std::filesystem::path &path) {
  std::ifstream input(path);
  REQUIRE(input.is_open());
  std::ostringstream contents;
  contents << input.rdbuf();
  return statewright::contracts::parse_json(contents.str());
}

[[nodiscard]] std::string read_text(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  REQUIRE(input.is_open());
  std::ostringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

[[nodiscard]] statewright::reasoning::BenchmarkRun regenerate(
    const std::vector<statewright::reasoning::BenchmarkTask> &tasks,
    const statewright::reasoning::FixtureObservations &observations,
    const statewright::reasoning::BenchmarkRun &baseline) {
  statewright::reasoning::FixtureBenchmarkExecutor base(
      "base", observations.at("base"));
  statewright::reasoning::FixtureBenchmarkExecutor oiec(
      "oiec", observations.at("oiec"));
  statewright::reasoning::FixtureBenchmarkExecutor oiec_sr(
      "oiec_sr", observations.at("oiec_sr"));
  return statewright::reasoning::run_benchmark(
      tasks, {&base, &oiec, &oiec_sr}, baseline.generated_on,
      baseline.package_version, baseline.git_head, baseline.worktree_dirty,
      baseline.source_files, baseline.execution_mode,
      baseline.qualification_status);
}

} // namespace

TEST_CASE("reasoning benchmark baseline has exact frozen identity") {
  const auto path = benchmark_root() / "baseline-v1.json";
  const auto run = statewright::reasoning::load_benchmark_run(path);
  REQUIRE(statewright::reasoning::to_json(run) == read_json(path));
  REQUIRE_NOTHROW(statewright::reasoning::require_benchmark_run_integrity(run));
  REQUIRE(statewright::reasoning::verify_benchmark_checksum(
              path, benchmark_root() / "baseline-v1.sha256") ==
          "cb10269b5016d3bbc1a7dc31d2cd753438f9431702bfed5d38425d212e914027");

  for (std::size_t index = 0;
       index < statewright::reasoning::benchmark_system_ids.size(); ++index) {
    REQUIRE(statewright::reasoning::summarize_results(
                statewright::reasoning::benchmark_system_ids[index],
                run.results) == run.summaries[index]);
  }
}

TEST_CASE("reasoning benchmark scoring matches the frozen first result") {
  const auto task = statewright::reasoning::canonicalize_benchmark_task(
      {.schema_version = 1,
       .problem_id = "logic-001",
       .category = "logic",
       .prompt = "All governed mutations require EON. This operation has no "
                 "EON action. Is the mutation permitted?",
       .oracle = {.kind = "exact", .expected = "no"},
       .oracle_method =
           "Apply modus tollens to the stated governance rule.",
       .required_evidence_ids = {"logic:eon-required"},
       .required_counterexamples = {},
       .source_refs = {"README.md"},
       .signature = {}});
  const auto observation =
      statewright::reasoning::canonicalize_benchmark_observation(
          {.schema_version = 1,
           .problem_id = "logic-001",
           .system_id = "base",
           .answer = "no",
           .confidence_bp = 9'000,
           .evidence_ids = {"logic:eon-required"},
           .counterexamples = {},
           .token_count = 24,
           .tool_calls = 0,
           .collisions = 0,
           .retries = 0,
           .wall_time_ms = 10,
           .terminal_state = "ANSWER",
           .signature = {}});
  const auto run = statewright::reasoning::load_benchmark_run(
      benchmark_root() / "baseline-v1.json");
  REQUIRE(task.signature ==
          "19e1f5ffb9ca9b08c73335d30c519f236da292e8c53540da8254d14bf71722b5");
  REQUIRE(observation.signature ==
          "008a9c135f2459517a01b3bf0d886ab146e757205fafa373146fccf3fcce38c0");
  REQUIRE(statewright::reasoning::score_observation(task, observation) ==
          run.results.front());
}

TEST_CASE("fixture benchmark execution regenerates the frozen baseline") {
  const auto baseline_path = benchmark_root() / "baseline-v1.json";
  const auto baseline =
      statewright::reasoning::load_benchmark_run(baseline_path);
  const auto tasks = statewright::reasoning::load_benchmark_tasks(
      benchmark_root() / "tasks" / "development-v1.jsonl");
  const auto observations = statewright::reasoning::load_fixture_observations(
      benchmark_root() / "fixtures" / "development-v1.outputs.json", tasks);
  const auto regenerated = regenerate(tasks, observations, baseline);
  REQUIRE(regenerated == baseline);
  REQUIRE(statewright::reasoning::benchmark_json(regenerated) ==
          read_text(baseline_path));
}

TEST_CASE("fixture benchmark shards merge to the frozen baseline") {
  const auto baseline = statewright::reasoning::load_benchmark_run(
      benchmark_root() / "baseline-v1.json");
  const auto tasks = statewright::reasoning::load_benchmark_tasks(
      benchmark_root() / "tasks" / "development-v1.jsonl");
  const auto observations = statewright::reasoning::load_fixture_observations(
      benchmark_root() / "fixtures" / "development-v1.outputs.json", tasks);
  const auto first = regenerate(
      statewright::reasoning::select_benchmark_tasks(tasks, 0, 4), observations,
      baseline);
  const auto second = regenerate(
      statewright::reasoning::select_benchmark_tasks(tasks, 4, 4), observations,
      baseline);
  REQUIRE(statewright::reasoning::merge_benchmark_runs({second, first}, tasks) ==
          baseline);

  auto overlapping = second;
  overlapping.results = first.results;
  overlapping.task_count = first.task_count;
  overlapping.summaries = first.summaries;
  overlapping.benchmark_id = first.benchmark_id;
  overlapping.signature = first.signature;
  REQUIRE_THROWS_AS(
      statewright::reasoning::merge_benchmark_runs({first, overlapping}, tasks),
      statewright::common::Error);
}

TEST_CASE("reasoning benchmark integrity rejects identity tampering") {
  const auto baseline = statewright::reasoning::load_benchmark_run(
      benchmark_root() / "baseline-v1.json");

  auto signature_tampered = baseline;
  signature_tampered.signature.front() =
      signature_tampered.signature.front() == '0' ? '1' : '0';
  REQUIRE_THROWS_AS(
      statewright::reasoning::require_benchmark_run_integrity(
          signature_tampered),
      statewright::common::Error);

  auto cardinality_tampered = baseline;
  cardinality_tampered.results.pop_back();
  REQUIRE_THROWS_AS(
      statewright::reasoning::require_benchmark_run_integrity(
          cardinality_tampered),
      statewright::common::Error);

  auto ordering_tampered = baseline;
  std::swap(ordering_tampered.systems[0], ordering_tampered.systems[1]);
  REQUIRE_THROWS_AS(
      statewright::reasoning::require_benchmark_run_integrity(ordering_tampered),
      statewright::common::Error);
}

TEST_CASE("checked-in provider benchmark runs retain source-bound identity") {
  std::vector<std::filesystem::path> runs;
  const auto run_root = benchmark_root() / "runs";
  for (const auto &entry : std::filesystem::directory_iterator(run_root)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      runs.push_back(entry.path());
    }
  }
  std::ranges::sort(runs);
  REQUIRE_FALSE(runs.empty());
  for (const auto &path : runs) {
    CAPTURE(path);
    const auto run = statewright::reasoning::load_benchmark_run(path);
    REQUIRE(run.execution_mode == "provider_bound");
    auto checksum_path = path;
    checksum_path.replace_extension(".sha256");
    REQUIRE(statewright::reasoning::verify_benchmark_checksum(
                path, checksum_path) ==
            statewright::reasoning::verify_benchmark_checksum(path,
                                                               checksum_path));
  }
}
