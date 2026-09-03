#include "statewright/saa/improvement_scheduling.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <ranges>
#include <set>
#include <tuple>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

inline constexpr std::array<std::string_view, 6U> opportunity_kinds = {
    "FAILURE_PATTERN", "BENCHMARK_GAP", "INTEGRITY_SIGNAL",
    "RETRIEVAL_GAP", "EXPERIMENT_TRADEOFF", "SEMANTIC_CONTRADICTION"};

[[noreturn]] void scheduling_error(std::string message) {
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

[[nodiscard]] std::string canonical_text(std::string value) {
  std::string result;
  bool pending_space = false;
  for (const char character : value) {
    if (std::isspace(static_cast<unsigned char>(character)) != 0) {
      pending_space = !result.empty();
      continue;
    }
    if (pending_space) {
      result.push_back(' ');
      pending_space = false;
    }
    result.push_back(character);
  }
  return result;
}

[[nodiscard]] std::string uppercase(std::string value) {
  value = trimmed(std::move(value));
  std::ranges::transform(value, value.begin(), [](const char character) {
    return static_cast<char>(
        std::toupper(static_cast<unsigned char>(character)));
  });
  return value;
}

[[nodiscard]] std::string lowercase(std::string value) {
  value = trimmed(std::move(value));
  std::ranges::transform(value, value.begin(), [](const char character) {
    return static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  });
  return value;
}

[[nodiscard]] int basis_points(int value, std::string_view label) {
  if (value < 0 || value > 10000) {
    scheduling_error(std::string(label) +
                     " must be integer basis points in 0..10000");
  }
  return value;
}

[[nodiscard]] Json deferred_payload(
    const std::vector<std::pair<std::string, std::string>> &deferred) {
  Json result = Json::array();
  for (const auto &[opportunity_id, reason] : deferred) {
    result.push_back(Json::array({opportunity_id, reason}));
  }
  return result;
}

} // namespace

ImprovementSchedulingPolicy canonical_improvement_scheduling_policy(
    ImprovementSchedulingPolicy policy) {
  if (policy.max_selected < 1 || policy.max_selected > 16) {
    scheduling_error("SAA-12.4 max_selected outside bounded range");
  }
  if (policy.total_cost_budget_bp < 0 ||
      policy.total_cost_budget_bp > 160000) {
    scheduling_error("SAA-12.4 total cost budget outside bounded range");
  }
  policy.maximum_risk_bp =
      basis_points(policy.maximum_risk_bp, "maximum scheduling risk");
  policy.minimum_priority_bp =
      basis_points(policy.minimum_priority_bp, "minimum scheduling priority");
  return policy;
}

