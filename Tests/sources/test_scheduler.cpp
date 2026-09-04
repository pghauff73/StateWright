#include "statewright/common/error.hpp"
#include "statewright/sources/policy.hpp"
#include "statewright/sources/scheduler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <utility>
#include <vector>

namespace {

statewright::sources::InternetWatch watch() {
  using namespace statewright;
  const auto policy = sources::canonical_source_policy({});
  sources::InternetWatch value;
  value.canonical_url = "https://example.com/algorithm";
  value.source_policy_id = policy.object_id();
  value.source_group = "example.com";
  value.accepted_mime_types = policy.accepted_mime_types;
  return sources::canonical_watch(std::move(value));
}

statewright::sources::InternetWatch watch_for(std::string url,
                                              std::string source_group) {
  auto value = watch();
  value.canonical_url = std::move(url);
  value.source_group = std::move(source_group);
  value.watch_signature.clear();
  return statewright::sources::canonical_watch(std::move(value));
}

} // namespace

TEST_CASE("internet scheduler creates idempotent jobs and ordered work") {
  using namespace statewright;
  const auto source_watch = watch();
  const auto later = sources::make_fetch_job(
      source_watch, "2026-09-02T02:00:00Z", "2026-09-02T02:00:00Z",
      "2026-09-02T02:05:00Z");
  const auto earlier = sources::make_fetch_job(
      source_watch, "2026-09-02T01:00:00Z", "2026-09-02T01:00:00Z",
      "2026-09-02T01:05:00Z");
  const auto repeated = sources::make_fetch_job(
      source_watch, "2026-09-02T01:00:00Z", "2026-09-02T01:00:00Z",
      "2026-09-02T01:05:00Z");
  REQUIRE(earlier.object_id() == repeated.object_id());
  const auto ordered = sources::order_fetch_jobs({later, earlier});
  REQUIRE(ordered.front().object_id() == earlier.object_id());
}

TEST_CASE("internet leases are bounded and append-only") {
  using namespace statewright;
  const auto job = sources::make_fetch_job(
      watch(), "2026-09-02T01:00:00Z", "2026-09-02T01:00:00Z",
      "2026-09-02T01:05:00Z");
  const auto lease = sources::acquire_fetch_lease(
      job.object_id(), "worker-a", "2026-09-02T01:00:01Z",
      "2026-09-02T01:01:01Z");
  REQUIRE(sources::latest_lease_is_current(lease,
                                           "2026-09-02T01:00:30Z"));
  REQUIRE_FALSE(sources::latest_lease_is_current(
      lease, "2026-09-02T01:01:01Z"));
  const auto completed = sources::close_fetch_lease(lease, "COMPLETED");
  REQUIRE_FALSE(completed.active());
  REQUIRE(completed.predecessor_lease_id == lease.object_id());
  REQUIRE_THROWS_AS(sources::close_fetch_lease(completed, "ABANDONED"),
                    common::Error);
}

TEST_CASE("internet scheduler resumes without duplicate logical jobs") {
  using namespace statewright;
  const auto first = watch_for("https://one.example/algorithm", "one.example");
  auto disabled = watch_for("https://two.example/algorithm", "two.example");
  disabled.enabled = false;
  disabled.watch_signature.clear();
  disabled = sources::canonical_watch(std::move(disabled));

  const auto initial = sources::schedule_fetch_interval(
      {disabled, first}, {}, "2026-09-02T01:00:00Z",
      "2026-09-02T01:00:00Z", "2026-09-02T01:05:00Z");
  REQUIRE(initial.size() == 1U);
  REQUIRE(initial.front().watch_id == first.object_id());
  REQUIRE(sources::schedule_fetch_interval(
              {first}, initial, "2026-09-02T01:00:00Z",
              "2026-09-02T01:00:00Z", "2026-09-02T01:05:00Z")
              .empty());
}

