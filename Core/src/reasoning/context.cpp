#include "statewright/reasoning/context.hpp"

#include "statewright/common/error.hpp"
#include "statewright/common/utf8.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::reasoning {
namespace {

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

[[nodiscard]] std::size_t utf8_character_count(std::string_view value) {
  common::require_valid_utf8(value);
  return static_cast<std::size_t>(std::ranges::count_if(
      value, [](unsigned char byte) { return (byte & 0xC0U) != 0x80U; }));
}

[[nodiscard]] std::string utf8_prefix(std::string_view value,
                                      std::size_t character_limit) {
  if (character_limit == 0) {
    return {};
  }
  std::size_t characters = 0;
  std::size_t bytes = 0;
  while (bytes < value.size() && characters < character_limit) {
    const unsigned char byte = static_cast<unsigned char>(value[bytes]);
    std::size_t width = 1;
    if ((byte & 0xE0U) == 0xC0U) {
      width = 2;
    } else if ((byte & 0xF0U) == 0xE0U) {
      width = 3;
    } else if ((byte & 0xF8U) == 0xF0U) {
      width = 4;
    }
    bytes += width;
    ++characters;
  }
  return std::string(value.substr(0, bytes));
}

[[nodiscard]] std::string collapse_space(std::string value) {
  std::string result;
  bool separated = false;
  for (const char byte : value) {
    if (std::isspace(static_cast<unsigned char>(byte)) != 0) {
      separated = !result.empty();
      continue;
    }
    if (separated) {
      result.push_back(' ');
      separated = false;
    }
    result.push_back(byte);
  }
  return result;
}

[[nodiscard]] std::string clip(std::string value, int limit) {
  value = collapse_space(std::move(value));
  const std::size_t bound = static_cast<std::size_t>(std::max(0, limit));
  if (utf8_character_count(value) <= bound) {
    return value;
  }
  return utf8_prefix(value, bound > 0 ? bound - 1 : 0) + "…";
}

void validate_score(int value, std::string_view label) {
  if (value < 0 || value > score_scale) {
    policy_error(std::string(label) + " must be 0..10000");
  }
}

[[nodiscard]] std::size_t total_context_characters(
    const ReasoningContext &value) {
  std::size_t total = 0;
  const std::array<const std::vector<std::string> *, 6> lists{
      &value.constraints,        &value.hypothesis_ids,
      &value.evidence_ids,       &value.topology_node_ids,
      &value.collision_ids,      &value.unresolved_questions};
  for (const auto *values : lists) {
    for (const auto &item : *values) {
      total += utf8_character_count(item);
    }
  }
  for (const auto &[path_id, summary] : value.candidate_summaries) {
    total += utf8_character_count(path_id);
    total += utf8_character_count(summary);
  }
  return total;
}

} // namespace

contracts::Json to_json(const ReasoningContext &value) {
  return {{"schema_version", value.schema_version},
          {"problem_hash", value.problem_hash},
          {"constraints", value.constraints},
          {"hypothesis_ids", value.hypothesis_ids},
          {"evidence_ids", value.evidence_ids},
          {"topology_node_ids", value.topology_node_ids},
          {"collision_ids", value.collision_ids},
          {"unresolved_questions", value.unresolved_questions},
          {"candidate_summaries", value.candidate_summaries},
          {"max_items", value.max_items},
          {"max_chars_per_item", value.max_chars_per_item},
          {"max_total_chars", value.max_total_chars},
          {"signature", value.signature}};
}

contracts::Json to_json(const ReasoningOperationChoice &value) {
  return {{"schema_version", value.schema_version},
          {"operation", value.operation},
          {"expected_quality_gain_bp", value.expected_quality_gain_bp},
          {"cost_bp", value.cost_bp},
          {"value_bp", value.value_bp},
          {"requires_iurm", value.requires_iurm},
          {"read_only", value.read_only},
          {"signature", value.signature}};
}