ImprovementOpportunity make_improvement_opportunity(
    const ReasoningEvidenceResolver &evidence_resolver,
    std::string opportunity_id, std::string kind,
    std::string source_signature, std::string objective,
    int evidence_value_bp, int expected_impact_bp,
    int uncertainty_reduction_bp, int cost_bp, int risk_bp,
    std::vector<std::string> evidence_ids,
    std::vector<std::string> blocked_reasons) {
  opportunity_id = trimmed(std::move(opportunity_id));
  kind = uppercase(std::move(kind));
  source_signature = lowercase(std::move(source_signature));
  objective = canonical_text(std::move(objective));
  if (opportunity_id.empty() || objective.empty()) {
    scheduling_error("SAA-12.4 opportunity id and objective are required");
  }
  if (std::ranges::find(opportunity_kinds, kind) == opportunity_kinds.end()) {
    scheduling_error("unsupported SAA-12.4 opportunity kind: " + kind);
  }
  if (source_signature.size() != 64U ||
      !std::ranges::all_of(source_signature, [](const char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
      })) {
    scheduling_error("SAA-12.4 source_signature must be SHA-256");
  }
  evidence_value_bp = basis_points(evidence_value_bp, "evidence value");
  expected_impact_bp = basis_points(expected_impact_bp, "expected impact");
  uncertainty_reduction_bp =
      basis_points(uncertainty_reduction_bp, "uncertainty reduction");
  cost_bp = basis_points(cost_bp, "cost");
  risk_bp = basis_points(risk_bp, "risk");

  std::set<std::string> canonical_evidence;
  for (auto &evidence_id : evidence_ids) {
    evidence_id = trimmed(std::move(evidence_id));
    if (!evidence_id.empty()) {
      canonical_evidence.insert(std::move(evidence_id));
    }
  }
  std::vector<std::string> grounded;
  std::set<std::string> group_set;
  for (const auto &evidence_id : canonical_evidence) {
    std::optional<ReasoningGroundingEvidence> record;
    try {
      record = evidence_resolver(evidence_id);
    } catch (...) {
      record = std::nullopt;
    }
    if (!record) {
      scheduling_error("SAA-12.4 scheduling evidence is not registered: " +
                       evidence_id);
    }
    if (record->object_type != "egcf-evidence") {
      scheduling_error(
          "SAA-12.4 scheduling evidence must reference EvidenceArtifact");
    }
    if (record->success != true || record->simulated) {
      scheduling_error("SAA-12.4 scheduling evidence must be successful and "
                       "non-simulated");
    }
    if ((!record->producer.starts_with("deterministic-") &&
         !record->producer.starts_with("human-")) ||
        record->method == "reported") {
      scheduling_error("SAA-12.4 scheduling evidence must be "
                       "deterministic/human grounded");
    }
    grounded.push_back(evidence_id);
    if (!record->independence_group.empty()) {
      group_set.insert(record->independence_group);
    }
  }
  if (grounded.empty()) {
    scheduling_error(
        "SAA-12.4 improvement opportunity requires grounded evidence");
  }
  std::set<std::string> blocker_set;
  for (auto &reason : blocked_reasons) {
    reason = canonical_text(std::move(reason));
    if (!reason.empty()) {
      blocker_set.insert(std::move(reason));
    }
  }
  const std::vector<std::string> groups(group_set.begin(), group_set.end());
  const std::vector<std::string> blockers(blocker_set.begin(),
                                           blocker_set.end());
  const int benefit = (evidence_value_bp * expected_impact_bp) / 10000;
  const int burden = (cost_bp + risk_bp) / 2;
  const int priority = std::clamp(
      benefit + uncertainty_reduction_bp / 4 - burden / 2, 0, 10000);
  const Json payload =
      {{"blocked_reasons", blockers},
       {"cost_bp", cost_bp},
       {"evidence_ids", grounded},
       {"evidence_value_bp", evidence_value_bp},
       {"expected_impact_bp", expected_impact_bp},
       {"independence_groups", groups},
       {"kind", kind},
       {"objective", objective},
       {"opportunity_id", opportunity_id},
       {"priority_bp", priority},
       {"risk_bp", risk_bp},
       {"source_signature", source_signature},
       {"uncertainty_reduction_bp", uncertainty_reduction_bp},
       {"version", improvement_scheduling_version}};
  return {.opportunity_id = std::move(opportunity_id),
          .kind = std::move(kind),
          .source_signature = std::move(source_signature),
          .objective = std::move(objective),
          .evidence_value_bp = evidence_value_bp,
          .expected_impact_bp = expected_impact_bp,
          .uncertainty_reduction_bp = uncertainty_reduction_bp,
          .cost_bp = cost_bp,
          .risk_bp = risk_bp,
          .priority_bp = priority,
          .evidence_ids = std::move(grounded),
          .independence_groups = groups,
          .blocked_reasons = blockers,
          .opportunity_signature = contracts::sha256_json(payload)};
}

