#include "statewright/egcf/internet_improvement_orchestrator.hpp"

#include "statewright/egcf/internet_feed.hpp"
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
  const auto root =
      std::filesystem::temp_directory_path() /
      ("statewright-orchestrator-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
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
    response.body = bytes(body_text);
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
  std::string body_text = "Identity algorithm; inputs: x; outputs: y; "
                          "procedure: return the input\n";
};

} // namespace

TEST_CASE("internet orchestrator persists a no-work run") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::InternetImprovementOrchestrator orchestrator(store);
  const auto result = orchestrator.run_once(run_request());
  REQUIRE(result.status == "NO_ELIGIBLE_WORK");
  REQUIRE(store.get(result.plan_id).object_type == "internet-improvement-plan");
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
  const auto source_policy_id = internet.register_source_policy(source_policy);
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
  const auto source_policy_id = internet.register_source_policy(source_policy);
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
  static_cast<void>(internet.register_improvement_action_lease(action_lease));
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
  REQUIRE(
      orchestrator.run_status({}, request.worker_id, true).at("runs").empty());
  std::filesystem::remove_all(root);
}

TEST_CASE(
    "internet orchestrator advances one polling window through reasoning") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::InternetImprovementStore internet(store);
  const auto source_policy = sources::canonical_source_policy({});
  const auto source_policy_id = internet.register_source_policy(source_policy);
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
      "2026-09-04T00:00:00Z", "2026-09-04T00:00:01Z", "2026-09-04T00:00:02Z",
      "2026-09-04T00:00:03Z", "2026-09-04T00:00:04Z", "2026-09-04T00:00:05Z"};
  const std::vector<egcf::InternetDirectedActionKind> expected_actions = {
      egcf::InternetDirectedActionKind::schedule_fetch,
      egcf::InternetDirectedActionKind::execute_fetch,
      egcf::InternetDirectedActionKind::assess_source,
      egcf::InternetDirectedActionKind::extract_snapshot,
      egcf::InternetDirectedActionKind::feed_extraction,
      egcf::InternetDirectedActionKind::reason_candidate};

  for (std::size_t index = 0; index < timestamps.size(); ++index) {
    const auto result =
        orchestrator.run_once(run_request_at(timestamps[index]));
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

TEST_CASE("internet orchestrator does not fetch ineligible queued work") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::InternetImprovementStore internet(store);
  const auto policy = sources::canonical_source_policy({});
  sources::InternetWatch watch;
  watch.canonical_url = "https://example.com/guarded";
  watch.source_policy_id = internet.register_source_policy(policy);
  watch.source_group = "example.com";
  watch.accepted_mime_types = policy.accepted_mime_types;
  watch = sources::canonical_watch(std::move(watch));
  static_cast<void>(internet.register_watch(watch));
  const auto job =
      sources::make_fetch_job(watch, "2026-09-04T00:00:00Z",
                              "2026-09-04T00:00:10Z", "2026-09-04T00:00:20Z");
  static_cast<void>(internet.register_fetch_job(job));
  auto request = run_request_at("2026-09-04T00:00:15Z");
  request.policy.enabled_action_kinds = {"EXECUTE_FETCH"};
  SECTION("before start") {
    request.current_timestamp = "2026-09-04T00:00:09Z";
  }
  SECTION("at deadline") { request.current_timestamp = job.deadline; }
  SECTION("insufficient scheduler budget") {
    request.policy.scheduler_limits.global_response_byte_budget = 1U;
  }
  SECTION("disabled successor") {
    watch.supersedes_watch_id = watch.object_id();
    watch.enabled = false;
    ++watch.schedule_generation;
    static_cast<void>(
        internet.register_watch(sources::canonical_watch(std::move(watch))));
  }
  FixtureFetchProvider provider;
  egcf::InternetImprovementOrchestrator orchestrator(store, &provider);
  const auto result = orchestrator.run_once(request);
  REQUIRE(result.status == "NO_ELIGIBLE_WORK");
  REQUIRE(provider.call_count == 0);
  REQUIRE(internet.list("internet-fetch-lease").empty());
  std::filesystem::remove_all(root);
}

