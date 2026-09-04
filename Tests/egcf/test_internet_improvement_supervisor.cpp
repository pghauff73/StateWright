#include "statewright/egcf/internet_improvement_supervisor.hpp"

#include "statewright/contracts/canonical_json.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace {

using Json = statewright::contracts::Json;
using Invocation = statewright::egcf::InternetSupervisorInvocation;

Invocation successful(Json result) {
  return {.exit_code = 0,
          .timed_out = false,
          .cancelled = false,
          .output_limit_exceeded = false,
          .termination_signal = 0,
          .stdout_text = statewright::contracts::canonical_json(
              {{"ok", true}, {"result", std::move(result)}}),
          .stderr_text = {}};
}

Json empty_status() {
  return {{"action_leases", Json::array()},
          {"action_receipts", Json::array()},
          {"plans", Json::array()},
          {"run_events", Json::array()},
          {"runs", Json::array()}};
}

statewright::egcf::InternetSupervisorPolicy test_policy() {
  statewright::egcf::InternetSupervisorPolicy policy;
  policy.maximum_cycles_per_wake = 1;
  policy.maximum_wall_seconds = 30;
  policy.child_timeout_seconds = 5;
  policy.action_lease_seconds = 10;
  policy.fetch_lease_seconds = 10;
  policy.action_deadline_seconds = 5;
  policy.success_delay_milliseconds = 0;
  policy.failure_backoff_initial_milliseconds = 0;
  policy.failure_backoff_maximum_milliseconds = 0;
  return policy;
}

auto fixed_clock() {
  return [] {
    return std::chrono::system_clock::from_time_t(1788480060);
  };
}

} // namespace

TEST_CASE("internet supervisor stops when no work is eligible") {
  using namespace statewright;
  std::vector<Json> requests;
  std::vector<Json> events;
  egcf::InternetImprovementSupervisor supervisor(
      "/tmp/statewright-supervisor-no-work", {}, "fixture-worker",
      [&](const Json &request, std::chrono::milliseconds, std::size_t) {
        requests.push_back(request);
        if (request.at("action") == "run-status") {
          return successful(empty_status());
        }
        return successful({{"action_key", ""},
                           {"diagnostic", ""},
                           {"run_id", "run:no-work"},
                           {"status", "NO_ELIGIBLE_WORK"}});
      },
      [&](const Json &event) { events.push_back(event); }, fixed_clock(),
      [](std::chrono::milliseconds) {});

  const auto summary = supervisor.run(test_policy());
  REQUIRE(summary.successful);
  REQUIRE(summary.final_status == "NO_ELIGIBLE_WORK");
  REQUIRE(summary.cycles_started == 1);
  REQUIRE(requests.size() == 2U);
  REQUIRE(requests.at(0).at("action") == "run-status");
  REQUIRE(requests.at(1).at("action") == "run-once");
  REQUIRE(requests.at(1).at("worker_id") == "fixture-worker");
  REQUIRE(events.front().at("event_type") == "SUPERVISOR_STARTED");
  REQUIRE(events.back().at("event_type") == "SUPERVISOR_STOPPED");
}

TEST_CASE("internet supervisor resumes an expired nonterminal run") {
  using namespace statewright;
  std::vector<Json> requests;
  std::vector<Json> events;
  Json status = empty_status();
  status["runs"].push_back(
      {{"object_id", "run:pending"},
       {"payload", {{"resume_of_run_id", ""},
                    {"started_at", "2026-09-04T00:00:00Z"},
                    {"worker_id", "fixture-worker"}}}});
  status["run_events"].push_back(
      {{"object_id", "event:started"},
       {"payload", {{"event_type", "STARTED"},
                    {"run_id", "run:pending"}}}});
  status["action_leases"].push_back(
      {{"object_id", "lease:active"},
       {"payload", {{"expires_at", "2026-09-04T00:00:30Z"},
                    {"predecessor_lease_id", ""},
                    {"run_id", "run:pending"},
                    {"state", "ACTIVE"}}}});

  egcf::InternetImprovementSupervisor supervisor(
      "/tmp/statewright-supervisor-recovery", {}, "fixture-worker",
      [&](const Json &request, std::chrono::milliseconds, std::size_t) {
        requests.push_back(request);
        if (request.at("action") == "run-status") {
          return successful(status);
        }
        return successful({{"action_key", "action:fixture"},
                           {"diagnostic", "recovered"},
                           {"run_id", "run:resumed"},
                           {"status", "RECONCILED"}});
      },
      [&](const Json &event) { events.push_back(event); }, fixed_clock(),
      [](std::chrono::milliseconds) {});

  const auto summary = supervisor.run(test_policy());
  REQUIRE(summary.successful);
  REQUIRE(summary.final_status == "MAXIMUM_CYCLES");
  REQUIRE(summary.reconciled_actions == 1);
  REQUIRE(requests.size() == 2U);
  REQUIRE(requests.at(1).at("action") == "resume");
  REQUIRE(requests.at(1).at("run_id") == "run:pending");
  REQUIRE(events.at(2).at("event_type") == "RECOVERY_SELECTED");
}

