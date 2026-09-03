#include "statewright/reasoning/qualification.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/reasoning/ablation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace statewright::reasoning {
namespace {

constexpr int score_scale = 10'000;

[[noreturn]] void policy_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied, std::move(message));
}

[[nodiscard]] int mean(const std::vector<int> &values) {
  if (values.empty()) {
    return 0;
  }
  const auto total = std::accumulate(values.begin(), values.end(),
                                     std::int64_t{0});
  return static_cast<int>(total / static_cast<std::int64_t>(values.size()));
}

[[nodiscard]] std::vector<std::string>
task_signature_sequence(const BenchmarkRun &run) {
  std::vector<std::string> signatures;
  for (const auto &result : run.results) {
    if (result.system_id == "base") {
      signatures.push_back(result.task_signature);
    }
  }
  if (signatures.size() != static_cast<std::size_t>(run.task_count)) {
    policy_error(
        "benchmark run does not contain one base task signature per task");
  }
  return signatures;
}

[[nodiscard]] contracts::Json execution_identity(const BenchmarkRun &run) {
  contracts::Json identity = contracts::Json::array();
  for (const auto &descriptor : run.systems) {
    auto item = descriptor;
    item.erase("pipeline");
    item.erase("telemetry");
    identity.push_back(std::move(item));
  }
  return identity;
}

[[nodiscard]] std::int64_t provider_failures(const contracts::Json &descriptor) {
  if (!descriptor.contains("telemetry") ||
      !descriptor.at("telemetry").is_object()) {
    return 0;
  }
  return descriptor.at("telemetry").value("provider_failures", 0LL);
}

[[nodiscard]] contracts::Json
system_metrics(const std::vector<BenchmarkRun> &runs,
               const std::vector<BenchmarkResult> &results,
               std::string_view system_id) {
  std::vector<BenchmarkResult> selected;
  for (const auto &result : results) {
    if (result.system_id == system_id) {
      selected.push_back(result);
    }
  }
  if (selected.empty()) {
    policy_error("qualification has no results for system: " +
                 std::string(system_id));
  }
  std::int64_t successes = 0;
  std::int64_t unsupported_empirical = 0;
  std::vector<int> correctness;
  std::vector<int> evidence_coverage;
  std::vector<int> counterexample_detection;
  std::vector<int> calibration_error;
  std::int64_t total_tokens = 0;
  std::int64_t total_tool_calls = 0;
  std::int64_t total_collisions = 0;
  std::int64_t total_retries = 0;
  std::int64_t total_wall_time_ms = 0;
  for (const auto &result : selected) {
    successes += result.correctness_bp == score_scale ? 1 : 0;
    unsupported_empirical +=
        (result.category == "scientific_inference" ||
         result.category == "causal_reasoning") &&
                result.terminal_state == "ANSWER" &&
                result.evidence_coverage_bp < score_scale
            ? 1
            : 0;
    correctness.push_back(result.correctness_bp);
    evidence_coverage.push_back(result.evidence_coverage_bp);
    counterexample_detection.push_back(result.counterexample_detection_bp);
    calibration_error.push_back(result.calibration_error_bp);
    total_tokens += result.token_count;
    total_tool_calls += result.tool_calls;
    total_collisions += result.collisions;
    total_retries += result.retries;
    total_wall_time_ms += result.wall_time_ms;
  }
  std::int64_t total_provider_failures = 0;
  const auto system = std::ranges::find(benchmark_system_ids, system_id);
  const auto system_index = static_cast<std::size_t>(
      std::distance(benchmark_system_ids.begin(), system));
  for (const auto &run : runs) {
    if (run.execution_mode == "provider_bound") {
      total_provider_failures += provider_failures(run.systems.at(system_index));
    }
  }
  const auto confidence_interval =
      wilson_interval_bp(successes, static_cast<std::int64_t>(selected.size()));
  return {{"system_id", system_id},
          {"result_count", selected.size()},
          {"accuracy_bp", mean(correctness)},
          {"accuracy_ci95_bp",
           contracts::Json::array(
               {confidence_interval.first, confidence_interval.second})},
          {"evidence_coverage_bp", mean(evidence_coverage)},
          {"counterexample_detection_bp", mean(counterexample_detection)},
          {"mean_calibration_error_bp", mean(calibration_error)},
          {"total_tokens", total_tokens},
          {"total_tool_calls", total_tool_calls},
          {"total_collisions", total_collisions},
          {"total_retries", total_retries},
          {"total_wall_time_ms", total_wall_time_ms},
          {"total_provider_failures", total_provider_failures},
          {"unsupported_empirical_claims", unsupported_empirical}};
}

