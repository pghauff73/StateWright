#pragma once

#include "statewright/saa/nonlinear_jet.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view nonlinear_evidence_version =
    "saa-nonlinear-evidence-v1";

struct ExactDerivativeTerm final {
  std::size_t output_index = 0U;
  std::vector<int> powers;
  NumericCoefficient derivative = 0;
};

struct ExactPolynomialTerm final {
  std::size_t output_index = 0U;
  std::vector<int> powers;
  NumericCoefficient coefficient = 0;
};

struct ExactPolynomialSystem final {
  std::size_t input_count = 0U;
  std::size_t output_count = 0U;
  std::vector<ExactPolynomialTerm> terms;
};

struct BoundedDerivativeEstimate final {
  std::size_t output_index = 0U;
  std::vector<int> powers;
  NumericCoefficient lower = 0;
  NumericCoefficient upper = 0;
};

struct CanonicalBoundedDerivativeEstimate final {
  std::size_t output_index = 0U;
  std::vector<int> powers;
  mpq_class lower;
  mpq_class upper;
};

struct GovernedJetEvidence final {
  int schema_version = 1;
  std::string evidence_version = std::string(nonlinear_evidence_version);
  std::string evidence_kind;
  std::string parent_representative_behavior_signature;
  std::string source_snapshot_hash;
  std::string producer;
  std::string method;
  bool independent_acquisition = false;
  bool exact = false;
  bool canonical_local_eligible = false;
  std::vector<mpq_class> center;
  std::vector<mpq_class> validity_radius;
  std::size_t order = 0U;
  std::optional<CanonicalTaylorJet> jet;
  std::vector<CanonicalBoundedDerivativeEstimate> estimated_derivatives;
  std::string evidence_signature;
  std::vector<std::string> warnings;
};

[[nodiscard]] contracts::Json to_json(const ExactDerivativeTerm &value);
[[nodiscard]] contracts::Json to_json(const ExactPolynomialTerm &value);
[[nodiscard]] contracts::Json to_json(const ExactPolynomialSystem &value);
[[nodiscard]] contracts::Json
 to_json(const CanonicalBoundedDerivativeEstimate &value);
[[nodiscard]] contracts::Json to_json(const GovernedJetEvidence &value);

[[nodiscard]] GovernedJetEvidence acquire_exact_derivative_jet(
    const CanonicalRepresentativeAlgorithmForm &form,
    std::vector<NumericCoefficient> center,
    std::vector<NumericCoefficient> validity_radius, std::size_t order,
    const std::vector<ExactDerivativeTerm> &derivatives,
    std::string source_snapshot_hash, std::string producer,
    std::string method = "exact-derivative-table",
    bool independent_acquisition = true);

[[nodiscard]] GovernedJetEvidence acquire_exact_polynomial_jet(
    const CanonicalRepresentativeAlgorithmForm &form,
    const ExactPolynomialSystem &system,
    std::vector<NumericCoefficient> center,
    std::vector<NumericCoefficient> validity_radius, std::size_t order);

[[nodiscard]] GovernedJetEvidence acquire_bounded_estimated_derivatives(
    const CanonicalRepresentativeAlgorithmForm &form,
    std::vector<NumericCoefficient> center,
    std::vector<NumericCoefficient> validity_radius, std::size_t order,
    const std::vector<BoundedDerivativeEstimate> &estimates,
    std::string source_snapshot_hash, std::string producer,
    std::string method);

} // namespace statewright::saa
