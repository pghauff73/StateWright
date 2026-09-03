#include "statewright/saa/algorithm_experiment.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <ranges>
#include <set>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[noreturn]] void experiment_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

[[nodiscard]] std::string trimmed(std::string value) {
  const auto first = value.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\n\r\f\v");
  return value.substr(first, last - first + 1U);
}

[[nodiscard]] std::string canonical_text(std::string value) {
  std::string result;
  bool pending_space = false;
  for (const char character : value) {
    if (std::isspace(static_cast<unsigned char>(character)) != 0) {
      pending_space = !result.empty();
      continue;
    }
    if (pending_space) {
      result.push_back(' ');
      pending_space = false;
    }
    result.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }
  return result;
}

[[nodiscard]] std::string uppercase(std::string value) {
  value = trimmed(std::move(value));
  std::ranges::transform(value, value.begin(), [](const char character) {
    return static_cast<char>(
        std::toupper(static_cast<unsigned char>(character)));
  });
  return value;
}

[[nodiscard]] std::vector<std::string>
canonical_texts(std::vector<std::string> values) {
  std::set<std::string> result;
  for (auto &value : values) {
    value = canonical_text(std::move(value));
    if (!value.empty()) {
      result.insert(std::move(value));
    }
  }
  return {result.begin(), result.end()};
}

[[nodiscard]] std::vector<std::string>
canonical_ids(std::vector<std::string> values) {
  std::set<std::string> result;
  for (auto &value : values) {
    value = trimmed(std::move(value));
    if (!value.empty()) {
      result.insert(std::move(value));
    }
  }
  return {result.begin(), result.end()};
}

[[nodiscard]] std::string canonical_sha(std::string value,
                                        std::string_view error) {
  value = trimmed(std::move(value));
  std::ranges::transform(value, value.begin(), [](const char character) {
    return static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  });
  if (value.size() != 64U ||
      !std::ranges::all_of(value, [](const char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
      })) {
    experiment_error(std::string(error));
  }
  return value;
}

[[nodiscard]] Json metric_values_json(
    const std::vector<std::pair<std::string, mpq_class>> &values) {
  Json result = Json::object();
  for (const auto &[name, value] : values) {
    result[name] = rational_json(value);
  }
  return result;
}

[[nodiscard]] Json boolean_values_json(
    const std::vector<std::pair<std::string, bool>> &values) {
  Json result = Json::object();
  for (const auto &[name, value] : values) {
    result[name] = value;
  }
  return result;
}

struct GroundedEvidence final {
  std::vector<std::string> evidence_ids;
  std::vector<std::string> independence_groups;
  int coverage_bp = 0;
};

[[nodiscard]] GroundedEvidence grounded_evidence(
    const ReasoningEvidenceResolver &resolver,
    const std::vector<std::string> &evidence_ids,
    const std::vector<std::string> &requirements) {
  std::set<std::string> grounded;
  std::set<std::string> groups;
  std::set<std::string> covered;
  const std::set<std::string> required(requirements.begin(),
                                        requirements.end());
  for (const auto &evidence_id : evidence_ids) {
    std::optional<ReasoningGroundingEvidence> record;
    try {
      record = resolver(evidence_id);
    } catch (...) {
      record = std::nullopt;
    }
    if (!record) {
      experiment_error("SAA-11.2 evidence is not registered: " +
                       evidence_id);
    }
    if (record->object_type != "egcf-evidence") {
      experiment_error(
          "SAA-11.2 evidence ID does not reference EvidenceArtifact");
    }
    if (record->success != true || record->simulated) {
      experiment_error(
          "SAA-11.2 evidence must be successful and non-simulated");
    }
    if ((!record->producer.starts_with("deterministic-") &&
         !record->producer.starts_with("human-")) ||
        record->method == "reported") {
      experiment_error("SAA-11.2 evidence must be deterministic/human "
                       "grounded and not reported-only");
    }
    grounded.insert(evidence_id);
    if (!record->independence_group.empty()) {
      groups.insert(record->independence_group);
    }
    for (const auto &requirement : record->requirement_ids) {
      covered.insert(canonical_text(requirement));
    }
  }
  const int coverage =
      required.empty()
          ? 10000
          : (10000 * static_cast<int>(std::ranges::count_if(
                         required, [&](const auto &requirement) {
                           return covered.contains(requirement);
                         }))) /
                static_cast<int>(required.size());
  return {.evidence_ids = {grounded.begin(), grounded.end()},
          .independence_groups = {groups.begin(), groups.end()},
          .coverage_bp = coverage};
}

} // namespace

