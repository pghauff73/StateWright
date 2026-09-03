#include "statewright/reasoning/synthesis.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::reasoning {
namespace {

[[noreturn]] void policy_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied, std::move(message));
}

[[nodiscard]] std::string trim(std::string value) {
  const auto first = std::ranges::find_if_not(value, [](unsigned char byte) {
    return std::isspace(byte) != 0;
  });
  const auto last = std::find_if_not(value.rbegin(), value.rend(),
                                     [](unsigned char byte) {
                                       return std::isspace(byte) != 0;
                                     })
                        .base();
  if (first >= last) {
    return {};
  }
  return std::string(first, last);
}

[[nodiscard]] std::string normalized_conclusion(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char byte) {
    return static_cast<char>(std::tolower(byte));
  });
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

[[nodiscard]] std::string json_string(const contracts::Json &value,
                                      std::string_view label) {
  if (!value.is_string()) {
    policy_error(std::string(label) + " must be a string");
  }
  return value.get<std::string>();
}

[[nodiscard]] std::vector<std::string>
json_strings(const contracts::Json &payload, std::string_view key,
             bool canonical = false) {
  const auto found = payload.find(key);
  if (found == payload.end()) {
    return {};
  }
  if (!found->is_array()) {
    policy_error(std::string(key) + " must be an array");
  }
  std::vector<std::string> result;
  for (const auto &value : *found) {
    const std::string item = json_string(value, key);
    if (!item.empty()) {
      result.push_back(item);
    }
  }
  return canonical ? canonical_strings(result) : result;
}

[[nodiscard]] std::map<std::string, ReasoningStep>
source_steps(const std::vector<ReasoningPath> &sources) {
  std::map<std::string, ReasoningStep> steps;
  for (const auto &path : sources) {
    for (const auto &step : path.steps) {
      const auto found = steps.find(step.step_id);
      if (found != steps.end() && found->second.signature != step.signature) {
        policy_error("synthesis source paths contain conflicting step IDs");
      }
      steps[step.step_id] = step;
    }
  }
  return steps;
}

void validate_score(int value, std::string_view label) {
  if (value < 0 || value > score_scale) {
    policy_error(std::string(label) + " must be 0..10000");
  }
}

} // namespace

bool compatible_synthesis_sources(const ReasoningPath &winner_value,
                                  const std::vector<ReasoningPath> &sources) {
  const auto winner = canonicalize_reasoning_path(winner_value);
  const std::set<std::string> winner_hypotheses(winner.hypothesis_ids.begin(),
                                                 winner.hypothesis_ids.end());
  const std::string winner_conclusion =
      normalized_conclusion(winner.conclusion);
  for (const auto &source_value : sources) {
    const auto source = canonicalize_reasoning_path(source_value);
    if (source.path_id == winner.path_id) {
      continue;
    }
    std::vector<std::string> intersection;
    std::ranges::set_intersection(
        winner_hypotheses, source.hypothesis_ids,
        std::back_inserter(intersection));
    if (!intersection.empty() ||
        normalized_conclusion(source.conclusion) == winner_conclusion) {
      continue;
    }
    return false;
  }
  return true;
}

