#include "statewright/common/error.hpp"
#include "statewright/saa/dynamics.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

statewright::contracts::Json siso_mapping(std::string input_type = "scalar") {
  return {{"entry_nodes", {"identity"}},
          {"inputs",
           {{{"data_type", std::move(input_type)},
             {"name", "u"},
             {"position", 0}}}},
          {"name", "siso-linear-shell"},
          {"nodes",
           {{{"id", "identity"},
             {"operands", {{{"input", 0}}, {{"constant", 1}}}},
             {"primitive", "MULTIPLY"}}}},
          {"outputs",
           {{{"data_type", "scalar"},
             {"name", "y"},
             {"position", 0},
             {"source", {{"node", "identity"}}}}}}};
}

statewright::saa::NormalizationContract normalization(
    std::string kind = "EXACT_BOUND", double characteristic_time = 1.0,
    std::pair<double, double> input_bound = {0.0, 1.0},
    std::pair<double, double> output_bound = {0.0, 1.0},
    std::string input_type = "scalar") {
  const auto spec = statewright::saa::structure_from_mapping(
      siso_mapping(std::move(input_type)));
  return statewright::saa::build_normalization_contract(
      spec,
      {{0, {input_bound.first, input_bound.second, kind}}}, {}, {},
      {{0, {output_bound.first, output_bound.second, kind}}},
      statewright::saa::TimeNormalization(characteristic_time, kind,
                                           "source-time-unit"));
}

} // namespace

TEST_CASE("SAA exact transfer reduction matches frozen oracle") {
  const auto contract = normalization();
  const auto reduced = statewright::saa::canonicalize_transfer_function(
      {"continuous", {2, 6, 4}, {2, 8, 6}}, contract);
  const auto simple = statewright::saa::canonicalize_transfer_function(
      {"s", {1, 2}, {1, 3}}, contract);
  REQUIRE(reduced.canonical_signature ==
          "7e7523838fe78794c707e5a2b4b7cd8c6f569d1b1a804cedea03c3b4e37f8d44");
  REQUIRE(reduced.canonical_signature == simple.canonical_signature);
  REQUIRE(reduced.dynamic_strength == "EXACT_LINEAR_DYNAMICS");
  REQUIRE(reduced.dynamic_order == 1);
  REQUIRE(reduced.reductions ==
          std::vector<std::string>{"EXACT_COMMON_FACTOR_CANCELLATION",
                                   "MONIC_DENOMINATOR"});
  REQUIRE(statewright::saa::polynomial_json(reduced.numerator) ==
          statewright::contracts::Json::array({{1, 1}, {2, 1}}));
  REQUIRE(statewright::saa::polynomial_json(reduced.denominator) ==
          statewright::contracts::Json::array({{1, 1}, {3, 1}}));
}

TEST_CASE("SAA approximate dynamics never receive exact cancellation") {
  const auto approximate = statewright::saa::canonicalize_transfer_function(
      {"continuous", {1.0, 3.0, 2.0}, {1.0, 4.0, 3.0}},
      normalization());
  REQUIRE(approximate.dynamic_strength == "APPROXIMATE_LINEAR_DYNAMICS");
  REQUIRE(approximate.dynamic_order == 2);
  REQUIRE(approximate.reductions ==
          std::vector<std::string>{"MONIC_DENOMINATOR",
                                   "NO_APPROXIMATE_POLE_ZERO_CANCELLATION"});
  REQUIRE_FALSE(approximate.warnings.empty());
  REQUIRE(approximate.canonical_signature !=
          statewright::saa::canonicalize_transfer_function(
              {"continuous", {1, 2}, {1, 3}}, normalization())
              .canonical_signature);
}

