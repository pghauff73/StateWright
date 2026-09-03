#pragma once

#include "statewright/providers/reasoning_provider.hpp"
#include "statewright/reasoning/evaluation.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace statewright::reasoning {

inline constexpr int verifier_batch_max_output_tokens = 1'024;
inline constexpr int falsifier_batch_max_output_tokens = 1'024;

using ProcessChecks = std::map<std::string, bool>;

[[nodiscard]] const std::vector<std::string> &required_process_checks();
[[nodiscard]] ProcessChecks
normalize_process_checks(const contracts::Json &value);

[[nodiscard]] contracts::Json verifier_request(
    const ReasoningProblem &problem, const ReasoningPath &path,
    const std::vector<Hypothesis> &hypotheses);
[[nodiscard]] contracts::Json verifier_repair_request(
    const ReasoningProblem &problem, const ReasoningPath &path,
    const std::vector<Hypothesis> &hypotheses, std::string invalid_response,
    std::string validation_error);
[[nodiscard]] contracts::Json verifier_batch_request(
    const ReasoningProblem &problem, const std::vector<ReasoningPath> &paths,
    const std::vector<Hypothesis> &hypotheses);
[[nodiscard]] contracts::Json
expand_compact_verifier_payload(const contracts::Json &payload);

[[nodiscard]] VerifierReport verify_reasoning_path(
    const ReasoningPath &path, const std::vector<Hypothesis> &hypotheses,
    const std::vector<std::string> &declared_evidence_ids,
    const contracts::Json &payload);
[[nodiscard]] std::vector<VerifierReport> verify_reasoning_paths(
    providers::ReasoningProvider &provider, const ReasoningProblem &problem,
    const std::vector<ReasoningPath> &paths,
    const std::vector<Hypothesis> &hypotheses,
    const std::vector<std::string> &declared_evidence_ids,
    const ReasoningBudget &budget, int role_batch_size = 1);

[[nodiscard]] contracts::Json falsifier_request(
    const ReasoningProblem &problem, const ReasoningPath &path);
[[nodiscard]] contracts::Json falsifier_batch_request(
    const ReasoningProblem &problem, const std::vector<ReasoningPath> &paths);

[[nodiscard]] FalsifierReport falsify_reasoning_path(
    const ReasoningPath &path, const contracts::Json &payload,
    const std::optional<std::vector<std::string>> &declared_evidence_ids =
        std::nullopt);
[[nodiscard]] std::vector<FalsifierReport> falsify_reasoning_paths(
    providers::ReasoningProvider &provider, const ReasoningProblem &problem,
    const std::vector<ReasoningPath> &paths, const ReasoningBudget &budget,
    int role_batch_size = 1);

} // namespace statewright::reasoning
