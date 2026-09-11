#include "statewright/egcf/internet_polynomial_bounds.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace statewright;

TEST_CASE("internet polynomial analytical bounds remain conditional on review") {
  const auto quadratic = egcf::derive_internet_polynomial_bounds(
      egcf::internet_exact_polynomial_ir({-1, 0, 2}));
  CHECK(quadratic.at("output_minimum") == "-1");
  CHECK(quadratic.at("output_maximum") == "1");
  CHECK(quadratic.at("absolute_coefficient_sum") == "3");
  CHECK(quadratic.at("full_domain_checked_component_limit_sufficient") == true);
  CHECK(quadratic.at("qualification_claim") == "NONE");
  CHECK(quadratic.at("admission") == false);
  const auto changed = egcf::derive_internet_polynomial_bounds(
      egcf::internet_exact_polynomial_ir({-1, 0, 3}));
  CHECK(changed.at("output_minimum") == "-4");
  CHECK(changed.at("output_maximum") == "4");
  CHECK(changed.at("output_range_method") == "TRIANGLE_INEQUALITY_ON_ABS_X_AT_MOST_ONE");
  for (std::size_t degree = 0; degree <= 16; ++degree) {
    const auto bounds = egcf::derive_internet_polynomial_bounds(
        egcf::internet_exact_polynomial_ir(std::vector<mpq_class>(degree + 1U, 1)));
    CHECK(bounds.at("degree") == degree);
    CHECK(bounds.at("output_maximum") == std::to_string(degree + 1U));
    CHECK(bounds.at("full_domain_checked_component_limit_sufficient") == true);
    CHECK(bounds.at("maximum_checked_component_bits").get<std::size_t>() <= 8709U);
  }
  const auto rational = egcf::derive_internet_polynomial_bounds(
      egcf::internet_exact_polynomial_ir({mpq_class(-1, 3), mpq_class(1, 2)}));
  CHECK(rational.at("output_minimum") == "-5/6");
  CHECK(rational.at("output_maximum") == "5/6");
  CHECK(rational.at("absolute_arithmetic_error_on_success") == "0");
  REQUIRE_THROWS(egcf::derive_internet_polynomial_bounds(
      egcf::internet_exact_polynomial_reference_ir({-1, 0, 2})));
}