ReasoningContext make_reasoning_context(ReasoningContext value) {
  if (value.max_items < 1) {
    policy_error("reasoning context max_items must be positive");
  }
  if (value.max_chars_per_item < 1 || value.max_total_chars < 1) {
    policy_error("reasoning context character bounds must be positive");
  }
  const std::array<std::pair<std::string_view, std::vector<std::string> *>, 6>
      lists{{{"constraints", &value.constraints},
             {"hypothesis_ids", &value.hypothesis_ids},
             {"evidence_ids", &value.evidence_ids},
             {"topology_node_ids", &value.topology_node_ids},
             {"collision_ids", &value.collision_ids},
             {"unresolved_questions", &value.unresolved_questions}}};
  for (const auto &[name, values] : lists) {
    *values = canonical_strings(*values);
    if (values->size() > static_cast<std::size_t>(value.max_items)) {
      policy_error("reasoning context " + std::string(name) +
                   " exceeds the item bound");
    }
    if (std::ranges::any_of(*values, [&value](const std::string &item) {
          return utf8_character_count(item) >
                 static_cast<std::size_t>(value.max_chars_per_item);
        })) {
      policy_error("reasoning context " + std::string(name) +
                   " exceeds the character bound");
    }
  }
  std::set<std::pair<std::string, std::string>> summaries;
  for (const auto &[path_id, summary] : value.candidate_summaries) {
    if (!path_id.empty() && !summary.empty()) {
      summaries.emplace(path_id, summary);
    }
  }
  value.candidate_summaries = {summaries.begin(), summaries.end()};
  if (value.candidate_summaries.size() >
      static_cast<std::size_t>(value.max_items)) {
    policy_error("reasoning context candidate summaries exceeds the item bound");
  }
  if (std::ranges::any_of(
          value.candidate_summaries,
          [&value](const auto &summary) {
            return utf8_character_count(summary.first) >
                       static_cast<std::size_t>(value.max_chars_per_item) ||
                   utf8_character_count(summary.second) >
                       static_cast<std::size_t>(value.max_chars_per_item);
          })) {
    policy_error("reasoning context candidate summary exceeds the character bound");
  }
  if (total_context_characters(value) >
      static_cast<std::size_t>(value.max_total_chars)) {
    policy_error("reasoning context exceeds the total character bound");
  }

  const std::string supplied = value.signature;
  value.signature.clear();
  auto material = to_json(value);
  material.erase("signature");
  const std::string expected = contracts::sha256_json(material);
  if (!supplied.empty() && supplied != expected) {
    policy_error("reasoning context signature mismatch");
  }
  value.signature = expected;
  return value;
}

ReasoningOperationChoice
make_reasoning_operation_choice(ReasoningOperationChoice value) {
  if (!contains(reasoning_operations, value.operation)) {
    policy_error("invalid reasoning operation: " + value.operation);
  }
  validate_score(value.expected_quality_gain_bp, "operation expected gain");
  validate_score(value.cost_bp, "operation cost");
  if (value.value_bp < -score_scale || value.value_bp > score_scale) {
    policy_error("operation value must be -10000..10000");
  }
  if (value.operation == "RUN_READ_ONLY_EXPERIMENT" && !value.read_only) {
    policy_error("SR experiments must remain read-only");
  }
  if (value.operation == "REFINE_DIMENSION" && !value.requires_iurm) {
    policy_error("dimension refinement must require IURM");
  }
  const std::string supplied = value.signature;
  value.signature.clear();
  auto material = to_json(value);
  material.erase("signature");
  const std::string expected = contracts::sha256_json(material);
  if (!supplied.empty() && supplied != expected) {
    policy_error("reasoning operation signature mismatch");
  }
  value.signature = expected;
  return value;
}

void require_reasoning_context_integrity(const ReasoningContext &value) {
  ReasoningContext rebuilt = value;
  rebuilt.signature.clear();
  if (to_json(make_reasoning_context(std::move(rebuilt))) != to_json(value)) {
    policy_error("reasoning context integrity check failed");
  }
}

