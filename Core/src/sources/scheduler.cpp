#include "statewright/sources/scheduler.hpp"

#include "statewright/common/error.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace statewright::sources {
namespace {

[[noreturn]] void schedule_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

std::chrono::system_clock::time_point parse_utc(std::string_view timestamp) {
  if (timestamp.size() != 20U || timestamp[4] != '-' || timestamp[7] != '-' ||
      timestamp[10] != 'T' || timestamp[13] != ':' || timestamp[16] != ':' ||
      timestamp[19] != 'Z') {
    schedule_error("scheduler timestamp must use canonical UTC form");
  }
  std::tm parts{};
  const std::string value(timestamp);
  if (strptime(value.c_str(), "%Y-%m-%dT%H:%M:%SZ", &parts) == nullptr) {
    schedule_error("scheduler timestamp is invalid");
  }
  const std::time_t seconds = timegm(&parts);
  if (seconds < 0) {
    schedule_error("scheduler timestamp is out of range");
  }
  return std::chrono::system_clock::from_time_t(seconds);
}

void validate_limits(const InternetSchedulerLimits &limits) {
  if (limits.global_concurrency == 0U ||
      limits.per_source_group_concurrency == 0U ||
      limits.global_response_byte_budget == 0U ||
      limits.global_cpu_unit_budget == 0U ||
      limits.maximum_clock_jump_seconds <= 0) {
    schedule_error("internet scheduler limits are invalid");
  }
}

} // namespace

InternetFetchJob make_fetch_job(const InternetWatch &watch,
                                std::string scheduled_interval,
                                std::string earliest_start,
                                std::string deadline, int retry_number,
                                int retry_ceiling) {
  InternetFetchJob job;
  job.watch_id = watch.object_id();
  job.scheduled_interval = std::move(scheduled_interval);
  job.expected_watch_generation = watch.schedule_generation;
  job.earliest_start = std::move(earliest_start);
  job.deadline = std::move(deadline);
  job.retry_number = retry_number;
  job.retry_ceiling = retry_ceiling;
  job.source_group = watch.source_group;
  job.allocated_response_bytes = watch.maximum_response_bytes;
  return canonical_fetch_job(std::move(job));
}

InternetFetchLease acquire_fetch_lease(std::string job_id,
                                       std::string worker_id,
                                       std::string acquired_at,
                                       std::string expires_at,
                                       std::string predecessor_lease_id) {
  InternetFetchLease lease;
  lease.job_id = std::move(job_id);
  lease.worker_id = std::move(worker_id);
  lease.acquired_at = std::move(acquired_at);
  lease.expires_at = std::move(expires_at);
  lease.predecessor_lease_id = std::move(predecessor_lease_id);
  lease.state = "ACTIVE";
  return canonical_fetch_lease(std::move(lease));
}

InternetFetchLease close_fetch_lease(const InternetFetchLease &lease,
                                     std::string terminal_state) {
  if (!lease.active()) {
    schedule_error("only an active lease can be closed");
  }
  InternetFetchLease closed = lease;
  closed.predecessor_lease_id = lease.object_id();
  closed.state = std::move(terminal_state);
  closed.lease_signature.clear();
  return canonical_fetch_lease(std::move(closed));
}

bool latest_lease_is_current(const InternetFetchLease &lease,
                             std::string_view current_timestamp) {
  return lease.active() && current_timestamp >= lease.acquired_at &&
         current_timestamp < lease.expires_at;
}

std::vector<InternetFetchJob>
order_fetch_jobs(std::vector<InternetFetchJob> jobs) {
  std::ranges::sort(jobs, [](const auto &left, const auto &right) {
    return std::tuple(left.earliest_start, left.priority_class,
                      -left.opportunity_score, left.source_group, left.watch_id,
                      left.job_signature) <
           std::tuple(right.earliest_start, right.priority_class,
                      -right.opportunity_score, right.source_group,
                      right.watch_id, right.job_signature);
  });
  return jobs;
}

std::vector<InternetFetchJob> schedule_fetch_interval(
    const std::vector<InternetWatch> &watches,
    const std::vector<InternetFetchJob> &existing_jobs,
    std::string scheduled_interval, std::string earliest_start,
    std::string deadline, int retry_ceiling) {
  std::set<std::string> existing;
  for (const auto &job : existing_jobs) {
    existing.insert(canonical_fetch_job(job).object_id());
  }
  std::vector<InternetFetchJob> created;
  for (const auto &watch_value : watches) {
    const auto watch = canonical_watch(watch_value);
    if (!watch.enabled) {
      continue;
    }
    auto job = make_fetch_job(watch, scheduled_interval, earliest_start,
                              deadline, 0, retry_ceiling);
    if (existing.insert(job.object_id()).second) {
      created.push_back(std::move(job));
    }
  }
  return order_fetch_jobs(std::move(created));
}

std::vector<InternetFetchLease>
latest_fetch_leases(const std::vector<InternetFetchLease> &leases) {
  std::map<std::string, std::vector<InternetFetchLease>> by_job;
  std::set<std::string> predecessors;
  for (const auto &lease_value : leases) {
    auto lease = canonical_fetch_lease(lease_value);
    if (!lease.predecessor_lease_id.empty()) {
      predecessors.insert(lease.predecessor_lease_id);
    }
    by_job[lease.job_id].push_back(std::move(lease));
  }
  std::vector<InternetFetchLease> latest;
  for (const auto &[job_id, candidates] : by_job) {
    std::vector<InternetFetchLease> current;
    for (const auto &candidate : candidates) {
      if (!predecessors.contains(candidate.object_id())) {
        current.push_back(candidate);
      }
    }
    if (current.size() != 1U) {
      schedule_error("fetch lease lineage is conflicting for job: " + job_id);
    }
    latest.push_back(current.front());
  }
  std::ranges::sort(latest, [](const auto &left, const auto &right) {
    return left.job_id < right.job_id;
  });
  return latest;
}

