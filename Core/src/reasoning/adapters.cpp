#include "statewright/reasoning/adapters.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <gmpxx.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace statewright::reasoning {
namespace {

using Decimal = mpq_class;
using ExpressionValue = std::variant<Decimal, bool>;
using Monomial = std::vector<std::pair<std::string, int>>;
using PolynomialTerms = std::map<Monomial, Decimal>;

constexpr std::size_t maximum_expression_length = 4'096;
constexpr int maximum_parse_depth = 128;
constexpr long maximum_decimal_exponent = 10'000;

[[noreturn]] void policy_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied, std::move(message));
}

[[nodiscard]] std::string trim(std::string value) {
  const auto first = std::ranges::find_if_not(value, [](unsigned char byte) {
    return std::isspace(byte) != 0;
  });
  const auto last = std::find_if_not(value.rbegin(), value.rend(),
                                     [](unsigned char byte) {
                                       return std::isspace(byte) != 0;
                                     })
                        .base();
  if (first >= last) {
    return {};
  }
  return std::string(first, last);
}

[[nodiscard]] mpz_class power_of_ten(unsigned long exponent) {
  mpz_class result;
  mpz_ui_pow_ui(result.get_mpz_t(), 10, exponent);
  return result;
}

[[nodiscard]] Decimal parse_decimal(std::string value) {
  value = trim(std::move(value));
  if (value.empty()) {
    throw std::invalid_argument("empty decimal value");
  }

  std::size_t position = 0;
  bool negative = false;
  if (value[position] == '+' || value[position] == '-') {
    negative = value[position] == '-';
    ++position;
  }

  std::string digits;
  std::size_t fractional_digits = 0;
  bool decimal_point_seen = false;
  bool digit_seen = false;
  while (position < value.size()) {
    const char character = value[position];
    if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
      digits.push_back(character);
      digit_seen = true;
      if (decimal_point_seen) {
        ++fractional_digits;
      }
      ++position;
      continue;
    }
    if (character == '.' && !decimal_point_seen) {
      decimal_point_seen = true;
      ++position;
      continue;
    }
    break;
  }
  if (!digit_seen) {
    throw std::invalid_argument("invalid decimal value");
  }

  long exponent = 0;
  if (position < value.size() &&
      (value[position] == 'e' || value[position] == 'E')) {
    ++position;
    bool exponent_negative = false;
    if (position < value.size() &&
        (value[position] == '+' || value[position] == '-')) {
      exponent_negative = value[position] == '-';
      ++position;
    }
    if (position >= value.size() ||
        std::isdigit(static_cast<unsigned char>(value[position])) == 0) {
      throw std::invalid_argument("invalid decimal exponent");
    }
    while (position < value.size() &&
           std::isdigit(static_cast<unsigned char>(value[position])) != 0) {
      const int digit = value[position] - '0';
      if (exponent > (maximum_decimal_exponent - digit) / 10) {
        throw std::invalid_argument("decimal exponent exceeds bound");
      }
      exponent = exponent * 10 + digit;
      ++position;
    }
    if (exponent_negative) {
      exponent = -exponent;
    }
  }
  if (position != value.size()) {
    throw std::invalid_argument("invalid decimal value");
  }

  const long scale = static_cast<long>(fractional_digits) - exponent;
  if (scale > maximum_decimal_exponent || scale < -maximum_decimal_exponent) {
    throw std::invalid_argument("decimal scale exceeds bound");
  }
  mpz_class numerator(digits);
  if (negative) {
    numerator = -numerator;
  }
  mpz_class denominator = 1;
  if (scale >= 0) {
    denominator = power_of_ten(static_cast<unsigned long>(scale));
  } else {
    numerator *= power_of_ten(static_cast<unsigned long>(-scale));
  }
  Decimal result(numerator, denominator);
  result.canonicalize();
  return result;
}

[[nodiscard]] std::size_t decimal_digits(const mpz_class &value) {
  return value == 0 ? 1 : value.get_str().size();
}

[[nodiscard]] mpz_class round_half_even(const mpz_class &numerator,
                                        const mpz_class &denominator) {
  mpz_class quotient;
  mpz_class remainder;
  mpz_fdiv_qr(quotient.get_mpz_t(), remainder.get_mpz_t(),
              numerator.get_mpz_t(), denominator.get_mpz_t());
  const mpz_class doubled = remainder * 2;
  if (doubled > denominator ||
      (doubled == denominator && mpz_odd_p(quotient.get_mpz_t()) != 0)) {
    ++quotient;
  }
  return quotient;
}

