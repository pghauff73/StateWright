#pragma once

#include <gmpxx.h>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace statewright::egcf {

// A bounded expression parser, not a source-code interpreter. Only affine
// arithmetic over one declared variable and exact decimal/rational constants.
struct ParsedAffineExpression {
  mpq_class slope{0};
  mpq_class bias{0};
};

inline std::optional<ParsedAffineExpression>
parse_exact_affine_expression(std::string_view text, std::string_view variable) {
  if (text.empty() || text.size() > 256 || variable.empty()) return std::nullopt;
  struct Parser {
    std::string_view text, variable;
    std::size_t position = 0;
    void spaces() {
      while (position < text.size() &&
             std::isspace(static_cast<unsigned char>(text[position]))) ++position;
    }
    bool take(char c) {
      spaces();
      if (position == text.size() || text[position] != c) return false;
      ++position;
      return true;
    }
    ParsedAffineExpression atom(unsigned depth) {
      if (depth > 32) throw std::runtime_error("expression nesting");
      if (take('+')) return atom(depth + 1);
      if (take('-')) {
        auto value = atom(depth + 1);
        value.slope = -value.slope;
        value.bias = -value.bias;
        return value;
      }
      if (take('(')) {
        auto value = sum(depth + 1);
        if (!take(')')) throw std::runtime_error("unclosed expression");
        return value;
      }
      spaces();
      if (text.substr(position, variable.size()) == variable) {
        position += variable.size();
        return {mpq_class{1}, mpq_class{0}};
      }
      std::string digits;
      unsigned fractional = 0;
      bool decimal = false;
      while (position < text.size()) {
        const char c = text[position];
        if (c >= '0' && c <= '9') {
          digits += c;
          if (decimal) ++fractional;
          ++position;
        } else if (c == '.' && !decimal) {
          decimal = true;
          ++position;
        } else break;
      }
      if (digits.empty() || digits.size() > 64)
        throw std::runtime_error("invalid exact constant");
      mpz_class denominator{1};
      for (unsigned i = 0; i < fractional; ++i) denominator *= 10;
      mpq_class value(mpz_class(digits, 10), denominator);
      value.canonicalize();
      return {mpq_class{0}, value};
    }
    ParsedAffineExpression product(unsigned depth) {
      auto left = atom(depth);
      for (;;) {
        if (take('*')) {
          const auto right = atom(depth);
          if (left.slope != 0 && right.slope != 0)
            throw std::runtime_error("nonlinear product");
          left.slope = left.slope * right.bias + right.slope * left.bias;
          left.bias *= right.bias;
        } else if (take('/')) {
          const auto right = atom(depth);
          if (right.slope != 0 || right.bias == 0)
            throw std::runtime_error("nonconstant or zero divisor");
          left.slope /= right.bias;
          left.bias /= right.bias;
        } else return left;
      }
    }
    ParsedAffineExpression sum(unsigned depth) {
      auto left = product(depth);
      for (;;) {
        if (take('+')) {
          const auto right = product(depth);
          left.slope += right.slope; left.bias += right.bias;
        } else if (take('-')) {
          const auto right = product(depth);
          left.slope -= right.slope; left.bias -= right.bias;
        } else return left;
      }
    }
  };
  try {
    Parser parser{text, variable};
    auto value = parser.sum(0);
    parser.spaces();
    if (parser.position != text.size() || value.slope == 0) return std::nullopt;
    value.slope.canonicalize(); value.bias.canonicalize();
    return value;
  } catch (const std::exception &) { return std::nullopt; }
}
} // namespace statewright::egcf
