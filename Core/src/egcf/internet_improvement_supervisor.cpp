#include "statewright/egcf/internet_improvement_supervisor.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace statewright::egcf {
namespace {

using Json = contracts::Json;
using SystemTime = std::chrono::system_clock::time_point;

[[noreturn]] void supervisor_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument,
                      std::move(message));
}

std::string format_utc(SystemTime value) {
  const auto seconds =
      std::chrono::time_point_cast<std::chrono::seconds>(value);
  const std::time_t raw = std::chrono::system_clock::to_time_t(seconds);
  std::tm parts{};
  if (gmtime_r(&raw, &parts) == nullptr) {
    supervisor_error("cannot format supervisor timestamp");
  }
  std::array<char, 21> buffer{};
  if (strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ",
               &parts) != 20U) {
    supervisor_error("cannot format supervisor timestamp");
  }
  return buffer.data();
}

SystemTime parse_utc(std::string_view value) {
  if (value.size() != 20U || value.back() != 'Z') {
    supervisor_error("supervisor status contains a non-canonical timestamp");
  }
  std::tm parts{};
  std::string text(value);
  char *end = strptime(text.c_str(), "%Y-%m-%dT%H:%M:%SZ", &parts);
  if (end == nullptr || *end != '\0') {
    supervisor_error("supervisor status contains an invalid timestamp");
  }
  const std::time_t raw = timegm(&parts);
  const auto result = std::chrono::system_clock::from_time_t(raw);
  if (format_utc(result) != value) {
    supervisor_error("supervisor status timestamp is not canonical UTC");
  }
  return result;
}

SystemTime add_seconds(SystemTime value, int seconds) {
  return value + std::chrono::seconds(seconds);
}

std::string cycle_key(SystemTime now, int interval_seconds) {
  const auto epoch = std::chrono::duration_cast<std::chrono::seconds>(
                         now.time_since_epoch())
                         .count();
  const auto bucket = epoch - (epoch % interval_seconds);
  return format_utc(SystemTime(std::chrono::seconds(bucket)));
}

struct ParsedInvocation final {
  bool successful = false;
  Json result = Json::object();
  std::string diagnostic;
};

ParsedInvocation parse_invocation(const InternetSupervisorInvocation &value) {
  if (value.cancelled) {
    return {.diagnostic = "child invocation cancelled"};
  }
  if (value.timed_out) {
    return {.diagnostic = "child invocation timed out"};
  }
  if (value.output_limit_exceeded) {
    return {.diagnostic = "child invocation exceeded its output limit"};
  }
  if (value.termination_signal != 0) {
    return {.diagnostic =
                "child invocation terminated by signal " +
                std::to_string(value.termination_signal)};
  }
  Json envelope;
  try {
    envelope = contracts::parse_json(value.stdout_text);
  } catch (const std::exception &error) {
    return {.diagnostic =
                "child invocation returned invalid JSON: " +
                std::string(error.what())};
  }
  if (!envelope.is_object() || !envelope.contains("ok") ||
      !envelope.at("ok").is_boolean()) {
    return {.diagnostic = "child invocation returned an invalid envelope"};
  }
  if (value.exit_code != 0 || !envelope.at("ok").get<bool>()) {
    std::string diagnostic = "child invocation failed";
    if (envelope.contains("error") && envelope.at("error").is_object()) {
      diagnostic = envelope.at("error").value("message", diagnostic);
    }
    if (!value.stderr_text.empty()) {
      diagnostic += " (stderr sha256 " +
                    contracts::sha256_text(value.stderr_text) + ")";
    }
    return {.diagnostic = std::move(diagnostic)};
  }
  if (!envelope.contains("result")) {
    return {.diagnostic = "child invocation result is missing"};
  }
  return {.successful = true,
          .result = envelope.at("result"),
          .diagnostic = {}};
}

struct PendingRun final {
  std::string run_id;
  std::string started_at;
  std::optional<std::string> active_lease_expires_at;
};

