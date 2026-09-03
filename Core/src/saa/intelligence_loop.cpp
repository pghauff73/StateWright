#include "statewright/saa/intelligence_loop.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[noreturn]] void loop_error(std::string message) {
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

[[nodiscard]] Json optional_json(const std::optional<std::string> &value) {
  return value ? Json(*value) : Json(nullptr);
}

[[nodiscard]] IntelligenceImprovementDecision decision(
    const RetrieveFirstReceipt &receipt, std::string phase,
    std::string status, std::string next_action, bool terminal,
    std::vector<std::string> permitted_actions,
    std::vector<std::string> blocking_reasons = {},
    std::optional<std::string> candidate_ref = std::nullopt,
    std::optional<std::string> promoted_canonical_algorithm_ref = std::nullopt,
    const RetrievalExplanation *explanation = nullptr,
    const ControlledAdaptationPlan *adaptation_plan = nullptr,
    const MultiStepEvolutionAssessment *evolution_assessment = nullptr,
    const RepeatedExperimentAggregate *experiment_aggregate = nullptr,
    std::string promotion_ref = {},
    const RetrieveFirstReceipt *post_promotion_receipt = nullptr) {
  const std::string explanation_signature =
      explanation == nullptr ? "" : explanation->explanation_signature;
  const std::string plan_signature =
      adaptation_plan == nullptr ? "" : adaptation_plan->plan_signature;
  const std::string evolution_signature = evolution_assessment == nullptr
                                              ? ""
                                              : evolution_assessment
                                                    ->assessment_signature;
  const std::string aggregate_signature = experiment_aggregate == nullptr
                                              ? ""
                                              : experiment_aggregate
                                                    ->aggregate_signature;
  const std::string post_signature = post_promotion_receipt == nullptr
                                         ? ""
                                         : post_promotion_receipt
                                               ->receipt_signature;
  const Json payload =
      {{"adaptation_plan_signature", plan_signature},
       {"blocking_reasons", blocking_reasons},
       {"candidate_ref", optional_json(candidate_ref)},
       {"evolution_assessment_signature", evolution_signature},
       {"experiment_aggregate_signature", aggregate_signature},
       {"explanation_signature", explanation_signature},
       {"next_action", next_action},
       {"permitted_actions", permitted_actions},
       {"phase", phase},
       {"post_promotion_receipt_signature", post_signature},
       {"promoted_canonical_algorithm_ref",
        optional_json(promoted_canonical_algorithm_ref)},
       {"promotion_ref", promotion_ref},
       {"retrieval_receipt_signature", receipt.receipt_signature},
       {"selected_mathematical_algorithm_id",
        optional_json(receipt.selected_mathematical_algorithm_id)},
       {"selected_reasoning_id", optional_json(receipt.selected_reasoning_id)},
       {"status", status},
       {"terminal", terminal},
       {"version", intelligence_loop_version}};
  return {.schema_version = 1,
          .loop_version = std::string(intelligence_loop_version),
          .phase = std::move(phase),
          .status = std::move(status),
          .next_action = std::move(next_action),
          .terminal = terminal,
          .permitted_actions = std::move(permitted_actions),
          .blocking_reasons = std::move(blocking_reasons),
          .selected_mathematical_algorithm_id =
              receipt.selected_mathematical_algorithm_id,
          .selected_reasoning_id = receipt.selected_reasoning_id,
          .candidate_ref = std::move(candidate_ref),
          .promoted_canonical_algorithm_ref =
              std::move(promoted_canonical_algorithm_ref),
          .retrieval_receipt_signature = receipt.receipt_signature,
          .explanation_signature = explanation_signature,
          .adaptation_plan_signature = plan_signature,
          .evolution_assessment_signature = evolution_signature,
          .experiment_aggregate_signature = aggregate_signature,
          .promotion_ref = std::move(promotion_ref),
          .post_promotion_receipt_signature = post_signature,
          .decision_signature = contracts::sha256_json(payload)};
}

} // namespace

