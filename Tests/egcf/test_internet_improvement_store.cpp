#include "statewright/egcf/internet_improvement_store.hpp"
#include "statewright/egcf/internet_feed.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/core/file_io.hpp"
#include "statewright/sources/policy.hpp"
#include "statewright/sources/scheduler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

std::filesystem::path temporary_root() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("statewright-internet-store-" +
                     std::to_string(std::chrono::steady_clock::now()
                                        .time_since_epoch()
                                        .count()));
  std::filesystem::create_directories(root);
  return root;
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

statewright::sources::FetchResponse response_for(std::string url) {
  statewright::sources::FetchResponse response;
  response.requested_url = std::move(url);
  response.final_url = response.requested_url;
  response.resolved_addresses = {"93.184.216.34"};
  response.http_status = 200;
  response.headers["content-type"] = "text/plain; charset=utf-8";
  response.headers["etag"] = "\"fixture-v1\"";
  response.body = bytes("algorithm identity(x) = x\nbenchmark exact = true\n");
  response.tls_verified = true;
  response.compressed_bytes = response.body.size();
  response.decompressed_bytes = response.body.size();
  response.total_time_milliseconds = 5;
  response.provider_identity = "fixture-http-provider-v1";
  return response;
}

struct RegisteredFixture final {
  statewright::sources::InternetSourcePolicy policy;
  statewright::sources::InternetWatch watch;
  statewright::sources::InternetFetchJob job;
  statewright::sources::InternetFetchLease lease;
};

RegisteredFixture register_fixture(
    statewright::egcf::InternetImprovementStore &internet) {
  using namespace statewright;
  RegisteredFixture fixture;
  fixture.policy = sources::canonical_source_policy({});
  const auto policy_id = internet.register_source_policy(fixture.policy);
  REQUIRE(policy_id == fixture.policy.object_id());

  fixture.watch.canonical_url = "https://example.com/algorithm";
  fixture.watch.source_policy_id = policy_id;
  fixture.watch.source_group = "example.com";
  fixture.watch.accepted_mime_types = fixture.policy.accepted_mime_types;
  fixture.watch = sources::canonical_watch(std::move(fixture.watch));
  REQUIRE(internet.register_watch(fixture.watch) == fixture.watch.object_id());

  fixture.job = sources::make_fetch_job(
      fixture.watch, "2026-09-03T01:00:00Z", "2026-09-03T01:00:00Z",
      "2026-09-03T01:05:00Z");
  REQUIRE(internet.register_fetch_job(fixture.job) == fixture.job.object_id());

  fixture.lease = sources::acquire_fetch_lease(
      fixture.job.object_id(), "worker-a", "2026-09-03T01:00:01Z",
      "2026-09-03T01:01:01Z");
  REQUIRE(internet.register_fetch_lease(fixture.lease) ==
          fixture.lease.object_id());
  return fixture;
}

} // namespace

TEST_CASE("internet improvement store captures immutable reusable snapshots") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::InternetImprovementStore internet(store);
  const auto fixture = register_fixture(internet);
  const auto response = response_for(fixture.watch.canonical_url);

  const auto first = internet.capture_success(
      fixture.job.object_id(), fixture.lease.object_id(), response,
      fixture.watch.source_group);
  const auto repeated = internet.capture_success(
      fixture.job.object_id(), fixture.lease.object_id(), response,
      fixture.watch.source_group);
  REQUIRE(first.artifact_bytes_id == repeated.artifact_bytes_id);
  REQUIRE(first.snapshot_id == repeated.snapshot_id);
  REQUIRE(first.fetch_receipt_id == repeated.fetch_receipt_id);
  REQUIRE(internet.snapshot_bytes(first.snapshot_id) == response.body);

  auto not_modified = response;
  not_modified.http_status = 304;
  not_modified.body.clear();
  not_modified.compressed_bytes = 0U;
  not_modified.decompressed_bytes = 0U;
  const auto not_modified_receipt = internet.capture_not_modified(
      fixture.job.object_id(), fixture.lease.object_id(), not_modified,
      first.snapshot_id);
  REQUIRE(not_modified_receipt.starts_with("internet-fetch-receipt:sha256:"));

  sources::InternetPolicyAssessment assessment;
  assessment.snapshot_id = first.snapshot_id;
  assessment.fetch_receipt_id = first.fetch_receipt_id;
  assessment.source_policy_id = fixture.policy.object_id();
  assessment.public_address_valid = true;
  assessment.redirects_valid = true;
  assessment.robots_allowed = true;
  assessment.license_classification = "fixture-permitted";
  assessment.mime_valid = true;
  assessment.encoding_valid = true;
  assessment.credential_free = true;
  assessment.size_valid = true;
  const auto assessment_id = internet.register_policy_assessment(assessment);
  REQUIRE(assessment_id.starts_with("internet-policy-assessment:sha256:"));

  const auto extraction = sources::extract_internet_snapshot(
      first.snapshot_id, "text/plain",
      std::span<const std::byte>(response.body));
  const auto extraction_id = internet.register_extraction(extraction);
  REQUIRE(extraction_id.starts_with("internet-extraction-receipt:sha256:"));

  const auto completed = sources::close_fetch_lease(fixture.lease, "COMPLETED");
  REQUIRE(internet.register_fetch_lease(completed) == completed.object_id());
  REQUIRE_THROWS_AS(
      internet.capture_failure(fixture.job.object_id(), fixture.lease.object_id(),
                               fixture.watch.canonical_url,
                               "fixture-http-provider-v1", "stale completion"),
      common::Error);

  internet.verify_integrity();
  std::filesystem::remove(store.projection_path());
  internet.rebuild_projection();
  internet.verify_integrity();
  REQUIRE(internet.list("internet-source-snapshot").size() == 1U);
  std::filesystem::remove_all(root);
}