std::optional<PendingRun> pending_run(const Json &status,
                                      std::string_view worker_id) {
  if (!status.is_object() || !status.contains("runs") ||
      !status.at("runs").is_array() || !status.contains("run_events") ||
      !status.at("run_events").is_array() ||
      !status.contains("action_leases") ||
      !status.at("action_leases").is_array()) {
    supervisor_error("run-status result is incomplete");
  }

  std::set<std::string> terminal_runs;
  for (const auto &stored : status.at("run_events")) {
    const auto &payload = stored.at("payload");
    const auto event_type = payload.at("event_type").get<std::string>();
    if (event_type == "COMPLETED" || event_type == "ABANDONED") {
      terminal_runs.insert(payload.at("run_id").get<std::string>());
    }
  }

  std::set<std::string> resumed_runs;
  std::vector<PendingRun> pending;
  for (const auto &stored : status.at("runs")) {
    const auto &payload = stored.at("payload");
    const auto resumed =
        payload.value("resume_of_run_id", std::string{});
    if (!resumed.empty()) {
      resumed_runs.insert(resumed);
    }
  }
  for (const auto &stored : status.at("runs")) {
    const auto run_id = stored.at("object_id").get<std::string>();
    const auto &payload = stored.at("payload");
    if (payload.at("worker_id").get<std::string>() != worker_id ||
        terminal_runs.contains(run_id) || resumed_runs.contains(run_id)) {
      continue;
    }
    pending.push_back(
        {.run_id = run_id,
         .started_at = payload.at("started_at").get<std::string>(),
         .active_lease_expires_at = std::nullopt});
  }
  std::ranges::sort(pending, [](const PendingRun &left,
                                const PendingRun &right) {
    return std::tie(left.started_at, left.run_id) <
           std::tie(right.started_at, right.run_id);
  });
  if (pending.size() > 1U) {
    supervisor_error("multiple nonterminal runs exist for the supervisor worker");
  }
  if (pending.empty()) {
    return std::nullopt;
  }

  std::set<std::string> predecessors;
  std::vector<std::pair<std::string, Json>> leases;
  for (const auto &stored : status.at("action_leases")) {
    const auto &payload = stored.at("payload");
    if (payload.at("run_id").get<std::string>() != pending.front().run_id) {
      continue;
    }
    const auto predecessor =
        payload.value("predecessor_lease_id", std::string{});
    if (!predecessor.empty()) {
      predecessors.insert(predecessor);
    }
    leases.emplace_back(stored.at("object_id").get<std::string>(), payload);
  }
  std::vector<Json> latest;
  for (const auto &[object_id, payload] : leases) {
    if (!predecessors.contains(object_id)) {
      latest.push_back(payload);
    }
  }
  if (latest.size() > 1U) {
    supervisor_error("pending run has conflicting latest action leases");
  }
  if (latest.size() == 1U &&
      latest.front().at("state").get<std::string>() == "ACTIVE") {
    pending.front().active_lease_expires_at =
        latest.front().at("expires_at").get<std::string>();
  }
  return pending.front();
}

Json base_request(const std::filesystem::path &workspace,
                  const std::filesystem::path &resource_root,
                  const Json &request_template) {
  Json result = request_template;
  result["workspace"] = workspace.string();
  if (!resource_root.empty()) {
    result["resource_root"] = resource_root.string();
  } else {
    result.erase("resource_root");
  }
  return result;
}

Json status_request(const std::filesystem::path &workspace,
                    const std::filesystem::path &resource_root,
                    std::string_view worker_id) {
  Json result = {{"action", "run-status"},
                 {"nonterminal_only", true},
                 {"worker_id", worker_id},
                 {"workspace", workspace.string()}};
  if (!resource_root.empty()) {
    result["resource_root"] = resource_root.string();
  }
  return result;
}

Json action_request(const std::filesystem::path &workspace,
                    const std::filesystem::path &resource_root,
                    std::string_view worker_id,
                    const InternetSupervisorPolicy &policy,
                    SystemTime now, std::string action,
                    std::string run_id = {}) {
  Json result = base_request(workspace, resource_root, policy.request_template);
  result["action"] = std::move(action);
  result["worker_id"] = worker_id;
  result["current_timestamp"] = format_utc(now);
  result["cycle_key"] = cycle_key(now, policy.cycle_interval_seconds);
  result["action_lease_expires_at"] =
      format_utc(add_seconds(now, policy.action_lease_seconds));
  result["fetch_lease_expires_at"] =
      format_utc(add_seconds(now, policy.fetch_lease_seconds));
  Json director = result.value("policy", Json::object());
  if (!director.is_object()) {
    supervisor_error("supervisor request template policy must be an object");
  }
  director["maximum_actions"] = 1;
  director["action_deadline"] =
      format_utc(add_seconds(now, policy.action_deadline_seconds));
  result["policy"] = std::move(director);
  if (!run_id.empty()) {
    result["run_id"] = std::move(run_id);
  } else {
    result.erase("run_id");
  }
  return result;
}

Json event_json(std::string_view event_type, std::string_view worker_id,
                int cycle_index, SystemTime now, Json details) {
  return {{"cycle_index", cycle_index},
          {"details", std::move(details)},
          {"event_type", event_type},
          {"occurred_at", format_utc(now)},
          {"protocol", internet_improvement_supervisor_version},
          {"schema_version", 1},
          {"worker_id", worker_id}};
}