ExperimentMetricSpec canonical_experiment_metric(ExperimentMetricSpec metric) {
  metric.name = canonical_text(std::move(metric.name));
  if (metric.name.empty()) {
    experiment_error("SAA-11.2 metric name is required");
  }
  metric.direction = uppercase(std::move(metric.direction));
  if (metric.direction != "HIGHER_IS_BETTER" &&
      metric.direction != "LOWER_IS_BETTER") {
    experiment_error("unsupported SAA-11.2 metric direction: " +
                     metric.direction);
  }
  metric.minimum_material_effect.canonicalize();
  if (metric.minimum_material_effect < 0) {
    experiment_error("minimum material effect cannot be negative");
  }
  return metric;
}

AlgorithmABExperimentDesign make_ab_experiment_design(
    std::string baseline_ref, std::string candidate_ref,
    std::string context_signature, std::vector<ExperimentMetricSpec> metrics,
    std::vector<std::string> required_invariants,
    std::vector<std::string> evidence_requirements, int minimum_trials,
    bool paired_context) {
  baseline_ref = trimmed(std::move(baseline_ref));
  candidate_ref = trimmed(std::move(candidate_ref));
  if (baseline_ref.empty() || candidate_ref.empty() ||
      baseline_ref == candidate_ref) {
    experiment_error(
        "SAA-11.2 A/B experiment requires distinct baseline and candidate refs");
  }
  context_signature = canonical_sha(
      std::move(context_signature),
      "SAA-11.2 context signature must be SHA-256");
  for (auto &metric : metrics) {
    metric = canonical_experiment_metric(std::move(metric));
  }
  if (metrics.empty() || metrics.size() > max_experiment_metrics) {
    experiment_error("SAA-11.2 metric count outside bounded range");
  }
  std::set<std::string> names;
  for (const auto &metric : metrics) {
    if (!names.insert(metric.name).second) {
      experiment_error("SAA-11.2 metric names must be unique");
    }
  }
  if (minimum_trials < 1 || minimum_trials > max_experiment_trials) {
    experiment_error("SAA-11.2 minimum trial count outside bounded range");
  }
  required_invariants = canonical_texts(std::move(required_invariants));
  evidence_requirements = canonical_texts(std::move(evidence_requirements));
  Json metric_payload = Json::array();
  for (const auto &metric : metrics) {
    metric_payload.push_back(to_json(metric));
  }
  const Json payload =
      {{"baseline_ref", baseline_ref},
       {"candidate_ref", candidate_ref},
       {"context_signature", context_signature},
       {"evidence_requirements", evidence_requirements},
       {"metrics", metric_payload},
       {"minimum_trials", minimum_trials},
       {"paired_context", paired_context},
       {"required_invariants", required_invariants},
       {"version", algorithm_experiment_version}};
  return {.schema_version = 1,
          .experiment_version = std::string(algorithm_experiment_version),
          .baseline_ref = std::move(baseline_ref),
          .candidate_ref = std::move(candidate_ref),
          .context_signature = std::move(context_signature),
          .metrics = std::move(metrics),
          .required_invariants = std::move(required_invariants),
          .evidence_requirements = std::move(evidence_requirements),
          .minimum_trials = minimum_trials,
          .paired_context = paired_context,
          .design_signature = contracts::sha256_json(payload)};
}

