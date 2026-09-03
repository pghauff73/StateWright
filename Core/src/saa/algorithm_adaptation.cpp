#include "statewright/saa/algorithm_adaptation.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <ranges>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

inline constexpr std::array<std::string_view, 17U>
    allowed_adaptation_dimensions = {
        "MATHEMATICAL_INPUT_SEMANTICS",
        "MATHEMATICAL_OUTPUT_SHAPE",
        "MATHEMATICAL_DOMAIN",
        "MATHEMATICAL_CONTRACT",
        "REASONING_INPUT_SEMANTICS",
        "REASONING_OUTPUT_SEMANTICS",
        "REASONING_APPLICABILITY",
        "REASONING_INVARIANTS",
        "REASONING_EVIDENCE_CAPABILITY",
        "REASONING_TERMINATION_BUDGET",
        "REASONING_CONTRACT",
        "BOUNDARY_CONTRACT",
        "INVARIANT_CONTRACT",
        "DYNAMICS_CONTRACT",
        "EVIDENCE_CONTRACT",
        "MISSING_MATHEMATICAL_ALGORITHM",
        "MISSING_REASONING_ALGORITHM"};

[[noreturn]] void adaptation_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

[[nodiscard]] std::string trimmed(std::string value) {
  const auto first = value.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\n\r\f\v");
  return value.substr(first, last - first + 1U);
}

[[nodiscard]] std::string uppercase(std::string value) {
  value = trimmed(std::move(value));
  std::ranges::transform(value, value.begin(), [](const char character) {
    return static_cast<char>(
        std::toupper(static_cast<unsigned char>(character)));
  });
  return value;
}

} // namespace

bool allowed_adaptation_dimension(std::string_view dimension) {
  return std::ranges::find(allowed_adaptation_dimensions, dimension) !=
         allowed_adaptation_dimensions.end();
}

ControlledAdaptationPlan build_controlled_adaptation_plan(
    const RetrievalExplanation &explanation,
    std::string selected_mathematical_algorithm_id,
    std::string selected_reasoning_id) {
  if (explanation.counterfactual_changes.size() > max_adaptation_steps) {
    adaptation_error("SAA-11 adaptation plan exceeds bounded step count");
  }
  std::vector<AdaptationStep> steps;
  steps.reserve(explanation.counterfactual_changes.size());
  for (std::size_t index = 0;
       index < explanation.counterfactual_changes.size(); ++index) {
    const auto &change = explanation.counterfactual_changes[index];
    const std::string dimension = uppercase(change.dimension);
    if (!allowed_adaptation_dimension(dimension)) {
      adaptation_error("unsupported SAA-11 adaptation dimension: " +
                       dimension);
    }
    const std::string component = uppercase(change.component);
    std::string base_id;
    if (component == "MATHEMATICAL_ALGORITHM") {
      base_id = selected_mathematical_algorithm_id;
    } else if (component == "REASONING_ALGORITHM") {
      base_id = selected_reasoning_id;
    }
    const Json material =
        {{"action", "SATISFY_EXACT_COUNTERFACTUAL_CONTRACT"},
         {"dimension", dimension},
         {"required_change", change.required_change}};
    const Json payload =
        {{"base_algorithm_id", base_id},
         {"component", component},
         {"current", change.current},
         {"dimension", dimension},
         {"index", index},
         {"material", material},
         {"target", change.required_change},
         {"version", algorithm_adaptation_version}};
    steps.push_back(
        {.index = static_cast<int>(index),
         .component = component,
         .dimension = dimension,
         .base_algorithm_id = std::move(base_id),
         .current_contract = change.current,
         .target_contract = change.required_change,
         .proposed_change = material,
         .step_signature = contracts::sha256_json(payload)});
  }
  std::vector<std::string> step_signatures;
  for (const auto &step : steps) {
    step_signatures.push_back(step.step_signature);
  }
  const Json payload =
      {{"policy", "ONE_DECLARED_DIMENSION_PER_STEP"},
       {"source_explanation_signature", explanation.explanation_signature},
       {"step_signatures", step_signatures},
       {"version", algorithm_adaptation_version}};
  return {.schema_version = 1,
          .adaptation_version = std::string(algorithm_adaptation_version),
          .source_explanation_signature = explanation.explanation_signature,
          .steps = std::move(steps),
          .one_dimension_per_step = true,
          .qualification_required = !step_signatures.empty(),
          .canonical_reuse_eligible = false,
          .plan_signature = contracts::sha256_json(payload)};
}