IntelligenceImprovementDecision evaluate_intelligence_improvement_loop(
    const RetrieveFirstReceipt &receipt,
    const RetrievalExplanation *explanation,
    const ControlledAdaptationPlan *adaptation_plan,
    const MultiStepEvolutionAssessment *evolution_assessment,
    const RepeatedExperimentAggregate *experiment_aggregate,
    std::optional<std::string> candidate_ref, std::string promotion_ref,
    std::optional<std::string> promoted_canonical_algorithm_ref,
    std::string promoted_component,
    const RetrieveFirstReceipt *post_promotion_receipt) {
  if (!receipt.retrieval_attempted || !receipt.required_search_completed) {
    return decision(
        receipt, "RETRIEVE", "LOOP_BLOCKED_RETRIEVAL_INCOMPLETE",
        "COMPLETE_REQUIRED_QUALIFIED_RETRIEVAL", false, {"RETRIEVE"},
        {"required canonical stores have not been completely searched"});
  }
  if (receipt.status == "REUSE_QUALIFIED_KNOWN_SOLUTION" &&
      receipt.generation_scope.empty() && promotion_ref.empty()) {
    return decision(receipt, "REUSE", "KNOWN_SOLUTION_REUSE_COMPLETE",
                    "USE_QUALIFIED_KNOWN_SOLUTION", true,
                    {"REUSE", "MONITOR_EVIDENCE"});
  }
  if (explanation == nullptr) {
    return decision(receipt, "EXPLAIN_GAP",
                    "LOOP_REQUIRES_DETERMINISTIC_FIT_EXPLANATION",
                    "EXPLAIN_RETRIEVAL_DELTA", false, {"EXPLAIN_GAP"});
  }
  if (explanation->decision_signature !=
      receipt.retrieval_decision_signature) {
    loop_error(
        "SAA-12 retrieval explanation does not belong to the receipt decision");
  }
  if (adaptation_plan == nullptr) {
    return decision(receipt, "PLAN_ADAPTATION",
                    "LOOP_REQUIRES_BOUNDED_ADAPTATION_PLAN",
                    "BUILD_ONE_DIMENSION_ADAPTATION_PLAN", false,
                    {"PLAN_ADAPTATION"}, {}, std::nullopt, std::nullopt,
                    explanation);
  }
  if (adaptation_plan->source_explanation_signature !=
      explanation->explanation_signature) {
    loop_error(
        "SAA-12 adaptation plan does not derive from the supplied explanation");
  }
  if (evolution_assessment == nullptr) {
    return decision(receipt, "EVOLVE",
                    "LOOP_REQUIRES_INVARIANT_PRESERVING_EVOLUTION",
                    "QUALIFY_EACH_EVOLUTION_STEP", false,
                    {"ADAPT_ONE_DIMENSION", "QUALIFY_EVOLUTION_STEP"}, {},
                    std::move(candidate_ref), std::nullopt, explanation,
                    adaptation_plan);
  }
  if (!evolution_assessment->evolution_qualified) {
    return decision(
        receipt, "EVOLVE", "LOOP_BLOCKED_EVOLUTION_NOT_QUALIFIED",
        "REVISE_OR_GATHER_STEP_EVIDENCE", false,
        {"GATHER_EVIDENCE", "REVISE_ADAPTATION"},
        evolution_assessment->blocking_steps,
        evolution_assessment->final_candidate_ref, std::nullopt, explanation,
        adaptation_plan, evolution_assessment);
  }
  if (!candidate_ref || candidate_ref->empty()) {
    candidate_ref = evolution_assessment->final_candidate_ref;
  }
  if (*candidate_ref != evolution_assessment->final_candidate_ref) {
    loop_error(
        "SAA-12 candidate reference disagrees with qualified evolution endpoint");
  }
  if (experiment_aggregate == nullptr) {
    return decision(
        receipt, "EXPERIMENT",
        "LOOP_REQUIRES_REPEATED_COMPARATIVE_EVIDENCE",
        "RUN_AND_AGGREGATE_BOUNDED_AB_EXPERIMENTS", false,
        {"RUN_AB_EXPERIMENT", "AGGREGATE_EXPERIMENTS"}, {}, candidate_ref,
        std::nullopt, explanation, adaptation_plan, evolution_assessment);
  }
  if (!experiment_aggregate->sustained_improvement_qualified) {
    return decision(
        receipt, "EXPERIMENT",
        "LOOP_BLOCKED_SUSTAINED_IMPROVEMENT_NOT_QUALIFIED",
        "GATHER_MORE_INDEPENDENT_EVIDENCE_OR_REVISE", false,
        {"RUN_AB_EXPERIMENT", "REVISE_ADAPTATION", "STOP"},
        {experiment_aggregate->status}, candidate_ref, std::nullopt,
        explanation, adaptation_plan, evolution_assessment,
        experiment_aggregate);
  }
  if (promotion_ref.empty() || !promoted_canonical_algorithm_ref ||
      promoted_canonical_algorithm_ref->empty()) {
    return decision(
        receipt, "QUALIFY_AND_PROMOTE",
        "LOOP_REQUIRES_CANONICAL_QUALIFICATION_AND_PROMOTION",
        "RUN_NORMAL_CANONICAL_QUALIFICATION_THEN_RECORD_PROMOTION", false,
        {"QUALIFY_CANONICALLY", "RECORD_PROMOTION"}, {}, candidate_ref,
        std::nullopt, explanation, adaptation_plan, evolution_assessment,
        experiment_aggregate);
  }
  promoted_component = uppercase(std::move(promoted_component));
  if (promoted_component != "MATHEMATICAL_ALGORITHM" &&
      promoted_component != "REASONING_ALGORITHM") {
    loop_error("SAA-12 promoted_component must identify mathematical or "
               "reasoning algorithm");
  }
  if (post_promotion_receipt == nullptr) {
    return decision(
        receipt, "RE_RETRIEVE", "LOOP_REQUIRES_POST_PROMOTION_RETRIEVAL",
        "RETRIEVE_FROM_UPDATED_CANONICAL_STORES", false, {"RETRIEVE"}, {},
        candidate_ref, promoted_canonical_algorithm_ref, explanation,
        adaptation_plan, evolution_assessment, experiment_aggregate,
        std::move(promotion_ref));
  }
  if (!post_promotion_receipt->retrieval_attempted ||
      !post_promotion_receipt->required_search_completed) {
    return decision(
        receipt, "RE_RETRIEVE",
        "LOOP_BLOCKED_POST_PROMOTION_RETRIEVAL_INCOMPLETE",
        "COMPLETE_POST_PROMOTION_RETRIEVAL", false, {"RETRIEVE"}, {},
        candidate_ref, promoted_canonical_algorithm_ref, explanation,
        adaptation_plan, evolution_assessment, experiment_aggregate,
        std::move(promotion_ref), post_promotion_receipt);
  }
  const std::optional<std::string> &selected =
      promoted_component == "MATHEMATICAL_ALGORITHM"
          ? post_promotion_receipt->selected_mathematical_algorithm_id
          : post_promotion_receipt->selected_reasoning_id;
  if (selected != promoted_canonical_algorithm_ref) {
    return decision(
        receipt, "VERIFY_CLOSURE",
        "LOOP_POST_PROMOTION_RETRIEVAL_DID_NOT_SELECT_PROMOTED_KNOWLEDGE",
        "EXPLAIN_NEW_FIT_OR_STOP", false, {"EXPLAIN_GAP", "STOP"},
        {"promoted canonical algorithm is not the selected qualified fit "
         "after store update"},
        candidate_ref, promoted_canonical_algorithm_ref, explanation,
        adaptation_plan, evolution_assessment, experiment_aggregate,
        std::move(promotion_ref), post_promotion_receipt);
  }
  return decision(
      receipt, "VERIFY_CLOSURE", "CLOSED_LOOP_IMPROVEMENT_VERIFIED",
      "REUSE_PROMOTED_QUALIFIED_KNOWLEDGE", true,
      {"REUSE", "MONITOR_EVIDENCE"}, {}, candidate_ref,
      promoted_canonical_algorithm_ref, explanation, adaptation_plan,
      evolution_assessment, experiment_aggregate, std::move(promotion_ref),
      post_promotion_receipt);
}

