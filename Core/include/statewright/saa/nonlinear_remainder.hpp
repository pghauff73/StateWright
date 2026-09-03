#pragma once

#include "statewright/saa/nonlinear_evidence.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view nonlinear_remainder_version =
    "saa-nonlinear-remainder-v1";
inline constexpr std::size_t max_remainder_terms = 4096U;

struct DerivativeRemainderTerm final {
  std::size_t output_index = 0U;
  std::vector<int> powers;
  NumericCoefficient absolute_upper = 0;
};

struct ValidatedTaylorRemainder final {
  int schema_version = 1;
  std::string remainder_version = std::string(nonlinear_remainder_version);
  std::string proof_kind;
  std::string parent_jet_signature;
  std::vector<mpq_class> center;
  std::vector<mpq_class> validity_radius;
  std::size_t order = 0U;
  std::vector<mpq_class> output_absolute_upper;
  bool exact_containment = false;
  std::string source_snapshot_hash;
  bool independent_validation = false;
  bool global_equivalence_eligible = false;
  std::string certificate_signature;
  std::vector<std::string> warnings;
};

struct LocalBehaviorDeltaBound final {
  std::string status;
  std::vector<mpq_class> center;
  std::vector<mpq_class> overlap_radius;
  std::vector<mpq_class> output_absolute_upper;
  bool exact_zero_difference = false;
  bool local_equivalence_eligible = false;
  bool global_equivalence_eligible = false;
  std::string signature;
};

[[nodiscard]] contracts::Json to_json(const DerivativeRemainderTerm &value);
[[nodiscard]] contracts::Json to_json(const ValidatedTaylorRemainder &value);
[[nodiscard]] contracts::Json to_json(const LocalBehaviorDeltaBound &value);

[[nodiscard]] ValidatedTaylorRemainder certify_polynomial_remainder(
    const GovernedJetEvidence &evidence,
    const ExactPolynomialSystem &full_polynomial);
[[nodiscard]] ValidatedTaylorRemainder certify_derivative_remainder(
    const CanonicalTaylorJet &jet,
    const std::vector<DerivativeRemainderTerm> &derivative_bounds,
    std::string source_snapshot_hash, bool independent_validation);
[[nodiscard]] LocalBehaviorDeltaBound bound_local_behavior_difference(
    const CanonicalTaylorJet &left, const CanonicalTaylorJet &right,
    const ValidatedTaylorRemainder &left_remainder,
    const ValidatedTaylorRemainder &right_remainder);

} // namespace statewright::saa