AlgorithmVariantObservation make_variant_observation(
    const AlgorithmABExperimentDesign &design, std::string variant_ref,
    std::vector<std::pair<std::string, mpq_class>> metric_values,
    std::vector<std::string> evidence_ids,
    std::vector<std::pair<std::string, bool>> invariant_results,
    int trial_count, bool execution_success) {
  variant_ref = trimmed(std::move(variant_ref));
  if (variant_ref != design.baseline_ref && variant_ref != design.candidate_ref) {
    experiment_error(
        "SAA-11.2 observation variant is not part of experiment design");
  }
  std::map<std::string, mpq_class> supplied_metrics;
  for (auto &[name, value] : metric_values) {
    name = canonical_text(std::move(name));
    value.canonicalize();
    if (!supplied_metrics.emplace(std::move(name), std::move(value)).second) {
      experiment_error("SAA-11.2 observation metric names must be unique");
    }
  }
  std::set<std::string> expected_names;
  for (const auto &metric : design.metrics) {
    expected_names.insert(metric.name);
  }
  std::set<std::string> supplied_names;
  for (const auto &[name, value] : supplied_metrics) {
    static_cast<void>(value);
    supplied_names.insert(name);
  }
  if (supplied_names != expected_names) {
    experiment_error("SAA-11.2 observation must provide every designed metric "
                     "and no extras");
  }
  std::vector<std::pair<std::string, mpq_class>> canonical_metrics;
  for (const auto &metric : design.metrics) {
    canonical_metrics.emplace_back(metric.name,
                                   supplied_metrics.at(metric.name));
  }
  std::map<std::string, bool> supplied_invariants;
  for (auto &[name, result] : invariant_results) {
    name = canonical_text(std::move(name));
    if (!supplied_invariants.emplace(std::move(name), result).second) {
      experiment_error(
          "SAA-11.2 observation invariant names must be unique");
    }
  }
  std::set<std::string> invariant_names;
  for (const auto &[name, result] : supplied_invariants) {
    static_cast<void>(result);
    invariant_names.insert(name);
  }
  const std::set<std::string> required_invariants(
      design.required_invariants.begin(), design.required_invariants.end());
  if (invariant_names != required_invariants) {
    experiment_error("SAA-11.2 observation must report every required "
                     "invariant and no extras");
  }
  std::vector<std::pair<std::string, bool>> canonical_invariants(
      supplied_invariants.begin(), supplied_invariants.end());
  evidence_ids = canonical_ids(std::move(evidence_ids));
  if (evidence_ids.empty()) {
    experiment_error("SAA-11.2 observation requires evidence references");
  }
  if (trial_count < design.minimum_trials ||
      trial_count > max_experiment_trials) {
    experiment_error(
        "SAA-11.2 observation trial count violates design bounds");
  }
  const Json payload =
      {{"design_signature", design.design_signature},
       {"evidence_ids", evidence_ids},
       {"execution_success", execution_success},
       {"invariant_results", boolean_values_json(canonical_invariants)},
       {"metric_values", metric_values_json(canonical_metrics)},
       {"trial_count", trial_count},
       {"variant_ref", variant_ref},
       {"version", algorithm_experiment_version}};
  return {.schema_version = 1,
          .experiment_version = std::string(algorithm_experiment_version),
          .design_signature = design.design_signature,
          .variant_ref = std::move(variant_ref),
          .metric_values = std::move(canonical_metrics),
          .evidence_ids = std::move(evidence_ids),
          .invariant_results = std::move(canonical_invariants),
          .trial_count = trial_count,
          .execution_success = execution_success,
          .observation_signature = contracts::sha256_json(payload)};
}

