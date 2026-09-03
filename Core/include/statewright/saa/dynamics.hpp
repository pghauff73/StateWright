#pragma once

#include "statewright/saa/algorithm_ir.hpp"
#include "statewright/saa/normalization.hpp"

#include <gmpxx.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view dynamics_version =
    "saa-linear-dynamics-v1";
inline constexpr std::size_t max_state_order = 12U;
inline constexpr std::size_t max_polynomial_degree = 64U;

class NumericCoefficient final {
public:
  NumericCoefficient(int value);
  NumericCoefficient(long value);
  NumericCoefficient(long long value);
  NumericCoefficient(double value);
  NumericCoefficient(const char *value);
  NumericCoefficient(std::string value);
  NumericCoefficient(mpq_class value);

  [[nodiscard]] const mpq_class &value() const noexcept;
  [[nodiscard]] bool exact() const noexcept;
  [[nodiscard]] const std::string &source() const noexcept;

private:
  mpq_class value_;
  bool exact_ = true;
  std::string source_;
};

using Polynomial = std::vector<NumericCoefficient>;
using RationalPolynomial = std::vector<mpq_class>;
using CoefficientMatrix = std::vector<std::vector<NumericCoefficient>>;
using RationalMatrix = std::vector<std::vector<mpq_class>>;

struct ReducedTransfer final {
  RationalPolynomial numerator;
  RationalPolynomial denominator;
  std::vector<std::string> reductions;
};

struct LinearTransferFunction final {
  std::string domain;
  Polynomial numerator;
  Polynomial denominator;
  std::optional<NumericCoefficient> sample_period;
  contracts::Json metadata = contracts::Json::object();

  LinearTransferFunction(
      std::string domain_value, Polynomial numerator_value,
      Polynomial denominator_value,
      std::optional<NumericCoefficient> sample_period_value = std::nullopt,
      contracts::Json metadata_value = contracts::Json::object());
};

struct LinearStateSpace final {
  std::string domain;
  CoefficientMatrix a;
  CoefficientMatrix b;
  CoefficientMatrix c;
  NumericCoefficient d;
  std::optional<NumericCoefficient> sample_period;
  contracts::Json metadata = contracts::Json::object();

  LinearStateSpace(
      std::string domain_value, CoefficientMatrix a_value,
      CoefficientMatrix b_value, CoefficientMatrix c_value,
      NumericCoefficient d_value = 0,
      std::optional<NumericCoefficient> sample_period_value = std::nullopt,
      contracts::Json metadata_value = contracts::Json::object());
};

struct CanonicalLinearDynamics final {
  int schema_version = 1;
  std::string dynamics_version_value = std::string(dynamics_version);
  std::string domain;
  std::string variable;
  RationalPolynomial numerator;
  RationalPolynomial denominator;
  int dynamic_order = 0;
  std::optional<int> relative_degree;
  bool proper = true;
  std::optional<mpq_class> normalized_sample_interval;
  std::string dynamic_strength;
  std::string normalization_signature;
  std::string audit_hash;
  std::string canonical_signature;
  std::vector<std::string> reductions;
  std::vector<std::string> warnings;
};

[[nodiscard]] contracts::Json rational_json(const mpq_class &value);
[[nodiscard]] contracts::Json polynomial_json(const RationalPolynomial &value);
[[nodiscard]] contracts::Json to_json(const CanonicalLinearDynamics &value);
[[nodiscard]] ReducedTransfer reduce_exact_transfer(
    RationalPolynomial numerator, RationalPolynomial denominator);

[[nodiscard]] CanonicalLinearDynamics canonicalize_transfer_function(
    const LinearTransferFunction &transfer,
    const NormalizationContract &normalization);
[[nodiscard]] CanonicalLinearDynamics canonicalize_state_space(
    const LinearStateSpace &state_space,
    const NormalizationContract &normalization);
[[nodiscard]] std::string dynamic_algorithm_signature(
    const CanonicalAlgorithmIR &structural_ir,
    const NormalizationContract &normalization,
    const CanonicalLinearDynamics &dynamics);

} // namespace statewright::saa