TEST_CASE("internet improvement store persists watches and rejects stale jobs") {
  using namespace statewright;
  const auto root = temporary_root();
  std::string watch_id;
  {
    egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
    egcf::InternetImprovementStore internet(store);
    const auto fixture = register_fixture(internet);
    watch_id = fixture.watch.object_id();

    auto replacement = fixture.watch;
    replacement.supersedes_watch_id = watch_id;
    replacement.schedule_generation = 2;
    replacement.polling_interval_seconds = 7200;
    replacement.watch_signature.clear();
    replacement = sources::canonical_watch(std::move(replacement));
    REQUIRE(internet.register_watch(replacement) == replacement.object_id());
    REQUIRE(internet.active_watch_ids() ==
            std::vector<std::string>{replacement.object_id()});

    auto stale = sources::make_fetch_job(
        replacement, "2026-09-03T02:00:00Z", "2026-09-03T02:00:00Z",
        "2026-09-03T02:05:00Z");
    stale.expected_watch_generation = 1;
    stale.job_signature.clear();
    REQUIRE_THROWS_AS(internet.register_fetch_job(stale), common::Error);
  }
  {
    egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
    egcf::InternetImprovementStore internet(store);
    REQUIRE(internet.list("internet-watch").size() == 2U);
    REQUIRE(internet.active_watch_ids().size() == 1U);
    internet.verify_integrity();
  }
  std::filesystem::remove_all(root);
}

TEST_CASE("internet improvement store detects tampered snapshot bytes") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::InternetImprovementStore internet(store);
  const auto fixture = register_fixture(internet);
  const auto capture = internet.capture_success(
      fixture.job.object_id(), fixture.lease.object_id(),
      response_for(fixture.watch.canonical_url), fixture.watch.source_group);
  const std::string digest = capture.artifact_bytes_id.substr(
      std::string("artifact-bytes:sha256:").size());
  const auto path = store.artifacts().root() / digest.substr(0, 2U) / digest;
  core::atomic_write_bytes(path, bytes("tampered"));
  REQUIRE_THROWS_AS(internet.verify_integrity(), common::Error);
  std::filesystem::remove_all(root);
}

TEST_CASE("internet candidate lineage migration preserves legacy records") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::InternetImprovementStore internet(store);
  const auto fixture = register_fixture(internet);
  const auto response = response_for(fixture.watch.canonical_url);
  const auto capture = internet.capture_success(
      fixture.job.object_id(), fixture.lease.object_id(), response,
      fixture.watch.source_group);
  const auto snapshot = sources::make_source_snapshot(
      response, capture.artifact_bytes_id, fixture.watch.source_group);
  const auto receipt = sources::make_fetch_receipt(
      fixture.job.object_id(), fixture.lease.object_id(), response,
      capture.snapshot_id);
  const auto assessment = sources::assess_internet_source(
      snapshot, receipt, fixture.policy,
      std::span<const std::byte>(response.body), true, "CC0-1.0");
  const auto extraction = sources::extract_internet_snapshot(
      capture.snapshot_id, snapshot.content_type,
      std::span<const std::byte>(response.body));
  egcf::InternetFeedCoordinator feed(store);
  const auto staged = feed.process(assessment, extraction, "legacy fixture");
  REQUIRE(staged.candidates.size() == 1U);
  const auto current = staged.candidates.front();

  auto legacy = egcf::to_json(current);
  for (const auto *field : {"probation_admission_ids",
                            "probation_observation_ids",
                            "promotion_decision_ids",
                            "demotion_decision_ids",
                            "canonical_algorithm_ids"}) {
    legacy.erase(field);
  }
  legacy.erase("candidate_signature");
  legacy["candidate_signature"] = contracts::sha256_json(legacy);
  const std::string legacy_id = store.register_record(
      {.object_type = "internet-algorithm-candidate", .payload = legacy},
      "legacy_internet_candidate_fixture_registered");
  REQUIRE(legacy_id != current.object_id());

  const std::string migrated_id =
      internet.migrate_algorithm_candidate(legacy_id);
  REQUIRE(migrated_id == current.object_id());
  REQUIRE(store.get(legacy_id).payload == legacy);
  REQUIRE(store.active_ids("internet-algorithm-candidate") ==
          std::vector<std::string>{current.object_id()});
  internet.verify_integrity();
  std::filesystem::remove_all(root);
}
