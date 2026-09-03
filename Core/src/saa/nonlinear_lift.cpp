#include "statewright/saa/nonlinear_lift.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <map>
#include <tuple>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;
using Powers = std::vector<int>;
using OrderedPolynomial = std::vector<std::pair<Powers, mpq_class>>;

[[noreturn]] void lift_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

[[nodiscard]] mpq_class exact_value(const NumericCoefficient &value) {
  if (!value.exact()) {
    lift_error("SAA-7.11 dynamic coefficient must be exact and cannot be float");
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

void add_coefficient(OrderedPolynomial &polynomial, Powers powers,
                     const mpq_class &coefficient) {
  const auto found = std::ranges::find_if(
      polynomial, [&](const auto &item) { return item.first == powers; });
  if (found == polynomial.end()) {
    if (coefficient != 0) {
      polynomial.emplace_back(std::move(powers), coefficient);
    }
    return;
  }
  found->second += coefficient;
  if (found->second == 0) {
    polynomial.erase(found);
  }
}

void enumerate_basis(std::size_t dimension, int max_degree,
                     std::size_t axis, Powers &current,
                     std::vector<Powers> &result) {
  if (axis == dimension) {
    if (degree(current) <= max_degree) {
      result.push_back(current);
    }
    return;
  }
  for (int exponent = 0; exponent <= max_degree; ++exponent) {
    current[axis] = exponent;
    enumerate_basis(dimension, max_degree, axis + 1U, current, result);
  }
}

[[nodiscard]] std::vector<Powers> monomial_basis(std::size_t dimension,
                                                 int max_degree) {
  std::vector<Powers> result;
  Powers current(dimension, 0);
  enumerate_basis(dimension, max_degree, 0U, current, result);
  std::ranges::sort(result, [](const auto &left, const auto &right) {
    return std::tuple(degree(left), left) < std::tuple(degree(right), right);
  });
  if (result.size() > max_lift_observables) {
    lift_error("SAA-7.11 monomial lift exceeds bounded observable cap");
  }
  return result;
}

[[nodiscard]] std::vector<OrderedPolynomial>
vector_field(const ExactPolynomialSystem &system) {
  if (system.input_count != system.output_count) {
    lift_error("SAA-7.11 autonomous polynomial dynamics must be square");
  }
  std::vector<OrderedPolynomial> components(system.input_count);
  for (const auto &term : system.terms) {
    if (term.output_index >= system.input_count) {
      lift_error("SAA-7.11 dynamic component outside state dimension");
    }
    if (term.powers.size() != system.input_count ||
        std::ranges::any_of(term.powers,
                            [](int value) { return value < 0; })) {
      lift_error("SAA-7.11 invalid dynamic monomial");
    }
    add_coefficient(components[term.output_index], term.powers,
                    exact_value(term.coefficient));
  }
  return components;
}

[[nodiscard]] Powers add_powers(const Powers &left, const Powers &right) {
  Powers result(left.size());
  for (std::size_t index = 0; index < left.size(); ++index) {
    result[index] = left[index] + right[index];
  }
  return result;
}

[[nodiscard]] OrderedPolynomial monomial_derivative_along_field(
    const Powers &powers, const std::vector<OrderedPolynomial> &field) {
  OrderedPolynomial result;
  for (std::size_t variable = 0; variable < powers.size(); ++variable) {
    const int exponent = powers[variable];
    if (exponent == 0) {
      continue;
    }
    Powers reduced = powers;
    --reduced[variable];
    for (const auto &[field_powers, field_coefficient] : field[variable]) {
      add_coefficient(result, add_powers(reduced, field_powers),
                      exponent * field_coefficient);
    }
  }
  if (result.size() > max_lift_remainder_terms) {
    lift_error("SAA-7.11 lifted derivative exceeds bounded term cap");
  }
  return result;
}

[[nodiscard]] Json matrix_json(const RationalMatrix &matrix) {
  Json result = Json::array();
  for (const auto &row : matrix) {
    result.push_back(polynomial_json(row));
  }
  return result;
}

[[nodiscard]] Json remainders_json(
    const std::vector<LiftRemainderTerm> &terms) {
  Json result = Json::array();
  for (const auto &term : terms) {
    result.push_back(to_json(term));
  }
  return result;
}

} // namespace

Json to_json(const LiftRemainderTerm &value) {
  return {{"coefficient", rational_json(value.coefficient)},
          {"observable_index", value.observable_index},
          {"powers", value.powers}};
}

Json to_json(const CarlemanKoopmanLift &value) {
  return {{"basis", value.basis},
          {"canonical_equivalence_eligible",
           value.canonical_equivalence_eligible},
          {"discovery_aid_only", value.discovery_aid_only},
          {"exact_finite_closure", value.exact_finite_closure},
          {"generator_matrix", matrix_json(value.generator_matrix)},
          {"lift_degree", value.lift_degree},
          {"lift_signature", value.lift_signature},
          {"lift_version", value.lift_version},
          {"remainder_terms", remainders_json(value.remainder_terms)},
          {"schema_version", value.schema_version},
          {"state_dimension", value.state_dimension},
          {"state_reconstruction_indices",
           value.state_reconstruction_indices},
          {"status", value.status},
          {"warnings", value.warnings}};
}

CarlemanKoopmanLift
build_carleman_koopman_lift(const ExactPolynomialSystem &dynamics,
                            std::size_t lift_degree) {
  if (dynamics.input_count != dynamics.output_count) {
    lift_error("SAA-7.11 requires autonomous square polynomial dynamics");
  }
  const std::size_t dimension = dynamics.input_count;
  if (dimension < 1U || dimension > max_lift_state_dimension) {
    lift_error("SAA-7.11 state dimension outside bounded range");
  }
  if (lift_degree < 1U || lift_degree > max_lift_degree) {
    lift_error("SAA-7.11 lift degree outside bounded range");
  }
  const auto field = vector_field(dynamics);
  auto basis = monomial_basis(dimension, static_cast<int>(lift_degree));
  std::map<Powers, std::size_t> index;
  for (std::size_t position = 0; position < basis.size(); ++position) {
    index.emplace(basis[position], position);
  }
  RationalMatrix matrix(basis.size(),
                        std::vector<mpq_class>(basis.size(), 0));
  std::vector<LiftRemainderTerm> remainder;
  for (std::size_t row = 0; row < basis.size(); ++row) {
    const auto derivative =
        monomial_derivative_along_field(basis[row], field);
    for (const auto &[target_powers, coefficient] : derivative) {
      const auto column = index.find(target_powers);
      if (column == index.end()) {
        remainder.push_back({.observable_index = row,
                             .powers = target_powers,
                             .coefficient = coefficient});
      } else {
        matrix[row][column->second] += coefficient;
      }
    }
  }
  if (remainder.size() > max_lift_remainder_terms) {
    lift_error("SAA-7.11 lift remainder exceeds bounded cap");
  }
  std::vector<std::size_t> reconstruction;
  reconstruction.reserve(dimension);
  for (std::size_t state = 0; state < dimension; ++state) {
    Powers powers(dimension, 0);
    powers[state] = 1;
    reconstruction.push_back(index.at(powers));
  }
  const bool exact_closed = remainder.empty();
  const std::string status =
      exact_closed ? "EXACT_FINITE_CARLEMAN_KOOPMAN_CLOSURE"
                   : "TRUNCATED_CARLEMAN_KOOPMAN_DISCOVERY_AID";
  const Json material =
      {{"basis", basis},
       {"generator_matrix", matrix_json(matrix)},
       {"lift_degree", lift_degree},
       {"remainder_terms", remainders_json(remainder)},
       {"state_dimension", dimension},
       {"state_reconstruction_indices", reconstruction},
       {"status", status},
       {"version", nonlinear_lift_version}};
  return {
      .schema_version = 1,
      .lift_version = std::string(nonlinear_lift_version),
      .state_dimension = dimension,
      .lift_degree = lift_degree,
      .basis = std::move(basis),
      .generator_matrix = std::move(matrix),
      .state_reconstruction_indices = std::move(reconstruction),
      .remainder_terms = std::move(remainder),
      .exact_finite_closure = exact_closed,
      .status = status,
      .discovery_aid_only = !exact_closed,
      .canonical_equivalence_eligible = exact_closed,
      .lift_signature = contracts::sha256_json(material),
      .warnings = {exact_closed
                       ? "The finite monomial basis is exactly invariant under the polynomial generator and contains the original state coordinates; within this exact model the lift is a finite linear representation."
                       : "The finite lift omits generated monomials outside the retained basis. It is a representation-discovery aid only and cannot establish nonlinear equivalence without a remainder/closure proof."}};
}

} // namespace statewright::saa
