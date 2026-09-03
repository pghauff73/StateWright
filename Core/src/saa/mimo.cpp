#include "statewright/saa/mimo.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numeric>
#include <set>
#include <string>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

constexpr double continuous_frequencies[] = {0.1, 1.0, 10.0};
constexpr double pi = 3.141592653589793238462643383279502884;
constexpr double discrete_angles[] = {pi / 4.0, pi / 2.0, 3.0 * pi / 4.0};

[[noreturn]] void mimo_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument,
                      std::move(message));
}

[[nodiscard]] std::string trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n\f\v");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n\f\v");
  return value.substr(first, last - first + 1U);
}

[[nodiscard]] std::string normalized_domain(std::string value) {
  value = trim(std::move(value));
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::toupper(character));
                 });
  if (value == "S" || value == "S_DOMAIN" ||
      value == "CONTINUOUS_TIME") {
    return "CONTINUOUS";
  }
  if (value == "Z" || value == "Z_DOMAIN" || value == "DISCRETE_TIME") {
    return "DISCRETE";
  }
  if (value != "CONTINUOUS" && value != "DISCRETE") {
    mimo_error("unsupported SAA-4 MIMO domain: " + value);
  }
  return value;
}

[[nodiscard]] Json channel_payload(const CanonicalLinearDynamics &channel) {
  return {{"denominator", polynomial_json(channel.denominator)},
          {"domain", channel.domain},
          {"dynamic_order", channel.dynamic_order},
          {"dynamic_strength", channel.dynamic_strength},
          {"normalized_sample_interval",
           channel.normalized_sample_interval
               ? rational_json(*channel.normalized_sample_interval)
               : Json(nullptr)},
          {"numerator", polynomial_json(channel.numerator)},
          {"proper", channel.proper},
          {"relative_degree",
           channel.relative_degree ? Json(*channel.relative_degree)
                                   : Json(nullptr)},
          {"variable", channel.variable}};
}

[[nodiscard]] bool zero_channel(const CanonicalLinearDynamics &channel) {
  return std::all_of(channel.numerator.begin(), channel.numerator.end(),
                     [](const mpq_class &value) { return value == 0; });
}

[[nodiscard]] std::string local_contract_strength(
    const std::vector<NormalizationBinding> &bindings,
    const NormalizationContract &normalization) {
  std::set<std::string> strengths;
  for (const auto &binding : bindings) {
    strengths.insert(binding.strength());
  }
  if (normalization.time) {
    strengths.insert(normalization.time->strength());
  }
  if (strengths.empty() || strengths == std::set<std::string>{"EXACT"}) {
    return "EXACT_NORMALIZATION";
  }
  if (strengths == std::set<std::string>{"APPROXIMATE"}) {
    return "APPROXIMATE_NORMALIZATION";
  }
  return "MIXED_NORMALIZATION";
}

[[nodiscard]] NormalizationBinding local_binding(
    const NormalizationBinding &source, std::string role) {
  return {std::move(role), 0, source.data_type, source.shape, source.bound};
}

[[nodiscard]] NormalizationContract channel_normalization(
    const NormalizationContract &normalization,
    const NormalizationBinding &input_binding,
    const NormalizationBinding &output_binding) {
  std::vector<NormalizationBinding> bindings;
  bindings.push_back(local_binding(input_binding, "INPUT"));
  bindings.push_back(local_binding(output_binding, "OUTPUT"));
  const std::string strength =
      local_contract_strength(bindings, normalization);
  Json audit_bindings = Json::array();
  Json canonical_bindings = Json::array();
  for (const auto &binding : bindings) {
    audit_bindings.push_back(binding.audit_payload());
    canonical_bindings.push_back(binding.canonical_payload());
  }
  const Json audit =
      {{"bindings", audit_bindings},
       {"normalization_strength", strength},
       {"normalizer_version", normalization.normalizer_version_value},
       {"schema_version", 1},
       {"source_contract_hash", normalization.contract_hash},
       {"source_positions",
        {{"input", input_binding.position},
         {"output", output_binding.position}}},
       {"time", normalization.time ? normalization.time->audit_payload()
                                   : Json(nullptr)}};
  const Json canonical =
      {{"bindings", canonical_bindings},
       {"claim_scope", "LOCAL_SISO_VIEW_OF_MIMO_NORMALIZATION"},
       {"normalization_strength", strength},
       {"normalizer_version", normalization.normalizer_version_value},
       {"schema_version", 1},
       {"time", normalization.time ? normalization.time->canonical_payload()
                                   : Json(nullptr)}};
  std::vector<std::string> warnings;
  if (strength != "EXACT_NORMALIZATION") {
    warnings.push_back(
        "local MIMO channel normalization includes approximate/observed bounds");
  }
  return {.schema_version = 1,
          .normalizer_version_value = normalization.normalizer_version_value,
          .bindings = std::move(bindings),
          .time = normalization.time,
          .normalization_strength = strength,
          .contract_hash = contracts::sha256_json(audit),
          .canonical_signature = contracts::sha256_json(canonical),
          .warnings = std::move(warnings)};
}

