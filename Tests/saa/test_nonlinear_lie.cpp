#include "statewright/common/error.hpp"
#include "statewright/saa/nonlinear_lie.hpp"

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using PolynomialRow =
    std::tuple<std::size_t, std::vector<int>, statewright::saa::NumericCoefficient>;

statewright::saa::ExactPolynomialSystem
polynomial_system(std::size_t input_count, std::size_t output_count,
                  std::initializer_list<PolynomialRow> rows) {
  statewright::saa::ExactPolynomialSystem system;
  system.input_count = input_count;
  system.output_count = output_count;
  for (const auto &[output_index, powers, coefficient] : rows) {
    system.terms.push_back({.output_index = output_index,
                            .powers = powers,
                            .coefficient = coefficient});
  }
  return system;
}

statewright::saa::ExactControlAffinePolynomialSystem
double_integrator(bool include_control = true) {
  using namespace statewright;
  auto drift = polynomial_system(2U, 2U, {{0U, {0, 1}, 1}});
  auto control = polynomial_system(2U, 2U, {{1U, {0, 0}, 1}});
  auto outputs = polynomial_system(2U, 1U, {{0U, {1, 0}, 1}});
  std::vector<saa::ExactPolynomialSystem> controls;
  if (include_control) {
    controls.push_back(std::move(control));
  }
  return saa::make_control_affine_polynomial_system(
      2U, std::move(drift), std::move(controls), std::move(outputs), {0, 0},
      {1, 1});
}

} // namespace

TEST_CASE("SAA nonlinear Lie structure identifies the double integrator") {
  using namespace statewright;
  const auto assessment =
      saa::assess_nonlinear_lie_structure(double_integrator(), {0, 0}, 2);

  REQUIRE(assessment.accessibility.rank == 2U);
  REQUIRE(assessment.observability.rank == 2U);
  REQUIRE(assessment.accessibility.generated_field_count == 2U);
  REQUIRE(assessment.observability.generated_function_count == 3U);
  REQUIRE(assessment.locally_accessible_and_observable);
  REQUIRE_FALSE(assessment.global_claim_eligible);
  REQUIRE_FALSE(assessment.accessibility.global_accessibility_eligible);
  REQUIRE_FALSE(assessment.observability.global_observability_eligible);
  REQUIRE(assessment.accessibility.field_signatures ==
          std::vector<std::string>{
              "62f000729183117eff4c145d7afcb4c819e55c41f2044ea0970615830f7b6be8",
              "e47674a213013e6e9f3a7e43dc4a7e6de6d48d8c1aed08db9eda2f1a5be4afb9"});
  REQUIRE(assessment.observability.function_signatures ==
          std::vector<std::string>{
              "3e632d6a4002fc6afbefcdee05bfa7098ab5f6bd61eeec1db361319b95430c1b",
              "9393d6471abc15f90c1f93b41f86187e12756a7979a70d3d29fca813cad38f7d",
              "eec47412b0319a3f4b6314a96c91b3f535dc0a66b2a0719a37bd87b4c7bba205"});
  REQUIRE(assessment.accessibility.assessment_signature ==
          "c58787ca8935721f4f9fadcd252c800473e39c1c7d8ec07f24a905cbc252f4dd");
  REQUIRE(assessment.observability.assessment_signature ==
          "c6abd378e0167a744339489d6c9ba89fea56c3aebbea72b54ea273f13f6bcd80");
  REQUIRE(assessment.assessment_signature ==
          "fd54e38150b74649a595b5b21a0fbfac51c2fcf22a92c75871dfc60da421623c");
}

TEST_CASE("SAA nonlinear Lie accessibility refuses missing controls") {
  const auto assessment = statewright::saa::assess_nonlinear_lie_structure(
      double_integrator(false), {0, 0});

  REQUIRE(assessment.accessibility.rank == 0U);
  REQUIRE_FALSE(assessment.accessibility.full_rank);
  REQUIRE(assessment.observability.rank == 2U);
  REQUIRE(assessment.accessibility.assessment_signature ==
          "eff527dcf35c7340b3c616e5e9d5d34ba17c09839be2a3de26e32ab745845d7c");
  REQUIRE(assessment.observability.assessment_signature ==
          "a3381ca89546c0a791fdd6beadbac940072777742fa3e0d0109df468824e501d");
  REQUIRE(assessment.assessment_signature ==
          "40bcb82fbabb2310e51a06e779e101f2c7abfe024aad2182b66543c2128b64b0");
}

TEST_CASE("SAA nonlinear Lie assessment enforces bounded exact scope") {
  using namespace statewright;
  const auto system = double_integrator();
  REQUIRE_THROWS_AS(saa::assess_nonlinear_lie_structure(system, {2, 0}, 2),
                    common::Error);
  REQUIRE_THROWS_AS(saa::assess_nonlinear_lie_structure(system, {0, 0}, -1),
                    common::Error);
  REQUIRE_THROWS_AS(
      saa::assess_nonlinear_lie_structure(system, {0, 0},
                                          saa::max_lie_depth + 1),
      common::Error);
}