Json to_json(const IntelligenceImprovementDecision &value) {
  return {{"adaptation_plan_signature", value.adaptation_plan_signature},
          {"blocking_reasons", value.blocking_reasons},
          {"candidate_ref", optional_json(value.candidate_ref)},
          {"decision_signature", value.decision_signature},
          {"evolution_assessment_signature",
           value.evolution_assessment_signature},
          {"experiment_aggregate_signature",
           value.experiment_aggregate_signature},
          {"explanation_signature", value.explanation_signature},
          {"loop_version", value.loop_version},
          {"next_action", value.next_action},
          {"permitted_actions", value.permitted_actions},
          {"phase", value.phase},
          {"post_promotion_receipt_signature",
           value.post_promotion_receipt_signature},
          {"promoted_canonical_algorithm_ref",
           optional_json(value.promoted_canonical_algorithm_ref)},
          {"promotion_ref", value.promotion_ref},
          {"retrieval_receipt_signature",
           value.retrieval_receipt_signature},
          {"schema_version", value.schema_version},
          {"selected_mathematical_algorithm_id",
           optional_json(value.selected_mathematical_algorithm_id)},
          {"selected_reasoning_id", optional_json(value.selected_reasoning_id)},
          {"status", value.status},
          {"terminal", value.terminal}};
}

} // namespace statewright::saa
