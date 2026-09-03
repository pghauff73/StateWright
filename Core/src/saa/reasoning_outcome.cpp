#include "statewright/saa/reasoning_outcome.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[noreturn]] void outcome_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument,
                      std::move(message));
}

[[nodiscard]] std::string trimmed(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n\f\v");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n\f\v");
  return value.substr(first, last - first + 1U);
}

[[nodiscard]] std::string normalized_text(std::string value) {
  std::istringstream input(value);
  std::ostringstream output;
  std::string word;
  bool first = true;
  while (input >> word) {
    if (!first) {
      output << ' ';
    }
    std::transform(word.begin(), word.end(), word.begin(),
                   [](unsigned char character) {
                     return static_cast<char>(std::tolower(character));
                   });
    output << word;
    first = false;
  }
  return output.str();
}

[[nodiscard]] std::string uppercase(std::string value) {
  value = trimmed(std::move(value));
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::toupper(character));
                 });
  return value;
}

[[nodiscard]] std::vector<std::string>
normalized_texts(std::vector<std::string> values) {
  std::set<std::string> normalized;
  for (auto &value : values) {
    value = normalized_text(std::move(value));
    if (!value.empty()) {
      normalized.insert(std::move(value));
    }
  }
  return {normalized.begin(), normalized.end()};
}

[[nodiscard]] std::vector<std::string>
normalized_ids(std::vector<std::string> values) {
  std::set<std::string> normalized;
  for (auto &value : values) {
    value = trimmed(std::move(value));
    if (!value.empty()) {
      normalized.insert(std::move(value));
    }
  }
  return {normalized.begin(), normalized.end()};
}

[[nodiscard]] std::vector<std::pair<std::string, bool>>
normalized_invariant_results(
    std::vector<std::pair<std::string, bool>> values) {
  std::map<std::string, bool> normalized;
  for (auto &[label_value, result] : values) {
    std::string label = normalized_text(std::move(label_value));
    if (label.empty()) {
      outcome_error("reasoning outcome invariant labels must be non-empty");
    }
    if (!normalized.emplace(std::move(label), result).second) {
      outcome_error("duplicate reasoning outcome invariant result");
    }
  }
  return {normalized.begin(), normalized.end()};
}

[[nodiscard]] std::vector<std::pair<std::string, std::string>>
normalized_falsifier_results(
    std::vector<std::pair<std::string, std::string>> values) {
  static const std::set<std::string> supported = {"SURVIVED", "TRIGGERED",
                                                  "UNTESTED"};
  std::map<std::string, std::string> normalized;
  for (auto &[label_value, result_value] : values) {
    std::string label = normalized_text(std::move(label_value));
    std::string result = uppercase(std::move(result_value));
    if (label.empty()) {
      outcome_error("reasoning falsifier labels must be non-empty");
    }
    if (!supported.contains(result)) {
      outcome_error("unsupported reasoning falsifier result: '" + result +
                    "'");
    }
    if (!normalized.emplace(std::move(label), std::move(result)).second) {
      outcome_error("duplicate reasoning falsifier result");
    }
  }
  return {normalized.begin(), normalized.end()};
}

[[nodiscard]] Json invariant_json(
    const std::vector<std::pair<std::string, bool>> &values) {
  Json result = Json::array();
  for (const auto &[name, value] : values) {
    result.push_back(Json::array({name, value}));
  }
  return result;
}

[[nodiscard]] Json falsifier_json(
    const std::vector<std::pair<std::string, std::string>> &values) {
  Json result = Json::array();
  for (const auto &[name, value] : values) {
    result.push_back(Json::array({name, value}));
  }
  return result;
}

[[nodiscard]] int algorithm_max_steps(
    const CanonicalReasoningAlgorithm &algorithm) {
  if (!algorithm.termination.is_object() ||
      !algorithm.termination.contains("max_steps") ||
      !algorithm.termination.at("max_steps").is_number_integer()) {
    return 0;
  }
  return algorithm.termination.at("max_steps").get<int>();
}