[[nodiscard]] long adjusted_decimal_exponent(const Decimal &absolute_value) {
  const mpz_class numerator = absolute_value.get_num();
  const mpz_class denominator = absolute_value.get_den();
  if (numerator >= denominator) {
    const mpz_class integer = numerator / denominator;
    return static_cast<long>(decimal_digits(integer)) - 1;
  }
  mpz_class scaled = numerator;
  long places = 0;
  while (scaled < denominator) {
    if (places >= maximum_decimal_exponent) {
      throw std::invalid_argument("decimal magnitude exceeds bound");
    }
    scaled *= 10;
    ++places;
  }
  return -places;
}

[[nodiscard]] std::string render_decimal_coefficient(mpz_class coefficient,
                                                     long exponent) {
  if (coefficient == 0) {
    return "0";
  }
  std::string digits = coefficient.get_str();
  const long adjusted = static_cast<long>(digits.size()) - 1 + exponent;
  if (exponent <= 0 && adjusted >= -6) {
    const long point = static_cast<long>(digits.size()) + exponent;
    if (point >= static_cast<long>(digits.size())) {
      digits.append(static_cast<std::size_t>(point) - digits.size(), '0');
    } else if (point > 0) {
      digits.insert(static_cast<std::size_t>(point), 1, '.');
    } else {
      digits.insert(0, static_cast<std::size_t>(-point), '0');
      digits.insert(0, "0.");
    }
    return digits;
  }

  std::string result(1, digits.front());
  if (digits.size() > 1) {
    result.push_back('.');
    result.append(digits.substr(1));
  }
  result.push_back('E');
  if (adjusted >= 0) {
    result.push_back('+');
  }
  result.append(std::to_string(adjusted));
  return result;
}

[[nodiscard]] std::string format_decimal(const Decimal &value,
                                         int precision = 50) {
  if (value == 0) {
    return "0";
  }
  const bool negative = value < 0;
  Decimal absolute_value = negative ? -value : value;
  const long adjusted = adjusted_decimal_exponent(absolute_value);
  const int bounded_precision = std::clamp(precision, 16, 200);
  long coefficient_exponent =
      adjusted - static_cast<long>(bounded_precision) + 1;

  mpz_class scaled_numerator = absolute_value.get_num();
  mpz_class scaled_denominator = absolute_value.get_den();
  if (coefficient_exponent < 0) {
    scaled_numerator *= power_of_ten(
        static_cast<unsigned long>(-coefficient_exponent));
  } else if (coefficient_exponent > 0) {
    scaled_denominator *=
        power_of_ten(static_cast<unsigned long>(coefficient_exponent));
  }
  mpz_class coefficient =
      round_half_even(scaled_numerator, scaled_denominator);

  while (coefficient != 0 && mpz_divisible_ui_p(coefficient.get_mpz_t(), 10)) {
    coefficient /= 10;
    ++coefficient_exponent;
  }
  std::string result =
      render_decimal_coefficient(coefficient, coefficient_exponent);
  if (negative) {
    result.insert(result.begin(), '-');
  }
  return result;
}

[[nodiscard]] Decimal require_decimal(const ExpressionValue &value) {
  if (!std::holds_alternative<Decimal>(value)) {
    throw std::invalid_argument("boolean value used as a number");
  }
  return std::get<Decimal>(value);
}

[[nodiscard]] bool truthy(const ExpressionValue &value) {
  if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value);
  }
  return std::get<Decimal>(value) != 0;
}

[[nodiscard]] Decimal truncate_quotient(const Decimal &left,
                                        const Decimal &right) {
  if (right == 0) {
    throw std::invalid_argument("division by zero");
  }
  const Decimal quotient = left / right;
  mpz_class integer;
  mpz_tdiv_q(integer.get_mpz_t(), quotient.get_num().get_mpz_t(),
             quotient.get_den().get_mpz_t());
  return Decimal(integer);
}

[[nodiscard]] Decimal decimal_power(Decimal base, const Decimal &power) {
  if (power.get_den() != 1) {
    throw std::invalid_argument("adapter exponents must be integral and bounded");
  }
  const mpz_class exponent_value = power.get_num();
  if (exponent_value > 32 || exponent_value < -32) {
    throw std::invalid_argument("adapter exponents must be integral and bounded");
  }
  const long exponent = exponent_value.get_si();
  if (exponent < 0) {
    if (base == 0) {
      throw std::invalid_argument("division by zero");
    }
    base = 1 / base;
  }
  Decimal result = 1;
  for (long index = 0; index < std::abs(exponent); ++index) {
    result *= base;
  }
  return result;
}

