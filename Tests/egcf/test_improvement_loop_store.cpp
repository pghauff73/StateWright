#include "statewright/contracts/hash.hpp"
#include "statewright/egcf/improvement_loop_store.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view base_ref =
    "canonical-algorithm:sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

std::filesystem::path temporary_directory() {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  auto path = std::filesystem::temp_directory_path() /
              ("statewright-improvement-loop-" + std::to_string(suffix));
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

statewright::egcf::EgcfRecord evidence(std::string requirement,
                                       std::string group) {
  using statewright::contracts::Json;
  const Json content = {{"group", group}, {"requirement", requirement}};
  return {.object_type = "egcf-evidence",
          .payload =
              {{"algorithm_id", "improvement-loop-fixture"},
               {"category", "qualification"},
               {"claim_ids", Json::array()},
               {"command_id", "assurance.generate@1"},
               {"content", content},
               {"created_at", "2026-09-02T00:00:00Z"},
               {"environment", {{"suite", "improvement-loop"}}},
               {"independence_group", std::move(group)},
               {"limitations", Json::array()},
               {"method", "controlled-ab-experiment"},
               {"oracle", "deterministic-test-oracle"},
               {"path", ""},
               {"producer", "deterministic-saa-12-loop-store-test"},
               {"requirement_ids", Json::array({std::move(requirement)})},
               {"sha256", statewright::contracts::sha256_json(content)},
               {"simulated", false},
               {"source_snapshot_hash",
                statewright::contracts::sha256_json({{"source", content}})},
               {"subject_id", "improvement-loop:test"},
               {"success", true},
               {"target", "store"}}};
}

std::pair<std::string, std::string>
register_two_step_lineage(statewright::egcf::AdaptationLineageStore &store) {
  const auto first_step = adaptation_step(0, "DYNAMICS_CONTRACT");
  const auto first_candidate = statewright::saa::create_adapted_candidate(
      first_step,
      {{"dimension", "DYNAMICS_CONTRACT"}, {"target", "v2"}});
  const auto first = store.register_candidate(first_candidate, first_step,
                                               std::string(64U, 'b'));
  const auto second_step = adaptation_step(1, "BOUNDARY_CONTRACT");
  const auto second_candidate = statewright::saa::create_adapted_candidate(
      second_step,
      {{"dimension", "BOUNDARY_CONTRACT"}, {"target", "v3"}},
      first_candidate.candidate_signature);
  const auto second = store.register_candidate(second_candidate, second_step,
                                                std::string(64U, 'b'));
  return {first.first, second.first};
}

} // namespace

