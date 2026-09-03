#include "statewright/egcf/internet_feed.hpp"

#include "statewright/contracts/hash.hpp"
#include "statewright/saa/algorithm_ir.hpp"
#include "statewright/sources/extraction.hpp"
#include "statewright/sources/policy.hpp"
#include "statewright/sources/scheduler.hpp"
#include "statewright/sources/snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path temporary_root() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("statewright-internet-feed-" +
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

} // namespace

TEST_CASE("internet feed stages supported SAA IR without canonical admission") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::InternetImprovementStore internet(store);

  const auto policy = sources::canonical_source_policy({});
  const std::string policy_id = internet.register_source_policy(policy);
  sources::InternetWatch watch;
  watch.canonical_url = "https://example.com/identity";
  watch.source_policy_id = policy_id;
  watch.source_group = "example.com";
  watch.accepted_mime_types = policy.accepted_mime_types;
  watch = sources::canonical_watch(std::move(watch));
  static_cast<void>(internet.register_watch(watch));
  const auto job = sources::make_fetch_job(
      watch, "2026-09-02T01:00:00Z", "2026-09-02T01:00:00Z",
      "2026-09-02T01:05:00Z");
  static_cast<void>(internet.register_fetch_job(job));
  const auto lease = sources::acquire_fetch_lease(
      job.object_id(), "worker-a", "2026-09-02T01:00:01Z",
      "2026-09-02T01:01:01Z");
  static_cast<void>(internet.register_fetch_lease(lease));

  sources::FetchResponse response;
  response.requested_url = watch.canonical_url;
  response.final_url = watch.canonical_url;
  response.resolved_addresses = {"93.184.216.34"};
  response.http_status = 200;
  response.headers["content-type"] = "text/plain";
  response.body = bytes(
      "Identity algorithm; inputs: x; outputs: y; procedure: return the input\n");
  response.tls_verified = true;
  response.compressed_bytes = response.body.size();
  response.decompressed_bytes = response.body.size();
  response.provider_identity = "fixture-http-provider-v1";
  const auto capture = internet.capture_success(
      job.object_id(), lease.object_id(), response, watch.source_group);
  const auto snapshot = sources::make_source_snapshot(
      response, capture.artifact_bytes_id, watch.source_group);
  const auto fetch_receipt = sources::make_fetch_receipt(
      job.object_id(), lease.object_id(), response, capture.snapshot_id);
  const auto assessment = sources::assess_internet_source(
      snapshot, fetch_receipt, policy,
      std::span<const std::byte>(response.body), true, "CC0-1.0");
  const auto extraction = sources::extract_internet_snapshot(
      capture.snapshot_id, snapshot.content_type,
      std::span<const std::byte>(response.body));

  egcf::InternetFeedCoordinator coordinator(store);
  const auto first = coordinator.process(assessment, extraction, "identity");
  REQUIRE(first.brain_feed_batch.canonical_algorithm_admissions == 0U);
  REQUIRE(first.candidates.size() == 1U);
  auto first_material = egcf::to_json(first);
  first_material.erase("result_signature");
  REQUIRE(first.result_signature == contracts::sha256_json(first_material));
  REQUIRE(first.retrieval_receipts.size() == 1U);
  REQUIRE(first.candidates.front().status == "VALIDATION_READY");
  REQUIRE(first.retrieval_receipts.front().novelty_status ==
          "NOVEL_CANDIDATE");
  REQUIRE(first.retrieval_receipts.front().search_complete);
  REQUIRE_FALSE(first.retrieval_receipts.front().exclusions.empty());
  const auto ir =
      saa::canonicalize_mapping(first.candidates.front().proposed_saa_ir);
  REQUIRE(ir.structural_hash.size() == 64U);
  REQUIRE(store.list("algorithm-definition").empty());

  const auto repeated = coordinator.process(assessment, extraction, "identity");
  REQUIRE(repeated.candidates.size() == 1U);
  REQUIRE(repeated.candidates.front().status == "DUPLICATE");
  REQUIRE(repeated.brain_feed_batch.duplicate_count >= 2U);
  REQUIRE(internet.list("internet-retrieval-receipt").size() == 2U);
  std::filesystem::remove_all(root);
}