int next_backoff(int current, int maximum) {
  if (current >= maximum) {
    return maximum;
  }
  return std::min(maximum, current > maximum / 2 ? maximum : current * 2);
}

} // namespace

InternetSupervisorPolicy
canonical_internet_supervisor_policy(InternetSupervisorPolicy policy) {
  if (policy.maximum_cycles_per_wake <= 0 ||
      policy.maximum_consecutive_failures <= 0 ||
      policy.maximum_wall_seconds <= 0 || policy.child_timeout_seconds <= 0 ||
      policy.cycle_interval_seconds <= 0 || policy.action_lease_seconds <= 0 ||
      policy.fetch_lease_seconds <= 0 ||
      policy.action_deadline_seconds <= 0 ||
      policy.success_delay_milliseconds < 0 ||
      policy.failure_backoff_initial_milliseconds < 0 ||
      policy.failure_backoff_maximum_milliseconds <
          policy.failure_backoff_initial_milliseconds ||
      policy.maximum_child_output_bytes == 0U ||
      !policy.request_template.is_object()) {
    supervisor_error("internet supervisor policy is invalid");
  }
  if (policy.action_lease_seconds < policy.child_timeout_seconds ||
      policy.fetch_lease_seconds < policy.child_timeout_seconds ||
      policy.action_deadline_seconds > policy.action_lease_seconds) {
    supervisor_error("internet supervisor lease and timeout bounds are invalid");
  }
  constexpr std::size_t maximum_output_bytes = 64U * 1024U * 1024U;
  constexpr std::size_t maximum_request_bytes = 64U * 1024U;
  if (policy.maximum_child_output_bytes > maximum_output_bytes ||
      contracts::canonical_json(policy.request_template).size() >
          maximum_request_bytes) {
    supervisor_error("internet supervisor request or output bound is excessive");
  }
  return policy;
}

InternetImprovementSupervisor::InternetImprovementSupervisor(
    std::filesystem::path workspace, std::filesystem::path resource_root,
    std::string worker_id, InternetSupervisorInvoker invoker,
    InternetSupervisorEventSink event_sink, InternetSupervisorClock clock,
    InternetSupervisorSleeper sleeper,
    InternetSupervisorStopRequested stop_requested)
    : workspace_(std::move(workspace)), resource_root_(std::move(resource_root)),
      worker_id_(std::move(worker_id)), invoker_(std::move(invoker)),
      event_sink_(std::move(event_sink)), clock_(std::move(clock)),
      sleeper_(std::move(sleeper)), stop_requested_(std::move(stop_requested)) {
  if (workspace_.empty() || worker_id_.empty() || !invoker_ || !event_sink_) {
    supervisor_error("internet supervisor configuration is incomplete");
  }
  if (!clock_) {
    clock_ = [] { return std::chrono::system_clock::now(); };
  }
  if (!sleeper_) {
    sleeper_ = [](std::chrono::milliseconds delay) {
      std::this_thread::sleep_for(delay);
    };
  }
  if (!stop_requested_) {
    stop_requested_ = [] { return false; };
  }
}

