#include "statewright/saa/dynamics.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[noreturn]] void dynamics_error(std::string message) {
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

[[nodiscard]] std::string uppercase(std::string value) {
  value = trim(std::move(value));
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::toupper(character));
                 });
  return value;
}

[[nodiscard]] std::string normalized_domain(std::string value) {
  value = uppercase(std::move(value));
  if (value == "S" || value == "S_DOMAIN" || value == "CONTINUOUS_TIME") {
    value = "CONTINUOUS";
  } else if (value == "Z" || value == "Z_DOMAIN" ||
             value == "DISCRETE_TIME") {
    value = "DISCRETE";
  }
  if (value != "CONTINUOUS" && value != "DISCRETE") {
    dynamics_error("unsupported SAA-3 linear domain: " + value);
  }
  return value;
}

[[nodiscard]] std::string double_text(double value) {
  if (!std::isfinite(value)) {
    dynamics_error("SAA-3 numeric coefficient must be finite");
  }
  if (value == 0.0) {
    value = 0.0;
  }
  std::array<char, 128> buffer{};
  const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                    value, std::chars_format::general);
  if (result.ec != std::errc()) {
    dynamics_error("cannot format SAA-3 numeric coefficient");
  }
  std::string text(buffer.data(), result.ptr);
  if (text.find_first_of(".eE") == std::string::npos) {
    text += ".0";
  }
  return text;
}

[[nodiscard]] mpq_class decimal_rational(std::string text) {
  text = trim(std::move(text));
  if (text.empty()) {
    dynamics_error("exact numeric string must be non-empty");
  }
  const auto slash = text.find('/');
  if (slash != std::string::npos) {
    try {
      mpq_class result(text);
      result.canonicalize();
      return result;
    } catch (const std::invalid_argument &) {
      dynamics_error("invalid exact rational: " + text);
    }
  }

  bool negative = false;
  std::size_t position = 0;
  if (text[position] == '+' || text[position] == '-') {
    negative = text[position] == '-';
    ++position;
  }
  const auto exponent_at = text.find_first_of("eE", position);
  std::string mantissa = text.substr(position, exponent_at - position);
  long exponent = 0;
  if (exponent_at != std::string::npos) {
    const std::string exponent_text = text.substr(exponent_at + 1U);
    try {
      std::size_t consumed = 0;
      exponent = std::stol(exponent_text, &consumed);
      if (consumed != exponent_text.size()) {
        dynamics_error("invalid exact decimal exponent: " + text);
      }
    } catch (const std::exception &) {
      dynamics_error("invalid exact decimal exponent: " + text);
    }
  }
  const auto point = mantissa.find('.');
  std::size_t fractional_digits = 0;
  if (point != std::string::npos) {
    fractional_digits = mantissa.size() - point - 1U;
    mantissa.erase(point, 1U);
  }
  if (mantissa.empty() ||
      !std::all_of(mantissa.begin(), mantissa.end(), [](unsigned char value) {
        return std::isdigit(value) != 0;
      })) {
    dynamics_error("invalid exact decimal: " + text);
  }
  mpz_class numerator(mantissa);
  if (negative) {
    numerator = -numerator;
  }
  mpz_class denominator = 1;
  for (std::size_t index = 0; index < fractional_digits; ++index) {
    denominator *= 10;
  }
  if (exponent > 0) {
    for (long index = 0; index < exponent; ++index) {
      numerator *= 10;
    }
  } else {
    for (long index = 0; index > exponent; --index) {
      denominator *= 10;
    }
  }
  mpq_class result(numerator, denominator);
  result.canonicalize();
  return result;
}

[[nodiscard]] Json json_integer(const mpz_class &value) {
  return contracts::parse_json(value.get_str());
}

[[nodiscard]] Json fraction_payload(const mpq_class &value) {
  return Json::array({json_integer(value.get_num()),
                      json_integer(value.get_den())});
}

[[nodiscard]] bool zero_polynomial(const RationalPolynomial &values) {
  return std::all_of(values.begin(), values.end(), [](const mpq_class &value) {
    return value == 0;
  });
}

[[nodiscard]] RationalPolynomial trim_descending(RationalPolynomial values) {
  if (values.empty()) {
    return {0};
  }
  auto first = values.begin();
  while (first + 1 != values.end() && *first == 0) {
    ++first;
  }
  values.erase(values.begin(), first);
  return values;
}

