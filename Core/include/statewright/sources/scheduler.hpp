#pragma once

#include "statewright/sources/records.hpp"

#include <cstddef>
#include <chrono>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::sources {

[[nodiscard]] InternetFetchJob make_fetch_job(
    const InternetWatch &watch, std::string scheduled_interval,
    std::string earliest_start, std::string deadline, int retry_number = 0,
    int retry_ceiling = 3);
[[nodiscard]] InternetFetchLease acquire_fetch_lease(
    std::string job_id, std::string worker_id, std::string acquired_at,
    std::string expires_at, std::string predecessor_lease_id = {});
[[nodiscard]] InternetFetchLease close_fetch_lease(
    const InternetFetchLease &lease, std::string terminal_state);
[[nodiscard]] bool latest_lease_is_current(
    const InternetFetchLease &lease, std::string_view current_timestamp);
[[nodiscard]] std::vector<InternetFetchJob>
order_fetch_jobs(std::vector<InternetFetchJob> jobs);

struct InternetSchedulerLimits final {
  std::size_t global_concurrency = 4U;
  std::size_t per_source_group_concurrency = 1U;
  std::size_t global_response_byte_budget = 32U * 1024U * 1024U;
  std::size_t global_cpu_unit_budget = 4U;
  int maximum_clock_jump_seconds = 24 * 60 * 60;
};

struct InternetScheduleExclusion final {
  std::string job_id;
  std::string reason;
};

struct InternetScheduleDiagnostic final {
  std::string status;
  std::string previous_wall_time;
  std::string current_wall_time;
  std::string reason;
};

struct InternetScheduleSelection final {
  std::vector<InternetFetchJob> selected;
  std::vector<InternetScheduleExclusion> excluded;
  std::size_t allocated_response_bytes = 0U;
  std::size_t allocated_cpu_units = 0U;
};

[[nodiscard]] std::vector<InternetFetchJob> schedule_fetch_interval(
    const std::vector<InternetWatch> &watches,
    const std::vector<InternetFetchJob> &existing_jobs,
    std::string scheduled_interval, std::string earliest_start,
    std::string deadline, int retry_ceiling = 3);
[[nodiscard]] std::vector<InternetFetchLease> latest_fetch_leases(
    const std::vector<InternetFetchLease> &leases);
[[nodiscard]] std::vector<InternetFetchLease> recover_expired_fetch_leases(
    const std::vector<InternetFetchLease> &leases,
    std::string_view current_timestamp);
[[nodiscard]] InternetScheduleSelection select_due_fetch_jobs(
    std::vector<InternetFetchJob> jobs,
    const std::vector<InternetFetchLease> &leases,
    std::string_view current_timestamp, const InternetSchedulerLimits &limits);
[[nodiscard]] InternetScheduleDiagnostic assess_scheduler_clock(
    std::string previous_wall_time, std::string current_wall_time,
    const InternetSchedulerLimits &limits);

[[nodiscard]] contracts::Json to_json(const InternetScheduleExclusion &value);
[[nodiscard]] contracts::Json to_json(const InternetScheduleDiagnostic &value);
[[nodiscard]] contracts::Json to_json(const InternetScheduleSelection &value);

} // namespace statewright::sources
