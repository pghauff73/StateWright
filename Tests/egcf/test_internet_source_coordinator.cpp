#include "statewright/egcf/internet_source_coordinator.hpp"

#include "statewright/sources/policy.hpp"
#include "statewright/sources/scheduler.hpp"
#include "statewright/sources/watchlist.hpp"

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
    response.robots_policy_evaluated = true;
    response.robots_allowed = true;
    response.robots_evidence.push_back(
        {{"allowed", true},
         {"body_sha256", std::string(64U, 'a')},
         {"final_url", "https://example.com/robots.txt"},
         {"http_status", 200},
         {"path", "/source-coordinator"},
         {"redirect_chain", statewright::contracts::Json::array()},
         {"requested_url", "https://example.com/robots.txt"},
         {"user_agent", request.policy.user_agent}});
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
  REQUIRE_FALSE(fetched.source_assessment_input_id.empty());
  const auto automatic_input =
      store.get(fetched.source_assessment_input_id);
  REQUIRE(automatic_input.object_type ==
          "internet-source-assessment-input");
  REQUIRE(automatic_input.payload.at("robots_allowed") == true);
  REQUIRE(automatic_input.payload.at("license_classification") == "UNKNOWN");
  const auto receipt = store.get(fetched.fetch_receipt_id);
  REQUIRE(receipt.payload.at("robots_policy_evaluated") == true);
  REQUIRE(receipt.payload.at("robots_allowed") == true);

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

TEST_CASE("internet source coordinator uses verified watchlist license evidence") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::InternetImprovementStore internet(store);

  const contracts::Json manifest = {
      {"schema_version", 1},
      {"watchlist_version", "fixture-v1"},
      {"description", "fixture"},
      {"source_policy_ref", "fixture"},
      {"registration_defaults",
       {{"enabled", false}, {"deterministic_jitter_seconds", 0}}},
      {"watches", contracts::Json::array()}};
  const contracts::Json entry = {
      {"accepted_mime_types", {"text/plain"}},
      {"canonical_url", "https://example.com/source-coordinator-license"},
      {"controlling_publisher", "Fixture Publisher"},
      {"deterministic_jitter_seconds", 0},
      {"enabled", false},
      {"extraction_strategy", "plain-text"},
      {"license",
       {{"classification", "FIXTURE_VERIFIED_LICENSE"},
        {"evidence_urls", {"https://example.com/license"}},
        {"status", "verified"}}},
      {"name", "fixture-verified-license"},
      {"polling_interval_seconds", 3600},
      {"purpose", "authoritative-evidence"},
      {"robots", {{"declared_status", "pending"}, {"required", true}}},
      {"source_group", "fixture"},
      {"stability", "stable-section"},
      {"subject", "fixture"},
      {"tier", 1}};

  auto policy = sources::InternetSourcePolicy{};
  policy.accepted_mime_types = {"text/plain"};
  policy = sources::watchlist_source_policy(entry, std::move(policy));
  const auto policy_id = internet.register_source_policy(policy);
  const auto watch =
      sources::watchlist_watch(entry, policy_id, true, false);
  const auto watch_id = internet.register_watch(watch);
  const auto registration = sources::make_watchlist_registration(
      manifest, entry, watch_id, policy_id, std::string(64U, 'b'),
      "REGISTERED_DISABLED");
  const auto registration_id = store.register_record(
      {.object_type = "internet-watch-registration",
       .payload = registration},
      "internet_watch_registration_registered");

  const auto job = sources::make_fetch_job(
      watch, "2026-09-04T02:00:00Z", "2026-09-04T02:00:00Z",
      "2026-09-04T02:05:00Z");
  static_cast<void>(internet.register_fetch_job(job));
  const auto lease = sources::acquire_fetch_lease(
      job.object_id(), "fixture-worker", "2026-09-04T02:00:01Z",
      "2026-09-04T02:01:01Z");
  static_cast<void>(internet.register_fetch_lease(lease));

  FixtureProvider provider;
  egcf::InternetSourceCoordinator coordinator(store);
  const auto fetched = coordinator.execute_fetch(
      job.object_id(), lease.object_id(), "2026-09-04T02:00:02Z",
      provider);
  REQUIRE_FALSE(fetched.source_assessment_input_id.empty());
  const auto input = store.get(fetched.source_assessment_input_id);
  REQUIRE(input.payload.at("license_classification") ==
          "FIXTURE_VERIFIED_LICENSE");
  REQUIRE(std::find(input.payload.at("evidence_ids").begin(),
                    input.payload.at("evidence_ids").end(),
                    registration_id) !=
          input.payload.at("evidence_ids").end());
  REQUIRE(input.payload.at("provenance")
              .at("license")
              .at("registration_ids") ==
          contracts::Json::array({registration_id}));

  std::filesystem::remove_all(root);
}