[[nodiscard]] RationalPolynomial trim_ascending(RationalPolynomial values) {
  if (values.empty()) {
    return {0};
  }
  while (values.size() > 1U && values.back() == 0) {
    values.pop_back();
  }
  return values;
}

[[nodiscard]] std::pair<RationalPolynomial, RationalPolynomial>
polynomial_divmod(RationalPolynomial dividend,
                  RationalPolynomial divisor) {
  dividend = trim_ascending(std::move(dividend));
  divisor = trim_ascending(std::move(divisor));
  if (zero_polynomial(divisor)) {
    dynamics_error("internal SAA-3 polynomial division by zero");
  }
  if (dividend.size() < divisor.size()) {
    return {{0}, dividend};
  }
  RationalPolynomial quotient(dividend.size() - divisor.size() + 1U, 0);
  while (dividend.size() >= divisor.size() &&
         !zero_polynomial(dividend)) {
    const std::size_t shift = dividend.size() - divisor.size();
    const mpq_class factor = dividend.back() / divisor.back();
    quotient[shift] += factor;
    for (std::size_t index = 0; index < divisor.size(); ++index) {
      dividend[index + shift] -= factor * divisor[index];
    }
    dividend = trim_ascending(std::move(dividend));
  }
  return {trim_ascending(std::move(quotient)),
          trim_ascending(std::move(dividend))};
}

[[nodiscard]] RationalPolynomial polynomial_gcd(RationalPolynomial left,
                                                RationalPolynomial right) {
  left = trim_ascending(std::move(left));
  right = trim_ascending(std::move(right));
  while (!zero_polynomial(right)) {
    auto [ignored, remainder] = polynomial_divmod(left, right);
    static_cast<void>(ignored);
    left = std::move(right);
    right = std::move(remainder);
  }
  if (zero_polynomial(left)) {
    return {1};
  }
  const mpq_class lead = left.back();
  for (auto &value : left) {
    value /= lead;
  }
  return left;
}

[[nodiscard]] std::pair<RationalPolynomial, RationalPolynomial>
normalize_denominator(RationalPolynomial numerator,
                      RationalPolynomial denominator) {
  numerator = trim_descending(std::move(numerator));
  denominator = trim_descending(std::move(denominator));
  if (zero_polynomial(denominator)) {
    dynamics_error("SAA-3 transfer denominator cannot be zero");
  }
  if (zero_polynomial(numerator)) {
    return {{0}, {1}};
  }
  const mpq_class lead = denominator.front();
  for (auto &value : numerator) {
    value /= lead;
  }
  for (auto &value : denominator) {
    value /= lead;
  }
  return {std::move(numerator), std::move(denominator)};
}

[[nodiscard]] ReducedTransfer reduce_exact_impl(RationalPolynomial numerator,
                                                RationalPolynomial denominator) {
  numerator = trim_descending(std::move(numerator));
  denominator = trim_descending(std::move(denominator));
  if (zero_polynomial(denominator)) {
    dynamics_error("SAA-3 transfer denominator cannot be zero");
  }
  if (zero_polynomial(numerator)) {
    return {{0}, {1}, {"ZERO_TRANSFER_CANONICALIZATION"}};
  }
  RationalPolynomial numerator_ascending(numerator.rbegin(), numerator.rend());
  RationalPolynomial denominator_ascending(denominator.rbegin(),
                                           denominator.rend());
  const auto gcd = polynomial_gcd(numerator_ascending, denominator_ascending);
  std::vector<std::string> reductions;
  if (gcd.size() > 1U) {
    auto [numerator_quotient, numerator_remainder] =
        polynomial_divmod(numerator_ascending, gcd);
    auto [denominator_quotient, denominator_remainder] =
        polynomial_divmod(denominator_ascending, gcd);
    if (!zero_polynomial(numerator_remainder) ||
        !zero_polynomial(denominator_remainder)) {
      dynamics_error("internal SAA-3 exact factor cancellation failed");
    }
    numerator.assign(numerator_quotient.rbegin(), numerator_quotient.rend());
    denominator.assign(denominator_quotient.rbegin(),
                       denominator_quotient.rend());
    reductions.push_back("EXACT_COMMON_FACTOR_CANCELLATION");
  }
  auto normalized =
      normalize_denominator(std::move(numerator), std::move(denominator));
  reductions.push_back("MONIC_DENOMINATOR");
  return {std::move(normalized.first), std::move(normalized.second),
          std::move(reductions)};
}