struct BindingSet final {
  std::vector<const NormalizationBinding *> inputs;
  std::vector<const NormalizationBinding *> outputs;
};

[[nodiscard]] BindingSet validate_normalization(
    const NormalizationContract &normalization, std::size_t outputs,
    std::size_t inputs) {
  BindingSet result;
  for (const auto &binding : normalization.bindings) {
    if (binding.role == "INPUT") {
      result.inputs.push_back(&binding);
    } else if (binding.role == "OUTPUT") {
      result.outputs.push_back(&binding);
    }
  }
  const auto by_position = [](const NormalizationBinding *left,
                              const NormalizationBinding *right) {
    return left->position < right->position;
  };
  std::sort(result.inputs.begin(), result.inputs.end(), by_position);
  std::sort(result.outputs.begin(), result.outputs.end(), by_position);
  if (result.inputs.size() != inputs || result.outputs.size() != outputs) {
    mimo_error(
        "SAA-4 normalization input/output counts must match transfer-matrix dimensions");
  }
  if (!normalization.time) {
    mimo_error("SAA-4 requires SAA-2 characteristic time");
  }
  for (const auto *binding : result.inputs) {
    if (binding->canonical_data_type() != "CONTINUOUS_SCALAR") {
      mimo_error(
          "SAA-4 v1 requires continuous scalar input/output coordinates");
    }
    if (!binding->shape.empty()) {
      mimo_error(
          "SAA-4 v1 does not support shaped input/output coordinates");
    }
  }
  for (const auto *binding : result.outputs) {
    if (binding->canonical_data_type() != "CONTINUOUS_SCALAR") {
      mimo_error(
          "SAA-4 v1 requires continuous scalar input/output coordinates");
    }
    if (!binding->shape.empty()) {
      mimo_error(
          "SAA-4 v1 does not support shaped input/output coordinates");
    }
  }
  return result;
}

[[nodiscard]] Json matrix_payload(
    const CanonicalDynamicsMatrix &channels,
    const std::vector<std::size_t> &output_permutation,
    const std::vector<std::size_t> &input_permutation) {
  Json matrix = Json::array();
  for (const std::size_t output : output_permutation) {
    Json row = Json::array();
    for (const std::size_t input : input_permutation) {
      row.push_back(channel_payload(channels[output][input]));
    }
    matrix.push_back(std::move(row));
  }
  return matrix;
}

[[nodiscard]] std::size_t factorial(std::size_t value) {
  std::size_t result = 1;
  for (std::size_t current = 2; current <= value; ++current) {
    result *= current;
  }
  return result;
}

struct PermutationResult final {
  std::optional<std::string> signature;
  std::string strength;
  std::optional<std::vector<std::size_t>> output;
  std::optional<std::vector<std::size_t>> input;
  std::size_t considered = 0;
};

[[nodiscard]] PermutationResult canonical_permutation(
    const CanonicalDynamicsMatrix &channels, std::string_view domain,
    std::string_view variable,
    const std::optional<mpq_class> &normalized_sample_interval,
    std::string_view dynamic_strength, std::size_t budget) {
  const std::size_t outputs = channels.size();
  const std::size_t inputs = channels.front().size();
  const std::size_t count = factorial(outputs) * factorial(inputs);
  if (count > budget) {
    return {.signature = std::nullopt,
            .strength = "ORDERED_ONLY_PERMUTATION_BUDGET_EXCEEDED",
            .output = std::nullopt,
            .input = std::nullopt,
            .considered = 0};
  }

  std::vector<std::size_t> output(outputs);
  std::vector<std::size_t> input(inputs);
  std::iota(output.begin(), output.end(), 0U);
  std::iota(input.begin(), input.end(), 0U);
  std::optional<std::string> best_key;
  Json best_payload;
  std::optional<std::vector<std::size_t>> best_output;
  std::optional<std::vector<std::size_t>> best_input;
  std::size_t considered = 0;
  do {
    std::iota(input.begin(), input.end(), 0U);
    do {
      Json payload =
          {{"claim_scope",
            "NORMALIZED_MIMO_LINEAR_IO_DYNAMICS_UP_TO_PORT_PERMUTATION"},
           {"domain", domain},
           {"dynamic_strength", dynamic_strength},
           {"inputs", inputs},
           {"matrix", matrix_payload(channels, output, input)},
           {"mimo_version", mimo_version},
           {"normalized_sample_interval",
            normalized_sample_interval
                ? rational_json(*normalized_sample_interval)
                : Json(nullptr)},
           {"outputs", outputs},
           {"schema_version", 1},
           {"variable", variable}};
      const std::string key = contracts::canonical_json(payload);
      ++considered;
      if (!best_key || key < *best_key) {
        best_key = key;
        best_payload = std::move(payload);
        best_output = output;
        best_input = input;
      }
    } while (std::next_permutation(input.begin(), input.end()));
  } while (std::next_permutation(output.begin(), output.end()));
  if (!best_key || !best_output || !best_input) {
    mimo_error("internal SAA-4 canonical permutation search failed");
  }
  return {.signature = contracts::sha256_json(best_payload),
          .strength = "EXACT_PORT_PERMUTATION",
          .output = std::move(best_output),
          .input = std::move(best_input),
          .considered = considered};
}