std::vector<InternetFetchLease> recover_expired_fetch_leases(
    const std::vector<InternetFetchLease> &leases,
    std::string_view current_timestamp) {
  static_cast<void>(parse_utc(current_timestamp));
  std::vector<InternetFetchLease> recovered;
  for (const auto &lease : latest_fetch_leases(leases)) {
    if (lease.active() && current_timestamp >= lease.expires_at) {
      recovered.push_back(close_fetch_lease(lease, "EXPIRED"));
    }
  }
  return recovered;
}

InternetScheduleSelection select_due_fetch_jobs(
    std::vector<InternetFetchJob> jobs,
    const std::vector<InternetFetchLease> &leases,
    std::string_view current_timestamp, const InternetSchedulerLimits &limits) {
  validate_limits(limits);
  static_cast<void>(parse_utc(current_timestamp));
  const auto latest = latest_fetch_leases(leases);
  std::map<std::string, InternetFetchLease> latest_by_job;
  for (const auto &lease : latest) {
    latest_by_job.emplace(lease.job_id, lease);
  }
  std::map<std::string, std::size_t> active_by_group;
  std::size_t active_total = 0U;
  for (const auto &job : jobs) {
    const auto found = latest_by_job.find(job.object_id());
    if (found != latest_by_job.end() &&
        latest_lease_is_current(found->second, current_timestamp)) {
      ++active_total;
      ++active_by_group[job.source_group];
    }
  }

  InternetScheduleSelection result;
  jobs = order_fetch_jobs(std::move(jobs));
  for (const auto &job_value : jobs) {
    const auto job = canonical_fetch_job(job_value);
    const auto exclude = [&](std::string reason) {
      result.excluded.push_back(
          {.job_id = job.object_id(), .reason = std::move(reason)});
    };
    if (current_timestamp < job.earliest_start) {
      exclude("NOT_DUE");
      continue;
    }
    if (current_timestamp >= job.deadline) {
      exclude("DEADLINE_EXPIRED");
      continue;
    }
    const auto lease = latest_by_job.find(job.object_id());
    if (lease != latest_by_job.end() &&
        latest_lease_is_current(lease->second, current_timestamp)) {
      exclude("LEASE_ACTIVE");
      continue;
    }
    if (active_total + result.selected.size() >= limits.global_concurrency) {
      exclude("GLOBAL_CONCURRENCY_BUDGET");
      continue;
    }
    if (active_by_group[job.source_group] >=
        limits.per_source_group_concurrency) {
      exclude("SOURCE_GROUP_CONCURRENCY_BUDGET");
      continue;
    }
    if (job.allocated_response_bytes >
        limits.global_response_byte_budget - result.allocated_response_bytes) {
      exclude("GLOBAL_RESPONSE_BYTE_BUDGET");
      continue;
    }
    if (job.allocated_cpu_units >
        limits.global_cpu_unit_budget - result.allocated_cpu_units) {
      exclude("GLOBAL_CPU_BUDGET");
      continue;
    }
    result.allocated_response_bytes += job.allocated_response_bytes;
    result.allocated_cpu_units += job.allocated_cpu_units;
    ++active_by_group[job.source_group];
    result.selected.push_back(job);
  }
  return result;
}

InternetScheduleDiagnostic assess_scheduler_clock(
    std::string previous_wall_time, std::string current_wall_time,
    const InternetSchedulerLimits &limits) {
  validate_limits(limits);
  const auto previous = parse_utc(previous_wall_time);
  const auto current = parse_utc(current_wall_time);
  InternetScheduleDiagnostic diagnostic;
  diagnostic.previous_wall_time = std::move(previous_wall_time);
  diagnostic.current_wall_time = std::move(current_wall_time);
  const auto difference =
      std::chrono::duration_cast<std::chrono::seconds>(current - previous).count();
  if (difference < 0) {
    diagnostic.status = "CLOCK_ROLLBACK";
    diagnostic.reason = "wall clock moved backwards";
  } else if (difference > limits.maximum_clock_jump_seconds) {
    diagnostic.status = "CLOCK_JUMP";
    diagnostic.reason = "wall clock exceeded maximum scheduler jump";
  } else {
    diagnostic.status = "CLOCK_OK";
    diagnostic.reason = "wall clock progression is within policy";
  }
  return diagnostic;
}

contracts::Json to_json(const InternetScheduleExclusion &value) {
  return {{"job_id", value.job_id}, {"reason", value.reason}};
}

contracts::Json to_json(const InternetScheduleDiagnostic &value) {
  return {{"current_wall_time", value.current_wall_time},
          {"previous_wall_time", value.previous_wall_time},
          {"reason", value.reason},
          {"status", value.status}};
}

contracts::Json to_json(const InternetScheduleSelection &value) {
  contracts::Json selected = contracts::Json::array();
  for (const auto &job : value.selected) {
    selected.push_back(to_json(job));
  }
  contracts::Json excluded = contracts::Json::array();
  for (const auto &entry : value.excluded) {
    excluded.push_back(to_json(entry));
  }
  return {{"allocated_cpu_units", value.allocated_cpu_units},
          {"allocated_response_bytes", value.allocated_response_bytes},
          {"excluded", std::move(excluded)},
          {"selected", std::move(selected)}};
}

} // namespace statewright::sources