[[nodiscard]] RationalPolynomial
scale_time_polynomial(const RationalPolynomial &coefficients,
                      const mpq_class &characteristic_time) {
  const std::size_t degree = coefficients.size() - 1U;
  RationalPolynomial result;
  result.reserve(coefficients.size());
  for (std::size_t index = 0; index < coefficients.size(); ++index) {
    mpq_class scale = 1;
    for (std::size_t power = 0; power < degree - index; ++power) {
      scale *= characteristic_time;
    }
    result.push_back(coefficients[index] / scale);
  }
  return result;
}

struct ParsedPolynomial final {
  RationalPolynomial values;
  bool exact = true;
  Json audit = Json::array();
};

[[nodiscard]] ParsedPolynomial parse_polynomial(const Polynomial &values,
                                                std::string_view label) {
  if (values.empty()) {
    dynamics_error(std::string(label) +
                   " must be a non-empty coefficient sequence");
  }
  ParsedPolynomial result;
  for (const auto &coefficient : values) {
    result.values.push_back(coefficient.value());
    result.exact = result.exact && coefficient.exact();
    result.audit.push_back(
        {{"inferred_strength", coefficient.exact() ? "EXACT" : "APPROXIMATE"},
         {"source", coefficient.source()}});
  }
  result.values = trim_descending(std::move(result.values));
  if (result.values.size() - 1U > max_polynomial_degree) {
    dynamics_error(std::string(label) + " degree exceeds SAA-3 limit");
  }
  return result;
}

struct NormalizationScales final {
  mpq_class input_width;
  mpq_class output_width;
  mpq_class characteristic_time;
  bool exact = false;
};

[[nodiscard]] NormalizationScales
normalization_scales(const NormalizationContract &contract) {
  std::vector<const NormalizationBinding *> inputs;
  std::vector<const NormalizationBinding *> outputs;
  for (const auto &binding : contract.bindings) {
    if (binding.role == "INPUT") {
      inputs.push_back(&binding);
    } else if (binding.role == "OUTPUT") {
      outputs.push_back(&binding);
    }
  }
  if (inputs.size() != 1U || outputs.size() != 1U) {
    dynamics_error("SAA-3 v1 supports SISO normalization contracts only");
  }
  if (inputs.front()->canonical_data_type() != "CONTINUOUS_SCALAR" ||
      outputs.front()->canonical_data_type() != "CONTINUOUS_SCALAR") {
    dynamics_error("SAA-3 v1 requires continuous scalar coordinates");
  }
  if (!contract.time) {
    dynamics_error("SAA-3 requires SAA-2 characteristic time");
  }
  return {.input_width = decimal_rational(double_text(inputs.front()->bound.width())),
          .output_width =
              decimal_rational(double_text(outputs.front()->bound.width())),
          .characteristic_time =
              decimal_rational(double_text(contract.time->characteristic_time)),
          .exact =
              contract.normalization_strength == "EXACT_NORMALIZATION"};
}