void require_reasoning_operation_integrity(
    const ReasoningOperationChoice &value) {
  ReasoningOperationChoice rebuilt = value;
  rebuilt.signature.clear();
  if (to_json(make_reasoning_operation_choice(std::move(rebuilt))) !=
      to_json(value)) {
    policy_error("reasoning operation integrity check failed");
  }
}

ReasoningContext project_reasoning_context(
    const ReasoningProblem &problem_value,
    const std::vector<Hypothesis> &hypothesis_values,
    const ReasoningBudget &budget_value,
    const std::optional<ReasoningTopology> &topology,
    const std::optional<CandidateSet> &candidate_value,
    std::vector<std::string> collision_ids,
    const std::optional<std::vector<std::string>> &top_evidence_ids) {
  const auto problem = canonicalize_reasoning_problem(problem_value);
  const auto budget = canonicalize_reasoning_budget(budget_value);
  const int max_items = budget.max_context_items;
  constexpr int max_chars = 512;
  const std::vector<std::string> evidence =
      top_evidence_ids.has_value() ? *top_evidence_ids : problem.evidence_ids;

  std::set<std::string> unresolved;
  for (const auto &hypothesis : hypothesis_values) {
    if (hypothesis.status == "FALSIFIED") {
      continue;
    }
    for (const auto &assumption : hypothesis.assumptions) {
      if (!collapse_space(assumption).empty()) {
        unresolved.insert(clip(assumption, max_chars));
      }
    }
  }

  CandidateSummaries summaries;
  if (candidate_value.has_value()) {
    const auto candidates = canonicalize_candidate_set(*candidate_value);
    for (const auto &report : candidates.verifier_reports) {
      for (const auto &assumption : report.missing_assumptions) {
        unresolved.insert(clip(assumption, max_chars));
      }
    }
    if (candidates.synthesis.has_value()) {
      for (const auto &uncertainty :
           candidates.synthesis->remaining_uncertainties) {
        unresolved.insert(clip(uncertainty, max_chars));
      }
    }

    std::vector<ReasoningMetrics> ranked_metrics = candidates.metrics;
    std::ranges::sort(ranked_metrics,
                      [](const ReasoningMetrics &left,
                         const ReasoningMetrics &right) {
                        if (left.total_score_bp != right.total_score_bp) {
                          return left.total_score_bp > right.total_score_bp;
                        }
                        return left.path_id < right.path_id;
                      });
    std::map<std::string, std::size_t> rank;
    for (std::size_t index = 0; index < ranked_metrics.size(); ++index) {
      rank[ranked_metrics[index].path_id] = index;
    }
    std::vector<ReasoningPath> paths = candidates.paths;
    std::ranges::sort(paths, [&rank](const ReasoningPath &left,
                                    const ReasoningPath &right) {
      const auto left_rank = rank.contains(left.path_id)
                                 ? rank.at(left.path_id)
                                 : rank.size();
      const auto right_rank = rank.contains(right.path_id)
                                  ? rank.at(right.path_id)
                                  : rank.size();
      return std::pair{left_rank, left.path_id} <
             std::pair{right_rank, right.path_id};
    });
    if (paths.size() > static_cast<std::size_t>(max_items)) {
      paths.resize(static_cast<std::size_t>(max_items));
    }
    for (const auto &path : paths) {
      const auto found = rank.find(path.path_id);
      const std::size_t path_rank =
          (found == rank.end() ? rank.size() : found->second) + 1;
      summaries.emplace_back(
          path.path_id,
          clip("rank=" + std::to_string(path_rank) +
                   "; strategy=" + path.perspective +
                   "; conclusion=" + path.conclusion,
               max_chars));
    }
  }

  ReasoningContext context;
  context.problem_hash = problem.signature;
  context.constraints = {
      clip("goal:" + problem.goal, max_chars),
      clip("boundary:" + problem.boundary_signature, max_chars),
      clip("dimension:" + problem.dimension_signature, max_chars)};
  for (const auto &hypothesis : hypothesis_values) {
    if (hypothesis.status != "FALSIFIED" &&
        context.hypothesis_ids.size() <
            static_cast<std::size_t>(max_items)) {
      context.hypothesis_ids.push_back(hypothesis.hypothesis_id);
    }
  }
  for (const auto &evidence_id : evidence) {
    if (!evidence_id.empty() && context.evidence_ids.size() <
                                    static_cast<std::size_t>(max_items)) {
      context.evidence_ids.push_back(evidence_id);
    }
  }
  if (topology.has_value()) {
    for (const auto &node : topology->nodes) {
      if (context.topology_node_ids.size() >=
          static_cast<std::size_t>(max_items)) {
        break;
      }
      context.topology_node_ids.push_back(node.node_id);
    }
  }
  for (const auto &collision_id : collision_ids) {
    if (!collision_id.empty() && context.collision_ids.size() <
                                     static_cast<std::size_t>(max_items)) {
      context.collision_ids.push_back(collision_id);
    }
  }
  for (const auto &question : unresolved) {
    if (context.unresolved_questions.size() >=
        static_cast<std::size_t>(max_items)) {
      break;
    }
    context.unresolved_questions.push_back(question);
  }
  context.candidate_summaries = std::move(summaries);
  context.max_items = max_items;
  context.max_chars_per_item = max_chars;
  context.max_total_chars =
      std::max(4'096, std::min(65'536, max_items * max_chars));
  return make_reasoning_context(std::move(context));
}

