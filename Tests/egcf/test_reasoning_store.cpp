#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/egcf/reasoning_store.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>

namespace {

std::filesystem::path reasoning_store_temporary_directory() {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  auto path = std::filesystem::temp_directory_path() /
              ("statewright-reasoning-store-" + std::to_string(suffix));
  std::filesystem::create_directories(path);
  return path;
}

statewright::saa::CanonicalReasoningAlgorithm store_algorithm() {
  using namespace statewright::saa;
  return canonicalize_reasoning_algorithm(
      {.name = "falsification-first",
       .inputs = {"problem evidence"},
       .outputs = {"qualified conclusion"},
       .nodes = {{.node_id = "observe",
                  .operator_name = "OBSERVE",
                  .semantic_inputs = {"problem evidence"},
                  .semantic_outputs = {"candidate evidence"},
                  .public_claim_ids = {},
                  .evidence_requirements = {"source snapshot"},
                  .assumptions = {},
                  .falsifiers = {},
                  .description = ""},
                 {.node_id = "test",
                  .operator_name = "FALSIFY",
                  .semantic_inputs = {"candidate evidence"},
                  .semantic_outputs = {"surviving claim"},
                  .public_claim_ids = {},
                  .evidence_requirements = {},
                  .assumptions = {},
                  .falsifiers = {"counterexample exists"},
                  .description = ""},
                 {.node_id = "verify",
                  .operator_name = "VERIFY",
                  .semantic_inputs = {"surviving claim"},
                  .semantic_outputs = {"qualified conclusion"},
                  .public_claim_ids = {},
                  .evidence_requirements = {"independent verification"},
                  .assumptions = {},
                  .falsifiers = {},
                  .description = ""}},
       .edges = {{"observe", "test", "NEXT", ""},
                 {"test", "verify", "NEXT", ""}},
       .invariants = {
           "unverified claims do not become facts without evidence"},
       .termination = {"bounded evidence decision",
                       "claim verified or falsified or budget exhausted", 8},
       .applicability = {"evidence-backed factual reasoning"}});
}

statewright::egcf::EgcfRecord reasoning_evidence(
    std::string target, std::string requirement, std::string group,
    std::string producer) {
  using statewright::contracts::Json;
  const Json content = {{"requirement", requirement}, {"target", target}};
  return {.object_type = "egcf-evidence",
          .payload =
              {{"algorithm_id", "reasoning-test"},
               {"category", "reasoning-grounding"},
               {"claim_ids", Json::array()},
               {"command_id", "reasoning.qualify"},
               {"content", content},
               {"created_at", "2026-09-02T00:00:00Z"},
               {"environment", {{"suite", "saa-8"}}},
               {"independence_group", std::move(group)},
               {"limitations", Json::array()},
               {"method", "direct-observation"},
               {"oracle", "reasoning-test-oracle"},
               {"path", ""},
               {"producer", std::move(producer)},
               {"requirement_ids", Json::array({std::move(requirement)})},
               {"sha256", statewright::contracts::sha256_json(content)},
               {"simulated", false},
               {"source_snapshot_hash",
                statewright::contracts::sha256_json({{"source", content}})},
               {"subject_id", "reasoning:" + target},
               {"success", true},
               {"target", std::move(target)}}};
}

} // namespace

TEST_CASE("SAA canonical reasoning store admits reuses and rebuilds") {
  using namespace statewright;
  const auto root = reasoning_store_temporary_directory();
  egcf::EgcfStore egcf_store(root, STATEWRIGHT_RESOURCE_ROOT);
  const auto source_id = egcf_store.register_record(reasoning_evidence(
      "source", "source snapshot", "source", "deterministic-reader"));
  const auto review_id = egcf_store.register_record(reasoning_evidence(
      "review", "independent verification", "verification",
      "human-reviewer"));
  egcf::CanonicalReasoningStore store(egcf_store);
  const auto algorithm = store_algorithm();
  const auto outcome = saa::make_reasoning_execution_outcome(
      algorithm, "run-1", {"qualified conclusion"}, {review_id, source_id},
      {{"unverified claims do not become facts without evidence", true}},
      {{"counterexample exists", "SURVIVED"}}, true, 3, true, true);
  const auto qualification =
      saa::qualify_reasoning_outcome(store.evidence_resolver(), algorithm,
                                     outcome);
  const auto first = store.admit(algorithm, qualification);
  REQUIRE(first.status == "ADMITTED_NEW_CANONICAL_REASONING");
  REQUIRE(first.reasoning_id ==
          "canonical-reasoning:sha256:3646a849a19655a002a86a33c0c11b04b961ddc76d3af2bc2959963f9db676c7");
  REQUIRE(first.qualification_id ==
          "reasoning-qualification:sha256:" +
              qualification.qualification_signature);
  REQUIRE(first.store_generation == 1);
  REQUIRE(store.load_algorithm(first.reasoning_id)
              .canonical_reasoning_signature ==
          algorithm.canonical_reasoning_signature);

  const auto second = store.admit(algorithm, qualification);
  REQUIRE(second.status == "REUSED_EXISTING_CANONICAL_REASONING");
  REQUIRE(second.store_generation == 1);
  REQUIRE(store.list().size() == 1U);
  REQUIRE(store.qualifications(first.reasoning_id).size() == 1U);

  std::filesystem::remove(egcf_store.projection_path());
  egcf_store.rebuild_projection();
  store.rebuild_projection();
  REQUIRE(store.current_generation() == 1);
  REQUIRE(store.lookup(algorithm).status ==
          "REASONING_EQUIVALENT_ALREADY_STORED");
  std::filesystem::remove_all(root);
}

TEST_CASE("SAA canonical reasoning store rejects unqualified execution") {
  using namespace statewright;
  const auto root = reasoning_store_temporary_directory();
  egcf::EgcfStore egcf_store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::CanonicalReasoningStore store(egcf_store);
  const auto algorithm = store_algorithm();
  saa::ReasoningOutcomeQualification forged;
  forged.canonical_reasoning_signature =
      algorithm.canonical_reasoning_signature;
  forged.status = "UNQUALIFIED_REASONING_EVIDENCE";
  forged.qualification_signature = std::string(64U, '0');
  REQUIRE_THROWS_AS(store.admit(algorithm, forged), common::Error);
  std::filesystem::remove_all(root);
}