class DecimalExpressionParser final {
public:
  DecimalExpressionParser(std::string_view expression,
                          const DecimalVariables &variables)
      : expression_(expression), variables_(variables) {
    if (expression_.size() > maximum_expression_length) {
      throw std::invalid_argument("adapter expression exceeds length bound");
    }
  }

  [[nodiscard]] ExpressionValue parse() {
    ExpressionValue value = parse_or();
    skip_space();
    if (position_ != expression_.size()) {
      throw std::invalid_argument("unexpected adapter expression token");
    }
    return value;
  }

private:
  class DepthGuard final {
  public:
    explicit DepthGuard(int &depth) : depth_(depth) {
      ++depth_;
      if (depth_ > maximum_parse_depth) {
        throw std::invalid_argument("adapter expression nesting exceeds bound");
      }
    }
    ~DepthGuard() { --depth_; }

  private:
    int &depth_;
  };

  void skip_space() {
    while (position_ < expression_.size() &&
           std::isspace(
               static_cast<unsigned char>(expression_[position_])) != 0) {
      ++position_;
    }
  }

  [[nodiscard]] bool match(std::string_view token) {
    skip_space();
    if (expression_.substr(position_, token.size()) == token) {
      position_ += token.size();
      return true;
    }
    return false;
  }

  [[nodiscard]] bool match_keyword(std::string_view keyword) {
    skip_space();
    if (expression_.substr(position_, keyword.size()) != keyword) {
      return false;
    }
    const std::size_t end = position_ + keyword.size();
    if (end < expression_.size()) {
      const unsigned char next = static_cast<unsigned char>(expression_[end]);
      if (std::isalnum(next) != 0 || next == '_') {
        return false;
      }
    }
    position_ = end;
    return true;
  }

  [[nodiscard]] ExpressionValue parse_or() {
    DepthGuard guard(depth_);
    std::vector<ExpressionValue> values{parse_and()};
    while (match_keyword("or")) {
      values.push_back(parse_and());
    }
    if (values.size() == 1) {
      return values.front();
    }
    return std::ranges::any_of(values, truthy);
  }

  [[nodiscard]] ExpressionValue parse_and() {
    DepthGuard guard(depth_);
    std::vector<ExpressionValue> values{parse_not()};
    while (match_keyword("and")) {
      values.push_back(parse_not());
    }
    if (values.size() == 1) {
      return values.front();
    }
    return std::ranges::all_of(values, truthy);
  }

  [[nodiscard]] ExpressionValue parse_not() {
    DepthGuard guard(depth_);
    if (match_keyword("not")) {
      return !truthy(parse_not());
    }
    return parse_comparison();
  }

  [[nodiscard]] ExpressionValue parse_comparison() {
    DepthGuard guard(depth_);
    ExpressionValue left = parse_additive();
    bool compared = false;
    bool passed = true;
    while (true) {
      std::string_view operation;
      if (match("==")) {
        operation = "==";
      } else if (match("!=")) {
        operation = "!=";
      } else if (match("<=")) {
        operation = "<=";
      } else if (match(">=")) {
        operation = ">=";
      } else if (match("<")) {
        operation = "<";
      } else if (match(">")) {
        operation = ">";
      } else {
        break;
      }
      const ExpressionValue right = parse_additive();
      const Decimal left_number = require_decimal(left);
      const Decimal right_number = require_decimal(right);
      if (operation == "==") {
        passed = passed && left_number == right_number;
      } else if (operation == "!=") {
        passed = passed && left_number != right_number;
      } else if (operation == "<=") {
        passed = passed && left_number <= right_number;
      } else if (operation == ">=") {
        passed = passed && left_number >= right_number;
      } else if (operation == "<") {
        passed = passed && left_number < right_number;
      } else {
        passed = passed && left_number > right_number;
      }
      compared = true;
      left = right;
    }
    return compared ? ExpressionValue{passed} : left;
  }

  [[nodiscard]] ExpressionValue parse_additive() {
    DepthGuard guard(depth_);
    Decimal value = require_decimal(parse_multiplicative());
    while (true) {
      if (match("+")) {
        value += require_decimal(parse_multiplicative());
      } else if (match("-")) {
        value -= require_decimal(parse_multiplicative());
      } else {
        return value;
      }
    }
  }

