#include "statewright/saa/representative.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <functional>
#include <numeric>
#include <set>
#include <string>
#include <tuple>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;
using RationalChannelMatrix = std::vector<std::vector<RationalChannel>>;

const std::vector<mpq_class> continuous_algebraic_probes = {0, 1, -1, 2, -2};
const std::vector<mpq_class> discrete_algebraic_probes = {1, -1, 0, 2, -2};

[[noreturn]] void representative_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument,
                      std::move(message));
}

[[nodiscard]] Json rational_matrix_json(const RationalMatrix &matrix) {
  Json result = Json::array();
  for (const auto &row : matrix) {
    Json encoded = Json::array();
    for (const auto &value : row) {
      encoded.push_back(rational_json(value));
    }
    result.push_back(std::move(encoded));
  }
  return result;
}

[[nodiscard]] Json rational_channel_matrix_json(
    const RationalChannelMatrix &matrix) {
  Json result = Json::array();
  for (const auto &row : matrix) {
    Json encoded = Json::array();
    for (const auto &value : row) {
      encoded.push_back(to_json(value));
    }
    result.push_back(std::move(encoded));
  }
  return result;
}

[[nodiscard]] RationalChannelMatrix
source_rational_matrix(const CanonicalMIMOCoupling &mimo) {
  RationalChannelMatrix result;
  for (const auto &row : mimo.channels) {
    std::vector<RationalChannel> encoded;
    for (const auto &channel : row) {
      encoded.push_back({channel.numerator, channel.denominator});
    }
    result.push_back(std::move(encoded));
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

[[nodiscard]] RationalMatrix matrix_multiply(const RationalMatrix &left,
                                             const RationalMatrix &right) {
  if (left.empty()) {
    return {};
  }
  if (right.empty()) {
    return RationalMatrix(left.size());
  }
  const std::size_t inner = left.front().size();
  if (std::any_of(left.begin(), left.end(), [inner](const auto &row) {
        return row.size() != inner;
      }) ||
      right.size() != inner) {
    representative_error("SAA-5 matrix dimension mismatch");
  }
  const std::size_t columns = right.front().size();
  if (std::any_of(right.begin(), right.end(), [columns](const auto &row) {
        return row.size() != columns;
      })) {
    representative_error("SAA-5 matrix rows must have equal width");
  }
  RationalMatrix result(left.size(), std::vector<mpq_class>(columns, 0));
  for (std::size_t row = 0; row < left.size(); ++row) {
    for (std::size_t column = 0; column < columns; ++column) {
      for (std::size_t index = 0; index < inner; ++index) {
        result[row][column] += left[row][index] * right[index][column];
      }
    }
  }
  return result;
}

[[nodiscard]] std::size_t matrix_rank(const RationalMatrix &matrix) {
  if (matrix.empty()) {
    return 0;
  }
  RationalMatrix work = matrix;
  const std::size_t width = work.front().size();
  if (std::any_of(work.begin(), work.end(), [width](const auto &row) {
        return row.size() != width;
      })) {
    representative_error("SAA-5 rank matrix rows must have equal width");
  }
  std::size_t pivot_row = 0;
  for (std::size_t column = 0; column < width; ++column) {
    std::size_t pivot = pivot_row;
    while (pivot < work.size() && work[pivot][column] == 0) {
      ++pivot;
    }
    if (pivot == work.size()) {
      continue;
    }
    std::swap(work[pivot_row], work[pivot]);
    const mpq_class scale = work[pivot_row][column];
    for (auto &value : work[pivot_row]) {
      value /= scale;
    }
    for (std::size_t row = 0; row < work.size(); ++row) {
      if (row == pivot_row) {
        continue;
      }
      const mpq_class factor = work[row][column];
      if (factor == 0) {
        continue;
      }
      for (std::size_t index = 0; index < width; ++index) {
        work[row][index] -= factor * work[pivot_row][index];
      }
    }
    ++pivot_row;
    if (pivot_row == work.size()) {
      break;
    }
  }
  return pivot_row;
}

[[nodiscard]] std::size_t coefficient_bits(const RationalMatrix &matrix) {
  std::size_t maximum = 1;
  for (const auto &row : matrix) {
    for (const auto &value : row) {
      mpz_class numerator = value.get_num();
      if (numerator < 0) {
        numerator = -numerator;
      }
      mpz_class denominator = value.get_den();
      if (denominator < 0) {
        denominator = -denominator;
      }
      const std::size_t numerator_bits =
          numerator == 0 ? 0U : mpz_sizeinbase(numerator.get_mpz_t(), 2);
      const std::size_t denominator_bits =
          denominator == 0 ? 0U : mpz_sizeinbase(denominator.get_mpz_t(), 2);
      maximum = std::max({maximum, numerator_bits, denominator_bits});
    }
  }
  return maximum;
}

[[nodiscard]] RationalPolynomial polynomial_multiply(
    const RationalPolynomial &first, const RationalPolynomial &second) {
  RationalPolynomial result(first.size() + second.size() - 1U, 0);
  for (std::size_t left = 0; left < first.size(); ++left) {
    for (std::size_t right = 0; right < second.size(); ++right) {
      result[left + right] += first[left] * second[right];
    }
  }
  auto leading = result.begin();
  while (leading + 1 != result.end() && *leading == 0) {
    ++leading;
  }
  return {leading, result.end()};
}

[[nodiscard]] RationalPolynomial polynomial_product(
    const std::vector<RationalPolynomial> &polynomials) {
  RationalPolynomial result{1};
  for (const auto &polynomial : polynomials) {
    result = polynomial_multiply(result, polynomial);
  }
  return result;
}

[[nodiscard]] std::vector<RationalPolynomial> constant_column_vectors(
    const RationalChannelMatrix &channels, std::size_t term_budget) {
  const std::size_t outputs = channels.size();
  const std::size_t inputs = outputs == 0U ? 0U : channels.front().size();
  std::vector<RationalPolynomial> vectors(inputs);
  std::size_t terms = 0;
  for (std::size_t output = 0; output < outputs; ++output) {
    std::vector<RationalPolynomial> denominators;
    for (const auto &channel : channels[output]) {
      denominators.push_back(channel.denominator);
    }
    std::vector<RationalPolynomial> row_polynomials;
    row_polynomials.reserve(inputs);
    for (std::size_t column = 0; column < inputs; ++column) {
      std::vector<RationalPolynomial> factors;
      for (std::size_t index = 0; index < inputs; ++index) {
        if (index != column) {
          factors.push_back(denominators[index]);
        }
      }
      row_polynomials.push_back(polynomial_multiply(
          channels[output][column].numerator, polynomial_product(factors)));
    }
    std::size_t width = 1;
    for (const auto &polynomial : row_polynomials) {
      width = std::max(width, polynomial.size());
    }
    terms += width * std::max(inputs, std::size_t(1));
    if (terms > term_budget) {
      representative_error("SAA-5 exact rank vector exceeds bounded term budget " +
                           std::to_string(term_budget));
    }
    for (std::size_t column = 0; column < inputs; ++column) {
      vectors[column].insert(vectors[column].end(),
                             width - row_polynomials[column].size(), 0);
      vectors[column].insert(vectors[column].end(),
                             row_polynomials[column].begin(),
                             row_polynomials[column].end());
    }
  }
  return vectors;
}

struct RrefResult final {
  std::vector<std::size_t> pivots;
  RationalMatrix projection;
};

[[nodiscard]] RrefResult
column_rref(const std::vector<RationalPolynomial> &vectors) {
  const std::size_t columns = vectors.size();
  if (columns == 0U) {
    return {};
  }
  const std::size_t height = vectors.front().size();
  if (std::any_of(vectors.begin(), vectors.end(), [height](const auto &value) {
        return value.size() != height;
      })) {
    representative_error("SAA-5 column vectors have inconsistent lengths");
  }
  RationalMatrix matrix(height, std::vector<mpq_class>(columns, 0));
  for (std::size_t row = 0; row < height; ++row) {
    for (std::size_t column = 0; column < columns; ++column) {
      matrix[row][column] = vectors[column][row];
    }
  }
  std::vector<std::size_t> pivot_rows;
  std::vector<std::size_t> pivot_columns;
  std::size_t row_index = 0;
  for (std::size_t column = 0; column < columns; ++column) {
    std::size_t pivot = row_index;
    while (pivot < height && matrix[pivot][column] == 0) {
      ++pivot;
    }
    if (pivot == height) {
      continue;
    }
    std::swap(matrix[row_index], matrix[pivot]);
    const mpq_class scale = matrix[row_index][column];
    for (auto &value : matrix[row_index]) {
      value /= scale;
    }
    for (std::size_t row = 0; row < height; ++row) {
      if (row == row_index) {
        continue;
      }
      const mpq_class factor = matrix[row][column];
      if (factor == 0) {
        continue;
      }
      for (std::size_t index = 0; index < columns; ++index) {
        matrix[row][index] -= factor * matrix[row_index][index];
      }
    }
    pivot_columns.push_back(column);
    pivot_rows.push_back(row_index);
    ++row_index;
    if (row_index == height) {
      break;
    }
  }
  RationalMatrix projection(pivot_columns.size(),
                            std::vector<mpq_class>(columns, 0));
  for (std::size_t basis = 0; basis < pivot_columns.size(); ++basis) {
    projection[basis] = matrix[pivot_rows[basis]];
    projection[basis][pivot_columns[basis]] = 1;
  }
  return {.pivots = std::move(pivot_columns),
          .projection = std::move(projection)};
}

[[nodiscard]] RationalChannelMatrix apply_constant_transform(
    const RationalChannelMatrix &channels, const RationalMatrix &transform) {
  const std::size_t outputs = channels.size();
  const std::size_t source_inputs =
      outputs == 0U ? 0U : channels.front().size();
  if (source_inputs != transform.size()) {
    representative_error("SAA-5 transform source dimension mismatch");
  }
  const std::size_t target_inputs =
      transform.empty() ? 0U : transform.front().size();
  if (std::any_of(transform.begin(), transform.end(),
                  [target_inputs](const auto &row) {
                    return row.size() != target_inputs;
                  })) {
    representative_error("SAA-5 transform rows must have equal width");
  }
  RationalChannelMatrix result;
  for (std::size_t output = 0; output < outputs; ++output) {
    std::vector<RationalChannel> row;
    for (std::size_t target = 0; target < target_inputs; ++target) {
      RationalChannel accumulated{{0}, {1}};
      for (std::size_t source = 0; source < source_inputs; ++source) {
        accumulated = add_rational_channels(
            accumulated,
            scale_rational_channel(channels[output][source],
                                   transform[source][target]));
      }
      row.push_back(std::move(accumulated));
    }
    result.push_back(std::move(row));
  }
  return result;
}

[[nodiscard]] bool factorization_matches(const RationalChannelMatrix &source,
                                         const RationalChannelMatrix &basis,
                                         const RationalMatrix &projection) {
  const auto reconstructed = apply_constant_transform(basis, projection);
  if (source.size() != reconstructed.size()) {
    return false;
  }
  for (std::size_t row = 0; row < source.size(); ++row) {
    if (source[row].size() != reconstructed[row].size()) {
      return false;
    }
    for (std::size_t column = 0; column < source[row].size(); ++column) {
      if (source[row][column].numerator !=
              reconstructed[row][column].numerator ||
          source[row][column].denominator !=
              reconstructed[row][column].denominator) {
        return false;
      }
    }
  }
  return true;
}

[[nodiscard]] std::optional<mpq_class>
evaluate_exact(const RationalChannel &channel, const mpq_class &point) {
  mpq_class numerator = 0;
  mpq_class denominator = 0;
  for (const auto &coefficient : channel.numerator) {
    numerator = numerator * point + coefficient;
  }
  for (const auto &coefficient : channel.denominator) {
    denominator = denominator * point + coefficient;
  }
  if (denominator == 0) {
    return std::nullopt;
  }
  return numerator / denominator;
}

[[nodiscard]] RationalMatrix
normalize_transform_columns(RationalMatrix transform) {
  if (transform.empty()) {
    return {};
  }
  const std::size_t rows = transform.size();
  const std::size_t columns = transform.front().size();
  for (std::size_t column = 0; column < columns; ++column) {
    std::optional<mpq_class> first;
    for (std::size_t row = 0; row < rows; ++row) {
      if (transform[row][column] != 0) {
        first = transform[row][column];
        break;
      }
    }
    if (!first) {
      representative_error("SAA-5 candidate transform has a zero column");
    }
    for (std::size_t row = 0; row < rows; ++row) {
      transform[row][column] /= *first;
    }
  }
  return transform;
}

[[nodiscard]] int python_rounded_basis_points(std::size_t off,
                                              std::size_t total) {
  const std::size_t numerator = off * 10000U;
  std::size_t quotient = numerator / total;
  const std::size_t remainder = numerator % total;
  if (remainder * 2U > total ||
      (remainder * 2U == total && quotient % 2U != 0U)) {
    ++quotient;
  }
  return static_cast<int>(quotient);
}

struct CouplingResult final {
  int basis_points = 0;
  std::optional<std::vector<std::size_t>> pairing;
  bool decoupled = false;
};

template <typename Callback>
bool for_each_k_permutation(std::size_t count, std::size_t length,
                            Callback callback) {
  std::vector<std::size_t> current;
  std::vector<bool> used(count, false);
  std::function<bool()> visit = [&]() {
    if (current.size() == length) {
      return callback(current);
    }
    for (std::size_t value = 0; value < count; ++value) {
      if (used[value]) {
        continue;
      }
      used[value] = true;
      current.push_back(value);
      if (!visit()) {
        return false;
      }
      current.pop_back();
      used[value] = false;
    }
    return true;
  };
  return visit();
}

[[nodiscard]] CouplingResult
support_coupling(const RationalChannelMatrix &channels) {
  const std::size_t outputs = channels.size();
  const std::size_t inputs = outputs == 0U ? 0U : channels.front().size();
  if (inputs == 0U) {
    return {.basis_points = 0,
            .pairing = std::vector<std::size_t>{},
            .decoupled = true};
  }
  std::vector<std::vector<bool>> pattern(
      outputs, std::vector<bool>(inputs, false));
  std::size_t total = 0;
  for (std::size_t output = 0; output < outputs; ++output) {
    for (std::size_t input = 0; input < inputs; ++input) {
      pattern[output][input] = !channels[output][input].zero();
      total += pattern[output][input] ? 1U : 0U;
    }
  }
  if (total == 0U) {
    return {.basis_points = 0,
            .pairing = std::vector<std::size_t>{},
            .decoupled = true};
  }
  if (inputs > outputs) {
    return {.basis_points = 10000,
            .pairing = std::nullopt,
            .decoupled = false};
  }
  std::optional<std::vector<std::size_t>> best;
  int best_matched = -1;
  for_each_k_permutation(outputs, inputs,
                         [&](const std::vector<std::size_t> &pairing) {
                           int matched = 0;
                           for (std::size_t input = 0; input < inputs; ++input) {
                             matched += pattern[pairing[input]][input] ? 1 : 0;
                           }
                           if (matched > best_matched ||
                               (matched == best_matched &&
                                (!best || pairing < *best))) {
                             best = pairing;
                             best_matched = matched;
                           }
                           return true;
                         });
  const std::size_t off =
      total - static_cast<std::size_t>(std::max(best_matched, 0));
  return {.basis_points = python_rounded_basis_points(off, total),
          .pairing = best,
          .decoupled = best.has_value() &&
                       best_matched == static_cast<int>(inputs) && off == 0U};
}

[[nodiscard]] bool selector_projection(const RationalMatrix &projection) {
  std::set<std::size_t> selected;
  for (const auto &row : projection) {
    std::vector<std::size_t> positions;
    for (std::size_t index = 0; index < row.size(); ++index) {
      if (row[index] != 0) {
        positions.push_back(index);
      }
    }
    if (positions.size() != 1U || row[positions.front()] != 1 ||
        selected.contains(positions.front())) {
      return false;
    }
    selected.insert(positions.front());
  }
  return true;
}

[[nodiscard]] bool all_zero(const CanonicalMIMOCoupling &mimo) {
  return std::all_of(mimo.channels.begin(), mimo.channels.end(),
                     [](const auto &row) {
                       return std::all_of(row.begin(), row.end(),
                                          [](const auto &channel) {
                                            return std::all_of(
                                                channel.numerator.begin(),
                                                channel.numerator.end(),
                                                [](const mpq_class &value) {
                                                  return value == 0;
                                                });
                                          });
                     });
}

[[nodiscard]] MinimalityAssessment
zero_minimality(const CanonicalMIMOCoupling &mimo) {
  std::vector<std::size_t> nonpivots(mimo.input_count);
  std::iota(nonpivots.begin(), nonpivots.end(), 0U);
  return {.source_input_count = mimo.input_count,
          .effective_input_rank = 0,
          .redundant_input_count = mimo.input_count,
          .pivot_input_positions = {},
          .nonpivot_input_positions = std::move(nonpivots),
          .source_to_basis_projection = {},
          .status = "EXACT_ZERO_EFFECTIVE_INPUT_RANK",
          .exact = true};
}

struct MinimalityResult final {
  MinimalityAssessment assessment;
  RationalChannelMatrix source;
  RationalChannelMatrix basis;
};

[[nodiscard]] MinimalityResult minimality(const CanonicalMIMOCoupling &mimo,
                                          std::size_t term_budget) {
  auto source = source_rational_matrix(mimo);
  const auto vectors = constant_column_vectors(source, term_budget);
  auto rref = column_rref(vectors);
  std::vector<std::size_t> nonpivots;
  for (std::size_t input = 0; input < mimo.input_count; ++input) {
    if (std::find(rref.pivots.begin(), rref.pivots.end(), input) ==
        rref.pivots.end()) {
      nonpivots.push_back(input);
    }
  }
  RationalChannelMatrix basis;
  for (const auto &row : source) {
    std::vector<RationalChannel> basis_row;
    for (const std::size_t pivot : rref.pivots) {
      basis_row.push_back(row[pivot]);
    }
    basis.push_back(std::move(basis_row));
  }
  if (!factorization_matches(source, basis, rref.projection)) {
    representative_error(
        "internal SAA-5 constant-input factorization failed");
  }
  std::string status = "EXACT_REDUNDANT_INPUTS_QUOTIENTED";
  if (rref.pivots.size() == mimo.input_count) {
    status = "EXACT_FULL_CONSTANT_INPUT_RANK";
  } else if (rref.pivots.empty()) {
    status = "EXACT_ZERO_EFFECTIVE_INPUT_RANK";
  }
  MinimalityAssessment assessment{
      .source_input_count = mimo.input_count,
      .effective_input_rank = rref.pivots.size(),
      .redundant_input_count = mimo.input_count - rref.pivots.size(),
      .pivot_input_positions = std::move(rref.pivots),
      .nonpivot_input_positions = std::move(nonpivots),
      .source_to_basis_projection = std::move(rref.projection),
      .status = std::move(status),
      .exact = true};
  return {.assessment = std::move(assessment),
          .source = std::move(source),
          .basis = std::move(basis)};
}

[[nodiscard]] RationalMatrix transpose(const RationalMatrix &matrix) {
  if (matrix.empty()) {
    return {};
  }
  RationalMatrix result(matrix.front().size(),
                        std::vector<mpq_class>(matrix.size(), 0));
  for (std::size_t row = 0; row < matrix.size(); ++row) {
    for (std::size_t column = 0; column < matrix[row].size(); ++column) {
      result[column][row] = matrix[row][column];
    }
  }
  return result;
}

[[nodiscard]] TransformAdmissibility admissibility(
    const RationalMatrix &source_to_representative,
    const RationalMatrix &representative_to_source, std::size_t source_inputs,
    std::size_t representative_inputs, std::size_t coefficient_bit_limit) {
  const std::size_t bits =
      std::max(coefficient_bits(source_to_representative),
               coefficient_bits(representative_to_source));
  std::string inverse_status;
  bool invertible = false;
  if (representative_inputs == source_inputs) {
    inverse_status = "FULLY_INVERTIBLE";
    invertible = invert_rational_matrix(source_to_representative).has_value() &&
                 invert_rational_matrix(representative_to_source).has_value();
  } else {
    inverse_status = "INVERTIBLE_ON_BEHAVIORAL_QUOTIENT";
    invertible = matrix_rank(source_to_representative) == representative_inputs &&
                 matrix_rank(transpose(representative_to_source)) ==
                     representative_inputs &&
                 matrix_multiply(source_to_representative,
                                 representative_to_source) ==
                     identity(representative_inputs);
  }
  std::vector<std::string> warnings;
  if (bits > coefficient_bit_limit) {
    warnings.push_back(
        "representative transform exceeds configured exact coefficient bit budget");
  }
  const bool admissible_value = invertible && bits <= coefficient_bit_limit;
  return {.status = admissible_value ? "ADMISSIBLE_CONSTANT_REAL_TRANSFORM"
                                    : "INADMISSIBLE_TRANSFORM",
          .admissible = admissible_value,
          .causal = true,
          .stable = true,
          .finite_real = true,
          .invertibility_status =
              invertible ? std::move(inverse_status) : "NOT_INVERTIBLE",
          .coefficient_bits = bits,
          .coefficient_bit_limit = coefficient_bit_limit,
          .warnings = std::move(warnings)};
}

[[nodiscard]] RationalMatrix source_section(
    std::size_t source_inputs, const std::vector<std::size_t> &pivots,
    const RationalMatrix &basis_transform) {
  const std::size_t representative_inputs =
      basis_transform.empty() ? 0U : basis_transform.front().size();
  RationalMatrix section(source_inputs,
                         std::vector<mpq_class>(representative_inputs, 0));
  for (std::size_t basis_row = 0; basis_row < pivots.size(); ++basis_row) {
    section[pivots[basis_row]] = basis_transform[basis_row];
  }
  return section;
}

[[nodiscard]] RepresentativeInputCandidate candidate(
    const RationalChannelMatrix &source, const RationalChannelMatrix &basis,
    const MinimalityAssessment &minimality_assessment,
    const RationalMatrix &basis_transform, std::string transform_class,
    std::optional<mpq_class> probe,
    std::vector<std::size_t> selected_output_rows, int coupling_before_bp,
    std::size_t coefficient_bit_limit) {
  const std::size_t representative_inputs =
      minimality_assessment.effective_input_rank;
  std::optional<RationalMatrix> inverse;
  if (representative_inputs != 0U) {
    inverse = invert_rational_matrix(basis_transform);
    if (!inverse) {
      representative_error("SAA-5 candidate basis transform must be invertible");
    }
  }
  RationalMatrix source_to_representative =
      representative_inputs == 0U
          ? RationalMatrix{}
          : matrix_multiply(*inverse,
                            minimality_assessment.source_to_basis_projection);
  RationalMatrix representative_to_source = source_section(
      minimality_assessment.source_input_count,
      minimality_assessment.pivot_input_positions, basis_transform);
  auto representative_channels =
      apply_constant_transform(basis, basis_transform);
  if (representative_inputs != 0U &&
      !factorization_matches(
          source, representative_channels, source_to_representative)) {
    representative_error(
        "SAA-5 representative transform failed exact behavior preservation");
  }
  const auto coupling = support_coupling(representative_channels);
  auto admissibility_result = admissibility(
      source_to_representative, representative_to_source,
      minimality_assessment.source_input_count, representative_inputs,
      coefficient_bit_limit);
  const bool minimal =
      representative_inputs == minimality_assessment.effective_input_rank;
  std::string status;
  if (coupling.decoupled && admissibility_result.admissible && minimal) {
    status = "REPRESENTATIVE_FORM_CANDIDATE";
  } else if (coupling.decoupled) {
    status = "DECOUPLED_INADMISSIBLE_CANDIDATE";
  } else if (coupling.basis_points < coupling_before_bp) {
    status = "IMPROVED_NON_REPRESENTATIVE_CANDIDATE";
  } else {
    status = "NON_REPRESENTATIVE_CANDIDATE";
  }
  const bool requires_renormalization =
      !selector_projection(source_to_representative);
  const Json payload =
      {{"admissibility", to_json(admissibility_result)},
       {"claim_scope", "REPRESENTATIVE_INPUT_CANDIDATE_NOT_CANONICAL_STORE_ID"},
       {"coupling_after_bp", coupling.basis_points},
       {"minimal", minimal},
       {"probe", probe ? rational_json(*probe) : Json(nullptr)},
       {"representative_channels",
        rational_channel_matrix_json(representative_channels)},
       {"representative_to_source_section",
        rational_matrix_json(representative_to_source)},
       {"representation_version", representation_version},
       {"schema_version", 1},
       {"selected_output_rows", selected_output_rows},
       {"source_to_representative_projection",
        rational_matrix_json(source_to_representative)},
       {"transform_class", transform_class}};
  const std::string signature = contracts::sha256_json(payload);
  return {.candidate_id = "rep-candidate:sha256:" + signature,
          .status = std::move(status),
          .transform_class = std::move(transform_class),
          .algebraic_probe = std::move(probe),
          .selected_output_rows = std::move(selected_output_rows),
          .source_input_count = minimality_assessment.source_input_count,
          .representative_input_count = representative_inputs,
          .source_to_representative_projection =
              std::move(source_to_representative),
          .representative_to_source_section =
              std::move(representative_to_source),
          .basis_transform = basis_transform,
          .representative_channels = std::move(representative_channels),
          .coupling_before_bp = coupling_before_bp,
          .coupling_after_bp = coupling.basis_points,
          .preferred_input_to_output_pairing = coupling.pairing,
          .exact_decoupled = coupling.decoupled,
          .independent = true,
          .minimal = minimal,
          .requires_renormalization = requires_renormalization,
          .admissibility = admissibility_result,
          .canonical_signature = signature,
          .warnings = admissibility_result.warnings};
}

[[nodiscard]] std::optional<RationalMatrix> probe_transform(
    const RationalChannelMatrix &basis,
    const std::vector<std::size_t> &output_rows, const mpq_class &point) {
  const std::size_t size = basis.empty() ? 0U : basis.front().size();
  RationalMatrix matrix;
  for (const std::size_t output : output_rows) {
    std::vector<mpq_class> row;
    for (std::size_t input = 0; input < size; ++input) {
      const auto value = evaluate_exact(basis[output][input], point);
      if (!value) {
        return std::nullopt;
      }
      row.push_back(*value);
    }
    matrix.push_back(std::move(row));
  }
  auto inverse = invert_rational_matrix(matrix);
  if (!inverse) {
    return std::nullopt;
  }
  return normalize_transform_columns(std::move(*inverse));
}

[[nodiscard]] RepresentationAssessment zero_assessment(
    const CanonicalMIMOCoupling &mimo) {
  auto minimality_assessment = zero_minimality(mimo);
  const std::string reason =
      "all declared inputs have zero observable effect and collapse to the zero-input behavioral quotient";
  const Json payload =
      {{"canonical_admission_eligible", false},
       {"coupling_bp", 0},
       {"minimality", to_json(minimality_assessment)},
       {"mimo_ordered_signature", mimo.ordered_signature},
       {"pairing", Json::array()},
       {"reason", reason},
       {"representation_version", representation_version},
       {"schema_version", 1},
       {"status", "NON_REPRESENTATIVE_REDUNDANT_INPUTS"}};
  return {.schema_version = 1,
          .representation_version_value = std::string(representation_version),
          .status = "NON_REPRESENTATIVE_REDUNDANT_INPUTS",
          .reason = reason,
          .coupling_bp = 0,
          .preferred_input_to_output_pairing =
              std::vector<std::size_t>{},
          .canonical_admission_eligible = false,
          .requires_representative_search = true,
          .minimality = std::move(minimality_assessment),
          .assessment_signature = contracts::sha256_json(payload),
          .warnings = {}};
}

} // namespace

bool RepresentativeInputSearch::representative_found() const noexcept {
  return best_candidate.has_value() &&
         best_candidate->status == "REPRESENTATIVE_FORM_CANDIDATE" &&
         best_candidate->admissibility.admissible;
}

Json to_json(const MinimalityAssessment &value) {
  return {{"effective_input_rank", value.effective_input_rank},
          {"exact", value.exact},
          {"nonpivot_input_positions", value.nonpivot_input_positions},
          {"pivot_input_positions", value.pivot_input_positions},
          {"redundant_input_count", value.redundant_input_count},
          {"source_input_count", value.source_input_count},
          {"source_to_basis_projection",
           rational_matrix_json(value.source_to_basis_projection)},
          {"status", value.status}};
}

Json to_json(const RepresentationAssessment &value) {
  return {{"assessment_signature", value.assessment_signature},
          {"canonical_admission_eligible",
           value.canonical_admission_eligible},
          {"coupling_bp",
           value.coupling_bp ? Json(*value.coupling_bp) : Json(nullptr)},
          {"minimality",
           value.minimality ? to_json(*value.minimality) : Json(nullptr)},
          {"preferred_input_to_output_pairing",
           value.preferred_input_to_output_pairing
               ? Json(*value.preferred_input_to_output_pairing)
               : Json(nullptr)},
          {"reason", value.reason},
          {"representation_version", value.representation_version_value},
          {"requires_representative_search",
           value.requires_representative_search},
          {"schema_version", value.schema_version},
          {"status", value.status},
          {"warnings", value.warnings}};
}

Json to_json(const TransformAdmissibility &value) {
  return {{"admissible", value.admissible},
          {"causal", value.causal},
          {"coefficient_bit_limit", value.coefficient_bit_limit},
          {"coefficient_bits", value.coefficient_bits},
          {"finite_real", value.finite_real},
          {"invertibility_status", value.invertibility_status},
          {"stable", value.stable},
          {"status", value.status},
          {"warnings", value.warnings}};
}

Json to_json(const RepresentativeInputCandidate &value) {
  return {{"admissibility", to_json(value.admissibility)},
          {"algebraic_probe",
           value.algebraic_probe ? rational_json(*value.algebraic_probe)
                                 : Json(nullptr)},
          {"basis_transform", rational_matrix_json(value.basis_transform)},
          {"candidate_id", value.candidate_id},
          {"canonical_signature", value.canonical_signature},
          {"coupling_after_bp", value.coupling_after_bp},
          {"coupling_before_bp", value.coupling_before_bp},
          {"exact_decoupled", value.exact_decoupled},
          {"independent", value.independent},
          {"minimal", value.minimal},
          {"preferred_input_to_output_pairing",
           value.preferred_input_to_output_pairing
               ? Json(*value.preferred_input_to_output_pairing)
               : Json(nullptr)},
          {"representative_channels",
           rational_channel_matrix_json(value.representative_channels)},
          {"representative_input_count", value.representative_input_count},
          {"representative_to_source_section",
           rational_matrix_json(value.representative_to_source_section)},
          {"requires_renormalization", value.requires_renormalization},
          {"selected_output_rows", value.selected_output_rows},
          {"source_input_count", value.source_input_count},
          {"source_to_representative_projection",
           rational_matrix_json(value.source_to_representative_projection)},
          {"status", value.status},
          {"transform_class", value.transform_class},
          {"warnings", value.warnings}};
}

Json to_json(const RepresentativeInputSearch &value) {
  Json candidates = Json::array();
  for (const auto &candidate_value : value.candidates) {
    candidates.push_back(to_json(candidate_value));
  }
  return {{"audit_hash", value.audit_hash},
          {"best_candidate",
           value.best_candidate ? to_json(*value.best_candidate)
                                : Json(nullptr)},
          {"candidates", candidates},
          {"candidates_considered", value.candidates_considered},
          {"minimality",
           value.minimality ? to_json(*value.minimality) : Json(nullptr)},
          {"representation_version", value.representation_version_value},
          {"schema_version", value.schema_version},
          {"search_status", value.search_status},
          {"source_assessment", to_json(value.source_assessment)},
          {"warnings", value.warnings}};
}

RepresentationAssessment assess_mimo_representation(
    const CanonicalMIMOCoupling &mimo, std::size_t rank_term_budget) {
  if (mimo.dynamic_strength == "EXACT_MIMO_LINEAR_DYNAMICS" &&
      all_zero(mimo)) {
    return zero_assessment(mimo);
  }
  if (rank_term_budget < 1U) {
    representative_error("max_rank_terms must be positive");
  }
  std::string status;
  std::string reason;
  std::optional<int> coupling_bp;
  std::optional<std::vector<std::size_t>> pairing;
  bool eligible = false;
  bool requires_search = true;
  std::optional<MinimalityAssessment> minimality_assessment;
  std::vector<std::string> warnings;
  if (mimo.dynamic_strength != "EXACT_MIMO_LINEAR_DYNAMICS") {
    status = "REPRESENTATION_UNRESOLVED_APPROXIMATE";
    reason =
        "coupling or independence cannot be promoted to representative-form evidence because one or more transfer channels are approximate";
  } else {
    try {
      auto result = minimality(mimo, rank_term_budget);
      minimality_assessment = std::move(result.assessment);
      const auto coupling = support_coupling(result.source);
      coupling_bp = coupling.basis_points;
      pairing = coupling.pairing;
      if (minimality_assessment->effective_input_rank <
          minimality_assessment->source_input_count) {
        status = "NON_REPRESENTATIVE_REDUNDANT_INPUTS";
        reason =
            "declared inputs contain exact constant linear redundancy and therefore do not form a minimal representative input basis";
      } else if (coupling.decoupled) {
        status = "REPRESENTATIVE_EXACT";
        reason =
            "the exact normalized input basis is full-rank and has no residual cross-output coupling up to one-to-one port pairing";
        eligible = true;
        requires_search = false;
      } else {
        status = "NON_REPRESENTATIVE_COUPLED";
        reason =
            "exact normalized inputs remain coupled, so the current input coordinates are treated as a non-representative source description";
      }
    } catch (const common::Error &error) {
      status = "REPRESENTATION_UNRESOLVED_RANK_BUDGET";
      reason = error.what();
      warnings.push_back(error.what());
    }
  }
  const Json payload =
      {{"canonical_admission_eligible", eligible},
       {"coupling_bp", coupling_bp ? Json(*coupling_bp) : Json(nullptr)},
       {"minimality", minimality_assessment
                           ? to_json(*minimality_assessment)
                           : Json(nullptr)},
       {"mimo_ordered_signature", mimo.ordered_signature},
       {"pairing", pairing ? Json(*pairing) : Json(nullptr)},
       {"reason", reason},
       {"representation_version", representation_version},
       {"schema_version", 1},
       {"status", status}};
  return {.schema_version = 1,
          .representation_version_value = std::string(representation_version),
          .status = std::move(status),
          .reason = std::move(reason),
          .coupling_bp = coupling_bp,
          .preferred_input_to_output_pairing = std::move(pairing),
          .canonical_admission_eligible = eligible,
          .requires_representative_search = requires_search,
          .minimality = std::move(minimality_assessment),
          .assessment_signature = contracts::sha256_json(payload),
          .warnings = std::move(warnings)};
}

RepresentativeInputSearch discover_representative_inputs(
    const CanonicalMIMOCoupling &mimo, std::size_t rank_term_budget,
    std::size_t transform_budget, std::size_t coefficient_bit_budget) {
  if (mimo.dynamic_strength == "EXACT_MIMO_LINEAR_DYNAMICS" &&
      all_zero(mimo)) {
    auto minimality_assessment = zero_minimality(mimo);
    auto assessment = assess_mimo_representation(mimo, rank_term_budget);
    TransformAdmissibility admissibility_result{
        .status = "ADMISSIBLE_BEHAVIORAL_ZERO_INPUT_QUOTIENT",
        .admissible = true,
        .causal = true,
        .stable = true,
        .finite_real = true,
        .invertibility_status = "INVERTIBLE_ON_BEHAVIORAL_QUOTIENT",
        .coefficient_bits = 1,
        .coefficient_bit_limit = coefficient_bit_budget,
        .warnings = {}};
    const Json candidate_payload =
        {{"claim_scope", "ZERO_EFFECTIVE_INPUT_REPRESENTATIVE_CANDIDATE"},
         {"representation_version", representation_version},
         {"schema_version", 1},
         {"source_signature", mimo.ordered_signature}};
    const std::string signature = contracts::sha256_json(candidate_payload);
    RationalMatrix section(mimo.input_count);
    RationalChannelMatrix channels(mimo.output_count);
    RepresentativeInputCandidate candidate_result{
        .candidate_id = "rep-candidate:sha256:" + signature,
        .status = "REPRESENTATIVE_FORM_CANDIDATE",
        .transform_class = "BEHAVIORAL_ZERO_INPUT_QUOTIENT",
        .algebraic_probe = std::nullopt,
        .selected_output_rows = {},
        .source_input_count = mimo.input_count,
        .representative_input_count = 0,
        .source_to_representative_projection = {},
        .representative_to_source_section = section,
        .basis_transform = {},
        .representative_channels = channels,
        .coupling_before_bp = 0,
        .coupling_after_bp = 0,
        .preferred_input_to_output_pairing =
            std::vector<std::size_t>{},
        .exact_decoupled = true,
        .independent = true,
        .minimal = true,
        .requires_renormalization = false,
        .admissibility = admissibility_result,
        .canonical_signature = signature,
        .warnings = {}};
    const Json audit =
        {{"best_candidate", signature},
         {"candidate_signatures", Json::array({signature})},
         {"candidates_considered", 1},
         {"minimality", to_json(minimality_assessment)},
         {"representation_version", representation_version},
         {"schema_version", 1},
         {"search_status", "REPRESENTATIVE_FORM_FOUND"},
         {"source_assessment", assessment.assessment_signature},
         {"zero_input_quotient", true}};
    return {.schema_version = 1,
            .representation_version_value =
                std::string(representation_version),
            .source_assessment = std::move(assessment),
            .minimality = std::move(minimality_assessment),
            .search_status = "REPRESENTATIVE_FORM_FOUND",
            .candidates_considered = 1,
            .candidates = {candidate_result},
            .best_candidate = std::move(candidate_result),
            .audit_hash = contracts::sha256_json(audit),
            .warnings = {}};
  }
  if (transform_budget < 1U) {
    representative_error("max_transforms must be positive");
  }
  if (coefficient_bit_budget < 1U) {
    representative_error(
        "max_transform_coefficient_bits must be positive");
  }
  auto assessment = assess_mimo_representation(mimo, rank_term_budget);
  std::vector<std::string> warnings = assessment.warnings;
  if (mimo.dynamic_strength != "EXACT_MIMO_LINEAR_DYNAMICS") {
    const Json payload =
        {{"representation_version", representation_version},
         {"search_status", "REPRESENTATIVE_FORM_UNRESOLVED_APPROXIMATE"},
         {"source_assessment", assessment.assessment_signature}};
    return {.schema_version = 1,
            .representation_version_value =
                std::string(representation_version),
            .source_assessment = std::move(assessment),
            .minimality = std::nullopt,
            .search_status = "REPRESENTATIVE_FORM_UNRESOLVED_APPROXIMATE",
            .candidates_considered = 0,
            .candidates = {},
            .best_candidate = std::nullopt,
            .audit_hash = contracts::sha256_json(payload),
            .warnings = std::move(warnings)};
  }

  auto minimality_result = minimality(mimo, rank_term_budget);
  const auto source_coupling = support_coupling(minimality_result.source);
  const std::size_t rank =
      minimality_result.assessment.effective_input_rank;
  std::vector<RepresentativeInputCandidate> candidates;
  std::set<std::string> seen;
  std::size_t considered = 0;
  bool budget_exhausted = false;

  auto identity_candidate = candidate(
      minimality_result.source, minimality_result.basis,
      minimality_result.assessment, identity(rank),
      rank < minimality_result.assessment.source_input_count
          ? "IDENTITY_AFTER_REDUNDANCY_QUOTIENT"
          : "IDENTITY",
      std::nullopt, {}, source_coupling.basis_points,
      coefficient_bit_budget);
  candidates.push_back(identity_candidate);
  seen.insert(identity_candidate.canonical_signature);
  considered = 1;

  if (!identity_candidate.exact_decoupled && rank <= mimo.output_count) {
    const auto &probes = mimo.domain == "CONTINUOUS"
                             ? continuous_algebraic_probes
                             : discrete_algebraic_probes;
    for_each_k_permutation(
        mimo.output_count, rank,
        [&](const std::vector<std::size_t> &output_rows) {
          for (const auto &probe : probes) {
            if (considered >= transform_budget) {
              budget_exhausted = true;
              return false;
            }
            auto transform = probe_transform(minimality_result.basis,
                                             output_rows, probe);
            if (!transform) {
              continue;
            }
            auto next = candidate(
                minimality_result.source, minimality_result.basis,
                minimality_result.assessment, *transform,
                "CONSTANT_LINEAR_ALGEBRAIC_PROBE", probe, output_rows,
                source_coupling.basis_points, coefficient_bit_budget);
            ++considered;
            if (seen.insert(next.canonical_signature).second) {
              candidates.push_back(std::move(next));
            }
          }
          return true;
        });
  }

  std::sort(candidates.begin(), candidates.end(),
            [](const RepresentativeInputCandidate &left,
               const RepresentativeInputCandidate &right) {
              const int left_rank =
                  left.status == "REPRESENTATIVE_FORM_CANDIDATE" &&
                          left.admissibility.admissible
                      ? 0
                      : 1;
              const int right_rank =
                  right.status == "REPRESENTATIVE_FORM_CANDIDATE" &&
                          right.admissibility.admissible
                      ? 0
                      : 1;
              return std::tie(left_rank, left.coupling_after_bp,
                              left.admissibility.coefficient_bits,
                              left.representative_input_count,
                              left.canonical_signature) <
                     std::tie(right_rank, right.coupling_after_bp,
                              right.admissibility.coefficient_bits,
                              right.representative_input_count,
                              right.canonical_signature);
            });
  std::optional<RepresentativeInputCandidate> best;
  if (!candidates.empty()) {
    best = candidates.front();
  }
  std::string search_status;
  if (best && best->status == "REPRESENTATIVE_FORM_CANDIDATE" &&
      best->admissibility.admissible) {
    search_status = "REPRESENTATIVE_FORM_FOUND";
  } else if (budget_exhausted) {
    search_status = "REPRESENTATIVE_SEARCH_BUDGET_EXHAUSTED";
    warnings.push_back(
        "bounded SAA-5 transform search exhausted before a representative form was found");
  } else {
    search_status =
        "REPRESENTATIVE_FORM_UNRESOLVED_CONSTANT_LINEAR_SEARCH";
    warnings.push_back(
        "no admissible exact constant input transform removed all residual coupling; a stronger dynamic or nonlinear representation search is required");
  }
  Json candidate_signatures = Json::array();
  for (const auto &candidate_value : candidates) {
    candidate_signatures.push_back(candidate_value.canonical_signature);
  }
  const Json audit =
      {{"best_candidate",
        best ? Json(best->canonical_signature) : Json(nullptr)},
       {"candidate_signatures", candidate_signatures},
       {"candidates_considered", considered},
       {"max_transform_coefficient_bits", coefficient_bit_budget},
       {"max_transforms", transform_budget},
       {"minimality", to_json(minimality_result.assessment)},
       {"representation_version", representation_version},
       {"schema_version", 1},
       {"search_status", search_status},
       {"source_assessment", assessment.assessment_signature}};
  return {.schema_version = 1,
          .representation_version_value = std::string(representation_version),
          .source_assessment = std::move(assessment),
          .minimality = minimality_result.assessment,
          .search_status = std::move(search_status),
          .candidates_considered = considered,
          .candidates = std::move(candidates),
          .best_candidate = std::move(best),
          .audit_hash = contracts::sha256_json(audit),
          .warnings = std::move(warnings)};
}

} // namespace statewright::saa