TEST_CASE("SAA time and interface scaling produce invariant dynamics") {
  const auto slow = statewright::saa::canonicalize_transfer_function(
      {"continuous", {1}, {1, 1}}, normalization("EXACT_BOUND", 1.0));
  const auto fast = statewright::saa::canonicalize_transfer_function(
      {"continuous", {1000}, {1, 1000}},
      normalization("EXACT_BOUND", 0.001));
  REQUIRE(slow.canonical_signature == fast.canonical_signature);
  const auto volts = statewright::saa::canonicalize_transfer_function(
      {"continuous", {2}, {1}},
      normalization("EXACT_BOUND", 1.0, {0.0, 10.0}, {0.0, 20.0}));
  const auto millivolts = statewright::saa::canonicalize_transfer_function(
      {"continuous", {"0.002"}, {1}},
      normalization("EXACT_BOUND", 1.0, {0.0, 10'000.0}, {0.0, 20.0}));
  REQUIRE(volts.canonical_signature == millivolts.canonical_signature);
  REQUIRE(volts.numerator == statewright::saa::RationalPolynomial{1});
}

TEST_CASE("SAA zero improper and discrete forms are explicit") {
  const auto zero = statewright::saa::canonicalize_transfer_function(
      {"continuous", {0, 0}, {1, 2, 1}}, normalization());
  REQUIRE(zero.canonical_signature ==
          "c58b4cf07edcac6e6d910c366e1b2cb88b159480865c359ad4df1d51d50ba8ca");
  REQUIRE(zero.numerator == statewright::saa::RationalPolynomial{0});
  REQUIRE(zero.denominator == statewright::saa::RationalPolynomial{1});
  REQUIRE_FALSE(zero.relative_degree.has_value());

  const auto improper = statewright::saa::canonicalize_transfer_function(
      {"continuous", {1, 0}, {1}}, normalization());
  REQUIRE_FALSE(improper.proper);
  REQUIRE(improper.relative_degree == -1);
  REQUIRE_FALSE(improper.warnings.empty());

  const auto discrete = statewright::saa::canonicalize_transfer_function(
      {"z", {1}, {1, -1}, "1/10"}, normalization());
  REQUIRE(discrete.variable == "Z");
  REQUIRE(discrete.normalized_sample_interval == mpq_class(1, 10));
  REQUIRE(discrete.canonical_signature ==
          "491614dffb2b6e178f6f9c3774feb8e336764e7cfee5c318bd9f12445b966767");
}

TEST_CASE("SAA state-space transfer is realization invariant") {
  const auto contract = normalization();
  const auto state = statewright::saa::canonicalize_state_space(
      {"continuous", {{-1}}, {{1}}, {{2}}, 0}, contract);
  const auto scaled = statewright::saa::canonicalize_state_space(
      {"continuous", {{-1}}, {{3}}, {{"2/3"}}, 0}, contract);
  const auto transfer = statewright::saa::canonicalize_transfer_function(
      {"continuous", {2}, {1, 1}}, contract);
  REQUIRE(state.canonical_signature == transfer.canonical_signature);
  REQUIRE(scaled.canonical_signature == transfer.canonical_signature);

  const auto nonminimal = statewright::saa::canonicalize_state_space(
      {"continuous", {{-1, 0}, {0, -2}}, {{1}, {0}}, {{2, 0}}, 0},
      contract);
  REQUIRE(nonminimal.canonical_signature == transfer.canonical_signature);
  REQUIRE(nonminimal.dynamic_order == 1);
}

TEST_CASE("SAA dynamics fail closed at contract boundaries") {
  REQUIRE_THROWS_AS(statewright::saa::canonicalize_transfer_function(
                        {"continuous", {1}, {0}}, normalization()),
                    statewright::common::Error);
  REQUIRE_THROWS_AS(statewright::saa::canonicalize_transfer_function(
                        {"discrete", {1}, {1, 1}}, normalization()),
                    statewright::common::Error);
  REQUIRE_THROWS_AS(statewright::saa::canonicalize_transfer_function(
                        {"continuous", {1}, {1, 1}},
                        normalization("EXACT_BOUND", 1.0, {0.0, 1.0},
                                      {0.0, 1.0}, "integer")),
                    statewright::common::Error);
  const auto spec =
      statewright::saa::structure_from_mapping(siso_mapping());
  const auto without_time = statewright::saa::build_normalization_contract(
      spec, {{0, {0.0, 1.0}}}, {}, {}, {{0, {0.0, 1.0}}});
  REQUIRE_THROWS_AS(statewright::saa::canonicalize_transfer_function(
                        {"continuous", {1}, {1, 1}}, without_time),
                    statewright::common::Error);
}

TEST_CASE("SAA combined dynamics signature binds every canonical layer") {
  const auto mapping = siso_mapping();
  const auto structural = statewright::saa::canonicalize_mapping(mapping);
  const auto contract = normalization();
  const auto dynamics = statewright::saa::canonicalize_transfer_function(
      {"continuous", {1, 2}, {1, 3}}, contract);
  REQUIRE(statewright::saa::dynamic_algorithm_signature(
              structural, contract, dynamics) ==
          "67666206ba0a885375af5d2b67152f556659e897d9e76c4130e19a9e8d48ab16");
  const auto incompatible = normalization("OBSERVED_BOUND");
  REQUIRE_THROWS_AS(statewright::saa::dynamic_algorithm_signature(
                        structural, incompatible, dynamics),
                    statewright::common::Error);
}
