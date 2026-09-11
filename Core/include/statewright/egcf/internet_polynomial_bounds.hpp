#pragma once

#include "statewright/common/error.hpp"
#include "statewright/egcf/internet_polynomial.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>

namespace statewright::egcf {

// A mathematical derivation under the native exact-arithmetic contract, not an
// independent reviewer verdict or a bound on GMP's internal allocation space.
[[nodiscard]] inline contracts::Json derive_internet_polynomial_bounds(
    const contracts::Json &ir) {
  const auto program = internet_exact_polynomial_program(ir);
  if (program.direct_power_sum) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_BOUND_DERIVATION_REQUIRES_HORNER");
  }
  const auto contract = internet_polynomial_contract(program);
  const std::size_t count = program.coefficients.size();
  const std::size_t degree = count - 1U;
  const auto input_bits = contract.at("maximum_input_bits").get<std::size_t>();
  std::size_t denominator_bits = degree * input_bits;
  std::size_t maximum_numerator_bits = 1;
  mpq_class absolute_sum = 0;
  for (const auto &coefficient : program.coefficients) {
    denominator_bits += mpz_sizeinbase(coefficient.get_den().get_mpz_t(), 2);
    maximum_numerator_bits = std::max(maximum_numerator_bits,
        mpz_sizeinbase(coefficient.get_num().get_mpz_t(), 2));
    if (coefficient < 0) absolute_sum -= coefficient;
    else absolute_sum += coefficient;
  }
  std::size_t count_bits = 0;
  for (std::size_t capacity = 1; capacity < count; capacity *= 2U) ++count_bits;
  const auto numerator_bits = denominator_bits + maximum_numerator_bits + count_bits;
  const auto component_bits = std::max(denominator_bits, numerator_bits);
  mpq_class lower = -absolute_sum;
  mpq_class upper = absolute_sum;
  std::string range_method = "TRIANGLE_INEQUALITY_ON_ABS_X_AT_MOST_ONE";
  if (program.coefficients == std::vector<mpq_class>{-1, 0, 2}) {
    lower = -1;
    upper = 1;
    range_method = "QUADRATIC_SPECIALIZATION_ZERO_LE_X_SQUARED_LE_ONE";
  }
  return contracts::Json{
      {"version", "internet-exact-polynomial-bound-derivation-v1"},
      {"status", "ANALYTICAL_DERIVATION_REVIEW_REQUIRED"},
      {"candidate_ir_sha256", contracts::sha256_json(ir)},
      {"execution_contract_sha256", contracts::sha256_json(contract)},
      {"degree", degree}, {"absolute_coefficient_sum", absolute_sum.get_str()},
      {"output_minimum", lower.get_str()}, {"output_maximum", upper.get_str()},
      {"output_range_method", range_method},
      {"maximum_checked_denominator_bits", denominator_bits},
      {"maximum_checked_numerator_bits", numerator_bits},
      {"maximum_checked_component_bits", component_bits},
      {"full_domain_checked_component_limit_sufficient", component_bits <= program.maximum_integer_bits},
      {"absolute_arithmetic_error_on_success", "0"},
      {"argument", contracts::Json::array({
          "On [-1,1], every monomial has absolute value at most one; use the sum of absolute coefficients",
          "A Horner partial denominator divides the product of its coefficient denominators times the input denominator to its degree",
          "The product denominator bit bound is the sum of factor bit bounds",
          "A partial value is bounded by coefficient_count times 2^maximum_coefficient_numerator_bits",
          "Combining the denominator bound and value bound bounds reduced numerator bits; reduction cannot increase either component",
          "For coefficients [-1,0,2], 0 <= x*x <= 1 implies -1 <= 2*x*x-1 <= 1"})},
      {"assumptions", contracts::Json::array({
          "Native validated Horner structure and fixed coefficients",
          "Canonical exact-rational input within the declared domain and input-bit limit",
          "Correct exact-rational primitive implementation; no rounding or approximate source coefficients"})},
      {"limitations", contracts::Json::array({
          "Not a proof-kernel certificate or independent reviewer evidence",
          "Does not establish truth of the source's named-function identity",
          "Bounds checked rational components, not GMP temporary allocations, wall time or memory exhaustion",
          "No benchmark, longitudinal-integrity, probation or acceptance claim"})},
      {"qualification_claim", "NONE"}, {"admission", false}};
}

} // namespace statewright::egcf