[[nodiscard]] CanonicalLinearDynamics canonicalize_fraction_transfer(
    std::string domain, RationalPolynomial numerator,
    RationalPolynomial denominator, bool coefficient_exact,
    std::optional<mpq_class> sample_period, bool sample_period_exact,
    const NormalizationContract &normalization, std::string source_kind,
    Json source_audit) {
  domain = normalized_domain(std::move(domain));
  numerator = trim_descending(std::move(numerator));
  denominator = trim_descending(std::move(denominator));
  if (zero_polynomial(denominator)) {
    dynamics_error("SAA-3 transfer denominator cannot be zero");
  }
  if (numerator.size() - 1U > max_polynomial_degree ||
      denominator.size() - 1U > max_polynomial_degree) {
    dynamics_error("SAA-3 transfer degree exceeds limit");
  }
  const auto scales = normalization_scales(normalization);
  const mpq_class interface_gain = scales.input_width / scales.output_width;
  for (auto &value : numerator) {
    value *= interface_gain;
  }

  std::optional<mpq_class> normalized_sample_interval;
  std::string variable;
  bool sample_exact = true;
  if (domain == "CONTINUOUS") {
    if (sample_period) {
      dynamics_error("continuous SAA-3 transfer must not declare sample_period");
    }
    numerator = scale_time_polynomial(numerator, scales.characteristic_time);
    denominator =
        scale_time_polynomial(denominator, scales.characteristic_time);
    variable = "SIGMA";
  } else {
    if (!sample_period || *sample_period <= 0) {
      dynamics_error("discrete SAA-3 sample_period must be positive");
    }
    normalized_sample_interval = *sample_period / scales.characteristic_time;
    variable = "Z";
    sample_exact = sample_period_exact;
  }
  const bool exact = coefficient_exact && scales.exact && sample_exact;
  ReducedTransfer reduced;
  std::vector<std::string> warnings;
  if (zero_polynomial(numerator)) {
    reduced = {{0}, {1}, {"ZERO_TRANSFER_CANONICALIZATION"}};
  } else if (exact) {
    reduced = reduce_exact_impl(std::move(numerator), std::move(denominator));
  } else {
    auto normalized =
        normalize_denominator(std::move(numerator), std::move(denominator));
    reduced = {std::move(normalized.first), std::move(normalized.second),
               {"MONIC_DENOMINATOR",
                "NO_APPROXIMATE_POLE_ZERO_CANCELLATION"}};
    warnings.push_back(
        "approximate SAA-3 dynamics are not pole-zero cancelled; near "
        "cancellation cannot establish exact dynamic equivalence");
  }

  const int denominator_degree =
      static_cast<int>(reduced.denominator.size() - 1U);
  std::optional<int> relative_degree;
  bool proper = true;
  if (!zero_polynomial(reduced.numerator)) {
    const int numerator_degree =
        static_cast<int>(reduced.numerator.size() - 1U);
    relative_degree = denominator_degree - numerator_degree;
    proper = numerator_degree <= denominator_degree;
  }
  if (!proper) {
    warnings.push_back(
        "improper transfer form retained; SAA-3 signature is algebraic and "
        "does not claim a standard proper finite-dimensional realization");
  }
  const std::string strength = exact ? "EXACT_LINEAR_DYNAMICS"
                                     : "APPROXIMATE_LINEAR_DYNAMICS";
  const Json audit =
      {{"coefficient_strength", coefficient_exact ? "EXACT" : "APPROXIMATE"},
       {"dynamics_version", dynamics_version},
       {"normalization_contract_hash", normalization.contract_hash},
       {"normalization_signature", normalization.canonical_signature},
       {"sample_period_strength",
        domain == "CONTINUOUS"
            ? Json(nullptr)
            : Json(sample_period_exact ? "EXACT" : "APPROXIMATE")},
       {"schema_version", 1},
       {"source", std::move(source_audit)},
       {"source_kind", std::move(source_kind)}};
  const Json canonical =
      {{"claim_scope", "NORMALIZED_SISO_LINEAR_INPUT_OUTPUT_DYNAMICS"},
       {"denominator", polynomial_json(reduced.denominator)},
       {"domain", domain},
       {"dynamic_order", denominator_degree},
       {"dynamic_strength", strength},
       {"dynamics_version", dynamics_version},
       {"linearization_coordinate", "DEVIATION_DYNAMICS"},
       {"normalization_signature", normalization.canonical_signature},
       {"normalized_sample_interval",
        normalized_sample_interval ? fraction_payload(*normalized_sample_interval)
                                   : Json(nullptr)},
       {"numerator", polynomial_json(reduced.numerator)},
       {"proper", proper},
       {"reduction_policy",
        exact ? "EXACT_RATIONAL_POLYNOMIAL_REDUCTION"
              : "MONIC_ONLY_NO_APPROXIMATE_FACTOR_CANCELLATION"},
       {"relative_degree", relative_degree ? Json(*relative_degree) : Json(nullptr)},
       {"schema_version", 1},
       {"variable", variable}};
  return {.schema_version = 1,
          .dynamics_version_value = std::string(dynamics_version),
          .domain = std::move(domain),
          .variable = std::move(variable),
          .numerator = std::move(reduced.numerator),
          .denominator = std::move(reduced.denominator),
          .dynamic_order = denominator_degree,
          .relative_degree = relative_degree,
          .proper = proper,
          .normalized_sample_interval = normalized_sample_interval,
          .dynamic_strength = strength,
          .normalization_signature = normalization.canonical_signature,
          .audit_hash = contracts::sha256_json(audit),
          .canonical_signature = contracts::sha256_json(canonical),
          .reductions = std::move(reduced.reductions),
          .warnings = std::move(warnings)};
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
  if (left.empty() || right.empty() || left.front().size() != right.size()) {
    dynamics_error("internal SAA-3 matrix dimension mismatch");
  }
  RationalMatrix result(left.size(),
                        std::vector<mpq_class>(right.front().size(), 0));
  for (std::size_t row = 0; row < left.size(); ++row) {
    for (std::size_t column = 0; column < right.front().size(); ++column) {
      for (std::size_t inner = 0; inner < right.size(); ++inner) {
        result[row][column] += left[row][inner] * right[inner][column];
      }
    }
  }
  return result;
}

