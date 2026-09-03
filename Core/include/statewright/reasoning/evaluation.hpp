#pragma once

#include "statewright/contracts/canonical_json.hpp"
#include "statewright/reasoning/hypotheses.hpp"
#include "statewright/reasoning/topology.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::reasoning {

using ScorePairs = std::vector<std::pair<std::string, int>>;

struct ReasoningProblem final {
  int schema_version = 1;
  std::string problem_id;
  std::string statement;
  std::string goal;
  std::string source_snapshot_hash;
  std::string boundary_signature;
  std::string dimension_signature;
  std::vector<std::string> evidence_ids;
  int uncertainty_bp = 0;
  int difficulty_bp = 0;
  bool mutually_exclusive_hypotheses = false;
  std::string signature;
};

struct ReasoningStep final {
  std::string step_id;
  std::string claim;
  std::vector<std::string> premises;
  std::vector<std::string> evidence_ids;
  std::string inference = "deductive";
  int confidence_bp = 0;
  std::vector<std::string> assumptions;
  std::string falsifier;
  std::string signature;
};

struct ReasoningPath final {
  std::string path_id;
  std::string perspective;
  std::vector<std::string> hypothesis_ids;
  std::vector<ReasoningStep> steps;
  std::string conclusion;
  int provider_confidence_bp = 0;
  int estimated_cost_bp = 0;
  int goal_relevance_bp = 0;
  int risk_bp = 0;
  std::vector<std::string> topology_node_ids;
  std::vector<std::string> topology_edge_ids;
  std::string structure_signature;
  int diversity_bp = 0;
  std::string signature;
};

struct VerifierReport final {
  std::string report_id;
  std::string path_id;
  ScorePairs step_scores;
  std::vector<std::string> failures;
  std::vector<std::string> contradictions;
  std::vector<std::string> unsupported_nodes;
  std::vector<std::string> missing_assumptions;
  int premise_validity_bp = 0;
  int evidence_support_bp = 0;
  int inference_quality_bp = 0;
  int consistency_bp = 0;
  int completeness_bp = 0;
  int weakest_step_bp = 0;
  int score_bp = 0;
  std::string verdict = "REJECT";
  std::string signature;
};

struct FalsifierReport final {
  std::string report_id;
  std::string path_id;
  std::vector<std::string> searched_falsifiers;
  std::vector<std::string> counterexamples;
  std::vector<std::string> contradicted_step_ids;
  std::vector<std::string> unresolved_defeat_conditions;
  std::vector<std::string> unresolved_defeat_evidence_ids;
  std::vector<std::string> alternative_explanations;
  std::vector<std::string> boundary_cases;
  std::vector<std::string> reversed_causal_directions;
  std::vector<std::string> invalid_invariants;
  std::vector<std::string> evidence_reversal_conditions;
  int severity_bp = 0;
  int survival_bp = 0;
  int residual_uncertainty_bp = 0;
  std::string verdict = "REJECT";
  std::string signature;
};

struct DiversityConfiguration final {
  int schema_version = 1;
  std::string config_id = "oiec-sr-diversity-v1";
  int hypothesis_weight = 25;
  int evidence_weight = 25;
  int inference_weight = 20;
  int assumption_weight = 10;
  int falsifier_weight = 10;
  int conclusion_weight = 10;
  int duplicate_threshold_bp = 9'001;
  std::string signature;
};

struct ScoreConfiguration final {
  int schema_version = 1;
  std::string config_id = "oiec-sr-score-v1";
  int verifier_weight = 30;
  int evidence_weight = 25;
  int consistency_weight = 15;
  int falsifier_weight = 15;
  int goal_weight = 10;
  int diversity_weight = 5;
  int uncertainty_penalty = 15;
  int compute_penalty = 5;
  int minimum_survivor_bp = 5'000;
  std::string signature;
};

struct SynthesisResult final {
  int schema_version = 1;
  std::string winning_path_id;
  std::string synthesized_path_id;
  std::vector<std::string> source_path_ids;
  std::vector<std::string> accepted_node_ids;
  std::vector<std::string> rejected_node_ids;
  std::string merged_conclusion;
  std::vector<std::string> remaining_uncertainties;
  int confidence_bp = 0;
  std::string verifier_report_id;
  std::string topology_signature;
  bool verified = false;
  bool fallback_used = false;
  std::vector<std::string> failure_reasons;
  std::string signature;
};

