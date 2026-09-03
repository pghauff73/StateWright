#include "statewright/saa/nonlinear_geometry.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <map>
#include <numeric>
#include <set>
#include <tuple>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[noreturn]] void geometry_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

[[nodiscard]] mpq_class exact_value(const NumericCoefficient &value,
                                    std::string_view label) {
  if (!value.exact()) {
    geometry_error(std::string(label) + " must be exact and cannot be float");
  }
  return value.value();
}

[[nodiscard]] Json matrix_json(const RationalMatrix &matrix) {
  Json result = Json::array();
  for (const auto &row : matrix) {
    result.push_back(polynomial_json(row));
  }
  return result;
}

[[nodiscard]] Json tensor_json(const RationalTensor3 &tensor) {
  Json result = Json::array();
  for (const auto &matrix : tensor) {
    result.push_back(matrix_json(matrix));
  }
  return result;
}

[[nodiscard]] Json rationals_json(const std::vector<mpq_class> &values) {
  return polynomial_json(values);
}

[[nodiscard]] std::pair<RationalMatrix, std::vector<std::size_t>>
rref(RationalMatrix rows) {
  if (rows.empty()) {
    return {std::move(rows), {}};
  }
  const std::size_t columns = rows.front().size();
  if (std::ranges::any_of(rows, [columns](const auto &row) {
        return row.size() != columns;
      })) {
    geometry_error("geometry matrix is not rectangular");
  }
  std::vector<std::size_t> pivots;
  std::size_t pivot_row = 0U;
  for (std::size_t column = 0U; column < columns; ++column) {
    auto selected = pivot_row;
    while (selected < rows.size() && rows[selected][column] == 0) {
      ++selected;
    }
    if (selected == rows.size()) {
      continue;
    }
    std::swap(rows[pivot_row], rows[selected]);
    const mpq_class pivot = rows[pivot_row][column];
    for (auto &value : rows[pivot_row]) {
      value /= pivot;
    }
    for (std::size_t row = 0U; row < rows.size(); ++row) {
      if (row == pivot_row) {
        continue;
      }
      const mpq_class factor = rows[row][column];
      if (factor != 0) {
        for (std::size_t index = 0; index < columns; ++index) {
          rows[row][index] -= factor * rows[pivot_row][index];
        }
      }
    }
    pivots.push_back(column);
    ++pivot_row;
    if (pivot_row == rows.size()) {
      break;
    }
  }
  return {std::move(rows), std::move(pivots)};
}

[[nodiscard]] mpq_class rational_power(mpq_class value, int exponent) {
  mpq_class result = 1;
  for (int index = 0; index < exponent; ++index) {
    result *= value;
  }
  return result;
}

[[nodiscard]] std::vector<mpq_class>
exact_point_and_delta(const CanonicalTaylorJet &jet,
                      const std::vector<NumericCoefficient> &point,
                      std::vector<mpq_class> *exact_point) {
  if (point.size() != jet.input_count) {
    geometry_error("SAA-7.6 geometry point dimension mismatch");
  }
  exact_point->clear();
  exact_point->reserve(point.size());
  std::vector<mpq_class> delta;
  delta.reserve(point.size());
  for (std::size_t index = 0; index < point.size(); ++index) {
    const auto value =
        exact_value(point[index], "geometry point " + std::to_string(index));
    if (abs(value - jet.center[index]) > jet.validity_radius[index]) {
      geometry_error("SAA-7.6 geometry point coordinate " +
                     std::to_string(index) +
                     " lies outside certified box");
    }
    exact_point->push_back(value);
    delta.push_back(value - jet.center[index]);
  }
  return delta;
}

[[nodiscard]] mpq_class monomial_value(const std::vector<mpq_class> &delta,
                                       const std::vector<int> &powers) {
  mpq_class value = 1;
  for (std::size_t index = 0; index < powers.size(); ++index) {
    if (powers[index] != 0) {
      value *= rational_power(delta[index], powers[index]);
    }
  }
  return value;
}

[[nodiscard]] RationalMatrix jacobian_from_delta(
    const CanonicalTaylorJet &jet, const std::vector<mpq_class> &delta) {
  RationalMatrix matrix(jet.output_count,
                        std::vector<mpq_class>(jet.input_count, 0));
  for (const auto &term : jet.terms) {
    for (std::size_t input = 0; input < term.powers.size(); ++input) {
      const int power = term.powers[input];
      if (power == 0) {
        continue;
      }
      auto derivative_powers = term.powers;
      --derivative_powers[input];
      matrix[term.output_index][input] +=
          term.coefficient * power * monomial_value(delta, derivative_powers);
    }
  }
  return matrix;
}