AlgorithmABExperimentResult qualify_ab_experiment(
    const ReasoningEvidenceResolver &evidence_resolver,
    const AlgorithmABExperimentDesign &design,
    const AlgorithmVariantObservation &baseline,
    const AlgorithmVariantObservation &candidate, bool independent_review) {
  if (baseline.design_signature != design.design_signature ||
      candidate.design_signature != design.design_signature) {
    experiment_error(
        "SAA-11.2 observations belong to a different experiment design");
  }
  if (baseline.variant_ref != design.baseline_ref ||
      candidate.variant_ref != design.candidate_ref) {
    experiment_error(
        "SAA-11.2 baseline/candidate observations are swapped or mismatched");
  }
  std::string status =
      !baseline.execution_success || !candidate.execution_success
          ? "EXPERIMENT_EXECUTION_FAILED"
          : "";
  const auto baseline_evidence = grounded_evidence(
      evidence_resolver, baseline.evidence_ids, design.evidence_requirements);
  const auto candidate_evidence = grounded_evidence(
      evidence_resolver, candidate.evidence_ids, design.evidence_requirements);
  std::set<std::string> evidence_set(baseline_evidence.evidence_ids.begin(),
                                     baseline_evidence.evidence_ids.end());
  evidence_set.insert(candidate_evidence.evidence_ids.begin(),
                      candidate_evidence.evidence_ids.end());
  std::set<std::string> group_set(
      baseline_evidence.independence_groups.begin(),
      baseline_evidence.independence_groups.end());
  group_set.insert(candidate_evidence.independence_groups.begin(),
                   candidate_evidence.independence_groups.end());
  std::vector<std::string> evidence_ids(evidence_set.begin(),
                                        evidence_set.end());
  std::vector<std::string> groups(group_set.begin(), group_set.end());
  const int coverage = std::min(baseline_evidence.coverage_bp,
                                candidate_evidence.coverage_bp);
  const std::map<std::string, bool> baseline_invariants(
      baseline.invariant_results.begin(), baseline.invariant_results.end());
  const std::map<std::string, bool> candidate_invariants(
      candidate.invariant_results.begin(), candidate.invariant_results.end());
  const bool invariant_gate =
      std::ranges::all_of(design.required_invariants, [&](const auto &name) {
        const auto left = baseline_invariants.find(name);
        const auto right = candidate_invariants.find(name);
        return left != baseline_invariants.end() && left->second &&
               right != candidate_invariants.end() && right->second;
      });
  const std::map<std::string, mpq_class> baseline_metrics(
      baseline.metric_values.begin(), baseline.metric_values.end());
  const std::map<std::string, mpq_class> candidate_metrics(
      candidate.metric_values.begin(), candidate.metric_values.end());
  std::vector<ExperimentMetricComparison> comparisons;
  int improved = 0;
  int regressed = 0;
  for (const auto &metric : design.metrics) {
    const mpq_class left = baseline_metrics.at(metric.name);
    const mpq_class right = candidate_metrics.at(metric.name);
    const mpq_class signed_improvement =
        metric.direction == "HIGHER_IS_BETTER" ? right - left
                                                : left - right;
    std::string metric_status;
    if (signed_improvement > 0 &&
        signed_improvement >= metric.minimum_material_effect) {
      metric_status = "MATERIAL_IMPROVEMENT";
      ++improved;
    } else if (signed_improvement < 0 &&
               -signed_improvement >= metric.minimum_material_effect) {
      metric_status = "MATERIAL_REGRESSION";
      ++regressed;
    } else {
      metric_status = "NO_MATERIAL_CHANGE";
    }
    comparisons.push_back(
        {.metric_name = metric.name,
         .direction = metric.direction,
         .baseline_value = left,
         .candidate_value = right,
         .signed_improvement = signed_improvement,
         .minimum_material_effect = metric.minimum_material_effect,
         .status = std::move(metric_status)});
  }
  if (status.empty()) {
    if (coverage != 10000) {
      status = "EXPERIMENT_EVIDENCE_INCOMPLETE";
    } else if (!invariant_gate) {
      status = "EXPERIMENT_INVARIANT_VIOLATION";
    } else if (!independent_review) {
      status = "EXPERIMENT_REVIEW_REQUIRED";
    } else if (improved != 0 && regressed != 0) {
      status = "EXPERIMENT_TRADEOFF_UNRESOLVED";
    } else if (regressed != 0) {
      status = "CANDIDATE_REGRESSION_DETECTED";
    } else if (improved != 0) {
      status = "CANDIDATE_IMPROVEMENT_QUALIFIED";
    } else {
      status = "NO_MATERIAL_IMPROVEMENT";
    }
  }
  const bool qualified = status == "CANDIDATE_IMPROVEMENT_QUALIFIED";
  Json comparison_payload = Json::array();
  for (const auto &comparison : comparisons) {
    comparison_payload.push_back(to_json(comparison));
  }
  const Json payload =
      {{"baseline_observation_signature", baseline.observation_signature},
       {"candidate_improvement_qualified", qualified},
       {"candidate_observation_signature", candidate.observation_signature},
       {"comparisons", comparison_payload},
       {"design_signature", design.design_signature},
       {"evidence_requirement_coverage_bp", coverage},
       {"grounded_evidence_ids", evidence_ids},
       {"independence_groups", groups},
       {"independent_review", independent_review},
       {"invariant_gate_passed", invariant_gate},
       {"status", status},
       {"version", algorithm_experiment_version}};
  return {.schema_version = 1,
          .experiment_version = std::string(algorithm_experiment_version),
          .design_signature = design.design_signature,
          .baseline_observation_signature = baseline.observation_signature,
          .candidate_observation_signature = candidate.observation_signature,
          .metric_comparisons = std::move(comparisons),
          .grounded_evidence_ids = std::move(evidence_ids),
          .independence_groups = std::move(groups),
          .evidence_requirement_coverage_bp = coverage,
          .invariant_gate_passed = invariant_gate,
          .independent_review = independent_review,
          .status = std::move(status),
          .candidate_improvement_qualified = qualified,
          .qualification_required_before_canonical_reuse = true,
          .result_signature = contracts::sha256_json(payload)};
}

