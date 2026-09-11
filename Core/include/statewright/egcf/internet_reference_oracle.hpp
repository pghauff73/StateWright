#pragma once

#include "statewright/egcf/internet_chebyshev_integer_reference.hpp"
#include "statewright/egcf/internet_experiment.hpp"

namespace statewright::egcf {

struct InternetReferenceOracle final {
  std::optional<InternetExactScalarProgram> scalar;
  std::optional<int> chebyshev_degree;
};

[[nodiscard]] inline contracts::Json chebyshev_reference_oracle_descriptor(int degree) {
  if (degree < 0 || degree > 16) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "CHEBYSHEV_ORACLE_DEGREE_OUTSIDE_CONTRACT");
  }
  return {{"kind", "CHEBYSHEV_INTEGER_REFERENCE_ORACLE_V1"},
      {"reference_version", internet_chebyshev_integer_reference_version},
      {"degree", degree}, {"maximum_denominator", 8}};
}

// A reference descriptor is not candidate SAA IR. Legacy scalar oracle IR keeps
// its original parser and execution path. No independence verdict is inferred.
[[nodiscard]] inline InternetReferenceOracle internet_reference_oracle_program(
    const contracts::Json &mapping) {
  if (mapping.value("kind", std::string{}) == "CHEBYSHEV_INTEGER_REFERENCE_ORACLE_V1") {
    const auto &degree = mapping.at("degree");
    if (!degree.is_number_integer() || degree < 0 || degree > 16 ||
        mapping != chebyshev_reference_oracle_descriptor(degree.get<int>())) {
      throw common::Error(common::ErrorCode::invalid_argument,
                          "CHEBYSHEV_ORACLE_DESCRIPTOR_INVALID");
    }
    return {.scalar = std::nullopt, .chebyshev_degree = degree.get<int>()};
  }
  return {.scalar = internet_exact_scalar_program(mapping), .chebyshev_degree = std::nullopt};
}

inline void verify_internet_reference_oracle_scope(const InternetReferenceOracle &oracle,
    const InternetExactScalarProgram &candidate) {
  if (!oracle.chebyshev_degree) return;
  if (!candidate.polynomial || candidate.polynomial->direct_power_sum) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "CHEBYSHEV_ORACLE_REQUIRES_POLYNOMIAL_HORNER_CANDIDATE");
  }
  auto coefficients = candidate.polynomial->coefficients;
  while (coefficients.size() > 1U && coefficients.back() == 0) coefficients.pop_back();
  if (coefficients.size() - 1U != static_cast<std::size_t>(*oracle.chebyshev_degree)) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "CHEBYSHEV_ORACLE_CANDIDATE_DEGREE_MISMATCH");
  }
}

[[nodiscard]] inline mpq_class internet_execute_reference_oracle(
    const InternetReferenceOracle &oracle, const mpq_class &input) {
  if (oracle.chebyshev_degree && !oracle.scalar) {
    // GMP is only the transport type here. The numerical recurrence and
    // normalization of its output use the separately implemented int64 path.
    if (input.get_den() < 1 || input.get_den() > 8 ||
        input.get_num() < -8 || input.get_num() > 8) {
      throw common::Error(common::ErrorCode::invalid_argument,
                          "CHEBYSHEV_ORACLE_INPUT_OUTSIDE_BOUNDED_REFERENCE_DOMAIN");
    }
    return mpq_class(internet_chebyshev_integer_reference(*oracle.chebyshev_degree,
        input.get_num().get_si(), input.get_den().get_si()));
  }
  if (oracle.scalar && !oracle.chebyshev_degree) {
    return internet_execute_exact_scalar(*oracle.scalar, input);
  }
  throw common::Error(common::ErrorCode::invalid_argument, "REFERENCE_ORACLE_VARIANT_INVALID");
}

} // namespace statewright::egcf
