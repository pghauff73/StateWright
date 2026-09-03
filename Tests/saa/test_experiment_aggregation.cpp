#include "statewright/saa/experiment_aggregation.hpp"

#include "statewright/common/error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <map>

namespace {

constexpr std::string_view baseline_ref =
    "canonical-algorithm:sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr std::string_view candidate_ref =
    "adapted-candidate:sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";

statewright::saa::ReasoningEvidenceResolver evidence_resolver() {
  using statewright::saa::ReasoningGroundingEvidence;
  std::map<std::string, ReasoningGroundingEvidence> evidence;
  for (int index = 0; index < 2; ++index) {
    for (const char prefix : {'b', 'c'}) {
      const std::string suffix = std::string(1U, prefix) +
                                 std::to_string(index);
      evidence.emplace(
          "evidence:" + suffix,
          ReasoningGroundingEvidence{
              .object_type = "egcf-evidence",
              .success = true,
              .simulated = false,
              .producer = "deterministic-saa-11-test",
              .method = "controlled-ab-experiment",
              .requirement_ids = {"benchmark"},
              .independence_group = suffix});
    }
  }
  return [evidence = std::move(evidence)](std::string_view evidence_id)
             -> std::optional<ReasoningGroundingEvidence> {
    const auto found = evidence.find(std::string(evidence_id));
    return found == evidence.end()
               ? std::nullopt
               : std::optional<ReasoningGroundingEvidence>(found->second);
  };
}

std::pair<statewright::saa::AlgorithmABExperimentDesign,
          std::vector<statewright::saa::AlgorithmABExperimentResult>>
repeated_results() {
  using namespace statewright::saa;
  const auto experiment = make_ab_experiment_design(
      std::string(baseline_ref), std::string(candidate_ref),
      std::string(64U, 'c'),
      {{.name = "error",
        .direction = "LOWER_IS_BETTER",
        .minimum_material_effect = mpq_class(1, 100)}},
      {"bounded"}, {"benchmark"}, 10);
  std::vector<AlgorithmABExperimentResult> results;
  for (int index = 0; index < 2; ++index) {
    const auto baseline = make_variant_observation(
        experiment, std::string(baseline_ref),
        {{"error", mpq_class(10, 100)}},
        {"evidence:b" + std::to_string(index)}, {{"bounded", true}}, 20,
        true);
    const auto candidate = make_variant_observation(
        experiment, std::string(candidate_ref),
        {{"error", mpq_class(5 - index, 100)}},
        {"evidence:c" + std::to_string(index)}, {{"bounded", true}}, 20,
        true);
    results.push_back(qualify_ab_experiment(
        evidence_resolver(), experiment, baseline, candidate, true));
  }
  return {experiment, std::move(results)};
}

} // namespace

TEST_CASE("SAA repeated experiments qualify sustained improvement") {
  using namespace statewright::saa;
  auto [experiment, results] = repeated_results();
  REQUIRE(experiment.design_signature ==
          "794e9050de3ad778a7148796321fc0c61ca6db2291feedc81c4b8ed820a1c667");
  REQUIRE(results.at(0).result_signature ==
          "941b33a1edad1800f8838f8bff9843777618f25e7b255a61fbc04613e898f210");
  REQUIRE(results.at(1).result_signature ==
          "b0371bd25766023a7203e2a281cebe7ba7187b848644a28f98785be71d7fbb5b");
  const auto aggregate = aggregate_repeated_experiments(results);
  REQUIRE(aggregate.status ==
          "SUSTAINED_CANDIDATE_IMPROVEMENT_QUALIFIED");
  REQUIRE(aggregate.sustained_improvement_qualified);
  REQUIRE(aggregate.independence_groups ==
          std::vector<std::string>{"b0", "b1", "c0", "c1"});
  REQUIRE(aggregate.metric_evidence.size() == 1U);
  REQUIRE(aggregate.metric_evidence.front().mean_signed_improvement ==
          mpq_class(11, 200));
  REQUIRE(aggregate.aggregate_signature ==
          "ae0664b55408f19fd5e2e51be3de118a0d5bf14ccc391b50e7d883bac998c30c");
}

TEST_CASE("SAA repeated experiments require independent groups") {
  using namespace statewright::saa;
  auto [experiment, results] = repeated_results();
  static_cast<void>(experiment);
  const auto aggregate = aggregate_repeated_experiments(results, 2, 5);
  REQUIRE(aggregate.status ==
          "REPEATED_EVIDENCE_INDEPENDENCE_INSUFFICIENT");
  REQUIRE_FALSE(aggregate.sustained_improvement_qualified);
  REQUIRE(aggregate.aggregate_signature ==
          "e4494f946b750a6c15cf00797f0504a39923db521cb2c2a91c5988fd0f646bf3");
}

TEST_CASE("SAA repeated experiments reject duplicate result counting") {
  using namespace statewright::saa;
  auto [experiment, results] = repeated_results();
  static_cast<void>(experiment);
  REQUIRE_THROWS_AS(
      aggregate_repeated_experiments({results.front(), results.front()}),
      statewright::common::Error);
}
