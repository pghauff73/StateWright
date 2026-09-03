#include "statewright/reasoning/evaluation.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <utility>

namespace statewright::reasoning {
namespace {

constexpr int critical_contradiction_bp = 7'500;

constexpr std::array<std::string_view, 3> verifier_verdicts{
    "ACCEPT", "REVISE", "REJECT"};
constexpr std::array<std::string_view, 3> falsifier_verdicts{
    "SURVIVES", "REVISE", "REJECT"};
constexpr std::array<std::string_view, 7> contradiction_types{
    "logical", "empirical", "causal", "scope", "temporal", "definition",
    "measurement"};
constexpr std::array<std::string_view, 4> contradiction_statuses{
    "UNRESOLVED", "QUALIFIED", "RESOLVED", "SUPERSEDED"};
constexpr std::array<std::string_view, 7> reasoning_operations{
    "GENERATE_HYPOTHESIS", "RETRIEVE_EVIDENCE", "RUN_READ_ONLY_EXPERIMENT",
    "VERIFY_AGAIN", "SEARCH_COUNTEREXAMPLE", "REFINE_DIMENSION", "STOP"};

[[noreturn]] void policy_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied, std::move(message));
}

template <std::size_t Size>
[[nodiscard]] bool contains(
    const std::array<std::string_view, Size> &values,
    std::string_view candidate) noexcept {
  return std::ranges::find(values, candidate) != values.end();
}

[[nodiscard]] std::string trim(std::string value) {
  const auto first = std::ranges::find_if_not(
      value, [](unsigned char character) { return std::isspace(character) != 0; });
  const auto last = std::find_if_not(
      value.rbegin(), value.rend(),
      [](unsigned char character) { return std::isspace(character) != 0; })
                        .base();
  if (first >= last) {
    return {};
  }
  return std::string(first, last);
}

[[nodiscard]] std::vector<std::string>
canonical_strings(const std::vector<std::string> &values) {
  std::set<std::string> unique;
  for (const auto &value : values) {
    if (!value.empty()) {
      unique.insert(value);
    }
  }
  return {unique.begin(), unique.end()};
}

[[nodiscard]] std::vector<std::string>
stable_strings(const std::vector<std::string> &values) {
  std::set<std::string> seen;
  std::vector<std::string> result;
  for (const auto &value : values) {
    if (!value.empty() && seen.insert(value).second) {
      result.push_back(value);
    }
  }
  return result;
}

[[nodiscard]] ScorePairs canonical_score_pairs(const ScorePairs &values) {
  std::map<std::string, int> unique;
  for (const auto &[name, value] : values) {
    if (name.empty()) {
      policy_error("score keys must be non-empty");
    }
    unique[name] = value;
  }
  return {unique.begin(), unique.end()};
}

void validate_score(int value, std::string_view label, bool signed_score = false) {
  const int minimum = signed_score ? -score_scale : 0;
  if (value < minimum || value > score_scale) {
    policy_error(std::string(label) + " must be " +
                 (signed_score ? "-10000..10000" : "0..10000"));
  }
}

[[nodiscard]] std::string normalized_conclusion(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  std::string result;
  bool separated = true;
  for (const char byte : value) {
    const auto character = static_cast<unsigned char>(byte);
    if (std::isspace(character) != 0) {
      separated = true;
      continue;
    }
    if (!result.empty() && separated) {
      result.push_back(' ');
    }
    result.push_back(static_cast<char>(character));
    separated = false;
  }
  return result;
}

template <typename Value, typename Member>
void sort_by(std::vector<Value> &values, Member member) {
  std::ranges::sort(values, {}, member);
}

template <typename Value, typename Member>
[[nodiscard]] std::map<std::string, Value>
unique_by(const std::vector<Value> &values, Member member,
          std::string_view label) {
  std::map<std::string, Value> result;
  for (const auto &value : values) {
    const auto &key = value.*member;
    if (!result.emplace(key, value).second) {
      policy_error(std::string(label) + " identities must be unique");
    }
  }
  return result;
}

[[nodiscard]] int jaccard_bp(const std::vector<std::string> &left,
                             const std::vector<std::string> &right) {
  const std::set<std::string> left_set(left.begin(), left.end());
  const std::set<std::string> right_set(right.begin(), right.end());
  if (left_set.empty() && right_set.empty()) {
    return score_scale;
  }
  std::vector<std::string> intersection;
  std::ranges::set_intersection(left_set, right_set,
                                std::back_inserter(intersection));
  std::vector<std::string> union_values;
  std::ranges::set_union(left_set, right_set,
                         std::back_inserter(union_values));
  return static_cast<int>(intersection.size() *
                          static_cast<std::size_t>(score_scale) /
                          union_values.size());
}

[[nodiscard]] std::vector<std::string>
json_strings(const contracts::Json &value, std::string_view key) {
  return value.at(std::string(key)).get<std::vector<std::string>>();
}

[[nodiscard]] std::vector<std::string>
path_evidence(const ReasoningPath &path) {
  std::vector<std::string> evidence;
  for (const auto &step : path.steps) {
    evidence.insert(evidence.end(), step.evidence_ids.begin(),
                    step.evidence_ids.end());
  }
  return canonical_strings(evidence);
}

} // namespace

contracts::Json to_json(const ReasoningProblem &value) {
  return {{"schema_version", value.schema_version},
          {"problem_id", value.problem_id},
          {"statement", value.statement},
          {"goal", value.goal},
          {"source_snapshot_hash", value.source_snapshot_hash},
          {"boundary_signature", value.boundary_signature},
          {"dimension_signature", value.dimension_signature},
          {"evidence_ids", value.evidence_ids},
          {"uncertainty_bp", value.uncertainty_bp},
          {"difficulty_bp", value.difficulty_bp},
          {"mutually_exclusive_hypotheses",
           value.mutually_exclusive_hypotheses},
          {"signature", value.signature}};
}

contracts::Json to_json(const ReasoningStep &value) {
  return {{"step_id", value.step_id},
          {"claim", value.claim},
          {"premises", value.premises},
          {"evidence_ids", value.evidence_ids},
          {"inference", value.inference},
          {"confidence_bp", value.confidence_bp},
          {"assumptions", value.assumptions},
          {"falsifier", value.falsifier},
          {"signature", value.signature}};
}

contracts::Json to_json(const ReasoningPath &value) {
  contracts::Json steps = contracts::Json::array();
  for (const auto &step : value.steps) {
    steps.push_back(to_json(step));
  }
  return {{"path_id", value.path_id},
          {"perspective", value.perspective},
          {"hypothesis_ids", value.hypothesis_ids},
          {"steps", std::move(steps)},
          {"conclusion", value.conclusion},
          {"provider_confidence_bp", value.provider_confidence_bp},
          {"estimated_cost_bp", value.estimated_cost_bp},
          {"goal_relevance_bp", value.goal_relevance_bp},
          {"risk_bp", value.risk_bp},
          {"topology_node_ids", value.topology_node_ids},
          {"topology_edge_ids", value.topology_edge_ids},
          {"structure_signature", value.structure_signature},
          {"diversity_bp", value.diversity_bp},
          {"signature", value.signature}};
}

