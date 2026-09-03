#include "statewright/common/error.hpp"
#include "statewright/saa/nonlinear_lift.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

statewright::saa::ExactPolynomialSystem dynamics(int power) {
  return {.input_count = 1U,
          .output_count = 1U,
          .terms = {{.output_index = 0U,
                     .powers = {power},
                     .coefficient = 1}}};
}

} // namespace

TEST_CASE("SAA nonlinear finite lift closes exact linear dynamics") {
  const auto lift = statewright::saa::build_carleman_koopman_lift(
      dynamics(1), 2U);
  REQUIRE(lift.exact_finite_closure);
  REQUIRE(lift.canonical_equivalence_eligible);
  REQUIRE_FALSE(lift.discovery_aid_only);
  REQUIRE(lift.remainder_terms.empty());
  REQUIRE(lift.generator_matrix ==
          statewright::saa::RationalMatrix{
              {mpq_class(0), mpq_class(0), mpq_class(0)},
              {mpq_class(0), mpq_class(1), mpq_class(0)},
              {mpq_class(0), mpq_class(0), mpq_class(2)}});
  REQUIRE(lift.lift_signature ==
          "85f51567f76480063aa3aa645a850d42340831893d497641f26cb6dd837d64e7");
}

TEST_CASE("SAA nonlinear finite lift exposes nonlinear truncation") {
  const auto lift = statewright::saa::build_carleman_koopman_lift(
      dynamics(2), 2U);
  REQUIRE_FALSE(lift.exact_finite_closure);
  REQUIRE_FALSE(lift.canonical_equivalence_eligible);
  REQUIRE(lift.discovery_aid_only);
  REQUIRE(lift.remainder_terms.size() == 1U);
  REQUIRE(lift.remainder_terms.front().powers == std::vector<int>{3});
  REQUIRE(lift.remainder_terms.front().coefficient == 2);
  REQUIRE(lift.lift_signature ==
          "5eafb17d6310cd9e5108e0203abba19190b5cc47f5d18cfa46544393bd80144f");
}

TEST_CASE("SAA nonlinear lift enforces bounded square dynamics") {
  using namespace statewright;
  const saa::ExactPolynomialSystem nonsquare = {
      .input_count = 1U, .output_count = 2U, .terms = {}};
  REQUIRE_THROWS_AS(saa::build_carleman_koopman_lift(nonsquare, 2U),
                    common::Error);
  REQUIRE_THROWS_AS(saa::build_carleman_koopman_lift(dynamics(1), 0U),
                    common::Error);
}
