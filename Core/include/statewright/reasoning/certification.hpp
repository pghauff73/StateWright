#pragma once

#include "statewright/reasoning/ablation.hpp"
#include "statewright/reasoning/context.hpp"
#include "statewright/reasoning/synthesis.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace statewright::reasoning {

struct ReasoningCertificate final {
  int schema_version = 2;
  std::string problem_hash;
  std::string boundary_signature;
  std::string dimension_signature;
  std::string hypothesis_signature;
  std::string topology_signature;
  std::string candidate_set_signature;
  std::string synthesis_signature;
  std::string score_config_id;
  std::string score_config_hash;
  std::string ablation_id = "full_sr";
  std::string ablation_config_hash;
  std::vector<std::string> active_hypothesis_ids;
  int candidate_count = 0;
  int surviving_candidate_count = 0;
  std::string winning_candidate_id;
  std::string winning_path_id;
  std::vector<std::string> verifier_report_ids;
  std::vector<std::string> falsifier_report_ids;
  int evidence_coverage_bp = 0;
  int verifier_score_bp = 0;
  int falsification_score_bp = 0;
  int contradiction_count = 0;
  std::vector<std::string> unresolved_contradiction_ids;
  int uncertainty_before_bp = 0;
  int uncertainty_after_bp = 0;
  int disagreement_bp = 0;
  int residual_risk_bp = 0;
  int compute_spent_bp = 0;
  std::vector<std::string> unresolved_assumptions;
  std::string reasoning_topology_hash;
  int derived_confidence_bp = 0;
  std::string decision = "STOP_UNRESOLVED";
  std::string terminal_state = "INSUFFICIENT_EVIDENCE";
  std::vector<std::string> reasons;
  std::string signature;
};

struct CertificationPolicy final {
  int acceptance_confidence_bp = 5'000;
  int acceptance_verifier_bp = 5'000;
  int acceptance_falsifier_bp = 5'000;
  AblationConfiguration ablation;
};

[[nodiscard]] contracts::Json to_json(const ReasoningCertificate &value);

[[nodiscard]] ReasoningCertificate
canonicalize_reasoning_certificate(ReasoningCertificate value);

[[nodiscard]] ReasoningProblem create_reasoning_problem(
    std::string statement, std::string goal, std::string source_snapshot_hash,
    std::string boundary_signature, std::string dimension_signature,
    std::vector<std::string> evidence_ids = {}, int uncertainty_bp = 0,
    int difficulty_bp = 0, bool mutually_exclusive_hypotheses = false);

[[nodiscard]] CandidateSet sign_candidate_set(CandidateSet candidates);
[[nodiscard]] int verifier_disagreement_bp(const CandidateSet &candidates);
[[nodiscard]] std::string
hypothesis_collection_signature(const std::vector<Hypothesis> &hypotheses);

[[nodiscard]] ReasoningCertificate certify_reasoning(
    const ReasoningProblem &problem,
    const std::vector<Hypothesis> &hypotheses,
    const ReasoningBudget &budget, const CandidateSet &candidates,
    const ReasoningTopology &topology,
    const CertificationPolicy &policy = {},
    const std::optional<ReasoningCertificate> &previous_certificate =
        std::nullopt);

void require_problem_integrity(const ReasoningProblem &problem);
void require_candidate_integrity(const CandidateSet &candidates);
void require_reasoning_topology_integrity(const ReasoningTopology &topology);
void require_reasoning_certificate_integrity(
    const ReasoningCertificate &certificate);

} // namespace statewright::reasoning
