#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::reasoning {

inline constexpr std::array<std::string_view, 8> benchmark_categories{
    "logic", "arithmetic", "debugging", "evidence_synthesis",
    "scientific_inference", "causal_reasoning", "ambiguity_resolution",
    "adversarial"};
inline constexpr std::array<std::string_view, 3> benchmark_system_ids{
    "base", "oiec", "oiec_sr"};
inline constexpr std::string_view development_fixture_qualification_status =
    "development_fixture_only";
inline constexpr std::string_view development_model_qualification_status =
    "development_model_plumbing_only";
inline constexpr std::string_view held_out_model_qualification_status =
    "held_out_model_qualification_candidate";

struct BenchmarkOracle final {
  std::string kind;
  std::string expected;

  bool operator==(const BenchmarkOracle &) const = default;
};

struct BenchmarkTask final {
  int schema_version = 1;
  std::string problem_id;
  std::string category;
  std::string prompt;
  BenchmarkOracle oracle;
  std::string oracle_method;
  std::vector<std::string> required_evidence_ids;
  std::vector<std::string> required_counterexamples;
  std::vector<std::string> source_refs;
  std::string signature;

  bool operator==(const BenchmarkTask &) const = default;
};

struct BenchmarkObservation final {
  int schema_version = 1;
  std::string problem_id;
  std::string system_id;
  std::string answer;
  int confidence_bp = 0;
  std::vector<std::string> evidence_ids;
  std::vector<std::string> counterexamples;
  std::int64_t token_count = 0;
  std::int64_t tool_calls = 0;
  std::int64_t collisions = 0;
  std::int64_t retries = 0;
  std::int64_t wall_time_ms = 0;
  std::string terminal_state = "ANSWER";
  std::string signature;

  bool operator==(const BenchmarkObservation &) const = default;
};

struct BenchmarkResult final {
  std::string problem_id;
  std::string category;
  std::string system_id;
  std::string task_signature;
  std::string observation_signature;
  std::string answer;
  int correctness_bp = 0;
  int evidence_coverage_bp = 0;
  int counterexample_detection_bp = 0;
  int calibration_error_bp = 0;
  std::int64_t token_count = 0;
  std::int64_t tool_calls = 0;
  std::int64_t collisions = 0;
  std::int64_t retries = 0;
  std::int64_t wall_time_ms = 0;
  std::string terminal_state = "ANSWER";
  std::string signature;

  bool operator==(const BenchmarkResult &) const = default;
};

struct BenchmarkSystemSummary final {
  std::string system_id;
  std::int64_t problem_count = 0;
  int accuracy_bp = 0;
  int evidence_coverage_bp = 0;
  int counterexample_detection_bp = 0;
  int mean_calibration_error_bp = 0;
  std::int64_t total_tokens = 0;
  std::int64_t total_tool_calls = 0;
  std::int64_t total_collisions = 0;
  std::int64_t total_retries = 0;
  std::int64_t total_wall_time_ms = 0;
  std::string signature;

  bool operator==(const BenchmarkSystemSummary &) const = default;
};

struct SourceFileRecord final {
  std::string path;
  std::string sha256;

  bool operator==(const SourceFileRecord &) const = default;
};

struct BenchmarkRun final {
  int schema_version = 1;
  std::string benchmark_id;
  std::string generated_on;
  std::string execution_mode;
  std::string qualification_status;
  bool performance_claim_allowed = false;
  std::string package_version;
  std::string git_head;
  bool worktree_dirty = true;
  std::string source_manifest_hash;
  std::vector<SourceFileRecord> source_files;
  std::int64_t task_count = 0;
  std::vector<contracts::Json> systems;
  std::vector<BenchmarkResult> results;
  std::vector<BenchmarkSystemSummary> summaries;
  std::string signature;

  bool operator==(const BenchmarkRun &) const = default;
};

class BenchmarkExecutor {
public:
  virtual ~BenchmarkExecutor() = default;