contracts::Json to_json(const VerifierReport &value) {
  return {{"report_id", value.report_id},
          {"path_id", value.path_id},
          {"step_scores", value.step_scores},
          {"failures", value.failures},
          {"contradictions", value.contradictions},
          {"unsupported_nodes", value.unsupported_nodes},
          {"missing_assumptions", value.missing_assumptions},
          {"premise_validity_bp", value.premise_validity_bp},
          {"evidence_support_bp", value.evidence_support_bp},
          {"inference_quality_bp", value.inference_quality_bp},
          {"consistency_bp", value.consistency_bp},
          {"completeness_bp", value.completeness_bp},
          {"weakest_step_bp", value.weakest_step_bp},
          {"score_bp", value.score_bp},
          {"verdict", value.verdict},
          {"signature", value.signature}};
}

contracts::Json to_json(const FalsifierReport &value) {
  return {{"report_id", value.report_id},
          {"path_id", value.path_id},
          {"searched_falsifiers", value.searched_falsifiers},
          {"counterexamples", value.counterexamples},
          {"contradicted_step_ids", value.contradicted_step_ids},
          {"unresolved_defeat_conditions",
           value.unresolved_defeat_conditions},
          {"unresolved_defeat_evidence_ids",
           value.unresolved_defeat_evidence_ids},
          {"alternative_explanations", value.alternative_explanations},
          {"boundary_cases", value.boundary_cases},
          {"reversed_causal_directions", value.reversed_causal_directions},
          {"invalid_invariants", value.invalid_invariants},
          {"evidence_reversal_conditions",
           value.evidence_reversal_conditions},
          {"severity_bp", value.severity_bp},
          {"survival_bp", value.survival_bp},
          {"residual_uncertainty_bp", value.residual_uncertainty_bp},
          {"verdict", value.verdict},
          {"signature", value.signature}};
}

contracts::Json to_json(const DiversityConfiguration &value) {
  return {{"schema_version", value.schema_version},
          {"config_id", value.config_id},
          {"hypothesis_weight", value.hypothesis_weight},
          {"evidence_weight", value.evidence_weight},
          {"inference_weight", value.inference_weight},
          {"assumption_weight", value.assumption_weight},
          {"falsifier_weight", value.falsifier_weight},
          {"conclusion_weight", value.conclusion_weight},
          {"duplicate_threshold_bp", value.duplicate_threshold_bp},
          {"signature", value.signature}};
}

contracts::Json to_json(const ScoreConfiguration &value) {
  return {{"schema_version", value.schema_version},
          {"config_id", value.config_id},
          {"verifier_weight", value.verifier_weight},
          {"evidence_weight", value.evidence_weight},
          {"consistency_weight", value.consistency_weight},
          {"falsifier_weight", value.falsifier_weight},
          {"goal_weight", value.goal_weight},
          {"diversity_weight", value.diversity_weight},
          {"uncertainty_penalty", value.uncertainty_penalty},
          {"compute_penalty", value.compute_penalty},
          {"minimum_survivor_bp", value.minimum_survivor_bp},
          {"signature", value.signature}};
}

contracts::Json to_json(const SynthesisResult &value) {
  return {{"schema_version", value.schema_version},
          {"winning_path_id", value.winning_path_id},
          {"synthesized_path_id", value.synthesized_path_id},
          {"source_path_ids", value.source_path_ids},
          {"accepted_node_ids", value.accepted_node_ids},
          {"rejected_node_ids", value.rejected_node_ids},
          {"merged_conclusion", value.merged_conclusion},
          {"remaining_uncertainties", value.remaining_uncertainties},
          {"confidence_bp", value.confidence_bp},
          {"verifier_report_id", value.verifier_report_id},
          {"topology_signature", value.topology_signature},
          {"verified", value.verified},
          {"fallback_used", value.fallback_used},
          {"failure_reasons", value.failure_reasons},
          {"signature", value.signature}};
}

contracts::Json to_json(const ContradictionRecord &value) {
  return {{"schema_version", value.schema_version},
          {"contradiction_id", value.contradiction_id},
          {"left_claim_id", value.left_claim_id},
          {"right_claim_id", value.right_claim_id},
          {"evidence_left", value.evidence_left},
          {"evidence_right", value.evidence_right},
          {"conflict_type", value.conflict_type},
          {"severity_bp", value.severity_bp},
          {"resolution_status", value.resolution_status},
          {"resolution_evidence_ids", value.resolution_evidence_ids},
          {"signature", value.signature}};
}

contracts::Json to_json(const ReasoningMetrics &value) {
  return {{"path_id", value.path_id},
          {"evidence_support_bp", value.evidence_support_bp},
          {"verifier_bp", value.verifier_bp},
          {"consistency_bp", value.consistency_bp},
          {"falsifier_bp", value.falsifier_bp},
          {"goal_relevance_bp", value.goal_relevance_bp},
          {"diversity_bp", value.diversity_bp},
          {"uncertainty_bp", value.uncertainty_bp},
          {"risk_bp", value.risk_bp},
          {"cost_bp", value.cost_bp},
          {"total_score_bp", value.total_score_bp},
          {"score_config_id", value.score_config_id},
          {"score_config_hash", value.score_config_hash},
          {"signature", value.signature}};
}

contracts::Json to_json(const CandidateSet &value) {
  contracts::Json paths = contracts::Json::array();
  for (const auto &path : value.paths) {
    paths.push_back(to_json(path));
  }
  contracts::Json verifiers = contracts::Json::array();
  for (const auto &report : value.verifier_reports) {
    verifiers.push_back(to_json(report));
  }
  contracts::Json falsifiers = contracts::Json::array();
  for (const auto &report : value.falsifier_reports) {
    falsifiers.push_back(to_json(report));
  }
  contracts::Json metrics = contracts::Json::array();
  for (const auto &item : value.metrics) {
    metrics.push_back(to_json(item));
  }
  contracts::Json updates = contracts::Json::array();
  for (const auto &record : value.hypothesis_updates) {
    updates.push_back(to_json(record));
  }
  return {{"schema_version", value.schema_version},
          {"problem_id", value.problem_id},
          {"paths", std::move(paths)},
          {"verifier_reports", std::move(verifiers)},
          {"falsifier_reports", std::move(falsifiers)},
          {"metrics", std::move(metrics)},
          {"selected_path_id", value.selected_path_id},
          {"surviving_path_ids", value.surviving_path_ids},
          {"rejected_path_ids", value.rejected_path_ids},
          {"synthesized_conclusion", value.synthesized_conclusion},
          {"synthesis_source_path_ids", value.synthesis_source_path_ids},
          {"synthesis", value.synthesis ? to_json(*value.synthesis)
                                         : contracts::Json(nullptr)},
          {"score_config_id", value.score_config_id},
          {"score_config_hash", value.score_config_hash},
          {"diversity_config_hash", value.diversity_config_hash},
          {"ablation_id", value.ablation_id},
          {"ablation_config_hash", value.ablation_config_hash},
          {"contradiction_ids", value.contradiction_ids},
          {"hypothesis_updates", std::move(updates)},
          {"signature", value.signature}};
}