TEST_CASE("EGCF improvement loop rebuilds a closed immutable ledger") {
  using namespace statewright;
  const auto root = temporary_directory();
  egcf::EgcfStore egcf_store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::AdaptationLineageStore lineage_store(egcf_store);
  const auto [first_ref, final_ref] =
      register_two_step_lineage(lineage_store);
  egcf::ImprovementLoopStore loop_store(egcf_store, lineage_store);

  const auto plan = saa::make_multistep_evolution_plan(
      lineage_store, final_ref, {"bounded"},
      {"DYNAMICS_CONTRACT", "BOUNDARY_CONTRACT"});
  REQUIRE(loop_store.register_evolution_plan(plan).starts_with(
      "evolution-plan:sha256:"));

  std::vector<saa::EvolutionStepQualification> qualifications;
  for (std::size_t index = 0; index < plan.steps.size(); ++index) {
    const auto evidence_id = egcf_store.register_record(
        evidence("invariant", "evolution-" + std::to_string(index)));
    const auto qualification = saa::qualify_evolution_step(
        loop_store.evidence_resolver(), plan, plan.steps[index].candidate_ref,
        {{"bounded", true}}, {evidence_id}, true);
    REQUIRE(loop_store.register_step_qualification(qualification).starts_with(
        "evolution-step-qualification:sha256:"));
    qualifications.push_back(qualification);
  }
  const auto assessment = saa::assess_multistep_evolution(plan, qualifications);
  REQUIRE(assessment.evolution_qualified);
  REQUIRE(loop_store.register_evolution_assessment(assessment).starts_with(
      "evolution-assessment:sha256:"));

  const auto design = saa::make_ab_experiment_design(
      std::string(base_ref), final_ref, std::string(64U, 'c'),
      {{.name = "error",
        .direction = "LOWER_IS_BETTER",
        .minimum_material_effect = mpq_class(1, 100)}},
      {"bounded"}, {"benchmark"}, 10);
  static_cast<void>(lineage_store.register_experiment_design(design));
  std::vector<saa::AlgorithmABExperimentResult> results;
  std::vector<std::string> promotion_evidence;
  for (int index = 0; index < 2; ++index) {
    const auto baseline_evidence = egcf_store.register_record(
        evidence("benchmark", "baseline-" + std::to_string(index)));
    const auto candidate_evidence = egcf_store.register_record(
        evidence("benchmark", "candidate-" + std::to_string(index)));
    promotion_evidence.push_back(baseline_evidence);
    promotion_evidence.push_back(candidate_evidence);
    const auto baseline = saa::make_variant_observation(
        design, std::string(base_ref), {{"error", mpq_class(10, 100)}},
        {baseline_evidence}, {{"bounded", true}}, 20, true);
    const auto candidate = saa::make_variant_observation(
        design, final_ref, {{"error", mpq_class(5, 100)}},
        {candidate_evidence}, {{"bounded", true}}, 20, true);
    const auto result = saa::qualify_ab_experiment(
        loop_store.evidence_resolver(), design, baseline, candidate, true);
    REQUIRE(result.candidate_improvement_qualified);
    static_cast<void>(lineage_store.register_experiment_result(result));
    results.push_back(result);
  }
  const auto aggregate = saa::aggregate_repeated_experiments(results);
  REQUIRE(aggregate.sustained_improvement_qualified);
  REQUIRE(loop_store.register_experiment_aggregate(aggregate).starts_with(
      "experiment-aggregate:sha256:"));

  const std::string promoted_ref =
      "canonical-algorithm:sha256:" + std::string(64U, 'f');
  const auto promotion = saa::make_adaptation_promotion(
      final_ref, promoted_ref, aggregate.aggregate_signature,
      promotion_evidence);
  const auto promotion_ref = lineage_store.register_promotion(promotion);

  const saa::IntelligenceImprovementDecision decision = {
      .phase = "CLOSED",
      .status = "CLOSED_LOOP_IMPROVEMENT_VERIFIED",
      .next_action = "STOP",
      .terminal = true,
      .permitted_actions = {},
      .blocking_reasons = {},
      .selected_mathematical_algorithm_id = std::string(base_ref),
      .selected_reasoning_id = std::nullopt,
      .candidate_ref = final_ref,
      .promoted_canonical_algorithm_ref = promoted_ref,
      .retrieval_receipt_signature = std::string(64U, '1'),
      .explanation_signature = std::string(64U, '2'),
      .adaptation_plan_signature = std::string(64U, '3'),
      .evolution_assessment_signature = assessment.assessment_signature,
      .experiment_aggregate_signature = aggregate.aggregate_signature,
      .promotion_ref = promotion_ref,
      .post_promotion_receipt_signature = std::string(64U, '4'),
      .decision_signature = std::string(64U, '5')};
  REQUIRE(loop_store.register_loop_decision(decision).starts_with(
      "improvement-loop-decision:sha256:"));

  loop_store.rebuild_projection();
  REQUIRE(loop_store.aggregates().size() == 1U);
  REQUIRE(loop_store.decisions().size() == 1U);
  REQUIRE(loop_store.decisions().front().at("payload").at("status") ==
          "CLOSED_LOOP_IMPROVEMENT_VERIFIED");
  REQUIRE(lineage_store.ancestors(final_ref) ==
          std::vector<std::string>{first_ref, std::string(base_ref)});
  std::filesystem::remove_all(root);
}