[[nodiscard]] std::optional<mpq_class>
steady_gain(const CanonicalLinearDynamics &channel) {
  if (zero_channel(channel)) {
    return mpq_class(0);
  }
  mpq_class numerator = 0;
  mpq_class denominator = 0;
  if (channel.domain == "CONTINUOUS") {
    numerator = channel.numerator.back();
    denominator = channel.denominator.back();
  } else {
    for (const auto &value : channel.numerator) {
      numerator += value;
    }
    for (const auto &value : channel.denominator) {
      denominator += value;
    }
  }
  if (denominator == 0) {
    return std::nullopt;
  }
  return numerator / denominator;
}

[[nodiscard]] std::optional<RationalMatrix>
matrix_inverse(const RationalMatrix &matrix) {
  const std::size_t size = matrix.size();
  if (size == 0U || std::any_of(matrix.begin(), matrix.end(),
                                [size](const auto &row) {
                                  return row.size() != size;
                                })) {
    return std::nullopt;
  }
  RationalMatrix work(size, std::vector<mpq_class>(size * 2U, 0));
  for (std::size_t row = 0; row < size; ++row) {
    for (std::size_t column = 0; column < size; ++column) {
      work[row][column] = matrix[row][column];
      work[row][size + column] = row == column ? 1 : 0;
    }
  }
  for (std::size_t column = 0; column < size; ++column) {
    std::size_t pivot = column;
    while (pivot < size && work[pivot][column] == 0) {
      ++pivot;
    }
    if (pivot == size) {
      return std::nullopt;
    }
    if (pivot != column) {
      std::swap(work[column], work[pivot]);
    }
    const mpq_class pivot_value = work[column][column];
    for (auto &value : work[column]) {
      value /= pivot_value;
    }
    for (std::size_t row = 0; row < size; ++row) {
      if (row == column) {
        continue;
      }
      const mpq_class factor = work[row][column];
      if (factor == 0) {
        continue;
      }
      for (std::size_t index = 0; index < size * 2U; ++index) {
        work[row][index] -= factor * work[column][index];
      }
    }
  }
  RationalMatrix inverse(size, std::vector<mpq_class>(size, 0));
  for (std::size_t row = 0; row < size; ++row) {
    std::copy(work[row].begin() + static_cast<std::ptrdiff_t>(size),
              work[row].end(), inverse[row].begin());
  }
  return inverse;
}

struct RgaResult final {
  std::optional<RationalMatrix> rga;
  std::optional<RationalMatrix> inverse;
};

[[nodiscard]] RgaResult relative_gain_array(
    const OptionalRationalMatrix &steady_gain_matrix) {
  const std::size_t size = steady_gain_matrix.size();
  if (size == 0U ||
      std::any_of(steady_gain_matrix.begin(), steady_gain_matrix.end(),
                  [size](const auto &row) { return row.size() != size; })) {
    return {};
  }
  RationalMatrix matrix(size, std::vector<mpq_class>(size, 0));
  for (std::size_t row = 0; row < size; ++row) {
    for (std::size_t column = 0; column < size; ++column) {
      if (!steady_gain_matrix[row][column]) {
        return {};
      }
      matrix[row][column] = *steady_gain_matrix[row][column];
    }
  }
  auto inverse = matrix_inverse(matrix);
  if (!inverse) {
    return {};
  }
  RationalMatrix rga(size, std::vector<mpq_class>(size, 0));
  for (std::size_t row = 0; row < size; ++row) {
    for (std::size_t column = 0; column < size; ++column) {
      rga[row][column] = matrix[row][column] * (*inverse)[column][row];
    }
  }
  return {.rga = std::move(rga), .inverse = std::move(inverse)};
}