TEST_CASE("internet supervisor defers an unexpired active lease") {
  using namespace statewright;
  std::vector<Json> requests;
  Json status = empty_status();
  status["runs"].push_back(
      {{"object_id", "run:pending"},
       {"payload", {{"resume_of_run_id", ""},
                    {"started_at", "2026-09-04T00:00:00Z"},
                    {"worker_id", "fixture-worker"}}}});
  status["action_leases"].push_back(
      {{"object_id", "lease:active"},
       {"payload", {{"expires_at", "2026-09-04T00:10:00Z"},
                    {"predecessor_lease_id", ""},
                    {"run_id", "run:pending"},
                    {"state", "ACTIVE"}}}});

  egcf::InternetImprovementSupervisor supervisor(
      "/tmp/statewright-supervisor-deferred", {}, "fixture-worker",
      [&](const Json &request, std::chrono::milliseconds, std::size_t) {
        requests.push_back(request);
        return successful(status);
      },
      [](const Json &) {}, fixed_clock(),
      [](std::chrono::milliseconds) {});

  const auto summary = supervisor.run(test_policy());
  REQUIRE(summary.successful);
  REQUIRE(summary.final_status == "DEFERRED_ACTIVE_LEASE");
  REQUIRE(summary.last_run_id == "run:pending");
  REQUIRE(requests.size() == 1U);
}

TEST_CASE("internet supervisor opens its circuit after bounded failures") {
  using namespace statewright;
  std::vector<Json> requests;
  auto policy = test_policy();
  policy.maximum_cycles_per_wake = 3;
  policy.maximum_consecutive_failures = 2;
  egcf::InternetImprovementSupervisor supervisor(
      "/tmp/statewright-supervisor-circuit", {}, "fixture-worker",
      [&](const Json &request, std::chrono::milliseconds, std::size_t) {
        requests.push_back(request);
        if (request.at("action") == "run-status") {
          return successful(empty_status());
        }
        return successful({{"action_key", "action:failed"},
                           {"diagnostic", "fixture failure"},
                           {"run_id", "run:failed"},
                           {"status", "FAILED"}});
      },
      [](const Json &) {}, fixed_clock(),
      [](std::chrono::milliseconds) {});

  const auto summary = supervisor.run(policy);
  REQUIRE_FALSE(summary.successful);
  REQUIRE(summary.final_status == "CIRCUIT_OPEN");
  REQUIRE(summary.failed_actions == 2);
  REQUIRE(summary.cycles_started == 2);
  REQUIRE(requests.size() == 4U);
}

TEST_CASE("internet supervisor stop request overrides its cycle limit") {
  using namespace statewright;
  bool stop_requested = false;
  std::vector<Json> events;
  egcf::InternetImprovementSupervisor supervisor(
      "/tmp/statewright-supervisor-stop", {}, "fixture-worker",
      [&](const Json &request, std::chrono::milliseconds, std::size_t) {
        if (request.at("action") == "run-status") {
          return successful(empty_status());
        }
        stop_requested = true;
        return Invocation{.exit_code = -1,
                          .timed_out = false,
                          .cancelled = true,
                          .output_limit_exceeded = false,
                          .termination_signal = 0,
                          .stdout_text = {},
                          .stderr_text = {}};
      },
      [&](const Json &event) { events.push_back(event); }, fixed_clock(),
      [](std::chrono::milliseconds) {},
      [&] { return stop_requested; });

  const auto summary = supervisor.run(test_policy());
  REQUIRE(summary.successful);
  REQUIRE(summary.final_status == "STOP_REQUESTED");
  REQUIRE(summary.invocation_failures == 1);
  REQUIRE(summary.diagnostic == "child invocation cancelled");
  REQUIRE(events.at(events.size() - 2U).at("event_type") ==
          "INVOCATION_FAILED");
  REQUIRE(events.back().at("event_type") == "SUPERVISOR_STOPPED");
}
