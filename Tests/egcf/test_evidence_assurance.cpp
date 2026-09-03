#include "statewright/egcf/assurance.hpp"

#include "statewright/common/error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>

namespace {

std::filesystem::path temporary_root() {
  const auto root =
      std::filesystem::temp_directory_path() /
      ("statewright-evidence-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(root);
  return root;
}

} // namespace

TEST_CASE("EGCF evidence gates reuse simulation duplicates and conflicts") {
  const auto root = temporary_root();
  statewright::egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  statewright::egcf::EvidenceManager evidence(store);
  statewright::egcf::Ieps ieps(evidence);

  const auto first = ieps.oracle("subject", "unit", "test", "unit", true, 0,
                                 "unit");
  const auto second = ieps.oracle("subject", "integration", "test",
                                  "integration", true, 0, "integration");
  const auto simulated = evidence.collect(
      {.subject_id = "subject",
       .content = {{"passed", true}},
       .category = "test",
       .producer = "deterministic-simulation",
       .method = "simulation",
       .source_snapshot_hash = "snapshot",
       .oracle = "unit",
       .requirement_ids = {first},
       .success = true,
       .independence_group = "unit",
       .simulated = true});
  REQUIRE(simulated.starts_with("egcf-evidence:sha256:"));
  REQUIRE(evidence.coverage("subject").at("missing_mandatory").size() == 2U);

  static_cast<void>(evidence.collect(
      {.subject_id = "subject",
       .content = {{"passed", true}},
       .category = "test",
       .producer = "deterministic-unit",
       .method = "unit",
       .source_snapshot_hash = "snapshot",
       .oracle = "unit",
       .requirement_ids = {first},
       .success = true,
       .independence_group = "unit"}));
  static_cast<void>(evidence.collect(
      {.subject_id = "subject",
       .content = {{"passed", true}},
       .category = "test",
       .producer = "deterministic-integration",
       .method = "integration",
       .source_snapshot_hash = "snapshot",
       .oracle = "integration",
       .requirement_ids = {second},
       .success = true,
       .independence_group = "integration"}));
  REQUIRE(evidence.coverage("subject").at("missing_mandatory").empty());

  static_cast<void>(evidence.collect(
      {.subject_id = "subject",
       .content = {{"passed", false}},
       .category = "test",
       .producer = "deterministic-refutation",
       .method = "integration",
       .source_snapshot_hash = "snapshot",
       .oracle = "integration",
       .requirement_ids = {second},
       .success = false,
       .independence_group = "independent-refutation"}));
  REQUIRE_FALSE(evidence.conflicts("subject").empty());
  REQUIRE_FALSE(ieps.qualify("subject").at("qualified").get<bool>());

  for (const auto &producer : {"deterministic-one", "deterministic-two"}) {
    static_cast<void>(evidence.collect(
        {.subject_id = "duplicate",
         .content = {{"same", true}},
         .category = "test",
         .producer = producer,
         .method = "unit",
         .source_snapshot_hash = "snapshot",
         .oracle = "unit",
         .success = true,
         .independence_group = "shared"}));
  }
  const auto confidence = evidence.confidence("duplicate");
  REQUIRE(confidence.conclusion == "BLOCKED");
  REQUIRE(std::ranges::find(confidence.blocking_gaps,
                            "duplicate evidence content") !=
          confidence.blocking_gaps.end());
  REQUIRE(std::ranges::find(confidence.blocking_gaps,
                            "evidence independence groups are reused") !=
          confidence.blocking_gaps.end());

  const auto mutation = statewright::egcf::Ieps::mutation(
      {{{"name", "keep approval"}, {"detected", true}},
       {{"name", "remove approval"}, {"detected", false}}});
  REQUIRE(mutation.at("survivors").size() == 1U);
  REQUIRE(mutation.at("mutation_score") == 0.5);
  std::filesystem::remove_all(root);
}

TEST_CASE("EGCF governance remains append-only and assurance preserves gaps") {
  const auto root = temporary_root();
  statewright::egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  statewright::egcf::EvidenceManager evidence(store);
  statewright::egcf::InvariantManager invariants(store);
  statewright::egcf::DecisionManager decisions(store);
  statewright::egcf::AssuranceManager assurance(store, evidence, invariants,
                                                decisions);

  const auto evidence_id = evidence.collect(
      {.subject_id = "governance",
       .content = {{"validated", true}},
       .category = "test",
       .producer = "deterministic-validator",
       .method = "validator",
       .source_snapshot_hash = "snapshot",
       .success = true});
  static_cast<void>(invariants.register_invariant(
      "parser-stability", "parser accepts legacy inputs", {"src/**"},
      {{"kind", "test"}}, {evidence_id}, "a legacy input is rejected",
      "human-a"));
  static_cast<void>(invariants.register_invariant(
      "parser-stability", "parser rejects legacy inputs", {"src/**"},
      {{"kind", "test"}}, {evidence_id}, "a legacy input is accepted",
      "human-b"));
  REQUIRE_FALSE(invariants.conflicts().empty());

  const auto first = decisions.create("Which parser?", {"a", "b"}, "a",
                                      "first", {evidence_id}, {}, "team",
                                      {"src/**"});
  const auto second = decisions.create("Which parser?", {"a", "b"}, "b",
                                       "second", {evidence_id}, {}, "team",
                                       {"src/**"});
  static_cast<void>(decisions.supersede(first, "a", "activate a",
                                        {evidence_id}, "human-a"));
  static_cast<void>(decisions.supersede(second, "b", "activate b",
                                        {evidence_id}, "human-b"));
  REQUIRE_FALSE(decisions.conflicts().empty());
  REQUIRE_THROWS_AS(decisions.create("unsafe", {"yes"}, "yes", "model",
                                      {}, {}, "model", {"**"}, true, "model"),
                    statewright::common::Error);

  const auto result = assurance.generate(
      "unproven", "candidate is safe", statewright::contracts::Json::object(),
      {{"satisfied", false}}, {{"required", true}, {"covered", false}},
      {"no runtime evidence"});
  REQUIRE(result.conclusion == "NOT_SUPPORTED");
  REQUIRE_FALSE(result.gaps.empty());
  REQUIRE(result.approval_facts.at("satisfied") == false);
  REQUIRE(result.object_id().starts_with("assurance-case:sha256:"));
  std::filesystem::remove_all(root);
}
