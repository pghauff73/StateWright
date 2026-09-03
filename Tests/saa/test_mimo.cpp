#include "statewright/common/error.hpp"
#include "statewright/saa/mimo.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <optional>
#include <string>

namespace {

statewright::contracts::Json algorithm_mapping(std::size_t inputs,
                                               std::size_t outputs) {
  statewright::contracts::Json nodes = statewright::contracts::Json::array();
  statewright::contracts::Json input_specs =
      statewright::contracts::Json::array();
  statewright::contracts::Json output_specs =
      statewright::contracts::Json::array();
  statewright::contracts::Json entry_nodes =
      statewright::contracts::Json::array();
  for (std::size_t input = 0; input < inputs; ++input) {
    input_specs.push_back({{"data_type", "scalar"},
                           {"name", "u" + std::to_string(input)},
                           {"position", input}});
  }
  for (std::size_t output = 0; output < outputs; ++output) {
    const std::string node = "out" + std::to_string(output);
    nodes.push_back({{"id", node},
                     {"operands", {{{"constant", 0}}}},
                     {"primitive", "CONST"}});
    output_specs.push_back({{"data_type", "scalar"},
                            {"name", "y" + std::to_string(output)},
                            {"position", output},
                            {"source", {{"node", node}}}});
    entry_nodes.push_back(node);
  }
  return {{"entry_nodes", entry_nodes},
          {"inputs", input_specs},
          {"name", "mimo-fixture"},
          {"nodes", nodes},
          {"outputs", output_specs}};
}

statewright::saa::NormalizationContract normalization(std::size_t inputs,
                                                      std::size_t outputs,
                                                      double time = 1.0) {
  statewright::saa::BoundMap input_bounds;
  statewright::saa::BoundMap output_bounds;
  for (std::size_t input = 0; input < inputs; ++input) {
    input_bounds.emplace(static_cast<int>(input),
                         statewright::saa::NumericBound(0.0, 1.0));
  }
  for (std::size_t output = 0; output < outputs; ++output) {
    output_bounds.emplace(static_cast<int>(output),
                          statewright::saa::NumericBound(0.0, 1.0));
  }
  return statewright::saa::build_normalization_contract(
      statewright::saa::structure_from_mapping(
          algorithm_mapping(inputs, outputs)),
      input_bounds, {}, {}, output_bounds,
      statewright::saa::TimeNormalization(time));
}

statewright::saa::LinearTransferFunction
constant_transfer(std::string value,
                  std::optional<std::string> sample_period = std::nullopt) {
  if (sample_period) {
    return {"DISCRETE", {std::move(value)}, {1},
            statewright::saa::NumericCoefficient(*sample_period)};
  }
  return {"CONTINUOUS", {std::move(value)}, {1}};
}

statewright::saa::LinearTransferFunction transfer(
    statewright::saa::Polynomial numerator,
    statewright::saa::Polynomial denominator,
    std::optional<std::string> sample_period = std::nullopt) {
  if (sample_period) {
    return {"DISCRETE", std::move(numerator), std::move(denominator),
            statewright::saa::NumericCoefficient(*sample_period)};
  }
  return {"CONTINUOUS", std::move(numerator), std::move(denominator)};
}

} // namespace

TEST_CASE("SAA MIMO exact diagonal form matches frozen oracle") {
  const auto result = statewright::saa::canonicalize_mimo_transfer_matrix(
      {"CONTINUOUS",
       {{constant_transfer("2"), constant_transfer("0")},
        {constant_transfer("0"), constant_transfer("4")}}},
      normalization(2, 2));

  REQUIRE(result.dynamic_strength == "EXACT_MIMO_LINEAR_DYNAMICS");
  REQUIRE(result.coupling_strength == "EXACT_COUPLING_ANALYSIS");
  REQUIRE(result.permutation_decoupled);
  REQUIRE(result.exact_diagonal_input_permutation ==
          std::vector<std::size_t>{0, 1});
  REQUIRE(result.relative_gain_array ==
          statewright::saa::RationalMatrix{{1, 0}, {0, 1}});
  REQUIRE(result.preferred_rga_pairing ==
          std::vector<std::size_t>{0, 1});
  REQUIRE(result.rga_off_pairing_mass == mpq_class(0));
  REQUIRE(result.static_decoupling.has_value());
  REQUIRE(result.ordered_signature ==
          "445511f780b3038c2bf872476c1d2f6678dbf1d606ce51af39e36163bbad6d47");
  REQUIRE(result.permutation_invariant_signature ==
          "764c9d96356ba7ff375a638e00250d285cd57c4e22d0cafd0e3257653b173719");
  REQUIRE(result.audit_hash ==
          "e1e5f5901f8e52677b22650c0b72f5d4b7165e17f0ebd9ec2427c129dadb878a");
  REQUIRE(result.canonical_output_permutation ==
          std::vector<std::size_t>{0, 1});
  REQUIRE(result.canonical_input_permutation ==
          std::vector<std::size_t>{1, 0});
  REQUIRE(result.static_decoupling->canonical_signature ==
          "18de35f70c7c7c90459e15ed0747b3dcbd0ee0c7dd10ca10df4f9e4ae395cbf6");
  for (const auto &sample :
       result.static_decoupling->residual_coupling_samples) {
    REQUIRE(sample.ratio == 0.0);
  }
}

