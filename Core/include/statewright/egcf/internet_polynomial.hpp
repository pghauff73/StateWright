#pragma once

#include "statewright/contracts/canonical_json.hpp"
#include <gmpxx.h>
#include <cstddef>
#include <string_view>
#include <vector>

namespace statewright::egcf {

inline constexpr std::string_view internet_polynomial_version =
    "internet-exact-rational-horner-v1";
inline constexpr std::string_view internet_polynomial_protocol_version =
    "saa-grounded-polynomial-experiment-v1";
inline constexpr std::string_view internet_polynomial_reference_version =
    "internet-exact-rational-power-sum-v1";

struct InternetExactPolynomialProgram final {
  // Fixed coefficients in ascending power order. Inputs are rationals in [-1,1].
  std::vector<mpq_class> coefficients;
  std::size_t maximum_integer_bits = 16384;
  bool direct_power_sum = false;
};

// Construct a closed graph, not source translation or qualification evidence.
[[nodiscard]] contracts::Json internet_exact_polynomial_ir(
    const std::vector<mpq_class> &coefficients,
    std::size_t maximum_integer_bits = 16384);
[[nodiscard]] contracts::Json internet_exact_polynomial_reference_ir(
    const std::vector<mpq_class> &coefficients,
    std::size_t maximum_integer_bits = 16384);
[[nodiscard]] InternetExactPolynomialProgram
internet_exact_polynomial_program(const contracts::Json &mapping);
[[nodiscard]] mpq_class internet_execute_exact_polynomial(
    const InternetExactPolynomialProgram &program, const mpq_class &input);
[[nodiscard]] contracts::Json internet_polynomial_contract(
    const InternetExactPolynomialProgram &program);

// Raw same-process observations only. No protocol registration, score mapping,
// source review, longitudinal observations or qualification claims are implied.
[[nodiscard]] contracts::Json internet_measure_polynomial_pair(
    const contracts::Json &candidate_ir, const contracts::Json &baseline_ir,
    const std::vector<mpq_class> &frozen_inputs, std::size_t repetitions = 10);

} // namespace statewright::egcf