struct ContradictionRecord final {
  int schema_version = 1;
  std::string contradiction_id;
  std::string left_claim_id;
  std::string right_claim_id;
  std::vector<std::string> evidence_left;
  std::vector<std::string> evidence_right;
  std::string conflict_type = "logical";
  int severity_bp = 0;
  std::string resolution_status = "UNRESOLVED";
  std::vector<std::string> resolution_evidence_ids;
  std::string signature;
};

struct ReasoningMetrics final {
  std::string path_id;
  int evidence_support_bp = 0;
  int verifier_bp = 0;
  int consistency_bp = 0;
  int falsifier_bp = 0;
  int goal_relevance_bp = 0;
  int diversity_bp = 0;
  int uncertainty_bp = 0;
  int risk_bp = 0;
  int cost_bp = 0;
  int total_score_bp = 0;
  std::string score_config_id;
  std::string score_config_hash;
  std::string signature;
};

struct CandidateSet final {
  int schema_version = 1;
  std::string problem_id;
  std::vector<ReasoningPath> paths;
  std::vector<VerifierReport> verifier_reports;
  std::vector<FalsifierReport> falsifier_reports;
  std::vector<ReasoningMetrics> metrics;
  std::string selected_path_id;
  std::vector<std::string> surviving_path_ids;
  std::vector<std::string> rejected_path_ids;
  std::string synthesized_conclusion;
  std::vector<std::string> synthesis_source_path_ids;
  std::optional<SynthesisResult> synthesis;
  std::string score_config_id;
  std::string score_config_hash;
  std::string diversity_config_hash;
  std::string ablation_id = "full_sr";
  std::string ablation_config_hash;
  std::vector<std::string> contradiction_ids;
  std::vector<HypothesisUpdateRecord> hypothesis_updates;
  std::string signature;
};

struct DimensionBudget final {
  int max_active_relations = 256;
  int max_active_hypotheses = 16;
  int max_candidate_actions = 16;
  int max_decomposition_depth = 8;
  int max_branch_factor = 16;
};

struct ReasoningBudget final {
  int schema_version = 1;
  int max_hypotheses = 16;
  int minimum_candidates = 1;
  int maximum_candidates = 16;
  int candidate_count = 4;
  int max_steps_per_path = 8;
  int max_branch_factor = 16;
  int max_topology_nodes = 256;
  int max_topology_edges = 512;
  int max_provider_calls = 16;
  int max_generation_attempts = 8;
  int verifier_count = 4;
  int falsifier_count = 2;
  int max_verifier_passes = 2;
  int max_falsifier_passes = 2;
  int max_tokens = 12'000;
  int max_tool_calls = 20;
  int max_context_items = 128;
  ScorePairs operation_costs_bp;
  int max_compute_bp = score_scale;
  int minimum_voi_bp = 100;
  std::string signature;
};

[[nodiscard]] contracts::Json to_json(const ReasoningProblem &value);
[[nodiscard]] contracts::Json to_json(const ReasoningStep &value);
[[nodiscard]] contracts::Json to_json(const ReasoningPath &value);
[[nodiscard]] contracts::Json to_json(const VerifierReport &value);
[[nodiscard]] contracts::Json to_json(const FalsifierReport &value);
[[nodiscard]] contracts::Json to_json(const DiversityConfiguration &value);
[[nodiscard]] contracts::Json to_json(const ScoreConfiguration &value);
[[nodiscard]] contracts::Json to_json(const SynthesisResult &value);
[[nodiscard]] contracts::Json to_json(const ContradictionRecord &value);
[[nodiscard]] contracts::Json to_json(const ReasoningMetrics &value);
[[nodiscard]] contracts::Json to_json(const CandidateSet &value);
[[nodiscard]] contracts::Json to_json(const ReasoningBudget &value);

