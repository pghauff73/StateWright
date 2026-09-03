#include "statewright/saa/retrieve_first.hpp"

#include "statewright/contracts/hash.hpp"

#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[nodiscard]] Json optional_json(const std::optional<std::string> &value) {
  return value ? Json(*value) : Json(nullptr);
}

} // namespace

RetrieveFirstController::RetrieveFirstController(
    MathematicalAlgorithmCatalog *mathematical_catalog,
    const ReasoningAlgorithmCatalog *reasoning_catalog,
    SemanticMeaningEquivalence *ontology)
    : mathematical_catalog_(mathematical_catalog),
      reasoning_catalog_(reasoning_catalog), ontology_(ontology) {}

RetrieveFirstReceipt RetrieveFirstController::evaluate(
    UnifiedProblemRequirements requirements) const {
  requirements = canonical_unified_requirements(std::move(requirements));
  std::vector<std::string> infrastructure_missing;
  if (requirements.require_mathematical_algorithm &&
      mathematical_catalog_ == nullptr) {
    infrastructure_missing.emplace_back("MATHEMATICAL_ALGORITHM_STORE");
  }
  if (requirements.require_reasoning_algorithm && reasoning_catalog_ == nullptr) {
    infrastructure_missing.emplace_back("REASONING_ALGORITHM_STORE");
  }
  if (!infrastructure_missing.empty()) {
    const Json payload =
        {{"missing", infrastructure_missing},
         {"problem", to_json(requirements)},
         {"status", "RETRIEVAL_INFRASTRUCTURE_MISSING"},
         {"version", retrieve_first_version}};
    return {
        .schema_version = 1,
        .policy_version = std::string(retrieve_first_version),
        .status = "RETRIEVAL_INFRASTRUCTURE_MISSING",
        .retrieval_attempted = false,
        .required_search_completed = false,
        .new_algorithm_generation_allowed = false,
        .adaptation_allowed = false,
        .generation_scope = {},
        .selected_mathematical_algorithm_id = std::nullopt,
        .selected_reasoning_id = std::nullopt,
        .retrieval_decision_signature = {},
        .explanation_signature = {},
        .fit_gap_dimensions = {},
        .guidance = {"Required canonical stores are unavailable. Do not claim "
                     "that novelty search has been completed."},
        .receipt_signature = contracts::sha256_json(payload)};
  }

  const auto decision = retrieve_unified_solution(
      mathematical_catalog_, reasoning_catalog_, requirements, ontology_);
  const auto explanation = explain_unified_retrieval(decision, requirements);
  std::string status;
  bool generation_allowed = false;
  bool adaptation_allowed = false;
  std::vector<std::string> guidance;
  if (decision.required_components_satisfied) {
    status = "REUSE_QUALIFIED_KNOWN_SOLUTION";
    guidance = {
        "Reuse the selected qualified canonical algorithms; do not invent a "
        "replacement algorithm without new contrary evidence or changed "
        "requirements."};
  } else if (decision.selected_mathematical_algorithm_id ||
             decision.selected_reasoning_id) {
    status = "ADAPT_OR_FILL_CONFIRMED_GAP";
    generation_allowed = true;
    adaptation_allowed = true;
    guidance = {
        "Reuse the qualified component that fits and generate or adapt only "
        "the explicitly missing component(s).",
        "Use the system-verified fit-gap dimensions as the adaptation "
        "boundary.",
        "Any adapted or newly composed algorithm remains unqualified until "
        "it passes the applicable evidence gates."};
  } else {
    status = "NOVEL_GENERATION_ALLOWED_AFTER_QUALIFIED_SEARCH";
    generation_allowed = true;
    adaptation_allowed = true;
    guidance = {
        "Qualified canonical stores were searched and no eligible known "
        "solution was found for the required components.",
        "Novel generation is allowed only inside the confirmed missing scope "
        "and fit-gap dimensions and must be qualified before canonical "
        "reuse."};
  }
  const Json payload =
      {{"adaptation_allowed", adaptation_allowed},
       {"explanation_signature", explanation.explanation_signature},
       {"fit_gap_dimensions", explanation.fit_gap_dimensions},
       {"generation_allowed", generation_allowed},
       {"generation_scope", decision.missing_components},
       {"problem_signature", decision.problem_signature},
       {"retrieval_decision_signature", decision.decision_signature},
       {"selected_mathematical_algorithm_id",
        optional_json(decision.selected_mathematical_algorithm_id)},
       {"selected_reasoning_id", optional_json(decision.selected_reasoning_id)},
       {"status", status},
       {"version", retrieve_first_version}};
  return {.schema_version = 1,
          .policy_version = std::string(retrieve_first_version),
          .status = std::move(status),
          .retrieval_attempted = true,
          .required_search_completed = true,
          .new_algorithm_generation_allowed = generation_allowed,
          .adaptation_allowed = adaptation_allowed,
          .generation_scope = decision.missing_components,
          .selected_mathematical_algorithm_id =
              decision.selected_mathematical_algorithm_id,
          .selected_reasoning_id = decision.selected_reasoning_id,
          .retrieval_decision_signature = decision.decision_signature,
          .explanation_signature = explanation.explanation_signature,
          .fit_gap_dimensions = explanation.fit_gap_dimensions,
          .guidance = std::move(guidance),
          .receipt_signature = contracts::sha256_json(payload)};
}

Json to_json(const RetrieveFirstReceipt &value) {
  return {{"adaptation_allowed", value.adaptation_allowed},
          {"explanation_signature", value.explanation_signature},
          {"fit_gap_dimensions", value.fit_gap_dimensions},
          {"generation_scope", value.generation_scope},
          {"guidance", value.guidance},
          {"new_algorithm_generation_allowed",
           value.new_algorithm_generation_allowed},
          {"policy_version", value.policy_version},
          {"receipt_signature", value.receipt_signature},
          {"required_search_completed", value.required_search_completed},
          {"retrieval_attempted", value.retrieval_attempted},
          {"retrieval_decision_signature",
           value.retrieval_decision_signature},
          {"schema_version", value.schema_version},
          {"selected_mathematical_algorithm_id",
           optional_json(value.selected_mathematical_algorithm_id)},
          {"selected_reasoning_id", optional_json(value.selected_reasoning_id)},
          {"status", value.status}};
}

} // namespace statewright::saa