TEST_CASE("SAA MIMO crossed and coupled matrices expose exact pairing") {
  const auto crossed = statewright::saa::canonicalize_mimo_transfer_matrix(
      {"CONTINUOUS",
       {{constant_transfer("0"), constant_transfer("2")},
        {constant_transfer("3"), constant_transfer("0")}}},
      normalization(2, 2));
  REQUIRE(crossed.permutation_decoupled);
  REQUIRE(crossed.exact_diagonal_input_permutation ==
          std::vector<std::size_t>{1, 0});
  REQUIRE(crossed.preferred_rga_pairing ==
          std::vector<std::size_t>{1, 0});
  REQUIRE(crossed.rga_off_pairing_mass == mpq_class(0));
  REQUIRE(crossed.ordered_signature ==
          "3eeeefd178f0455d536e2a2789122e299c69184b87b3c2703a7ee884708d9171");
  REQUIRE(crossed.permutation_invariant_signature ==
          "5790b3909d6d7bc9394ff0c7ac525d62d844bf7a9379a5061b84286524f7f05f");

  const auto coupled = statewright::saa::canonicalize_mimo_transfer_matrix(
      {"CONTINUOUS",
       {{constant_transfer("1"), constant_transfer("1/2")},
        {constant_transfer("1/4"), constant_transfer("1")}}},
      normalization(2, 2));
  REQUIRE_FALSE(coupled.permutation_decoupled);
  REQUIRE(coupled.relative_gain_array ==
          statewright::saa::RationalMatrix{{mpq_class(8, 7),
                                            mpq_class(-1, 7)},
                                           {mpq_class(-1, 7),
                                            mpq_class(8, 7)}});
  REQUIRE(coupled.preferred_rga_pairing ==
          std::vector<std::size_t>{0, 1});
  REQUIRE(coupled.rga_off_pairing_mass == mpq_class(1, 9));
  REQUIRE(coupled.ordered_signature ==
          "7636dae330cdc885735d0d7ad2f2bc6a87ce7833efdf9b579406188547ede96f");
  REQUIRE(coupled.permutation_invariant_signature ==
          "0d2342a2f7962ad0f8161fb5b616ff00263c70526c7604b0eef4d98c0eed0d47");
  REQUIRE(coupled.static_decoupling->canonical_signature ==
          "d43f19a5c5a6bc08e2f5b7367f27a759f117b1b5c214d0381bcac7eef19cf2e6");
}

TEST_CASE("SAA MIMO static decoupling reports residual dynamics") {
  const auto result = statewright::saa::canonicalize_mimo_transfer_matrix(
      {"CONTINUOUS",
       {{transfer({1}, {1, 1}), transfer({1}, {1, 2})},
        {transfer({1}, {1, 3}), transfer({1}, {1, 1})}}},
      normalization(2, 2));
  REQUIRE(result.static_decoupling.has_value());
  bool has_residual = false;
  for (const auto &sample :
       result.static_decoupling->residual_coupling_samples) {
    has_residual = has_residual ||
                   (std::isfinite(sample.ratio) && sample.ratio > 1e-8);
  }
  REQUIRE(has_residual);
}

TEST_CASE("SAA MIMO ordered and permutation signatures remain distinct") {
  const auto contract = normalization(2, 2);
  const auto first = statewright::saa::canonicalize_mimo_transfer_matrix(
      {"CONTINUOUS",
       {{constant_transfer("1"), constant_transfer("2")},
        {constant_transfer("3"), constant_transfer("4")}}},
      contract);
  const auto second = statewright::saa::canonicalize_mimo_transfer_matrix(
      {"CONTINUOUS",
       {{constant_transfer("2"), constant_transfer("1")},
        {constant_transfer("4"), constant_transfer("3")}}},
      contract);
  REQUIRE(first.ordered_signature != second.ordered_signature);
  REQUIRE(first.permutation_invariant_signature ==
          second.permutation_invariant_signature);
  REQUIRE(first.permutation_strength == "EXACT_PORT_PERMUTATION");

  const auto structural =
      statewright::saa::canonicalize_mapping(algorithm_mapping(2, 2));
  REQUIRE(statewright::saa::mimo_algorithm_signature(structural, contract,
                                                     first) !=
          statewright::saa::mimo_algorithm_signature(structural, contract,
                                                     second));
  REQUIRE(statewright::saa::mimo_algorithm_signature(structural, contract,
                                                     first, true) ==
          statewright::saa::mimo_algorithm_signature(structural, contract,
                                                     second, true));
}

