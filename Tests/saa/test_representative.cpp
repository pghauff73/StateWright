#include "statewright/common/error.hpp"
#include "statewright/saa/representative.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {

statewright::contracts::Json representative_mapping(std::size_t inputs,
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
          {"name", "representative-fixture"},
          {"nodes", nodes},
          {"outputs", output_specs}};
}

statewright::saa::NormalizationContract representative_normalization(
    std::size_t inputs, std::size_t outputs) {
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
          representative_mapping(inputs, outputs)),
      input_bounds, {}, {}, output_bounds,
      statewright::saa::TimeNormalization(1.0));
}

statewright::saa::LinearTransferFunction constant(std::string value) {
  return {"CONTINUOUS", {std::move(value)}, {1}};
}

statewright::saa::LinearTransferFunction dynamic(
    statewright::saa::Polynomial numerator,
    statewright::saa::Polynomial denominator) {
  return {"CONTINUOUS", std::move(numerator), std::move(denominator)};
}

statewright::saa::CanonicalMIMOCoupling canonical(
    statewright::saa::TransferFunctionMatrix matrix) {
  const std::size_t outputs = matrix.size();
  const std::size_t inputs = matrix.front().size();
  return statewright::saa::canonicalize_mimo_transfer_matrix(
      {"CONTINUOUS", std::move(matrix)},
      representative_normalization(inputs, outputs));
}

} // namespace

TEST_CASE("SAA representative assessment matches exact oracle identities") {
  const auto diagonal = statewright::saa::assess_mimo_representation(
      canonical({{constant("2"), constant("0")},
                 {constant("0"), constant("3")}}));
  REQUIRE(diagonal.status == "REPRESENTATIVE_EXACT");
  REQUIRE(diagonal.coupling_bp == 0);
  REQUIRE(diagonal.canonical_admission_eligible);
  REQUIRE_FALSE(diagonal.requires_representative_search);
  REQUIRE(diagonal.assessment_signature ==
          "ac1cd184a3c98bc7bae104e7bbd5ce17c3adbb7b9a587228c481c376920d5dd1");

  const auto crossed = statewright::saa::assess_mimo_representation(
      canonical({{constant("0"), constant("2")},
                 {constant("3"), constant("0")}}));
  REQUIRE(crossed.status == "REPRESENTATIVE_EXACT");
  REQUIRE(crossed.preferred_input_to_output_pairing ==
          std::vector<std::size_t>{1, 0});
  REQUIRE(crossed.coupling_bp == 0);

  const auto coupled = statewright::saa::assess_mimo_representation(
      canonical({{constant("1"), constant("1/2")},
                 {constant("1/4"), constant("1")}}));
  REQUIRE(coupled.status == "NON_REPRESENTATIVE_COUPLED");
  REQUIRE(coupled.coupling_bp == 5000);
  REQUIRE_FALSE(coupled.canonical_admission_eligible);
  REQUIRE(coupled.assessment_signature ==
          "f0e14bf9f77f2538f78c29e9b640f8b6de45f4f62c3887181ee14e1071597b9b");
}

TEST_CASE("SAA representative minimality quotients general redundancy") {
  const auto assessment = statewright::saa::assess_mimo_representation(
      canonical({{constant("1"), constant("0"), constant("1")},
                 {constant("0"), constant("1"), constant("1")}}));
  REQUIRE(assessment.status == "NON_REPRESENTATIVE_REDUNDANT_INPUTS");
  REQUIRE(assessment.minimality.has_value());
  REQUIRE(assessment.minimality->effective_input_rank == 2);
  REQUIRE(assessment.minimality->redundant_input_count == 1);
  REQUIRE(assessment.minimality->pivot_input_positions ==
          std::vector<std::size_t>{0, 1});
  REQUIRE(assessment.minimality->source_to_basis_projection ==
          statewright::saa::RationalMatrix{{1, 0, 1}, {0, 1, 1}});
  REQUIRE(assessment.assessment_signature ==
          "6d1c24fd705682835360160438d67e107439454db5157f92fdeadd06145f7386");
}

