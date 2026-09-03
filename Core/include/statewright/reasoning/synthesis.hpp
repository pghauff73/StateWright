#pragma once

#include "statewright/providers/reasoning_provider.hpp"
#include "statewright/reasoning/evaluation.hpp"

#include <optional>
#include <string>
#include <vector>

namespace statewright::reasoning {

struct ValidatedSynthesis final {
  ReasoningPath path;
  SynthesisResult result;
};

[[nodiscard]] contracts::Json synthesizer_request(
    const ReasoningProblem &problem, const ReasoningPath &winner,
    const std::vector<ReasoningPath> &survivors);

[[nodiscard]] bool compatible_synthesis_sources(
    const ReasoningPath &winner,
    const std::vector<ReasoningPath> &sources);
[[nodiscard]] ReasoningPath make_synthesis_path(
    const ReasoningProblem &problem, const ReasoningPath &winner,
    const std::vector<ReasoningPath> &sources, std::string conclusion,
    std::vector<std::string> accepted_step_ids = {});
[[nodiscard]] SynthesisResult fallback_to_verified_winner(
    const ReasoningPath &winner, const VerifierReport &verifier,
    std::vector<std::string> reasons = {});
[[nodiscard]] ValidatedSynthesis validate_synthesis_payload(
    const ReasoningProblem &problem, const ReasoningPath &winner,
    const std::vector<ReasoningPath> &survivors,
    const VerifierReport &winner_verifier, const contracts::Json &payload,
    bool verify_synthesis = true,
    const std::optional<VerifierReport> &synthesis_verifier = std::nullopt);
[[nodiscard]] std::pair<std::string, std::vector<std::string>>
validate_synthesized_conclusion(const ReasoningPath &winner,
                                const std::vector<ReasoningPath> &survivors,
                                const contracts::Json &payload);
[[nodiscard]] SynthesisResult synthesize_verified_result(
    providers::ReasoningProvider &provider, const ReasoningProblem &problem,
    const std::vector<Hypothesis> &hypotheses, const ReasoningPath &winner,
    const std::vector<ReasoningPath> &survivors,
    const std::vector<VerifierReport> &verifier_reports,
    const std::vector<std::string> &declared_evidence_ids,
    const ReasoningBudget &budget, bool verify_synthesis = true,
    int verifier_batch_size = 1);
[[nodiscard]] std::pair<std::string, std::vector<std::string>>
synthesize_conclusion(providers::ReasoningProvider &provider,
                      const ReasoningProblem &problem,
                      const ReasoningPath &winner,
                      const std::vector<ReasoningPath> &survivors,
                      const ReasoningBudget &budget);

} // namespace statewright::reasoning
