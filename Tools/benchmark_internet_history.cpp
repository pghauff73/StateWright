#include "statewright/contracts/hash.hpp"
#include "statewright/egcf/internet_improvement_director.hpp"
#include "statewright/egcf/internet_improvement_store.hpp"
#include "statewright/sources/policy.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace statewright;
using Json = contracts::Json;

template <typename Record>
void add(egcf::InternetImprovementState &state, std::string type,
         const Record &record) {
  state.internet_records.push_back({.object_id = record.object_id(),
                                    .object_type = std::move(type),
                                    .digest = {},
                                    .payload = sources::to_json(record),
                                    .relative_path = {}});
}

egcf::InternetImprovementState fixture(std::size_t size, bool completed) {
  egcf::InternetImprovementState state;
  state.event_head = "GENESIS";
  state.projection_digest = contracts::sha256_text("history-benchmark-v1");
  state.planned_at = "2026-09-04T00:00:30Z";
  state.cycle_key = "2026-09-04T00:00:00Z";
  sources::InternetSourcePolicy source_policy;
  source_policy.accepted_mime_types = {"text/plain"};
  source_policy.maximum_response_bytes = 4096U;
  source_policy.maximum_decompressed_bytes = 4096U;
  source_policy = sources::canonical_source_policy(source_policy);
  add(state, "internet-source-policy", source_policy);
  const auto body_hash = contracts::sha256_text("synthetic benchmark data");
  for (std::size_t index = 0; index < size; ++index) {
    sources::InternetWatch watch;
    watch.canonical_url =
        "https://example.invalid/history/" + std::to_string(index);
    watch.source_policy_id = source_policy.object_id();
    watch.source_group = "benchmark";
    watch.accepted_mime_types = source_policy.accepted_mime_types;
    watch.maximum_response_bytes = 4096U;
    watch.maximum_decompressed_bytes = 4096U;
    watch = sources::canonical_watch(watch);
    add(state, "internet-watch", watch);
    state.active_watch_ids.push_back(watch.object_id());
    const auto window = sources::polling_window(watch, state.planned_at);
    const auto job =
        sources::make_fetch_job(watch, window.scheduled_interval,
                                window.earliest_start, window.deadline);
    add(state, "internet-fetch-job", job);
    if (!completed)
      continue;

    const auto lease = sources::acquire_fetch_lease(
        job.object_id(), "benchmark", "2026-09-04T00:00:01Z",
        "2026-09-04T00:01:00Z");
    add(state, "internet-fetch-lease", lease);
    add(state, "internet-fetch-lease",
        sources::close_fetch_lease(lease, "COMPLETED"));
    sources::InternetSourceSnapshot snapshot;
    snapshot.canonical_url = watch.canonical_url;
    snapshot.final_url = watch.canonical_url;
    snapshot.body_sha256 = body_hash;
    snapshot.content_type = "text/plain";
    snapshot.body_size = 24U;
    snapshot.artifact_id = "artifact-bytes:sha256:" + body_hash;
    snapshot.source_group = watch.source_group;
    snapshot = sources::canonical_source_snapshot(snapshot);
    add(state, "internet-source-snapshot", snapshot);
    sources::InternetFetchReceipt receipt;
    receipt.job_id = job.object_id();
    receipt.lease_id = lease.object_id();
    receipt.requested_url = watch.canonical_url;
    receipt.final_url = watch.canonical_url;
    receipt.resolved_addresses = {"93.184.216.34"};
    receipt.http_status = 200;
    receipt.tls_verified = true;
    receipt.compressed_bytes = snapshot.body_size;
    receipt.decompressed_bytes = snapshot.body_size;
    receipt.total_time_milliseconds = 1;
    receipt.provider_identity = "synthetic-history-fixture";
    receipt.snapshot_id = snapshot.object_id();
    receipt.status = "FETCH_SUCCEEDED";
    receipt = sources::canonical_fetch_receipt(receipt);
    add(state, "internet-fetch-receipt", receipt);
    sources::InternetPolicyAssessment assessment;
    assessment.snapshot_id = snapshot.object_id();
    assessment.fetch_receipt_id = receipt.object_id();
    assessment.source_policy_id = source_policy.object_id();
    assessment.public_address_valid = true;
    assessment.redirects_valid = true;
    assessment.mime_valid = true;
    assessment.encoding_valid = true;
    assessment.size_valid = true;
    assessment.license_classification = "UNKNOWN";
    assessment.blocking_reasons = {"BENCHMARK_SYNTHETIC_SOURCE_BLOCKED"};
    assessment = sources::canonical_policy_assessment(assessment);
    add(state, "internet-policy-assessment", assessment);
  }
  std::ranges::sort(state.internet_records, {}, &egcf::StoredObject::object_id);
  std::ranges::sort(state.active_watch_ids);
  return state;
}