struct AblationEvidence final {
  std::vector<contracts::Json> evidence;
  std::vector<std::string> task_signatures;
  std::vector<std::pair<std::string, std::int64_t>> category_counts;
  std::int64_t provider_failure_count = 0;
};

[[nodiscard]] AblationEvidence build_ablation_evidence(
    const std::vector<BenchmarkRun> &main_runs,
    const std::map<std::string, std::vector<BenchmarkRun>> &ablation_runs) {
  const std::set<std::string> required(required_ablation_ids().begin(),
                                       required_ablation_ids().end());
  for (const auto &[ablation_id, ignored] : ablation_runs) {
    static_cast<void>(ignored);
    if (!required.contains(ablation_id)) {
      policy_error(
          "qualification ablation manifest has unknown IDs: " + ablation_id);
    }
  }
  std::map<std::string, AblationConfiguration> configurations;
  for (const auto &configuration : standard_ablation_configurations()) {
    configurations.emplace(configuration.ablation_id, configuration);
  }
  const auto &reference = main_runs.front();
  const auto reference_identity = execution_identity(reference);
  AblationEvidence result;
  for (const auto &ablation_id : required_ablation_ids()) {
    const auto found = ablation_runs.find(ablation_id);
    if (found == ablation_runs.end() || found->second.empty()) {
      continue;
    }
    const auto expected_pipeline =
        ablation_pipeline(configurations.at(ablation_id));
    std::vector<std::string> run_signatures;
    for (const auto &raw_run : found->second) {
      const auto run = canonicalize_benchmark_run(raw_run);
      if (run.execution_mode != "provider_bound") {
        policy_error("qualification ablation run is not provider-bound");
      }
      if (run.qualification_status != held_out_model_qualification_status) {
        policy_error("qualification ablation run is not held-out evidence");
      }
      if (run.source_manifest_hash != reference.source_manifest_hash) {
        policy_error("qualification ablation source manifest mismatch");
      }
      const auto task_signatures = task_signature_sequence(run);
      if (!result.task_signatures.empty() &&
          task_signatures != result.task_signatures) {
        policy_error("qualification ablation task set mismatch");
      }
      if (result.task_signatures.empty()) {
        result.task_signatures = task_signatures;
        for (const auto category : required_qualification_categories) {
          std::set<std::string> problem_ids;
          for (const auto &benchmark_result : run.results) {
            if (benchmark_result.system_id == "base" &&
                benchmark_result.category == category) {
              problem_ids.insert(benchmark_result.problem_id);
            }
          }
          result.category_counts.emplace_back(std::string(category),
                                              problem_ids.size());
        }
        std::ranges::sort(result.category_counts);
      }
      if (execution_identity(run) != reference_identity) {
        policy_error(
            "qualification ablation provider/runtime identity mismatch");
      }
      if (run.systems.at(2).value("pipeline", std::string{}) !=
          expected_pipeline) {
        policy_error("qualification ablation pipeline mismatch: " +
                     ablation_id);
      }
      run_signatures.push_back(run.signature);
      for (const auto &descriptor : run.systems) {
        result.provider_failure_count += provider_failures(descriptor);
      }
    }
    if (std::set<std::string>(run_signatures.begin(), run_signatures.end())
            .size() != run_signatures.size()) {
      policy_error("qualification ablation repeats one artifact: " +
                   ablation_id);
    }
    result.evidence.push_back(
        {{"ablation_id", ablation_id},
         {"configuration_signature", configurations.at(ablation_id).signature},
         {"benchmark_run_signatures", run_signatures}});
  }
  return result;
}

} // namespace

