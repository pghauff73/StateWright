#pragma once

#include "statewright/egcf/internet_orchestration_records.hpp"
#include "statewright/egcf/store.hpp"
#include "statewright/saa/improvement_scheduling.hpp"
#include "statewright/sources/scheduler.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace statewright::egcf {

inline constexpr std::string_view internet_improvement_director_version =
    "statewright-internet-improvement-director-v1";

struct InternetDirectorPolicy final {
  int maximum_actions = 1;
  int maximum_provider_calls = 1;
  std::size_t maximum_response_bytes = 32U * 1024U * 1024U;
  std::size_t maximum_cpu_units = 4U;
  int maximum_cost_bp = 20000;
  int maximum_risk_bp = 6000;
  bool require_reasoning = true;
  bool enable_acquisition = true;
  bool enable_candidate_advancement = true;
  std::string candidate_scope_id;
  std::string action_deadline;
  std::string promotion_policy_id;
  std::string probation_query_signature;
  std::vector<std::string> enabled_action_kinds;
  sources::InternetSchedulerLimits scheduler_limits;
  saa::ImprovementSchedulingPolicy improvement_policy;
  std::string policy_signature;
};

struct InternetImprovementState final {
  std::string event_head;
  std::string projection_digest;
  std::string planned_at;
  std::string cycle_key;
  std::vector<StoredObject> internet_records;
  std::vector<std::string> active_watch_ids;
  std::vector<std::string> active_candidate_ids;
  std::vector<std::string> active_protocol_ids;
  std::vector<std::string> active_promotion_policy_ids;
  std::vector<contracts::Json> improvement_opportunities;
};

class InternetImprovementStateReader final {
public:
  explicit InternetImprovementStateReader(EgcfStore &store);

  [[nodiscard]] InternetImprovementState
  read(std::string planned_at, std::string cycle_key);

private:
  EgcfStore &store_;
};

class InternetImprovementDirector final {
public:
  [[nodiscard]] InternetImprovementPlan
  plan(const InternetImprovementState &state,
       const InternetDirectorPolicy &policy) const;
};

[[nodiscard]] InternetDirectorPolicy
canonical_internet_director_policy(InternetDirectorPolicy policy);
[[nodiscard]] InternetDirectorPolicy
internet_director_policy_from_json(const contracts::Json &value);
[[nodiscard]] contracts::Json to_json(const InternetDirectorPolicy &value);
[[nodiscard]] contracts::Json to_json(const InternetImprovementState &value);

} // namespace statewright::egcf
