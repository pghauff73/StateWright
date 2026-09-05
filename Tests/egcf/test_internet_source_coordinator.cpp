#include "statewright/egcf/autonomous_promotion.hpp"
#include "statewright/egcf/internet_source_coordinator.hpp"

#include "statewright/contracts/hash.hpp"
#include "statewright/core/file_io.hpp"
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
  const auto root =
      std::filesystem::temp_directory_path() /
      ("statewright-source-coordinator-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
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
  std::vector<statewright::sources::FetchRequest> requests;
  bool conditional = false;
  std::string content_type = "text/plain; charset=utf-8";
  std::string body_text = "Identity algorithm; inputs: x; outputs: y; "
                          "procedure: return the input\n";
  [[nodiscard]] statewright::sources::FetchResponse
  fetch(const statewright::sources::FetchRequest &request) override {
    requests.push_back(request);
    statewright::sources::FetchResponse response;
    response.requested_url = request.url;
    response.final_url = request.url;
    response.resolved_addresses = {"93.184.216.34"};
    response.http_status = 200;
    response.headers["content-type"] = content_type;
    response.body = bytes(body_text);
    if (conditional) {
      response.headers["etag"] = "\"fixture-v1\"";
      response.headers["last-modified"] = "Fri, 04 Sep 2026 00:00:00 GMT";
      if (request.headers.contains("If-None-Match")) {
        response.http_status = 304;
        response.body.clear();
      }
    }
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

TEST_CASE("mathematical source coordinator rejects changed bytes before "
          "snapshot capture") {
  using namespace statewright;
  const auto root = temporary_root();
  {
    egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
    egcf::InternetImprovementStore internet(store);
    const auto registry = contracts::parse_json(
        core::read_text(std::filesystem::path(STATEWRIGHT_RESOURCE_ROOT) /
                        "watchlists/internet/source-groups-v1.json"));
    const auto &group = registry.at("source_groups").at(5);
    auto manifest = sources::create_watchlist_manifest(
        {{"watches",
          {{{"name", "math-fixture"},
            {"source_group", "fungrim"},
            {"canonical_url", group.at("reviewed_sources").begin().key()},
            {"enabled", true}}}}},
        registry);
    auto &entry = manifest["watches"][0];
    FixtureProvider provider;
    entry["source_review"]["body_sha256"] =
        contracts::sha256_text(provider.body_text);
    const auto policy = sources::watchlist_source_policy(entry, {});
    const auto policy_id = internet.register_source_policy(policy);
    const auto watch = sources::watchlist_watch(entry, policy_id, true, true);
    const auto watch_id = internet.register_watch(watch);
    const auto registration = sources::make_watchlist_registration(
        manifest, entry, watch_id, policy_id, std::string(64, 'b'),
        "REGISTERED_ENABLED");
    static_cast<void>(
        store.register_record({.object_type = "internet-watch-registration",
                               .payload = registration}));
    const auto job =
        sources::make_fetch_job(watch, "2026-09-05T00:00:00Z",
                                "2026-09-05T00:00:00Z", "2026-09-05T00:05:00Z");
    static_cast<void>(internet.register_fetch_job(job));
    const auto lease = sources::acquire_fetch_lease(job.object_id(), "worker",
                                                    "2026-09-05T00:00:01Z",
                                                    "2026-09-05T00:01:01Z");
    static_cast<void>(internet.register_fetch_lease(lease));
    egcf::InternetSourceCoordinator coordinator(store);
    SECTION("matching bytes retain review through registered extraction") {
      const auto fetched = coordinator.execute_fetch(
          job.object_id(), lease.object_id(), "2026-09-05T00:00:02Z", provider);
      REQUIRE(fetched.status == "FETCH_SUCCEEDED");
      const auto extracted = coordinator.extract(fetched.snapshot_id);
      REQUIRE(extracted.extraction.fragments.size() == 1);
      REQUIRE(extracted.extraction.fragments.front().metadata.at(
                  "source_review") == entry.at("source_review"));
      REQUIRE(extracted.extraction.fragments.front().metadata.at(
                  "mathematical_context_review_required") == true);
    }
    SECTION("changed content is never captured as a snapshot") {
      provider.body_text += "Unreviewed content";
      REQUIRE_THROWS(
          coordinator.execute_fetch(job.object_id(), lease.object_id(),
                                    "2026-09-05T00:00:02Z", provider));
      REQUIRE(store.list("internet-source-snapshot").empty());
      REQUIRE(store.list("internet-fetch-receipt").size() == 1);
      REQUIRE(store.list("internet-fetch-receipt")
                  .front()
                  .payload.at("failure_reason")
                  .get<std::string>()
                  .find("PINNED_SOURCE_REVIEW_OR_HASH_INVALID") !=
              std::string::npos);
    }
  }
  std::filesystem::remove_all(root);
}

TEST_CASE(
    "internet source coordinator owns fetch assess and extract assembly") {
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
  const auto job =
      sources::make_fetch_job(watch, "2026-09-04T00:00:00Z",
                              "2026-09-04T00:00:00Z", "2026-09-04T00:05:00Z");
  static_cast<void>(internet.register_fetch_job(job));
  const auto lease = sources::acquire_fetch_lease(
      job.object_id(), "fixture-worker", "2026-09-04T00:00:01Z",
      "2026-09-04T00:01:01Z");
  static_cast<void>(internet.register_fetch_lease(lease));

  FixtureProvider provider;
  egcf::InternetSourceCoordinator coordinator(store);
  const auto fetched = coordinator.execute_fetch(
      job.object_id(), lease.object_id(), "2026-09-04T00:00:02Z", provider);
  REQUIRE(fetched.status == "FETCH_SUCCEEDED");
  REQUIRE_FALSE(fetched.snapshot_id.empty());
  REQUIRE_FALSE(fetched.source_assessment_input_id.empty());
  const auto automatic_input = store.get(fetched.source_assessment_input_id);
  REQUIRE(automatic_input.object_type == "internet-source-assessment-input");
  REQUIRE(automatic_input.payload.at("robots_allowed") == true);
  REQUIRE(automatic_input.payload.at("license_classification") == "UNKNOWN");
  const auto receipt = store.get(fetched.fetch_receipt_id);
  REQUIRE(receipt.payload.at("robots_policy_evaluated") == true);
  REQUIRE(receipt.payload.at("robots_allowed") == true);

  const auto assessed =
      coordinator.assess(fetched.snapshot_id, fetched.fetch_receipt_id,
                         policy_id, true, "CC0-1.0");
  REQUIRE(assessed.assessment.admissible());
  const auto extracted = coordinator.extract(fetched.snapshot_id);
  REQUIRE_FALSE(extracted.extraction.receipt.fragment_ids.empty());
  REQUIRE(store.get(extracted.extraction_receipt_id).object_type ==
          "internet-extraction-receipt");
  std::filesystem::remove_all(root);
}

TEST_CASE(
    "internet source coordinator uses verified watchlist license evidence") {
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
  auto watch = sources::watchlist_watch(entry, policy_id, true, false);
  const auto watch_id = internet.register_watch(watch);
  const auto registration = sources::make_watchlist_registration(
      manifest, entry, watch_id, policy_id, std::string(64U, 'b'),
      "REGISTERED_DISABLED");
  auto registration_id = store.register_record(
      {.object_type = "internet-watch-registration", .payload = registration},
      "internet_watch_registration_registered");

  const auto disabled = watch;
  watch.enabled = true;
  watch.schedule_generation += 1;
  watch.supersedes_watch_id = watch_id;
  watch.watch_signature.clear();
  watch = sources::canonical_watch(watch);
  static_cast<void>(internet.register_watch(watch));
  const auto enabled_registration = sources::supersede_watchlist_registration(
      registration, disabled, watch, registration_id);
  registration_id =
      store.register_record({.object_type = "internet-watch-registration",
                             .payload = enabled_registration},
                            "internet_watch_registration_registered");

  const auto job =
      sources::make_fetch_job(watch, "2026-09-04T02:00:00Z",
                              "2026-09-04T02:00:00Z", "2026-09-04T02:05:00Z");
  static_cast<void>(internet.register_fetch_job(job));
  const auto lease = sources::acquire_fetch_lease(
      job.object_id(), "fixture-worker", "2026-09-04T02:00:01Z",
      "2026-09-04T02:01:01Z");
  static_cast<void>(internet.register_fetch_lease(lease));

  FixtureProvider provider;
  egcf::InternetSourceCoordinator coordinator(store);
  const auto fetched = coordinator.execute_fetch(
      job.object_id(), lease.object_id(), "2026-09-04T02:00:02Z", provider);
  REQUIRE_FALSE(fetched.source_assessment_input_id.empty());
  const auto input = store.get(fetched.source_assessment_input_id);
  REQUIRE(input.payload.at("license_classification") ==
          "FIXTURE_VERIFIED_LICENSE");
  REQUIRE(std::find(input.payload.at("evidence_ids").begin(),
                    input.payload.at("evidence_ids").end(),
                    registration_id) != input.payload.at("evidence_ids").end());
  REQUIRE(input.payload.at("provenance").at("license").at("registration_ids") ==
          contracts::Json::array({registration_id}));

  std::filesystem::remove_all(root);
}

TEST_CASE("internet conditional fetch reuses content and refreshes assessment "
          "lineage") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::InternetImprovementStore internet(store);
  const auto policy = sources::canonical_source_policy({});
  const auto policy_id = internet.register_source_policy(policy);
  sources::InternetWatch watch;
  watch.canonical_url = "https://example.com/conditional";
  watch.source_policy_id = policy_id;
  watch.source_group = "example.com";
  watch.accepted_mime_types = policy.accepted_mime_types;
  watch = sources::canonical_watch(watch);
  static_cast<void>(internet.register_watch(watch));
  FixtureProvider provider;
  provider.conditional = true;
  egcf::InternetSourceCoordinator coordinator(store);
  const auto fetch = [&](const std::string &day) {
    const auto job = sources::make_fetch_job(
        watch, day + "T00:00:00Z", day + "T00:00:00Z", day + "T00:05:00Z");
    static_cast<void>(internet.register_fetch_job(job));
    const auto lease = sources::acquire_fetch_lease(
        job.object_id(), "worker", day + "T00:00:01Z", day + "T00:01:01Z");
    static_cast<void>(internet.register_fetch_lease(lease));
    return coordinator.execute_fetch(job.object_id(), lease.object_id(),
                                     day + "T00:00:02Z", provider);
  };
  const auto first = fetch("2026-09-04");
  const auto assessed = coordinator.assess(
      first.snapshot_id, first.fetch_receipt_id, policy_id, true, "CC0-1.0");
  const auto second = fetch("2026-09-06");
  REQUIRE(second.status == "NOT_MODIFIED");
  REQUIRE(second.snapshot_id == first.snapshot_id);
  REQUIRE(internet.list("internet-source-snapshot").size() == 1U);
  REQUIRE(provider.requests.back().headers.at("If-None-Match") ==
          "\"fixture-v1\"");
  REQUIRE(provider.requests.back().headers.contains("If-Modified-Since"));
  REQUIRE(second.source_assessment_input_id !=
          first.source_assessment_input_id);
  const auto refreshed = coordinator.assess(
      second.snapshot_id, second.fetch_receipt_id, policy_id, true, "CC0-1.0");
  REQUIRE(refreshed.assessment.admissible());
  egcf::InternetAlgorithmCandidate candidate;
  candidate.snapshot_id = first.snapshot_id;
  candidate.source_policy_assessment_id = assessed.assessment_id;
  const auto freshness =
      egcf::internet_source_freshness(store, candidate, "2026-09-06T00:01:00Z");
  REQUIRE(freshness.assessment_id == refreshed.assessment_id);
  REQUIRE(freshness.age_seconds == 59);
  REQUIRE(freshness.admissible);
  REQUIRE_THROWS(egcf::internet_source_freshness(store, candidate,
                                                 "2026-09-05T00:00:00Z"));
  std::filesystem::remove_all(root);
}

TEST_CASE("internet source coordinator rejects a disabled watch before HTTP") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::InternetImprovementStore internet(store);
  const auto policy = sources::canonical_source_policy({});
  sources::InternetWatch watch;
  watch.canonical_url = "https://example.com/disabled";
  watch.source_policy_id = internet.register_source_policy(policy);
  watch.source_group = "example.com";
  watch.accepted_mime_types = policy.accepted_mime_types;
  watch = sources::canonical_watch(watch);
  static_cast<void>(internet.register_watch(watch));
  const auto job =
      sources::make_fetch_job(watch, "2026-09-04T00:00:00Z",
                              "2026-09-04T00:00:00Z", "2026-09-04T00:05:00Z");
  static_cast<void>(internet.register_fetch_job(job));
  const auto lease = sources::acquire_fetch_lease(job.object_id(), "worker",
                                                  "2026-09-04T00:00:01Z",
                                                  "2026-09-04T00:01:01Z");
  static_cast<void>(internet.register_fetch_lease(lease));
  watch.supersedes_watch_id = watch.object_id();
  watch.schedule_generation += 1;
  watch.enabled = false;
  watch.watch_signature.clear();
  static_cast<void>(internet.register_watch(sources::canonical_watch(watch)));
  FixtureProvider provider;
  egcf::InternetSourceCoordinator coordinator(store);
  REQUIRE_THROWS(coordinator.execute_fetch(job.object_id(), lease.object_id(),
                                           "2026-09-04T00:00:02Z", provider));
  REQUIRE(provider.requests.empty());
  std::filesystem::remove_all(root);
}

