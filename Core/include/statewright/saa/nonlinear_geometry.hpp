#pragma once

#include "statewright/saa/nonlinear_jet.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view nonlinear_geometry_version =
    "saa-nonlinear-geometry-v1";

using RationalTensor3 = std::vector<RationalMatrix>;

struct DifferentialGeometryAssessment final {
  int schema_version = 1;
  std::string geometry_version = std::string(nonlinear_geometry_version);
  std::string parent_representative_behavior_signature;
  std::string jet_local_behavior_signature;
  std::vector<mpq_class> point;
  RationalMatrix jacobian;
  std::size_t jacobian_rank = 0U;
  RationalMatrix tangent_nullspace;
  std::size_t local_manifold_dimension = 0U;
  bool local_diffeomorphism = false;
  RationalTensor3 hessian;
  std::size_t cross_curvature_count = 0U;
  RationalMatrix invariant_distribution_basis;
  std::size_t invariant_distribution_dimension = 0U;
  bool invariant_distribution_integrable = true;
  std::size_t behavioral_input_dimension = 0U;
  std::string status;
  bool canonical_geometry_eligible = false;
  std::string assessment_signature;
  std::vector<std::string> warnings;
};

[[nodiscard]] std::size_t exact_matrix_rank(const RationalMatrix &matrix);
[[nodiscard]] RationalMatrix exact_nullspace(
    const RationalMatrix &matrix,
    std::optional<std::size_t> column_count = std::nullopt);
[[nodiscard]] RationalMatrix jacobian_at(
    const CanonicalTaylorJet &jet,
    const std::vector<NumericCoefficient> &point);
[[nodiscard]] RationalTensor3 hessian_at(
    const CanonicalTaylorJet &jet,
    const std::vector<NumericCoefficient> &point);
[[nodiscard]] DifferentialGeometryAssessment assess_nonlinear_geometry(
    const CanonicalRepresentativeAlgorithmForm &form,
    const CanonicalTaylorJet &jet,
    std::optional<std::vector<NumericCoefficient>> point = std::nullopt);

[[nodiscard]] contracts::Json
 to_json(const DifferentialGeometryAssessment &value);

} // namespace statewright::saa
