#pragma once

#include "statewright/saa/nonlinear_evidence.hpp"
#include "statewright/saa/nonlinear_geometry.hpp"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view nonlinear_lie_version =
    "saa-nonlinear-lie-v1";
inline constexpr std::size_t max_lie_state_dimension = 6U;
inline constexpr std::size_t max_lie_control_fields = 6U;
inline constexpr int max_lie_depth = 4;
inline constexpr int max_lie_generated_objects = 256;
inline constexpr std::size_t max_lie_polynomial_terms = 1024U;

using ExactMultivariatePolynomial = std::map<std::vector<int>, mpq_class>;
using PolynomialVectorField = std::vector<ExactMultivariatePolynomial>;

struct ExactControlAffinePolynomialSystem final {
  std::size_t state_dimension = 0U;
  ExactPolynomialSystem drift;
  std::vector<ExactPolynomialSystem> control_fields;
  ExactPolynomialSystem outputs;
  std::vector<mpq_class> domain_center;
  std::vector<mpq_class> domain_radius;
};

struct LieAccessibilityAssessment final {
  std::string status;
  std::size_t state_dimension = 0U;
  std::size_t rank = 0U;
  std::size_t generated_field_count = 0U;
  int depth_reached = 0;
  bool full_rank = false;
  bool exact = true;
  bool local_only = true;
  bool global_accessibility_eligible = false;
  std::vector<std::string> field_signatures;
  std::string assessment_signature;
  std::vector<std::string> warnings;
};

struct LieObservabilityAssessment final {
  std::string status;
  std::size_t state_dimension = 0U;
  std::size_t rank = 0U;
  std::size_t generated_function_count = 0U;
  int depth_reached = 0;
  bool full_rank = false;
  bool exact = true;
  bool local_only = true;
  bool global_observability_eligible = false;
  std::vector<std::string> function_signatures;
  std::string assessment_signature;
  std::vector<std::string> warnings;
};

struct NonlinearLieAssessment final {
  int schema_version = 1;
  std::string lie_version = std::string(nonlinear_lie_version);
  std::vector<mpq_class> operating_point;
  LieAccessibilityAssessment accessibility;
  LieObservabilityAssessment observability;
  bool locally_accessible_and_observable = false;
  bool global_claim_eligible = false;
  std::string assessment_signature;
};

[[nodiscard]] PolynomialVectorField
lie_bracket(const PolynomialVectorField &left,
            const PolynomialVectorField &right);
[[nodiscard]] ExactMultivariatePolynomial
lie_derivative(const ExactMultivariatePolynomial &polynomial,
               const PolynomialVectorField &field);

[[nodiscard]] ExactControlAffinePolynomialSystem
make_control_affine_polynomial_system(
    std::size_t state_dimension, ExactPolynomialSystem drift,
    std::vector<ExactPolynomialSystem> control_fields,
    ExactPolynomialSystem outputs,
    std::vector<NumericCoefficient> domain_center,
    std::vector<NumericCoefficient> domain_radius);

[[nodiscard]] LieAccessibilityAssessment assess_lie_accessibility(
    const ExactControlAffinePolynomialSystem &system,
    std::vector<NumericCoefficient> operating_point,
    int max_depth = max_lie_depth,
    int max_generated = max_lie_generated_objects);
[[nodiscard]] LieObservabilityAssessment assess_lie_observability(
    const ExactControlAffinePolynomialSystem &system,
    std::vector<NumericCoefficient> operating_point,
    int max_depth = max_lie_depth,
    int max_generated = max_lie_generated_objects);
[[nodiscard]] NonlinearLieAssessment assess_nonlinear_lie_structure(
    const ExactControlAffinePolynomialSystem &system,
    std::vector<NumericCoefficient> operating_point,
    int max_depth = max_lie_depth);

[[nodiscard]] contracts::Json
to_json(const ExactControlAffinePolynomialSystem &value);
[[nodiscard]] contracts::Json to_json(const LieAccessibilityAssessment &value);
[[nodiscard]] contracts::Json to_json(const LieObservabilityAssessment &value);
[[nodiscard]] contracts::Json to_json(const NonlinearLieAssessment &value);

} // namespace statewright::saa
