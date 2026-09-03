#include "statewright/saa/adaptation_lineage.hpp"
#include "statewright/saa/multistep_evolution.hpp"

#include "statewright/contracts/hash.hpp"

#include <catch2/catch_test_macros.hpp>

#include <map>

namespace {

constexpr std::string_view base_ref =
    "canonical-algorithm:sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

statewright::saa::AdaptationStep adaptation_step(
    int index, std::string dimension) {
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

class LineageCatalog final : public statewright::saa::AdaptationLineageCatalog {
public:
  LineageCatalog() {
    using namespace statewright::saa;
    const auto step_one = adaptation_step(0, "DYNAMICS_CONTRACT");
    const auto candidate_one = create_adapted_candidate(
        step_one,
        {{"dimension", "DYNAMICS_CONTRACT"}, {"target", "v2"}});
    const auto edge_one = make_adaptation_lineage_edge(
        candidate_one, step_one, std::string(64U, 'b'));
    first_ref_ = edge_one.child_ref;

    const auto step_two = adaptation_step(1, "BOUNDARY_CONTRACT");
    const auto candidate_two = create_adapted_candidate(
        step_two,
        {{"dimension", "BOUNDARY_CONTRACT"}, {"target", "v3"}},
        candidate_one.candidate_signature);
    const auto edge_two = make_adaptation_lineage_edge(
        candidate_two, step_two, std::string(64U, 'b'));
    final_ref_ = edge_two.child_ref;

    candidates_[first_ref_] = {{"payload", to_json(candidate_one)}};
    candidates_[final_ref_] = {{"payload", to_json(candidate_two)}};
    auto first_edge = to_json(edge_one);
    first_edge["payload"] = to_json(edge_one);
    auto second_edge = to_json(edge_two);
    second_edge["payload"] = to_json(edge_two);
    edges_ = {std::move(first_edge), std::move(second_edge)};
  }

  std::vector<std::string>
  ancestors(std::string_view candidate_ref) const override {
    if (candidate_ref != final_ref_) {
      return {std::string(base_ref)};
    }
    return {first_ref_, std::string(base_ref)};
  }

  statewright::contracts::Json
  get_candidate(std::string_view candidate_ref) const override {
    return candidates_.at(std::string(candidate_ref));
  }

  std::vector<statewright::contracts::Json>
  lineage_edges() const override {
    return edges_;
  }

  [[nodiscard]] const std::string &first_ref() const noexcept {
    return first_ref_;
  }

  [[nodiscard]] const std::string &final_ref() const noexcept {
    return final_ref_;
  }

private:
  std::string first_ref_;
  std::string final_ref_;
  std::map<std::string, statewright::contracts::Json> candidates_;
  std::vector<statewright::contracts::Json> edges_;
};

statewright::saa::ReasoningEvidenceResolver evidence_resolver() {
  using statewright::saa::ReasoningGroundingEvidence;
  const std::map<std::string, ReasoningGroundingEvidence> evidence = {
      {"evidence:q1",
       {.object_type = "egcf-evidence",
        .success = true,
        .simulated = false,
        .producer = "deterministic-saa-11-test",
        .method = "controlled-ab-experiment",
        .requirement_ids = {"invariant"},
        .independence_group = "step1"}},
      {"evidence:q2",
       {.object_type = "egcf-evidence",
        .success = true,
        .simulated = false,
        .producer = "deterministic-saa-11-test",
        .method = "controlled-ab-experiment",
        .requirement_ids = {"invariant"},
        .independence_group = "step2"}}};
  return [evidence](std::string_view evidence_id)
             -> std::optional<ReasoningGroundingEvidence> {
    const auto found = evidence.find(std::string(evidence_id));
    return found == evidence.end()
               ? std::nullopt
               : std::optional<ReasoningGroundingEvidence>(found->second);
  };
}

} // namespace

TEST_CASE("SAA multistep evolution freezes every intermediate invariant") {
  using namespace statewright::saa;
  const LineageCatalog lineage;
  REQUIRE(lineage.first_ref() ==
          "adapted-candidate:sha256:34b821162e6733b6956482efba5429d1d710f8755c94926f0b7003bdd14cf30d");
  REQUIRE(lineage.final_ref() ==
          "adapted-candidate:sha256:82c4ce10f576b1a7e69be14875bb574e44c94c7621f979f6323d375539f92da4");
  const auto plan = make_multistep_evolution_plan(
      lineage, lineage.final_ref(), {"bounded"},
      {"DYNAMICS_CONTRACT", "BOUNDARY_CONTRACT"});
  REQUIRE(plan.steps.size() == 2U);
  REQUIRE(plan.root_algorithm_ref == base_ref);
  REQUIRE(plan.plan_signature ==
          "b9fcee015f47bdf2c36296dd376b24d74ebb63e03f084d4c58d09bd46b043798");

  const auto first = qualify_evolution_step(
      evidence_resolver(), plan, lineage.first_ref(), {{"bounded", true}},
      {"evidence:q1"}, true);
  const auto second = qualify_evolution_step(
      evidence_resolver(), plan, lineage.final_ref(), {{"bounded", true}},
      {"evidence:q2"}, true);
  REQUIRE(first.qualification_signature ==
          "c6b1de83bb7aae217006c62a5d942f32c9cf70a96db56c1be8992a84e33f2703");
  REQUIRE(second.qualification_signature ==
          "c7eda9555d0fc0c82b8a29d67293cee7b3b715961d4a51cfbb802c474e7684df");
  const auto assessment = assess_multistep_evolution(plan, {first, second});
  REQUIRE(assessment.status == "MULTISTEP_EVOLUTION_QUALIFIED");
  REQUIRE(assessment.evolution_qualified);
  REQUIRE(assessment.qualified_step_count == 2);
  REQUIRE(assessment.assessment_signature ==
          "f7e872850e077c384f1108f04c628c36f6b1e840ae382956cffb12514be21b30");
}

TEST_CASE("SAA multistep evolution blocks one failed intermediate invariant") {
  using namespace statewright::saa;
  const LineageCatalog lineage;
  const auto plan = make_multistep_evolution_plan(
      lineage, lineage.final_ref(), {"bounded"},
      {"DYNAMICS_CONTRACT", "BOUNDARY_CONTRACT"});
  const auto first = qualify_evolution_step(
      evidence_resolver(), plan, lineage.first_ref(), {{"bounded", true}},
      {"evidence:q1"}, true);
  const auto failed = qualify_evolution_step(
      evidence_resolver(), plan, lineage.final_ref(), {{"bounded", false}},
      {"evidence:q2"}, true);
  REQUIRE(failed.status == "EVOLUTION_STEP_INVARIANT_VIOLATION");
  REQUIRE(failed.qualification_signature ==
          "fc8b9c8c73599335668fdcead78081998dfc3283a234348640bf35cd8bae9f01");
  const auto assessment = assess_multistep_evolution(plan, {first, failed});
  REQUIRE(assessment.status == "MULTISTEP_EVOLUTION_BLOCKED");
  REQUIRE_FALSE(assessment.evolution_qualified);
  REQUIRE(assessment.blocking_steps.size() == 1U);
  REQUIRE(assessment.assessment_signature ==
          "35312105a8b1c616f754718fcdffe3dc99e0140ddfd2707b046e5951ca08a1ee");
}