struct PairingResult final {
  std::vector<std::size_t> pairing;
  mpq_class off_pairing_mass;
};

[[nodiscard]] PairingResult preferred_pairing(const RationalMatrix &rga) {
  const std::size_t size = rga.size();
  std::vector<std::size_t> permutation(size);
  std::iota(permutation.begin(), permutation.end(), 0U);
  std::optional<std::vector<std::size_t>> best;
  std::optional<mpq_class> best_score;
  do {
    mpq_class score = 0;
    for (std::size_t row = 0; row < size; ++row) {
      score += abs(rga[row][permutation[row]]);
    }
    if (!best_score || score > *best_score ||
        (score == *best_score && permutation < *best)) {
      best = permutation;
      best_score = score;
    }
  } while (std::next_permutation(permutation.begin(), permutation.end()));
  if (!best || !best_score) {
    mimo_error("internal SAA-4 RGA pairing search failed");
  }
  mpq_class total = 0;
  mpq_class off = 0;
  for (std::size_t row = 0; row < size; ++row) {
    for (std::size_t column = 0; column < size; ++column) {
      total += abs(rga[row][column]);
      if (column != (*best)[row]) {
        off += abs(rga[row][column]);
      }
    }
  }
  return {.pairing = *best,
          .off_pairing_mass = total == 0 ? mpq_class(0) : off / total};
}

[[nodiscard]] std::optional<std::vector<std::size_t>>
exact_diagonal_permutation(const CanonicalDynamicsMatrix &channels) {
  const std::size_t size = channels.size();
  if (size == 0U || std::any_of(channels.begin(), channels.end(),
                                [size](const auto &row) {
                                  return row.size() != size;
                                })) {
    return std::nullopt;
  }
  std::vector<std::size_t> permutation(size);
  std::iota(permutation.begin(), permutation.end(), 0U);
  do {
    bool diagonal = true;
    for (std::size_t row = 0; row < size && diagonal; ++row) {
      for (std::size_t column = 0; column < size; ++column) {
        if (column != permutation[row] &&
            !zero_channel(channels[row][column])) {
          diagonal = false;
          break;
        }
      }
    }
    if (diagonal) {
      return permutation;
    }
  } while (std::next_permutation(permutation.begin(), permutation.end()));
  return std::nullopt;
}

