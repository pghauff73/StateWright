#pragma once

#include "statewright/contracts/canonical_json.hpp"
#include "statewright/reasoning/benchmark.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::reasoning {

inline constexpr std::int64_t minimum_ablation_tasks_per_category = 10;
inline constexpr std::array<std::string_view, 6>
    required_qualification_categories{"logic", "arithmetic", "debugging",
                                      "scientific_inference",
                                      "causal_reasoning", "adversarial"};

struct ReasoningQualificationReport final {
  int schema_version = 1;
  std::string qualification_id;
  std::string source_manifest_hash;
  std::string provider_identity_signature;
  std::vector<std::string> benchmark_run_signatures;
  std::int64_t repeated_run_count = 0;
  std::int64_t task_count_per_run = 0;
  std::vector<std::pair<std::string, std::int64_t>> category_counts;
  std::vector<contracts::Json> system_metrics;
  int difficult_accuracy_gain_bp = 0;
  int certificate_reproducibility_bp = 0;
  std::vector<std::string> required_ablation_ids;
  std::vector<std::string> present_ablation_ids;
  std::vector<std::string> missing_ablation_ids;
  std::int64_t ablation_task_count_per_run = 0;
  std::vector<std::pair<std::string, std::int64_t>> ablation_category_counts;
  std::vector<contracts::Json> ablation_evidence;
  std::vector<contracts::Json> task_failures;
  std::vector<std::string> limitations;
  bool performance_gate_passed = false;
  bool performance_claim_allowed = false;
  bool human_review_required = true;
  std::string signature;

  bool operator==(const ReasoningQualificationReport &) const = default;
};

[[nodiscard]] std::pair<int, int>
wilson_interval_bp(std::int64_t successes, std::int64_t total,
                   double z = 1.96);
[[nodiscard]] int
certificate_reproducibility_bp(const std::vector<BenchmarkRun> &runs);

[[nodiscard]] contracts::Json
to_json(const ReasoningQualificationReport &value);
[[nodiscard]] ReasoningQualificationReport
canonicalize_reasoning_qualification_report(
    ReasoningQualificationReport value);
void require_reasoning_qualification_integrity(
    const ReasoningQualificationReport &report);

[[nodiscard]] ReasoningQualificationReport qualify_reasoning_runs(
    const std::vector<BenchmarkRun> &runs,
    const std::map<std::string, std::vector<BenchmarkRun>> &ablation_runs = {},
    std::optional<int> certificate_reproducibility_assertion_bp =
        std::nullopt);

} // namespace statewright::reasoning