[[nodiscard]] RationalMatrix add_identity(RationalMatrix matrix,
                                          const mpq_class &scalar) {
  for (std::size_t index = 0; index < matrix.size(); ++index) {
    matrix[index][index] += scalar;
  }
  return matrix;
}

[[nodiscard]] mpq_class trace(const RationalMatrix &matrix) {
  mpq_class result = 0;
  for (std::size_t index = 0; index < matrix.size(); ++index) {
    result += matrix[index][index];
  }
  return result;
}

[[nodiscard]] mpq_class row_matrix_column(
    const std::vector<mpq_class> &row, const RationalMatrix &matrix,
    const std::vector<mpq_class> &column) {
  mpq_class result = 0;
  for (std::size_t left = 0; left < row.size(); ++left) {
    for (std::size_t right = 0; right < column.size(); ++right) {
      result += row[left] * matrix[left][right] * column[right];
    }
  }
  return result;
}

[[nodiscard]] std::pair<RationalPolynomial, RationalPolynomial>
state_space_transfer(const RationalMatrix &a, const std::vector<mpq_class> &b,
                     const std::vector<mpq_class> &c,
                     const mpq_class &d) {
  const std::size_t size = a.size();
  RationalMatrix previous = identity(size);
  std::vector<RationalMatrix> adjugate = {previous};
  RationalPolynomial denominator = {1};
  for (std::size_t order = 1; order <= size; ++order) {
    auto product = multiply(a, previous);
    mpq_class coefficient = -trace(product) / static_cast<long>(order);
    denominator.push_back(coefficient);
    if (order < size) {
      previous = add_identity(std::move(product), coefficient);
      adjugate.push_back(previous);
    }
  }
  RationalPolynomial numerator;
  numerator.reserve(denominator.size());
  for (const auto &coefficient : denominator) {
    numerator.push_back(d * coefficient);
  }
  for (std::size_t index = 0; index < adjugate.size(); ++index) {
    numerator[index + 1U] += row_matrix_column(c, adjugate[index], b);
  }
  return {std::move(numerator), std::move(denominator)};
}

struct ParsedMatrix final {
  RationalMatrix values;
  bool exact = true;
  Json audit = Json::array();
};

[[nodiscard]] ParsedMatrix parse_matrix(const CoefficientMatrix &matrix,
                                        std::string_view label) {
  if (matrix.empty() || matrix.front().empty()) {
    dynamics_error(std::string(label) + " must be a non-empty matrix");
  }
  ParsedMatrix result;
  const std::size_t width = matrix.front().size();
  for (const auto &row : matrix) {
    if (row.size() != width) {
      dynamics_error(std::string(label) + " rows must have equal width");
    }
    std::vector<mpq_class> parsed_row;
    Json audit_row = Json::array();
    for (const auto &coefficient : row) {
      parsed_row.push_back(coefficient.value());
      result.exact = result.exact && coefficient.exact();
      audit_row.push_back(
          {{"inferred_strength",
            coefficient.exact() ? "EXACT" : "APPROXIMATE"},
           {"source", coefficient.source()}});
    }
    result.values.push_back(std::move(parsed_row));
    result.audit.push_back(std::move(audit_row));
  }
  return result;
}

struct ParsedVector final {
  std::vector<mpq_class> values;
  bool exact = true;
  Json audit = Json::array();
};