contracts::Json to_json(const ReasoningBudget &value) {
  return {{"schema_version", value.schema_version},
          {"max_hypotheses", value.max_hypotheses},
          {"minimum_candidates", value.minimum_candidates},
          {"maximum_candidates", value.maximum_candidates},
          {"candidate_count", value.candidate_count},
          {"max_steps_per_path", value.max_steps_per_path},
          {"max_branch_factor", value.max_branch_factor},
          {"max_topology_nodes", value.max_topology_nodes},
          {"max_topology_edges", value.max_topology_edges},
          {"max_provider_calls", value.max_provider_calls},
          {"max_generation_attempts", value.max_generation_attempts},
          {"verifier_count", value.verifier_count},
          {"falsifier_count", value.falsifier_count},
          {"max_verifier_passes", value.max_verifier_passes},
          {"max_falsifier_passes", value.max_falsifier_passes},
          {"max_tokens", value.max_tokens},
          {"max_tool_calls", value.max_tool_calls},
          {"max_context_items", value.max_context_items},
          {"operation_costs_bp", value.operation_costs_bp},
          {"max_compute_bp", value.max_compute_bp},
          {"minimum_voi_bp", value.minimum_voi_bp},
          {"signature", value.signature}};
}

ReasoningProblem canonicalize_reasoning_problem(ReasoningProblem value) {
  if (trim(value.statement).empty()) {
    policy_error("reasoning problem statement must be non-empty");
  }
  if (trim(value.goal).empty()) {
    policy_error("reasoning problem goal must be non-empty");
  }
  validate_score(value.uncertainty_bp, "problem uncertainty");
  validate_score(value.difficulty_bp, "problem difficulty");
  value.evidence_ids = canonical_strings(value.evidence_ids);
  return value;
}

ReasoningStep canonicalize_reasoning_step(ReasoningStep value) {
  if (value.step_id.empty()) {
    policy_error("reasoning step ID must be non-empty");
  }
  if (trim(value.claim).empty()) {
    policy_error("reasoning step claim must be non-empty");
  }
  value.inference = canonical_inference_mode(value.inference);
  validate_score(value.confidence_bp, "reasoning step confidence");
  value.premises = stable_strings(value.premises);
  value.evidence_ids = canonical_strings(value.evidence_ids);
  value.assumptions = canonical_strings(value.assumptions);
  return value;
}

ReasoningPath canonicalize_reasoning_path(ReasoningPath value) {
  if (value.path_id.empty() || value.perspective.empty()) {
    policy_error("reasoning path identity must be non-empty");
  }
  if (value.steps.empty()) {
    policy_error("reasoning path must contain at least one step");
  }
  if (trim(value.conclusion).empty()) {
    policy_error("reasoning path conclusion must be non-empty");
  }
  validate_score(value.provider_confidence_bp, "provider_confidence_bp");
  validate_score(value.estimated_cost_bp, "estimated_cost_bp");
  validate_score(value.goal_relevance_bp, "goal_relevance_bp");
  validate_score(value.risk_bp, "risk_bp");
  validate_score(value.diversity_bp, "diversity_bp");
  value.hypothesis_ids = canonical_strings(value.hypothesis_ids);
  value.topology_node_ids = canonical_strings(value.topology_node_ids);
  value.topology_edge_ids = canonical_strings(value.topology_edge_ids);
  std::set<std::string> step_ids;
  for (auto &step : value.steps) {
    step = canonicalize_reasoning_step(std::move(step));
    if (!step_ids.insert(step.step_id).second) {
      policy_error("reasoning path step IDs must be unique");
    }
  }
  return value;
}

VerifierReport canonicalize_verifier_report(VerifierReport value) {
  if (value.report_id.empty() || value.path_id.empty()) {
    policy_error("verifier report identity must be non-empty");
  }
  validate_score(value.score_bp, "verifier score");
  validate_score(value.premise_validity_bp, "premise_validity_bp");
  validate_score(value.evidence_support_bp, "evidence_support_bp");
  validate_score(value.inference_quality_bp, "inference_quality_bp");
  validate_score(value.consistency_bp, "consistency_bp");
  validate_score(value.completeness_bp, "completeness_bp");
  validate_score(value.weakest_step_bp, "weakest_step_bp");
  if (!contains(verifier_verdicts, value.verdict)) {
    policy_error("invalid verifier verdict: " + value.verdict);
  }
  value.step_scores = canonical_score_pairs(value.step_scores);
  for (const auto &[name, score] : value.step_scores) {
    static_cast<void>(name);
    validate_score(score, "verifier step scores");
  }
  value.failures = canonical_strings(value.failures);
  value.contradictions = canonical_strings(value.contradictions);
  value.unsupported_nodes = canonical_strings(value.unsupported_nodes);
  value.missing_assumptions = canonical_strings(value.missing_assumptions);
  return value;
}

FalsifierReport canonicalize_falsifier_report(FalsifierReport value) {
  if (value.report_id.empty() || value.path_id.empty()) {
    policy_error("falsifier report identity must be non-empty");
  }
  validate_score(value.survival_bp, "falsifier survival");
  validate_score(value.severity_bp, "falsifier severity");
  validate_score(value.residual_uncertainty_bp,
                 "falsifier residual uncertainty");
  if (!contains(falsifier_verdicts, value.verdict)) {
    policy_error("invalid falsifier verdict: " + value.verdict);
  }
  value.searched_falsifiers = canonical_strings(value.searched_falsifiers);
  value.counterexamples = canonical_strings(value.counterexamples);
  value.contradicted_step_ids = canonical_strings(value.contradicted_step_ids);
  value.unresolved_defeat_conditions =
      canonical_strings(value.unresolved_defeat_conditions);
  value.unresolved_defeat_evidence_ids =
      canonical_strings(value.unresolved_defeat_evidence_ids);
  value.alternative_explanations =
      canonical_strings(value.alternative_explanations);
  value.boundary_cases = canonical_strings(value.boundary_cases);
  value.reversed_causal_directions =
      canonical_strings(value.reversed_causal_directions);
  value.invalid_invariants = canonical_strings(value.invalid_invariants);
  value.evidence_reversal_conditions =
      canonical_strings(value.evidence_reversal_conditions);
  return value;
}