InternetSupervisorSummary
InternetImprovementSupervisor::run(const InternetSupervisorPolicy &raw_policy) {
  const auto policy = canonical_internet_supervisor_policy(raw_policy);
  InternetSupervisorSummary summary;
  summary.final_status = "STARTED";
  const auto steady_started = std::chrono::steady_clock::now();
  const auto steady_deadline =
      steady_started + std::chrono::seconds(policy.maximum_wall_seconds);
  auto emit = [&](std::string_view event_type, int cycle_index, Json details) {
    event_sink_(event_json(event_type, worker_id_, cycle_index, clock_(),
                           std::move(details)));
  };
  auto finish = [&](std::string final_status, bool successful,
                    std::string diagnostic = {}) {
    summary.final_status = std::move(final_status);
    summary.successful = successful;
    summary.diagnostic = std::move(diagnostic);
    emit("SUPERVISOR_STOPPED", summary.cycles_started,
         {{"summary", to_json(summary)}});
    return summary;
  };
  auto wall_limit_reached = [&] {
    return std::chrono::steady_clock::now() >= steady_deadline;
  };
  auto invoke = [&](const Json &request) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        steady_deadline - std::chrono::steady_clock::now());
    if (remaining <= std::chrono::milliseconds::zero()) {
      return InternetSupervisorInvocation{
          .exit_code = -1,
          .timed_out = true,
          .cancelled = false,
          .output_limit_exceeded = false,
          .termination_signal = 0,
          .stdout_text = {},
          .stderr_text = {}};
    }
    const auto configured_timeout = std::chrono::duration_cast<
        std::chrono::milliseconds>(
        std::chrono::seconds(policy.child_timeout_seconds));
    return invoker_(request, std::min(configured_timeout, remaining),
                    policy.maximum_child_output_bytes);
  };
  auto register_failure = [&](int cycle_index, std::string diagnostic,
                              const InternetSupervisorInvocation *invocation) {
    ++summary.invocation_failures;
    ++summary.consecutive_failures;
    summary.diagnostic = diagnostic;
    Json details = {{"diagnostic", diagnostic}};
    if (invocation != nullptr) {
      details["invocation"] = to_json(*invocation);
    }
    emit("INVOCATION_FAILED", cycle_index, std::move(details));
  };

  emit("SUPERVISOR_STARTED", 0,
       {{"policy", to_json(policy)},
        {"resource_root", resource_root_.string()},
        {"workspace", workspace_.string()}});

  int backoff = policy.failure_backoff_initial_milliseconds;
  while (summary.cycles_started < policy.maximum_cycles_per_wake) {
    if (stop_requested_()) {
      return finish("STOP_REQUESTED", true, summary.diagnostic);
    }
    if (wall_limit_reached()) {
      return finish("MAXIMUM_WALL_TIME",
                    summary.consecutive_failures == 0,
                    summary.consecutive_failures == 0
                        ? std::string{}
                        : summary.diagnostic);
    }

    ++summary.cycles_started;
    const int cycle_index = summary.cycles_started;
    emit("CYCLE_STARTED", cycle_index, Json::object());

    const auto status_invocation =
        invoke(status_request(workspace_, resource_root_, worker_id_));
    const auto parsed_status = parse_invocation(status_invocation);
    if (!parsed_status.successful) {
      register_failure(cycle_index, parsed_status.diagnostic,
                       &status_invocation);
    } else {
      try {
        const auto pending = pending_run(parsed_status.result, worker_id_);
        const auto now = clock_();
        if (wall_limit_reached()) {
          return finish("MAXIMUM_WALL_TIME",
                        summary.consecutive_failures == 0,
                        summary.diagnostic);
        }
        Json request;
        if (pending && pending->active_lease_expires_at &&
            parse_utc(*pending->active_lease_expires_at) > now) {
          summary.last_run_id = pending->run_id;
          emit("RECOVERY_DEFERRED", cycle_index,
               {{"lease_expires_at", *pending->active_lease_expires_at},
                {"run_id", pending->run_id}});
          return finish("DEFERRED_ACTIVE_LEASE", true);
        }
        if (pending) {
          summary.last_run_id = pending->run_id;
          emit("RECOVERY_SELECTED", cycle_index,
               {{"run_id", pending->run_id},
                {"started_at", pending->started_at}});
          request = action_request(workspace_, resource_root_, worker_id_,
                                   policy, now, "resume", pending->run_id);
        } else {
          request = action_request(workspace_, resource_root_, worker_id_,
                                   policy, now, "run-once");
        }

        const auto action_invocation = invoke(request);
        const auto parsed_action = parse_invocation(action_invocation);
        if (!parsed_action.successful || !parsed_action.result.is_object() ||
            !parsed_action.result.contains("status")) {
          const auto diagnostic = parsed_action.successful
                                      ? "orchestrator result status is missing"
                                      : parsed_action.diagnostic;
          register_failure(cycle_index, diagnostic, &action_invocation);
        } else {
          const auto status =
              parsed_action.result.at("status").get<std::string>();
          summary.last_run_id =
              parsed_action.result.value("run_id", std::string{});
          summary.last_action_key =
              parsed_action.result.value("action_key", std::string{});
          summary.diagnostic =
              parsed_action.result.value("diagnostic", std::string{});
          emit("RUN_RESULT", cycle_index,
               {{"action_key", summary.last_action_key},
                {"diagnostic", summary.diagnostic},
                {"run_id", summary.last_run_id},
                {"status", status}});
          if (status == "NO_ELIGIBLE_WORK") {
            summary.consecutive_failures = 0;
            return finish("NO_ELIGIBLE_WORK", true);
          }
          if (status == "COMPLETED") {
            ++summary.completed_actions;
            summary.consecutive_failures = 0;
            backoff = policy.failure_backoff_initial_milliseconds;
          } else if (status == "RECONCILED") {
            ++summary.reconciled_actions;
            summary.consecutive_failures = 0;
            backoff = policy.failure_backoff_initial_milliseconds;
          } else if (status == "STALE") {
            ++summary.stale_actions;
            summary.consecutive_failures = 0;
            backoff = policy.failure_backoff_initial_milliseconds;
          } else if (status == "FAILED") {
            ++summary.failed_actions;
            ++summary.consecutive_failures;
          } else {
            ++summary.consecutive_failures;
            summary.diagnostic = "unknown orchestrator status: " + status;
          }
        }
      } catch (const std::exception &error) {
        register_failure(cycle_index, error.what(), nullptr);
      }
    }

    if (summary.consecutive_failures >=
        policy.maximum_consecutive_failures) {
      if (stop_requested_()) {
        return finish("STOP_REQUESTED", true, summary.diagnostic);
      }
      return finish("CIRCUIT_OPEN", false, summary.diagnostic);
    }
    if (stop_requested_()) {
      return finish("STOP_REQUESTED", true, summary.diagnostic);
    }
    if (summary.cycles_started >= policy.maximum_cycles_per_wake) {
      break;
    }
    const bool failed = summary.consecutive_failures != 0;
    const int delay = failed ? backoff : policy.success_delay_milliseconds;
    if (delay > 0) {
      const auto remaining =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              steady_deadline - std::chrono::steady_clock::now());
      if (remaining <= std::chrono::milliseconds::zero()) {
        return finish("MAXIMUM_WALL_TIME",
                      summary.consecutive_failures == 0,
                      summary.diagnostic);
      }
      const auto bounded_delay =
          std::min(std::chrono::milliseconds(delay), remaining);
      emit(failed ? "FAILURE_BACKOFF" : "SUCCESS_DELAY", cycle_index,
           {{"delay_milliseconds", bounded_delay.count()}});
      sleeper_(bounded_delay);
    }
    if (failed) {
      backoff = next_backoff(backoff,
                             policy.failure_backoff_maximum_milliseconds);
    }
  }

  return finish(summary.consecutive_failures == 0
                    ? "MAXIMUM_CYCLES"
                    : "MAXIMUM_CYCLES_WITH_FAILURE",
                summary.consecutive_failures == 0, summary.diagnostic);
}

