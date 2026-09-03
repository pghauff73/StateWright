#pragma once

#include "statewright/providers/reasoning_provider.hpp"
#include "statewright/reasoning/ablation.hpp"
#include "statewright/reasoning/evaluation.hpp"

#include <optional>
#include <string>
#include <vector>

namespace statewright::reasoning {

[[nodiscard]] FalsifierReport
make_rejected_falsifier(const ReasoningPath &path);
[[nodiscard]] VerifierReport make_bypass_verifier(
    const ReasoningPath &path,
    const std::vector<std::string> &declared_evidence_ids);
[[nodiscard]] FalsifierReport
make_bypass_falsifier(const ReasoningPath &path);

[[nodiscard]] CandidateSet assemble_reasoning_candidates(
    const ReasoningProblem &problem,
    const std::vector<ReasoningPath> &paths,
    const std::vector<VerifierReport> &verifier_reports,
    const std::vector<FalsifierReport> &actual_falsifier_reports,
    const std::vector<std::string> &declared_evidence_ids,
    const AblationConfiguration &ablation = make_ablation_configuration(),
    const std::optional<SynthesisResult> &synthesis = std::nullopt);

[[nodiscard]] CandidateSet search_reasoning_candidates(
    providers::ReasoningProvider &provider, const ReasoningProblem &problem,
    const std::vector<Hypothesis> &hypotheses,
    const std::vector<std::string> &declared_evidence_ids,
    const ReasoningBudget &budget,
    const AblationConfiguration &ablation = make_ablation_configuration());

} // namespace statewright::reasoning