[[nodiscard]] RationalPolynomial polynomial_add(RationalPolynomial first,
                                                RationalPolynomial second) {
  const std::size_t size = std::max(first.size(), second.size());
  first.insert(first.begin(), size - first.size(), mpq_class(0));
  second.insert(second.begin(), size - second.size(), mpq_class(0));
  RationalPolynomial result(size, 0);
  for (std::size_t index = 0; index < size; ++index) {
    result[index] = first[index] + second[index];
  }
  auto leading = result.begin();
  while (leading + 1 != result.end() && *leading == 0) {
    ++leading;
  }
  return {leading, result.end()};
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

[[nodiscard]] RationalChannel rational_scaled(const RationalChannel &channel,
                                              const mpq_class &scalar) {
  if (scalar == 0 || channel.zero()) {
    return {{0}, {1}};
  }
  RationalPolynomial numerator = channel.numerator;
  for (auto &value : numerator) {
    value *= scalar;
  }
  auto reduced =
      reduce_exact_transfer(std::move(numerator), channel.denominator);
  return {std::move(reduced.numerator), std::move(reduced.denominator)};
}

[[nodiscard]] RationalChannel rational_add(const RationalChannel &first,
                                           const RationalChannel &second) {
  if (first.zero()) {
    return second;
  }
  if (second.zero()) {
    return first;
  }
  auto numerator = polynomial_add(
      polynomial_multiply(first.numerator, second.denominator),
      polynomial_multiply(second.numerator, first.denominator));
  auto denominator =
      polynomial_multiply(first.denominator, second.denominator);
  auto reduced =
      reduce_exact_transfer(std::move(numerator), std::move(denominator));
  return {std::move(reduced.numerator), std::move(reduced.denominator)};
}

[[nodiscard]] RationalChannel
to_rational(const CanonicalLinearDynamics &channel) {
  return {channel.numerator, channel.denominator};
}

[[nodiscard]] std::vector<std::vector<RationalChannel>> apply_static_decoupler(
    const CanonicalDynamicsMatrix &channels,
    const RationalMatrix &decoupler) {
  const std::size_t outputs = channels.size();
  const std::size_t inputs = channels.front().size();
  if (inputs != decoupler.size() ||
      std::any_of(decoupler.begin(), decoupler.end(),
                  [inputs](const auto &row) {
                    return row.size() != inputs;
                  })) {
    mimo_error("internal SAA-4 decoupler dimension mismatch");
  }
  std::vector<std::vector<RationalChannel>> result;
  result.reserve(outputs);
  for (std::size_t output = 0; output < outputs; ++output) {
    std::vector<RationalChannel> row;
    row.reserve(inputs);
    for (std::size_t virtual_input = 0; virtual_input < inputs;
         ++virtual_input) {
      RationalChannel accumulated{{0}, {1}};
      for (std::size_t physical_input = 0; physical_input < inputs;
           ++physical_input) {
        accumulated = rational_add(
            accumulated,
            rational_scaled(to_rational(channels[output][physical_input]),
                            decoupler[physical_input][virtual_input]));
      }
      row.push_back(std::move(accumulated));
    }
    result.push_back(std::move(row));
  }
  return result;
}

[[nodiscard]] std::optional<std::complex<double>> evaluate_rational(
    const RationalChannel &channel, std::complex<double> point) {
  std::complex<double> numerator(0.0, 0.0);
  std::complex<double> denominator(0.0, 0.0);
  for (const auto &coefficient : channel.numerator) {
    numerator = numerator * point + coefficient.get_d();
  }
  for (const auto &coefficient : channel.denominator) {
    denominator = denominator * point + coefficient.get_d();
  }
  if (std::abs(denominator) <= 1e-14) {
    return std::nullopt;
  }
  return numerator / denominator;
}

[[nodiscard]] double coupling_energy_ratio(
    const std::vector<std::vector<RationalChannel>> &channels,
    std::complex<double> point) {
  const std::size_t outputs = channels.size();
  const std::size_t inputs = channels.front().size();
  if (outputs != inputs) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  double total = 0.0;
  double off = 0.0;
  for (std::size_t row = 0; row < outputs; ++row) {
    for (std::size_t column = 0; column < inputs; ++column) {
      const auto value = evaluate_rational(channels[row][column], point);
      if (!value) {
        return std::numeric_limits<double>::quiet_NaN();
      }
      const double energy = std::norm(*value);
      total += energy;
      if (row != column) {
        off += energy;
      }
    }
  }
  return total == 0.0 ? 0.0 : off / total;
}

[[nodiscard]] std::vector<ResidualCouplingSample> residual_samples(
    std::string_view domain,
    const std::vector<std::vector<RationalChannel>> &channels) {
  if (channels.size() != channels.front().size()) {
    return {};
  }
  std::vector<ResidualCouplingSample> result;
  if (domain == "CONTINUOUS") {
    for (const double frequency : continuous_frequencies) {
      result.push_back(
          {frequency,
           coupling_energy_ratio(channels, {0.0, frequency})});
    }
  } else {
    for (const double angle : discrete_angles) {
      result.push_back(
          {angle, coupling_energy_ratio(channels, std::polar(1.0, angle))});
    }
  }
  return result;
}

[[nodiscard]] Json rational_matrix_json(const RationalMatrix &matrix) {
  Json result = Json::array();
  for (const auto &row : matrix) {
    Json encoded_row = Json::array();
    for (const auto &value : row) {
      encoded_row.push_back(rational_json(value));
    }
    result.push_back(std::move(encoded_row));
  }
  return result;
}

[[nodiscard]] std::optional<StaticDecouplingResult> static_decoupling(
    const CanonicalDynamicsMatrix &channels,
    const std::optional<RationalMatrix> &inverse_steady_gain, bool exact,
    std::string_view domain) {
  if (!exact || !inverse_steady_gain) {
    return std::nullopt;
  }
  auto transformed = apply_static_decoupler(channels, *inverse_steady_gain);
  Json transformed_payload = Json::array();
  for (const auto &row : transformed) {
    Json encoded_row = Json::array();
    for (const auto &channel : row) {
      encoded_row.push_back(to_json(channel));
    }
    transformed_payload.push_back(std::move(encoded_row));
  }
  const Json payload =
      {{"claim_scope",
        "EXACT_DC_STATIC_DECOUPLING_IN_NORMALIZED_DEVIATION_COORDINATES"},
       {"decoupled_channels", transformed_payload},
       {"decoupler", rational_matrix_json(*inverse_steady_gain)},
       {"mimo_version", mimo_version},
       {"schema_version", 1}};
  auto samples = residual_samples(domain, transformed);
  return StaticDecouplingResult{
      .decoupler = *inverse_steady_gain,
      .decoupled_channels = std::move(transformed),
      .canonical_signature = contracts::sha256_json(payload),
      .residual_coupling_samples = std::move(samples)};
}

[[nodiscard]] Json index_array(
    const std::optional<std::vector<std::size_t>> &values) {
  if (!values) {
    return nullptr;
  }
  return *values;
}

} // namespace

