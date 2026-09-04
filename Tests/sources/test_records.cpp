#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/sources/policy.hpp"
#include "statewright/sources/records.hpp"
#include "statewright/sources/snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace {

std::vector<std::byte> bytes(std::string_view text) {
  std::vector<std::byte> result;
  result.reserve(text.size());
  for (const char value : text) {
    result.push_back(static_cast<std::byte>(value));
  }
  return result;
}

} // namespace

TEST_CASE("internet source policy has deterministic identity") {
  using namespace statewright;
  auto policy = sources::canonical_source_policy({});
  REQUIRE(policy.policy_signature.size() == 64U);
  REQUIRE(policy.object_id().starts_with("internet-source-policy:sha256:"));

  auto reordered = policy;
  reordered.policy_signature.clear();
  reordered.accepted_mime_types = {"text/html", "application/json",
                                   "text/plain", "text/html"};
  reordered.allowed_ports = {443, 443};
  REQUIRE(sources::canonical_source_policy(std::move(reordered)).object_id() ==
          policy.object_id());
}

TEST_CASE("internet records preserve deterministic job and snapshot identity") {
  using namespace statewright;
  const auto policy = sources::canonical_source_policy({});
  sources::InternetWatch watch;
  watch.canonical_url = "https://example.com/algorithm";
  watch.source_policy_id = policy.object_id();
  watch.source_group = "example.com";
  watch.accepted_mime_types = policy.accepted_mime_types;
  watch = sources::canonical_watch(std::move(watch));
  REQUIRE(watch.object_id().starts_with("internet-watch:sha256:"));

  sources::FetchResponse response;
  response.requested_url = watch.canonical_url;
  response.final_url = watch.canonical_url;
  response.resolved_addresses = {"93.184.216.34"};
  response.http_status = 200;
  response.headers["content-type"] = "text/plain; charset=utf-8";
  response.body = bytes("algorithm identity(x) = x");
  response.tls_verified = true;
  response.provider_identity = "fixture-http-provider-v1";
  response.decompressed_bytes = response.body.size();
  response.compressed_bytes = response.body.size();

  const auto snapshot = sources::make_source_snapshot(
      response, "artifact:sha256:" + contracts::sha256_text("artifact"),
      watch.source_group);
  const auto repeated = sources::make_source_snapshot(
      response, "artifact:sha256:" + contracts::sha256_text("artifact"),
      watch.source_group);
  REQUIRE(snapshot.object_id() == repeated.object_id());
  REQUIRE(snapshot.content_type == "text/plain");
  REQUIRE(snapshot.body_sha256 ==
          contracts::sha256_bytes(std::span<const std::byte>(response.body)));
}

TEST_CASE("internet records fail closed on invalid limits") {
  using namespace statewright;
  sources::InternetWatch watch;
  watch.canonical_url = "https://example.com";
  watch.source_policy_id = "internet-source-policy:sha256:invalid";
  watch.source_group = "example.com";
  watch.accepted_mime_types = {"text/plain"};
  watch.maximum_response_bytes = 0U;
  REQUIRE_THROWS_AS(sources::canonical_watch(std::move(watch)), common::Error);

  auto policy = sources::InternetSourcePolicy{};
  policy.allowed_schemes = {"file"};
  REQUIRE_THROWS_AS(sources::canonical_source_policy(std::move(policy)),
                    common::Error);
}

TEST_CASE("internet fetch receipts reject placeholder robots evidence") {
  using namespace statewright;
  sources::InternetFetchReceipt receipt;
  receipt.job_id = "fixture-job";
  receipt.lease_id = "fixture-lease";
  receipt.requested_url = "https://example.com/algorithm";
  receipt.final_url = receipt.requested_url;
  receipt.http_status = 200;
  receipt.robots_policy_evaluated = true;
  receipt.robots_allowed = true;
  receipt.robots_evidence.push_back(contracts::Json::object());
  receipt.provider_identity = "fixture-provider";
  receipt.snapshot_id = "fixture-snapshot";
  receipt.status = "FETCH_SUCCEEDED";
  REQUIRE_THROWS_AS(sources::canonical_fetch_receipt(std::move(receipt)),
                    common::Error);
}
