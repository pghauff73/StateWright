#include "statewright/sources/watchlist.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/canonical_json.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/core/file_io.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

namespace {

using Json = statewright::contracts::Json;

Json source_registry() {
  return statewright::contracts::parse_json(statewright::core::read_text(
      std::filesystem::path(STATEWRIGHT_RESOURCE_ROOT) /
      "watchlists/internet/source-groups-v1.json"));
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

class EligibleProvider final : public statewright::sources::HttpFetchProvider {
public:
  std::string content_type = "text/html; charset=utf-8";
  std::string body = "<html><body>specification</body></html>";
  [[nodiscard]] statewright::sources::FetchResponse
  fetch(const statewright::sources::FetchRequest &request) override {
    statewright::sources::FetchResponse response;
    response.requested_url = request.url;
    response.final_url = request.url;
    response.resolved_addresses = {"93.184.216.34"};
    response.http_status = 200;
    response.headers["content-type"] = content_type;
    response.body = bytes(body);
    response.tls_verified = true;
    response.robots_policy_evaluated = true;
    response.robots_allowed = true;
    response.robots_evidence =
        Json::array({{{"allowed", true},
                      {"body_sha256", std::string(64U, 'a')},
                      {"final_url", "https://www.w3.org/robots.txt"},
                      {"http_status", 200},
                      {"path", "/TR/rdf-canon/"},
                      {"redirect_chain", Json::array()},
                      {"requested_url", "https://www.w3.org/robots.txt"},
                      {"user_agent", request.policy.user_agent}}});
    response.compressed_bytes = response.body.size();
    response.decompressed_bytes = response.body.size();
    response.total_time_milliseconds = 2;
    response.provider_identity = "watchlist-fixture-v1";
    return response;
  }
};

} // namespace

TEST_CASE("mathematical watchlist requires reviewed immutable files and body "
          "hashes") {
  using namespace statewright;
  auto registry = source_registry();
  auto &group = registry["source_groups"][5];
  REQUIRE(group.at("source_group") == "fungrim");
  const std::string url = group.at("reviewed_sources").begin().key();
  EligibleProvider provider;
  provider.content_type = "text/plain";
  provider.body =
      "Formula with Assumptions(x > 0); principal branch; error <= 1e-20\n";
  group["reviewed_sources"][url] = contracts::sha256_text(provider.body);
  const auto manifest =
      sources::create_watchlist_manifest({{"watches",
                                           {{{"name", "fungrim-fixture"},
                                             {"source_group", "fungrim"},
                                             {"canonical_url", url},
                                             {"enabled", true}}}}},
                                         registry);
  const auto &entry = manifest.at("watches").front();
  REQUIRE(sources::valid_mathematical_source_review(entry.at("source_review")));
  const auto policy = sources::watchlist_source_policy(entry, {});
  const auto report = sources::preflight_watchlist_manifest(
      manifest, registry, {}, provider, "2026-09-05T01:00:00Z");
  REQUIRE(sources::watchlist_preflight_eligible(
      entry, report.at("results").front(), policy));
  const auto registration = sources::make_watchlist_registration(
      manifest, entry, "watch", policy.object_id(), "report",
      "REGISTERED_DISABLED");
  REQUIRE(registration.at("source_review") == entry.at("source_review"));
  provider.body += "changed";
  const auto changed = sources::preflight_watchlist_manifest(
      manifest, registry, {}, provider, "2026-09-05T01:00:01Z");
  REQUIRE_FALSE(changed.at("results").front().at("eligible").get<bool>());
  REQUIRE(changed.at("results").front().at("blocking_reasons").at(0) ==
          "pinned-content-mismatch");
  auto forged_report = report.at("results").front();
  forged_report.erase("body_sha256");
  REQUIRE_FALSE(
      sources::watchlist_preflight_eligible(entry, forged_report, policy));
  for (const auto *field : {"revision", "body_sha256", "third_party_review"}) {
    auto invalid = manifest;
    invalid["watches"][0]["source_review"][field] = "changed";
    REQUIRE_THROWS(sources::validate_watchlist_manifest(invalid, registry));
  }
  auto invalid = manifest;
  invalid["watches"][0]["source_review"]["license_notices"][0]["text"] =
      "changed";
  REQUIRE_FALSE(sources::valid_mathematical_source_review(
      invalid["watches"][0]["source_review"]));
  REQUIRE_THROWS(sources::validate_watchlist_manifest(invalid, registry));
  invalid = manifest;
  invalid["watches"][0]["canonical_url"] = url + "-unreviewed";
  REQUIRE_THROWS(sources::validate_watchlist_manifest(invalid, registry));
  invalid = manifest;
  invalid["watches"][0]["extraction_strategy"] = "plain-text";
  REQUIRE_THROWS(sources::validate_watchlist_manifest(invalid, registry));
}

TEST_CASE("watchlist URL creator generates the authoritative DLMF lane") {
  using namespace statewright;
  const Json request = {
      {"template", "nist-dlmf-section"},
      {"watchlist_version", "dlmf-test-v1"},
      {"subject", "numerical-methods"},
      {"sections", {"3.2", "3.3", "3.5", "3.8", "3.9", "3.11", "3.12"}}};
  const auto manifest =
      sources::create_watchlist_manifest(request, source_registry());
  REQUIRE(manifest.at("watches").size() == 7U);
  REQUIRE(manifest.at("watches").front().at("name") ==
          "nist-dlmf-linear-algebra");
  REQUIRE(manifest.at("watches").at(3).at("canonical_url") ==
          "https://dlmf.nist.gov/3.8");
  REQUIRE(manifest.at("watches").back().at("name") ==
          "nist-dlmf-mathematical-constants");
  REQUIRE(manifest.at("watches").front().at("enabled") == false);

  const auto policy = sources::watchlist_source_policy(
      manifest.at("watches").front(), sources::InternetSourcePolicy{});
  REQUIRE(policy.require_known_license);
}

TEST_CASE(
    "watchlist semantic validation rejects duplicates and group spoofing") {
  using namespace statewright;
  auto manifest = sources::create_watchlist_manifest(
      {{"template", "w3c-recommendation"}, {"slugs", {"rdf-canon"}}},
      source_registry());
  manifest["watches"].push_back(manifest.at("watches").front());
  REQUIRE_THROWS_AS(
      sources::validate_watchlist_manifest(manifest, source_registry()),
      common::Error);

  manifest = sources::create_watchlist_manifest(
      {{"template", "w3c-recommendation"}, {"slugs", {"rdf-canon"}}},
      source_registry());
  manifest["watches"][0]["canonical_url"] = "https://example.com/TR/rdf-canon/";
  REQUIRE_THROWS_AS(
      sources::validate_watchlist_manifest(manifest, source_registry()),
      common::Error);
}

TEST_CASE(
    "watchlist preflight binds eligibility to transport robots and license") {
  using namespace statewright;
  const auto registry = source_registry();
  auto manifest =
      sources::create_watchlist_manifest({{"template", "w3c-recommendation"},
                                          {"slugs", {"rdf-canon"}},
                                          {"enabled", true}},
                                         registry);
  EligibleProvider provider;
  auto base_policy = sources::InternetSourcePolicy{};
  const auto report = sources::preflight_watchlist_manifest(
      manifest, registry, base_policy, provider, "2026-09-04T12:00:00Z");
  REQUIRE(report.at("results").front().at("eligible") == true);
  REQUIRE(report.at("results").front().at("status") == "PREFLIGHT_ELIGIBLE");

  const auto policy = sources::watchlist_source_policy(
      manifest.at("watches").front(), base_policy);
  REQUIRE(policy.require_known_license);
  REQUIRE(report.at("source_registry_sha256") ==
          contracts::sha256_json(registry));
  REQUIRE(report.at("results").front().at("source_policy_id") ==
          policy.object_id());
  REQUIRE(sources::watchlist_preflight_eligible(
      manifest.at("watches").front(), report.at("results").front(), policy));
  const auto watch = sources::watchlist_watch(manifest.at("watches").front(),
                                              policy.object_id(), true, true);
  REQUIRE(watch.enabled);
  const auto registration = sources::make_watchlist_registration(
      manifest, manifest.at("watches").front(), watch.object_id(),
      policy.object_id(), report.at("report_signature").get<std::string>(),
      "REGISTERED_ENABLED");
  REQUIRE(registration.at("license_status") == "verified");
  REQUIRE(registration.at("eligibility_status") == "REGISTERED_ENABLED");
  REQUIRE(registration.at("extraction_strategy") == "w3c-specification");
  REQUIRE(registration.at("evidence_independence_group") == "w3c");
  REQUIRE(registration.at("registration_signature").get<std::string>().size() ==
          64U);
}

TEST_CASE("all named watchlist templates obey registry permissions") {
  using namespace statewright;
  for (const Json &request :
       {Json{{"template", "nist-dlmf-section"}, {"sections", {"3.8"}}},
        Json{{"template", "rfc-number"}, {"numbers", {5869}}},
        Json{{"template", "w3c-recommendation"}, {"slugs", {"rdf-canon"}}}}) {
    auto registry = source_registry();
    for (auto &group : registry["source_groups"]) {
      group["allowed_templates"] = Json::array();
    }
    REQUIRE_THROWS_AS(sources::create_watchlist_manifest(request, registry),
                      common::Error);
  }
}

TEST_CASE("imported watchlist eligibility cannot override license or effective "
          "policy") {
  using namespace statewright;
  const auto registry = source_registry();
  auto manifest =
      sources::create_watchlist_manifest({{"template", "w3c-recommendation"},
                                          {"slugs", {"rdf-canon"}},
                                          {"enabled", true}},
                                         registry);
  auto &entry = manifest["watches"][0];
  const auto policy = sources::watchlist_source_policy(entry, {});
  EligibleProvider provider;
  const auto report = sources::preflight_watchlist_manifest(
      manifest, registry, {}, provider, "2026-09-04T12:00:00Z");
  auto result = report.at("results").front();
  REQUIRE(sources::watchlist_preflight_eligible(entry, result, policy));

  SECTION("prohibited and unresolved licenses") {
    for (const std::string status : {"prohibited", "review-required"}) {
      entry["license"]["status"] = status;
      result["entry_sha256"] = contracts::sha256_json(entry);
      REQUIRE_FALSE(
          sources::watchlist_preflight_eligible(entry, result, policy));
      REQUIRE_FALSE(
          sources::watchlist_watch(entry, policy.object_id(), true, true)
              .enabled);
    }
  }
  SECTION("policy changed after preflight") {
    auto changed = policy;
    changed.maximum_response_bytes = 1;
    changed.policy_signature.clear();
    changed = sources::canonical_source_policy(changed);
    REQUIRE_FALSE(
        sources::watchlist_preflight_eligible(entry, result, changed));
    result["source_policy_id"] = changed.object_id();
    REQUIRE_FALSE(
        sources::watchlist_preflight_eligible(entry, result, changed));
  }
  SECTION("missing or private transport evidence") {
    result["resolved_addresses"] = {"127.0.0.1"};
    REQUIRE_FALSE(sources::watchlist_preflight_eligible(entry, result, policy));
    result = report.at("results").front();
    result.erase("compressed_bytes");
    REQUIRE_FALSE(sources::watchlist_preflight_eligible(entry, result, policy));
  }
  SECTION("redirect and robots denial") {
    result["redirect_chain"] = {"https://www.w3.org/"};
    REQUIRE_FALSE(sources::watchlist_preflight_eligible(entry, result, policy));
    result = report.at("results").front();
    entry["robots"]["declared_status"] = "denied";
    result["entry_sha256"] = contracts::sha256_json(entry);
    REQUIRE_FALSE(sources::watchlist_preflight_eligible(entry, result, policy));
  }
  SECTION("entry cannot relax base robots or MIME constraints") {
    entry["robots"]["required"] = false;
    REQUIRE(
        sources::watchlist_source_policy(entry, {}).require_robots_permission);
    auto restricted = sources::InternetSourcePolicy{};
    restricted.accepted_mime_types = {"application/json"};
    REQUIRE_THROWS_AS(sources::watchlist_source_policy(entry, restricted),
                      common::Error);
  }
}

TEST_CASE(
    "watchlist generations preserve evidence only for scheduling changes") {
  using namespace statewright;
  const auto manifest =
      sources::create_watchlist_manifest({{"template", "w3c-recommendation"},
                                          {"slugs", {"rdf-canon"}},
                                          {"enabled", true}},
                                         source_registry());
  const auto &entry = manifest.at("watches").front();
  const auto policy = sources::watchlist_source_policy(entry, {});
  const auto previous =
      sources::watchlist_watch(entry, policy.object_id(), true, false);
  auto registration = sources::make_watchlist_registration(
      manifest, entry, previous.object_id(), policy.object_id(),
      std::string(64, 'a'), "REGISTERED_DISABLED", "publisher-family");
  auto next = previous;
  next.supersedes_watch_id = previous.object_id();
  next.schedule_generation++;
  next.enabled = true;
  next.polling_interval_seconds *= 2;
  next.watch_signature.clear();
  next = sources::canonical_watch(next);

  SECTION("enable and reschedule copy verified provenance") {
    const auto carried = sources::supersede_watchlist_registration(
        registration, previous, next, "registration:previous");
    REQUIRE(carried.at("watch_id") == next.object_id());
    REQUIRE(carried.at("predecessor_registration_id") ==
            "registration:previous");
    REQUIRE(carried.at("license_status") == "verified");
    REQUIRE(carried.at("eligibility_status") == "REGISTERED_ENABLED");
    REQUIRE(carried.at("extraction_strategy") == "w3c-specification");
    REQUIRE(carried.at("evidence_independence_group") == "publisher-family");
    REQUIRE(carried.at("registration_signature") !=
            registration.at("registration_signature"));
  }
  SECTION("source identity change needs new evidence") {
    next.canonical_url = "https://www.w3.org/TR/json-ld11-api/";
    next.watch_signature.clear();
    next = sources::canonical_watch(next);
    REQUIRE_THROWS_AS(
        sources::supersede_watchlist_registration(registration, previous, next,
                                                  "registration:previous"),
        common::Error);
    next.enabled = false;
    next.watch_signature.clear();
    next = sources::canonical_watch(next);
    const auto quarantine = sources::supersede_watchlist_registration(
        registration, previous, next, "registration:previous");
    REQUIRE(quarantine.at("license_status") == "review-required");
    REQUIRE(quarantine.at("eligibility_status") == "QUARANTINED");
    REQUIRE(quarantine.at("preflight_report_sha256") == "");
    auto enabled = next;
    enabled.enabled = true;
    enabled.supersedes_watch_id = next.object_id();
    enabled.schedule_generation++;
    enabled.watch_signature.clear();
    enabled = sources::canonical_watch(enabled);
    REQUIRE_THROWS_AS(sources::supersede_watchlist_registration(
                          quarantine, next, enabled, "registration:quarantine"),
                      common::Error);
  }
  SECTION("legacy signed records remain usable") {
    registration.erase("extraction_strategy");
    registration.erase("evidence_independence_group");
    registration.erase("registration_signature");
    registration["registration_signature"] =
        contracts::sha256_json(registration);
    const auto carried = sources::supersede_watchlist_registration(
        registration, previous, next, "registration:legacy");
    REQUIRE(carried.at("license_status") == "verified");
    REQUIRE_FALSE(carried.contains("extraction_strategy"));
  }
}

TEST_CASE("review-required watchlist entries remain quarantined by preflight") {
  using namespace statewright;
  const auto registry = source_registry();
  const auto manifest = sources::create_watchlist_manifest(
      {{"template", "nist-dlmf-section"}, {"sections", {"3.8"}}}, registry);
  EligibleProvider provider;
  const auto report = sources::preflight_watchlist_manifest(
      manifest, registry, sources::InternetSourcePolicy{}, provider,
      "2026-09-04T12:00:00Z");
  REQUIRE(report.at("results").front().at("eligible") == false);
  REQUIRE(report.at("results").front().at("blocking_reasons") ==
          Json::array({"license-not-verified"}));
}