MIMOTransferMatrix::MIMOTransferMatrix(std::string domain_value,
                                       TransferFunctionMatrix channels_value,
                                       Json metadata_value)
    : domain(normalized_domain(std::move(domain_value))),
      channels(std::move(channels_value)), metadata(std::move(metadata_value)) {
  if (channels.empty() || channels.front().empty()) {
    mimo_error("SAA-4 MIMO transfer matrix must be non-empty");
  }
  const std::size_t width = channels.front().size();
  if (std::any_of(channels.begin(), channels.end(), [width](const auto &row) {
        return row.size() != width;
      })) {
    mimo_error("SAA-4 MIMO transfer matrix must be rectangular");
  }
  if (channels.size() > max_mimo_outputs || width > max_mimo_inputs) {
    mimo_error("SAA-4 MIMO dimensions exceed 6 outputs x 6 inputs");
  }
  for (const auto &row : channels) {
    for (const auto &channel : row) {
      if (channel.domain != domain) {
        mimo_error("all SAA-4 channels must share the matrix domain");
      }
    }
  }
}

bool RationalChannel::zero() const noexcept {
  return std::all_of(numerator.begin(), numerator.end(),
                     [](const mpq_class &value) { return value == 0; });
}

Json to_json(const RationalChannel &value) {
  return {{"denominator", polynomial_json(value.denominator)},
          {"numerator", polynomial_json(value.numerator)}};
}

Json to_json(const StaticDecouplingResult &value) {
  Json channels = Json::array();
  for (const auto &row : value.decoupled_channels) {
    Json encoded_row = Json::array();
    for (const auto &channel : row) {
      encoded_row.push_back(to_json(channel));
    }
    channels.push_back(std::move(encoded_row));
  }
  Json samples = Json::array();
  for (const auto &sample : value.residual_coupling_samples) {
    samples.push_back(
        Json::array({sample.coordinate,
                     std::isfinite(sample.ratio) ? Json(sample.ratio)
                                                 : Json(nullptr)}));
  }
  return {{"canonical_signature", value.canonical_signature},
          {"decoupled_channels", channels},
          {"decoupler", rational_matrix_json(value.decoupler)},
          {"residual_coupling_samples", samples}};
}

std::optional<RationalMatrix>
invert_rational_matrix(const RationalMatrix &matrix) {
  return matrix_inverse(matrix);
}

RationalChannel scale_rational_channel(const RationalChannel &channel,
                                       const mpq_class &scalar) {
  return rational_scaled(channel, scalar);
}

RationalChannel add_rational_channels(const RationalChannel &first,
                                      const RationalChannel &second) {
  return rational_add(first, second);
}

Json to_json(const CanonicalMIMOCoupling &value) {
  Json channels = Json::array();
  for (const auto &row : value.channels) {
    Json encoded_row = Json::array();
    for (const auto &channel : row) {
      encoded_row.push_back(channel_payload(channel));
    }
    channels.push_back(std::move(encoded_row));
  }
  Json nonzero = Json::array();
  for (const auto &row : value.nonzero_pattern) {
    nonzero.push_back(row);
  }
  Json gains = Json::array();
  for (const auto &row : value.steady_gain) {
    Json encoded_row = Json::array();
    for (const auto &gain : row) {
      encoded_row.push_back(gain ? rational_json(*gain) : Json(nullptr));
    }
    gains.push_back(std::move(encoded_row));
  }
  return {
      {"audit_hash", value.audit_hash},
      {"canonical_input_permutation",
       index_array(value.canonical_input_permutation)},
      {"canonical_output_permutation",
       index_array(value.canonical_output_permutation)},
      {"channels", channels},
      {"coupling_strength", value.coupling_strength},
      {"domain", value.domain},
      {"dynamic_strength", value.dynamic_strength},
      {"exact_diagonal_input_permutation",
       index_array(value.exact_diagonal_input_permutation)},
      {"input_count", value.input_count},
      {"mimo_version", value.mimo_version_value},
      {"nonzero_pattern", nonzero},
      {"normalized_sample_interval",
       value.normalized_sample_interval
           ? rational_json(*value.normalized_sample_interval)
           : Json(nullptr)},
      {"ordered_signature", value.ordered_signature},
      {"output_count", value.output_count},
      {"permutation_decoupled", value.permutation_decoupled},
      {"permutation_invariant_signature",
       value.permutation_invariant_signature
           ? Json(*value.permutation_invariant_signature)
           : Json(nullptr)},
      {"permutation_strength", value.permutation_strength},
      {"preferred_rga_pairing", index_array(value.preferred_rga_pairing)},
      {"relative_gain_array",
       value.relative_gain_array ? rational_matrix_json(*value.relative_gain_array)
                                 : Json(nullptr)},
      {"rga_off_pairing_mass",
       value.rga_off_pairing_mass ? rational_json(*value.rga_off_pairing_mass)
                                  : Json(nullptr)},
      {"schema_version", value.schema_version},
      {"static_decoupling",
       value.static_decoupling ? to_json(*value.static_decoupling)
                               : Json(nullptr)},
      {"steady_gain", gains},
      {"variable", value.variable},
      {"warnings", value.warnings}};
}

