#include "statewright/egcf/internet_chebyshev_integer_reference.hpp"
#include "statewright/egcf/internet_polynomial.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace statewright;

TEST_CASE("internet Chebyshev integer reference uses bounded non-GMP arithmetic") {
  const std::vector<std::vector<mpq_class>> coefficients = {
      {1}, {0, 1}, {-1, 0, 2}, {0, -3, 0, 4}};
  for (int degree = 0; degree <= 3; ++degree) {
    const auto candidate = egcf::internet_exact_polynomial_program(
        egcf::internet_exact_polynomial_ir(coefficients[static_cast<std::size_t>(degree)]));
    for (int denominator = 1; denominator <= 8; ++denominator) {
      for (int numerator = -denominator; numerator <= denominator; ++numerator) {
        mpq_class input(numerator, denominator);
        input.canonicalize();
        const mpq_class expected(egcf::internet_chebyshev_integer_reference(
            degree, numerator, denominator));
        CHECK(egcf::internet_execute_exact_polynomial(candidate, input) == expected);
      }
    }
  }
  const std::vector<std::string> at_zero = {"1", "0", "-1", "0"};
  for (int degree = 0; degree <= 16; ++degree) {
    CHECK(egcf::internet_chebyshev_integer_reference(degree, 1, 1) == "1");
    CHECK(egcf::internet_chebyshev_integer_reference(degree, -1, 1) == (degree % 2 ? "-1" : "1"));
    CHECK(egcf::internet_chebyshev_integer_reference(degree, 0, 8) == at_zero[static_cast<std::size_t>(degree % 4)]);
  }
  CHECK(egcf::internet_chebyshev_integer_reference(2, 1, 2) == "-1/2");
  CHECK(egcf::internet_chebyshev_integer_reference(2, 2, 4) == "-1/2");
  REQUIRE_THROWS(egcf::internet_chebyshev_integer_reference(-1, 0, 1));
  REQUIRE_THROWS(egcf::internet_chebyshev_integer_reference(17, 0, 1));
  REQUIRE_THROWS(egcf::internet_chebyshev_integer_reference(2, 0, 0));
  REQUIRE_THROWS(egcf::internet_chebyshev_integer_reference(2, 0, 9));
  REQUIRE_THROWS(egcf::internet_chebyshev_integer_reference(2, 9, 8));
  REQUIRE_THROWS(egcf::internet_chebyshev_integer_reference(2, -9, 8));
  REQUIRE_THROWS(egcf::chebyshev_integer_reference_detail::multiply(
      std::numeric_limits<std::int64_t>::max(), 2));
  REQUIRE_THROWS(egcf::chebyshev_integer_reference_detail::subtract(
      std::numeric_limits<std::int64_t>::min(), 1));
  const auto incorrect = egcf::internet_exact_polynomial_program(
      egcf::internet_exact_polynomial_ir({-1, 0, 3}));
  CHECK(egcf::internet_execute_exact_polynomial(incorrect, mpq_class(1)) !=
        mpq_class(egcf::internet_chebyshev_integer_reference(2, 1, 1)));
}
