#include "statewright/egcf/internet_feed.hpp"
#include "statewright/egcf/internet_polynomial.hpp"
#include "statewright/egcf/grounded_experiment.hpp"
#include "statewright/egcf/internet_polynomial_protocol.hpp"

#include "statewright/contracts/hash.hpp"
#include "statewright/core/file_io.hpp"
#include "statewright/saa/algorithm_ir.hpp"
#include "statewright/sources/extraction.hpp"
#include "statewright/sources/policy.hpp"
#include "statewright/sources/scheduler.hpp"
#include "statewright/sources/snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path temporary_root() {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("statewright-internet-feed-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
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

struct FeedFixture final {
  statewright::sources::InternetPolicyAssessment assessment;
  statewright::sources::InternetExtractionResult extraction;
};

FeedFixture fixture(statewright::egcf::EgcfStore &store,
                    std::string_view text,
                    std::string_view content_type = "text/plain") {
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
  const auto job =
      sources::make_fetch_job(watch, "2026-09-02T01:00:00Z",
                              "2026-09-02T01:00:00Z", "2026-09-02T01:05:00Z");
  static_cast<void>(internet.register_fetch_job(job));
  const auto lease = sources::acquire_fetch_lease(job.object_id(), "worker-a",
                                                  "2026-09-02T01:00:01Z",
                                                  "2026-09-02T01:01:01Z");
  static_cast<void>(internet.register_fetch_lease(lease));

  sources::FetchResponse response;
  response.requested_url = watch.canonical_url;
  response.final_url = watch.canonical_url;
  response.resolved_addresses = {"93.184.216.34"};
  response.http_status = 200;
  response.headers["content-type"] = content_type;
  response.body = bytes(text);
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
  return {.assessment = assessment, .extraction = extraction};
}

} // namespace

TEST_CASE("mathematical context cannot be promoted through a supported "
          "identity example") {
  using namespace statewright;
  const auto root = temporary_root();
  {
    egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
    auto source = fixture(store, "Identity algorithm; inputs: x; outputs: y; "
                                 "procedure: return the input\n");
    auto &fragment = source.extraction.fragments.front();
    fragment.metadata["mathematical_context_review_required"] = true;
    fragment = sources::canonical_source_fragment(fragment);
    source.extraction.receipt.fragment_ids = {fragment.object_id()};
    source.extraction.receipt =
        sources::canonical_extraction_receipt(source.extraction.receipt);
    const auto result = egcf::InternetFeedCoordinator(store).process(
        source.assessment, source.extraction, "math-context");
    REQUIRE(result.candidates.size() == 1);
    REQUIRE(result.candidates.front().status == "QUARANTINED");
    REQUIRE(result.candidates.front().proposed_saa_ir.empty());
    REQUIRE(result.candidates.front().unresolved_assumptions.size() == 2);
    REQUIRE(store.list("algorithm-definition").empty());
  }
  std::filesystem::remove_all(root);
}

TEST_CASE("internet feed stages supported SAA IR without canonical admission") {
  using namespace statewright;
  const auto root = temporary_root();
  egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::InternetImprovementStore internet(store);
  const auto source = fixture(store, "Identity algorithm; inputs: x; outputs: "
                                     "y; procedure: return the input\n");

  egcf::InternetFeedCoordinator coordinator(store);
  const auto first =
      coordinator.process(source.assessment, source.extraction, "identity");
  REQUIRE(first.brain_feed_batch.canonical_algorithm_admissions == 0U);
  REQUIRE(first.candidates.size() == 1U);
  auto first_material = egcf::to_json(first);
  first_material.erase("result_signature");
  REQUIRE(first.result_signature == contracts::sha256_json(first_material));
  REQUIRE(first.retrieval_receipts.size() == 1U);
  REQUIRE(first.candidates.front().status == "VALIDATION_READY");
  REQUIRE(first.retrieval_receipts.front().novelty_status == "NOVEL_CANDIDATE");
  REQUIRE(first.retrieval_receipts.front().search_complete);
  REQUIRE_FALSE(first.retrieval_receipts.front().exclusions.empty());
  const auto ir =
      saa::canonicalize_mapping(first.candidates.front().proposed_saa_ir);
  REQUIRE(ir.structural_hash.size() == 64U);
  REQUIRE(store.list("algorithm-definition").empty());

  const auto repeated =
      coordinator.process(source.assessment, source.extraction, "identity");
  REQUIRE(repeated.candidates.size() == 1U);
  REQUIRE(repeated.candidates.front().object_id() ==
          first.candidates.front().object_id());
  REQUIRE(repeated.result_signature == first.result_signature);
  REQUIRE(internet.list("internet-retrieval-receipt").size() == 1U);
  std::filesystem::remove_all(root);
}

