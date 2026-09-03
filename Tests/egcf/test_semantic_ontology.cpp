#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/egcf/semantic_ontology.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>

namespace {

std::filesystem::path ontology_temporary_directory() {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  auto path = std::filesystem::temp_directory_path() /
              ("statewright-semantic-ontology-" + std::to_string(suffix));
  std::filesystem::create_directories(path);
  return path;
}

statewright::egcf::EgcfRecord semantic_evidence(std::string label,
                                                std::string group) {
  using statewright::contracts::Json;
  const Json content = {{"group", group}, {"label", label}};
  return {.object_type = "egcf-evidence",
          .payload =
              {{"algorithm_id", "semantic-test"},
               {"category", "semantic-grounding"},
               {"claim_ids", Json::array()},
               {"command_id", "semantic.qualify"},
               {"content", content},
               {"created_at", "2026-08-29T00:00:00Z"},
               {"environment", {{"suite", "saa-9"}}},
               {"independence_group", std::move(group)},
               {"limitations", Json::array()},
               {"method", "independent-semantic-review"},
               {"oracle", "semantic-test-oracle"},
               {"path", ""},
               {"producer", "human-semantic-test"},
               {"requirement_ids", Json::array()},
               {"sha256", statewright::contracts::sha256_json(
                              {{"evidence", content}})},
               {"simulated", false},
               {"source_snapshot_hash",
                statewright::contracts::sha256_json(content)},
               {"subject_id", "semantic:" + label},
               {"success", true},
               {"target", std::move(label)}}};
}

} // namespace

TEST_CASE("SAA semantic ontology persists equivalence and rebuilds projection") {
  using namespace statewright;
  const auto root = ontology_temporary_directory();
  egcf::EgcfStore egcf_store(root, STATEWRIGHT_RESOURCE_ROOT);
  const auto left_evidence = egcf_store.register_record(
      semantic_evidence("vehicle-speed", "vehicle"));
  const auto right_evidence = egcf_store.register_record(
      semantic_evidence("particle-speed", "physics"));
  const auto alignment_evidence = egcf_store.register_record(
      semantic_evidence("speed-alignment", "alignment"));

  const auto velocity = saa::LENGTH / saa::TIME;
  const auto left = saa::make_semantic_concept(
      "vehicle speed", "magnitude of vehicle translational velocity",
      "vehicle dynamics", "speed", {"road speed"}, velocity, std::nullopt,
      {left_evidence});
  const auto right = saa::make_semantic_concept(
      "translational speed", "magnitude of translational velocity",
      "mechanics", "speed", {}, velocity, std::nullopt, {right_evidence});
  const std::string falsifier =
      "equal numeric values under the same frame predict different translational displacement rates";
  const auto proposal = saa::propose_semantic_alignment(
      left, right, "EXACT_EQUIVALENT",
      "magnitude of translational velocity in a specified frame", true,
      {alignment_evidence}, {falsifier}, true);

  egcf::SemanticOntologyStore ontology(egcf_store);
  const auto assessment = saa::assess_semantic_alignment(
      ontology.evidence_resolver(), left, right, proposal,
      {saa::SemanticAlignmentFalsifierResult(
          falsifier, "SURVIVED", alignment_evidence)});
  const auto left_id = ontology.admit_concept(left);
  const auto right_id = ontology.admit_concept(right);
  REQUIRE(ontology.admit_concept(left) == left_id);
  REQUIRE(ontology.admit_alignment(assessment).starts_with(
      "semantic-alignment:sha256:"));
  const auto equivalent = ontology.equivalent_concept_ids(left_id);
  REQUIRE(std::find(equivalent.begin(), equivalent.end(), right_id) !=
          equivalent.end());
  REQUIRE(ontology.meanings_equivalent("road speed", "translational speed"));

  std::filesystem::remove(egcf_store.projection_path());
  egcf_store.rebuild_projection();
  ontology.rebuild_projection();
  REQUIRE(ontology.meanings_equivalent("vehicle speed",
                                       "translational speed"));
  std::filesystem::remove_all(root);
}

TEST_CASE("SAA semantic ontology rejects forged grounding") {
  using namespace statewright;
  const auto root = ontology_temporary_directory();
  egcf::EgcfStore egcf_store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::SemanticOntologyStore ontology(egcf_store);
  const auto forged = saa::make_semantic_concept(
      "temperature", "thermodynamic temperature", "mechanics",
      "temperature", {}, std::nullopt, std::nullopt, {"missing:evidence"});
  REQUIRE_THROWS_AS(ontology.admit_concept(forged), common::Error);
  std::filesystem::remove_all(root);
}
