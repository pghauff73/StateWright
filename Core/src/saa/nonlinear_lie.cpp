#include "statewright/saa/nonlinear_lie.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;
using Powers = std::vector<int>;

[[noreturn]] void lie_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

[[nodiscard]] mpq_class exact_value(const NumericCoefficient &value,
                                    std::string_view label) {
  if (!value.exact()) {
    lie_error(std::string(label) + " must be exact and cannot be float");
  }
  return value.value();
}

[[nodiscard]] int degree(const Powers &powers) {
  int result = 0;
  for (const int power : powers) {
    result += power;
  }
  return result;
}

[[nodiscard]] ExactMultivariatePolynomial
normalize(ExactMultivariatePolynomial polynomial) {
  for (auto iterator = polynomial.begin(); iterator != polynomial.end();) {
    if (iterator->second == 0) {
      iterator = polynomial.erase(iterator);
    } else {
      ++iterator;
    }
  }
  if (polynomial.size() > max_lie_polynomial_terms) {
    lie_error("SAA-7.8 polynomial term budget exceeded");
  }
  return polynomial;
}

[[nodiscard]] ExactMultivariatePolynomial add(
    ExactMultivariatePolynomial left,
    const ExactMultivariatePolynomial &right) {
  for (const auto &[powers, coefficient] : right) {
    left[powers] += coefficient;
    if (left[powers] == 0) {
      left.erase(powers);
    }
  }
  return normalize(std::move(left));
}

[[nodiscard]] ExactMultivariatePolynomial
scale(ExactMultivariatePolynomial polynomial, const mpq_class &factor) {
  for (auto &[powers, coefficient] : polynomial) {
    static_cast<void>(powers);
    coefficient *= factor;
  }
  return normalize(std::move(polynomial));
}

[[nodiscard]] Powers add_powers(const Powers &left, const Powers &right) {
  Powers result(left.size());
  for (std::size_t index = 0; index < left.size(); ++index) {
    result[index] = left[index] + right[index];
  }
  return result;
}

[[nodiscard]] ExactMultivariatePolynomial multiply(
    const ExactMultivariatePolynomial &left,
    const ExactMultivariatePolynomial &right) {
  ExactMultivariatePolynomial result;
  for (const auto &[left_powers, left_coefficient] : left) {
    for (const auto &[right_powers, right_coefficient] : right) {
      result[add_powers(left_powers, right_powers)] +=
          left_coefficient * right_coefficient;
    }
  }
  return normalize(std::move(result));
}

[[nodiscard]] ExactMultivariatePolynomial derivative(
    const ExactMultivariatePolynomial &polynomial, std::size_t variable) {
  ExactMultivariatePolynomial result;
  for (const auto &[powers, coefficient] : polynomial) {
    const int exponent = powers.at(variable);
    if (exponent == 0) {
      continue;
    }
    auto reduced = powers;
    --reduced[variable];
    result[std::move(reduced)] += coefficient * exponent;
  }
  return normalize(std::move(result));
}

[[nodiscard]] mpq_class rational_power(mpq_class value, int exponent) {
  mpq_class result = 1;
  for (int index = 0; index < exponent; ++index) {
    result *= value;
  }
  return result;
}

[[nodiscard]] mpq_class evaluate(
    const ExactMultivariatePolynomial &polynomial,
    const std::vector<mpq_class> &point) {
  mpq_class total = 0;
  for (const auto &[powers, coefficient] : polynomial) {
    mpq_class value = coefficient;
    for (std::size_t index = 0; index < powers.size(); ++index) {
      if (powers[index] != 0) {
        value *= rational_power(point[index], powers[index]);
      }
    }
    total += value;
  }
  return total;
}

[[nodiscard]] Json polynomial_payload(
    const ExactMultivariatePolynomial &polynomial) {
  std::vector<std::pair<Powers, mpq_class>> terms(polynomial.begin(),
                                                  polynomial.end());
  std::ranges::sort(terms, [](const auto &left, const auto &right) {
    return std::tuple(degree(left.first), left.first) <
           std::tuple(degree(right.first), right.first);
  });
  Json result = Json::array();
  for (const auto &[powers, coefficient] : terms) {
    result.push_back({{"coefficient", rational_json(coefficient)},
                      {"powers", powers}});
  }
  return result;
}

