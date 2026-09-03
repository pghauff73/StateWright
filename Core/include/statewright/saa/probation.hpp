#pragma once

#include "statewright/saa/autonomous_promotion_policy.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view probation_version =
    "saa-autonomous-probation-v1";

struct ProbationPlan final {
  int schema_version = 1;
  std::string probation_version_value = std::string(probation_version);
  std::string policy_ref;
  std::string policy_signature;
  std::string promotion_assessment_ref;
  std::string candidate_ref;
  std::string previous_preferred_canonical_ref;
  int window_count = 0;
  int minimum_observations = 0;
  int minimum_uses = 0;
  int maximum_canary_share_bp = 0;
  std::vector<std::string> automatic_demotion_predicates;
  std::string plan_signature;
};

struct ProbationRegressionSignals final {
  bool semantic_contradiction = false;
  bool falsifier_succeeded = false;
  bool corrected_error_recurrence = false;
  bool equivalent_failure_retry_regression = false;
  bool independence_passed = true;
  bool evidence_fresh = true;
  bool projection_integrity_passed = true;
};

struct ProbationObservation final {
  int schema_version = 1;
  std::string probation_version_value = std::string(probation_version);
  std::string plan_signature;
  std::string candidate_ref;
  std::string baseline_ref;
  std::string query_signature;
  std::string context_signature;
  std::string observed_at;
  std::string selection_explanation;
  int window_index = 0;
  bool candidate_selected = false;
  bool candidate_correct = false;
  bool baseline_correct = false;
  bool invariant_passed = false;
  bool benchmark_passed = false;
  bool integrity_passed = false;
  bool source_valid = false;
  bool reproduction_passed = false;
  ProbationRegressionSignals regression_signals;
  std::vector<std::string> evidence_ids;
  std::string observation_signature;
};

struct ProbationAssessment final {
  int schema_version = 1;
  std::string probation_version_value = std::string(probation_version);
  std::string plan_signature;
  std::string candidate_ref;
  std::vector<std::string> observation_signatures;
  int observed_window_count = 0;
  int observation_count = 0;
  int candidate_use_count = 0;
  std::vector<std::string> regression_reasons;
  std::string status;
  bool promotion_ready = false;
  bool demotion_required = false;
  std::string assessment_signature;
};

struct AutomaticPromotionDecision final {
  int schema_version = 1;
  std::string probation_version_value = std::string(probation_version);
  std::string plan_signature;
  std::string assessment_signature;
  std::string candidate_ref;
  std::string canonical_algorithm_ref;
  std::string previous_preferred_canonical_ref;
  std::string status;
  bool human_approval_required = false;
  std::string decision_signature;
};

struct AutomaticDemotionDecision final {
  int schema_version = 1;
  std::string probation_version_value = std::string(probation_version);
  std::string plan_signature;
  std::string assessment_signature;
  std::string candidate_ref;
  std::string demoted_canonical_algorithm_ref;
  std::string restored_canonical_algorithm_ref;
  std::vector<std::string> reasons;
  std::string failure_observation_ref;
  std::string reevaluation_schedule_ref;
  std::string status;
  bool human_approval_required = false;
  std::string decision_signature;
};

[[nodiscard]] ProbationPlan make_probation_plan(
    const AutonomousPromotionAssessment &assessment, std::string policy_ref,
    std::string promotion_assessment_ref,
    std::string probation_candidate_ref,
    std::string previous_preferred_canonical_ref = {});
[[nodiscard]] bool probation_canary_selected(
    const ProbationPlan &plan, std::string_view query_signature);
[[nodiscard]] ProbationObservation make_probation_observation(
    const ProbationPlan &plan, std::string baseline_ref,
    std::string query_signature, std::string context_signature,
    std::string observed_at, int window_index, bool candidate_correct,
    bool baseline_correct,
    bool invariant_passed, bool benchmark_passed, bool integrity_passed,
    bool source_valid, bool reproduction_passed,
    std::vector<std::string> evidence_ids,
    ProbationRegressionSignals regression_signals = {});
[[nodiscard]] ProbationAssessment assess_probation(
    const ProbationPlan &plan,
    std::vector<ProbationObservation> observations);
[[nodiscard]] AutomaticPromotionDecision make_automatic_promotion_decision(
    const ProbationPlan &plan, const ProbationAssessment &assessment,
    std::string canonical_algorithm_ref);
[[nodiscard]] AutomaticDemotionDecision make_automatic_demotion_decision(
    const ProbationPlan &plan, const ProbationAssessment &assessment,
    std::string demoted_canonical_algorithm_ref,
    std::string restored_canonical_algorithm_ref,
    std::string failure_observation_ref,
    std::string reevaluation_schedule_ref);

[[nodiscard]] ProbationPlan probation_plan_from_json(
    const contracts::Json &value);
[[nodiscard]] ProbationObservation probation_observation_from_json(
    const contracts::Json &value);
[[nodiscard]] ProbationAssessment probation_assessment_from_json(
    const contracts::Json &value);
[[nodiscard]] AutomaticPromotionDecision
automatic_promotion_decision_from_json(const contracts::Json &value);
[[nodiscard]] AutomaticDemotionDecision
automatic_demotion_decision_from_json(const contracts::Json &value);

[[nodiscard]] contracts::Json to_json(const ProbationPlan &value);
[[nodiscard]] contracts::Json
to_json(const ProbationRegressionSignals &value);
[[nodiscard]] contracts::Json to_json(const ProbationObservation &value);
[[nodiscard]] contracts::Json to_json(const ProbationAssessment &value);
[[nodiscard]] contracts::Json
to_json(const AutomaticPromotionDecision &value);
[[nodiscard]] contracts::Json
to_json(const AutomaticDemotionDecision &value);

} // namespace statewright::saa