Json measure(std::size_t size, bool completed) {
  const auto state = fixture(size, completed);
  egcf::InternetDirectorPolicy policy;
  policy.action_deadline = "2026-09-04T00:05:00Z";
  policy.enable_candidate_advancement = false;
  const egcf::InternetImprovementDirector director;
  const auto expected = director.plan(state, policy);
  const auto expected_json = egcf::to_json(expected);
  std::vector<double> samples;
  for (int repetition = 0; repetition < 3; ++repetition) {
    const auto start = std::chrono::steady_clock::now();
    const auto plan = director.plan(state, policy);
    const auto stop = std::chrono::steady_clock::now();
    samples.push_back(
        std::chrono::duration<double, std::milli>(stop - start).count());
    if (egcf::to_json(plan) != expected_json)
      throw std::runtime_error(
          "history benchmark planning was not deterministic");
  }
  std::ranges::sort(samples);
  return {{"history",
           completed ? "completed_fetches_blocked_sources" : "pending_fetches"},
          {"watches", size},
          {"jobs", size},
          {"fetch_receipts", completed ? size : 0U},
          {"records", state.internet_records.size()},
          {"selected_actions", expected.actions.size()},
          {"deferred_actions", expected.deferred_actions.size()},
          {"repetitions", samples.size()},
          {"planning_milliseconds",
           {{"minimum", samples.front()},
            {"median", samples.at(1)},
            {"maximum", samples.back()}}}};
}

Json summary(std::vector<double> samples) {
  std::ranges::sort(samples);
  return {{"minimum", samples.front()},
          {"median", samples.at(1)},
          {"maximum", samples.back()}};
}

// Persist the same synthetic completed-fetch fixture through the schema-checked
// object/event store. Checkpoints extend one store, never modify user history.
Json measure_stored(const std::filesystem::path &workspace,
                    const std::filesystem::path &resources, std::size_t size) {
  const auto generated = fixture(size, true);
  {
    egcf::EgcfStore store(workspace, resources);
    const std::string body = "synthetic benchmark data";
    static_cast<void>(store.register_artifact(
        std::as_bytes(std::span(body.data(), body.size())), "text/plain", {},
        Json::object(), std::string("2026-09-04T00:00:00Z")));
    std::vector<egcf::EgcfRecord> records;
    for (const auto &record : generated.internet_records)
      records.push_back(
          {.object_type = record.object_type, .payload = record.payload});
    static_cast<void>(
        store.register_records(records, "synthetic_benchmark_record"));
    egcf::InternetImprovementStore(store).verify_integrity();
  }
  std::vector<double> open_times, read_times, plan_times, total_times;
  Json expected;
  std::size_t record_count = 0, object_count = 0, event_count = 0;
  const auto milliseconds = [](auto from, auto to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
  };
  for (int repetition = 0; repetition < 4; ++repetition) {
    const auto start = std::chrono::steady_clock::now();
    egcf::EgcfStore store(workspace, resources);
    const auto opened = std::chrono::steady_clock::now();
    const auto state = egcf::InternetImprovementStateReader(store).read(
        generated.planned_at, generated.cycle_key);
    const auto loaded = std::chrono::steady_clock::now();
    egcf::InternetDirectorPolicy policy;
    policy.action_deadline = "2026-09-04T00:05:00Z";
    policy.enable_candidate_advancement = false;
    const auto plan = egcf::InternetImprovementDirector{}.plan(state, policy);
    const auto planned = std::chrono::steady_clock::now();
    const auto json = egcf::to_json(plan);
    if (repetition == 0) {
      expected = json;
      record_count = state.internet_records.size();
      const auto checkpoint = store.projection_checkpoint();
      object_count = checkpoint.object_count;
      event_count = checkpoint.event_count;
    } else {
      if (json != expected)
        throw std::runtime_error(
            "persisted-history planning was not deterministic");
      open_times.push_back(milliseconds(start, opened));
      read_times.push_back(milliseconds(opened, loaded));
      plan_times.push_back(milliseconds(loaded, planned));
      total_times.push_back(milliseconds(start, planned));
    }
  }
  return {{"history", "persisted_completed_fetches_blocked_sources"},
          {"workspace", workspace.string()},
          {"watches", size},
          {"jobs", size},
          {"fetch_receipts", size},
          {"records", record_count},
          {"stored_objects", object_count},
          {"stored_events", event_count},
          {"repetitions", 3},
          {"selected_actions", expected.at("actions").size()},
          {"deferred_actions", expected.at("deferred_actions").size()},
          {"store_open_milliseconds", summary(open_times)},
          {"state_read_milliseconds", summary(read_times)},
          {"planning_milliseconds", summary(plan_times)},
          {"open_read_plan_milliseconds", summary(total_times)}};
}
} // namespace