Json to_json(const ExperimentMetricSpec &value) {
  return {{"direction", value.direction},
          {"minimum_material_effect",
           rational_json(value.minimum_material_effect)},
          {"name", value.name}};
}

Json to_json(const AlgorithmABExperimentDesign &value) {
  Json metrics = Json::array();
  for (const auto &metric : value.metrics) {
    metrics.push_back(to_json(metric));
  }
  return {{"baseline_ref", value.baseline_ref},
          {"candidate_ref", value.candidate_ref},
          {"context_signature", value.context_signature},
          {"design_signature", value.design_signature},
          {"evidence_requirements", value.evidence_requirements},
          {"experiment_version", value.experiment_version},
          {"metrics", metrics},
          {"minimum_trials", value.minimum_trials},
          {"paired_context", value.paired_context},
          {"required_invariants", value.required_invariants},
          {"schema_version", value.schema_version}};
}

Json to_json(const AlgorithmVariantObservation &value) {
  return {{"design_signature", value.design_signature},
          {"evidence_ids", value.evidence_ids},
          {"execution_success", value.execution_success},
          {"experiment_version", value.experiment_version},
          {"invariant_results", boolean_values_json(value.invariant_results)},
          {"metric_values", metric_values_json(value.metric_values)},
          {"observation_signature", value.observation_signature},
          {"schema_version", value.schema_version},
          {"trial_count", value.trial_count},
          {"variant_ref", value.variant_ref}};
}

Json to_json(const ExperimentMetricComparison &value) {
  return {{"baseline_value", rational_json(value.baseline_value)},
          {"candidate_value", rational_json(value.candidate_value)},
          {"direction", value.direction},
          {"metric_name", value.metric_name},
          {"minimum_material_effect",
           rational_json(value.minimum_material_effect)},
          {"signed_improvement", rational_json(value.signed_improvement)},
          {"status", value.status}};
}

Json to_json(const AlgorithmABExperimentResult &value) {
  Json comparisons = Json::array();
  for (const auto &comparison : value.metric_comparisons) {
    comparisons.push_back(to_json(comparison));
  }
  return {{"baseline_observation_signature",
           value.baseline_observation_signature},
          {"candidate_improvement_qualified",
           value.candidate_improvement_qualified},
          {"candidate_observation_signature",
           value.candidate_observation_signature},
          {"design_signature", value.design_signature},
          {"evidence_requirement_coverage_bp",
           value.evidence_requirement_coverage_bp},
          {"experiment_version", value.experiment_version},
          {"grounded_evidence_ids", value.grounded_evidence_ids},
          {"independence_groups", value.independence_groups},
          {"independent_review", value.independent_review},
          {"invariant_gate_passed", value.invariant_gate_passed},
          {"metric_comparisons", comparisons},
          {"qualification_required_before_canonical_reuse",
           value.qualification_required_before_canonical_reuse},
          {"result_signature", value.result_signature},
          {"schema_version", value.schema_version},
          {"status", value.status}};
}

} // namespace statewright::saa