TEST_CASE("internet feed extracts and stages a source-bound polynomial without qualification") {
  using namespace statewright;
  const auto root = temporary_root();
  {
    egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
    const auto source = fixture(store,
        "Quadratic algorithm; Inputs: x; Outputs: y; Domain: [-1,1]; "
        "Arithmetic: exact rational; Units: dimensionless; Procedure: y = 2*x^2 - 1\n");
    egcf::InternetFeedCoordinator coordinator(store);
    const auto first = coordinator.process(source.assessment, source.extraction, "test-only-polynomial");
    REQUIRE(first.complete);
    REQUIRE(first.candidates.size() == 1);
    REQUIRE(first.brain_feed_batch.canonical_algorithm_admissions == 0);
    const auto &candidate = first.candidates.front();
    REQUIRE(candidate.status == "VALIDATION_READY");
    REQUIRE(candidate.proposed_saa_ir == egcf::internet_exact_polynomial_ir({-1, 0, 2}));
    REQUIRE(candidate.applicability.at("translation").at("translator_version") == "exact-polynomial-source-v1");
    const auto fragment = sources::internet_source_fragment_from_json(
        store.get(candidate.source_fragment_id).payload);
    REQUIRE_NOTHROW(egcf::verify_internet_candidate_translation(candidate, fragment));
    egcf::InternetExperimentRequest request;
    std::string reason;
    try {
      static_cast<void>(egcf::validate_grounded_experiment(store, candidate, request));
    } catch (const std::exception &error) {
      reason = error.what();
    }
    REQUIRE(reason.find("POLYNOMIAL_VERSIONED_GROUNDED_PROTOCOL_REQUIRED") != std::string::npos);
    egcf::EvidenceInput baseline_input;
    baseline_input.subject_id = candidate.object_id();
    baseline_input.category = "catalogue-presence";
    baseline_input.producer = "deterministic-test-catalogue-query";
    baseline_input.method = "NATIVE_CATALOGUE_LOOKUP";
    baseline_input.source_snapshot_hash = store.get(candidate.snapshot_id).payload.at("body_sha256");
    baseline_input.success = true;
    baseline_input.content = {{"kind", "CANONICAL_CATALOG_UNSUPPORTED_BASELINE_V1"},
        {"candidate_id", candidate.object_id()}, {"workspace", root.string()},
        {"search", egcf::exact_capability_search(store, candidate)}};
    const auto baseline_id = egcf::EvidenceManager(store).collect(std::move(baseline_input));
    egcf::InternetExperimentProtocol protocol;
    protocol.protocol_version = egcf::internet_polynomial_protocol_version;
    protocol.applicable_candidate_statuses = {"VALIDATION_READY"};
    protocol.baseline_ref = baseline_id;
    protocol.dataset_snapshot_ids = {candidate.snapshot_id};
    protocol.valid_from = "2026-09-02T00:00:00Z";
    protocol.trial_groups = contracts::Json::array({
        {{"group_id", "test-only-anchors"}, {"inputs", {"-1", "0"}}, {"expected_outputs", {"1", "-1"}}},
        {{"group_id", "test-only-fractions"}, {"inputs", {"-1/2", "1/2"}}, {"expected_outputs", {"-1/2", "-1/2"}}}});
    protocol.source_provenance = {{"grounded", {
        {"adoption_mode", "NEW_CAPABILITY"}, {"author_identity", "synthetic-fixture-author"},
        {"candidate_id", candidate.object_id()},
        {"candidate_ir_sha256", contracts::sha256_json(candidate.proposed_saa_ir)},
        {"source_fragment_id", candidate.source_fragment_id},
        {"source_body_sha256", store.get(candidate.snapshot_id).payload.at("body_sha256")},
        {"execution_contract_sha256", contracts::sha256_json(egcf::internet_polynomial_contract(
            egcf::internet_exact_polynomial_program(candidate.proposed_saa_ir)))},
        {"claim", {{"inputs", candidate.semantic_inputs}, {"outputs", candidate.semantic_outputs},
            {"units", candidate.units}, {"domain", "exact rationals in [-1,1]"},
            {"exclusions", {"outside declared domain", "resource-limited evaluations"}}}},
        {"reference_oracle_ir", egcf::internet_exact_polynomial_reference_ir({-1, 0, 2})},
        {"baseline_rationale", "CANONICAL_CATALOG_LOOKUP_ONLY"},
        {"measurement_evidence_id", ""}, {"review_evidence_ids", contracts::Json::array()}}}};
    const auto frozen_id = egcf::freeze_internet_polynomial_protocol(store, protocol);
    const auto protocol_id = egcf::register_frozen_internet_polynomial_protocol(store, protocol, frozen_id);
    REQUIRE(store.get(protocol_id).object_type == "internet-experiment-protocol");
    REQUIRE(store.get(protocol_id).payload.at("protocol_version") == egcf::internet_polynomial_protocol_version);
    REQUIRE(store.get(protocol_id).payload.at("source_provenance").at("grounded").at("review_evidence_ids").empty());
    const auto registration_head = store.event_head();
    REQUIRE(egcf::register_frozen_internet_polynomial_protocol(store, protocol, frozen_id) == protocol_id);
    REQUIRE(store.event_head() == registration_head);
    auto altered_protocol = protocol;
    altered_protocol.minimum_output = "-5";
    REQUIRE_THROWS(egcf::register_frozen_internet_polynomial_protocol(store, altered_protocol, frozen_id));
    const auto repeated = coordinator.process(source.assessment, source.extraction, "test-only-polynomial");
    REQUIRE(repeated.candidates.size() == 1);
    REQUIRE(repeated.candidates.front().object_id() == candidate.object_id());
    REQUIRE(repeated.result_signature == first.result_signature);
    REQUIRE(store.list("internet-experiment-qualification").empty());
    REQUIRE(store.list("internet-polynomial-canonical").empty());
    REQUIRE(store.list("internet-probation-admission").empty());
  }
  std::filesystem::remove_all(root);
}

