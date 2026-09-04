#include "statewright/sources/watchlist.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/canonical_json.hpp"
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

class EligibleProvider final
    : public statewright::sources::HttpFetchProvider {
public:
  [[nodiscard]] statewright::sources::FetchResponse
  fetch(const statewright::sources::FetchRequest &request) override {
    statewright::sources::FetchResponse response;
    response.requested_url = request.url;
    response.final_url = request.url;
    response.resolved_addresses = {"93.184.216.34"};
    response.http_status = 200;
    response.headers["content-type"] = "text/html; charset=utf-8";
    response.body = bytes("<html><body>specification</body></html>");
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

TEST_CASE("watchlist semantic validation rejects duplicates and group spoofing") {
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
  manifest["watches"][0]["canonical_url"] =
      "https://example.com/TR/rdf-canon/";
  REQUIRE_THROWS_AS(
      sources::validate_watchlist_manifest(manifest, source_registry()),
      common::Error);
}

TEST_CASE("watchlist preflight binds eligibility to transport robots and license") {
  using namespace statewright;
  const auto registry = source_registry();
  auto manifest = sources::create_watchlist_manifest(
      {{"template", "w3c-recommendation"},
       {"slugs", {"rdf-canon"}},
       {"enabled", true}},
      registry);
  EligibleProvider provider;
  auto base_policy = sources::InternetSourcePolicy{};
  const auto report = sources::preflight_watchlist_manifest(
      manifest, registry, base_policy, provider, "2026-09-04T12:00:00Z");
  REQUIRE(report.at("results").front().at("eligible") == true);
  REQUIRE(report.at("results").front().at("status") ==
          "PREFLIGHT_ELIGIBLE");

  const auto policy = sources::watchlist_source_policy(
      manifest.at("watches").front(), base_policy);
  REQUIRE_FALSE(policy.require_known_license);
  const auto watch = sources::watchlist_watch(
      manifest.at("watches").front(), policy.object_id(), true, true);
  REQUIRE(watch.enabled);
  const auto registration = sources::make_watchlist_registration(
      manifest, manifest.at("watches").front(), watch.object_id(),
      policy.object_id(), report.at("report_signature").get<std::string>(),
      "REGISTERED_ENABLED");
  REQUIRE(registration.at("license_status") == "verified");
  REQUIRE(registration.at("eligibility_status") == "REGISTERED_ENABLED");
  REQUIRE(registration.at("registration_signature")
              .get<std::string>()
              .size() == 64U);
}

TEST_CASE("review-required watchlist entries remain quarantined by preflight") {
  using namespace statewright;
  const auto registry = source_registry();
  const auto manifest = sources::create_watchlist_manifest(
      {{"template", "nist-dlmf-section"}, {"sections", {"3.8"}}},
      registry);
  EligibleProvider provider;
  const auto report = sources::preflight_watchlist_manifest(
      manifest, registry, sources::InternetSourcePolicy{}, provider,
      "2026-09-04T12:00:00Z");
  REQUIRE(report.at("results").front().at("eligible") == false);
  REQUIRE(report.at("results").front().at("blocking_reasons") ==
          Json::array({"license-not-verified"}));
}
