#pragma once

#include "statewright/saa/algorithm_experiment.hpp"

#include <gmpxx.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view experiment_aggregation_version =
    "saa-repeated-experiment-aggregation-v1";
inline constexpr std::size_t max_aggregated_experiments = 64U;

struct AggregatedMetricEvidence final {
  std::string metric_name;
  std::string direction;
  int experiment_count = 0;
  int material_improvement_count = 0;
  int material_regression_count = 0;
  int no_material_change_count = 0;
  mpq_class mean_signed_improvement{0};
  mpq_class minimum_signed_improvement{0};
  mpq_class maximum_signed_improvement{0};
};

struct RepeatedExperimentAggregate final {
  int schema_version = 1;
  std::string aggregation_version =
      std::string(experiment_aggregation_version);
  std::string design_signature;
  std::vector<std::string> result_signatures;
  std::vector<std::string> grounded_evidence_ids;
  std::vector<std::string> independence_groups;
  int experiment_count = 0;
  int minimum_required_experiments = 0;
  int minimum_required_independence_groups = 0;
  std::vector<AggregatedMetricEvidence> metric_evidence;
  std::string status;
  bool sustained_improvement_qualified = false;
  bool qualification_required_before_canonical_reuse = true;
  std::string aggregate_signature;
};

[[nodiscard]] RepeatedExperimentAggregate aggregate_repeated_experiments(
    const std::vector<AlgorithmABExperimentResult> &results,
    int minimum_experiments = 2, int minimum_independence_groups = 2);

[[nodiscard]] contracts::Json to_json(const AggregatedMetricEvidence &value);
[[nodiscard]] contracts::Json
to_json(const RepeatedExperimentAggregate &value);

} // namespace statewright::saa
