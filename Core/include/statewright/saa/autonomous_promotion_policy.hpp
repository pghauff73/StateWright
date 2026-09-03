#pragma once

#include "statewright/saa/knowledge_integrity.hpp"
#include "statewright/saa/oiec_bench_gate.hpp"
#include "statewright/saa/promotion_governance.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view autonomous_promotion_policy_version =
    "saa-autonomous-promotion-policy-v1";

struct AutonomousPromotionPolicy final {
  int schema_version = 1;
  std::string policy_version =
      std::string(autonomous_promotion_policy_version);
  std::vector<std::string> domain_scopes;
  std::vector<std::string> allowed_candidate_classes;
  std::vector<std::string> prohibited_primitives;
  std::vector<std::string> prohibited_capability_classes;
  int minimum_independent_source_groups = 1;
  int minimum_independent_experiment_groups = 2;
  std::string required_semantic_strength = "DETERMINISTIC_SOURCE_BOUND";
  std::string required_mathematical_strength = "EXACT_STRUCTURAL";
  int maximum_unresolved_hard_contradictions = 0;
  int maximum_successful_falsifiers = 0;
  int maximum_source_age_seconds = 86400;
  int probation_window_count = 2;
  int minimum_probation_observations = 4;
  int minimum_probation_uses = 4;
  int maximum_canary_share_bp = 2500;
  std::vector<std::string> automatic_demotion_predicates;
  std::string policy_signature;
};

struct AutonomousPromotionInputs final {
  std::string candidate_ref;
  std::string candidate_status;
  std::string candidate_class;
  std::string candidate_domain;
  std::vector<std::string> candidate_primitives;
  std::vector<std::string> candidate_capability_classes;
  std::string source_policy_assessment_ref;
  std::string snapshot_ref;
  std::string retrieval_receipt_ref;
  std::string experiment_qualification_ref;
  bool source_policy_passed = false;
  bool snapshot_integrity_passed = false;
  int independent_source_groups = 0;
  std::string semantic_strength;
  std::string mathematical_strength;
  bool existing_knowledge_search_complete = false;
  bool experiment_qualified = false;
  int independent_experiment_groups = 0;
  OIECBenchGateAssessment benchmark_gate;
  KnowledgeIntegrityTrajectory integrity_trajectory;
  bool invariants_passed = false;
  int unresolved_hard_contradictions = 0;
  int successful_falsifiers = 0;
  int source_age_seconds = 0;
  bool probation_plan_valid = false;
  bool demotion_path_valid = false;
};

struct AutonomousPromotionAssessment final {
  int schema_version = 1;
  std::string policy_signature;
  std::string candidate_ref;
  std::string source_policy_assessment_ref;
  std::string snapshot_ref;
  std::string retrieval_receipt_ref;
  std::string experiment_qualification_ref;
  std::vector<std::pair<std::string, bool>> gate_results;
  CanonicalPromotionGovernanceAssessment canonical_governance;
  std::vector<std::string> blocking_reasons;
  int source_age_seconds = 0;
  int probation_window_count = 0;
  int minimum_probation_observations = 0;
  int minimum_probation_uses = 0;
  int maximum_canary_share_bp = 0;
  std::vector<std::string> automatic_demotion_predicates;
  bool human_approval_required = false;
  std::string resulting_state;
  bool promotion_allowed = false;
  std::string decision_signature;
};

[[nodiscard]] AutonomousPromotionPolicy
canonical_autonomous_promotion_policy(AutonomousPromotionPolicy policy);
[[nodiscard]] AutonomousPromotionPolicy
autonomous_promotion_policy_from_json(const contracts::Json &value);
[[nodiscard]] AutonomousPromotionAssessment
autonomous_promotion_assessment_from_json(const contracts::Json &value);
[[nodiscard]] AutonomousPromotionAssessment evaluate_autonomous_promotion(
    AutonomousPromotionPolicy policy, AutonomousPromotionInputs inputs);

[[nodiscard]] contracts::Json
to_json(const AutonomousPromotionPolicy &value);
[[nodiscard]] contracts::Json
to_json(const AutonomousPromotionInputs &value);
[[nodiscard]] contracts::Json
to_json(const AutonomousPromotionAssessment &value);

} // namespace statewright::saa