  [[nodiscard]] ExpressionValue parse_multiplicative() {
    DepthGuard guard(depth_);
    Decimal value = require_decimal(parse_unary());
    while (true) {
      if (match("//")) {
        value = truncate_quotient(value, require_decimal(parse_unary()));
      } else if (match("*")) {
        if (expression_.substr(position_, 1) == "*") {
          --position_;
          return value;
        }
        value *= require_decimal(parse_unary());
      } else if (match("/")) {
        const Decimal denominator = require_decimal(parse_unary());
        if (denominator == 0) {
          throw std::invalid_argument("division by zero");
        }
        value /= denominator;
      } else if (match("%")) {
        const Decimal denominator = require_decimal(parse_unary());
        const Decimal quotient = truncate_quotient(value, denominator);
        value -= quotient * denominator;
      } else {
        return value;
      }
    }
  }

  [[nodiscard]] ExpressionValue parse_unary() {
    DepthGuard guard(depth_);
    if (match("+")) {
      return require_decimal(parse_unary());
    }
    if (match("-")) {
      return -require_decimal(parse_unary());
    }
    return parse_power();
  }

  [[nodiscard]] ExpressionValue parse_power() {
    DepthGuard guard(depth_);
    Decimal value = require_decimal(parse_primary());
    if (match("**")) {
      value = decimal_power(value, require_decimal(parse_unary()));
    }
    return value;
  }

  [[nodiscard]] ExpressionValue parse_primary() {
    DepthGuard guard(depth_);
    skip_space();
    if (position_ >= expression_.size()) {
      throw std::invalid_argument("unexpected end of adapter expression");
    }
    if (match("(")) {
      ExpressionValue value = parse_or();
      if (!match(")")) {
        throw std::invalid_argument("missing closing parenthesis");
      }
      return value;
    }

    const unsigned char first =
        static_cast<unsigned char>(expression_[position_]);
    if (std::isdigit(first) != 0 || expression_[position_] == '.') {
      const std::size_t start = position_;
      bool point_seen = false;
      while (position_ < expression_.size()) {
        const char character = expression_[position_];
        if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
          ++position_;
        } else if (character == '.' && !point_seen) {
          point_seen = true;
          ++position_;
        } else {
          break;
        }
      }
      if (position_ < expression_.size() &&
          (expression_[position_] == 'e' || expression_[position_] == 'E')) {
        ++position_;
        if (position_ < expression_.size() &&
            (expression_[position_] == '+' || expression_[position_] == '-')) {
          ++position_;
        }
        while (position_ < expression_.size() &&
               std::isdigit(
                   static_cast<unsigned char>(expression_[position_])) != 0) {
          ++position_;
        }
      }
      return parse_decimal(
          std::string(expression_.substr(start, position_ - start)));
    }
    if (std::isalpha(first) != 0 || expression_[position_] == '_') {
      const std::size_t start = position_++;
      while (position_ < expression_.size()) {
        const unsigned char character =
            static_cast<unsigned char>(expression_[position_]);
        if (std::isalnum(character) == 0 && character != '_') {
          break;
        }
        ++position_;
      }
      const std::string name(expression_.substr(start, position_ - start));
      const auto found = variables_.find(name);
      if (found == variables_.end()) {
        throw std::invalid_argument("unknown variable: " + name);
      }
      return parse_decimal(found->second);
    }
    throw std::invalid_argument("unsupported adapter expression token");
  }

  std::string_view expression_;
  const DecimalVariables &variables_;
  std::size_t position_ = 0;
  int depth_ = 0;
};

struct Polynomial final {
  PolynomialTerms terms;
};

void normalize_polynomial(Polynomial &value) {
  std::erase_if(value.terms,
                [](const auto &entry) { return entry.second == 0; });
}

[[nodiscard]] Polynomial constant_polynomial(Decimal value) {
  Polynomial result;
  if (value != 0) {
    result.terms.emplace(Monomial{}, std::move(value));
  }
  return result;
}

[[nodiscard]] Polynomial variable_polynomial(std::string name) {
  Polynomial result;
  result.terms.emplace(Monomial{{std::move(name), 1}}, Decimal{1});
  return result;
}

[[nodiscard]] Polynomial add_polynomials(Polynomial left,
                                         const Polynomial &right,
                                         int sign = 1) {
  for (const auto &[monomial, coefficient] : right.terms) {
    left.terms[monomial] += sign * coefficient;
  }
  normalize_polynomial(left);
  return left;
}

[[nodiscard]] Monomial multiply_monomials(const Monomial &left,
                                          const Monomial &right) {
  std::map<std::string, int> powers;
  for (const auto &[name, exponent] : left) {
    powers[name] += exponent;
  }
  for (const auto &[name, exponent] : right) {
    powers[name] += exponent;
  }
  Monomial result;
  for (const auto &[name, exponent] : powers) {
    if (exponent != 0) {
      result.emplace_back(name, exponent);
    }
  }
  return result;
}

