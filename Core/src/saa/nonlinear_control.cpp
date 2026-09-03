#include "statewright/saa/nonlinear_control.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[noreturn]] void control_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

[[nodiscard]] mpq_class exact_value(const NumericCoefficient &value,
                                    std::string_view label) {
  if (!value.exact()) {
    control_error(std::string(label) + " must be exact and cannot be float");
  }
  return value.value();
}

[[nodiscard]] RationalMatrix exact_matrix(const NumericMatrix &matrix,
                                          std::size_t rows,
                                          std::size_t columns,
                                          std::string_view label) {
  if (matrix.size() != rows) {
    control_error(std::string(label) + " row count mismatch");
  }
  RationalMatrix result;
  result.reserve(rows);
  for (std::size_t row_index = 0; row_index < rows; ++row_index) {
    if (matrix[row_index].size() != columns) {
      control_error(std::string(label) + " column count mismatch");
    }
    std::vector<mpq_class> row;
    row.reserve(columns);
    for (std::size_t column = 0; column < columns; ++column) {
      row.push_back(exact_value(
          matrix[row_index][column],
          std::string(label) + "[" + std::to_string(row_index) + "," +
              std::to_string(column) + "]"));
    }
    result.push_back(std::move(row));
  }
  return result;
}

[[nodiscard]] RationalMatrix identity(std::size_t size) {
  RationalMatrix result(size, std::vector<mpq_class>(size, 0));
  for (std::size_t index = 0; index < size; ++index) {
    result[index][index] = 1;
  }
  return result;
}

[[nodiscard]] RationalMatrix multiply(const RationalMatrix &left,
                                      const RationalMatrix &right) {
  if (left.empty()) {
    return {};
  }
  if (right.empty()) {
    return RationalMatrix(left.size());
  }
  const std::size_t shared = left.front().size();
  if (std::ranges::any_of(left, [shared](const auto &row) {
        return row.size() != shared;
      }) ||
      right.size() != shared) {
    control_error("matrix multiplication dimension mismatch");
  }
  const std::size_t columns = right.front().size();
  if (std::ranges::any_of(right, [columns](const auto &row) {
        return row.size() != columns;
      })) {
    control_error("matrix multiplication right operand is not rectangular");
  }
  RationalMatrix result(left.size(), std::vector<mpq_class>(columns, 0));
  for (std::size_t row = 0; row < left.size(); ++row) {
    for (std::size_t column = 0; column < columns; ++column) {
      for (std::size_t index = 0; index < shared; ++index) {
        result[row][column] += left[row][index] * right[index][column];
      }
    }
  }
  return result;
}

[[nodiscard]] RationalMatrix horizontal_stack(
    const std::vector<RationalMatrix> &blocks) {
  if (blocks.empty()) {
    return {};
  }
  const std::size_t rows = blocks.front().size();
  if (std::ranges::any_of(blocks, [rows](const auto &block) {
        return block.size() != rows;
      })) {
    control_error("horizontal matrix stack row mismatch");
  }
  RationalMatrix result(rows);
  for (std::size_t row = 0; row < rows; ++row) {
    for (const auto &block : blocks) {
      result[row].insert(result[row].end(), block[row].begin(),
                         block[row].end());
    }
  }
  return result;
}