TEST_CASE("internet feed resumes every durable prefix of mixed fragments") {
  using namespace statewright;
  const auto original_root = temporary_root();
  egcf::EgcfStore original(original_root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::InternetImprovementStore internet(original);
  const auto policy = sources::canonical_source_policy({});
  sources::InternetWatch watch;
  watch.canonical_url = "https://example.com/recovery";
  watch.source_policy_id = internet.register_source_policy(policy);
  watch.source_group = "example.com";
  watch.accepted_mime_types = policy.accepted_mime_types;
  watch = sources::canonical_watch(std::move(watch));
  static_cast<void>(internet.register_watch(watch));
  FixtureFetchProvider provider;
  provider.body_text =
      "# Reference methods\n"
      "Identity algorithm; inputs: x; outputs: y; procedure: return the input\n"
      "Second identity algorithm; inputs: a; outputs: b; procedure: return the "
      "input\n";
  egcf::InternetImprovementOrchestrator initial(original, &provider);
  for (int index = 0; index != 4; ++index) {
    REQUIRE(initial
                .run_once(run_request_at("2026-09-04T00:00:0" +
                                         std::to_string(index) + "Z"))
                .status == "COMPLETED");
  }
  const auto extraction_record =
      internet.list("internet-extraction-receipt").front();
  sources::InternetExtractionResult extraction;
  extraction.receipt =
      sources::internet_extraction_receipt_from_json(extraction_record.payload);
  for (const auto &id : extraction.receipt.fragment_ids) {
    extraction.fragments.push_back(
        sources::internet_source_fragment_from_json(original.get(id).payload));
  }
  const auto assessment = sources::internet_policy_assessment_from_json(
      internet.list("internet-policy-assessment").front().payload);
  auto feed_request = run_request_at("2026-09-04T00:00:10Z");
  feed_request.policy.enable_candidate_advancement = false;
  feed_request.policy.enabled_action_kinds = {"FEED_EXTRACTION"};
  const auto pending_feed_plan = initial.plan(feed_request);
  REQUIRE(pending_feed_plan.actions.size() == 1U);
  egcf::InternetFeedCoordinator feed(original);
  const auto expected =
      feed.process(assessment, extraction, "internet-source", true);
  REQUIRE(extraction.fragments.size() == 3U);
  REQUIRE(expected.candidates.size() == 2U);
  REQUIRE(expected.retrieval_receipts.front().source_policy_assessment_id ==
          assessment.object_id());
  auto legacy_receipt = expected.retrieval_receipts.front();
  legacy_receipt.source_policy_assessment_id.clear();
  legacy_receipt =
      egcf::canonical_knowledge_search_receipt(std::move(legacy_receipt));
  REQUIRE_FALSE(
      egcf::to_json(legacy_receipt).contains("source_policy_assessment_id"));
  REQUIRE(egcf::internet_knowledge_search_receipt_from_json(
              egcf::to_json(legacy_receipt))
              .object_id() == legacy_receipt.object_id());
  const auto records = original.list();

  // Keep the exact durable prefixes: batch, first retrieval, first candidate,
  // second retrieval and finally the fully completed feed.
  for (int prefix = 0; prefix != 5; ++prefix) {
    INFO("durable prefix " << prefix);
    const auto resumed_root = temporary_root();
    egcf::EgcfStore resumed(resumed_root, STATEWRIGHT_RESOURCE_ROOT);
    std::vector<egcf::EgcfRecord> prefix_records;
    for (const auto &record : records) {
      if (record.object_type.starts_with("internet-improvement-")) {
        continue;
      }
      bool keep = true;
      for (std::size_t index = 0U; index != expected.candidates.size();
           ++index) {
        if (record.object_id ==
            expected.retrieval_receipts[index].object_id()) {
          keep = prefix >= static_cast<int>(index * 2U + 1U);
        }
        if (record.object_id == expected.candidates[index].object_id()) {
          keep = prefix >= static_cast<int>(index * 2U + 2U);
        }
      }
      if (keep) {
        prefix_records.push_back(
            {.object_type = record.object_type, .payload = record.payload});
      }
    }
    static_cast<void>(resumed.register_records(prefix_records));
    egcf::InternetImprovementStore resumed_internet(resumed);
    egcf::InternetImprovementOrchestrator orchestrator(resumed);
    auto request = run_request_at("2026-09-04T00:00:10Z");
    request.policy.enable_candidate_advancement = false;
    request.policy.enabled_action_kinds = {"FEED_EXTRACTION"};
    const auto plan = orchestrator.plan(request);
    if (prefix == 4) {
      REQUIRE(plan.actions.empty());
    } else {
      REQUIRE(plan.actions.size() == 1U);
      REQUIRE(plan.actions.front().kind ==
              egcf::InternetDirectedActionKind::feed_extraction);
    }
    {
      const auto &unfinished_plan = prefix == 4 ? pending_feed_plan : plan;
      const auto plan_id =
          resumed_internet.register_improvement_plan(unfinished_plan);
      egcf::InternetImprovementRun run;
      run.plan_id = plan_id;
      run.worker_id = request.worker_id;
      run.started_at = request.current_timestamp;
      run.requested_budgets = egcf::to_json(request.policy);
      const auto run_id = resumed_internet.register_improvement_run(
          egcf::canonical_internet_improvement_run(std::move(run)));
      egcf::InternetImprovementActionLease lease;
      lease.action_key = unfinished_plan.actions.front().action_key;
      lease.run_id = run_id;
      lease.worker_id = request.worker_id;
      lease.acquired_at = request.current_timestamp;
      lease.expires_at = request.action_lease_expires_at;
      static_cast<void>(resumed_internet.register_improvement_action_lease(
          egcf::canonical_internet_improvement_action_lease(std::move(lease))));
      REQUIRE(orchestrator.resume(run_id, request).status ==
              (prefix == 4 ? "RECONCILED" : "COMPLETED"));
      REQUIRE(orchestrator.plan(request).actions.empty());
    }
    egcf::InternetFeedCoordinator replay(resumed);
    const auto actual =
        replay.process(assessment, extraction, "internet-source", true);
    REQUIRE(egcf::to_json(actual) == egcf::to_json(expected));
    REQUIRE(resumed_internet.list("internet-retrieval-receipt").size() == 2U);
    REQUIRE(resumed_internet.list("internet-algorithm-candidate").size() == 2U);
    REQUIRE(resumed.list("brain-feed-batch").size() == 1U);
    REQUIRE(egcf::internet_feed_completion_outputs(extraction.receipt,
                                                   resumed.list())
                .has_value());
    std::filesystem::remove_all(resumed_root);
  }
  std::filesystem::remove_all(original_root);
}