  [[nodiscard]] virtual std::string_view system_id() const noexcept = 0;
  [[nodiscard]] virtual contracts::Json descriptor() const = 0;
  [[nodiscard]] virtual contracts::Json identity_descriptor() const;
  [[nodiscard]] virtual BenchmarkObservation
  execute(const BenchmarkTask &task) = 0;
  virtual void release_runtime();
};

class FixtureBenchmarkExecutor final : public BenchmarkExecutor {
public:
  FixtureBenchmarkExecutor(
      std::string system_id,
      std::map<std::string, BenchmarkObservation> observations);

  [[nodiscard]] std::string_view system_id() const noexcept override;
  [[nodiscard]] contracts::Json descriptor() const override;
  [[nodiscard]] BenchmarkObservation
  execute(const BenchmarkTask &task) override;

private:
  std::string system_id_;
  std::map<std::string, BenchmarkObservation> observations_;
};

using FixtureObservations =
    std::map<std::string, std::map<std::string, BenchmarkObservation>>;

[[nodiscard]] contracts::Json to_json(const BenchmarkOracle &value);
[[nodiscard]] contracts::Json to_json(const BenchmarkTask &value);
[[nodiscard]] contracts::Json to_json(const BenchmarkObservation &value);
[[nodiscard]] contracts::Json to_json(const BenchmarkResult &value);
[[nodiscard]] contracts::Json to_json(const BenchmarkSystemSummary &value);
[[nodiscard]] contracts::Json to_json(const SourceFileRecord &value);
[[nodiscard]] contracts::Json to_json(const BenchmarkRun &value);

[[nodiscard]] BenchmarkTask canonicalize_benchmark_task(BenchmarkTask value);
[[nodiscard]] BenchmarkObservation
canonicalize_benchmark_observation(BenchmarkObservation value);
[[nodiscard]] BenchmarkResult
canonicalize_benchmark_result(BenchmarkResult value);
[[nodiscard]] BenchmarkSystemSummary
canonicalize_benchmark_summary(BenchmarkSystemSummary value);
[[nodiscard]] BenchmarkRun canonicalize_benchmark_run(BenchmarkRun value);

void require_benchmark_run_integrity(const BenchmarkRun &run);

[[nodiscard]] BenchmarkResult
score_observation(const BenchmarkTask &task,
                  const BenchmarkObservation &observation);
[[nodiscard]] BenchmarkSystemSummary
summarize_results(std::string_view system_id,
                  const std::vector<BenchmarkResult> &results);

[[nodiscard]] std::vector<BenchmarkTask>
load_benchmark_tasks(const std::filesystem::path &task_root);
[[nodiscard]] std::vector<BenchmarkTask>
select_benchmark_tasks(const std::vector<BenchmarkTask> &tasks,
                       std::size_t start = 0,
                       std::optional<std::size_t> count = std::nullopt);
[[nodiscard]] FixtureObservations
load_fixture_observations(const std::filesystem::path &path,
                          const std::vector<BenchmarkTask> &tasks);
[[nodiscard]] std::vector<SourceFileRecord> build_source_manifest(
    const std::filesystem::path &root,
    const std::vector<std::string> &relative_paths);
[[nodiscard]] BenchmarkRun run_benchmark(
    const std::vector<BenchmarkTask> &tasks,
    const std::vector<BenchmarkExecutor *> &executors,
    std::string generated_on, std::string package_version,
    std::string git_head, bool worktree_dirty,
    std::vector<SourceFileRecord> source_files,
    std::string execution_mode = "deterministic_fixture",
    std::string qualification_status = "development_fixture_only");
[[nodiscard]] BenchmarkRun
merge_benchmark_runs(const std::vector<BenchmarkRun> &shards,
                     const std::vector<BenchmarkTask> &tasks);

[[nodiscard]] BenchmarkRun
load_benchmark_run(const std::filesystem::path &path);
[[nodiscard]] std::string
verify_benchmark_checksum(const std::filesystem::path &path,
                          const std::filesystem::path &checksum_path);
[[nodiscard]] std::string benchmark_json(const BenchmarkRun &run);
void write_benchmark_run(const std::filesystem::path &path,
                         const BenchmarkRun &run);

} // namespace statewright::reasoning
