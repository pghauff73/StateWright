#pragma once

#include "statewright/saa/algorithm_adaptation.hpp"
#include "statewright/saa/experiment_aggregation.hpp"
#include "statewright/saa/multistep_evolution.hpp"
#include "statewright/saa/retrieve_first.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view intelligence_loop_version =
    "saa-closed-intelligence-improvement-loop-v1";

struct IntelligenceImprovementDecision final {
  int schema_version = 1;
  std::string loop_version = std::string(intelligence_loop_version);
  std::string phase;
  std::string status;
  std::string next_action;
  bool terminal = false;
  std::vector<std::string> permitted_actions;
  std::vector<std::string> blocking_reasons;
  std::optional<std::string> selected_mathematical_algorithm_id;
  std::optional<std::string> selected_reasoning_id;
  std::optional<std::string> candidate_ref;
  std::optional<std::string> promoted_canonical_algorithm_ref;
  std::string retrieval_receipt_signature;
  std::string explanation_signature;
  std::string adaptation_plan_signature;
  std::string evolution_assessment_signature;
  std::string experiment_aggregate_signature;
  std::string promotion_ref;
  std::string post_promotion_receipt_signature;
  std::string decision_signature;
};

[[nodiscard]] IntelligenceImprovementDecision
evaluate_intelligence_improvement_loop(
    const RetrieveFirstReceipt &receipt,
    const RetrievalExplanation *explanation = nullptr,
    const ControlledAdaptationPlan *adaptation_plan = nullptr,
    const MultiStepEvolutionAssessment *evolution_assessment = nullptr,
    const RepeatedExperimentAggregate *experiment_aggregate = nullptr,
    std::optional<std::string> candidate_ref = std::nullopt,
    std::string promotion_ref = {},
    std::optional<std::string> promoted_canonical_algorithm_ref = std::nullopt,
    std::string promoted_component = {},
    const RetrieveFirstReceipt *post_promotion_receipt = nullptr);

[[nodiscard]] contracts::Json
to_json(const IntelligenceImprovementDecision &value);

} // namespace statewright::saa
