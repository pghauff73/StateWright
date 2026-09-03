#include "statewright/saa/experiment_aggregation.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <map>
#include <ranges>
#include <set>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[noreturn]] void aggregation_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

} // namespace

RepeatedExperimentAggregate aggregate_repeated_experiments(
    const std::vector<AlgorithmABExperimentResult> &results,
    int minimum_experiments, int minimum_independence_groups) {
  if (minimum_experiments < 2 ||
      minimum_experiments >
          static_cast<int>(max_aggregated_experiments)) {
    aggregation_error(
        "SAA-11.4 minimum experiment count outside supported range");
  }
  if (minimum_independence_groups < 1 ||
      minimum_independence_groups >
          static_cast<int>(max_aggregated_experiments)) {
    aggregation_error(
        "SAA-11.4 minimum independence-group count outside supported range");
  }
  if (results.size() < static_cast<std::size_t>(minimum_experiments) ||
      results.size() > max_aggregated_experiments) {
    aggregation_error(
        "SAA-11.4 repeated experiment count violates aggregation bounds");
  }
  std::set<std::string> design_signatures;
  std::vector<std::string> result_signatures;
  std::set<std::string> evidence_ids;
  std::set<std::string> groups;
  for (const auto &result : results) {
    design_signatures.insert(result.design_signature);
    result_signatures.push_back(result.result_signature);
    evidence_ids.insert(result.grounded_evidence_ids.begin(),
                        result.grounded_evidence_ids.end());
    for (const auto &group : result.independence_groups) {
      if (!group.empty()) {
        groups.insert(group);
      }
    }
  }
  if (design_signatures.size() != 1U) {
    aggregation_error(
        "SAA-11.4 can aggregate only exact experiment-design matches");
  }
  std::ranges::sort(result_signatures);
  if (std::ranges::adjacent_find(result_signatures) !=
      result_signatures.end()) {
    aggregation_error(
        "SAA-11.4 duplicate experiment results cannot be counted twice");
  }

  std::vector<std::string> metric_names;
  std::map<std::string, std::string> directions;
  for (const auto &comparison : results.front().metric_comparisons) {
    metric_names.push_back(comparison.metric_name);
    directions[comparison.metric_name] = comparison.direction;
  }
  for (std::size_t index = 1; index < results.size(); ++index) {
    std::vector<std::string> candidate_names;
    for (const auto &comparison : results[index].metric_comparisons) {
      candidate_names.push_back(comparison.metric_name);
    }
    if (candidate_names != metric_names) {
      aggregation_error(
          "SAA-11.4 metric order/identity differs across repeated results");
    }
    for (const auto &comparison : results[index].metric_comparisons) {
      if (directions[comparison.metric_name] != comparison.direction) {
        aggregation_error(
            "SAA-11.4 metric direction differs across repeated results");
      }
    }
  }

  std::vector<AggregatedMetricEvidence> summaries;
  bool any_regression = false;
  bool any_improvement = false;
  for (const auto &metric_name : metric_names) {
    std::vector<const ExperimentMetricComparison *> comparisons;
    for (const auto &result : results) {
      const auto found = std::ranges::find_if(
          result.metric_comparisons, [&](const auto &comparison) {
            return comparison.metric_name == metric_name;
          });
      comparisons.push_back(&*found);
    }
    int improvement_count = 0;
    int regression_count = 0;
    mpq_class sum = 0;
    mpq_class minimum = comparisons.front()->signed_improvement;
    mpq_class maximum = comparisons.front()->signed_improvement;
    for (const auto *comparison : comparisons) {
      if (comparison->status == "MATERIAL_IMPROVEMENT") {
        ++improvement_count;
      } else if (comparison->status == "MATERIAL_REGRESSION") {
        ++regression_count;
      }
      sum += comparison->signed_improvement;
      minimum = std::min(minimum, comparison->signed_improvement);
      maximum = std::max(maximum, comparison->signed_improvement);
    }
    any_regression = any_regression || regression_count != 0;
    any_improvement = any_improvement || improvement_count != 0;
    const int count = static_cast<int>(comparisons.size());
    summaries.push_back(
        {.metric_name = metric_name,
         .direction = comparisons.front()->direction,
         .experiment_count = count,
         .material_improvement_count = improvement_count,
         .material_regression_count = regression_count,
         .no_material_change_count =
             count - improvement_count - regression_count,
         .mean_signed_improvement = sum / count,
         .minimum_signed_improvement = minimum,
         .maximum_signed_improvement = maximum});
  }

  bool any_tradeoff_status = false;
  bool any_unqualified = false;
  for (const auto &result : results) {
    if (result.status == "EXPERIMENT_TRADEOFF_UNRESOLVED") {
      any_tradeoff_status = true;
    }
    if (result.status != "CANDIDATE_IMPROVEMENT_QUALIFIED" &&
        result.status != "NO_MATERIAL_IMPROVEMENT") {
      any_unqualified = true;
    }
    if (!result.invariant_gate_passed ||
        result.evidence_requirement_coverage_bp != 10000 ||
        !result.independent_review) {
      any_unqualified = true;
    }
  }
  std::string status;
  if (any_regression) {
    status = "REPEATED_EVIDENCE_REGRESSION_DETECTED";
  } else if (any_tradeoff_status) {
    status = "REPEATED_EVIDENCE_TRADEOFF_UNRESOLVED";
  } else if (any_unqualified) {
    status = "REPEATED_EVIDENCE_CONTAINS_UNQUALIFIED_RESULT";
  } else if (groups.size() <
             static_cast<std::size_t>(minimum_independence_groups)) {
    status = "REPEATED_EVIDENCE_INDEPENDENCE_INSUFFICIENT";
  } else if (std::ranges::all_of(results, [](const auto &result) {
               return result.candidate_improvement_qualified;
             }) &&
             any_improvement) {
    status = "SUSTAINED_CANDIDATE_IMPROVEMENT_QUALIFIED";
  } else {
    status = "REPEATED_EVIDENCE_NO_SUSTAINED_IMPROVEMENT";
  }
  const bool sustained =
      status == "SUSTAINED_CANDIDATE_IMPROVEMENT_QUALIFIED";
  const std::vector<std::string> grounded(evidence_ids.begin(),
                                          evidence_ids.end());
  const std::vector<std::string> independence(groups.begin(), groups.end());
  Json metric_payload = Json::array();
  for (const auto &summary : summaries) {
    metric_payload.push_back(to_json(summary));
  }
  const std::string design_signature = *design_signatures.begin();
  const Json payload =
      {{"design_signature", design_signature},
       {"experiment_count", results.size()},
       {"grounded_evidence_ids", grounded},
       {"independence_groups", independence},
       {"metric_evidence", metric_payload},
       {"minimum_required_experiments", minimum_experiments},
       {"minimum_required_independence_groups",
        minimum_independence_groups},
       {"result_signatures", result_signatures},
       {"status", status},
       {"sustained_improvement_qualified", sustained},
       {"version", experiment_aggregation_version}};
  return {.schema_version = 1,
          .aggregation_version =
              std::string(experiment_aggregation_version),
          .design_signature = design_signature,
          .result_signatures = std::move(result_signatures),
          .grounded_evidence_ids = grounded,
          .independence_groups = independence,
          .experiment_count = static_cast<int>(results.size()),
          .minimum_required_experiments = minimum_experiments,
          .minimum_required_independence_groups =
              minimum_independence_groups,
          .metric_evidence = std::move(summaries),
          .status = std::move(status),
          .sustained_improvement_qualified = sustained,
          .qualification_required_before_canonical_reuse = true,
          .aggregate_signature = contracts::sha256_json(payload)};
}

