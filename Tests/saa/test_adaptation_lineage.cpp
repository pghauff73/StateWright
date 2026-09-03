#include "statewright/saa/adaptation_lineage.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::string_view base_ref =
    "canonical-algorithm:sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

statewright::saa::AdaptationStep adaptation_step(
    int index, std::string dimension = "DYNAMICS_CONTRACT") {
  using statewright::contracts::Json;
  const Json material = {{"base_algorithm_id", base_ref},
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
          .step_signature = statewright::contracts::sha256_json(material)};
}

} // namespace

TEST_CASE("SAA adaptation lineage preserves exact parent and child identity") {
  using namespace statewright::saa;
  const auto step_one = adaptation_step(0);
  REQUIRE(step_one.step_signature ==
          "080f91a2b5200a9a77910ce0bda81dcd2c1967c65ef8c11551457df1ad4bf76e");
  const auto candidate_one = create_adapted_candidate(
      step_one,
      {{"dimension", "DYNAMICS_CONTRACT"}, {"target", "dynamics-v2"}});
  REQUIRE(candidate_one.candidate_signature ==
          "c29ce0c618631e58c201052f3d3da79e6c808cc7ce16b606cf03ef21efa727f9");
  const auto edge_one = make_adaptation_lineage_edge(
      candidate_one, step_one, std::string(64U, 'b'));
  REQUIRE(edge_one.parent_ref == base_ref);
  REQUIRE(edge_one.child_ref ==
          "adapted-candidate:sha256:c29ce0c618631e58c201052f3d3da79e6c808cc7ce16b606cf03ef21efa727f9");
  REQUIRE(edge_one.edge_signature ==
          "f2aef2b07b537855ab18e02297cd6b8dbfc2ae3c01b72a704554a56c70a1cb5d");

  const auto step_two = adaptation_step(1, "BOUNDARY_CONTRACT");
  const auto candidate_two = create_adapted_candidate(
      step_two,
      {{"dimension", "BOUNDARY_CONTRACT"}, {"target", "bounds-v2"}},
      candidate_one.candidate_signature);
  const auto edge_two = make_adaptation_lineage_edge(
      candidate_two, step_two, std::string(64U, 'b'));
  REQUIRE(edge_two.parent_ref == edge_one.child_ref);
  REQUIRE(edge_two.edge_signature ==
          "a8440b5a89d2b41c728fa086392dfff0d4fffbef48a5787f4d05560d315685d7");
}

TEST_CASE("SAA adaptation lineage rejects self-parenting") {
  using namespace statewright::saa;
  const auto step = adaptation_step(0);
  auto candidate = create_adapted_candidate(
      step,
      {{"dimension", "DYNAMICS_CONTRACT"}, {"target", "dynamics-v2"}});
  candidate.parent_candidate_signature = candidate.candidate_signature;
  REQUIRE_THROWS_AS(
      make_adaptation_lineage_edge(candidate, step, std::string(64U, 'b')),
      statewright::common::Error);
}

TEST_CASE("SAA adaptation promotion freezes qualification evidence") {
  using namespace statewright::saa;
  const auto promotion = make_adaptation_promotion(
      "adapted-candidate:sha256:465fbeca6a6e61fb7531245f1b8a877e30a4633f2214859f27d42985d31fea12",
      "canonical-algorithm:sha256:" + std::string(64U, 'f'),
      std::string(64U, 'd'), {"evidence:z", "evidence:a", "evidence:a"});
  REQUIRE(promotion.evidence_ids ==
          std::vector<std::string>{"evidence:a", "evidence:z"});
  REQUIRE(promotion.promotion_signature ==
          "5633e54bbf0a468ebfbbda394b1f95cbedcdd9fbbe0f2460338f275e6dc855b2");
  REQUIRE_THROWS_AS(
      make_adaptation_promotion(promotion.candidate_ref,
                                promotion.canonical_algorithm_ref,
                                std::string(64U, 'd'), {}),
      statewright::common::Error);
}
