#pragma once

#include "statewright/saa/dynamics.hpp"
#include "statewright/saa/reasoning_outcome.hpp"

#include <gmpxx.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view algorithm_experiment_version =
    "saa-controlled-algorithm-experiment-v1";
inline constexpr std::size_t max_experiment_metrics = 16U;
inline constexpr int max_experiment_trials = 10000;

struct ExperimentMetricSpec final {
  std::string name;
  std::string direction;
  mpq_class minimum_material_effect{0};
};

struct AlgorithmABExperimentDesign final {
  int schema_version = 1;
  std::string experiment_version = std::string(algorithm_experiment_version);
  std::string baseline_ref;
  std::string candidate_ref;
  std::string context_signature;
  std::vector<ExperimentMetricSpec> metrics;
  std::vector<std::string> required_invariants;
  std::vector<std::string> evidence_requirements;
  int minimum_trials = 1;
  bool paired_context = true;
  std::string design_signature;
};

struct AlgorithmVariantObservation final {
  int schema_version = 1;
  std::string experiment_version = std::string(algorithm_experiment_version);
  std::string design_signature;
  std::string variant_ref;
  std::vector<std::pair<std::string, mpq_class>> metric_values;
  std::vector<std::string> evidence_ids;
  std::vector<std::pair<std::string, bool>> invariant_results;
  int trial_count = 0;
  bool execution_success = false;
  std::string observation_signature;
};

struct ExperimentMetricComparison final {
  std::string metric_name;
  std::string direction;
  mpq_class baseline_value{0};
  mpq_class candidate_value{0};
  mpq_class signed_improvement{0};
  mpq_class minimum_material_effect{0};
  std::string status;
};

struct AlgorithmABExperimentResult final {
  int schema_version = 1;
  std::string experiment_version = std::string(algorithm_experiment_version);
  std::string design_signature;
  std::string baseline_observation_signature;
  std::string candidate_observation_signature;
  std::vector<ExperimentMetricComparison> metric_comparisons;
  std::vector<std::string> grounded_evidence_ids;
  std::vector<std::string> independence_groups;
  int evidence_requirement_coverage_bp = 0;
  bool invariant_gate_passed = false;
  bool independent_review = false;
  std::string status;
  bool candidate_improvement_qualified = false;
  bool qualification_required_before_canonical_reuse = true;
  std::string result_signature;
};

[[nodiscard]] ExperimentMetricSpec
canonical_experiment_metric(ExperimentMetricSpec metric);
[[nodiscard]] AlgorithmABExperimentDesign make_ab_experiment_design(
    std::string baseline_ref, std::string candidate_ref,
    std::string context_signature, std::vector<ExperimentMetricSpec> metrics,
    std::vector<std::string> required_invariants = {},
    std::vector<std::string> evidence_requirements = {},
    int minimum_trials = 1, bool paired_context = true);
[[nodiscard]] AlgorithmVariantObservation make_variant_observation(
    const AlgorithmABExperimentDesign &design, std::string variant_ref,
    std::vector<std::pair<std::string, mpq_class>> metric_values,
    std::vector<std::string> evidence_ids,
    std::vector<std::pair<std::string, bool>> invariant_results,
    int trial_count, bool execution_success);
[[nodiscard]] AlgorithmABExperimentResult qualify_ab_experiment(
    const ReasoningEvidenceResolver &evidence_resolver,
    const AlgorithmABExperimentDesign &design,
    const AlgorithmVariantObservation &baseline,
    const AlgorithmVariantObservation &candidate, bool independent_review);

[[nodiscard]] contracts::Json to_json(const ExperimentMetricSpec &value);
[[nodiscard]] contracts::Json
to_json(const AlgorithmABExperimentDesign &value);
[[nodiscard]] contracts::Json
to_json(const AlgorithmVariantObservation &value);
[[nodiscard]] contracts::Json
to_json(const ExperimentMetricComparison &value);
[[nodiscard]] contracts::Json
to_json(const AlgorithmABExperimentResult &value);

} // namespace statewright::saa