ReasoningPath make_synthesis_path(
    const ReasoningProblem &problem_value, const ReasoningPath &winner_value,
    const std::vector<ReasoningPath> &source_values, std::string conclusion,
    std::vector<std::string> accepted_step_ids) {
  const auto problem = canonicalize_reasoning_problem(problem_value);
  const auto winner = canonicalize_reasoning_path(winner_value);
  if (source_values.empty()) {
    policy_error("synthesis requires at least one source path");
  }
  std::vector<ReasoningPath> sources;
  sources.reserve(source_values.size());
  for (const auto &value : source_values) {
    sources.push_back(canonicalize_reasoning_path(value));
  }
  conclusion = trim(std::move(conclusion));
  if (conclusion.empty()) {
    policy_error("synthesis conclusion must be non-empty");
  }

  const auto available = source_steps(sources);
  accepted_step_ids = stable_strings(accepted_step_ids);
  if (accepted_step_ids.empty()) {
    for (const auto &step : winner.steps) {
      accepted_step_ids.push_back(step.step_id);
    }
  }
  for (const auto &step_id : accepted_step_ids) {
    if (!available.contains(step_id)) {
      policy_error("synthesis references a step outside its source paths");
    }
  }

  std::set<std::string> known_hypotheses;
  for (const auto &source : sources) {
    known_hypotheses.insert(source.hypothesis_ids.begin(),
                            source.hypothesis_ids.end());
  }
  std::set<std::string> known_prior;
  std::vector<ReasoningStep> ordered_steps;
  for (const auto &step_id : accepted_step_ids) {
    const auto &step = available.at(step_id);
    std::set<std::string> allowed{"problem"};
    allowed.insert(known_hypotheses.begin(), known_hypotheses.end());
    allowed.insert(known_prior.begin(), known_prior.end());
    for (const auto &premise : step.premises) {
      if (!allowed.contains(premise)) {
        policy_error("synthesis step order breaks a source premise");
      }
    }
    known_prior.insert(step.step_id);
    ordered_steps.push_back(step);
  }

  ReasoningPath path;
  path.perspective = "synthesis";
  path.path_id = "synthesis-pending";
  path.hypothesis_ids = {known_hypotheses.begin(), known_hypotheses.end()};
  path.steps = std::move(ordered_steps);
  path.conclusion = std::move(conclusion);
  path.provider_confidence_bp = 0;
  int total_cost = 0;
  path.goal_relevance_bp = score_scale;
  path.risk_bp = 0;
  for (const auto &source : sources) {
    total_cost += source.estimated_cost_bp;
    path.goal_relevance_bp =
        std::min(path.goal_relevance_bp, source.goal_relevance_bp);
    path.risk_bp = std::max(path.risk_bp, source.risk_bp);
  }
  path.structure_signature = path_structure_signature(path);
  path.path_id = "synthesis-path:" + path.structure_signature;

  contracts::Json steps = contracts::Json::array();
  for (const auto &step : path.steps) {
    steps.push_back(to_json(step));
  }
  const auto material = contracts::Json{
      {"problem_id", problem.problem_id},
      {"path_id", path.path_id},
      {"perspective", path.perspective},
      {"hypothesis_ids", path.hypothesis_ids},
      {"steps", std::move(steps)},
      {"conclusion", path.conclusion},
      {"provider_confidence_bp", path.provider_confidence_bp},
      {"estimated_cost_bp", total_cost},
      {"goal_relevance_bp", path.goal_relevance_bp},
      {"risk_bp", path.risk_bp},
      {"structure_signature", path.structure_signature}};
  path.estimated_cost_bp = std::min(score_scale, total_cost);
  path.signature = contracts::sha256_json(material);
  return canonicalize_reasoning_path(std::move(path));
}

SynthesisResult fallback_to_verified_winner(
    const ReasoningPath &winner_value, const VerifierReport &verifier_value,
    std::vector<std::string> reasons) {
  const auto winner = canonicalize_reasoning_path(winner_value);
  const auto verifier = canonicalize_verifier_report(verifier_value);
  if (verifier.path_id != winner.path_id) {
    policy_error("synthesis verifier does not match the winning path");
  }
  SynthesisResult result;
  result.winning_path_id = winner.path_id;
  result.synthesized_path_id = winner.path_id;
  result.source_path_ids = {winner.path_id};
  for (const auto &step : winner.steps) {
    result.accepted_node_ids.push_back(step.step_id);
  }
  result.merged_conclusion = winner.conclusion;
  result.confidence_bp = verifier.score_bp;
  result.verifier_report_id = verifier.report_id;
  result.topology_signature = winner.structure_signature;
  result.verified = verifier.verdict != "REJECT";
  result.fallback_used = true;
  result.failure_reasons = std::move(reasons);
  return make_synthesis_result(std::move(result));
}