TEST_CASE(
    "internet feed quarantines ambiguous identity mentions and procedures") {
  using namespace statewright;
  const std::vector<std::string> descriptions = {
      "Identity matrix algorithm; inputs: x; outputs: y; procedure: invert the "
      "matrix",
      "Identity algorithm; inputs: x; outputs: y; procedure: do not return the "
      "input",
      "Identity algorithm; inputs: x; outputs: y; procedure: return the input "
      "unless x is negative",
      "Identity algorithm; procedure: return the input",
      "Identity algorithm; inputs: x; outputs: y; procedure: return the input; "
      "procedure: negate x",
      "Identity algorithm; inputs: x; outputs: y; procedure: return the input; "
      "precondition: x is positive",
      "Affine algorithm; inputs: x; outputs: y; procedure: return 1/0*x+1",
      "Affine algorithm; inputs: x; outputs: y; procedure: return 0*x+1",
      "Affine algorithm; inputs: x; outputs: y; procedure: return 2*z+1",
      "Scale algorithm; inputs: x; outputs: y; procedure: return 1/0*x",
      "Scale algorithm; inputs: x; outputs: y; procedure: return 0*x",
      "Scale algorithm; inputs: x; outputs: y; procedure: return 2*z",
      "Scale algorithm; inputs: x; outputs: y; procedure: return 2*x*x",
      "Offset algorithm; inputs: x; outputs: y; procedure: return x+1/0",
      "Offset algorithm; inputs: x; outputs: y; procedure: return z+1",
      "Offset algorithm; inputs: x; outputs: y; procedure: return x+1 unless x "
      "is negative"};
  for (const auto &description : descriptions) {
    INFO(description);
    const auto root = temporary_root();
    {
      egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
      const auto source = fixture(store, description);
      const auto result = egcf::InternetFeedCoordinator(store).process(
          source.assessment, source.extraction, "negative-fidelity");
      REQUIRE(result.candidates.size() == 1U);
      REQUIRE(result.candidates.front().status == "QUARANTINED");
      REQUIRE(result.candidates.front().proposed_saa_ir.empty());
    }
    std::filesystem::remove_all(root);
  }
}

TEST_CASE("internet feed binds exact affine translation to its source span") {
  using namespace statewright;
  const auto root = temporary_root();
  {
    egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
    const auto source =
        fixture(store, "Affine calibration algorithm; inputs: x; outputs: y; "
                       "procedure: return -3/2*x+5/4");
    const auto result = egcf::InternetFeedCoordinator(store).process(
        source.assessment, source.extraction, "affine");
    REQUIRE(result.candidates.size() == 1U);
    const auto &candidate = result.candidates.front();
    REQUIRE(candidate.status == "VALIDATION_READY");
    REQUIRE(candidate.proposed_saa_ir.at("nodes").size() == 2U);
    const auto &provenance = candidate.applicability.at("translation");
    REQUIRE(provenance.at("slope") == "-3/2");
    REQUIRE(provenance.at("bias") == "5/4");
    REQUIRE(provenance.at("source_fragment_id") ==
            candidate.source_fragment_id);
    REQUIRE_NOTHROW(egcf::verify_internet_candidate_translation(
        candidate, source.extraction.fragments.front()));
    auto altered = candidate;
    altered.proposed_saa_ir["nodes"][1]["operands"][1]["constant"] = "0";
    REQUIRE_THROWS(egcf::verify_internet_candidate_translation(
        altered, source.extraction.fragments.front()));
  }
  std::filesystem::remove_all(root);
}

