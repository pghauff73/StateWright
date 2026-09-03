#include "nonlinear_fixture.hpp"

#include "statewright/common/error.hpp"
#include "statewright/saa/nonlinear_remainder.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

statewright::saa::ExactPolynomialSystem polynomial(
    std::size_t inputs, std::size_t outputs,
    std::vector<statewright::saa::ExactPolynomialTerm> terms) {
  return {.input_count = inputs,
          .output_count = outputs,
          .terms = std::move(terms)};
}

} // namespace

TEST_CASE("SAA nonlinear exact polynomial remainder matches frozen oracle") {
  using namespace statewright;
  const auto form = tests::nonlinear_parent_form(
      std::vector<std::string>{"normalized state"});
  const auto full = polynomial(
      1U, 1U,
      {{.output_index = 0U, .powers = {1}, .coefficient = 1},
       {.output_index = 0U, .powers = {3}, .coefficient = 1}});
  const auto evidence = saa::acquire_exact_polynomial_jet(
      form, full, {"1/2"}, {"1/4"}, 2U);
  const auto certificate = saa::certify_polynomial_remainder(evidence, full);

  REQUIRE(form.representative_behavior_signature ==
          "1394516c8cee2c8517be9db66d2ede03afd716bf1a1fd14c8d085d7d52444b60");
  REQUIRE(evidence.evidence_signature ==
          "5fc13308f24964332e02c074ac9482b8e2fc44ee9e0514a4647442c81d4106b6");
  REQUIRE(certificate.output_absolute_upper ==
          std::vector<mpq_class>{mpq_class(1, 64)});
  REQUIRE(certificate.exact_containment);
  REQUIRE_FALSE(certificate.global_equivalence_eligible);
  REQUIRE(certificate.certificate_signature ==
          "e558bcae906a7177fae3959e22fa88cba4eec09ec6f8ec3ac7e0da26b7e56ba2");

  REQUIRE(evidence.jet.has_value());
  const auto derivative = saa::certify_derivative_remainder(
      *evidence.jet,
      {{.output_index = 0U, .powers = {3}, .absolute_upper = 6}},
      std::string(64U, 'd'), true);
  REQUIRE(derivative.output_absolute_upper ==
          std::vector<mpq_class>{mpq_class(1, 64)});
  REQUIRE(derivative.certificate_signature ==
          "964c71e8f2d8d52ba80b57a975c089f1c33779915eca6a5606927d1cf5e6c346");
}

TEST_CASE("SAA nonlinear behavior delta is exact only with zero remainders") {
  using namespace statewright;
  const auto form = tests::nonlinear_parent_form(
      std::vector<std::string>{"normalized state"});
  const auto full = polynomial(
      1U, 1U,
      {{.output_index = 0U, .powers = {1}, .coefficient = 1}});
  const auto left = saa::acquire_exact_polynomial_jet(
      form, full, {"1/2"}, {"1/4"}, 2U);
  const auto right = saa::acquire_exact_polynomial_jet(
      form, full, {"1/2"}, {"1/8"}, 2U);
  REQUIRE(left.jet.has_value());
  REQUIRE(right.jet.has_value());
  const auto left_remainder = saa::certify_polynomial_remainder(left, full);
  const auto right_remainder = saa::certify_polynomial_remainder(right, full);
  const auto delta = saa::bound_local_behavior_difference(
      *left.jet, *right.jet, left_remainder, right_remainder);
  REQUIRE(delta.status ==
          "EXACT_LOCAL_BEHAVIOR_MATCH_WITH_VALIDATED_REMAINDER");
  REQUIRE(delta.exact_zero_difference);
  REQUIRE(delta.local_equivalence_eligible);
  REQUIRE_FALSE(delta.global_equivalence_eligible);
  REQUIRE(delta.overlap_radius ==
          std::vector<mpq_class>{mpq_class(1, 8)});
  REQUIRE(delta.signature ==
          "21a58f1c3eae42a68c5856e06569a43dbef603ae79f35f3b4fdef6c7c1f3fddc");
}

TEST_CASE("SAA nonlinear remainder validation fails closed") {
  using namespace statewright;
  const auto form = tests::nonlinear_parent_form(
      std::vector<std::string>{"normalized state"});
  const auto full = polynomial(
      1U, 1U,
      {{.output_index = 0U, .powers = {3}, .coefficient = 1}});
  const auto evidence = saa::acquire_exact_polynomial_jet(
      form, full, {"1/2"}, {"1/4"}, 2U);
  REQUIRE(evidence.jet.has_value());
  REQUIRE_THROWS_AS(saa::certify_derivative_remainder(
                        *evidence.jet,
                        {{.output_index = 0U,
                          .powers = {3},
                          .absolute_upper = 6}},
                        std::string(64U, 'd'), false),
                    common::Error);
  REQUIRE_THROWS_AS(saa::certify_derivative_remainder(
                        *evidence.jet,
                        {{.output_index = 0U,
                          .powers = {2},
                          .absolute_upper = 6}},
                        std::string(64U, 'd'), true),
                    common::Error);
}