ValidatedSynthesis validate_synthesis_payload(
    const ReasoningProblem &problem, const ReasoningPath &winner_value,
    const std::vector<ReasoningPath> &survivor_values,
    const VerifierReport &winner_verifier_value,
    const contracts::Json &payload, bool verify_synthesis,
    const std::optional<VerifierReport> &synthesis_verifier_value) {
  if (!payload.is_object()) {
    policy_error("synthesis payload must be an object");
  }
  const auto winner = canonicalize_reasoning_path(winner_value);
  const auto winner_verifier =
      canonicalize_verifier_report(winner_verifier_value);
  if (winner_verifier.path_id != winner.path_id) {
    policy_error("synthesis verifier does not match the winning path");
  }
  std::map<std::string, ReasoningPath> available;
  for (const auto &value : survivor_values) {
    const auto survivor = canonicalize_reasoning_path(value);
    if (!available.emplace(survivor.path_id, survivor).second) {
      policy_error("synthesis survivor path identities must be unique");
    }
  }

  const auto conclusion_value = payload.find("conclusion");
  const std::string conclusion =
      conclusion_value == payload.end()
          ? std::string{}
          : trim(json_string(*conclusion_value, "conclusion"));
  const auto source_ids = json_strings(payload, "source_path_ids", true);
  if (conclusion.empty() ||
      std::ranges::find(source_ids, winner.path_id) == source_ids.end()) {
    policy_error("synthesis source binding is invalid");
  }
  std::vector<ReasoningPath> sources;
  for (const auto &source_id : source_ids) {
    const auto found = available.find(source_id);
    if (found == available.end()) {
      policy_error("synthesis source binding is invalid");
    }
    sources.push_back(found->second);
  }
  if (!compatible_synthesis_sources(winner, sources)) {
    policy_error("synthesis source paths are structurally incompatible");
  }

  const auto accepted = json_strings(payload, "accepted_step_ids");
  const auto rejected = json_strings(payload, "rejected_step_ids");
  const auto source_step_map = source_steps(sources);
  for (const auto &step_id : rejected) {
    if (!source_step_map.contains(step_id)) {
      policy_error("synthesis rejects a step outside its sources");
    }
  }
  const std::set<std::string> accepted_set(accepted.begin(), accepted.end());
  for (const auto &step_id : rejected) {
    if (accepted_set.contains(step_id)) {
      policy_error("synthesis cannot both accept and reject the same step");
    }
  }
  ReasoningPath synthesis_path = make_synthesis_path(
      problem, winner, sources, conclusion, accepted);

  int proposed_confidence = winner_verifier.score_bp;
  if (const auto found = payload.find("confidence_bp"); found != payload.end()) {
    if (!found->is_number_integer()) {
      policy_error("synthesis confidence must be an integer");
    }
    proposed_confidence = found->get<int>();
    validate_score(proposed_confidence, "synthesis confidence");
  }

  SynthesisResult result;
  result.winning_path_id = winner.path_id;
  result.synthesized_path_id = synthesis_path.path_id;
  result.source_path_ids = source_ids;
  for (const auto &step : synthesis_path.steps) {
    result.accepted_node_ids.push_back(step.step_id);
  }
  result.rejected_node_ids = rejected;
  result.merged_conclusion = conclusion;
  result.remaining_uncertainties =
      json_strings(payload, "remaining_uncertainties");
  result.topology_signature = synthesis_path.structure_signature;
  if (!verify_synthesis) {
    result.confidence_bp =
        std::min(winner_verifier.score_bp, proposed_confidence);
    result.verified = false;
    result.failure_reasons = {
        "synthesis verification disabled by qualification ablation"};
  } else {
    if (!synthesis_verifier_value.has_value()) {
      policy_error("synthesis verification report is required");
    }
    const auto synthesis_verifier =
        canonicalize_verifier_report(*synthesis_verifier_value);
    if (synthesis_verifier.path_id != synthesis_path.path_id) {
      policy_error("synthesis verifier does not match the synthesized path");
    }
    if (synthesis_verifier.verdict == "REJECT") {
      policy_error("synthesized candidate failed independent verification");
    }
    result.confidence_bp =
        std::min(synthesis_verifier.score_bp, proposed_confidence);
    result.verifier_report_id = synthesis_verifier.report_id;
    result.verified = true;
  }
  return {.path = std::move(synthesis_path),
          .result = make_synthesis_result(std::move(result))};
}

std::pair<std::string, std::vector<std::string>>
validate_synthesized_conclusion(const ReasoningPath &winner_value,
                                const std::vector<ReasoningPath> &survivors,
                                const contracts::Json &payload) {
  const auto winner = canonicalize_reasoning_path(winner_value);
  if (!payload.is_object()) {
    return {winner.conclusion, {winner.path_id}};
  }
  try {
    const auto conclusion_value = payload.find("conclusion");
    const std::string conclusion =
        conclusion_value == payload.end()
            ? std::string{}
            : trim(json_string(*conclusion_value, "conclusion"));
    const auto source_ids = json_strings(payload, "source_path_ids", true);
    std::set<std::string> allowed;
    for (const auto &survivor : survivors) {
      allowed.insert(survivor.path_id);
    }
    const bool invalid_source = std::ranges::any_of(
        source_ids, [&allowed](const std::string &identity) {
          return !allowed.contains(identity);
        });
    if (conclusion.empty() ||
        std::ranges::find(source_ids, winner.path_id) == source_ids.end() ||
        invalid_source) {
      return {winner.conclusion, {winner.path_id}};
    }
    return {conclusion, source_ids};
  } catch (const common::Error &) {
    return {winner.conclusion, {winner.path_id}};
  }
}

} // namespace statewright::reasoning