[[nodiscard]] RationalTensor3 hessian_from_delta(
    const CanonicalTaylorJet &jet, const std::vector<mpq_class> &delta) {
  RationalTensor3 tensor(
      jet.output_count,
      RationalMatrix(jet.input_count,
                     std::vector<mpq_class>(jet.input_count, 0)));
  for (const auto &term : jet.terms) {
    for (std::size_t left = 0; left < jet.input_count; ++left) {
      for (std::size_t right = 0; right < jet.input_count; ++right) {
        const int left_power = term.powers[left];
        const int right_power = term.powers[right];
        int factor = 0;
        auto powers = term.powers;
        if (left == right) {
          if (left_power < 2) {
            continue;
          }
          factor = left_power * (left_power - 1);
          powers[left] -= 2;
        } else {
          if (left_power < 1 || right_power < 1) {
            continue;
          }
          factor = left_power * right_power;
          --powers[left];
          --powers[right];
        }
        tensor[term.output_index][left][right] +=
            term.coefficient * factor * monomial_value(delta, powers);
      }
    }
  }
  return tensor;
}

[[nodiscard]] RationalMatrix
derivative_coefficient_matrix(const CanonicalTaylorJet &jet) {
  using Key = std::pair<std::size_t, std::vector<int>>;
  std::map<Key, std::vector<mpq_class>> accumulator;
  for (const auto &term : jet.terms) {
    for (std::size_t input = 0; input < term.powers.size(); ++input) {
      const int power = term.powers[input];
      if (power == 0) {
        continue;
      }
      auto derivative_powers = term.powers;
      --derivative_powers[input];
      auto [iterator, inserted] = accumulator.try_emplace(
          {term.output_index, derivative_powers},
          std::vector<mpq_class>(jet.input_count, 0));
      static_cast<void>(inserted);
      iterator->second[input] += term.coefficient * power;
    }
  }
  std::vector<std::pair<Key, std::vector<mpq_class>>> rows(accumulator.begin(),
                                                           accumulator.end());
  std::ranges::sort(rows, [](const auto &left, const auto &right) {
    const int left_degree = std::accumulate(left.first.second.begin(),
                                            left.first.second.end(), 0);
    const int right_degree = std::accumulate(right.first.second.begin(),
                                             right.first.second.end(), 0);
    return std::tuple(left.first.first, left_degree, left.first.second) <
           std::tuple(right.first.first, right_degree, right.first.second);
  });
  RationalMatrix result;
  for (auto &row : rows) {
    result.push_back(std::move(row.second));
  }
  return result;
}

[[nodiscard]] std::vector<NumericCoefficient>
inputs_from_exact(const std::vector<mpq_class> &values) {
  std::vector<NumericCoefficient> result;
  result.reserve(values.size());
  for (const auto &value : values) {
    result.emplace_back(value);
  }
  return result;
}

} // namespace

std::size_t exact_matrix_rank(const RationalMatrix &matrix) {
  if (matrix.empty()) {
    return 0U;
  }
  return rref(matrix).second.size();
}

RationalMatrix exact_nullspace(const RationalMatrix &matrix,
                               std::optional<std::size_t> column_count) {
  const std::size_t width =
      matrix.empty() ? column_count.value_or(0U) : matrix.front().size();
  if (matrix.empty()) {
    RationalMatrix basis;
    for (std::size_t free = 0; free < width; ++free) {
      std::vector<mpq_class> vector(width, 0);
      vector[free] = 1;
      basis.push_back(std::move(vector));
    }
    return basis;
  }
  auto [reduced, pivots] = rref(matrix);
  std::set<std::size_t> pivot_set(pivots.begin(), pivots.end());
  RationalMatrix basis;
  for (std::size_t free = 0; free < width; ++free) {
    if (pivot_set.contains(free)) {
      continue;
    }
    std::vector<mpq_class> vector(width, 0);
    vector[free] = 1;
    for (std::size_t row = 0; row < pivots.size(); ++row) {
      vector[pivots[row]] = -reduced[row][free];
    }
    basis.push_back(std::move(vector));
  }
  return basis;
}

RationalMatrix jacobian_at(const CanonicalTaylorJet &jet,
                           const std::vector<NumericCoefficient> &point) {
  std::vector<mpq_class> exact_point;
  const auto delta = exact_point_and_delta(jet, point, &exact_point);
  return jacobian_from_delta(jet, delta);
}

RationalTensor3 hessian_at(const CanonicalTaylorJet &jet,
                           const std::vector<NumericCoefficient> &point) {
  std::vector<mpq_class> exact_point;
  const auto delta = exact_point_and_delta(jet, point, &exact_point);
  return hessian_from_delta(jet, delta);
}

