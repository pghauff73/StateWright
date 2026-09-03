#pragma once

#include "statewright/saa/nonlinear_evidence.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view nonlinear_lift_version =
    "saa-nonlinear-lift-v1";
inline constexpr std::size_t max_lift_state_dimension = 5U;
inline constexpr std::size_t max_lift_degree = 5U;
inline constexpr std::size_t max_lift_observables = 256U;
inline constexpr std::size_t max_lift_remainder_terms = 1024U;

struct LiftRemainderTerm final {
  std::size_t observable_index = 0U;
  std::vector<int> powers;
  mpq_class coefficient;
};

struct CarlemanKoopmanLift final {
  int schema_version = 1;
  std::string lift_version = std::string(nonlinear_lift_version);
  std::size_t state_dimension = 0U;
  std::size_t lift_degree = 0U;
  std::vector<std::vector<int>> basis;
  RationalMatrix generator_matrix;
  std::vector<std::size_t> state_reconstruction_indices;
  std::vector<LiftRemainderTerm> remainder_terms;
  bool exact_finite_closure = false;
  std::string status;
  bool discovery_aid_only = true;
  bool canonical_equivalence_eligible = false;
  std::string lift_signature;
  std::vector<std::string> warnings;
};

[[nodiscard]] contracts::Json to_json(const LiftRemainderTerm &value);
[[nodiscard]] contracts::Json to_json(const CarlemanKoopmanLift &value);
[[nodiscard]] CarlemanKoopmanLift
build_carleman_koopman_lift(const ExactPolynomialSystem &dynamics,
                            std::size_t lift_degree);

} // namespace statewright::saa
