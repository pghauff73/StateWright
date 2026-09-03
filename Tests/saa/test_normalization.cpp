#include "statewright/common/error.hpp"
#include "statewright/saa/normalization.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace {

statewright::contracts::Json bounded_binary(std::string primitive = "ADD") {
  return {{"entry_nodes", {"combine"}},
          {"inputs",
           {{{"data_type", "scalar"}, {"name", "left"}, {"position", 0}},
            {{"data_type", "scalar"}, {"name", "right"}, {"position", 1}}}},
          {"name", "bounded-binary"},
          {"nodes",
           {{{"id", "combine"},
             {"operands", {{{"input", 0}}, {{"input", 1}}}},
             {"primitive", std::move(primitive)}}}},
          {"outputs",
           {{{"data_type", "scalar"},
             {"name", "answer"},
             {"position", 0},
             {"source", {{"node", "combine"}}}}}}};
}

statewright::saa::NormalizationContract exact_contract() {
  const auto spec =
      statewright::saa::structure_from_mapping(bounded_binary());
  return statewright::saa::build_normalization_contract(
      spec,
      {{0, statewright::saa::NumericBound(
               -10.0, 10.0, "EXACT_BOUND", "V", {{"source", "test"}})},
       {1, statewright::saa::NumericBound(0.0, 20.0, "EXACT_BOUND", "V")}},
      {}, {},
      {{0, statewright::saa::NumericBound(-10.0, 30.0, "EXACT_BOUND", "V")}},
      statewright::saa::TimeNormalization(
          0.25, "EXACT_BOUND", "s", {{"source", "model"}}));
}

} // namespace

TEST_CASE("SAA normalization is bounded and exactly reversible") {
  const statewright::saa::NumericBound bound(-20.0, 80.0);
  REQUIRE(statewright::saa::normalize_value(bound, -20.0) == 0.0);
  REQUIRE(statewright::saa::normalize_value(bound, 30.0) == 0.5);
  REQUIRE(statewright::saa::normalize_value(bound, 80.0) == 1.0);
  REQUIRE(statewright::saa::denormalize_value(
              bound, statewright::saa::normalize_value(bound, 13.25)) ==
          Catch::Approx(13.25));
  REQUIRE_THROWS_AS(statewright::saa::normalize_value(bound, 81.0),
                    statewright::common::Error);
  REQUIRE_THROWS_AS(statewright::saa::denormalize_value(bound, -0.1),
                    statewright::common::Error);
  REQUIRE_THROWS_AS(
      statewright::saa::NumericBound(0.0,
                                     std::numeric_limits<double>::infinity()),
      statewright::common::Error);
}

TEST_CASE("SAA normalization contract matches frozen oracle hashes") {
  const auto contract = exact_contract();
  REQUIRE(contract.normalization_strength == "EXACT_NORMALIZATION");
  REQUIRE(contract.contract_hash ==
          "e3103fdd4c8d54441fba0ca8d9bc83d4a94f6e261fa0ebfd55f30abcdce0f0eb");
  REQUIRE(contract.canonical_signature ==
          "7f7e41f1e4377a9874ef8ef4e86abc5120cc21c0e358fd4c1a6c2af98e8859fa");
  REQUIRE(statewright::saa::normalize_role(contract, "input", {-10.0, 10.0}) ==
          std::vector<double>{0.0, 0.5});
  REQUIRE(statewright::saa::denormalize_role(contract, "INPUT", {0.0, 0.5}) ==
          std::vector<double>{-10.0, 10.0});
  REQUIRE(statewright::saa::normalize_time(*contract.time, 1.0) == 4.0);
  REQUIRE(statewright::saa::denormalize_time(*contract.time, 4.0) == 1.0);
  const auto structural =
      statewright::saa::canonicalize_mapping(bounded_binary());
  REQUIRE(statewright::saa::normalized_algorithm_signature(structural,
                                                            contract) ==
          "d054aa24fc473fa8b266e9226e661306ea97d3e6946f9f49fcf0b3c98e6dacef");
}

TEST_CASE("SAA exact rescaling preserves canonical interface identity") {
  const auto spec =
      statewright::saa::structure_from_mapping(bounded_binary());
  const auto first = statewright::saa::build_normalization_contract(
      spec,
      {{0, {-10.0, 10.0, "EXACT_BOUND", "V"}},
       {1, {0.0, 20.0, "EXACT_BOUND", "V"}}},
      {}, {}, {{0, {-10.0, 30.0, "EXACT_BOUND", "V"}}});
  const auto second = statewright::saa::build_normalization_contract(
      spec,
      {{0, {-10'000.0, 10'000.0, "EXACT_BOUND", "mV"}},
       {1, {0.0, 20'000.0, "EXACT_BOUND", "mV"}}},
      {}, {}, {{0, {-10'000.0, 30'000.0, "EXACT_BOUND", "mV"}}});
  REQUIRE(first.canonical_signature == second.canonical_signature);
  REQUIRE(first.contract_hash != second.contract_hash);

  const auto approximate = statewright::saa::build_normalization_contract(
      spec,
      {{0, {-10.0, 10.0, "OBSERVED_BOUND"}},
       {1, {0.0, 20.0, "OBSERVED_BOUND"}}},
      {}, {}, {{0, {-10.0, 30.0, "OBSERVED_BOUND"}}});
  REQUIRE(approximate.normalization_strength == "APPROXIMATE_NORMALIZATION");
  REQUIRE(approximate.canonical_signature != first.canonical_signature);
  REQUIRE_FALSE(approximate.warnings.empty());
}

TEST_CASE("SAA normalization rejects missing, extra, and vector bindings") {
  const auto spec =
      statewright::saa::structure_from_mapping(bounded_binary());
  REQUIRE_THROWS_AS(statewright::saa::build_normalization_contract(
                        spec, {{0, {0.0, 1.0}}}, {}, {},
                        {{0, {0.0, 1.0}}}),
                    statewright::common::Error);
  REQUIRE_THROWS_AS(statewright::saa::build_normalization_contract(
                        spec,
                        {{0, {0.0, 1.0}}, {1, {0.0, 1.0}},
                         {3, {0.0, 1.0}}},
                        {}, {}, {{0, {0.0, 1.0}}}),
                    statewright::common::Error);
  auto vector_mapping = bounded_binary();
  vector_mapping["inputs"][0]["shape"] = {2};
  const auto vector_spec =
      statewright::saa::structure_from_mapping(vector_mapping);
  REQUIRE_THROWS_AS(statewright::saa::build_normalization_contract(
                        vector_spec,
                        {{0, {0.0, 1.0}}, {1, {0.0, 1.0}}}, {}, {},
                        {{0, {0.0, 1.0}}}),
                    statewright::common::Error);
}
