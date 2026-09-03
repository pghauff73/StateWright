#pragma once

#include "statewright/providers/reasoning_provider.hpp"
#include "statewright/reasoning/certification.hpp"
#include "statewright/reasoning/search.hpp"

#include <optional>
#include <string>
#include <vector>

namespace statewright::reasoning {

struct SuperReasoningKernelOptions final {
  int max_candidates = 16;
  int max_provider_calls = 64;
  int minimum_voi_bp = 100;
  int acceptance_confidence_bp = 5'000;
  int acceptance_verifier_bp = 5'000;
  int acceptance_falsifier_bp = 5'000;
  std::optional<AblationConfiguration> ablation;
};

struct ReasoningRunResult final {
  std::vector<Hypothesis> hypotheses;
  ReasoningBudget budget;
  CandidateSet candidates;
  ReasoningTopology topology;
  ReasoningCertificate certificate;
};

class SuperReasoningKernel final {
public:
  explicit SuperReasoningKernel(SuperReasoningKernelOptions options = {});

  [[nodiscard]] static ReasoningProblem create_problem(
      std::string statement, std::string goal,
      std::string source_snapshot_hash, std::string boundary_signature,
      std::string dimension_signature,
      std::vector<std::string> evidence_ids = {}, int uncertainty_bp = 0,
      int difficulty_bp = 0,
      bool mutually_exclusive_hypotheses = false);
  [[nodiscard]] static HypothesisSet build_hypothesis_state(
      const std::vector<HypothesisProposal> &proposals,
      std::string problem_id, int max_hypotheses,
      bool mutually_exclusive = false);
  [[nodiscard]] static HypothesisSet build_hypothesis_state(
      const std::vector<Hypothesis> &hypotheses, std::string problem_id,
      int max_hypotheses, bool mutually_exclusive = false);
  [[nodiscard]] static std::vector<Hypothesis> build_hypotheses(
      const std::vector<HypothesisProposal> &proposals, int max_hypotheses,
      bool mutually_exclusive = false);

  [[nodiscard]] ReasoningBudget derive_budget(
      const DimensionBudget &dimension_budget, const ReasoningProblem &problem,
      int verifier_disagreement = 0,
      std::optional<int> provider_sample_cap = std::nullopt) const;
  [[nodiscard]] ReasoningRunResult run(
      providers::ReasoningProvider &provider, const ReasoningProblem &problem,
      const HypothesisSet &hypotheses,
      const DimensionBudget &dimension_budget,
      std::vector<std::string> declared_evidence_ids,
      const std::optional<ReasoningCertificate> &previous_certificate =
          std::nullopt,
      const std::optional<CandidateSet> &previous_candidates = std::nullopt,
      std::optional<int> provider_sample_cap = std::nullopt) const;
  [[nodiscard]] ReasoningRunResult run(
      providers::ReasoningProvider &provider, const ReasoningProblem &problem,
      const std::vector<Hypothesis> &hypotheses,
      const DimensionBudget &dimension_budget,
      std::vector<std::string> declared_evidence_ids,
      const std::optional<ReasoningCertificate> &previous_certificate =
          std::nullopt,
      const std::optional<CandidateSet> &previous_candidates = std::nullopt,
      std::optional<int> provider_sample_cap = std::nullopt) const;
  [[nodiscard]] ReasoningCertificate certify(
      const ReasoningProblem &problem,
      const std::vector<Hypothesis> &hypotheses,
      const ReasoningBudget &budget, const CandidateSet &candidates,
      const ReasoningTopology &topology,
      const std::optional<ReasoningCertificate> &previous_certificate =
          std::nullopt) const;

  [[nodiscard]] static HypothesisStateUpdate apply_falsifier_updates(
      const HypothesisSet &state, const CandidateSet &candidates);
  [[nodiscard]] static std::vector<Hypothesis> support_selected_hypotheses(
      const std::vector<Hypothesis> &hypotheses,
      const CandidateSet &candidates);

  [[nodiscard]] int max_candidates() const noexcept;
  [[nodiscard]] int max_provider_calls() const noexcept;
  [[nodiscard]] const AblationConfiguration &ablation() const noexcept;

private:
  int max_candidates_ = 16;
  int max_provider_calls_ = 64;
  int minimum_voi_bp_ = 100;
  int acceptance_confidence_bp_ = 5'000;
  int acceptance_verifier_bp_ = 5'000;
  int acceptance_falsifier_bp_ = 5'000;
  AblationConfiguration ablation_;
};

} // namespace statewright::reasoning