[[nodiscard]] ParsedVector parse_vector(const CoefficientMatrix &matrix,
                                        std::size_t length, bool allow_column,
                                        bool allow_row, std::string_view label) {
  std::vector<NumericCoefficient> flattened;
  if (matrix.size() == 1U && matrix.front().size() == length && allow_row) {
    flattened = matrix.front();
  } else if (matrix.size() == length && allow_column) {
    for (const auto &row : matrix) {
      if (row.size() != 1U) {
        dynamics_error(std::string(label) + " must be a SISO vector");
      }
      flattened.push_back(row.front());
    }
  } else {
    dynamics_error(std::string(label) + " must be a SISO vector");
  }
  ParsedVector result;
  for (const auto &coefficient : flattened) {
    result.values.push_back(coefficient.value());
    result.exact = result.exact && coefficient.exact();
    result.audit.push_back(
        {{"inferred_strength", coefficient.exact() ? "EXACT" : "APPROXIMATE"},
         {"source", coefficient.source()}});
  }
  return result;
}

} // namespace

NumericCoefficient::NumericCoefficient(int value)
    : NumericCoefficient(static_cast<long long>(value)) {}

NumericCoefficient::NumericCoefficient(long value)
    : NumericCoefficient(static_cast<long long>(value)) {}

NumericCoefficient::NumericCoefficient(long long value)
    : value_(decimal_rational(std::to_string(value))),
      source_(std::to_string(value)) {}

NumericCoefficient::NumericCoefficient(double value)
    : value_(decimal_rational(double_text(value))), exact_(false),
      source_(double_text(value)) {}

NumericCoefficient::NumericCoefficient(const char *value)
    : NumericCoefficient(std::string(value == nullptr ? "" : value)) {}

NumericCoefficient::NumericCoefficient(std::string value)
    : value_(decimal_rational(value)), source_(trim(std::move(value))) {}

NumericCoefficient::NumericCoefficient(mpq_class value)
    : value_(std::move(value)),
      source_(value_.get_num().get_str() + "/" + value_.get_den().get_str()) {
  value_.canonicalize();
}

const mpq_class &NumericCoefficient::value() const noexcept { return value_; }

bool NumericCoefficient::exact() const noexcept { return exact_; }

const std::string &NumericCoefficient::source() const noexcept { return source_; }

LinearTransferFunction::LinearTransferFunction(
    std::string domain_value, Polynomial numerator_value,
    Polynomial denominator_value,
    std::optional<NumericCoefficient> sample_period_value, Json metadata_value)
    : domain(normalized_domain(std::move(domain_value))),
      numerator(std::move(numerator_value)),
      denominator(std::move(denominator_value)),
      sample_period(std::move(sample_period_value)),
      metadata(std::move(metadata_value)) {}

LinearStateSpace::LinearStateSpace(
    std::string domain_value, CoefficientMatrix a_value,
    CoefficientMatrix b_value, CoefficientMatrix c_value,
    NumericCoefficient d_value,
    std::optional<NumericCoefficient> sample_period_value, Json metadata_value)
    : domain(normalized_domain(std::move(domain_value))), a(std::move(a_value)),
      b(std::move(b_value)), c(std::move(c_value)), d(std::move(d_value)),
      sample_period(std::move(sample_period_value)),
      metadata(std::move(metadata_value)) {}

Json rational_json(const mpq_class &value) { return fraction_payload(value); }

Json polynomial_json(const RationalPolynomial &value) {
  Json result = Json::array();
  for (const auto &coefficient : value) {
    result.push_back(fraction_payload(coefficient));
  }
  return result;
}

ReducedTransfer reduce_exact_transfer(RationalPolynomial numerator,
                                      RationalPolynomial denominator) {
  return reduce_exact_impl(std::move(numerator), std::move(denominator));
}

Json to_json(const CanonicalLinearDynamics &value) {
  return {{"audit_hash", value.audit_hash},
          {"canonical_signature", value.canonical_signature},
          {"denominator", polynomial_json(value.denominator)},
          {"domain", value.domain},
          {"dynamic_order", value.dynamic_order},
          {"dynamic_strength", value.dynamic_strength},
          {"dynamics_version", value.dynamics_version_value},
          {"normalization_signature", value.normalization_signature},
          {"normalized_sample_interval",
           value.normalized_sample_interval
               ? rational_json(*value.normalized_sample_interval)
               : Json(nullptr)},
          {"numerator", polynomial_json(value.numerator)},
          {"proper", value.proper},
          {"reductions", value.reductions},
          {"relative_degree",
           value.relative_degree ? Json(*value.relative_degree) : Json(nullptr)},
          {"schema_version", value.schema_version},
          {"variable", value.variable},
          {"warnings", value.warnings}};
}

