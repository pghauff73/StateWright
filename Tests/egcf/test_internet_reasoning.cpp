#include "statewright/egcf/internet_feed.hpp"
#include "statewright/egcf/internet_reasoning.hpp"

#include "statewright/contracts/hash.hpp"
#include "statewright/providers/reasoning_provider.hpp"
#include "statewright/sources/extraction.hpp"
#include "statewright/sources/policy.hpp"
#include "statewright/sources/scheduler.hpp"
#include "statewright/sources/snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

class StaticProvider final : public statewright::providers::ReasoningProvider {
public:
  explicit StaticProvider(statewright::contracts::Json response)
      : response_(std::move(response)) {}

  statewright::contracts::Json
  create_response(const statewright::contracts::Json &) override {
    return response_;
  }

private:
  statewright::contracts::Json response_;
};

std::filesystem::path temporary_root() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("statewright-internet-reasoning-" +
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

struct PreparedCandidate final {
  statewright::egcf::InternetAlgorithmCandidate candidate;
  std::vector<statewright::sources::InternetSourceFragment> fragments;
};

PreparedCandidate prepare_candidate(statewright::egcf::EgcfStore &store) {
  using namespace statewright;
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
  egcf::InternetFeedCoordinator feed(store);
  const auto result = feed.process(assessment, extraction, "identity");
  REQUIRE(result.candidates.size() == 1U);
  return {.candidate = result.candidates.front(),
          .fragments = extraction.fragments};
}

} // namespace

TEST_CASE("internet reasoning falls back deterministically without a provider") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  const auto prepared = prepare_candidate(store);
  egcf::InternetReasoningCoordinator coordinator(store);
  const auto result = coordinator.analyze(prepared.candidate, prepared.fragments);
  REQUIRE(result.analysis.status == "DETERMINISTIC_FALLBACK");
  REQUIRE_FALSE(result.analysis.provider_available);
  REQUIRE_FALSE(result.analysis.authoritative);
  REQUIRE(result.analysis.provider_output_signature !=
          contracts::sha256_json(contracts::Json::object()));
  REQUIRE(result.analysis.proposal_ids.size() == 2U);
  REQUIRE(result.analysis.falsifier_ids.size() == 2U);
  REQUIRE(result.updated_candidate.oiec_sr_proposal_ids ==
          result.analysis.proposal_ids);
  REQUIRE(store.active_ids("internet-algorithm-candidate") ==
          std::vector<std::string>{result.updated_candidate_id});
  REQUIRE(store.list("algorithm-definition").empty());
  auto material = egcf::to_json(result);
  material.erase("result_signature");
  REQUIRE(result.result_signature == contracts::sha256_json(material));
  std::filesystem::remove_all(root);
}

TEST_CASE("internet reasoning records malformed provider fallback") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  const auto prepared = prepare_candidate(store);
  StaticProvider provider({{"output_text", "not-json"},
                           {"usage", {{"output_tokens", 1}}}});
  egcf::InternetReasoningCoordinator coordinator(store);
  const auto result = coordinator.analyze(
      prepared.candidate, prepared.fragments, &provider, "fixture-provider-v1",
      "fixture-model-v1");
  REQUIRE(result.analysis.status == "PROVIDER_FAILED_FALLBACK");
  REQUIRE_FALSE(result.analysis.provider_available);
  REQUIRE_FALSE(result.analysis.authoritative);
  REQUIRE(result.analysis.provider_identity == "fixture-provider-v1");
  std::filesystem::remove_all(root);
}

TEST_CASE("internet reasoning retains valid provider and parser provenance") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  const auto prepared = prepare_candidate(store);
  StaticProvider provider(
      {{"output_text",
        R"({"falsifiers":["output differs from input"],"hypotheses":["the source defines identity","the source is incomplete"],"missing_evidence":["independent fixture"],"unresolved_assumptions":["input domain is unrestricted"]})"},
       {"usage", {{"input_tokens", 100}, {"output_tokens", 100}}}});
  egcf::InternetReasoningCoordinator coordinator(store);
  const auto result = coordinator.analyze(
      prepared.candidate, prepared.fragments, &provider, "fixture-provider-v1",
      "fixture-model-v1");
  REQUIRE(result.analysis.status == "PROVIDER_ADVISORY");
  REQUIRE(result.analysis.provider_available);
  REQUIRE_FALSE(result.analysis.authoritative);
  REQUIRE(result.analysis.grammar_identity.starts_with("sha256:"));
  REQUIRE(result.analysis.parser_version ==
          "statewright-internet-reasoning-parser-v1");
  REQUIRE(result.updated_candidate.unresolved_assumptions ==
          std::vector<std::string>{"input domain is unrestricted"});
  std::filesystem::remove_all(root);
}