TEST_CASE("internet feed translates explicit scale and offset procedures") {
  using namespace statewright;
  struct Example {
    std::string procedure;
    std::string slope;
    std::string bias;
  };
  const std::vector<Example> examples = {
      {"return 2*x", "2", "0"},
      {"return -3/2 * x", "-3/2", "0"},
      {"return x+3", "1", "3"},
      {"return x - 5/4", "1", "-5/4"},
      {"return 010*x", "10", "0"},
      {"return 2/2*x", "1", "0"},
      {"return x+0", "1", "0"}};
  for (const auto &example : examples) {
    INFO(example.procedure);
    const auto root = temporary_root();
    {
      egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
      const auto source = fixture(
          store, "Affine algorithm; inputs: x; outputs: y; procedure: " +
                     example.procedure);
      const auto result = egcf::InternetFeedCoordinator(store).process(
          source.assessment, source.extraction, "abbreviated-affine");
      REQUIRE(result.candidates.size() == 1U);
      const auto &candidate = result.candidates.front();
      REQUIRE(candidate.status == "VALIDATION_READY");
      const auto &provenance = candidate.applicability.at("translation");
      REQUIRE(provenance.at("translator_version") ==
              "exact-scalar-procedure-v3");
      REQUIRE(provenance.at("slope") == example.slope);
      REQUIRE(provenance.at("bias") == example.bias);
      REQUIRE_NOTHROW(egcf::verify_internet_candidate_translation(
          candidate, source.extraction.fragments.front()));
      auto altered = candidate;
      altered.applicability["translation"]["bias"] = "999";
      REQUIRE_THROWS(egcf::verify_internet_candidate_translation(
          altered, source.extraction.fragments.front()));
      REQUIRE(store.list("algorithm-definition").empty());
      const auto repeated = egcf::InternetFeedCoordinator(store).process(
          source.assessment, source.extraction, "abbreviated-affine");
      REQUIRE(repeated.result_signature == result.result_signature);
    }
    std::filesystem::remove_all(root);
  }
}

TEST_CASE("internet diagnostics distinguish unparsed declarations from source absence") {
  using namespace statewright;
  const auto root = temporary_root();
  {
    egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
    const auto source = fixture(store,
        "HKDF-Extract(salt, IKM) -> PRK; Inputs: salt, IKM; Output: PRK");
    const auto result = egcf::InternetFeedCoordinator(store).process(
        source.assessment, source.extraction, "declaration-diagnostics");
    REQUIRE(result.candidates.size() == 1U);
    REQUIRE(result.candidates.front().status == "QUARANTINED");
    const auto &reasons = result.candidates.front().unresolved_assumptions;
    REQUIRE(std::ranges::find(reasons, "SOURCE_INPUT_DECLARATION_NOT_PARSED") != reasons.end());
    REQUIRE(result.candidates.front().semantic_outputs == std::vector<std::string>{"PRK"});
    REQUIRE(std::ranges::find(reasons, "MISSING_SEMANTIC_INPUTS") == reasons.end());
    REQUIRE(std::ranges::find(reasons, "UNSUPPORTED_PROCEDURE_SYNTAX_OR_FAMILY") != reasons.end());
  }
  std::filesystem::remove_all(root);
}

