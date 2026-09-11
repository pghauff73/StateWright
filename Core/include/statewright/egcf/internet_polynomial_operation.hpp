#pragma once

#include "statewright/common/error.hpp"
#include "statewright/egcf/internet_orchestration_records.hpp"
#include "statewright/egcf/store.hpp"

#include <set>

namespace statewright::egcf {

// This is the logical checkpoint operation time, not the time of a measurement
// or observation. Resolve it from immutable native lease ancestry on each retry.
[[nodiscard]] inline std::string internet_polynomial_operation_time(
    const EgcfStore &store, std::string lease_id, const std::string &action_key,
    std::size_t maximum_links = 32U) {
  const auto reject = [](const char *reason) {
    throw common::Error(common::ErrorCode::invalid_argument, reason);
  };
  if (lease_id.empty() || action_key.empty() || maximum_links == 0U ||
      maximum_links > 128U) {
    reject("POLYNOMIAL_OPERATION_REQUIRES_BOUNDED_LEASE_ANCESTRY");
  }
  std::set<std::string> visited;
  std::string child_acquired_at;
  int child_attempt = 0;
  for (std::size_t links = 0; links < maximum_links; ++links) {
    if (!visited.insert(lease_id).second) {
      reject("POLYNOMIAL_OPERATION_LEASE_CYCLE");
    }
    const auto record = store.get(lease_id);
    if (record.object_type != "internet-improvement-action-lease") {
      reject("POLYNOMIAL_OPERATION_LEASE_TYPE_MISMATCH");
    }
    const auto lease = internet_improvement_action_lease_from_json(record.payload);
    if (lease.action_key != action_key) {
      reject("POLYNOMIAL_OPERATION_LEASE_ACTION_MISMATCH");
    }
    if (!child_acquired_at.empty() &&
        (lease.acquired_at > child_acquired_at ||
         lease.attempt_number > child_attempt)) {
      reject("POLYNOMIAL_OPERATION_LEASE_ORDER_MISMATCH");
    }
    if (lease.predecessor_lease_id.empty()) {
      if (lease.attempt_number != 1) {
        reject("POLYNOMIAL_OPERATION_LEASE_ROOT_MISSING");
      }
      return lease.acquired_at;
    }
    child_acquired_at = lease.acquired_at;
    child_attempt = lease.attempt_number;
    lease_id = lease.predecessor_lease_id;
  }
  reject("POLYNOMIAL_OPERATION_LEASE_ANCESTRY_BUDGET_EXHAUSTED");
  return {}; // The rejection helper always throws.
}

} // namespace statewright::egcf