CanonicalMIMOCoupling canonicalize_mimo_transfer_matrix(
    const MIMOTransferMatrix &transfer_matrix,
    const NormalizationContract &normalization,
    std::size_t permutation_budget) {
  const std::size_t outputs = transfer_matrix.channels.size();
  const std::size_t inputs = transfer_matrix.channels.front().size();
  if (permutation_budget < 1U) {
    mimo_error("max_port_permutations must be positive");
  }
  const auto bindings = validate_normalization(normalization, outputs, inputs);
  CanonicalDynamicsMatrix channels;
  channels.reserve(outputs);
  for (std::size_t output = 0; output < outputs; ++output) {
    std::vector<CanonicalLinearDynamics> row;
    row.reserve(inputs);
    for (std::size_t input = 0; input < inputs; ++input) {
      row.push_back(canonicalize_transfer_function(
          transfer_matrix.channels[output][input],
          channel_normalization(normalization, *bindings.inputs[input],
                                *bindings.outputs[output])));
    }
    channels.push_back(std::move(row));
  }

  for (const auto &row : channels) {
    for (const auto &channel : row) {
      if (channel.domain != transfer_matrix.domain) {
        mimo_error(
            "SAA-4 channel domains are inconsistent after canonicalization");
      }
    }
  }
  const auto normalized_sample_interval =
      channels.front().front().normalized_sample_interval;
  for (const auto &row : channels) {
    for (const auto &channel : row) {
      if (channel.normalized_sample_interval != normalized_sample_interval) {
        mimo_error(
            "all SAA-4 channels must share one normalized discrete sample interval");
      }
    }
  }
  const std::string variable = channels.front().front().variable;
  const bool exact =
      std::all_of(channels.begin(), channels.end(), [](const auto &row) {
        return std::all_of(row.begin(), row.end(), [](const auto &channel) {
          return channel.dynamic_strength == "EXACT_LINEAR_DYNAMICS";
        });
      });
  const std::string dynamic_strength =
      exact ? "EXACT_MIMO_LINEAR_DYNAMICS"
            : "APPROXIMATE_MIMO_LINEAR_DYNAMICS";
  const std::string coupling_strength =
      exact ? "EXACT_COUPLING_ANALYSIS" : "APPROXIMATE_COUPLING_ANALYSIS";
  std::vector<std::string> warnings;
  if (!exact) {
    warnings.push_back(
        "one or more SAA-4 channels are approximate; equality of matrix signatures does not prove exact MIMO equivalence");
  }
  std::vector<std::size_t> ordered_outputs(outputs);
  std::vector<std::size_t> ordered_inputs(inputs);
  std::iota(ordered_outputs.begin(), ordered_outputs.end(), 0U);
  std::iota(ordered_inputs.begin(), ordered_inputs.end(), 0U);
  const Json ordered_payload =
      {{"claim_scope", "NORMALIZED_ORDERED_MIMO_LINEAR_INPUT_OUTPUT_DYNAMICS"},
       {"domain", transfer_matrix.domain},
       {"dynamic_strength", dynamic_strength},
       {"inputs", inputs},
       {"matrix", matrix_payload(channels, ordered_outputs, ordered_inputs)},
       {"mimo_version", mimo_version},
       {"normalized_sample_interval",
        normalized_sample_interval ? rational_json(*normalized_sample_interval)
                                   : Json(nullptr)},
       {"outputs", outputs},
       {"schema_version", 1},
       {"variable", variable}};
  const std::string ordered_signature =
      contracts::sha256_json(ordered_payload);
  auto permutation = canonical_permutation(
      channels, transfer_matrix.domain, variable, normalized_sample_interval,
      dynamic_strength, permutation_budget);
  if (!permutation.signature) {
    warnings.push_back(
        "port permutation search exceeded the configured bound; no permutation-invariant equivalence signature was asserted");
  }

  BooleanMatrix nonzero_pattern;
  for (const auto &row : channels) {
    std::vector<bool> pattern_row;
    for (const auto &channel : row) {
      pattern_row.push_back(!zero_channel(channel));
    }
    nonzero_pattern.push_back(std::move(pattern_row));
  }
  auto diagonal = exact_diagonal_permutation(channels);

  OptionalRationalMatrix gain;
  for (const auto &row : channels) {
    std::vector<std::optional<mpq_class>> gain_row;
    for (const auto &channel : row) {
      gain_row.push_back(steady_gain(channel));
    }
    gain.push_back(std::move(gain_row));
  }
  auto rga = relative_gain_array(gain);
  std::optional<std::vector<std::size_t>> pairing;
  std::optional<mpq_class> rga_mass;
  if (rga.rga) {
    auto result = preferred_pairing(*rga.rga);
    pairing = std::move(result.pairing);
    rga_mass = std::move(result.off_pairing_mass);
  } else if (outputs == inputs) {
    warnings.push_back(
        "steady-state RGA unavailable because the normalized steady-gain matrix is singular or contains a pole at the evaluation point");
  }
  auto decoupling =
      static_decoupling(channels, rga.inverse, exact, transfer_matrix.domain);
  if (decoupling) {
    warnings.push_back(
        "static decoupler is a normalized deviation-coordinate transform that diagonalizes steady-state gain only; it is not a physical actuator map and does not imply dynamic decoupling at other frequencies");
  }
  const Json audit_payload =
      {{"mimo_version", mimo_version},
       {"normalization_contract_hash", normalization.contract_hash},
       {"normalization_signature", normalization.canonical_signature},
       {"ordered_signature", ordered_signature},
       {"permutation_signature",
        permutation.signature ? Json(*permutation.signature) : Json(nullptr)},
       {"permutation_strength", permutation.strength},
       {"permutations_considered", permutation.considered},
       {"schema_version", 1},
       {"source_metadata", transfer_matrix.metadata}};

  return {.schema_version = 1,
          .mimo_version_value = std::string(mimo_version),
          .domain = transfer_matrix.domain,
          .variable = variable,
          .output_count = outputs,
          .input_count = inputs,
          .channels = std::move(channels),
          .dynamic_strength = dynamic_strength,
          .coupling_strength = coupling_strength,
          .normalized_sample_interval = normalized_sample_interval,
          .ordered_signature = ordered_signature,
          .permutation_invariant_signature = std::move(permutation.signature),
          .permutation_strength = std::move(permutation.strength),
          .canonical_output_permutation = std::move(permutation.output),
          .canonical_input_permutation = std::move(permutation.input),
          .nonzero_pattern = std::move(nonzero_pattern),
          .permutation_decoupled = diagonal.has_value(),
          .exact_diagonal_input_permutation = std::move(diagonal),
          .steady_gain = std::move(gain),
          .relative_gain_array = std::move(rga.rga),
          .preferred_rga_pairing = std::move(pairing),
          .rga_off_pairing_mass = std::move(rga_mass),
          .static_decoupling = std::move(decoupling),
          .audit_hash = contracts::sha256_json(audit_payload),
          .warnings = std::move(warnings)};
}

