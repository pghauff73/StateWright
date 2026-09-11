#include "statewright/egcf/internet_reference_oracle.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace statewright;
using namespace statewright::egcf;

TEST_CASE("internet integer reference oracle is separate bounded and not candidate IR") {
  const auto descriptor = chebyshev_reference_oracle_descriptor(2);
  const auto oracle = internet_reference_oracle_program(descriptor);
  CHECK_FALSE(oracle.scalar.has_value());
  REQUIRE(oracle.chebyshev_degree == 2);
  CHECK(internet_execute_reference_oracle(oracle, mpq_class(1, 3)) == mpq_class(-7, 9));
  CHECK(internet_execute_reference_oracle(oracle, -1) == 1);
  CHECK(internet_execute_reference_oracle(oracle, 0) == -1);
  REQUIRE_THROWS(internet_exact_scalar_program(descriptor));
  REQUIRE_THROWS(internet_execute_reference_oracle(oracle, mpq_class(1, 9)));
  REQUIRE_THROWS(internet_execute_reference_oracle(oracle, 2));
  auto altered = descriptor;
  altered["maximum_denominator"] = 100;
  REQUIRE_THROWS(internet_reference_oracle_program(altered));
  altered = descriptor;
  altered["degree"] = 2.5;
  REQUIRE_THROWS(internet_reference_oracle_program(altered));
  altered = descriptor;
  altered["extra"] = "unreviewed-extension";
  REQUIRE_THROWS(internet_reference_oracle_program(altered));
  REQUIRE_THROWS(chebyshev_reference_oracle_descriptor(17));
  const auto candidate = internet_exact_scalar_program(internet_exact_polynomial_ir({-1, 0, 2}));
  REQUIRE_NOTHROW(verify_internet_reference_oracle_scope(oracle, candidate));
  REQUIRE_THROWS(verify_internet_reference_oracle_scope(oracle,
      internet_exact_scalar_program(internet_exact_polynomial_ir({0, 1}))));
  REQUIRE_THROWS(verify_internet_reference_oracle_scope(oracle,
      internet_exact_scalar_program(internet_exact_polynomial_reference_ir({-1, 0, 2}))));
  // Deliberately incorrect coefficients still produce independently calculated
  // expected values; degree matching alone does not approve the candidate.
  const auto wrong = internet_exact_scalar_program(internet_exact_polynomial_ir({-1, 0, 3}));
  CHECK(internet_execute_reference_oracle(oracle, 1) != internet_execute_exact_scalar(wrong, 1));
}

TEST_CASE("internet reference oracle preserves existing polynomial oracle execution") {
  const auto ir = internet_exact_polynomial_reference_ir({1, 2, 3});
  const auto oracle = internet_reference_oracle_program(ir);
  CHECK(oracle.scalar.has_value());
  CHECK_FALSE(oracle.chebyshev_degree.has_value());
  CHECK(internet_execute_reference_oracle(oracle, mpq_class(1, 2)) == mpq_class(11, 4));
  for (int degree = 0; degree <= 16; ++degree) {
    const auto recurrence = internet_reference_oracle_program(chebyshev_reference_oracle_descriptor(degree));
    CHECK(internet_execute_reference_oracle(recurrence, 1) == 1);
    CHECK(internet_execute_reference_oracle(recurrence, -1) == (degree % 2 == 0 ? 1 : -1));
  }
}