DifferentialGeometryAssessment assess_nonlinear_geometry(
    const CanonicalRepresentativeAlgorithmForm &form,
    const CanonicalTaylorJet &jet,
    std::optional<std::vector<NumericCoefficient>> point) {
  if (jet.parent_representative_behavior_signature !=
      form.representative_behavior_signature) {
    geometry_error("SAA-7.6 jet belongs to a different representative form");
  }
  auto selected = point.value_or(inputs_from_exact(jet.center));
  std::vector<mpq_class> exact_point;
  const auto delta = exact_point_and_delta(jet, selected, &exact_point);
  const auto jacobian = jacobian_from_delta(jet, delta);
  const std::size_t rank = exact_matrix_rank(jacobian);
  const auto tangent_nullspace = exact_nullspace(jacobian, jet.input_count);
  const auto hessian = hessian_from_delta(jet, delta);
  std::size_t cross_curvature = 0U;
  for (std::size_t output = 0; output < jet.output_count; ++output) {
    for (std::size_t left = 0; left < jet.input_count; ++left) {
      for (std::size_t right = left + 1U; right < jet.input_count; ++right) {
        if (hessian[output][left][right] != 0) {
          ++cross_curvature;
        }
      }
    }
  }
  const auto derivative_matrix = derivative_coefficient_matrix(jet);
  const auto invariant_basis =
      exact_nullspace(derivative_matrix, jet.input_count);
  const std::size_t invariant_dimension = invariant_basis.size();
  const std::size_t behavioral_dimension =
      jet.input_count - invariant_dimension;
  const bool local_diffeomorphism =
      jet.input_count == jet.output_count && rank == jet.input_count;
  std::string status;
  bool eligible = false;
  if (invariant_dimension != 0U) {
    status = "INVARIANT_REDUNDANT_DIRECTION_DETECTED";
  } else if (rank < std::min(jet.input_count, jet.output_count)) {
    status = "LOCAL_SINGULAR_REPRESENTATION";
  } else if (jet.output_count < jet.input_count) {
    status = "OUTPUT_DIMENSION_LIMITED_REPRESENTATION";
  } else {
    status = "FULL_RANK_LOCAL_GEOMETRY";
    eligible = true;
  }
  const Json payload =
      {{"behavioral_input_dimension", behavioral_dimension},
       {"cross_curvature_count", cross_curvature},
       {"geometry_version", nonlinear_geometry_version},
       {"invariant_distribution_basis", matrix_json(invariant_basis)},
       {"jacobian", matrix_json(jacobian)},
       {"jacobian_rank", rank},
       {"jet_local_behavior_signature", jet.local_behavior_signature},
       {"local_diffeomorphism", local_diffeomorphism},
       {"parent_representative_behavior_signature",
        form.representative_behavior_signature},
       {"point", rationals_json(exact_point)},
       {"schema_version", 1},
       {"status", status},
       {"tangent_nullspace", matrix_json(tangent_nullspace)}};
  std::vector<std::string> warnings = {
      "SAA-7.6 differential geometry is exact for the finite jet polynomial within its certified local scope, not for unknown higher-order/global behavior."};
  if (invariant_dimension != 0U) {
    warnings.emplace_back(
        "The invariant distribution consists of constant exact directions annihilating every derivative polynomial in the retained jet; constant vector fields commute, so this detected distribution is integrable.");
  }
  if (rank < jet.input_count && invariant_dimension == 0U) {
    warnings.emplace_back(
        "A rank loss at one point may be a local singularity rather than global redundancy; OIEC does not collapse dimensions from this point test alone.");
  }
  return {.schema_version = 1,
          .geometry_version = std::string(nonlinear_geometry_version),
          .parent_representative_behavior_signature =
              form.representative_behavior_signature,
          .jet_local_behavior_signature = jet.local_behavior_signature,
          .point = std::move(exact_point),
          .jacobian = jacobian,
          .jacobian_rank = rank,
          .tangent_nullspace = tangent_nullspace,
          .local_manifold_dimension = rank,
          .local_diffeomorphism = local_diffeomorphism,
          .hessian = hessian,
          .cross_curvature_count = cross_curvature,
          .invariant_distribution_basis = invariant_basis,
          .invariant_distribution_dimension = invariant_dimension,
          .invariant_distribution_integrable = true,
          .behavioral_input_dimension = behavioral_dimension,
          .status = std::move(status),
          .canonical_geometry_eligible = eligible,
          .assessment_signature = contracts::sha256_json(payload),
          .warnings = std::move(warnings)};
}

Json to_json(const DifferentialGeometryAssessment &value) {
  return {{"assessment_signature", value.assessment_signature},
          {"behavioral_input_dimension", value.behavioral_input_dimension},
          {"canonical_geometry_eligible", value.canonical_geometry_eligible},
          {"cross_curvature_count", value.cross_curvature_count},
          {"geometry_version", value.geometry_version},
          {"hessian", tensor_json(value.hessian)},
          {"invariant_distribution_basis",
           matrix_json(value.invariant_distribution_basis)},
          {"invariant_distribution_dimension",
           value.invariant_distribution_dimension},
          {"invariant_distribution_integrable",
           value.invariant_distribution_integrable},
          {"jacobian", matrix_json(value.jacobian)},
          {"jacobian_rank", value.jacobian_rank},
          {"jet_local_behavior_signature", value.jet_local_behavior_signature},
          {"local_diffeomorphism", value.local_diffeomorphism},
          {"local_manifold_dimension", value.local_manifold_dimension},
          {"parent_representative_behavior_signature",
           value.parent_representative_behavior_signature},
          {"point", rationals_json(value.point)},
          {"schema_version", value.schema_version},
          {"status", value.status},
          {"tangent_nullspace", matrix_json(value.tangent_nullspace)},
          {"warnings", value.warnings}};
}

} // namespace statewright::saa