[[nodiscard]] std::string polynomial_signature(
    const ExactMultivariatePolynomial &polynomial) {
  return contracts::sha256_json(polynomial_payload(polynomial));
}

[[nodiscard]] Json vector_payload(const PolynomialVectorField &field) {
  Json result = Json::array();
  for (const auto &component : field) {
    result.push_back(polynomial_payload(component));
  }
  return result;
}

[[nodiscard]] std::string vector_signature(
    const PolynomialVectorField &field) {
  return contracts::sha256_json(vector_payload(field));
}

[[nodiscard]] PolynomialVectorField system_to_vector(
    const ExactPolynomialSystem &system, std::size_t state_dimension) {
  if (system.input_count != state_dimension ||
      system.output_count != state_dimension) {
    lie_error("SAA-7.8 vector field dimensions must match state dimension");
  }
  PolynomialVectorField components(state_dimension);
  for (const auto &item : system.terms) {
    if (item.powers.size() != state_dimension) {
      lie_error("SAA-7.8 vector-field monomial dimension mismatch");
    }
    if (item.output_index >= state_dimension) {
      lie_error("SAA-7.8 vector-field component outside state dimension");
    }
    if (std::ranges::any_of(item.powers,
                            [](int value) { return value < 0; })) {
      lie_error("SAA-7.8 polynomial powers cannot be negative");
    }
    components[item.output_index][item.powers] +=
        exact_value(item.coefficient, "SAA-7.8 polynomial coefficient");
  }
  for (auto &component : components) {
    component = normalize(std::move(component));
  }
  return components;
}

[[nodiscard]] std::vector<ExactMultivariatePolynomial> system_to_scalars(
    const ExactPolynomialSystem &system, std::size_t state_dimension) {
  if (system.input_count != state_dimension) {
    lie_error("SAA-7.8 output input dimension must match state dimension");
  }
  std::vector<ExactMultivariatePolynomial> outputs(system.output_count);
  for (const auto &item : system.terms) {
    if (item.output_index >= system.output_count) {
      lie_error("SAA-7.8 output component outside output dimension");
    }
    if (item.powers.size() != state_dimension) {
      lie_error("SAA-7.8 output monomial dimension mismatch");
    }
    outputs[item.output_index][item.powers] +=
        exact_value(item.coefficient, "SAA-7.8 output coefficient");
  }
  for (auto &output : outputs) {
    output = normalize(std::move(output));
  }
  return outputs;
}

[[nodiscard]] ExactMultivariatePolynomial directional_derivative(
    const ExactMultivariatePolynomial &polynomial,
    const PolynomialVectorField &field) {
  ExactMultivariatePolynomial result;
  for (std::size_t index = 0; index < field.size(); ++index) {
    result = add(std::move(result),
                 multiply(derivative(polynomial, index), field[index]));
  }
  return result;
}

[[nodiscard]] std::vector<mpq_class>
vector_at(const PolynomialVectorField &field,
          const std::vector<mpq_class> &point) {
  std::vector<mpq_class> result;
  result.reserve(field.size());
  for (const auto &component : field) {
    result.push_back(evaluate(component, point));
  }
  return result;
}

[[nodiscard]] std::vector<mpq_class>
gradient_at(const ExactMultivariatePolynomial &polynomial,
            const std::vector<mpq_class> &point) {
  std::vector<mpq_class> result;
  result.reserve(point.size());
  for (std::size_t index = 0; index < point.size(); ++index) {
    result.push_back(evaluate(derivative(polynomial, index), point));
  }
  return result;
}

[[nodiscard]] bool zero_vector(const PolynomialVectorField &field) {
  return std::ranges::all_of(
      field, [](const auto &component) { return component.empty(); });
}

[[nodiscard]] std::vector<mpq_class> exact_inputs(
    const std::vector<NumericCoefficient> &values, std::string_view label) {
  std::vector<mpq_class> result;
  result.reserve(values.size());
  for (const auto &value : values) {
    result.push_back(exact_value(value, label));
  }
  return result;
}

[[nodiscard]] std::vector<NumericCoefficient>
numeric_inputs(const std::vector<mpq_class> &values) {
  std::vector<NumericCoefficient> result;
  result.reserve(values.size());
  for (const auto &value : values) {
    result.emplace_back(value);
  }
  return result;
}

