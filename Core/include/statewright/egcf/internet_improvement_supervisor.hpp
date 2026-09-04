#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace statewright::egcf {

inline constexpr std::string_view internet_improvement_supervisor_version =
    "statewright-internet-improvement-supervisor-v1";

struct InternetSupervisorPolicy final {
  int maximum_cycles_per_wake = 8;
  int maximum_consecutive_failures = 3;
  int maximum_wall_seconds = 300;
  int child_timeout_seconds = 120;
  int cycle_interval_seconds = 300;
  int action_lease_seconds = 180;
  int fetch_lease_seconds = 180;
  int action_deadline_seconds = 120;
  int success_delay_milliseconds = 250;
  int failure_backoff_initial_milliseconds = 5'000;
  int failure_backoff_maximum_milliseconds = 60'000;
  std::size_t maximum_child_output_bytes = 1024U * 1024U;
  contracts::Json request_template = contracts::Json::object();
};

struct InternetSupervisorInvocation final {
  int exit_code = -1;
  bool timed_out = false;
  bool cancelled = false;
  bool output_limit_exceeded = false;
  int termination_signal = 0;
  std::string stdout_text;
  std::string stderr_text;
};

struct InternetSupervisorSummary final {
  bool successful = false;
  int cycles_started = 0;
  int completed_actions = 0;
  int reconciled_actions = 0;
  int stale_actions = 0;
  int failed_actions = 0;
  int invocation_failures = 0;
  int consecutive_failures = 0;
  std::string final_status;
  std::string last_run_id;
  std::string last_action_key;
  std::string diagnostic;
};

using InternetSupervisorInvoker = std::function<InternetSupervisorInvocation(
    const contracts::Json &, std::chrono::milliseconds, std::size_t)>;
using InternetSupervisorEventSink =
    std::function<void(const contracts::Json &)>;
using InternetSupervisorClock =
    std::function<std::chrono::system_clock::time_point()>;
using InternetSupervisorSleeper =
    std::function<void(std::chrono::milliseconds)>;
using InternetSupervisorStopRequested = std::function<bool()>;

class InternetImprovementSupervisor final {
public:
  InternetImprovementSupervisor(
      std::filesystem::path workspace, std::filesystem::path resource_root,
      std::string worker_id, InternetSupervisorInvoker invoker,
      InternetSupervisorEventSink event_sink,
      InternetSupervisorClock clock = {},
      InternetSupervisorSleeper sleeper = {},
      InternetSupervisorStopRequested stop_requested = {});

  [[nodiscard]] InternetSupervisorSummary
  run(const InternetSupervisorPolicy &policy);

private:
  std::filesystem::path workspace_;
  std::filesystem::path resource_root_;
  std::string worker_id_;
  InternetSupervisorInvoker invoker_;
  InternetSupervisorEventSink event_sink_;
  InternetSupervisorClock clock_;
  InternetSupervisorSleeper sleeper_;
  InternetSupervisorStopRequested stop_requested_;
};

[[nodiscard]] InternetSupervisorPolicy
canonical_internet_supervisor_policy(InternetSupervisorPolicy policy);
[[nodiscard]] contracts::Json to_json(const InternetSupervisorPolicy &policy);
[[nodiscard]] contracts::Json
to_json(const InternetSupervisorInvocation &invocation);
[[nodiscard]] contracts::Json to_json(const InternetSupervisorSummary &summary);

} // namespace statewright::egcf
