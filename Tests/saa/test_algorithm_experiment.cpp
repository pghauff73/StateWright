#include "statewright/saa/algorithm_experiment.hpp"

#include "statewright/common/error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <map>

namespace {

constexpr std::string_view baseline_ref =
    "canonical-algorithm:sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr std::string_view candidate_ref =
    "adapted-candidate:sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";

statewright::saa::AlgorithmABExperimentDesign design() {
  using namespace statewright::saa;
  return make_ab_experiment_design(
      std::string(baseline_ref), std::string(candidate_ref),
      std::string(64U, 'c'),
      {{.name = "error",
        .direction = "LOWER_IS_BETTER",
        .minimum_material_effect = mpq_class(1, 100)},
       {.name = "throughput",
        .direction = "HIGHER_IS_BETTER",
        .minimum_material_effect = mpq_class(1)}},
      {"bounded"}, {"benchmark"}, 10);
}

statewright::saa::ReasoningEvidenceResolver evidence_resolver() {
  using statewright::saa::ReasoningGroundingEvidence;
  const std::map<std::string, ReasoningGroundingEvidence> evidence = {
      {"evidence:baseline",
       {.object_type = "egcf-evidence",
        .success = true,
        .simulated = false,
        .producer = "deterministic-saa-11-test",
        .method = "controlled-ab-experiment",
        .requirement_ids = {"benchmark"},
        .independence_group = "baseline"}},
      {"evidence:candidate",
       {.object_type = "egcf-evidence",
        .success = true,
        .simulated = false,
        .producer = "deterministic-saa-11-test",
        .method = "controlled-ab-experiment",
        .requirement_ids = {"benchmark"},
        .independence_group = "candidate"}}};
  return [evidence](std::string_view evidence_id)
             -> std::optional<ReasoningGroundingEvidence> {
    const auto found = evidence.find(std::string(evidence_id));
    if (found == evidence.end()) {
      return std::nullopt;
    }
    return found->second;
  };
}

statewright::saa::AlgorithmVariantObservation baseline_observation(
    const statewright::saa::AlgorithmABExperimentDesign &value) {
  return statewright::saa::make_variant_observation(
      value, std::string(baseline_ref),
      {{"error", mpq_class(10, 100)}, {"throughput", mpq_class(100)}},
      {"evidence:baseline"}, {{"bounded", true}}, 20, true);
}

statewright::saa::AlgorithmVariantObservation candidate_observation(
    const statewright::saa::AlgorithmABExperimentDesign &value,
    int throughput = 103) {
  return statewright::saa::make_variant_observation(
      value, std::string(candidate_ref),
      {{"error", mpq_class(5, 100)}, {"throughput", mpq_class(throughput)}},
      {"evidence:candidate"}, {{"bounded", true}}, 20, true);
}

} // namespace

TEST_CASE("SAA controlled experiment design and observations are exact") {
  const auto experiment = design();
  REQUIRE(experiment.design_signature ==
          "1e270e3d42447900858e2db72c4b9f176e7a6d90c949373d2a79a34d59b87b9d");
  const auto baseline = baseline_observation(experiment);
  const auto candidate = candidate_observation(experiment);
  REQUIRE(baseline.observation_signature ==
          "f5c7e352dcb4581d70ae551531e2955213081dbc65200a1cec68aa46d38826db");
  REQUIRE(candidate.observation_signature ==
          "a2d1969fa75e54d9f243fd0f24d8f322e9eba4eebd0a634e2a828d149843a834");
}

TEST_CASE("SAA controlled experiment qualifies grounded improvement") {
  using namespace statewright::saa;
  const auto experiment = design();
  const auto result = qualify_ab_experiment(
      evidence_resolver(), experiment, baseline_observation(experiment),
      candidate_observation(experiment), true);
  REQUIRE(result.status == "CANDIDATE_IMPROVEMENT_QUALIFIED");
  REQUIRE(result.candidate_improvement_qualified);
  REQUIRE(result.qualification_required_before_canonical_reuse);
  REQUIRE(result.evidence_requirement_coverage_bp == 10000);
  REQUIRE(result.grounded_evidence_ids ==
          std::vector<std::string>{"evidence:baseline",
                                   "evidence:candidate"});
  REQUIRE(result.independence_groups ==
          std::vector<std::string>{"baseline", "candidate"});
  REQUIRE(result.metric_comparisons.size() == 2U);
  REQUIRE(result.metric_comparisons.front().signed_improvement ==
          mpq_class(1, 20));
  REQUIRE(result.result_signature ==
          "dd0951f5d5bfd74d52efa2162332de2d3644fb64d4b99787b71345c36336c830");
}

TEST_CASE("SAA controlled experiment refuses tradeoff and missing review") {
  using namespace statewright::saa;
  const auto experiment = design();
  const auto baseline = baseline_observation(experiment);
  const auto tradeoff_candidate = candidate_observation(experiment, 95);
  REQUIRE(tradeoff_candidate.observation_signature ==
          "9c9543a180ccfcc8bb1fc1826d73abd4e441aad3a5cb91b5059500a534f37b2d");
  const auto tradeoff = qualify_ab_experiment(
      evidence_resolver(), experiment, baseline, tradeoff_candidate, true);
  REQUIRE(tradeoff.status == "EXPERIMENT_TRADEOFF_UNRESOLVED");
  REQUIRE_FALSE(tradeoff.candidate_improvement_qualified);
  REQUIRE(tradeoff.result_signature ==
          "0604036b0f96a81c0d7ca43083ddf8fcec987bb39bed26eac3de03bf0736055e");

  const auto review_required = qualify_ab_experiment(
      evidence_resolver(), experiment, baseline,
      candidate_observation(experiment), false);
  REQUIRE(review_required.status == "EXPERIMENT_REVIEW_REQUIRED");
  REQUIRE(review_required.result_signature ==
          "28dc9997a254c516eb031a38d751ef88a2563b5c46df6bd2ac134c147a903367");
}

TEST_CASE("SAA controlled experiment rejects unregistered evidence") {
  using namespace statewright::saa;
  const auto experiment = design();
  auto candidate = candidate_observation(experiment);
  candidate.evidence_ids = {"evidence:missing"};
  REQUIRE_THROWS_AS(
      qualify_ab_experiment(evidence_resolver(), experiment,
                            baseline_observation(experiment), candidate, true),
      statewright::common::Error);
}