TEST_CASE("SAA representative search finds exact constant bases") {
  const auto search = statewright::saa::discover_representative_inputs(
      canonical({{constant("1"), constant("1/2")},
                 {constant("1/4"), constant("1")}}));
  REQUIRE(search.search_status == "REPRESENTATIVE_FORM_FOUND");
  REQUIRE(search.representative_found());
  REQUIRE(search.candidates_considered == 11);
  REQUIRE(search.audit_hash ==
          "cc83082af62f9e376731b41ee278dafa8dc38c8bdcde7da43a4dd180afcca268");
  REQUIRE(search.best_candidate.has_value());
  REQUIRE(search.best_candidate->status == "REPRESENTATIVE_FORM_CANDIDATE");
  REQUIRE(search.best_candidate->transform_class ==
          "CONSTANT_LINEAR_ALGEBRAIC_PROBE");
  REQUIRE(search.best_candidate->coupling_after_bp == 0);
  REQUIRE(search.best_candidate->exact_decoupled);
  REQUIRE(search.best_candidate->admissibility.admissible);
  REQUIRE(search.best_candidate->admissibility.invertibility_status ==
          "FULLY_INVERTIBLE");
  REQUIRE(search.best_candidate->requires_renormalization);
  REQUIRE(search.best_candidate->canonical_signature ==
          "1158f34d981d6a187e6d74444ea6f9195d6ab6cd4eac55e897a953298c62ee74");
}

TEST_CASE("SAA representative search validates behavior beyond one probe") {
  const auto mixed = statewright::saa::discover_representative_inputs(
      canonical({{dynamic({1}, {1, 1}), dynamic({1}, {1, 1})},
                 {dynamic({1}, {1, 2}), dynamic({-1}, {1, 2})}}));
  REQUIRE(mixed.representative_found());
  REQUIRE(mixed.best_candidate->coupling_after_bp == 0);
  REQUIRE(mixed.best_candidate->transform_class ==
          "CONSTANT_LINEAR_ALGEBRAIC_PROBE");

  const auto pole = statewright::saa::discover_representative_inputs(
      canonical({{dynamic({1}, {1, 0}), dynamic({1}, {1, 0})},
                 {constant("1"), constant("-1")}}));
  REQUIRE(pole.representative_found());
  REQUIRE(pole.best_candidate->algebraic_probe.has_value());
  REQUIRE(*pole.best_candidate->algebraic_probe != 0);
}

TEST_CASE("SAA representative search preserves unresolved dynamic coupling") {
  const auto search = statewright::saa::discover_representative_inputs(
      canonical({{dynamic({1}, {1, 1}), dynamic({1}, {1, 2})},
                 {dynamic({1}, {1, 3}), dynamic({1}, {1, 1})}}));
  REQUIRE_FALSE(search.representative_found());
  REQUIRE(search.search_status ==
          "REPRESENTATIVE_FORM_UNRESOLVED_CONSTANT_LINEAR_SEARCH");
  REQUIRE(search.best_candidate.has_value());
  REQUIRE(search.best_candidate->coupling_after_bp > 0);
  REQUIRE(search.audit_hash ==
          "6ceaba91aeff26e6f449dcc4132fb63f7fbc5a90129e6108aa5902cc674002f0");
}