Json to_json(const InternetSupervisorPolicy &policy) {
  std::vector<std::string> request_template_keys;
  for (const auto &[key, value] : policy.request_template.items()) {
    static_cast<void>(value);
    request_template_keys.push_back(key);
  }
  return {{"action_deadline_seconds", policy.action_deadline_seconds},
          {"action_lease_seconds", policy.action_lease_seconds},
          {"child_timeout_seconds", policy.child_timeout_seconds},
          {"cycle_interval_seconds", policy.cycle_interval_seconds},
          {"failure_backoff_initial_milliseconds",
           policy.failure_backoff_initial_milliseconds},
          {"failure_backoff_maximum_milliseconds",
           policy.failure_backoff_maximum_milliseconds},
          {"fetch_lease_seconds", policy.fetch_lease_seconds},
          {"maximum_child_output_bytes", policy.maximum_child_output_bytes},
          {"maximum_consecutive_failures",
           policy.maximum_consecutive_failures},
          {"maximum_cycles_per_wake", policy.maximum_cycles_per_wake},
          {"maximum_wall_seconds", policy.maximum_wall_seconds},
          {"request_template_keys", request_template_keys},
          {"request_template_sha256",
           contracts::sha256_json(policy.request_template)},
          {"success_delay_milliseconds",
           policy.success_delay_milliseconds}};
}

Json to_json(const InternetSupervisorInvocation &invocation) {
  return {{"cancelled", invocation.cancelled},
          {"exit_code", invocation.exit_code},
          {"output_limit_exceeded", invocation.output_limit_exceeded},
          {"stderr_sha256", contracts::sha256_text(invocation.stderr_text)},
          {"stdout_sha256", contracts::sha256_text(invocation.stdout_text)},
          {"termination_signal", invocation.termination_signal},
          {"timed_out", invocation.timed_out}};
}

Json to_json(const InternetSupervisorSummary &summary) {
  return {{"completed_actions", summary.completed_actions},
          {"consecutive_failures", summary.consecutive_failures},
          {"cycles_started", summary.cycles_started},
          {"diagnostic", summary.diagnostic},
          {"failed_actions", summary.failed_actions},
          {"final_status", summary.final_status},
          {"invocation_failures", summary.invocation_failures},
          {"last_action_key", summary.last_action_key},
          {"last_run_id", summary.last_run_id},
          {"reconciled_actions", summary.reconciled_actions},
          {"stale_actions", summary.stale_actions},
          {"successful", summary.successful}};
}

} // namespace statewright::egcf