[[nodiscard]] Polynomial multiply_polynomials(const Polynomial &left,
                                              const Polynomial &right) {
  Polynomial result;
  if (left.terms.size() * right.terms.size() > 4'096) {
    throw std::invalid_argument("symbolic expansion exceeds term bound");
  }
  for (const auto &[left_monomial, left_coefficient] : left.terms) {
    for (const auto &[right_monomial, right_coefficient] : right.terms) {
      result.terms[multiply_monomials(left_monomial, right_monomial)] +=
          left_coefficient * right_coefficient;
    }
  }
  normalize_polynomial(result);
  return result;
}

[[nodiscard]] bool constant_value(const Polynomial &value, Decimal &result) {
  if (value.terms.empty()) {
    result = 0;
    return true;
  }
  if (value.terms.size() == 1 && value.terms.begin()->first.empty()) {
    result = value.terms.begin()->second;
    return true;
  }
  return false;
}

[[nodiscard]] Polynomial power_polynomial(Polynomial base, long exponent) {
  if (exponent < 0) {
    Decimal constant;
    if (!constant_value(base, constant) || constant == 0) {
      throw std::invalid_argument(
          "negative symbolic powers require a non-zero constant base");
    }
    return constant_polynomial(decimal_power(constant, Decimal(exponent)));
  }
  Polynomial result = constant_polynomial(1);
  while (exponent > 0) {
    if ((exponent & 1L) != 0) {
      result = multiply_polynomials(result, base);
    }
    exponent /= 2;
    if (exponent > 0) {
      base = multiply_polynomials(base, base);
    }
  }
  return result;
}

class SymbolicParser final {
public:
  explicit SymbolicParser(std::string expression)
      : expression_(std::move(expression)) {
    std::size_t found = 0;
    while ((found = expression_.find('^', found)) != std::string::npos) {
      expression_.replace(found, 1, "**");
      found += 2;
    }
  }

  [[nodiscard]] Polynomial parse() {
    Polynomial result = parse_additive();
    skip_space();
    if (position_ != expression_.size()) {
      throw std::invalid_argument("unsupported symbolic expression token");
    }
    return result;
  }

private:
  void skip_space() {
    while (position_ < expression_.size() &&
           std::isspace(
               static_cast<unsigned char>(expression_[position_])) != 0) {
      ++position_;
    }
  }

  [[nodiscard]] bool match(std::string_view token) {
    skip_space();
    if (expression_.substr(position_, token.size()) == token) {
      position_ += token.size();
      return true;
    }
    return false;
  }

  [[nodiscard]] Polynomial parse_additive() {
    Polynomial result = parse_multiplicative();
    while (true) {
      if (match("+")) {
        result = add_polynomials(std::move(result), parse_multiplicative());
      } else if (match("-")) {
        result =
            add_polynomials(std::move(result), parse_multiplicative(), -1);
      } else {
        return result;
      }
    }
  }

  [[nodiscard]] Polynomial parse_multiplicative() {
    Polynomial result = parse_unary();
    while (true) {
      if (match("**")) {
        position_ -= 2;
        return result;
      }
      if (match("*")) {
        result = multiply_polynomials(result, parse_unary());
      } else if (match("/")) {
        const Polynomial denominator_value = parse_unary();
        Decimal denominator;
        if (!constant_value(denominator_value, denominator) ||
            denominator == 0) {
          throw std::invalid_argument(
              "symbolic division requires a non-zero constant denominator");
        }
        for (auto &[monomial, coefficient] : result.terms) {
          static_cast<void>(monomial);
          coefficient /= denominator;
        }
      } else {
        return result;
      }
    }
  }

  [[nodiscard]] Polynomial parse_unary() {
    if (match("+")) {
      return parse_unary();
    }
    if (match("-")) {
      Polynomial result = parse_unary();
      for (auto &[monomial, coefficient] : result.terms) {
        static_cast<void>(monomial);
        coefficient = -coefficient;
      }
      return result;
    }
    return parse_power();
  }

  [[nodiscard]] Polynomial parse_power() {
    Polynomial result = parse_primary();
    if (!match("**")) {
      return result;
    }
    const Polynomial exponent_value = parse_unary();
    Decimal exponent;
    if (!constant_value(exponent_value, exponent) || exponent.get_den() != 1 ||
        exponent > 32 || exponent < -32) {
      throw std::invalid_argument(
          "symbolic exponents must be integral and bounded");
    }
    return power_polynomial(std::move(result), exponent.get_num().get_si());
  }

