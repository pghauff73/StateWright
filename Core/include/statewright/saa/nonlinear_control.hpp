#pragma once

#include "statewright/saa/nonlinear_geometry.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view nonlinear_control_version =
    "saa-nonlinear-observability-controllability-v1";
inline constexpr std::size_t max_local_state_dimension = 12U;

using NumericMatrix = std::vector<std::vector<NumericCoefficient>>;

struct ExactLocalDynamicLinearization final {
  int schema_version = 1;
  std::string control_version = std::string(nonlinear_control_version);
  std::string parent_representative_behavior_signature;
  std::size_t state_count = 0U;
  std::size_t control_count = 0U;
  std::size_t output_count = 0U;
  std::vector<std::string> state_meanings;
  RationalMatrix a;
  RationalMatrix b;
  RationalMatrix c;
  std::string linearization_signature;
};

struct RepresentativeControlAssessment final {
  int schema_version = 1;
  std::string control_version = std::string(nonlinear_control_version);
  std::string parent_representative_behavior_signature;
  std::string jet_local_behavior_signature;
  std::size_t input_observability_rank = 0U;
  std::size_t representative_input_count = 0U;
  bool representative_inputs_locally_observable = false;
  std::size_t invariant_unobservable_direction_count = 0U;
  bool dynamic_model_supplied = false;
  std::string dynamic_linearization_signature;
  std::size_t state_count = 0U;
  std::size_t controllability_rank = 0U;
  std::size_t observability_rank = 0U;
  std::optional<bool> dynamically_controllable;
  std::optional<bool> dynamically_observable;
  std::string status;
  bool canonical_control_eligible = false;
  std::string assessment_signature;
  std::vector<std::string> warnings;
};

[[nodiscard]] contracts::Json
 to_json(const ExactLocalDynamicLinearization &value);
[[nodiscard]] contracts::Json
 to_json(const RepresentativeControlAssessment &value);

[[nodiscard]] ExactLocalDynamicLinearization make_local_dynamic_linearization(
    const CanonicalRepresentativeAlgorithmForm &form, NumericMatrix a,
    NumericMatrix b, NumericMatrix c, std::vector<std::string> state_meanings);
[[nodiscard]] RationalMatrix controllability_matrix(
    const ExactLocalDynamicLinearization &linearization);
[[nodiscard]] RationalMatrix observability_matrix(
    const ExactLocalDynamicLinearization &linearization);
[[nodiscard]] RepresentativeControlAssessment
assess_representative_observability_controllability(
    const CanonicalRepresentativeAlgorithmForm &form,
    const CanonicalTaylorJet &jet,
    const DifferentialGeometryAssessment *geometry = nullptr,
    const ExactLocalDynamicLinearization *dynamic_linearization = nullptr);

} // namespace statewright::saa