std::pair<int, int> wilson_interval_bp(std::int64_t successes,
                                       std::int64_t total, double z) {
  if (total < 1 || successes < 0 || successes > total) {
    policy_error("Wilson interval requires 0 <= successes <= total");
  }
  const auto total_double = static_cast<double>(total);
  const auto proportion = static_cast<double>(successes) / total_double;
  const auto denominator = 1.0 + z * z / total_double;
  const auto center =
      (proportion + z * z / (2.0 * total_double)) / denominator;
  const auto margin =
      z * std::sqrt(proportion * (1.0 - proportion) / total_double +
                    z * z / (4.0 * total_double * total_double)) /
      denominator;
  const auto lower = static_cast<int>(
      std::nearbyint((center - margin) * static_cast<double>(score_scale)));
  const auto upper = static_cast<int>(
      std::nearbyint((center + margin) * static_cast<double>(score_scale)));
  return {std::max(0, lower), std::min(score_scale, upper)};
}

int certificate_reproducibility_bp(const std::vector<BenchmarkRun> &runs) {
  if (runs.size() < 2) {
    return 0;
  }
  std::vector<std::vector<std::string>> signatures;
  for (const auto &raw_run : runs) {
    const auto run = canonicalize_benchmark_run(raw_run);
    const auto &telemetry = run.systems.at(2).contains("telemetry")
                                ? run.systems.at(2).at("telemetry")
                                : contracts::Json::object();
    if (!telemetry.is_object() ||
        !telemetry.contains("certificate_signatures") ||
        !telemetry.at("certificate_signatures").is_array() ||
        telemetry.at("certificate_signatures").size() !=
            static_cast<std::size_t>(run.task_count)) {
      return 0;
    }
    std::vector<std::string> row;
    for (const auto &value : telemetry.at("certificate_signatures")) {
      if (!value.is_string()) {
        return 0;
      }
      row.push_back(value.get<std::string>());
    }
    signatures.push_back(std::move(row));
  }
  std::int64_t reproducible = 0;
  for (std::size_t index = 0; index < signatures.front().size(); ++index) {
    const auto &expected = signatures.front()[index];
    if (expected.empty()) {
      continue;
    }
    const bool matches = std::ranges::all_of(
        signatures | std::views::drop(1),
        [index, &expected](const std::vector<std::string> &row) {
          return row[index] == expected;
        });
    reproducible += matches ? 1 : 0;
  }
  return static_cast<int>(
      reproducible * score_scale /
      static_cast<std::int64_t>(signatures.front().size()));
}

contracts::Json to_json(const ReasoningQualificationReport &value) {
  contracts::Json category_counts = contracts::Json::array();
  for (const auto &[category, count] : value.category_counts) {
    category_counts.push_back(contracts::Json::array({category, count}));
  }
  contracts::Json ablation_category_counts = contracts::Json::array();
  for (const auto &[category, count] : value.ablation_category_counts) {
    ablation_category_counts.push_back(
        contracts::Json::array({category, count}));
  }
  return {{"schema_version", value.schema_version},
          {"qualification_id", value.qualification_id},
          {"source_manifest_hash", value.source_manifest_hash},
          {"provider_identity_signature", value.provider_identity_signature},
          {"benchmark_run_signatures", value.benchmark_run_signatures},
          {"repeated_run_count", value.repeated_run_count},
          {"task_count_per_run", value.task_count_per_run},
          {"category_counts", std::move(category_counts)},
          {"system_metrics", value.system_metrics},
          {"difficult_accuracy_gain_bp", value.difficult_accuracy_gain_bp},
          {"certificate_reproducibility_bp",
           value.certificate_reproducibility_bp},
          {"required_ablation_ids", value.required_ablation_ids},
          {"present_ablation_ids", value.present_ablation_ids},
          {"missing_ablation_ids", value.missing_ablation_ids},
          {"ablation_task_count_per_run", value.ablation_task_count_per_run},
          {"ablation_category_counts", std::move(ablation_category_counts)},
          {"ablation_evidence", value.ablation_evidence},
          {"task_failures", value.task_failures},
          {"limitations", value.limitations},
          {"performance_gate_passed", value.performance_gate_passed},
          {"performance_claim_allowed", value.performance_claim_allowed},
          {"human_review_required", value.human_review_required},
          {"signature", value.signature}};
}