[[nodiscard]] ReasoningGroundingEvidence grounded_evidence(
    const ReasoningEvidenceResolver &resolver, std::string_view evidence_id) {
  std::optional<ReasoningGroundingEvidence> evidence;
  try {
    evidence = resolver(evidence_id);
  } catch (const std::exception &) {
    evidence = std::nullopt;
  }
  if (!evidence) {
    outcome_error("reasoning outcome evidence is not registered: " +
                  std::string(evidence_id));
  }
  if (evidence->object_type != "egcf-evidence") {
    outcome_error(
        "reasoning outcome evidence must reference EvidenceArtifact");
  }
  if (evidence->success != true || evidence->simulated) {
    outcome_error(
        "reasoning outcome evidence must be successful and non-simulated");
  }
  if (!evidence->producer.starts_with("deterministic-") &&
      !evidence->producer.starts_with("human-")) {
    outcome_error(
        "reasoning outcome evidence must be deterministic or human-grounded");
  }
  const std::string method = normalized_text(evidence->method);
  if (method == "reported" || method == "model-claimed" ||
      method == "model-generated-claim") {
    outcome_error(
        "reported/model-claimed evidence cannot qualify reasoning outcomes");
  }
  return *evidence;
}

} // namespace

std::vector<std::string> reasoning_evidence_requirements(
    const CanonicalReasoningAlgorithm &algorithm) {
  std::set<std::string> result;
  for (const auto &node : algorithm.canonical_nodes) {
    if (!node.is_object() || !node.contains("evidence_requirements") ||
        !node.at("evidence_requirements").is_array()) {
      continue;
    }
    for (const auto &value : node.at("evidence_requirements")) {
      if (!value.is_string()) {
        continue;
      }
      const std::string normalized = normalized_text(value.get<std::string>());
      if (!normalized.empty()) {
        result.insert(normalized);
      }
    }
  }
  return {result.begin(), result.end()};
}

std::vector<std::string>
reasoning_falsifiers(const CanonicalReasoningAlgorithm &algorithm) {
  std::set<std::string> result;
  for (const auto &node : algorithm.canonical_nodes) {
    if (!node.is_object() || !node.contains("falsifiers") ||
        !node.at("falsifiers").is_array()) {
      continue;
    }
    for (const auto &value : node.at("falsifiers")) {
      if (!value.is_string()) {
        continue;
      }
      const std::string normalized = normalized_text(value.get<std::string>());
      if (!normalized.empty()) {
        result.insert(normalized);
      }
    }
  }
  return {result.begin(), result.end()};
}

ReasoningExecutionOutcome make_reasoning_execution_outcome(
    const CanonicalReasoningAlgorithm &algorithm, std::string execution_id,
    std::vector<std::string> observed_output_semantics,
    std::vector<std::string> evidence_ids,
    std::vector<std::pair<std::string, bool>> invariant_results,
    std::vector<std::pair<std::string, std::string>> falsifier_results,
    bool termination_satisfied, int steps_used, bool execution_success,
    bool independent_review) {
  execution_id = trimmed(std::move(execution_id));
  if (execution_id.empty()) {
    outcome_error("reasoning outcome execution_id must be non-empty");
  }
  if (steps_used < 0) {
    outcome_error("reasoning outcome steps_used cannot be negative");
  }
  if (steps_used > algorithm_max_steps(algorithm)) {
    outcome_error("reasoning outcome steps_used exceeds algorithm bound");
  }
  observed_output_semantics =
      normalized_texts(std::move(observed_output_semantics));
  evidence_ids = normalized_ids(std::move(evidence_ids));
  invariant_results =
      normalized_invariant_results(std::move(invariant_results));
  falsifier_results =
      normalized_falsifier_results(std::move(falsifier_results));
  const Json payload = {
      {"schema_version", 1},
      {"outcome_version", reasoning_outcome_version},
      {"canonical_reasoning_signature",
       algorithm.canonical_reasoning_signature},
      {"execution_id", execution_id},
      {"observed_output_semantics", observed_output_semantics},
      {"evidence_ids", evidence_ids},
      {"invariant_results", invariant_json(invariant_results)},
      {"falsifier_results", falsifier_json(falsifier_results)},
      {"termination_satisfied", termination_satisfied},
      {"steps_used", steps_used},
      {"execution_success", execution_success},
      {"independent_review", independent_review}};
  return {.schema_version = 1,
          .outcome_version = std::string(reasoning_outcome_version),
          .canonical_reasoning_signature =
              algorithm.canonical_reasoning_signature,
          .execution_id = std::move(execution_id),
          .observed_output_semantics =
              std::move(observed_output_semantics),
          .evidence_ids = std::move(evidence_ids),
          .invariant_results = std::move(invariant_results),
          .falsifier_results = std::move(falsifier_results),
          .termination_satisfied = termination_satisfied,
          .steps_used = steps_used,
          .execution_success = execution_success,
          .independent_review = independent_review,
          .outcome_signature = contracts::sha256_json(payload)};
}

