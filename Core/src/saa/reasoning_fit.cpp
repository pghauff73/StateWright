#include "statewright/saa/reasoning_fit.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[noreturn]] void fit_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument,
                      std::move(message));
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

[[nodiscard]] int coverage(const std::set<std::string> &required,
                           const std::set<std::string> &available) {
  if (required.empty()) {
    return 10000;
  }
  int matched = 0;
  for (const auto &value : required) {
    if (available.contains(value)) {
      ++matched;
    }
  }
  return (10000 * matched) / static_cast<int>(required.size());
}

[[nodiscard]] std::vector<std::string>
difference(const std::set<std::string> &left,
           const std::set<std::string> &right) {
  std::vector<std::string> result;
  std::set_difference(left.begin(), left.end(), right.begin(), right.end(),
                      std::back_inserter(result));
  return result;
}

[[nodiscard]] std::string joined(const std::vector<std::string> &values) {
  std::ostringstream output;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0U) {
      output << ", ";
    }
    output << values[index];
  }
  return output.str();
}

} // namespace

ReasoningTaskRequirements canonical_reasoning_task_requirements(
    ReasoningTaskRequirements requirements) {
  if (requirements.max_steps < 1 ||
      requirements.max_steps > max_reasoning_steps) {
    fit_error("reasoning task max_steps outside supported bounded range");
  }
  requirements.available_inputs =
      normalized_texts(std::move(requirements.available_inputs));
  requirements.desired_outputs =
      normalized_texts(std::move(requirements.desired_outputs));
  requirements.required_applicability =
      normalized_texts(std::move(requirements.required_applicability));
  requirements.required_invariants =
      normalized_texts(std::move(requirements.required_invariants));
  requirements.available_evidence_requirements = normalized_texts(
      std::move(requirements.available_evidence_requirements));
  return requirements;
}