[[nodiscard]] RationalMatrix vertical_stack(
    const std::vector<RationalMatrix> &blocks) {
  RationalMatrix result;
  std::optional<std::size_t> width;
  for (const auto &block : blocks) {
    for (const auto &row : block) {
      if (!width) {
        width = row.size();
      } else if (row.size() != *width) {
        control_error("vertical matrix stack column mismatch");
      }
      result.push_back(row);
    }
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

[[nodiscard]] std::string normalized_meaning(std::string value) {
  std::istringstream input(value);
  std::ostringstream output;
  std::string word;
  bool first = true;
  while (input >> word) {
    std::ranges::transform(word, word.begin(), [](unsigned char character) {
      return static_cast<char>(std::tolower(character));
    });
    if (!first) {
      output << ' ';
    }
    output << word;
    first = false;
  }
  return output.str();
}

[[nodiscard]] Json optional_bool(const std::optional<bool> &value) {
  return value ? Json(*value) : Json(nullptr);
}

} // namespace

Json to_json(const ExactLocalDynamicLinearization &value) {
  return {{"a", matrix_json(value.a)},
          {"b", matrix_json(value.b)},
          {"c", matrix_json(value.c)},
          {"control_count", value.control_count},
          {"control_version", value.control_version},
          {"linearization_signature", value.linearization_signature},
          {"output_count", value.output_count},
          {"parent_representative_behavior_signature",
           value.parent_representative_behavior_signature},
          {"schema_version", value.schema_version},
          {"state_count", value.state_count},
          {"state_meanings", value.state_meanings}};
}

Json to_json(const RepresentativeControlAssessment &value) {
  return {{"assessment_signature", value.assessment_signature},
          {"canonical_control_eligible", value.canonical_control_eligible},
          {"controllability_rank", value.controllability_rank},
          {"control_version", value.control_version},
          {"dynamic_linearization_signature",
           value.dynamic_linearization_signature},
          {"dynamic_model_supplied", value.dynamic_model_supplied},
          {"dynamically_controllable",
           optional_bool(value.dynamically_controllable)},
          {"dynamically_observable", optional_bool(value.dynamically_observable)},
          {"input_observability_rank", value.input_observability_rank},
          {"invariant_unobservable_direction_count",
           value.invariant_unobservable_direction_count},
          {"jet_local_behavior_signature", value.jet_local_behavior_signature},
          {"observability_rank", value.observability_rank},
          {"parent_representative_behavior_signature",
           value.parent_representative_behavior_signature},
          {"representative_input_count", value.representative_input_count},
          {"representative_inputs_locally_observable",
           value.representative_inputs_locally_observable},
          {"schema_version", value.schema_version},
          {"state_count", value.state_count},
          {"status", value.status},
          {"warnings", value.warnings}};
}

ExactLocalDynamicLinearization make_local_dynamic_linearization(
    const CanonicalRepresentativeAlgorithmForm &form, NumericMatrix a,
    NumericMatrix b, NumericMatrix c, std::vector<std::string> state_meanings) {
  const std::size_t state_count = a.size();
  if (state_count < 1U || state_count > max_local_state_dimension) {
    control_error("SAA-7.7 local state dimension outside bounded range");
  }
  if (state_meanings.size() != state_count ||
      std::ranges::any_of(state_meanings, [](const auto &meaning) {
        return normalized_meaning(meaning).empty();
      })) {
    control_error(
        "SAA-7.7 requires one non-empty meaning for every dynamic state");
  }
  auto exact_a = exact_matrix(a, state_count, state_count, "A");
  auto exact_b = exact_matrix(b, state_count,
                              form.representative_input_count, "B");
  auto exact_c = exact_matrix(c, form.output_count, state_count, "C");
  for (auto &meaning : state_meanings) {
    meaning = normalized_meaning(std::move(meaning));
  }
  const Json payload =
      {{"a", matrix_json(exact_a)},
       {"b", matrix_json(exact_b)},
       {"c", matrix_json(exact_c)},
       {"control_count", form.representative_input_count},
       {"control_version", nonlinear_control_version},
       {"output_count", form.output_count},
       {"parent_representative_behavior_signature",
        form.representative_behavior_signature},
       {"schema_version", 1},
       {"state_count", state_count},
       {"state_meanings", state_meanings}};
  return {.schema_version = 1,
          .control_version = std::string(nonlinear_control_version),
          .parent_representative_behavior_signature =
              form.representative_behavior_signature,
          .state_count = state_count,
          .control_count = form.representative_input_count,
          .output_count = form.output_count,
          .state_meanings = std::move(state_meanings),
          .a = std::move(exact_a),
          .b = std::move(exact_b),
          .c = std::move(exact_c),
          .linearization_signature = contracts::sha256_json(payload)};
}

RationalMatrix controllability_matrix(
    const ExactLocalDynamicLinearization &linearization) {
  std::vector<RationalMatrix> blocks;
  auto a_power = identity(linearization.state_count);
  for (std::size_t index = 0; index < linearization.state_count; ++index) {
    blocks.push_back(multiply(a_power, linearization.b));
    a_power = multiply(a_power, linearization.a);
  }
  return horizontal_stack(blocks);
}

RationalMatrix observability_matrix(
    const ExactLocalDynamicLinearization &linearization) {
  std::vector<RationalMatrix> blocks;
  auto a_power = identity(linearization.state_count);
  for (std::size_t index = 0; index < linearization.state_count; ++index) {
    blocks.push_back(multiply(linearization.c, a_power));
    a_power = multiply(a_power, linearization.a);
  }
  return vertical_stack(blocks);
}

RepresentativeControlAssessment
assess_representative_observability_controllability(
    const CanonicalRepresentativeAlgorithmForm &form,
    const CanonicalTaylorJet &jet,
    const DifferentialGeometryAssessment *provided_geometry,
    const ExactLocalDynamicLinearization *dynamic_linearization) {
  if (jet.parent_representative_behavior_signature !=
      form.representative_behavior_signature) {
    control_error("SAA-7.7 jet belongs to a different representative form");
  }
  const auto computed_geometry = provided_geometry == nullptr
                                     ? std::optional<DifferentialGeometryAssessment>(
                                           assess_nonlinear_geometry(form, jet))
                                     : std::nullopt;
  const auto &geometry =
      provided_geometry == nullptr ? *computed_geometry : *provided_geometry;
  if (geometry.jet_local_behavior_signature != jet.local_behavior_signature) {
    control_error(
        "SAA-7.7 geometry assessment belongs to a different jet");
  }
  const std::size_t input_rank = geometry.jacobian_rank;
  const bool input_observable =
      form.representative_input_count <= form.output_count &&
      input_rank == form.representative_input_count &&
      geometry.invariant_distribution_dimension == 0U;

  std::size_t state_count = 0U;
  std::size_t controllability_rank = 0U;
  std::size_t observability_rank = 0U;
  std::optional<bool> controllable;
  std::optional<bool> observable;
  std::string linearization_signature;
  if (dynamic_linearization != nullptr) {
    if (dynamic_linearization->parent_representative_behavior_signature !=
        form.representative_behavior_signature) {
      control_error(
          "SAA-7.7 dynamic linearization belongs to a different representative form");
    }
    state_count = dynamic_linearization->state_count;
    controllability_rank =
        exact_matrix_rank(controllability_matrix(*dynamic_linearization));
    observability_rank =
        exact_matrix_rank(observability_matrix(*dynamic_linearization));
    controllable = controllability_rank == state_count;
    observable = observability_rank == state_count;
    linearization_signature = dynamic_linearization->linearization_signature;
  }

  std::string status;
  bool eligible = false;
  if (!input_observable) {
    status = "REPRESENTATIVE_INPUT_NOT_LOCALLY_OBSERVABLE";
  } else if (dynamic_linearization == nullptr) {
    status = "OBSERVABLE_CONTROLLABILITY_REQUIRES_DYNAMIC_MODEL";
  } else if (!*observable && !*controllable) {
    status = "DYNAMIC_UNOBSERVABLE_AND_UNCONTROLLABLE";
  } else if (!*observable) {
    status = "DYNAMIC_UNOBSERVABLE";
  } else if (!*controllable) {
    status = "DYNAMIC_UNCONTROLLABLE";
  } else {
    status = "LOCALLY_OBSERVABLE_AND_CONTROLLABLE";
    eligible = true;
  }
  const Json payload =
      {{"controllability_rank", controllability_rank},
       {"control_version", nonlinear_control_version},
       {"dynamic_linearization_signature", linearization_signature},
       {"geometry_signature", geometry.assessment_signature},
       {"input_observability_rank", input_rank},
       {"invariant_unobservable_direction_count",
        geometry.invariant_distribution_dimension},
       {"jet_local_behavior_signature", jet.local_behavior_signature},
       {"observability_rank", observability_rank},
       {"parent_representative_behavior_signature",
        form.representative_behavior_signature},
       {"representative_input_count", form.representative_input_count},
       {"schema_version", 1},
       {"state_count", state_count},
       {"status", status}};
  std::vector<std::string> warnings = {
      "SAA-7.7 observability of representative inputs is a local differential property of the qualified finite jet."};
  warnings.push_back(
      dynamic_linearization == nullptr
          ? "Controllability is not inferred from a static input-output equation; an exact local dynamic model is required."
          : "Dynamic controllability/observability uses exact Kalman ranks of the supplied local linearization. This is a bounded local gate, not a proof of global nonlinear accessibility or observability.");
  return {.schema_version = 1,
          .control_version = std::string(nonlinear_control_version),
          .parent_representative_behavior_signature =
              form.representative_behavior_signature,
          .jet_local_behavior_signature = jet.local_behavior_signature,
          .input_observability_rank = input_rank,
          .representative_input_count = form.representative_input_count,
          .representative_inputs_locally_observable = input_observable,
          .invariant_unobservable_direction_count =
              geometry.invariant_distribution_dimension,
          .dynamic_model_supplied = dynamic_linearization != nullptr,
          .dynamic_linearization_signature =
              std::move(linearization_signature),
          .state_count = state_count,
          .controllability_rank = controllability_rank,
          .observability_rank = observability_rank,
          .dynamically_controllable = controllable,
          .dynamically_observable = observable,
          .status = std::move(status),
          .canonical_control_eligible = eligible,
          .assessment_signature = contracts::sha256_json(payload),
          .warnings = std::move(warnings)};
}

} // namespace statewright::saa