ReasoningOutcomeQualification qualify_reasoning_outcome(
    const ReasoningEvidenceResolver &evidence_resolver,
    const CanonicalReasoningAlgorithm &algorithm,
    const ReasoningExecutionOutcome &outcome) {
  if (outcome.canonical_reasoning_signature !=
      algorithm.canonical_reasoning_signature) {
    outcome_error(
        "reasoning outcome belongs to a different canonical algorithm");
  }

  auto expected_outputs = algorithm.output_semantics;
  std::sort(expected_outputs.begin(), expected_outputs.end());
  const bool output_eligible =
      outcome.observed_output_semantics == expected_outputs;

  const std::set<std::string> expected_invariants(algorithm.invariants.begin(),
                                                  algorithm.invariants.end());
  std::map<std::string, bool> invariant_map(outcome.invariant_results.begin(),
                                            outcome.invariant_results.end());
  std::set<std::string> actual_invariants;
  for (const auto &[key, unused] : invariant_map) {
    static_cast<void>(unused);
    actual_invariants.insert(key);
  }
  const bool invariant_eligible =
      actual_invariants == expected_invariants &&
      std::all_of(invariant_map.begin(), invariant_map.end(),
                  [](const auto &entry) { return entry.second; });

  const auto expected_falsifier_values = reasoning_falsifiers(algorithm);
  const std::set<std::string> expected_falsifiers(
      expected_falsifier_values.begin(), expected_falsifier_values.end());
  std::map<std::string, std::string> falsifier_map(
      outcome.falsifier_results.begin(), outcome.falsifier_results.end());
  std::set<std::string> actual_falsifiers;
  for (const auto &[key, unused] : falsifier_map) {
    static_cast<void>(unused);
    actual_falsifiers.insert(key);
  }
  const bool falsifier_eligible =
      actual_falsifiers == expected_falsifiers &&
      std::all_of(falsifier_map.begin(), falsifier_map.end(),
                  [](const auto &entry) {
                    return entry.second == "SURVIVED";
                  });

  const int max_steps = algorithm_max_steps(algorithm);
  const bool termination_eligible =
      outcome.termination_satisfied && outcome.execution_success &&
      outcome.steps_used >= 0 && outcome.steps_used <= max_steps;

  const auto requirement_values = reasoning_evidence_requirements(algorithm);
  const std::set<std::string> requirements(requirement_values.begin(),
                                           requirement_values.end());
  std::vector<std::string> grounded;
  std::set<std::string> covered;
  std::set<std::string> groups;
  bool evidence_error = false;
  for (const auto &evidence_id : outcome.evidence_ids) {
    try {
      const auto evidence = grounded_evidence(evidence_resolver, evidence_id);
      grounded.push_back(evidence_id);
      const auto evidence_requirements =
          normalized_texts(evidence.requirement_ids);
      covered.insert(evidence_requirements.begin(),
                     evidence_requirements.end());
      const std::string group =
          normalized_text(evidence.independence_group);
      if (!group.empty()) {
        groups.insert(group);
      }
    } catch (const common::Error &) {
      evidence_error = true;
    }
  }
  int covered_count = 0;
  for (const auto &requirement : requirements) {
    if (covered.contains(requirement)) {
      ++covered_count;
    }
  }
  const int coverage_bp = requirements.empty()
                              ? 10000
                              : (10000 * covered_count) /
                                    static_cast<int>(requirements.size());
  const bool evidence_eligible = !evidence_error && !grounded.empty() &&
                                 coverage_bp == 10000 && !groups.empty();
  const bool exact_canonical =
      algorithm.canonicalization_strength ==
      "EXACT_BOUNDED_GRAPH_CANONICALIZATION";
  const bool eligible = exact_canonical && output_eligible &&
                        invariant_eligible && falsifier_eligible &&
                        termination_eligible && evidence_eligible &&
                        outcome.independent_review;

  std::string status;
  if (!exact_canonical) {
    status = "UNQUALIFIED_REASONING_CANONICALIZATION";
  } else if (!output_eligible) {
    status = "UNQUALIFIED_REASONING_OUTPUT_CONTRACT";
  } else if (!invariant_eligible) {
    status = "UNQUALIFIED_REASONING_INVARIANT_FAILURE";
  } else if (!falsifier_eligible) {
    status = "UNQUALIFIED_REASONING_FALSIFIER";
  } else if (!termination_eligible) {
    status = "UNQUALIFIED_REASONING_TERMINATION";
  } else if (!evidence_eligible) {
    status = "UNQUALIFIED_REASONING_EVIDENCE";
  } else if (!outcome.independent_review) {
    status = "UNQUALIFIED_REASONING_INDEPENDENT_REVIEW";
  } else {
    status = "QUALIFIED_REASONING_OUTCOME";
  }
  std::vector<std::string> independence_groups(groups.begin(), groups.end());
  std::vector<std::string> warnings;
  if (!eligible) {
    warnings.push_back(
        "A completed reasoning execution is not reusable canonical evidence until semantics, invariants, falsifiers, termination and grounded evidence all qualify.");
  }
  const Json payload = {
      {"schema_version", 1},
      {"outcome_version", reasoning_outcome_version},
      {"canonical_reasoning_signature",
       algorithm.canonical_reasoning_signature},
      {"outcome_signature", outcome.outcome_signature},
      {"status", status},
      {"coverage_bp", coverage_bp},
      {"grounded_evidence_ids", grounded},
      {"independence_groups", independence_groups},
      {"invariant_eligible", invariant_eligible},
      {"falsifier_eligible", falsifier_eligible},
      {"termination_eligible", termination_eligible},
      {"output_contract_eligible", output_eligible},
      {"canonical_reuse_eligible", eligible}};
  return {.schema_version = 1,
          .outcome_version = std::string(reasoning_outcome_version),
          .canonical_reasoning_signature =
              algorithm.canonical_reasoning_signature,
          .outcome_signature = outcome.outcome_signature,
          .status = std::move(status),
          .evidence_requirement_coverage_bp = coverage_bp,
          .grounded_evidence_ids = std::move(grounded),
          .independence_groups = std::move(independence_groups),
          .invariant_eligible = invariant_eligible,
          .falsifier_eligible = falsifier_eligible,
          .termination_eligible = termination_eligible,
          .output_contract_eligible = output_eligible,
          .canonical_reuse_eligible = eligible,
          .qualification_signature = contracts::sha256_json(payload),
          .warnings = std::move(warnings)};
}

