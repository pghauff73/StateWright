#include "statewright/egcf/internet_source_coordinator.hpp"

#include "statewright/sources/policy.hpp"
#include "statewright/sources/scheduler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path temporary_root() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("statewright-source-coordinator-" +
                     std::to_string(std::chrono::steady_clock::now()
                                        .time_since_epoch()
                                        .count()));
  std::filesystem::create_directories(root);
  return root;
}

std::vector<std::byte> bytes(std::string_view text) {
  std::vector<std::byte> result;
  for (const char character : text) {
    result.push_back(
        static_cast<std::byte>(static_cast<unsigned char>(character)));
  }
  return result;
}

class FixtureProvider final : public statewright::sources::HttpFetchProvider {
public:
  [[nodiscard]] statewright::sources::FetchResponse
  fetch(const statewright::sources::FetchRequest &request) override {
    statewright::sources::FetchResponse response;
    response.requested_url = request.url;
    response.final_url = request.url;
    response.resolved_addresses = {"93.184.216.34"};
    response.http_status = 200;
    response.headers["content-type"] = "text/plain; charset=utf-8";
    response.body = bytes(
        "Identity algorithm; inputs: x; outputs: y; return the input\n");
    response.tls_verified = true;
    response.compressed_bytes = response.body.size();
    response.decompressed_bytes = response.body.size();
    response.total_time_milliseconds = 1;
    response.provider_identity = "fixture-provider-v1";
    return response;
  }
};

} // namespace

TEST_CASE("internet source coordinator owns fetch assess and extract assembly") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::InternetImprovementStore internet(store);

  const auto policy = sources::canonical_source_policy({});
  const auto policy_id = internet.register_source_policy(policy);
  sources::InternetWatch watch;
  watch.canonical_url = "https://example.com/source-coordinator";
  watch.source_policy_id = policy_id;
  watch.source_group = "example.com";
  watch.accepted_mime_types = policy.accepted_mime_types;
  watch = sources::canonical_watch(std::move(watch));
  static_cast<void>(internet.register_watch(watch));
  const auto job = sources::make_fetch_job(
      watch, "2026-09-04T00:00:00Z", "2026-09-04T00:00:00Z",
      "2026-09-04T00:05:00Z");
  static_cast<void>(internet.register_fetch_job(job));
  const auto lease = sources::acquire_fetch_lease(
      job.object_id(), "fixture-worker", "2026-09-04T00:00:01Z",
      "2026-09-04T00:01:01Z");
  static_cast<void>(internet.register_fetch_lease(lease));

  FixtureProvider provider;
  egcf::InternetSourceCoordinator coordinator(store);
  const auto fetched = coordinator.execute_fetch(
      job.object_id(), lease.object_id(), "2026-09-04T00:00:02Z",
      provider);
  REQUIRE(fetched.status == "FETCH_SUCCEEDED");
  REQUIRE_FALSE(fetched.snapshot_id.empty());

  const auto assessed = coordinator.assess(
      fetched.snapshot_id, fetched.fetch_receipt_id, policy_id, true,
      "CC0-1.0");
  REQUIRE(assessed.assessment.admissible());
  const auto extracted = coordinator.extract(fetched.snapshot_id);
  REQUIRE_FALSE(extracted.extraction.receipt.fragment_ids.empty());
  REQUIRE(store.get(extracted.extraction_receipt_id).object_type ==
          "internet-extraction-receipt");
  std::filesystem::remove_all(root);
}