ReasoningMetrics canonicalize_reasoning_metrics(ReasoningMetrics value) {
  if (value.path_id.empty()) {
    policy_error("reasoning metrics path_id must be non-empty");
  }
  validate_score(value.evidence_support_bp, "evidence_support_bp");
  validate_score(value.verifier_bp, "verifier_bp");
  validate_score(value.consistency_bp, "consistency_bp");
  validate_score(value.falsifier_bp, "falsifier_bp");
  validate_score(value.goal_relevance_bp, "goal_relevance_bp");
  validate_score(value.diversity_bp, "diversity_bp");
  validate_score(value.uncertainty_bp, "uncertainty_bp");
  validate_score(value.risk_bp, "risk_bp");
  validate_score(value.cost_bp, "cost_bp");
  validate_score(value.total_score_bp, "reasoning total score", true);
  return value;
}

CandidateSet canonicalize_candidate_set(CandidateSet value) {
  for (auto &path : value.paths) {
    path = canonicalize_reasoning_path(std::move(path));
  }
  for (auto &report : value.verifier_reports) {
    report = canonicalize_verifier_report(std::move(report));
  }
  for (auto &report : value.falsifier_reports) {
    report = canonicalize_falsifier_report(std::move(report));
  }
  for (auto &metrics : value.metrics) {
    metrics = canonicalize_reasoning_metrics(std::move(metrics));
  }
  sort_by(value.paths, &ReasoningPath::path_id);
  sort_by(value.verifier_reports, &VerifierReport::path_id);
  sort_by(value.falsifier_reports, &FalsifierReport::path_id);
  sort_by(value.metrics, &ReasoningMetrics::path_id);
  value.surviving_path_ids = canonical_strings(value.surviving_path_ids);
  value.rejected_path_ids = canonical_strings(value.rejected_path_ids);
  value.synthesis_source_path_ids =
      canonical_strings(value.synthesis_source_path_ids);
  value.contradiction_ids = canonical_strings(value.contradiction_ids);
  sort_by(value.hypothesis_updates, &HypothesisUpdateRecord::update_id);
  if (value.synthesis) {
    value.synthesis = make_synthesis_result(std::move(*value.synthesis));
    if (!value.synthesized_conclusion.empty() &&
        value.synthesized_conclusion != value.synthesis->merged_conclusion) {
      policy_error("candidate synthesis projection mismatch");
    }
    value.synthesized_conclusion = value.synthesis->merged_conclusion;
    value.synthesis_source_path_ids = value.synthesis->source_path_ids;
  }
  return value;
}

ReasoningBudget canonicalize_reasoning_budget(ReasoningBudget value) {
  const std::array<int, 16> positive{
      value.max_hypotheses,       value.minimum_candidates,
      value.maximum_candidates,  value.candidate_count,
      value.max_steps_per_path,   value.max_branch_factor,
      value.max_topology_nodes,   value.max_topology_edges,
      value.max_provider_calls,   value.max_generation_attempts,
      value.verifier_count,       value.max_verifier_passes,
      value.max_falsifier_passes, value.max_tokens,
      value.max_tool_calls,       value.max_context_items};
  if (std::ranges::any_of(positive, [](int item) { return item < 1; })) {
    policy_error("reasoning budget limits must be positive");
  }
  if (value.candidate_count < value.minimum_candidates ||
      value.candidate_count > value.maximum_candidates) {
    policy_error("candidate count must fit the reasoning budget");
  }
  if (value.verifier_count > value.candidate_count) {
    policy_error("verifier count cannot exceed candidate count");
  }
  if (value.max_generation_attempts < value.candidate_count) {
    policy_error("generation attempts cannot be below candidate count");
  }
  if (value.falsifier_count < 0 ||
      value.falsifier_count > value.verifier_count) {
    policy_error("falsifier count must fit verified candidates");
  }
  validate_score(value.max_compute_bp, "maximum reasoning compute");
  validate_score(value.minimum_voi_bp, "minimum value of information");
  value.operation_costs_bp = canonical_score_pairs(value.operation_costs_bp);
  for (const auto &[name, cost] : value.operation_costs_bp) {
    if (!contains(reasoning_operations, name)) {
      policy_error("reasoning budget contains an unknown operation cost");
    }
    validate_score(cost, "reasoning operation costs");
  }
  return value;
}

DiversityConfiguration
make_diversity_configuration(DiversityConfiguration value) {
  if (value.schema_version != 1) {
    policy_error("diversity configuration schema_version must be 1");
  }
  if (trim(value.config_id).empty()) {
    policy_error("diversity configuration ID must be non-empty");
  }
  const std::array<int, 6> weights{
      value.hypothesis_weight, value.evidence_weight, value.inference_weight,
      value.assumption_weight, value.falsifier_weight, value.conclusion_weight};
  if (std::ranges::any_of(weights,
                          [](int weight) { return weight < 0 || weight > 100; })) {
    policy_error("diversity weights must be 0..100");
  }
  if (std::accumulate(weights.begin(), weights.end(), 0) != 100) {
    policy_error("diversity weights must sum to 100");
  }
  validate_score(value.duplicate_threshold_bp,
                 "diversity duplicate threshold");
  const std::string supplied = value.signature;
  value.signature.clear();
  auto material = to_json(value);
  material.erase("signature");
  const std::string expected = contracts::sha256_json(material);
  if (!supplied.empty() && supplied != expected) {
    policy_error("diversity configuration signature mismatch");
  }
  value.signature = expected;
  return value;
}

ScoreConfiguration make_score_configuration(ScoreConfiguration value) {
  if (value.schema_version != 1) {
    policy_error("score configuration schema_version must be 1");
  }
  if (trim(value.config_id).empty()) {
    policy_error("score configuration ID must be non-empty");
  }
  const std::array<int, 6> weights{
      value.verifier_weight, value.evidence_weight, value.consistency_weight,
      value.falsifier_weight, value.goal_weight, value.diversity_weight};
  if (std::ranges::any_of(weights,
                          [](int weight) { return weight < 0 || weight > 100; })) {
    policy_error("score configuration weights must be 0..100");
  }
  if (std::accumulate(weights.begin(), weights.end(), 0) != 100) {
    policy_error("positive score configuration weights must sum to 100");
  }
  if (value.uncertainty_penalty < 0 || value.uncertainty_penalty > 100 ||
      value.compute_penalty < 0 || value.compute_penalty > 100) {
    policy_error("score configuration penalties must be 0..100");
  }
  validate_score(value.minimum_survivor_bp, "minimum survivor score");
  const std::string supplied = value.signature;
  value.signature.clear();
  auto material = to_json(value);
  material.erase("signature");
  const std::string expected = contracts::sha256_json(material);
  if (!supplied.empty() && supplied != expected) {
    policy_error("score configuration signature mismatch");
  }
  value.signature = expected;
  return value;
}

