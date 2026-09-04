#include "statewright/egcf/internet_improvement_orchestrator.hpp"

#include "statewright/egcf/internet_improvement_store.hpp"
#include "statewright/sources/policy.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path temporary_root() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("statewright-orchestrator-" +
                     std::to_string(std::chrono::steady_clock::now()
                                        .time_since_epoch()
                                        .count()));
  std::filesystem::create_directories(root);
  return root;
}

statewright::egcf::InternetImprovementRunRequest run_request() {
  statewright::egcf::InternetImprovementRunRequest request;
  request.cycle_key = "2026-09-04T00:00:00Z";
  request.worker_id = "fixture-worker";
  request.current_timestamp = "2026-09-04T00:00:00Z";
  request.action_lease_expires_at = "2026-09-04T00:01:00Z";
  request.fetch_lease_expires_at = "2026-09-04T00:01:00Z";
  request.policy.action_deadline = "2026-09-04T00:05:00Z";
  return request;
}

statewright::egcf::InternetImprovementRunRequest
run_request_at(std::string timestamp) {
  auto request = run_request();
  request.cycle_key = timestamp;
  request.current_timestamp = std::move(timestamp);
  return request;
}

std::vector<std::byte> bytes(std::string_view text) {
  std::vector<std::byte> result;
  result.reserve(text.size());
  for (const char character : text) {
    result.push_back(
        static_cast<std::byte>(static_cast<unsigned char>(character)));
  }
  return result;
}

class FixtureFetchProvider final
    : public statewright::sources::HttpFetchProvider {
public:
  [[nodiscard]] statewright::sources::FetchResponse
  fetch(const statewright::sources::FetchRequest &request) override {
    ++call_count;
    statewright::sources::FetchResponse response;
    response.requested_url = request.url;
    response.final_url = request.url;
    response.resolved_addresses = {"93.184.216.34"};
    response.http_status = 200;
    response.headers["content-type"] = "text/plain; charset=utf-8";
    response.body = bytes(
        "Identity algorithm; inputs: x; outputs: y; procedure: return the input\n");
    response.tls_verified = true;
    response.robots_policy_evaluated = true;
    response.robots_allowed = true;
    response.robots_evidence.push_back(
        {{"allowed", true},
         {"body_sha256", std::string(64U, 'a')},
         {"final_url", "https://example.com/robots.txt"},
         {"http_status", 200},
         {"path", "/automatic-pipeline"},
         {"redirect_chain", statewright::contracts::Json::array()},
         {"requested_url", "https://example.com/robots.txt"},
         {"user_agent", request.policy.user_agent}});
    response.compressed_bytes = response.body.size();
    response.decompressed_bytes = response.body.size();
    response.total_time_milliseconds = 1;
    response.provider_identity = "fixture-provider-v1";
    return response;
  }

  int call_count = 0;
};

} // namespace

TEST_CASE("internet orchestrator persists a no-work run") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::InternetImprovementOrchestrator orchestrator(store);
  const auto result = orchestrator.run_once(run_request());
  REQUIRE(result.status == "NO_ELIGIBLE_WORK");
  REQUIRE(store.get(result.plan_id).object_type ==
          "internet-improvement-plan");
  REQUIRE(store.get(result.run_id).object_type == "internet-improvement-run");
  REQUIRE(orchestrator.run_status(result.run_id).at("run_events").size() == 3U);
  std::filesystem::remove_all(root);
}

TEST_CASE("internet orchestrator executes one scheduled fetch action") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::InternetImprovementStore internet(store);
  const auto source_policy = sources::canonical_source_policy({});
  const auto source_policy_id =
      internet.register_source_policy(source_policy);
  sources::InternetWatch watch;
  watch.canonical_url = "https://example.com/orchestrator";
  watch.source_policy_id = source_policy_id;
  watch.source_group = "example.com";
  watch.accepted_mime_types = source_policy.accepted_mime_types;
  watch = sources::canonical_watch(std::move(watch));
  static_cast<void>(internet.register_watch(watch));

  egcf::InternetImprovementOrchestrator orchestrator(store);
  const auto result = orchestrator.run_once(run_request());
  REQUIRE(result.status == "COMPLETED");
  REQUIRE(result.plan.actions.front().kind ==
          egcf::InternetDirectedActionKind::schedule_fetch);
  REQUIRE(internet.list("internet-fetch-job").size() == 1U);
  REQUIRE(internet.list("internet-improvement-action-receipt").size() == 1U);
  REQUIRE(orchestrator.explain_action(result.action_key).at("eligible") ==
          true);
  std::filesystem::remove_all(root);
}