Json to_json(const ReasoningExecutionOutcome &value) {
  return {{"schema_version", value.schema_version},
          {"outcome_version", value.outcome_version},
          {"canonical_reasoning_signature",
           value.canonical_reasoning_signature},
          {"execution_id", value.execution_id},
          {"observed_output_semantics", value.observed_output_semantics},
          {"evidence_ids", value.evidence_ids},
          {"invariant_results", invariant_json(value.invariant_results)},
          {"falsifier_results", falsifier_json(value.falsifier_results)},
          {"termination_satisfied", value.termination_satisfied},
          {"steps_used", value.steps_used},
          {"execution_success", value.execution_success},
          {"independent_review", value.independent_review},
          {"outcome_signature", value.outcome_signature}};
}

Json to_json(const ReasoningOutcomeQualification &value) {
  return {{"schema_version", value.schema_version},
          {"outcome_version", value.outcome_version},
          {"canonical_reasoning_signature",
           value.canonical_reasoning_signature},
          {"outcome_signature", value.outcome_signature},
          {"status", value.status},
          {"evidence_requirement_coverage_bp",
           value.evidence_requirement_coverage_bp},
          {"grounded_evidence_ids", value.grounded_evidence_ids},
          {"independence_groups", value.independence_groups},
          {"invariant_eligible", value.invariant_eligible},
          {"falsifier_eligible", value.falsifier_eligible},
          {"termination_eligible", value.termination_eligible},
          {"output_contract_eligible", value.output_contract_eligible},
          {"canonical_reuse_eligible", value.canonical_reuse_eligible},
          {"qualification_signature", value.qualification_signature},
          {"warnings", value.warnings}};
}

} // namespace statewright::saa