[[nodiscard]] std::vector<mpq_class> validate_point(
    const ExactControlAffinePolynomialSystem &system,
    const std::vector<NumericCoefficient> &point) {
  if (point.size() != system.state_dimension) {
    lie_error("SAA-7.8 operating-point dimension mismatch");
  }
  auto exact_point = exact_inputs(point, "SAA-7.8 operating point");
  for (std::size_t index = 0; index < exact_point.size(); ++index) {
    if (abs(exact_point[index] - system.domain_center[index]) >
        system.domain_radius[index]) {
      lie_error("SAA-7.8 operating point lies outside certified local domain");
    }
  }
  return exact_point;
}

[[nodiscard]] Json point_json(const std::vector<mpq_class> &point) {
  return polynomial_json(point);
}

} // namespace

PolynomialVectorField lie_bracket(const PolynomialVectorField &left,
                                  const PolynomialVectorField &right) {
  if (left.size() != right.size()) {
    lie_error("Lie bracket vector-field dimensions differ");
  }
  PolynomialVectorField result;
  result.reserve(left.size());
  for (std::size_t component = 0; component < left.size(); ++component) {
    auto first = directional_derivative(right[component], left);
    auto second = directional_derivative(left[component], right);
    result.push_back(add(std::move(first), scale(std::move(second), -1)));
  }
  return result;
}

ExactMultivariatePolynomial
lie_derivative(const ExactMultivariatePolynomial &polynomial,
               const PolynomialVectorField &field) {
  return directional_derivative(polynomial, field);
}

ExactControlAffinePolynomialSystem make_control_affine_polynomial_system(
    std::size_t state_dimension, ExactPolynomialSystem drift,
    std::vector<ExactPolynomialSystem> control_fields,
    ExactPolynomialSystem outputs,
    std::vector<NumericCoefficient> domain_center,
    std::vector<NumericCoefficient> domain_radius) {
  if (state_dimension < 1U || state_dimension > max_lie_state_dimension) {
    lie_error("SAA-7.8 state dimension must lie in [1," +
              std::to_string(max_lie_state_dimension) + "]");
  }
  if (control_fields.size() > max_lie_control_fields) {
    lie_error("SAA-7.8 control-field count exceeds bounded cap");
  }
  static_cast<void>(system_to_vector(drift, state_dimension));
  for (const auto &field : control_fields) {
    static_cast<void>(system_to_vector(field, state_dimension));
  }
  static_cast<void>(system_to_scalars(outputs, state_dimension));
  if (domain_center.size() != state_dimension ||
      domain_radius.size() != state_dimension) {
    lie_error("SAA-7.8 domain dimension mismatch");
  }
  auto center = exact_inputs(domain_center, "SAA-7.8 domain center");
  auto radius = exact_inputs(domain_radius, "SAA-7.8 domain radius");
  if (std::ranges::any_of(radius,
                          [](const mpq_class &value) { return value <= 0; })) {
    lie_error("SAA-7.8 domain radii must be positive");
  }
  return {.state_dimension = state_dimension,
          .drift = std::move(drift),
          .control_fields = std::move(control_fields),
          .outputs = std::move(outputs),
          .domain_center = std::move(center),
          .domain_radius = std::move(radius)};
}

