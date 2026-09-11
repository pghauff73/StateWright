#pragma once

#include "statewright/common/error.hpp"

#include <cstdint>
#include <limits>
#include <numeric>
#include <string>

namespace statewright::egcf {

inline constexpr const char *internet_chebyshev_integer_reference_version =
    "chebyshev-scaled-integer-recurrence-v1";

namespace chebyshev_integer_reference_detail {

[[nodiscard]] inline std::int64_t multiply(std::int64_t value, std::int64_t factor) {
  constexpr auto limit = std::numeric_limits<std::int64_t>::max() / 64;
  if (factor < -64 || factor > 64 || value < -limit || value > limit)
    throw common::Error(common::ErrorCode::invalid_argument,
                        "CHEBYSHEV_INTEGER_REFERENCE_RESOURCE_LIMIT");
  return value * factor;
}

[[nodiscard]] inline std::int64_t subtract(std::int64_t left, std::int64_t right) {
  constexpr auto limit = std::numeric_limits<std::int64_t>::max() / 2;
  if (left < -limit || left > limit || right < -limit || right > limit)
    throw common::Error(common::ErrorCode::invalid_argument,
                        "CHEBYSHEV_INTEGER_REFERENCE_RESOURCE_LIMIT");
  return left - right;
}

} // namespace chebyshev_integer_reference_detail

// Mathematical reference: T_0=1, T_1=x, T_n=2*x*T_(n-1)-T_(n-2).
// The recurrence is also documented in SymPy orthopolys.py at revision
// 16fa855354eb7bcabd3fe10993841e03b1382692. This is an original integer
// implementation, not execution of that downloaded source. Source admissibility
// and independent review are separate requirements. Shared author/compiler/host
// must still be disclosed; different arithmetic is not full independence.
//
// With x=p/q, R_n=q^n*T_n(p/q) satisfies
// R_0=1, R_1=p, R_n=2*p*R_(n-1)-q*q*R_(n-2).
// This implementation does not consume candidate coefficients, Horner IR or GMP.
[[nodiscard]] inline std::string internet_chebyshev_integer_reference(
    int degree, std::int64_t numerator, std::int64_t denominator) {
  if (degree < 0 || degree > 16 || denominator < 1 || denominator > 8 ||
      numerator < -denominator || numerator > denominator)
    throw common::Error(common::ErrorCode::invalid_argument,
                        "CHEBYSHEV_INTEGER_REFERENCE_OUTSIDE_CONTRACT");
  if (degree == 0) return "1";
  std::int64_t older = 1;
  std::int64_t previous = numerator;
  std::int64_t output_denominator = denominator;
  for (int n = 2; n <= degree; ++n) {
    const auto next = chebyshev_integer_reference_detail::subtract(
        chebyshev_integer_reference_detail::multiply(previous, 2 * numerator),
        chebyshev_integer_reference_detail::multiply(older, denominator * denominator));
    older = previous;
    previous = next;
    output_denominator = chebyshev_integer_reference_detail::multiply(output_denominator, denominator);
  }
  // The arithmetic guards exclude INT64_MIN, so std::gcd has representable
  // absolute operands. Reduction also gives one canonical encoding of zero.
  const auto divisor = std::gcd(previous, output_denominator);
  previous /= divisor;
  output_denominator /= divisor;
  if (output_denominator == 1) return std::to_string(previous);
  return std::to_string(previous) + "/" + std::to_string(output_denominator);
}

} // namespace statewright::egcf
