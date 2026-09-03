#pragma once

#include "statewright/reasoning/evaluation.hpp"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace statewright::reasoning {

using CandidateSummaries =
    std::vector<std::pair<std::string, std::string>>;

struct ReasoningContext final {
  int schema_version = 1;
  std::string problem_hash;
  std::vector<std::string> constraints;
  std::vector<std::string> hypothesis_ids;
  std::vector<std::string> evidence_ids;
  std::vector<std::string> topology_node_ids;
  std::vector<std::string> collision_ids;
  std::vector<std::string> unresolved_questions;
  CandidateSummaries candidate_summaries;
  int max_items = 128;
  int max_chars_per_item = 512;
  int max_total_chars = 16'384;
  std::string signature;
};

struct ReasoningOperationChoice final {
  int schema_version = 1;
  std::string operation = "STOP";
  int expected_quality_gain_bp = 0;
  int cost_bp = 0;
  int value_bp = 0;
  bool requires_iurm = false;
  bool read_only = true;
  std::string signature;
};

[[nodiscard]] contracts::Json to_json(const ReasoningContext &value);
[[nodiscard]] contracts::Json to_json(const ReasoningOperationChoice &value);
[[nodiscard]] ReasoningContext make_reasoning_context(ReasoningContext value);
[[nodiscard]] ReasoningOperationChoice
make_reasoning_operation_choice(ReasoningOperationChoice value = {});
void require_reasoning_context_integrity(const ReasoningContext &value);
void require_reasoning_operation_integrity(
    const ReasoningOperationChoice &value);

[[nodiscard]] ReasoningContext project_reasoning_context(
    const ReasoningProblem &problem,
    const std::vector<Hypothesis> &hypotheses, const ReasoningBudget &budget,
    const std::optional<ReasoningTopology> &topology = std::nullopt,
    const std::optional<CandidateSet> &candidates = std::nullopt,
    std::vector<std::string> collision_ids = {},
    const std::optional<std::vector<std::string>> &top_evidence_ids =
        std::nullopt);

[[nodiscard]] ReasoningOperationChoice choose_reasoning_operation(
    const ReasoningBudget &budget,
    const std::map<std::string, int> &expected_gains_bp,
    std::vector<std::string> allowed_operations = {});

} // namespace statewright::reasoning