SynthesisResult make_synthesis_result(SynthesisResult value) {
  if (value.schema_version != 1) {
    policy_error("synthesis schema_version must be 1");
  }
  validate_score(value.confidence_bp, "synthesis confidence");
  value.source_path_ids = canonical_strings(value.source_path_ids);
  value.accepted_node_ids = canonical_strings(value.accepted_node_ids);
  value.rejected_node_ids = canonical_strings(value.rejected_node_ids);
  value.remaining_uncertainties =
      canonical_strings(value.remaining_uncertainties);
  value.failure_reasons = canonical_strings(value.failure_reasons);
  if (!value.merged_conclusion.empty() && value.winning_path_id.empty()) {
    policy_error("synthesis conclusion requires a winning path");
  }
  if (!value.winning_path_id.empty() &&
      std::ranges::find(value.source_path_ids, value.winning_path_id) ==
          value.source_path_ids.end()) {
    policy_error("synthesis winning path must be a source path");
  }
  if (value.verified && value.verifier_report_id.empty()) {
    policy_error("verified synthesis requires a verifier report");
  }
  if (value.verified && value.synthesized_path_id.empty()) {
    policy_error("verified synthesis requires a synthesized path ID");
  }
  const std::string supplied = value.signature;
  value.signature.clear();
  auto material = to_json(value);
  material.erase("signature");
  const std::string expected = contracts::sha256_json(material);
  if (!supplied.empty() && supplied != expected) {
    policy_error("synthesis signature mismatch");
  }
  value.signature = expected;
  return value;
}

ContradictionRecord make_contradiction_record(ContradictionRecord value) {
  if (!contains(contradiction_types, value.conflict_type)) {
    policy_error("invalid contradiction type: " + value.conflict_type);
  }
  if (!contains(contradiction_statuses, value.resolution_status)) {
    policy_error("invalid contradiction resolution status: " +
                 value.resolution_status);
  }
  if (value.left_claim_id.empty() || value.right_claim_id.empty()) {
    policy_error("contradiction claim IDs must be non-empty");
  }
  validate_score(value.severity_bp, "contradiction severity");
  value.evidence_left = canonical_strings(value.evidence_left);
  value.evidence_right = canonical_strings(value.evidence_right);
  value.resolution_evidence_ids =
      canonical_strings(value.resolution_evidence_ids);
  const std::string supplied_id = value.contradiction_id;
  const std::string supplied_signature = value.signature;
  value.contradiction_id.clear();
  value.signature.clear();
  auto material = to_json(value);
  material.erase("contradiction_id");
  material.erase("signature");
  auto identity_material = material;
  identity_material.erase("resolution_status");
  identity_material.erase("resolution_evidence_ids");
  const std::string expected_id =
      "contradiction:" + contracts::sha256_json(identity_material);
  if (!supplied_id.empty() && supplied_id != expected_id) {
    policy_error("contradiction ID mismatch");
  }
  material["contradiction_id"] = expected_id;
  const std::string expected_signature = contracts::sha256_json(material);
  if (!supplied_signature.empty() && supplied_signature != expected_signature) {
    policy_error("contradiction signature mismatch");
  }
  value.contradiction_id = expected_id;
  value.signature = expected_signature;
  return value;
}

const DiversityConfiguration &default_diversity_configuration() {
  static const DiversityConfiguration configuration =
      make_diversity_configuration();
  return configuration;
}

const ScoreConfiguration &default_score_configuration() {
  static const ScoreConfiguration configuration = make_score_configuration();
  return configuration;
}