ImprovementSchedule schedule_improvements(
    std::vector<ImprovementOpportunity> opportunities,
    ImprovementSchedulingPolicy policy) {
  policy = canonical_improvement_scheduling_policy(policy);
  std::set<std::string> signatures;
  for (const auto &item : opportunities) {
    if (!signatures.insert(item.opportunity_signature).second) {
      scheduling_error(
          "SAA-12.4 duplicate opportunity signatures cannot be scheduled "
          "twice");
    }
  }
  std::ranges::sort(opportunities, [](const auto &left, const auto &right) {
    return std::tuple(-left.priority_bp, left.risk_bp, left.cost_bp,
                      left.opportunity_id) <
           std::tuple(-right.priority_bp, right.risk_bp, right.cost_bp,
                      right.opportunity_id);
  });
  std::vector<ImprovementScheduleEntry> selected;
  std::vector<std::pair<std::string, std::string>> deferred;
  int allocated = 0;
  for (const auto &item : opportunities) {
    std::string reason;
    if (!item.eligible()) {
      reason = "BLOCKED:";
      for (std::size_t index = 0; index < item.blocked_reasons.size();
           ++index) {
        if (index != 0U) {
          reason += "; ";
        }
        reason += item.blocked_reasons[index];
      }
    } else if (item.risk_bp > policy.maximum_risk_bp) {
      reason = "RISK_CEILING_EXCEEDED";
    } else if (item.priority_bp < policy.minimum_priority_bp) {
      reason = "PRIORITY_BELOW_THRESHOLD";
    } else if (selected.size() >=
               static_cast<std::size_t>(policy.max_selected)) {
      reason = "SELECTION_COUNT_BUDGET_EXHAUSTED";
    } else if (allocated + item.cost_bp > policy.total_cost_budget_bp) {
      reason = "COST_BUDGET_EXHAUSTED";
    }
    if (!reason.empty()) {
      deferred.emplace_back(item.opportunity_id, std::move(reason));
      continue;
    }
    selected.push_back(
        {.opportunity_id = item.opportunity_id,
         .opportunity_signature = item.opportunity_signature,
         .rank = static_cast<int>(selected.size()) + 1,
         .priority_bp = item.priority_bp,
         .allocated_cost_bp = item.cost_bp});
    allocated += item.cost_bp;
  }
  const std::string status =
      selected.empty() ? "NO_ELIGIBLE_IMPROVEMENT_INVESTIGATION"
                       : "IMPROVEMENT_INVESTIGATIONS_SCHEDULED";
  Json selected_payload = Json::array();
  for (const auto &entry : selected) {
    selected_payload.push_back(to_json(entry));
  }
  const Json payload =
      {{"deferred", deferred_payload(deferred)},
       {"policy", to_json(policy)},
       {"selected", selected_payload},
       {"status", status},
       {"total_allocated_cost_bp", allocated},
       {"version", improvement_scheduling_version}};
  return {.selected = std::move(selected),
          .deferred = std::move(deferred),
          .total_allocated_cost_bp = allocated,
          .status = status,
          .schedule_signature = contracts::sha256_json(payload)};
}

Json to_json(const ImprovementOpportunity &value) {
  return {{"blocked_reasons", value.blocked_reasons},
          {"cost_bp", value.cost_bp},
          {"eligible", value.eligible()},
          {"evidence_ids", value.evidence_ids},
          {"evidence_value_bp", value.evidence_value_bp},
          {"expected_impact_bp", value.expected_impact_bp},
          {"independence_groups", value.independence_groups},
          {"kind", value.kind},
          {"objective", value.objective},
          {"opportunity_id", value.opportunity_id},
          {"opportunity_signature", value.opportunity_signature},
          {"priority_bp", value.priority_bp},
          {"risk_bp", value.risk_bp},
          {"source_signature", value.source_signature},
          {"uncertainty_reduction_bp", value.uncertainty_reduction_bp}};
}

Json to_json(const ImprovementSchedulingPolicy &value) {
  return {{"max_selected", value.max_selected},
          {"maximum_risk_bp", value.maximum_risk_bp},
          {"minimum_priority_bp", value.minimum_priority_bp},
          {"total_cost_budget_bp", value.total_cost_budget_bp}};
}

Json to_json(const ImprovementScheduleEntry &value) {
  return {{"allocated_cost_bp", value.allocated_cost_bp},
          {"opportunity_id", value.opportunity_id},
          {"opportunity_signature", value.opportunity_signature},
          {"priority_bp", value.priority_bp},
          {"rank", value.rank}};
}

Json to_json(const ImprovementSchedule &value) {
  Json deferred = Json::array();
  for (const auto &[opportunity_id, reason] : value.deferred) {
    deferred.push_back(
        Json{{"opportunity_id", opportunity_id}, {"reason", reason}});
  }
  Json selected = Json::array();
  for (const auto &entry : value.selected) {
    selected.push_back(to_json(entry));
  }
  return {{"deferred", deferred},
          {"schedule_signature", value.schedule_signature},
          {"selected", selected},
          {"status", value.status},
          {"total_allocated_cost_bp", value.total_allocated_cost_bp}};
}

} // namespace statewright::saa