ReasoningFitAssessment evaluate_reasoning_fit(
    std::string reasoning_id, const CanonicalReasoningAlgorithm &algorithm,
    ReasoningTaskRequirements requirements) {
  const auto task =
      canonical_reasoning_task_requirements(std::move(requirements));
  const std::set<std::string> available_inputs(task.available_inputs.begin(),
                                                task.available_inputs.end());
  const std::set<std::string> desired_outputs(task.desired_outputs.begin(),
                                               task.desired_outputs.end());
  const std::set<std::string> required_applicability(
      task.required_applicability.begin(), task.required_applicability.end());
  const std::set<std::string> required_invariants(
      task.required_invariants.begin(), task.required_invariants.end());
  const std::set<std::string> available_evidence(
      task.available_evidence_requirements.begin(),
      task.available_evidence_requirements.end());
  const std::set<std::string> algorithm_inputs(algorithm.input_semantics.begin(),
                                                algorithm.input_semantics.end());
  const std::set<std::string> algorithm_outputs(
      algorithm.output_semantics.begin(), algorithm.output_semantics.end());
  const std::set<std::string> applicability(algorithm.applicability.begin(),
                                            algorithm.applicability.end());
  const std::set<std::string> invariants(algorithm.invariants.begin(),
                                         algorithm.invariants.end());
  const auto evidence_values = reasoning_evidence_requirements(algorithm);
  const std::set<std::string> evidence_requirements(evidence_values.begin(),
                                                     evidence_values.end());
  const int algorithm_max_steps =
      algorithm.termination.is_object()
          ? algorithm.termination.value("max_steps", 0)
          : 0;

  const int input_fit = coverage(algorithm_inputs, available_inputs);
  const int output_fit = coverage(desired_outputs, algorithm_outputs);
  const int applicability_fit =
      coverage(required_applicability, applicability);
  const int invariant_fit = coverage(required_invariants, invariants);
  const int evidence_fit =
      coverage(evidence_requirements, available_evidence);
  const int termination_fit =
      algorithm_max_steps <= task.max_steps
          ? 10000
          : std::max(0, (10000 * task.max_steps) /
                            std::max(1, algorithm_max_steps));

  std::vector<std::string> blockers;
  std::vector<std::string> adaptation;
  const auto missing_inputs = difference(algorithm_inputs, available_inputs);
  const auto missing_outputs = difference(desired_outputs, algorithm_outputs);
  const auto missing_applicability =
      difference(required_applicability, applicability);
  const auto missing_invariants = difference(required_invariants, invariants);
  const auto missing_evidence =
      difference(evidence_requirements, available_evidence);
  if (!missing_inputs.empty()) {
    blockers.push_back("missing required inputs: " + joined(missing_inputs));
  }
  if (!missing_outputs.empty()) {
    blockers.push_back("missing desired outputs: " + joined(missing_outputs));
  }
  if (!missing_applicability.empty()) {
    blockers.push_back("applicability mismatch: " +
                       joined(missing_applicability));
  }
  if (!missing_invariants.empty()) {
    blockers.push_back("required invariants absent: " +
                       joined(missing_invariants));
  }
  if (!missing_evidence.empty()) {
    blockers.push_back("evidence capability unavailable: " +
                       joined(missing_evidence));
  }
  if (algorithm_max_steps > task.max_steps) {
    blockers.push_back("termination budget " +
                       std::to_string(algorithm_max_steps) +
                       " exceeds task budget " +
                       std::to_string(task.max_steps));
  }
  if (!desired_outputs.empty()) {
    const auto extra_outputs = difference(algorithm_outputs, desired_outputs);
    if (!extra_outputs.empty()) {
      adaptation.push_back("algorithm produces additional outputs: " +
                           joined(extra_outputs));
    }
  }
  const auto extra_inputs = difference(available_inputs, algorithm_inputs);
  if (!extra_inputs.empty()) {
    adaptation.push_back("task has unused available inputs: " +
                         joined(extra_inputs));
  }

  const int score = (20 * input_fit + 25 * output_fit +
                     15 * applicability_fit + 15 * invariant_fit +
                     10 * evidence_fit + 15 * termination_fit) /
                    100;
  std::string status;
  if (!blockers.empty()) {
    status = "INELIGIBLE_REASONING_FIT";
  } else if (score >= 9000) {
    status = "GOOD_REASONING_FIT";
  } else if (score >= 6500) {
    status = "PARTIAL_REASONING_FIT";
  } else {
    status = "POOR_REASONING_FIT";
  }
  const Json components = {{"applicability", applicability_fit},
                           {"evidence", evidence_fit},
                           {"input", input_fit},
                           {"invariant", invariant_fit},
                           {"output", output_fit},
                           {"termination", termination_fit}};
  const Json payload = {{"algorithm_signature",
                         algorithm.canonical_reasoning_signature},
                        {"adaptation_gaps", adaptation},
                        {"blocking_gaps", blockers},
                        {"components", components},
                        {"reasoning_id", reasoning_id},
                        {"requirements", to_json(task)},
                        {"score", score},
                        {"version", reasoning_fit_version}};
  return {.reasoning_id = std::move(reasoning_id),
          .canonical_reasoning_signature =
              algorithm.canonical_reasoning_signature,
          .status = std::move(status),
          .fit_score_bp = score,
          .input_fit_bp = input_fit,
          .output_fit_bp = output_fit,
          .applicability_fit_bp = applicability_fit,
          .invariant_fit_bp = invariant_fit,
          .evidence_fit_bp = evidence_fit,
          .termination_fit_bp = termination_fit,
          .blocking_gaps = std::move(blockers),
          .adaptation_gaps = std::move(adaptation),
          .fit_signature = contracts::sha256_json(payload)};
}