TEST_CASE("internet orchestrator reconciles a crash after durable scheduling") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::InternetImprovementStore internet(store);
  const auto source_policy = sources::canonical_source_policy({});
  const auto source_policy_id =
      internet.register_source_policy(source_policy);
  sources::InternetWatch watch;
  watch.canonical_url = "https://example.com/reconcile";
  watch.source_policy_id = source_policy_id;
  watch.source_group = "example.com";
  watch.accepted_mime_types = source_policy.accepted_mime_types;
  watch = sources::canonical_watch(std::move(watch));
  static_cast<void>(internet.register_watch(watch));

  egcf::InternetImprovementOrchestrator orchestrator(store);
  const auto request = run_request();
  const auto plan = orchestrator.plan(request);
  const auto plan_id = internet.register_improvement_plan(plan);
  egcf::InternetImprovementRun run;
  run.plan_id = plan_id;
  run.worker_id = request.worker_id;
  run.started_at = request.current_timestamp;
  run.requested_budgets = egcf::to_json(request.policy);
  run = egcf::canonical_internet_improvement_run(std::move(run));
  const auto run_id = internet.register_improvement_run(run);
  egcf::InternetImprovementActionLease action_lease;
  action_lease.action_key = plan.actions.front().action_key;
  action_lease.run_id = run_id;
  action_lease.worker_id = request.worker_id;
  action_lease.acquired_at = request.current_timestamp;
  action_lease.expires_at = request.action_lease_expires_at;
  action_lease = egcf::canonical_internet_improvement_action_lease(
      std::move(action_lease));
  static_cast<void>(
      internet.register_improvement_action_lease(action_lease));
  const auto job = sources::internet_fetch_job_from_json(
      plan.actions.front().parameters.at("job"));
  static_cast<void>(internet.register_fetch_job(job));

  const auto pending_status =
      orchestrator.run_status({}, request.worker_id, true);
  REQUIRE(pending_status.at("runs").size() == 1U);
  REQUIRE(pending_status.at("run_events").empty());
  REQUIRE(pending_status.at("action_leases").size() == 1U);

  auto resumed_request = request;
  resumed_request.current_timestamp = "2026-09-04T00:00:30Z";
  auto wrong_worker_request = resumed_request;
  wrong_worker_request.worker_id = "other-worker";
  REQUIRE_THROWS(orchestrator.resume(run_id, wrong_worker_request));
  const auto result = orchestrator.resume(run_id, resumed_request);
  REQUIRE(result.status == "RECONCILED");
  REQUIRE(result.run_id == run_id);
  REQUIRE(result.output_ids == std::vector<std::string>{job.object_id()});
  const auto receipt = egcf::internet_improvement_action_receipt_from_json(
      store.get(result.action_receipt_id).payload);
  REQUIRE(receipt.disposition == "RECONCILED");
  REQUIRE(orchestrator.run_status({}, request.worker_id, true)
              .at("runs")
              .empty());
  std::filesystem::remove_all(root);
}


TEST_CASE("internet orchestrator advances one polling window through reasoning") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::InternetImprovementStore internet(store);
  const auto source_policy = sources::canonical_source_policy({});
  const auto source_policy_id =
      internet.register_source_policy(source_policy);
  sources::InternetWatch watch;
  watch.canonical_url = "https://example.com/automatic-pipeline";
  watch.source_policy_id = source_policy_id;
  watch.source_group = "example.com";
  watch.accepted_mime_types = source_policy.accepted_mime_types;
  watch = sources::canonical_watch(std::move(watch));
  static_cast<void>(internet.register_watch(watch));

  FixtureFetchProvider provider;
  egcf::InternetImprovementOrchestrator orchestrator(store, &provider);
  const std::vector<std::string> timestamps = {
      "2026-09-04T00:00:00Z", "2026-09-04T00:00:01Z",
      "2026-09-04T00:00:02Z", "2026-09-04T00:00:03Z",
      "2026-09-04T00:00:04Z", "2026-09-04T00:00:05Z"};
  const std::vector<egcf::InternetDirectedActionKind> expected_actions = {
      egcf::InternetDirectedActionKind::schedule_fetch,
      egcf::InternetDirectedActionKind::execute_fetch,
      egcf::InternetDirectedActionKind::assess_source,
      egcf::InternetDirectedActionKind::extract_snapshot,
      egcf::InternetDirectedActionKind::feed_extraction,
      egcf::InternetDirectedActionKind::reason_candidate};

  for (std::size_t index = 0; index < timestamps.size(); ++index) {
    const auto result = orchestrator.run_once(run_request_at(timestamps[index]));
    REQUIRE(result.status == "COMPLETED");
    REQUIRE(result.plan.actions.size() == 1U);
    REQUIRE(result.plan.actions.front().kind == expected_actions[index]);
  }

  REQUIRE(provider.call_count == 1);
  REQUIRE(internet.list("internet-fetch-job").size() == 1U);
  REQUIRE(internet.list("internet-fetch-receipt").size() == 1U);
  REQUIRE(internet.list("internet-source-assessment-input").size() == 1U);
  REQUIRE(internet.list("internet-policy-assessment").size() == 1U);
  REQUIRE(internet.list("internet-extraction-receipt").size() == 1U);
  REQUIRE(internet.list("internet-reasoning-analysis").size() == 1U);

  const auto active_candidates = internet.active_candidate_ids();
  REQUIRE(active_candidates.size() == 1U);
  const auto candidate = egcf::internet_algorithm_candidate_from_json(
      store.get(active_candidates.front()).payload);
  REQUIRE(candidate.status == "VALIDATION_READY");
  REQUIRE(candidate.reasoning_analysis_ids.size() == 1U);
  REQUIRE_FALSE(candidate.oiec_sr_proposal_ids.empty());
  REQUIRE_FALSE(candidate.oiec_sr_falsifier_ids.empty());
  std::filesystem::remove_all(root);
}