  [[nodiscard]] Polynomial parse_primary() {
    skip_space();
    if (position_ >= expression_.size()) {
      throw std::invalid_argument("unexpected end of symbolic expression");
    }
    if (match("(")) {
      Polynomial result = parse_additive();
      if (!match(")")) {
        throw std::invalid_argument("missing closing parenthesis");
      }
      return result;
    }
    const unsigned char first =
        static_cast<unsigned char>(expression_[position_]);
    if (std::isdigit(first) != 0 || expression_[position_] == '.') {
      const std::size_t start = position_;
      bool point_seen = false;
      while (position_ < expression_.size()) {
        const char character = expression_[position_];
        if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
          ++position_;
        } else if (character == '.' && !point_seen) {
          point_seen = true;
          ++position_;
        } else {
          break;
        }
      }
      return constant_polynomial(parse_decimal(
          expression_.substr(start, position_ - start)));
    }
    if (std::isalpha(first) != 0 || expression_[position_] == '_') {
      const std::size_t start = position_++;
      while (position_ < expression_.size()) {
        const unsigned char character =
            static_cast<unsigned char>(expression_[position_]);
        if (std::isalnum(character) == 0 && character != '_') {
          break;
        }
        ++position_;
      }
      return variable_polynomial(
          expression_.substr(start, position_ - start));
    }
    throw std::invalid_argument("unsupported symbolic expression token");
  }

  std::string expression_;
  std::size_t position_ = 0;
};

[[nodiscard]] std::string rational_text(const Decimal &value) {
  if (value.get_den() == 1) {
    return value.get_num().get_str();
  }
  return value.get_num().get_str() + "/" + value.get_den().get_str();
}

[[nodiscard]] std::string monomial_text(const Monomial &monomial) {
  std::string result;
  for (const auto &[name, exponent] : monomial) {
    if (!result.empty()) {
      result.push_back('*');
    }
    result.append(name);
    if (exponent != 1) {
      result.push_back('^');
      result.append(std::to_string(exponent));
    }
  }
  return result;
}

[[nodiscard]] std::string polynomial_text(const Polynomial &value) {
  if (value.terms.empty()) {
    return "0";
  }
  std::string result;
  for (auto iterator = value.terms.rbegin(); iterator != value.terms.rend();
       ++iterator) {
    const Decimal coefficient = iterator->second;
    const bool negative = coefficient < 0;
    const Decimal magnitude = negative ? -coefficient : coefficient;
    if (result.empty()) {
      if (negative) {
        result.push_back('-');
      }
    } else {
      result.append(negative ? " - " : " + ");
    }
    const std::string monomial = monomial_text(iterator->first);
    if (monomial.empty() || magnitude != 1) {
      result.append(rational_text(magnitude));
      if (!monomial.empty()) {
        result.push_back('*');
      }
    }
    result.append(monomial);
  }
  return result;
}

[[nodiscard]] bool symbolic_grammar_allowed(std::string_view expression) {
  if (expression.empty() || expression.size() > maximum_expression_length ||
      expression.find("__") != std::string_view::npos) {
    return false;
  }
  return std::ranges::all_of(expression, [](unsigned char character) {
    return std::isalnum(character) != 0 || character == '_' ||
           character == '+' || character == '-' || character == '*' ||
           character == '/' || character == '^' || character == '(' ||
           character == ')' || character == '.' || character == ' ';
  });
}

[[nodiscard]] std::string expression_result_text(const ExpressionValue &value,
                                                 int precision) {
  if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value) ? "True" : "False";
  }
  return format_decimal(std::get<Decimal>(value), precision);
}

[[nodiscard]] std::string exception_text(const std::exception &error) {
  return "ValueError: " + std::string(error.what());
}

[[nodiscard]] std::vector<std::string>
canonical_details(std::vector<std::string> values) {
  std::ranges::sort(values);
  values.erase(std::unique(values.begin(), values.end()), values.end());
  return values;
}

[[nodiscard]] AdapterResult finish_adapter(
    std::string adapter, std::string claim, std::string status,
    std::string result, std::string tolerance = {},
    AdapterCounterexample counterexample = {},
    std::vector<std::string> details = {}) {
  AdapterResult value;
  value.adapter = std::move(adapter);
  value.claim = std::move(claim);
  value.status = std::move(status);
  value.result = std::move(result);
  value.details = std::move(details);
  value.tolerance = std::move(tolerance);
  value.counterexample = std::move(counterexample);
  return canonicalize_adapter_result(std::move(value));
}