TEST_CASE("SAA MIMO permutation search is explicitly bounded") {
  statewright::saa::TransferFunctionMatrix channels;
  for (std::size_t row = 0; row < 5; ++row) {
    std::vector<statewright::saa::LinearTransferFunction> channel_row;
    for (std::size_t column = 0; column < 5; ++column) {
      channel_row.push_back(constant_transfer(row == column ? "1" : "0"));
    }
    channels.push_back(std::move(channel_row));
  }
  const auto contract = normalization(5, 5);
  const auto result = statewright::saa::canonicalize_mimo_transfer_matrix(
      {"CONTINUOUS", std::move(channels)}, contract, 10);
  REQUIRE_FALSE(result.permutation_invariant_signature.has_value());
  REQUIRE(result.permutation_strength ==
          "ORDERED_ONLY_PERMUTATION_BUDGET_EXCEEDED");
  REQUIRE_FALSE(result.warnings.empty());
  REQUIRE_THROWS_AS(
      statewright::saa::mimo_algorithm_signature(
          statewright::saa::canonicalize_mapping(algorithm_mapping(5, 5)),
          contract, result, true),
      statewright::common::Error);
}

TEST_CASE("SAA MIMO fails conservatively when coupling is indeterminate") {
  const auto singular = statewright::saa::canonicalize_mimo_transfer_matrix(
      {"CONTINUOUS",
       {{constant_transfer("1"), constant_transfer("1")},
        {constant_transfer("2"), constant_transfer("2")}}},
      normalization(2, 2));
  REQUIRE_FALSE(singular.relative_gain_array.has_value());
  REQUIRE_FALSE(singular.static_decoupling.has_value());
  REQUIRE_FALSE(singular.warnings.empty());

  const auto pole = statewright::saa::canonicalize_mimo_transfer_matrix(
      {"CONTINUOUS",
       {{transfer({1}, {1, 0}), constant_transfer("0")},
        {constant_transfer("0"), constant_transfer("1")}}},
      normalization(2, 2));
  REQUIRE_FALSE(pole.steady_gain[0][0].has_value());
  REQUIRE_FALSE(pole.relative_gain_array.has_value());
  REQUIRE_FALSE(pole.static_decoupling.has_value());

  statewright::saa::LinearTransferFunction approximate("CONTINUOUS", {1.0},
                                                       {1.0});
  const auto approximate_result =
      statewright::saa::canonicalize_mimo_transfer_matrix(
          {"CONTINUOUS",
           {{approximate, constant_transfer("0")},
            {constant_transfer("0"), constant_transfer("1")}}},
          normalization(2, 2));
  REQUIRE(approximate_result.dynamic_strength ==
          "APPROXIMATE_MIMO_LINEAR_DYNAMICS");
  REQUIRE(approximate_result.coupling_strength ==
          "APPROXIMATE_COUPLING_ANALYSIS");
  REQUIRE_FALSE(approximate_result.static_decoupling.has_value());
}

TEST_CASE("SAA MIMO discrete and dimension contracts fail closed") {
  const auto discrete = statewright::saa::canonicalize_mimo_transfer_matrix(
      {"DISCRETE",
       {{constant_transfer("1", "1/2"),
         constant_transfer("0", "1/2")},
        {constant_transfer("0", "1/2"),
         constant_transfer("1", "1/2")}}},
      normalization(2, 2));
  REQUIRE(discrete.domain == "DISCRETE");
  REQUIRE(discrete.normalized_sample_interval == mpq_class(1, 2));

  REQUIRE_THROWS_AS(
      statewright::saa::canonicalize_mimo_transfer_matrix(
          {"DISCRETE",
           {{constant_transfer("1", "1"),
             constant_transfer("0", "1")},
            {constant_transfer("0", "1"),
             constant_transfer("1", "2")}}},
          normalization(2, 2)),
      statewright::common::Error);
  REQUIRE_THROWS_AS(
      statewright::saa::canonicalize_mimo_transfer_matrix(
          {"CONTINUOUS",
           {{constant_transfer("1"), constant_transfer("0")},
            {constant_transfer("0"), constant_transfer("1")}}},
          normalization(1, 2)),
      statewright::common::Error);
  REQUIRE_THROWS_AS(
      statewright::saa::MIMOTransferMatrix(
          "CONTINUOUS",
          {{constant_transfer("1"), constant_transfer("2")},
           {constant_transfer("3")}}),
      statewright::common::Error);
  statewright::saa::TransferFunctionMatrix oversized(
      2, std::vector<statewright::saa::LinearTransferFunction>(
             7, constant_transfer("0")));
  REQUIRE_THROWS_AS(
      statewright::saa::MIMOTransferMatrix("CONTINUOUS", std::move(oversized)),
      statewright::common::Error);
}
