#pragma once

#include "statewright/common/error.hpp"
#include <gmpxx.h>
#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::egcf {
namespace polynomial_expression_detail {

using Coefficients = std::vector<mpq_class>;

[[noreturn]] inline void invalid() {
  throw common::Error(common::ErrorCode::invalid_argument,
                      "UNSUPPORTED_OR_UNBOUNDED_POLYNOMIAL_EXPRESSION");
}

inline void normalize(Coefficients &values) {
  for (auto &value : values) {
    value.canonicalize();
    if (mpz_sizeinbase(value.get_num().get_mpz_t(), 2) > 256U ||
        mpz_sizeinbase(value.get_den().get_mpz_t(), 2) > 256U) invalid();
  }
  while (values.size() > 1U && values.back() == 0) values.pop_back();
}

inline Coefficients multiply(const Coefficients &left, const Coefficients &right) {
  if (left.size() + right.size() - 1U > 17U) invalid();
  Coefficients result(left.size() + right.size() - 1U, 0);
  for (std::size_t i = 0; i < left.size(); ++i) {
    for (std::size_t j = 0; j < right.size(); ++j) {
      result.at(i + j) += left.at(i) * right.at(j);
      auto &value = result.at(i + j);
      if (mpz_sizeinbase(value.get_num().get_mpz_t(), 2) > 256U ||
          mpz_sizeinbase(value.get_den().get_mpz_t(), 2) > 256U) invalid();
    }
  }
  normalize(result);
  return result;
}

class Parser final {
public:
  Parser(std::string_view source, std::string_view variable)
      : source_(source), variable_(variable) {}

  Coefficients parse() {
    auto result = expression();
    whitespace();
    if (offset_ != source_.size()) invalid();
    return result;
  }

private:
  struct Depth final {
    std::size_t &value;
    explicit Depth(std::size_t &depth) : value(depth) {
      if (++value > 32U) invalid();
    }
    ~Depth() { --value; }
  };

  void whitespace() {
    while (offset_ < source_.size() &&
           std::isspace(static_cast<unsigned char>(source_[offset_])) != 0) ++offset_;
  }
  bool take(char character) {
    whitespace();
    if (offset_ == source_.size() || source_[offset_] != character) return false;
    ++offset_;
    if (++tokens_ > 256U) invalid();
    return true;
  }
  Coefficients expression() {
    auto result = product();
    for (;;) {
      const bool plus = take('+');
      if (!plus && !take('-')) break;
      auto right = product();
      result.resize(std::max(result.size(), right.size()), 0);
      for (std::size_t i = 0; i < right.size(); ++i) {
        if (plus) result.at(i) += right.at(i);
        else result.at(i) -= right.at(i);
      }
      normalize(result);
    }
    return result;
  }
  Coefficients product() {
    auto result = unary();
    for (;;) {
      if (take('*')) result = multiply(result, unary());
      else if (take('/')) {
        auto divisor = unary();
        if (divisor.size() != 1U || divisor.front() == 0) invalid();
        divisor.front() = 1 / divisor.front();
        result = multiply(result, divisor);
      } else break;
    }
    return result;
  }
  Coefficients unary() {
    Depth guard(depth_);
    if (take('+')) return unary();
    if (take('-')) {
      auto value = unary();
      for (auto &coefficient : value) coefficient = -coefficient;
      return value;
    }
    auto base = atom();
    if (take('^')) {
      whitespace();
      const auto start = offset_;
      while (offset_ < source_.size() &&
             std::isdigit(static_cast<unsigned char>(source_[offset_])) != 0) ++offset_;
      const auto length = offset_ - start;
      if (length == 0U || length > 2U || ++tokens_ > 256U) invalid();
      const auto power = std::stoul(std::string(source_.substr(start, length)));
      if (power > 16U) invalid();
      Coefficients result{1};
      for (unsigned long i = 0; i < power; ++i) result = multiply(result, base);
      return result;
    }
    return base;
  }
  Coefficients atom() {
    if (take('(')) {
      auto result = expression();
      if (!take(')')) invalid();
      return result;
    }
    whitespace();
    const auto start = offset_;
    if (offset_ < source_.size() &&
        std::isdigit(static_cast<unsigned char>(source_[offset_])) != 0) {
      while (offset_ < source_.size() &&
             std::isdigit(static_cast<unsigned char>(source_[offset_])) != 0) ++offset_;
      if (offset_ - start > 80U || ++tokens_ > 256U) invalid();
      Coefficients result{mpq_class(mpz_class(std::string(source_.substr(start, offset_ - start)), 10))};
      normalize(result);
      return result;
    }
    while (offset_ < source_.size() &&
           (std::isalnum(static_cast<unsigned char>(source_[offset_])) != 0 || source_[offset_] == '_')) ++offset_;
    if (offset_ == start || source_.substr(start, offset_ - start) != variable_ ||
        ++tokens_ > 256U) invalid();
    return {0, 1};
  }

  std::string_view source_;
  std::string_view variable_;
  std::size_t offset_ = 0;
  std::size_t tokens_ = 0;
  std::size_t depth_ = 0;
};

} // namespace polynomial_expression_detail

// Parse the whole expression. No prose removal, implicit multiplication,
// function calls, runtime coefficients or approximate numeric literals.
[[nodiscard]] inline std::optional<std::vector<mpq_class>> parse_exact_polynomial_expression(
    std::string_view expression, std::string_view variable) {
  if (expression.empty() || expression.size() > 4096U || variable.empty() ||
      variable.size() > 64U ||
      (std::isalpha(static_cast<unsigned char>(variable.front())) == 0 && variable.front() != '_') ||
      !std::ranges::all_of(variable, [](unsigned char c) { return std::isalnum(c) != 0 || c == '_'; })) return std::nullopt;
  try {
    return polynomial_expression_detail::Parser(expression, variable).parse();
  } catch (const common::Error &) {
    return std::nullopt;
  }
}

} // namespace statewright::egcf