[[nodiscard]] contracts::Json adapter_material(const AdapterResult &value) {
  return {{"schema_version", value.schema_version},
          {"adapter", value.adapter},
          {"claim", value.claim},
          {"status", value.status},
          {"result", value.result},
          {"details", value.details},
          {"tolerance", value.tolerance},
          {"counterexample", value.counterexample}};
}

[[nodiscard]] std::string dimensions_text(
    const std::map<std::string, int> &dimensions) {
  std::string result = "(";
  bool first = true;
  for (const auto &[name, exponent] : dimensions) {
    if (exponent == 0) {
      continue;
    }
    if (!first) {
      result.append(", ");
    }
    result.append("('");
    result.append(name);
    result.append("', ");
    result.append(std::to_string(exponent));
    result.push_back(')');
    first = false;
  }
  if (!first && std::ranges::count_if(dimensions, [](const auto &entry) {
        return entry.second != 0;
      }) == 1) {
    result.push_back(',');
  }
  result.push_back(')');
  return result;
}

} // namespace

const std::vector<std::string> &adapter_statuses() {
  static const std::vector<std::string> values{
      "FAIL", "INCONCLUSIVE", "PASS", "UNAVAILABLE"};
  return values;
}

contracts::Json to_json(const AdapterResult &value) {
  return {{"schema_version", value.schema_version},
          {"adapter", value.adapter},
          {"claim", value.claim},
          {"status", value.status},
          {"result", value.result},
          {"evidence_id", value.evidence_id},
          {"details", value.details},
          {"tolerance", value.tolerance},
          {"counterexample", value.counterexample},
          {"signature", value.signature}};
}

AdapterResult canonicalize_adapter_result(AdapterResult value) {
  if (value.adapter.empty() || value.claim.empty()) {
    policy_error("adapter and claim must be non-empty");
  }
  if (std::ranges::find(adapter_statuses(), value.status) ==
      adapter_statuses().end()) {
    policy_error("invalid adapter status: " + value.status);
  }
  value.details = canonical_details(std::move(value.details));
  std::ranges::sort(value.counterexample);
  const contracts::Json material = adapter_material(value);
  const std::string evidence_id =
      "adapter-evidence:" + contracts::sha256_json(material);
  if (!value.evidence_id.empty() && value.evidence_id != evidence_id) {
    policy_error("adapter evidence ID mismatch");
  }
  value.evidence_id = evidence_id;
  contracts::Json signature_material = material;
  signature_material["evidence_id"] = evidence_id;
  const std::string signature = contracts::sha256_json(signature_material);
  if (!value.signature.empty() && value.signature != signature) {
    policy_error("adapter result signature mismatch");
  }
  value.signature = signature;
  return value;
}

void require_adapter_result_integrity(const AdapterResult &value) {
  AdapterResult rebuilt = value;
  rebuilt.evidence_id.clear();
  rebuilt.signature.clear();
  rebuilt = canonicalize_adapter_result(std::move(rebuilt));
  if (to_json(rebuilt) != to_json(value)) {
    policy_error("adapter result integrity check failed");
  }
}

AdapterResult evaluate_decimal_expression(std::string expression,
                                          DecimalVariables variables,
                                          int precision) {
  const std::string claim = "evaluate " + expression;
  try {
    const int bounded_precision = std::clamp(precision, 16, 200);
    const ExpressionValue result =
        DecimalExpressionParser(expression, variables).parse();
    return finish_adapter("decimal-arithmetic-v1", claim, "PASS",
                          expression_result_text(result, bounded_precision));
  } catch (const std::exception &error) {
    return finish_adapter("decimal-arithmetic-v1", claim, "INCONCLUSIVE",
                          exception_text(error));
  }
}

AdapterResult symbolic_equivalence(std::string left, std::string right) {
  const std::string claim = left + " == " + right;
  if (!symbolic_grammar_allowed(left) || !symbolic_grammar_allowed(right)) {
    return finish_adapter(
        "sympy-equivalence-v1", claim, "INCONCLUSIVE",
        "expression rejected by the bounded symbolic grammar");
  }
  try {
    Polynomial residual = add_polynomials(SymbolicParser(left).parse(),
                                          SymbolicParser(right).parse(), -1);
    const bool equivalent = residual.terms.empty();
    return finish_adapter("sympy-equivalence-v1", claim,
                          equivalent ? "PASS" : "FAIL",
                          polynomial_text(residual));
  } catch (const std::exception &error) {
    return finish_adapter("sympy-equivalence-v1", claim, "INCONCLUSIVE",
                          exception_text(error));
  }
}

