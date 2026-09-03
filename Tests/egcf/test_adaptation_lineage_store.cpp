#include "statewright/contracts/hash.hpp"
#include "statewright/egcf/adaptation_lineage_store.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view base_ref =
    "canonical-algorithm:sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

std::filesystem::path temporary_directory() {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  auto path = std::filesystem::temp_directory_path() /
              ("statewright-adaptation-lineage-" + std::to_string(suffix));
  std::filesystem::create_directories(path);
  return path;
}

statewright::saa::AdaptationStep adaptation_step(int index,
                                                  std::string dimension) {
  const statewright::contracts::Json material =
      {{"base_algorithm_id", base_ref},
       {"component", "MATHEMATICAL_ALGORITHM"},
       {"dimension", dimension},
       {"index", index}};
  return {.index = index,
          .component = "MATHEMATICAL_ALGORITHM",
          .dimension = std::move(dimension),
          .base_algorithm_id = std::string(base_ref),
          .current_contract = "source contract",
          .target_contract = "target contract",
          .proposed_change = {{"dimension", material.at("dimension")}},
          .step_signature =
              statewright::contracts::sha256_json(material)};
}

statewright::egcf::EgcfRecord evidence(std::string group) {
  using statewright::contracts::Json;
  const Json content = {{"group", group}, {"requirement", "benchmark"}};
  return {.object_type = "egcf-evidence",
          .payload =
              {{"algorithm_id", "adaptation-lineage-fixture"},
               {"category", "qualification"},
               {"claim_ids", Json::array()},
               {"command_id", "assurance.generate@1"},
               {"content", content},
               {"created_at", "2026-09-02T00:00:00Z"},
               {"environment", {{"suite", "adaptation-lineage"}}},
               {"independence_group", std::move(group)},
               {"limitations", Json::array()},
               {"method", "controlled-ab-experiment"},
               {"oracle", "deterministic-test-oracle"},
               {"path", ""},
               {"producer", "deterministic-saa-11-store-test"},
               {"requirement_ids", Json::array({"benchmark"})},
               {"sha256", statewright::contracts::sha256_json(content)},
               {"simulated", false},
               {"source_snapshot_hash",
                statewright::contracts::sha256_json({{"source", content}})},
               {"subject_id", "adaptation-lineage:test"},
               {"success", true},
               {"target", "store"}}};
}

} // namespace

TEST_CASE("EGCF adaptation lineage projection rebuilds immutable records") {
  using namespace statewright;
  const auto root = temporary_directory();
  egcf::EgcfStore egcf_store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::AdaptationLineageStore store(egcf_store);

  const auto step_one = adaptation_step(0, "DYNAMICS_CONTRACT");
  const auto candidate_one = saa::create_adapted_candidate(
      step_one, {{"dimension", "DYNAMICS_CONTRACT"}, {"target", "v2"}});
  const auto [first_ref, first_edge_ref] =
      store.register_candidate(candidate_one, step_one, std::string(64U, 'b'));

  const auto step_two = adaptation_step(1, "BOUNDARY_CONTRACT");
  const auto candidate_two = saa::create_adapted_candidate(
      step_two, {{"dimension", "BOUNDARY_CONTRACT"}, {"target", "v3"}},
      candidate_one.candidate_signature);
  const auto [final_ref, second_edge_ref] =
      store.register_candidate(candidate_two, step_two, std::string(64U, 'b'));

  REQUIRE(first_ref ==
          "adapted-candidate:sha256:34b821162e6733b6956482efba5429d1d710f8755c94926f0b7003bdd14cf30d");
  REQUIRE(final_ref ==
          "adapted-candidate:sha256:82c4ce10f576b1a7e69be14875bb574e44c94c7621f979f6323d375539f92da4");
  REQUIRE(first_edge_ref.starts_with("adaptation-edge:sha256:"));
  REQUIRE(second_edge_ref.starts_with("adaptation-edge:sha256:"));
  REQUIRE(store.ancestors(final_ref) ==
          std::vector<std::string>{first_ref, std::string(base_ref)});
  REQUIRE(store.descends_from(final_ref, base_ref));
  REQUIRE(store.children(first_ref) == std::vector<std::string>{final_ref});

  const auto plan = saa::make_multistep_evolution_plan(
      store, final_ref, {"bounded"},
      {"DYNAMICS_CONTRACT", "BOUNDARY_CONTRACT"});
  REQUIRE(plan.plan_signature ==
          "b9fcee015f47bdf2c36296dd376b24d74ebb63e03f084d4c58d09bd46b043798");

  const auto baseline_evidence =
      egcf_store.register_record(evidence("baseline"));
  const auto candidate_evidence =
      egcf_store.register_record(evidence("candidate"));
  const auto design = saa::make_ab_experiment_design(
      std::string(base_ref), final_ref, std::string(64U, 'c'),
      {{.name = "error",
        .direction = "LOWER_IS_BETTER",
        .minimum_material_effect = mpq_class(1, 100)}},
      {"bounded"}, {"benchmark"}, 10);
  const auto baseline = saa::make_variant_observation(
      design, std::string(base_ref), {{"error", mpq_class(10, 100)}},
      {baseline_evidence}, {{"bounded", true}}, 20, true);
  const auto adapted = saa::make_variant_observation(
      design, final_ref, {{"error", mpq_class(5, 100)}},
      {candidate_evidence}, {{"bounded", true}}, 20, true);
  const auto result = saa::qualify_ab_experiment(
      store.evidence_resolver(), design, baseline, adapted, true);
  const auto experiment_ref = store.register_experiment_design(design);
  const auto result_ref = store.register_experiment_result(result);
  const auto promotion = saa::make_adaptation_promotion(
      final_ref, "canonical-algorithm:sha256:" + std::string(64U, 'f'),
      result.result_signature, {baseline_evidence, candidate_evidence});
  const auto promotion_ref = store.register_promotion(promotion);

  REQUIRE(experiment_ref.starts_with("adaptation-experiment:sha256:"));
  REQUIRE(result_ref.starts_with("adaptation-experiment-result:sha256:"));
  REQUIRE(promotion_ref.starts_with("adaptation-promotion:sha256:"));
  REQUIRE(result.candidate_improvement_qualified);

  store.rebuild_projection();
  REQUIRE(store.candidates().size() == 2U);
  REQUIRE(store.lineage_edges().size() == 2U);
  REQUIRE(store.promotions(final_ref).size() == 1U);
  REQUIRE(store.experiments().size() == 1U);
  REQUIRE(store.experiment_results(design.design_signature).size() == 1U);
  REQUIRE(store.ancestors(final_ref) ==
          std::vector<std::string>{first_ref, std::string(base_ref)});
  std::filesystem::remove_all(root);
}