TEST_CASE("internet scheduler uses stable polling windows with deterministic jitter") {
  using namespace statewright;
  auto source_watch = watch();
  source_watch.polling_interval_seconds = 3600;
  source_watch.deterministic_jitter_seconds = 300;
  source_watch.watch_signature.clear();
  source_watch = sources::canonical_watch(std::move(source_watch));

  const auto before = sources::polling_window(
      source_watch, "2026-09-04T01:04:59Z");
  REQUIRE(before.scheduled_interval == "2026-09-04T00:05:00Z");
  REQUIRE(before.earliest_start == "2026-09-04T00:05:00Z");
  REQUIRE(before.deadline == "2026-09-04T01:05:00Z");

  const auto current = sources::polling_window(
      source_watch, "2026-09-04T01:05:00Z");
  const auto repeated = sources::polling_window(
      source_watch, "2026-09-04T01:59:59Z");
  REQUIRE(current.scheduled_interval == "2026-09-04T01:05:00Z");
  REQUIRE(repeated.scheduled_interval == current.scheduled_interval);
  REQUIRE(repeated.deadline == "2026-09-04T02:05:00Z");

  const auto jobs = sources::schedule_fetch_interval(
      {source_watch}, {}, current.scheduled_interval,
      current.earliest_start, current.deadline);
  REQUIRE(jobs.size() == 1U);
  REQUIRE(sources::schedule_fetch_interval(
              {source_watch}, jobs, repeated.scheduled_interval,
              repeated.earliest_start, repeated.deadline)
              .empty());
}

TEST_CASE("internet scheduler enforces deterministic concurrency budgets") {
  using namespace statewright;
  const auto first = watch_for("https://one.example/a", "one.example");
  const auto second = watch_for("https://one.example/b", "one.example");
  const auto third = watch_for("https://two.example/a", "two.example");
  auto jobs = sources::schedule_fetch_interval(
      {first, second, third}, {}, "2026-09-02T01:00:00Z",
      "2026-09-02T01:00:00Z", "2026-09-02T01:05:00Z");
  for (auto &job : jobs) {
    job.allocated_response_bytes = 64U;
    job.job_signature.clear();
    job = sources::canonical_fetch_job(std::move(job));
  }
  const sources::InternetSchedulerLimits limits{
      .global_concurrency = 2U,
      .per_source_group_concurrency = 1U,
      .global_response_byte_budget = 128U,
      .global_cpu_unit_budget = 2U,
      .maximum_clock_jump_seconds = 3600};
  const auto selection = sources::select_due_fetch_jobs(
      jobs, {}, "2026-09-02T01:00:01Z", limits);
  REQUIRE(selection.selected.size() == 2U);
  REQUIRE(selection.excluded.size() == 1U);
  REQUIRE(selection.allocated_response_bytes == 128U);
  REQUIRE(selection.selected.at(0).source_group !=
          selection.selected.at(1).source_group);
  REQUIRE(selection.excluded.front().reason ==
          "SOURCE_GROUP_CONCURRENCY_BUDGET");
}

TEST_CASE("internet scheduler blocks active leases and recovers expiry") {
  using namespace statewright;
  const auto job = sources::make_fetch_job(
      watch(), "2026-09-02T01:00:00Z", "2026-09-02T01:00:00Z",
      "2026-09-02T01:05:00Z");
  const auto lease = sources::acquire_fetch_lease(
      job.object_id(), "worker-a", "2026-09-02T01:00:01Z",
      "2026-09-02T01:01:01Z");
  const auto blocked = sources::select_due_fetch_jobs(
      {job}, {lease}, "2026-09-02T01:00:30Z", {});
  REQUIRE(blocked.selected.empty());
  REQUIRE(blocked.excluded.front().reason == "LEASE_ACTIVE");

  const auto recovered = sources::recover_expired_fetch_leases(
      {lease}, "2026-09-02T01:01:01Z");
  REQUIRE(recovered.size() == 1U);
  REQUIRE(recovered.front().state == "EXPIRED");
  REQUIRE(recovered.front().predecessor_lease_id == lease.object_id());
}

TEST_CASE("internet scheduler reports wall clock anomalies") {
  using namespace statewright;
  sources::InternetSchedulerLimits limits;
  limits.maximum_clock_jump_seconds = 60;
  REQUIRE(sources::assess_scheduler_clock("2026-09-02T01:00:30Z",
                                          "2026-09-02T01:00:00Z", limits)
              .status == "CLOCK_ROLLBACK");
  REQUIRE(sources::assess_scheduler_clock("2026-09-02T01:00:00Z",
                                          "2026-09-02T01:02:00Z", limits)
              .status == "CLOCK_JUMP");
  REQUIRE(sources::assess_scheduler_clock("2026-09-02T01:00:00Z",
                                          "2026-09-02T01:00:30Z", limits)
              .status == "CLOCK_OK");
}