TEST_CASE(
    "internet source coordinator dispatches registered discovery strategy") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::InternetImprovementStore internet(store);
  const contracts::Json entry = {
      {"accepted_mime_types", {"application/json"}},
      {"canonical_url", "https://example.com/discovery"},
      {"controlling_publisher", "Fixture"},
      {"deterministic_jitter_seconds", 0},
      {"enabled", true},
      {"extraction_strategy", "crossref-json"},
      {"license",
       {{"classification", "CC0-1.0"},
        {"evidence_urls", {"https://example.com/license"}},
        {"status", "verified"}}},
      {"name", "discovery"},
      {"polling_interval_seconds", 3600},
      {"purpose", "discovery"},
      {"robots", {{"declared_status", "pending"}, {"required", true}}},
      {"source_group", "fixture"},
      {"stability", "api-query"},
      {"subject", "algorithms"},
      {"tier", 3}};
  const auto policy = sources::watchlist_source_policy(entry, {});
  const auto policy_id = internet.register_source_policy(policy);
  const auto watch = sources::watchlist_watch(entry, policy_id, true, true);
  const auto watch_id = internet.register_watch(watch);
  const auto registration = sources::make_watchlist_registration(
      {{"schema_version", 1}, {"watchlist_version", "fixture-v1"}}, entry,
      watch_id, policy_id, std::string(64U, 'b'), "REGISTERED_ENABLED");
  static_cast<void>(store.register_record(
      {.object_type = "internet-watch-registration", .payload = registration}));
  const auto job =
      sources::make_fetch_job(watch, "2026-09-04T00:00:00Z",
                              "2026-09-04T00:00:00Z", "2026-09-04T00:05:00Z");
  static_cast<void>(internet.register_fetch_job(job));
  const auto lease = sources::acquire_fetch_lease(job.object_id(), "worker",
                                                  "2026-09-04T00:00:01Z",
                                                  "2026-09-04T00:01:01Z");
  static_cast<void>(internet.register_fetch_lease(lease));
  FixtureProvider provider;
  provider.content_type = "application/json";
  provider.body_text =
      R"({"message":{"items":[{"DOI":"10.1234/algorithm","URL":"https://publisher.example/algorithm","title":["Algorithm"]}]}})";
  egcf::InternetSourceCoordinator coordinator(store);
  const auto fetched = coordinator.execute_fetch(
      job.object_id(), lease.object_id(), "2026-09-04T00:00:02Z", provider);
  const auto extracted = coordinator.extract(fetched.snapshot_id);
  REQUIRE(extracted.extraction.fragments.size() == 1U);
  REQUIRE(extracted.discovery_watch_proposals.size() == 1U);
  REQUIRE(extracted.discovery_watch_proposals.front().at("enabled") == false);
  REQUIRE(
      extracted.discovery_watch_proposals.front().at("source_fragment_id") ==
      extracted.extraction.fragments.front().object_id());
  REQUIRE(internet.active_watch_ids() == std::vector<std::string>{watch_id});
  std::filesystem::remove_all(root);
}