CanonicalLinearDynamics canonicalize_transfer_function(
    const LinearTransferFunction &transfer,
    const NormalizationContract &normalization) {
  const auto numerator =
      parse_polynomial(transfer.numerator, "transfer numerator");
  const auto denominator =
      parse_polynomial(transfer.denominator, "transfer denominator");
  if (zero_polynomial(denominator.values)) {
    dynamics_error("SAA-3 transfer denominator cannot be zero");
  }
  std::optional<mpq_class> sample_period;
  bool sample_period_exact = true;
  Json sample_period_source = nullptr;
  if (transfer.domain == "DISCRETE") {
    if (!transfer.sample_period) {
      dynamics_error("discrete SAA-3 transfer requires sample_period");
    }
    sample_period = transfer.sample_period->value();
    sample_period_exact = transfer.sample_period->exact();
    sample_period_source = transfer.sample_period->source();
  } else if (transfer.sample_period) {
    dynamics_error("continuous SAA-3 transfer must not declare sample_period");
  }
  return canonicalize_fraction_transfer(
      transfer.domain, numerator.values, denominator.values,
      numerator.exact && denominator.exact, sample_period, sample_period_exact,
      normalization, "TRANSFER_FUNCTION",
      {{"denominator", denominator.audit},
       {"domain", transfer.domain},
       {"numerator", numerator.audit},
       {"sample_period", sample_period_source}});
}

CanonicalLinearDynamics canonicalize_state_space(
    const LinearStateSpace &state_space,
    const NormalizationContract &normalization) {
  const auto a = parse_matrix(state_space.a, "A");
  const std::size_t size = a.values.size();
  if (size < 1U || size > max_state_order) {
    dynamics_error("SAA-3 state order is outside configured bounds");
  }
  if (std::any_of(a.values.begin(), a.values.end(),
                  [size](const auto &row) { return row.size() != size; })) {
    dynamics_error("SAA-3 A matrix must be square");
  }
  const auto b = parse_vector(state_space.b, size, true, false, "B");
  const auto c = parse_vector(state_space.c, size, false, true, "C");
  std::optional<mpq_class> sample_period;
  bool sample_period_exact = true;
  Json sample_period_source = nullptr;
  if (state_space.domain == "DISCRETE") {
    if (!state_space.sample_period) {
      dynamics_error("discrete SAA-3 state space requires sample_period");
    }
    sample_period = state_space.sample_period->value();
    sample_period_exact = state_space.sample_period->exact();
    sample_period_source = state_space.sample_period->source();
  } else if (state_space.sample_period) {
    dynamics_error(
        "continuous SAA-3 state space must not declare sample_period");
  }
  auto [numerator, denominator] =
      state_space_transfer(a.values, b.values, c.values, state_space.d.value());
  return canonicalize_fraction_transfer(
      state_space.domain, std::move(numerator), std::move(denominator),
      a.exact && b.exact && c.exact && state_space.d.exact(), sample_period,
      sample_period_exact, normalization, "STATE_SPACE",
      {{"A", a.audit},
       {"B", b.audit},
       {"C", c.audit},
       {"D",
        {{"inferred_strength",
          state_space.d.exact() ? "EXACT" : "APPROXIMATE"},
         {"source", state_space.d.source()}}},
       {"domain", state_space.domain},
       {"sample_period", sample_period_source}});
}

std::string dynamic_algorithm_signature(
    const CanonicalAlgorithmIR &structural_ir,
    const NormalizationContract &normalization,
    const CanonicalLinearDynamics &dynamics) {
  if (dynamics.normalization_signature != normalization.canonical_signature) {
    dynamics_error(
        "SAA-3 dynamics were canonicalized against a different normalization "
        "contract");
  }
  return contracts::sha256_json(
      {{"canonicalizer_version", structural_ir.canonicalizer_version_value},
       {"claim_scope", "STRUCTURE_PLUS_NORMALIZED_LINEAR_IO_DYNAMICS"},
       {"dynamic_signature", dynamics.canonical_signature},
       {"dynamic_strength", dynamics.dynamic_strength},
       {"dynamics_version", dynamics.dynamics_version_value},
       {"normalization_signature", normalization.canonical_signature},
       {"normalization_strength", normalization.normalization_strength},
       {"normalizer_version", normalization.normalizer_version_value},
       {"schema_version", 1},
       {"structural_hash", structural_ir.structural_hash},
       {"structural_strength", structural_ir.canonicalization_strength}});
}

} // namespace statewright::saa