AdaptedAlgorithmCandidate create_adapted_candidate(
    const AdaptationStep &step, Json change_material,
    std::string parent_candidate_signature) {
  if (!allowed_adaptation_dimension(step.dimension)) {
    adaptation_error("SAA-11 step uses unsupported adaptation dimension");
  }
  if (!change_material.is_object() || change_material.empty()) {
    adaptation_error("SAA-11 adaptation requires explicit change material");
  }
  const std::string declared = uppercase(
      change_material.value("dimension", step.dimension));
  if (declared != step.dimension) {
    adaptation_error(
        "SAA-11 forbids changing a dimension other than the current step");
  }
  if (change_material.contains("also_changes")) {
    const auto &also_changes = change_material.at("also_changes");
    if ((also_changes.is_array() || also_changes.is_object() ||
         also_changes.is_string()) &&
        !also_changes.empty()) {
      adaptation_error(
          "SAA-11 one-dimension gate rejects multi-dimensional adaptation");
    }
  }
  const Json payload =
      {{"base_algorithm_id", step.base_algorithm_id},
       {"change_material", change_material},
       {"changed_dimension", step.dimension},
       {"component", step.component},
       {"parent_candidate_signature", parent_candidate_signature},
       {"version", algorithm_adaptation_version}};
  const std::string signature = contracts::sha256_json(payload);
  return {.schema_version = 1,
          .adaptation_version = std::string(algorithm_adaptation_version),
          .base_algorithm_id = step.base_algorithm_id,
          .component = step.component,
          .changed_dimension = step.dimension,
          .change_material = std::move(change_material),
          .parent_candidate_signature = std::move(parent_candidate_signature),
          .candidate_signature = signature,
          .epistemic_status =
              "UNQUALIFIED_ADAPTED_ALGORITHM_CANDIDATE",
          .qualification_required = true,
          .canonical_reuse_eligible = false};
}

Json to_json(const AdaptationStep &value) {
  return {{"base_algorithm_id", value.base_algorithm_id},
          {"component", value.component},
          {"current_contract", value.current_contract},
          {"dimension", value.dimension},
          {"index", value.index},
          {"proposed_change", value.proposed_change},
          {"step_signature", value.step_signature},
          {"target_contract", value.target_contract}};
}

Json to_json(const ControlledAdaptationPlan &value) {
  Json steps = Json::array();
  for (const auto &step : value.steps) {
    steps.push_back(to_json(step));
  }
  return {{"adaptation_version", value.adaptation_version},
          {"canonical_reuse_eligible", value.canonical_reuse_eligible},
          {"one_dimension_per_step", value.one_dimension_per_step},
          {"plan_signature", value.plan_signature},
          {"qualification_required", value.qualification_required},
          {"schema_version", value.schema_version},
          {"source_explanation_signature",
           value.source_explanation_signature},
          {"steps", steps}};
}

Json to_json(const AdaptedAlgorithmCandidate &value) {
  return {{"adaptation_version", value.adaptation_version},
          {"base_algorithm_id", value.base_algorithm_id},
          {"candidate_signature", value.candidate_signature},
          {"canonical_reuse_eligible", value.canonical_reuse_eligible},
          {"change_material", value.change_material},
          {"changed_dimension", value.changed_dimension},
          {"component", value.component},
          {"epistemic_status", value.epistemic_status},
          {"parent_candidate_signature", value.parent_candidate_signature},
          {"qualification_required", value.qualification_required},
          {"schema_version", value.schema_version}};
}

} // namespace statewright::saa