std::string mimo_algorithm_signature(const CanonicalAlgorithmIR &structural_ir,
                                     const NormalizationContract &normalization,
                                     const CanonicalMIMOCoupling &mimo,
                                     bool ignore_port_order) {
  std::string dynamic_signature = mimo.ordered_signature;
  std::string claim_scope =
      "STRUCTURE_PLUS_NORMALIZED_ORDERED_MIMO_DYNAMICS";
  if (ignore_port_order) {
    if (!mimo.permutation_invariant_signature) {
      mimo_error(
          "SAA-4 permutation-invariant signature is unavailable under the current budget");
    }
    dynamic_signature = *mimo.permutation_invariant_signature;
    claim_scope =
        "STRUCTURE_PLUS_NORMALIZED_MIMO_DYNAMICS_UP_TO_PORT_PERMUTATION";
  }
  return contracts::sha256_json(
      {{"claim_scope", claim_scope},
       {"mimo_dynamic_signature", dynamic_signature},
       {"mimo_dynamic_strength", mimo.dynamic_strength},
       {"mimo_version", mimo.mimo_version_value},
       {"normalization_signature", normalization.canonical_signature},
       {"normalization_strength", normalization.normalization_strength},
       {"schema_version", 1},
       {"structural_hash", structural_ir.structural_hash},
       {"structural_strength", structural_ir.canonicalization_strength}});
}

} // namespace statewright::saa