[[nodiscard]] ReasoningProblem
canonicalize_reasoning_problem(ReasoningProblem value);
[[nodiscard]] ReasoningStep canonicalize_reasoning_step(ReasoningStep value);
[[nodiscard]] ReasoningPath canonicalize_reasoning_path(ReasoningPath value);
[[nodiscard]] VerifierReport canonicalize_verifier_report(VerifierReport value);
[[nodiscard]] FalsifierReport
canonicalize_falsifier_report(FalsifierReport value);
[[nodiscard]] ReasoningMetrics
canonicalize_reasoning_metrics(ReasoningMetrics value);
[[nodiscard]] CandidateSet canonicalize_candidate_set(CandidateSet value);
[[nodiscard]] ReasoningBudget canonicalize_reasoning_budget(ReasoningBudget value);

[[nodiscard]] DiversityConfiguration make_diversity_configuration(
    DiversityConfiguration value = {});
[[nodiscard]] ScoreConfiguration
make_score_configuration(ScoreConfiguration value = {});
[[nodiscard]] SynthesisResult
make_synthesis_result(SynthesisResult value = {});
[[nodiscard]] ContradictionRecord
make_contradiction_record(ContradictionRecord value);

[[nodiscard]] const DiversityConfiguration &default_diversity_configuration();
[[nodiscard]] const ScoreConfiguration &default_score_configuration();

[[nodiscard]] ReasoningBudget derive_reasoning_budget(
    const DimensionBudget &dimension_budget, int uncertainty_bp,
    int difficulty_bp, int verifier_disagreement_bp = 0,
    int configured_max_candidates = 16,
    int configured_max_provider_calls = 64, int minimum_voi_bp = 100);

[[nodiscard]] int expected_value_of_information_bp(
    int expected_quality_gain_bp, int cost_bp, int cost_weight = 100);
[[nodiscard]] bool should_continue_reasoning(
    const ReasoningBudget &budget, int expected_quality_gain_bp, int cost_bp,
    int cost_weight = 100);
[[nodiscard]] TopologyBudget topology_budget(const ReasoningBudget &budget);

[[nodiscard]] int evidence_coverage_bp(
    const ReasoningPath &path,
    const std::vector<std::string> &declared_evidence_ids);
[[nodiscard]] int path_uncertainty_bp(
    const ReasoningPath &path, int unresolved_assumption_count = 0);
[[nodiscard]] ReasoningMetrics score_reasoning_path(
    const ReasoningPath &path, const VerifierReport &verifier,
    const FalsifierReport &falsifier,
    const std::vector<std::string> &declared_evidence_ids,
    const ScoreConfiguration &config = default_score_configuration());
[[nodiscard]] std::vector<ReasoningPath> rank_reasoning_paths(
    const std::vector<ReasoningPath> &paths,
    const std::vector<ReasoningMetrics> &metrics,
    const std::vector<VerifierReport> &verifier_reports,
    const std::vector<FalsifierReport> &falsifier_reports);
[[nodiscard]] int conclusion_agreement_bp(const CandidateSet &candidates);
[[nodiscard]] int derive_reasoning_confidence_bp(const CandidateSet &candidates);

[[nodiscard]] std::string normalize_claim(std::string_view value);
[[nodiscard]] contracts::Json
path_structure_material(const ReasoningPath &path);
[[nodiscard]] std::string path_structure_signature(const ReasoningPath &path);
[[nodiscard]] int structural_similarity_bp(
    const ReasoningPath &left, const ReasoningPath &right,
    const DiversityConfiguration &config = default_diversity_configuration());
[[nodiscard]] bool is_structural_duplicate(
    const ReasoningPath &candidate,
    const std::vector<ReasoningPath> &accepted,
    const DiversityConfiguration &config = default_diversity_configuration());
[[nodiscard]] std::vector<ReasoningPath> bind_diversity_scores(
    const std::vector<ReasoningPath> &paths,
    const DiversityConfiguration &config = default_diversity_configuration());

[[nodiscard]] std::vector<ContradictionRecord>
build_contradiction_records(const CandidateSet &candidates);
[[nodiscard]] ContradictionRecord resolve_contradiction(
    ContradictionRecord record,
    std::vector<std::string> resolution_evidence_ids,
    std::string status = "RESOLVED");
[[nodiscard]] std::vector<ContradictionRecord>
unresolved_critical_contradictions(
    const std::vector<ContradictionRecord> &records);
[[nodiscard]] int cap_confidence_for_contradictions(
    int confidence_bp, const std::vector<ContradictionRecord> &records);

} // namespace statewright::reasoning