ReasoningRetrievalResult retrieve_reasoning_algorithms(
    const ReasoningAlgorithmCatalog &catalog,
    ReasoningTaskRequirements requirements, std::size_t limit,
    bool include_ineligible) {
  if (limit < 1U || limit > max_reasoning_retrieval_results) {
    fit_error("reasoning retrieval limit outside supported range");
  }
  const auto task =
      canonical_reasoning_task_requirements(std::move(requirements));
  const std::string requirements_signature = contracts::sha256_json(
      {{"requirements", to_json(task)}, {"version", reasoning_fit_version}});
  std::vector<ReasoningFitAssessment> assessments;
  for (const auto &reasoning_id : catalog.list_reasoning_ids()) {
    auto assessment = evaluate_reasoning_fit(
        reasoning_id, catalog.load_reasoning_algorithm(reasoning_id), task);
    if (include_ineligible || assessment.eligible()) {
      assessments.push_back(std::move(assessment));
    }
  }
  std::sort(assessments.begin(), assessments.end(),
            [](const auto &left, const auto &right) {
              return std::make_tuple(left.eligible() ? 0 : 1,
                                     -left.fit_score_bp,
                                     left.reasoning_id) <
                     std::make_tuple(right.eligible() ? 0 : 1,
                                     -right.fit_score_bp,
                                     right.reasoning_id);
            });
  if (assessments.size() > limit) {
    assessments.resize(limit);
  }
  const auto selected = std::find_if(
      assessments.begin(), assessments.end(),
      [](const auto &assessment) { return assessment.eligible(); });
  std::optional<std::string> selected_id;
  int selected_score = 0;
  if (selected != assessments.end()) {
    selected_id = selected->reasoning_id;
    selected_score = selected->fit_score_bp;
  }
  std::vector<std::string> candidate_signatures;
  candidate_signatures.reserve(assessments.size());
  for (const auto &assessment : assessments) {
    candidate_signatures.push_back(assessment.fit_signature);
  }
  const Json payload = {
      {"candidate_signatures", candidate_signatures},
      {"requirements_signature", requirements_signature},
      {"search_scope", "QUALIFIED_CANONICAL_REASONING_STORE_ONLY"},
      {"selected_fit_score_bp", selected_score},
      {"selected_reasoning_id",
       selected_id ? Json(*selected_id) : Json(nullptr)},
      {"version", reasoning_fit_version}};
  return {.schema_version = 1,
          .fit_version = std::string(reasoning_fit_version),
          .requirements_signature = requirements_signature,
          .candidates = std::move(assessments),
          .selected_reasoning_id = std::move(selected_id),
          .selected_fit_score_bp = selected_score,
          .search_scope = "QUALIFIED_CANONICAL_REASONING_STORE_ONLY",
          .result_signature = contracts::sha256_json(payload)};
}

Json to_json(const ReasoningTaskRequirements &value) {
  return {{"available_evidence_requirements",
           value.available_evidence_requirements},
          {"available_inputs", value.available_inputs},
          {"desired_outputs", value.desired_outputs},
          {"max_steps", value.max_steps},
          {"required_applicability", value.required_applicability},
          {"required_invariants", value.required_invariants}};
}

Json to_json(const ReasoningFitAssessment &value) {
  return {{"reasoning_id", value.reasoning_id},
          {"canonical_reasoning_signature",
           value.canonical_reasoning_signature},
          {"status", value.status},
          {"fit_score_bp", value.fit_score_bp},
          {"input_fit_bp", value.input_fit_bp},
          {"output_fit_bp", value.output_fit_bp},
          {"applicability_fit_bp", value.applicability_fit_bp},
          {"invariant_fit_bp", value.invariant_fit_bp},
          {"evidence_fit_bp", value.evidence_fit_bp},
          {"termination_fit_bp", value.termination_fit_bp},
          {"blocking_gaps", value.blocking_gaps},
          {"adaptation_gaps", value.adaptation_gaps},
          {"fit_signature", value.fit_signature},
          {"eligible", value.eligible()}};
}

Json to_json(const ReasoningRetrievalResult &value) {
  Json candidates = Json::array();
  for (const auto &candidate : value.candidates) {
    candidates.push_back(to_json(candidate));
  }
  return {{"schema_version", value.schema_version},
          {"fit_version", value.fit_version},
          {"requirements_signature", value.requirements_signature},
          {"candidates", candidates},
          {"selected_reasoning_id",
           value.selected_reasoning_id ? Json(*value.selected_reasoning_id)
                                       : Json(nullptr)},
          {"selected_fit_score_bp", value.selected_fit_score_bp},
          {"search_scope", value.search_scope},
          {"result_signature", value.result_signature}};
}

} // namespace statewright::saa