int main(int argc, char **argv) {
  try {
    std::vector<std::size_t> sizes;
    std::filesystem::path stored_root;
    std::filesystem::path resources = "resources";
    for (int index = 1; index < argc; ++index) {
      const std::string_view argument(argv[index]);
      if (argument == "--stored-root" || argument == "--resources") {
        if (++index >= argc)
          throw std::runtime_error("path option requires a value");
        (argument == "--stored-root" ? stored_root : resources) = argv[index];
        continue;
      }
      std::size_t size = 0;
      const auto parsed = std::from_chars(
          argument.data(), argument.data() + argument.size(), size);
      if (parsed.ec != std::errc{} ||
          parsed.ptr != argument.data() + argument.size() || size < 1U ||
          size > 10'000U)
        throw std::runtime_error(
            "provide watch counts between 1 and 10000 (defaults: 10 100 1000)");
      sizes.push_back(size);
    }
    if (sizes.empty())
      sizes = {10U, 100U, 1000U};
    if (!stored_root.empty()) {
      if (!std::ranges::is_sorted(sizes) ||
          std::adjacent_find(sizes.begin(), sizes.end()) != sizes.end())
        throw std::runtime_error(
            "stored history sizes must be strictly increasing");
      if (!std::filesystem::create_directory(stored_root))
        throw std::runtime_error("stored root must be a new directory; "
                                 "existing evidence is never overwritten");
    }
    Json results = Json::array();
    for (const auto size : sizes) {
      if (stored_root.empty()) {
        results.push_back(measure(size, false));
        results.push_back(measure(size, true));
      } else {
        std::cerr << "Measuring persisted history at " << size << " fetches\n";
        results.push_back(measure_stored(stored_root, resources, size));
      }
    }
    std::cout
        << Json{{"schema_version", 1},
                {"benchmark", "internet-director-history-v1"},
                {"compiler", __VERSION__},
                {"scope",
                 stored_root.empty()
                     ? "In-memory Director planning only; normalized fixture "
                       "construction, disk/projection loading and HTTP are "
                       "excluded. One warmup and three measured repetitions; "
                       "timings are observations, not pass thresholds."
                     : "Persisted synthetic completed-fetch history grown in "
                       "one "
                       "new store. Open, validated state read, and Director "
                       "plan "
                       "measured separately. One warmup and three repetitions "
                       "with "
                       "warm OS caches; fixture creation, store destruction, "
                       "HTTP, "
                       "and serialization excluded. Not a cold-disk "
                       "benchmark."},
                {"results", results}}
               .dump(2)
        << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