ReasoningOperationChoice choose_reasoning_operation(
    const ReasoningBudget &budget_value,
    const std::map<std::string, int> &expected_gains_bp,
    std::vector<std::string> allowed_operations) {
  const auto budget = canonicalize_reasoning_budget(budget_value);
  if (allowed_operations.empty()) {
    allowed_operations.assign(reasoning_operations.begin(),
                              reasoning_operations.end());
  }
  const auto allowed = canonical_strings(allowed_operations);
  for (const auto &operation : allowed) {
    if (!contains(reasoning_operations, operation)) {
      policy_error("unknown reasoning operation: " + operation);
    }
  }
  const std::map<std::string, int> costs(budget.operation_costs_bp.begin(),
                                         budget.operation_costs_bp.end());
  struct Choice final {
    int value;
    std::string operation;
    int gain;
    int cost;
  };
  std::vector<Choice> viable;
  for (const auto &operation : allowed) {
    if (operation == "STOP") {
      continue;
    }
    const auto gain_value = expected_gains_bp.find(operation);
    const int gain =
        gain_value == expected_gains_bp.end() ? 0 : gain_value->second;
    validate_score(gain, operation + " expected gain");
    const auto cost_value = costs.find(operation);
    const int cost =
        cost_value == costs.end() ? score_scale : cost_value->second;
    const int value = std::clamp(gain - cost, -score_scale, score_scale);
    if (value >= budget.minimum_voi_bp) {
      viable.push_back({value, operation, gain, cost});
    }
  }
  if (viable.empty()) {
    return make_reasoning_operation_choice();
  }
  std::ranges::sort(viable, [](const Choice &left, const Choice &right) {
    if (left.value != right.value) {
      return left.value > right.value;
    }
    return left.operation < right.operation;
  });
  ReasoningOperationChoice result;
  result.operation = viable.front().operation;
  result.expected_quality_gain_bp = viable.front().gain;
  result.cost_bp = viable.front().cost;
  result.value_bp = viable.front().value;
  result.requires_iurm = result.operation == "REFINE_DIMENSION";
  result.read_only = true;
  return make_reasoning_operation_choice(std::move(result));
}

} // namespace statewright::reasoning
