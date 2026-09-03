#pragma once

#include "statewright/providers/reasoning_provider.hpp"
#include "statewright/reasoning/evaluation.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::reasoning {

inline constexpr std::string_view reasoning_batch_tool_name =
    "submit_oiec_reasoning_batch";
inline constexpr std::string_view reasoning_object_tool_name =
    "submit_oiec_reasoning_object";
inline constexpr int proposer_batch_max_output_tokens = 2'048;

[[nodiscard]] const std::vector<std::string> &default_perspectives();
[[nodiscard]] contracts::Json perspective_contract(std::string perspective);
[[nodiscard]] std::vector<std::string> perspective_names(int count);
[[nodiscard]] contracts::Json
provider_problem_context(const ReasoningProblem &problem);
[[nodiscard]] contracts::Json
provider_hypothesis_context(const Hypothesis &hypothesis);
[[nodiscard]] contracts::Json reasoning_batch_tool(
    std::string collection_key);
[[nodiscard]] contracts::Json reasoning_object_tool(
    std::vector<std::string> property_keys,
    std::vector<std::string> required_keys = {});
[[nodiscard]] contracts::Json proposer_request(
    const ReasoningProblem &problem,
    const std::vector<Hypothesis> &hypotheses, std::string perspective,
    const ReasoningBudget &budget);
[[nodiscard]] contracts::Json proposer_batch_request(
    const ReasoningProblem &problem,
    const std::vector<Hypothesis> &hypotheses,
    const std::vector<std::string> &perspectives,
    const ReasoningBudget &budget);
[[nodiscard]] ReasoningPath parse_reasoning_path(
    const contracts::Json &payload, const ReasoningProblem &problem,
    const std::vector<Hypothesis> &hypotheses, std::string perspective,
    const ReasoningBudget &budget);
[[nodiscard]] std::vector<ReasoningPath> generate_reasoning_paths(
    providers::ReasoningProvider &provider, const ReasoningProblem &problem,
    const std::vector<Hypothesis> &hypotheses, const ReasoningBudget &budget,
    bool diversity_filter_enabled = true, int role_batch_size = 1);

} // namespace statewright::reasoning