AdapterResult numerical_residual_check(std::string left, std::string right,
                                       const DecimalPoints &points,
                                       std::string tolerance) {
  const std::string claim =
      "numerical residual for " + left + " == " + right;
  const Decimal threshold = parse_decimal(tolerance);
  if (threshold < 0) {
    policy_error("numerical tolerance must be non-negative");
  }
  const std::string threshold_text = format_decimal(threshold, 200);
  try {
    for (const auto &point : points) {
      const Decimal left_value = require_decimal(
          DecimalExpressionParser(left, point).parse());
      const Decimal right_value = require_decimal(
          DecimalExpressionParser(right, point).parse());
      const Decimal residual =
          left_value >= right_value ? left_value - right_value
                                    : right_value - left_value;
      if (residual > threshold) {
        AdapterCounterexample counterexample(point.begin(), point.end());
        return finish_adapter("numerical-residual-v1", claim, "FAIL",
                              "residual " + format_decimal(residual) +
                                  " exceeds tolerance",
                              threshold_text, std::move(counterexample));
      }
    }
  } catch (const std::exception &error) {
    return finish_adapter("numerical-residual-v1", claim, "INCONCLUSIVE",
                          exception_text(error), threshold_text);
  }
  return finish_adapter(
      "numerical-residual-v1", claim, "PASS",
      std::to_string(points.size()) + " points within tolerance",
      threshold_text);
}

AdapterResult dimensional_equivalence(
    const std::map<std::string, int> &left_dimensions,
    const std::map<std::string, int> &right_dimensions,
    std::string equation) {
  std::map<std::string, int> left;
  std::map<std::string, int> right;
  for (const auto &[name, exponent] : left_dimensions) {
    if (exponent != 0) {
      left[name] = exponent;
    }
  }
  for (const auto &[name, exponent] : right_dimensions) {
    if (exponent != 0) {
      right[name] = exponent;
    }
  }
  return finish_adapter("dimensional-analysis-v1",
                        "dimensions match for " + equation,
                        left == right ? "PASS" : "FAIL",
                        "left=" + dimensions_text(left) +
                            "; right=" + dimensions_text(right));
}

AdapterResult finite_domain_check(std::string predicate,
                                  const FiniteDomains &domains,
                                  std::size_t max_combinations) {
  const std::string claim = "finite-domain predicate: " + predicate;
  const std::size_t bound = std::max<std::size_t>(1, max_combinations);
  std::size_t combinations = 1;
  for (const auto &[name, values] : domains) {
    static_cast<void>(name);
    if (values.empty()) {
      combinations = 0;
      break;
    }
    if (combinations > bound / values.size()) {
      return finish_adapter(
          "finite-domain-v1", claim, "INCONCLUSIVE",
          "finite domain exceeds the configured combination bound");
    }
    combinations *= values.size();
  }
  if (combinations > bound) {
    return finish_adapter(
        "finite-domain-v1", claim, "INCONCLUSIVE",
        "finite domain exceeds the configured combination bound");
  }

  std::vector<std::pair<std::string, const std::vector<std::string> *>> ordered;
  for (const auto &[name, values] : domains) {
    ordered.emplace_back(name, &values);
  }
  try {
    if (combinations != 0) {
      std::vector<std::size_t> indices(ordered.size(), 0);
      for (std::size_t combination = 0; combination < combinations;
           ++combination) {
        DecimalVariables assignment;
        AdapterCounterexample counterexample;
        for (std::size_t index = 0; index < ordered.size(); ++index) {
          const auto &[name, values] = ordered[index];
          const std::string &selected = values->at(indices[index]);
          assignment[name] = selected;
          counterexample.emplace_back(name, selected);
        }
        if (!truthy(DecimalExpressionParser(predicate, assignment).parse())) {
          return finish_adapter("finite-domain-v1", claim, "FAIL",
                                "counterexample found", {},
                                std::move(counterexample));
        }
        for (std::size_t index = ordered.size(); index > 0; --index) {
          const std::size_t current = index - 1;
          ++indices[current];
          if (indices[current] < ordered[current].second->size()) {
            break;
          }
          indices[current] = 0;
        }
      }
    }
  } catch (const std::exception &error) {
    return finish_adapter("finite-domain-v1", claim, "INCONCLUSIVE",
                          exception_text(error));
  }
  return finish_adapter("finite-domain-v1", claim, "PASS",
                        "all " + std::to_string(combinations) +
                            " assignments passed");
}

} // namespace statewright::reasoning