TEST_CASE("SAA representative search reports bounded uncertainty") {
  const auto full_rank =
      canonical({{constant("1"), constant("1")},
                 {constant("1"), constant("-1")}});
  const auto budgeted =
      statewright::saa::discover_representative_inputs(full_rank,
                                                       16384, 1);
  REQUIRE_FALSE(budgeted.representative_found());
  REQUIRE(budgeted.search_status ==
          "REPRESENTATIVE_SEARCH_BUDGET_EXHAUSTED");

  const auto rank_budget =
      statewright::saa::assess_mimo_representation(full_rank, 1);
  REQUIRE(rank_budget.status == "REPRESENTATION_UNRESOLVED_RANK_BUDGET");
  REQUIRE_FALSE(rank_budget.warnings.empty());

  statewright::saa::LinearTransferFunction approximate("CONTINUOUS", {1.0},
                                                       {1.0});
  const auto approximate_mimo = statewright::saa::canonicalize_mimo_transfer_matrix(
      {"CONTINUOUS",
       {{approximate, constant("1")},
        {constant("1"), constant("-1")}}},
      representative_normalization(2, 2));
  const auto approximate_search =
      statewright::saa::discover_representative_inputs(approximate_mimo);
  REQUIRE(approximate_search.search_status ==
          "REPRESENTATIVE_FORM_UNRESOLVED_APPROXIMATE");
  REQUIRE(approximate_search.candidates_considered == 0);
  REQUIRE_FALSE(approximate_search.best_candidate.has_value());
}

TEST_CASE("SAA representative quotient provides an exact section") {
  const auto search = statewright::saa::discover_representative_inputs(
      canonical({{constant("1"), constant("0"), constant("1")},
                 {constant("0"), constant("1"), constant("1")}}));
  REQUIRE(search.representative_found());
  REQUIRE(search.audit_hash ==
          "56a22d1988c42fb86809a5ea66f6167ecee2e2f35ce68cc01ba24177ec3a5576");
  REQUIRE(search.best_candidate->representative_input_count == 2);
  REQUIRE(search.best_candidate->admissibility.invertibility_status ==
          "INVERTIBLE_ON_BEHAVIORAL_QUOTIENT");
  const auto &projection =
      search.best_candidate->source_to_representative_projection;
  const auto &section = search.best_candidate->representative_to_source_section;
  statewright::saa::RationalMatrix product(
      2, std::vector<mpq_class>(2, 0));
  for (std::size_t row = 0; row < 2; ++row) {
    for (std::size_t column = 0; column < 2; ++column) {
      for (std::size_t index = 0; index < section.size(); ++index) {
        product[row][column] += projection[row][index] * section[index][column];
      }
    }
  }
  REQUIRE(product == statewright::saa::RationalMatrix{{1, 0}, {0, 1}});
}

TEST_CASE("SAA zero behavior collapses to the zero-input quotient") {
  const auto search = statewright::saa::discover_representative_inputs(
      canonical({{constant("0"), constant("0")},
                 {constant("0"), constant("0")}}));
  REQUIRE(search.representative_found());
  REQUIRE(search.best_candidate->representative_input_count == 0);
  REQUIRE(search.best_candidate->transform_class ==
          "BEHAVIORAL_ZERO_INPUT_QUOTIENT");
  REQUIRE(search.source_assessment.assessment_signature ==
          "a451b3e9a7aa66c481d85f0e44c6e405fc82dcdb164acc71acc5f61cdebffe1f");
  REQUIRE(search.best_candidate->canonical_signature ==
          "b962ba6dff0df8d9c3d6f2faf487afee7350cac91a1cca3ec5b8d1a94351fc9c");
  REQUIRE(search.audit_hash ==
          "551088b31ea59bdca9f1f55bbe1a59d681a7ae5f9cd84d204fc2c57386fc34b4");
}

TEST_CASE("SAA representative transform coefficient limits fail closed") {
  const auto search = statewright::saa::discover_representative_inputs(
      canonical({{constant("1"), constant("1/2")},
                 {constant("1/4"), constant("1")}}),
      16384, 4096, 1);
  REQUIRE_FALSE(search.representative_found());
  REQUIRE(search.best_candidate.has_value());
  REQUIRE_FALSE(search.best_candidate->admissibility.admissible);
  REQUIRE(search.best_candidate->admissibility.status ==
          "INADMISSIBLE_TRANSFORM");
  REQUIRE_THROWS_AS(
      statewright::saa::discover_representative_inputs(
          canonical({{constant("1")}}), 16384, 0, 256),
      statewright::common::Error);
}