Json to_json(const AggregatedMetricEvidence &value) {
  return {{"direction", value.direction},
          {"experiment_count", value.experiment_count},
          {"material_improvement_count",
           value.material_improvement_count},
          {"material_regression_count", value.material_regression_count},
          {"maximum_signed_improvement",
           rational_json(value.maximum_signed_improvement)},
          {"mean_signed_improvement",
           rational_json(value.mean_signed_improvement)},
          {"metric_name", value.metric_name},
          {"minimum_signed_improvement",
           rational_json(value.minimum_signed_improvement)},
          {"no_material_change_count", value.no_material_change_count}};
}

Json to_json(const RepeatedExperimentAggregate &value) {
  Json metrics = Json::array();
  for (const auto &metric : value.metric_evidence) {
    metrics.push_back(to_json(metric));
  }
  return {{"aggregate_signature", value.aggregate_signature},
          {"aggregation_version", value.aggregation_version},
          {"design_signature", value.design_signature},
          {"experiment_count", value.experiment_count},
          {"grounded_evidence_ids", value.grounded_evidence_ids},
          {"independence_groups", value.independence_groups},
          {"metric_evidence", metrics},
          {"minimum_required_experiments",
           value.minimum_required_experiments},
          {"minimum_required_independence_groups",
           value.minimum_required_independence_groups},
          {"qualification_required_before_canonical_reuse",
           value.qualification_required_before_canonical_reuse},
          {"result_signatures", value.result_signatures},
          {"schema_version", value.schema_version},
          {"status", value.status},
          {"sustained_improvement_qualified",
           value.sustained_improvement_qualified}};
}

} // namespace statewright::saa