ReasoningQualificationReport canonicalize_reasoning_qualification_report(
    ReasoningQualificationReport value) {
  if (value.schema_version != 1) {
    policy_error("qualification report schema_version must be 1");
  }
  if (value.performance_claim_allowed) {
    policy_error(
        "automated qualification cannot authorize a performance claim");
  }
  auto material = to_json(value);
  material.erase("qualification_id");
  material.erase("signature");
  const auto expected_id =
      "reasoning-qualification:" + contracts::sha256_json(material);
  if (!value.qualification_id.empty() &&
      value.qualification_id != expected_id) {
    policy_error("qualification report ID mismatch");
  }
  value.qualification_id = expected_id;
  material["qualification_id"] = expected_id;
  const auto expected_signature = contracts::sha256_json(material);
  if (!value.signature.empty() && value.signature != expected_signature) {
    policy_error("qualification report signature mismatch");
  }
  value.signature = expected_signature;
  return value;
}

void require_reasoning_qualification_integrity(
    const ReasoningQualificationReport &report) {
  static_cast<void>(canonicalize_reasoning_qualification_report(report));
}

ReasoningQualificationReport qualify_reasoning_runs(
    const std::vector<BenchmarkRun> &raw_runs,
    const std::map<std::string, std::vector<BenchmarkRun>> &ablation_runs,
    std::optional<int> certificate_reproducibility_assertion_bp) {
  if (raw_runs.empty()) {
    policy_error("qualification requires at least one matched benchmark run");
  }
  std::vector<BenchmarkRun> runs;
  runs.reserve(raw_runs.size());
  for (const auto &run : raw_runs) {
    runs.push_back(canonicalize_benchmark_run(run));
  }
  const auto source_manifest_hash = runs.front().source_manifest_hash;
  const auto task_count = runs.front().task_count;
  const auto task_signatures = task_signature_sequence(runs.front());
  const auto provider_identity = execution_identity(runs.front());
  for (const auto &run : runs) {
    if (run.source_manifest_hash != source_manifest_hash ||
        run.task_count != task_count) {
      policy_error("qualification runs must use one source and task cardinality");
    }
    if (task_signature_sequence(run) != task_signatures) {
      policy_error("qualification runs do not use the same matched tasks");
    }
    if (execution_identity(run) != provider_identity) {
      policy_error(
          "qualification runs do not share one provider/runtime identity");
    }
  }
  const auto provider_identity_signature =
      contracts::sha256_json(provider_identity);
  const auto configurations = standard_ablation_configurations();
  const auto full_sr = std::ranges::find(configurations, "full_sr",
                                         &AblationConfiguration::ablation_id);
  for (const auto &run : runs) {
    if (run.execution_mode == "provider_bound" &&
        run.qualification_status == held_out_model_qualification_status &&
        run.systems.at(2).value("pipeline", std::string{}) !=
            ablation_pipeline(*full_sr)) {
      policy_error("qualification main run is not the full_sr configuration");
    }
  }
  const auto ablations = build_ablation_evidence(runs, ablation_runs);
  std::vector<BenchmarkResult> all_results;
  for (const auto &run : runs) {
    all_results.insert(all_results.end(), run.results.begin(), run.results.end());
  }
  const int reproducibility = certificate_reproducibility_bp(runs);
  if (certificate_reproducibility_assertion_bp.has_value() &&
      *certificate_reproducibility_assertion_bp != reproducibility) {
    policy_error(
        "certificate reproducibility assertion does not match benchmark evidence");
  }
  std::vector<std::pair<std::string, std::int64_t>> category_counts;
  for (const auto category : required_qualification_categories) {
    std::set<std::string> problem_ids;
    for (const auto &result : runs.front().results) {
      if (result.category == category && result.system_id == "base") {
        problem_ids.insert(result.problem_id);
      }
    }
    category_counts.emplace_back(std::string(category), problem_ids.size());
  }
  std::vector<contracts::Json> metrics;
  for (const auto system_id : benchmark_system_ids) {
    metrics.push_back(system_metrics(runs, all_results, system_id));
  }
  const auto accuracy_gain = metrics.at(2).at("accuracy_bp").get<int>() -
                             metrics.at(0).at("accuracy_bp").get<int>();
  std::vector<contracts::Json> failures;
  for (const auto &run : runs) {
    for (const auto &result : run.results) {
      if (result.correctness_bp < score_scale) {
        failures.push_back({{"run_signature", run.signature},
                            {"problem_id", result.problem_id},
                            {"category", result.category},
                            {"system_id", result.system_id},
                            {"correctness_bp", result.correctness_bp},
                            {"terminal_state", result.terminal_state}});
      }
    }
  }
  std::vector<std::string> present_ablations;
  for (const auto &item : ablations.evidence) {
    present_ablations.push_back(item.at("ablation_id").get<std::string>());
  }
  std::vector<std::string> missing_ablations;
  for (const auto &ablation_id : required_ablation_ids()) {
    if (std::ranges::find(present_ablations, ablation_id) ==
        present_ablations.end()) {
      missing_ablations.push_back(ablation_id);
    }
  }
  std::vector<std::string> limitations;
  if (std::ranges::any_of(runs, [](const BenchmarkRun &run) {
        return run.execution_mode != "provider_bound" ||
               run.qualification_status != held_out_model_qualification_status;
      })) {
    limitations.push_back(
        "runs are not held-out provider qualification candidates");
  }
  if (runs.size() < 2) {
    limitations.push_back("fewer than two matched repeated runs");
  }
  for (const auto &[category, count] : category_counts) {
    if (count < 100) {
      limitations.push_back(category + " has only " + std::to_string(count) +
                            " held-out tasks");
    }
  }
  if (!missing_ablations.empty()) {
    limitations.push_back("required ablation results are incomplete");
  }
  if (!ablations.task_signatures.empty()) {
    for (const auto &[category, count] : ablations.category_counts) {
      if (count < minimum_ablation_tasks_per_category) {
        limitations.push_back("ablation corpus " + category + " has only " +
                              std::to_string(count) + " held-out tasks");
      }
    }
  }
  if (ablations.provider_failure_count != 0) {
    limitations.push_back("provider failures occurred during ablation runs");
  }
  if (reproducibility < score_scale) {
    limitations.push_back(
        "certificate reproducibility is below 100 percent");
  }
  if (std::ranges::any_of(metrics, [](const contracts::Json &item) {
        return item.at("total_provider_failures").get<std::int64_t>() != 0;
      })) {
    limitations.push_back("provider failures occurred during qualification");
  }
  if (std::ranges::any_of(metrics, [](const contracts::Json &item) {
        return item.at("total_retries").get<std::int64_t>() != 0;
      })) {
    limitations.push_back("blind retries occurred during qualification");
  }
  const bool performance_gate_passed =
      limitations.empty() && accuracy_gain >= 1'000 &&
      metrics.at(2).at("counterexample_detection_bp").get<int>() >= 9'000 &&
      metrics.at(2).at("total_retries").get<std::int64_t>() == 0 &&
      metrics.at(2).at("unsupported_empirical_claims").get<std::int64_t>() ==
          0 &&
      reproducibility == score_scale;
  auto sorted_category_counts = category_counts;
  std::ranges::sort(sorted_category_counts);
  std::vector<std::string> benchmark_run_signatures;
  for (const auto &run : runs) {
    benchmark_run_signatures.push_back(run.signature);
  }
  return canonicalize_reasoning_qualification_report(
      {.schema_version = 1,
       .qualification_id = {},
       .source_manifest_hash = source_manifest_hash,
       .provider_identity_signature = provider_identity_signature,
       .benchmark_run_signatures = std::move(benchmark_run_signatures),
       .repeated_run_count = static_cast<std::int64_t>(runs.size()),
       .task_count_per_run = task_count,
       .category_counts = std::move(sorted_category_counts),
       .system_metrics = std::move(metrics),
       .difficult_accuracy_gain_bp = accuracy_gain,
       .certificate_reproducibility_bp = reproducibility,
       .required_ablation_ids = required_ablation_ids(),
       .present_ablation_ids = std::move(present_ablations),
       .missing_ablation_ids = std::move(missing_ablations),
       .ablation_task_count_per_run =
           static_cast<std::int64_t>(ablations.task_signatures.size()),
       .ablation_category_counts = ablations.category_counts,
       .ablation_evidence = ablations.evidence,
       .task_failures = std::move(failures),
       .limitations = std::move(limitations),
       .performance_gate_passed = performance_gate_passed,
       .performance_claim_allowed = false,
       .human_review_required = true,
       .signature = {}});
}

} // namespace statewright::reasoning