LieAccessibilityAssessment assess_lie_accessibility(
    const ExactControlAffinePolynomialSystem &system,
    std::vector<NumericCoefficient> operating_point, int max_depth,
    int max_generated) {
  if (max_depth < 0 || max_depth > max_lie_depth) {
    lie_error("SAA-7.8 Lie depth outside bounded range");
  }
  if (max_generated < 1 || max_generated > max_lie_generated_objects) {
    lie_error("SAA-7.8 generated-field budget outside bounded range");
  }
  const auto point = validate_point(system, operating_point);
  const auto drift = system_to_vector(system.drift, system.state_dimension);
  std::vector<PolynomialVectorField> controls;
  for (const auto &item : system.control_fields) {
    controls.push_back(system_to_vector(item, system.state_dimension));
  }
  std::vector<PolynomialVectorField> generators = {drift};
  generators.insert(generators.end(), controls.begin(), controls.end());
  std::vector<PolynomialVectorField> fields;
  std::vector<PolynomialVectorField> frontier;
  std::set<std::string> seen;
  for (const auto &field : controls) {
    if (zero_vector(field)) {
      continue;
    }
    const auto signature = vector_signature(field);
    if (seen.insert(signature).second) {
      fields.push_back(field);
      frontier.push_back(field);
    }
  }
  int depth_reached = 0;
  for (int depth = 1; depth <= max_depth; ++depth) {
    if (frontier.empty() ||
        fields.size() >= static_cast<std::size_t>(max_generated)) {
      break;
    }
    std::vector<PolynomialVectorField> next_frontier;
    bool full = false;
    for (const auto &base : generators) {
      for (const auto &field : frontier) {
        auto bracket = lie_bracket(base, field);
        if (zero_vector(bracket)) {
          continue;
        }
        const auto signature = vector_signature(bracket);
        if (!seen.insert(signature).second) {
          continue;
        }
        fields.push_back(bracket);
        next_frontier.push_back(std::move(bracket));
        if (fields.size() >= static_cast<std::size_t>(max_generated)) {
          full = true;
          break;
        }
      }
      if (full) {
        break;
      }
    }
    frontier = std::move(next_frontier);
    depth_reached = depth;
  }
  RationalMatrix vectors;
  for (const auto &field : fields) {
    vectors.push_back(vector_at(field, point));
  }
  const std::size_t rank = vectors.empty() ? 0U : exact_matrix_rank(vectors);
  const bool full_rank = rank == system.state_dimension;
  const std::string status = full_rank ? "FULL_LOCAL_ACCESSIBILITY_RANK"
                                       : "PARTIAL_LOCAL_ACCESSIBILITY_RANK";
  std::vector<std::string> signatures(seen.begin(), seen.end());
  const Json material =
      {{"depth", depth_reached},
       {"fields", signatures},
       {"point", point_json(point)},
       {"rank", rank},
       {"version", nonlinear_lie_version}};
  return {.status = status,
          .state_dimension = system.state_dimension,
          .rank = rank,
          .generated_field_count = fields.size(),
          .depth_reached = depth_reached,
          .full_rank = full_rank,
          .exact = true,
          .local_only = true,
          .global_accessibility_eligible = false,
          .field_signatures = std::move(signatures),
          .assessment_signature = contracts::sha256_json(material),
          .warnings = {"Full Lie rank is a bounded local accessibility result at the supplied operating point; it is not global controllability."}};
}

LieObservabilityAssessment assess_lie_observability(
    const ExactControlAffinePolynomialSystem &system,
    std::vector<NumericCoefficient> operating_point, int max_depth,
    int max_generated) {
  if (max_depth < 0 || max_depth > max_lie_depth) {
    lie_error("SAA-7.8 Lie depth outside bounded range");
  }
  const auto point = validate_point(system, operating_point);
  const auto drift = system_to_vector(system.drift, system.state_dimension);
  std::vector<PolynomialVectorField> generators = {drift};
  for (const auto &item : system.control_fields) {
    generators.push_back(system_to_vector(item, system.state_dimension));
  }
  auto initial = system_to_scalars(system.outputs, system.state_dimension);
  std::vector<ExactMultivariatePolynomial> functions;
  std::vector<ExactMultivariatePolynomial> frontier;
  std::set<std::string> seen;
  for (const auto &polynomial : initial) {
    if (polynomial.empty()) {
      continue;
    }
    const auto signature = polynomial_signature(polynomial);
    if (seen.insert(signature).second) {
      functions.push_back(polynomial);
      frontier.push_back(polynomial);
    }
  }
  int depth_reached = 0;
  for (int depth = 1; depth <= max_depth; ++depth) {
    if (frontier.empty() ||
        functions.size() >= static_cast<std::size_t>(max_generated)) {
      break;
    }
    std::vector<ExactMultivariatePolynomial> next_frontier;
    bool full = false;
    for (const auto &field : generators) {
      for (const auto &polynomial : frontier) {
        auto derived = lie_derivative(polynomial, field);
        if (derived.empty()) {
          continue;
        }
        const auto signature = polynomial_signature(derived);
        if (!seen.insert(signature).second) {
          continue;
        }
        functions.push_back(derived);
        next_frontier.push_back(std::move(derived));
        if (functions.size() >= static_cast<std::size_t>(max_generated)) {
          full = true;
          break;
        }
      }
      if (full) {
        break;
      }
    }
    frontier = std::move(next_frontier);
    depth_reached = depth;
  }
  RationalMatrix gradients;
  for (const auto &polynomial : functions) {
    gradients.push_back(gradient_at(polynomial, point));
  }
  const std::size_t rank =
      gradients.empty() ? 0U : exact_matrix_rank(gradients);
  const bool full_rank = rank == system.state_dimension;
  const std::string status =
      full_rank ? "FULL_LOCAL_NONLINEAR_OBSERVABILITY_RANK"
                : "PARTIAL_LOCAL_NONLINEAR_OBSERVABILITY_RANK";
  std::vector<std::string> signatures(seen.begin(), seen.end());
  const Json material =
      {{"depth", depth_reached},
       {"functions", signatures},
       {"point", point_json(point)},
       {"rank", rank},
       {"version", nonlinear_lie_version}};
  return {.status = status,
          .state_dimension = system.state_dimension,
          .rank = rank,
          .generated_function_count = functions.size(),
          .depth_reached = depth_reached,
          .full_rank = full_rank,
          .exact = true,
          .local_only = true,
          .global_observability_eligible = false,
          .function_signatures = std::move(signatures),
          .assessment_signature = contracts::sha256_json(material),
          .warnings = {"The Lie-derivative rank result is local to the supplied exact polynomial model and operating point; it does not establish global injectivity."}};
}