TEST_CASE("internet CSS translation binds revision units and complete context") {
  using namespace statewright;
  auto section = core::read_text(std::filesystem::path(__FILE__)
      .parent_path().parent_path() / "fixtures/css-absolute-lengths-20240322.html");
  const auto root = temporary_root();
  {
    egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
    bool accepted = true;
    SECTION("reviewed section") {}
    SECTION("altered ratio") {
      section.replace(section.find("96px"), 4, "95px");
      accepted = false;
    }
    SECTION("extra condition") {
      section += "<p>Only for positive input.</p>";
      accepted = false;
    }
    SECTION("missing context") {
      section.erase(section.find("   <p>For a CSS device"));
      accepted = false;
    }
    auto source = fixture(store, section + "<h2>Next section</h2>", "text/html");
    const auto result = egcf::InternetFeedCoordinator(store).process(
        source.assessment, source.extraction, "css-unit-conversion");
    REQUIRE(result.candidates.size() == (accepted ? 6U : 1U));
    const auto selected = std::ranges::find_if(result.candidates, [](const auto &value) {
      return value.semantic_inputs == std::vector<std::string>{"css_length_in"};
    });
    const auto &candidate = accepted ? *selected : result.candidates.front();
    REQUIRE(candidate.status == (accepted ? "VALIDATION_READY" : "QUARANTINED"));
    if (accepted) {
      const auto fragment = std::ranges::find_if(source.extraction.fragments, [&](const auto &value) {
        return value.object_id() == candidate.source_fragment_id;
      });
      REQUIRE(fragment != source.extraction.fragments.end());
      const std::map<std::string, std::string> expected = {{"CSS in", "96"}, {"CSS cm", "4800/127"},
          {"CSS mm", "480/127"}, {"CSS Q", "120/127"}, {"CSS pc", "16"}, {"CSS pt", "4/3"}};
      for (const auto &conversion : result.candidates) {
        REQUIRE(conversion.status == "VALIDATION_READY");
        REQUIRE(conversion.applicability.at("translation").at("slope") == expected.at(conversion.units.at("input").get<std::string>()));
      }
      REQUIRE(candidate.units.at("input") == "CSS in");
      REQUIRE(candidate.units.at("output") == "CSS px");
      REQUIRE_NOTHROW(egcf::verify_internet_candidate_translation(
          candidate, *fragment));
      auto changed = candidate;
      changed.units["output"] = "device pixels";
      REQUIRE_THROWS(egcf::verify_internet_candidate_translation(
          changed, *fragment));
    } else {
      REQUIRE(candidate.proposed_saa_ir.empty());
    }
    REQUIRE(store.list("algorithm-definition").empty());
  }
  std::filesystem::remove_all(root);
}

TEST_CASE("internet feed never detaches an affine procedure from section conditions") {
  using namespace statewright;
  const auto root = temporary_root();
  {
    egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
    const auto source = fixture(store,
        "<h2>Affine algorithm</h2><p>Only for positive x.</p>"
        "<p>Affine algorithm; inputs: x; outputs: y; procedure: return 2*x</p>"
        "<h2>References</h2>", "text/html");
    const auto result = egcf::InternetFeedCoordinator(store).process(
        source.assessment, source.extraction, "section-conditions");
    REQUIRE(result.candidates.size() == 1U);
    REQUIRE(result.candidates.front().status == "QUARANTINED");
    REQUIRE(result.candidates.front().proposed_saa_ir.empty());
  }
  std::filesystem::remove_all(root);
}

TEST_CASE("internet feed resumes bounded fragments across store reopen") {
  using namespace statewright;
  const auto root = temporary_root();
  FeedFixture source;
  std::string first_candidate;
  {
    egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
    source = fixture(store,
        "First algorithm; inputs: x; outputs: y; procedure: return x\n"
        "Second algorithm; inputs: a; outputs: b; procedure: return a\n");
    REQUIRE(source.extraction.fragments.size() == 2U);
    const auto first = egcf::InternetFeedCoordinator(store).process(
        source.assessment, source.extraction, "bounded", true, 1U);
    REQUIRE_FALSE(first.complete);
    REQUIRE(first.processed_fragments == 1U);
    REQUIRE(first.total_fragments == 2U);
    REQUIRE(first.candidates.size() == 1U);
    first_candidate = first.candidates.front().object_id();
    REQUIRE(first.candidates.front().status == "VALIDATION_READY");
    REQUIRE_FALSE(egcf::internet_feed_completion_outputs(source.extraction.receipt, store.list()));
  }
  {
    egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
    egcf::InternetFeedCoordinator coordinator(store);
    const auto second = coordinator.process(source.assessment, source.extraction,
                                            "bounded", true, 1U);
    REQUIRE(second.complete);
    REQUIRE(second.processed_fragments == 1U);
    REQUIRE(second.candidates.size() == 1U);
    REQUIRE(second.candidates.front().status == "VALIDATION_READY");
    REQUIRE(second.candidates.front().object_id() != first_candidate);
    REQUIRE(egcf::internet_feed_completion_outputs(source.extraction.receipt, store.list()));
    const auto head = store.event_head();
    const auto repeated = coordinator.process(source.assessment, source.extraction,
                                              "bounded", true, 1U);
    REQUIRE(repeated.complete);
    REQUIRE(repeated.processed_fragments == 0U);
    REQUIRE(store.event_head() == head);
    REQUIRE(store.list("internet-algorithm-candidate").size() == 2U);
    store.validate_projection();
  }
  std::filesystem::remove_all(root);
}
