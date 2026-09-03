#pragma once

#include "statewright/saa/reasoning_outcome.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view improvement_scheduling_version =
    "saa-improvement-scheduling-v1";

struct ImprovementOpportunity final {
  std::string opportunity_id;
  std::string kind;
  std::string source_signature;
  std::string objective;
  int evidence_value_bp = 0;
  int expected_impact_bp = 0;
  int uncertainty_reduction_bp = 0;
  int cost_bp = 0;
  int risk_bp = 0;
  int priority_bp = 0;
  std::vector<std::string> evidence_ids;
  std::vector<std::string> independence_groups;
  std::vector<std::string> blocked_reasons;
  std::string opportunity_signature;

  [[nodiscard]] bool eligible() const noexcept {
    return blocked_reasons.empty();
  }
};

struct ImprovementSchedulingPolicy final {
  int max_selected = 4;
  int total_cost_budget_bp = 20000;
  int maximum_risk_bp = 6000;
  int minimum_priority_bp = 1000;
};

struct ImprovementScheduleEntry final {
  std::string opportunity_id;
  std::string opportunity_signature;
  int rank = 0;
  int priority_bp = 0;
  int allocated_cost_bp = 0;
};

struct ImprovementSchedule final {
  std::vector<ImprovementScheduleEntry> selected;
  std::vector<std::pair<std::string, std::string>> deferred;
  int total_allocated_cost_bp = 0;
  std::string status;
  std::string schedule_signature;
};

[[nodiscard]] ImprovementSchedulingPolicy
canonical_improvement_scheduling_policy(ImprovementSchedulingPolicy policy);
[[nodiscard]] ImprovementOpportunity make_improvement_opportunity(
    const ReasoningEvidenceResolver &evidence_resolver,
    std::string opportunity_id, std::string kind,
    std::string source_signature, std::string objective,
    int evidence_value_bp, int expected_impact_bp,
    int uncertainty_reduction_bp, int cost_bp, int risk_bp,
    std::vector<std::string> evidence_ids,
    std::vector<std::string> blocked_reasons = {});
[[nodiscard]] ImprovementSchedule schedule_improvements(
    std::vector<ImprovementOpportunity> opportunities,
    ImprovementSchedulingPolicy policy = {});

[[nodiscard]] contracts::Json to_json(const ImprovementOpportunity &value);
[[nodiscard]] contracts::Json
to_json(const ImprovementSchedulingPolicy &value);
[[nodiscard]] contracts::Json to_json(const ImprovementScheduleEntry &value);
[[nodiscard]] contracts::Json to_json(const ImprovementSchedule &value);

} // namespace statewright::saa