NonlinearLieAssessment assess_nonlinear_lie_structure(
    const ExactControlAffinePolynomialSystem &system,
    std::vector<NumericCoefficient> operating_point, int max_depth) {
  const auto point = validate_point(system, operating_point);
  auto accessibility = assess_lie_accessibility(
      system, numeric_inputs(point), max_depth, max_lie_generated_objects);
  auto observability = assess_lie_observability(
      system, numeric_inputs(point), max_depth, max_lie_generated_objects);
  const bool combined = accessibility.full_rank && observability.full_rank;
  const Json material =
      {{"accessibility", accessibility.assessment_signature},
       {"observability", observability.assessment_signature},
       {"point", point_json(point)},
       {"version", nonlinear_lie_version}};
  return {.schema_version = 1,
          .lie_version = std::string(nonlinear_lie_version),
          .operating_point = point,
          .accessibility = std::move(accessibility),
          .observability = std::move(observability),
          .locally_accessible_and_observable = combined,
          .global_claim_eligible = false,
          .assessment_signature = contracts::sha256_json(material)};
}

Json to_json(const ExactControlAffinePolynomialSystem &value) {
  Json controls = Json::array();
  for (const auto &control : value.control_fields) {
    controls.push_back(to_json(control));
  }
  return {{"control_fields", controls},
          {"domain_center", point_json(value.domain_center)},
          {"domain_radius", point_json(value.domain_radius)},
          {"drift", to_json(value.drift)},
          {"outputs", to_json(value.outputs)},
          {"state_dimension", value.state_dimension}};
}

Json to_json(const LieAccessibilityAssessment &value) {
  return {{"assessment_signature", value.assessment_signature},
          {"depth_reached", value.depth_reached},
          {"exact", value.exact},
          {"field_signatures", value.field_signatures},
          {"full_rank", value.full_rank},
          {"generated_field_count", value.generated_field_count},
          {"global_accessibility_eligible",
           value.global_accessibility_eligible},
          {"local_only", value.local_only},
          {"rank", value.rank},
          {"state_dimension", value.state_dimension},
          {"status", value.status},
          {"warnings", value.warnings}};
}

Json to_json(const LieObservabilityAssessment &value) {
  return {{"assessment_signature", value.assessment_signature},
          {"depth_reached", value.depth_reached},
          {"exact", value.exact},
          {"full_rank", value.full_rank},
          {"function_signatures", value.function_signatures},
          {"generated_function_count", value.generated_function_count},
          {"global_observability_eligible",
           value.global_observability_eligible},
          {"local_only", value.local_only},
          {"rank", value.rank},
          {"state_dimension", value.state_dimension},
          {"status", value.status},
          {"warnings", value.warnings}};
}

Json to_json(const NonlinearLieAssessment &value) {
  return {{"accessibility", to_json(value.accessibility)},
          {"assessment_signature", value.assessment_signature},
          {"global_claim_eligible", value.global_claim_eligible},
          {"lie_version", value.lie_version},
          {"locally_accessible_and_observable",
           value.locally_accessible_and_observable},
          {"observability", to_json(value.observability)},
          {"operating_point", point_json(value.operating_point)},
          {"schema_version", value.schema_version}};
}

} // namespace statewright::saa