ReasoningBudget derive_reasoning_budget(
    const DimensionBudget &dimension_budget, int uncertainty_bp,
    int difficulty_bp, int verifier_disagreement_bp,
    int configured_max_candidates, int configured_max_provider_calls,
    int minimum_voi_bp) {
  validate_score(uncertainty_bp, "reasoning uncertainty");
  validate_score(difficulty_bp, "reasoning difficulty");
  validate_score(verifier_disagreement_bp, "verifier disagreement");
  if (dimension_budget.max_active_relations < 1 ||
      dimension_budget.max_active_hypotheses < 1 ||
      dimension_budget.max_candidate_actions < 1 ||
      dimension_budget.max_decomposition_depth < 1 ||
      dimension_budget.max_branch_factor < 1) {
    policy_error("OIEC active-state limits must be positive");
  }
  const int maximum_candidates =
      std::min(std::max(1, configured_max_candidates),
               std::max(1, dimension_budget.max_candidate_actions));
  const int adaptive = 2 + uncertainty_bp / 2'500 + difficulty_bp / 2'500 +
                       verifier_disagreement_bp / 2'500;
  int candidate_count =
      std::min(maximum_candidates, std::max(1, adaptive));
  int verifier_count = candidate_count;
  int falsifier_count = std::min(2, verifier_count);
  const int max_provider_calls = std::max(1, configured_max_provider_calls);
  int max_generation_attempts =
      std::min(max_provider_calls, std::max(candidate_count, candidate_count * 2));
  int required_calls = max_generation_attempts + verifier_count +
                       falsifier_count + 2;
  if (required_calls > max_provider_calls) {
    while (candidate_count > 1) {
      --candidate_count;
      verifier_count = candidate_count;
      falsifier_count = std::min(2, verifier_count);
      max_generation_attempts = std::min(
          max_provider_calls, std::max(candidate_count, candidate_count * 2));
      required_calls = max_generation_attempts + verifier_count +
                       falsifier_count + 2;
      if (required_calls <= max_provider_calls) {
        break;
      }
    }
  }
  if (required_calls > max_provider_calls) {
    policy_error("provider-call budget cannot support one reasoning candidate");
  }
  ReasoningBudget budget;
  budget.max_hypotheses = dimension_budget.max_active_hypotheses;
  budget.minimum_candidates = 1;
  budget.maximum_candidates = maximum_candidates;
  budget.candidate_count = candidate_count;
  budget.max_steps_per_path = dimension_budget.max_decomposition_depth;
  budget.max_branch_factor = dimension_budget.max_branch_factor;
  budget.max_topology_nodes =
      std::max(16, dimension_budget.max_active_relations);
  budget.max_topology_edges =
      std::max(32, dimension_budget.max_active_relations * 2);
  budget.max_provider_calls = max_provider_calls;
  budget.max_generation_attempts = max_generation_attempts;
  budget.verifier_count = verifier_count;
  budget.falsifier_count = falsifier_count;
  budget.operation_costs_bp = {{"GENERATE_HYPOTHESIS", 1'500},
                               {"REFINE_DIMENSION", 2'000},
                               {"RETRIEVE_EVIDENCE", 1'000},
                               {"RUN_READ_ONLY_EXPERIMENT", 2'500},
                               {"SEARCH_COUNTEREXAMPLE", 1'500},
                               {"STOP", 0},
                               {"VERIFY_AGAIN", 1'000}};
  validate_score(minimum_voi_bp, "minimum value of information");
  budget.minimum_voi_bp = minimum_voi_bp;
  budget = canonicalize_reasoning_budget(std::move(budget));
  auto material = to_json(budget);
  material.erase("schema_version");
  material.erase("signature");
  budget.signature = contracts::sha256_json(material);
  return budget;
}

int expected_value_of_information_bp(int expected_quality_gain_bp, int cost_bp,
                                     int cost_weight) {
  validate_score(expected_quality_gain_bp, "expected quality gain");
  validate_score(cost_bp, "reasoning operation cost");
  if (cost_weight < 0 || cost_weight > 1'000) {
    policy_error("value-of-information cost weight must be 0..1000");
  }
  return expected_quality_gain_bp - (cost_bp * cost_weight / 100);
}

bool should_continue_reasoning(const ReasoningBudget &budget,
                               int expected_quality_gain_bp, int cost_bp,
                               int cost_weight) {
  const auto checked = canonicalize_reasoning_budget(budget);
  return expected_value_of_information_bp(expected_quality_gain_bp, cost_bp,
                                          cost_weight) >=
         checked.minimum_voi_bp;
}

TopologyBudget topology_budget(const ReasoningBudget &budget) {
  const auto checked = canonicalize_reasoning_budget(budget);
  return {.max_topology_nodes = static_cast<std::size_t>(checked.max_topology_nodes),
          .max_topology_edges = static_cast<std::size_t>(checked.max_topology_edges),
          .max_branch_factor = static_cast<std::size_t>(checked.max_branch_factor)};
}

int evidence_coverage_bp(
    const ReasoningPath &path_value,
    const std::vector<std::string> &declared_evidence_ids) {
  const auto path = canonicalize_reasoning_path(path_value);
  const std::set<std::string> declared(declared_evidence_ids.begin(),
                                       declared_evidence_ids.end());
  std::set<std::string> referenced;
  for (const auto &step : path.steps) {
    referenced.insert(step.evidence_ids.begin(), step.evidence_ids.end());
  }
  if (declared.empty()) {
    return referenced.empty() ? score_scale : 0;
  }
  std::vector<std::string> intersection;
  std::ranges::set_intersection(referenced, declared,
                                std::back_inserter(intersection));
  return std::min(
      score_scale,
      static_cast<int>(intersection.size() *
                       static_cast<std::size_t>(score_scale) / declared.size()));
}

int path_uncertainty_bp(const ReasoningPath &path_value,
                        int unresolved_assumption_count) {
  const auto path = canonicalize_reasoning_path(path_value);
  const auto weakest = std::ranges::min_element(
      path.steps, {}, &ReasoningStep::confidence_bp);
  const std::int64_t count = std::max(0, unresolved_assumption_count);
  const int assumption_penalty = static_cast<int>(std::min<std::int64_t>(
      score_scale, count * 500));
  return std::min(score_scale,
                  (score_scale - weakest->confidence_bp) + assumption_penalty);
}

ReasoningMetrics score_reasoning_path(
    const ReasoningPath &path_value, const VerifierReport &verifier_value,
    const FalsifierReport &falsifier_value,
    const std::vector<std::string> &declared_evidence_ids,
    const ScoreConfiguration &config_value) {
  const auto path = canonicalize_reasoning_path(path_value);
  const auto verifier = canonicalize_verifier_report(verifier_value);
  const auto falsifier = canonicalize_falsifier_report(falsifier_value);
  const auto config = make_score_configuration(config_value);
  if (verifier.path_id != path.path_id || falsifier.path_id != path.path_id) {
    policy_error("reasoning report path does not match scored path");
  }
  const int coverage = evidence_coverage_bp(path, declared_evidence_ids);
  const int consistency =
      std::min(verifier.consistency_bp,
               std::max(0, score_scale -
                               static_cast<int>(verifier.contradictions.size()) *
                                   2'000));
  int unresolved = static_cast<int>(verifier.missing_assumptions.size());
  for (const auto &step : path.steps) {
    unresolved += static_cast<int>(step.assumptions.size());
  }
  const int uncertainty = path_uncertainty_bp(path, unresolved);
  const std::int64_t positive =
      (static_cast<std::int64_t>(config.verifier_weight) * verifier.score_bp +
       static_cast<std::int64_t>(config.evidence_weight) * coverage +
       static_cast<std::int64_t>(config.consistency_weight) * consistency +
       static_cast<std::int64_t>(config.falsifier_weight) *
           falsifier.survival_bp +
       static_cast<std::int64_t>(config.goal_weight) * path.goal_relevance_bp +
       static_cast<std::int64_t>(config.diversity_weight) * path.diversity_bp) /
      100;
  const std::int64_t penalties =
      (static_cast<std::int64_t>(config.uncertainty_penalty) * uncertainty +
       static_cast<std::int64_t>(config.compute_penalty) *
           path.estimated_cost_bp) /
      100;
  ReasoningMetrics metrics;
  metrics.path_id = path.path_id;
  metrics.evidence_support_bp = coverage;
  metrics.verifier_bp = verifier.score_bp;
  metrics.consistency_bp = consistency;
  metrics.falsifier_bp = falsifier.survival_bp;
  metrics.goal_relevance_bp = path.goal_relevance_bp;
  metrics.diversity_bp = path.diversity_bp;
  metrics.uncertainty_bp = uncertainty;
  metrics.risk_bp = path.risk_bp;
  metrics.cost_bp = path.estimated_cost_bp;
  metrics.total_score_bp = static_cast<int>(std::clamp<std::int64_t>(
      positive - penalties, -score_scale, score_scale));
  metrics.score_config_id = config.config_id;
  metrics.score_config_hash = config.signature;
  auto material = to_json(metrics);
  material.erase("signature");
  metrics.signature = contracts::sha256_json(material);
  return canonicalize_reasoning_metrics(std::move(metrics));
}

std::vector<ReasoningPath> rank_reasoning_paths(
    const std::vector<ReasoningPath> &paths_value,
    const std::vector<ReasoningMetrics> &metrics_value,
    const std::vector<VerifierReport> &verifier_values,
    const std::vector<FalsifierReport> &falsifier_values) {
  std::vector<ReasoningPath> paths;
  paths.reserve(paths_value.size());
  for (const auto &path : paths_value) {
    paths.push_back(canonicalize_reasoning_path(path));
  }
  std::vector<ReasoningMetrics> metrics;
  for (const auto &value : metrics_value) {
    metrics.push_back(canonicalize_reasoning_metrics(value));
  }
  std::vector<VerifierReport> verifiers;
  for (const auto &value : verifier_values) {
    verifiers.push_back(canonicalize_verifier_report(value));
  }
  std::vector<FalsifierReport> falsifiers;
  for (const auto &value : falsifier_values) {
    falsifiers.push_back(canonicalize_falsifier_report(value));
  }
  const auto metrics_by_path =
      unique_by(metrics, &ReasoningMetrics::path_id, "reasoning metrics");
  const auto verifier_by_path =
      unique_by(verifiers, &VerifierReport::path_id, "verifier report");
  const auto falsifier_by_path =
      unique_by(falsifiers, &FalsifierReport::path_id, "falsifier report");
  for (const auto &path : paths) {
    if (!metrics_by_path.contains(path.path_id) ||
        !verifier_by_path.contains(path.path_id) ||
        !falsifier_by_path.contains(path.path_id)) {
      policy_error("reasoning path lacks complete ranking evidence");
    }
  }
  std::ranges::sort(paths, [&](const ReasoningPath &left,
                               const ReasoningPath &right) {
    const auto &left_metrics = metrics_by_path.at(left.path_id);
    const auto &right_metrics = metrics_by_path.at(right.path_id);
    if (left_metrics.total_score_bp != right_metrics.total_score_bp) {
      return left_metrics.total_score_bp > right_metrics.total_score_bp;
    }
    const auto &left_verifier = verifier_by_path.at(left.path_id);
    const auto &right_verifier = verifier_by_path.at(right.path_id);
    if (left_verifier.score_bp != right_verifier.score_bp) {
      return left_verifier.score_bp > right_verifier.score_bp;
    }
    const auto &left_falsifier = falsifier_by_path.at(left.path_id);
    const auto &right_falsifier = falsifier_by_path.at(right.path_id);
    if (left_falsifier.survival_bp != right_falsifier.survival_bp) {
      return left_falsifier.survival_bp > right_falsifier.survival_bp;
    }
    if (left.estimated_cost_bp != right.estimated_cost_bp) {
      return left.estimated_cost_bp < right.estimated_cost_bp;
    }
    return left.path_id < right.path_id;
  });
  return paths;
}

int conclusion_agreement_bp(const CandidateSet &candidate_value) {
  const auto candidates = canonicalize_candidate_set(candidate_value);
  if (candidates.paths.empty() || candidates.selected_path_id.empty()) {
    return 0;
  }
  const auto selected = std::ranges::find(
      candidates.paths, candidates.selected_path_id, &ReasoningPath::path_id);
  if (selected == candidates.paths.end()) {
    policy_error("selected reasoning path is absent from candidates");
  }
  const std::string selected_key = normalized_conclusion(selected->conclusion);
  const auto matching = std::ranges::count_if(
      candidates.paths, [&](const ReasoningPath &path) {
        return normalized_conclusion(path.conclusion) == selected_key;
      });
  return static_cast<int>(matching * score_scale /
                          static_cast<std::ptrdiff_t>(candidates.paths.size()));
}

int derive_reasoning_confidence_bp(const CandidateSet &candidate_value) {
  const auto candidates = canonicalize_candidate_set(candidate_value);
  if (candidates.selected_path_id.empty()) {
    return 0;
  }
  const auto metrics = std::ranges::find(
      candidates.metrics, candidates.selected_path_id,
      &ReasoningMetrics::path_id);
  if (metrics == candidates.metrics.end()) {
    policy_error("selected reasoning path lacks metrics");
  }
  const int agreement = conclusion_agreement_bp(candidates);
  const int value =
      (30 * metrics->verifier_bp + 20 * agreement +
       20 * metrics->evidence_support_bp + 20 * metrics->falsifier_bp +
       10 * (score_scale - metrics->uncertainty_bp)) /
      100;
  validate_score(value, "derived reasoning confidence");
  return value;
}

std::string normalize_claim(std::string_view value) {
  std::vector<std::string> tokens;
  std::string token;
  for (const char byte : value) {
    const auto character = static_cast<unsigned char>(byte);
    if (std::isalnum(character) != 0 && character < 128U) {
      token.push_back(static_cast<char>(std::tolower(character)));
    } else if (!token.empty()) {
      tokens.push_back(std::move(token));
      token.clear();
    }
  }
  if (!token.empty()) {
    tokens.push_back(std::move(token));
  }
  std::ranges::sort(tokens);
  std::string result;
  for (const auto &item : tokens) {
    if (!result.empty()) {
      result.push_back(' ');
    }
    result += item;
  }
  return result;
}

contracts::Json path_structure_material(const ReasoningPath &path_value) {
  const auto path = canonicalize_reasoning_path(path_value);
  std::vector<std::string> evidence_ids;
  std::vector<std::string> inference_modes;
  std::vector<std::string> assumptions;
  std::vector<std::string> falsifiers;
  for (const auto &step : path.steps) {
    evidence_ids.insert(evidence_ids.end(), step.evidence_ids.begin(),
                        step.evidence_ids.end());
    inference_modes.push_back(step.inference);
    for (const auto &assumption : step.assumptions) {
      const auto normalized = normalize_claim(assumption);
      if (!normalized.empty()) {
        assumptions.push_back(normalized);
      }
    }
    if (!step.falsifier.empty()) {
      const auto normalized = normalize_claim(step.falsifier);
      if (!normalized.empty()) {
        falsifiers.push_back(normalized);
      }
    }
  }
  assumptions = canonical_strings(assumptions);
  falsifiers = canonical_strings(falsifiers);
  return {{"strategy", path.perspective},
          {"hypothesis_ids", canonical_strings(path.hypothesis_ids)},
          {"evidence_ids", canonical_strings(evidence_ids)},
          {"inference_modes", inference_modes},
          {"assumptions", assumptions},
          {"falsifiers", falsifiers},
          {"conclusion", normalize_claim(path.conclusion)}};
}

std::string path_structure_signature(const ReasoningPath &path) {
  return contracts::sha256_json(path_structure_material(path));
}

int structural_similarity_bp(const ReasoningPath &left,
                             const ReasoningPath &right,
                             const DiversityConfiguration &config_value) {
  const auto config = make_diversity_configuration(config_value);
  const auto left_material = path_structure_material(left);
  const auto right_material = path_structure_material(right);
  const auto left_modes = json_strings(left_material, "inference_modes");
  const auto right_modes = json_strings(right_material, "inference_modes");
  const int inference_similarity =
      left_modes == right_modes ? score_scale : jaccard_bp(left_modes, right_modes);
  const auto words = [](const std::string &value) {
    std::vector<std::string> result;
    std::string token;
    for (const char byte : value) {
      const auto character = static_cast<unsigned char>(byte);
      if (std::isspace(character) != 0) {
        if (!token.empty()) {
          result.push_back(std::move(token));
          token.clear();
        }
      } else {
        token.push_back(static_cast<char>(character));
      }
    }
    if (!token.empty()) {
      result.push_back(std::move(token));
    }
    return result;
  };
  const std::int64_t value =
      (static_cast<std::int64_t>(config.hypothesis_weight) *
           jaccard_bp(json_strings(left_material, "hypothesis_ids"),
                      json_strings(right_material, "hypothesis_ids")) +
       static_cast<std::int64_t>(config.evidence_weight) *
           jaccard_bp(json_strings(left_material, "evidence_ids"),
                      json_strings(right_material, "evidence_ids")) +
       static_cast<std::int64_t>(config.inference_weight) *
           inference_similarity +
       static_cast<std::int64_t>(config.assumption_weight) *
           jaccard_bp(json_strings(left_material, "assumptions"),
                      json_strings(right_material, "assumptions")) +
       static_cast<std::int64_t>(config.falsifier_weight) *
           jaccard_bp(json_strings(left_material, "falsifiers"),
                      json_strings(right_material, "falsifiers")) +
       static_cast<std::int64_t>(config.conclusion_weight) *
           jaccard_bp(words(left_material.at("conclusion").get<std::string>()),
                      words(right_material.at("conclusion").get<std::string>()))) /
      100;
  return static_cast<int>(std::clamp<std::int64_t>(value, 0, score_scale));
}

bool is_structural_duplicate(const ReasoningPath &candidate,
                             const std::vector<ReasoningPath> &accepted,
                             const DiversityConfiguration &config_value) {
  const auto config = make_diversity_configuration(config_value);
  return std::ranges::any_of(accepted, [&](const ReasoningPath &other) {
    return structural_similarity_bp(candidate, other, config) >=
           config.duplicate_threshold_bp;
  });
}

std::vector<ReasoningPath> bind_diversity_scores(
    const std::vector<ReasoningPath> &path_values,
    const DiversityConfiguration &config_value) {
  const auto config = make_diversity_configuration(config_value);
  std::vector<ReasoningPath> paths;
  std::set<std::string> identities;
  for (const auto &value : path_values) {
    auto path = canonicalize_reasoning_path(value);
    if (!identities.insert(path.path_id).second) {
      policy_error("reasoning path identities must be unique");
    }
    paths.push_back(std::move(path));
  }
  if (paths.size() < 2) {
    for (auto &path : paths) {
      path.diversity_bp = 0;
    }
    return paths;
  }
  for (auto &path : paths) {
    int maximum_similarity = 0;
    for (const auto &other : paths) {
      if (other.path_id != path.path_id) {
        maximum_similarity = std::max(
            maximum_similarity,
            structural_similarity_bp(path, other, config));
      }
    }
    path.diversity_bp = score_scale - maximum_similarity;
  }
  return paths;
}

std::vector<ContradictionRecord>
build_contradiction_records(const CandidateSet &candidate_value) {
  const auto candidates = canonicalize_candidate_set(candidate_value);
  const auto paths = unique_by(candidates.paths, &ReasoningPath::path_id,
                               "reasoning path");
  std::vector<ContradictionRecord> records;
  for (const auto &report : candidates.verifier_reports) {
    if (!paths.contains(report.path_id)) {
      policy_error("verifier contradiction references an unknown path");
    }
    const auto evidence = path_evidence(paths.at(report.path_id));
    for (std::size_t index = 0; index < report.contradictions.size(); ++index) {
      ContradictionRecord record;
      record.left_claim_id = report.path_id + ":conclusion";
      record.right_claim_id = report.report_id + ":contradiction:" +
                              std::to_string(index);
      record.evidence_left = evidence;
      record.conflict_type = "logical";
      record.severity_bp = 8'000;
      records.push_back(make_contradiction_record(std::move(record)));
    }
  }
  for (const auto &report : candidates.falsifier_reports) {
    if (!paths.contains(report.path_id)) {
      policy_error("falsifier contradiction references an unknown path");
    }
    const auto evidence = path_evidence(paths.at(report.path_id));
    const auto append = [&](std::size_t count, std::string_view label,
                            std::string_view conflict_type, int floor) {
      for (std::size_t index = 0; index < count; ++index) {
        ContradictionRecord record;
        record.left_claim_id = report.path_id + ":conclusion";
        record.right_claim_id = report.report_id + ":" + std::string(label) +
                                ":" + std::to_string(index);
        record.evidence_left = evidence;
        record.conflict_type = conflict_type;
        record.severity_bp = std::max(floor, report.severity_bp);
        records.push_back(make_contradiction_record(std::move(record)));
      }
    };
    append(report.counterexamples.size(), "counterexample", "empirical", 6'000);
    append(report.alternative_explanations.size(), "alternative", "causal",
           5'000);
    append(report.reversed_causal_directions.size(), "causal-direction", "causal",
           7'000);
  }
  std::ranges::sort(records, {}, &ContradictionRecord::contradiction_id);
  return records;
}

ContradictionRecord resolve_contradiction(
    ContradictionRecord record,
    std::vector<std::string> resolution_evidence_ids, std::string status) {
  record.contradiction_id.clear();
  record.resolution_status = std::move(status);
  record.resolution_evidence_ids = std::move(resolution_evidence_ids);
  record.signature.clear();
  return make_contradiction_record(std::move(record));
}

std::vector<ContradictionRecord> unresolved_critical_contradictions(
    const std::vector<ContradictionRecord> &record_values) {
  std::vector<ContradictionRecord> result;
  for (const auto &value : record_values) {
    const auto record = make_contradiction_record(value);
    if (record.resolution_status == "UNRESOLVED" &&
        record.severity_bp >= critical_contradiction_bp) {
      result.push_back(record);
    }
  }
  return result;
}

int cap_confidence_for_contradictions(
    int confidence_bp, const std::vector<ContradictionRecord> &record_values) {
  if (!unresolved_critical_contradictions(record_values).empty()) {
    return std::min(confidence_bp, 4'999);
  }
  int unresolved = 0;
  for (const auto &value : record_values) {
    if (make_contradiction_record(value).resolution_status == "UNRESOLVED") {
      ++unresolved;
    }
  }
  return std::clamp(confidence_bp - unresolved * 500, 0, score_scale);
}

} // namespace statewright::reasoning
