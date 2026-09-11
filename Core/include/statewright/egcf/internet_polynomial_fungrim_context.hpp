#pragma once

#include "statewright/contracts/hash.hpp"
#include "statewright/egcf/exact_polynomial_expression.hpp"
#include "statewright/egcf/internet_polynomial.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::egcf {

// Inspect inert source data only. This is neither Python evaluation nor a
// mathematical review, and it must not clear the original quarantine flags.
[[nodiscard]] inline std::optional<contracts::Json>
inspect_fungrim_chebyshev_quadratic_context(const contracts::Json &fragment) {
  if (fragment.value("fragment_kind", std::string{}) != "ALGORITHM_DESCRIPTION" ||
      !fragment.contains("text") || !fragment.at("text").is_string()) return std::nullopt;
  const auto &source = fragment.at("text").get_ref<const std::string &>();
  if (source.empty() || source.size() > 65536U) return std::nullopt;
  constexpr std::string_view marker = "make_entry(ID(\"85e42e\"),";
  std::vector<char> stack;
  std::size_t selected_start = std::string::npos;
  std::size_t selected_end = std::string::npos;
  std::size_t matches = 0;
  for (std::size_t i = 0; i < source.size(); ++i) {
    const char ch = source[i];
    if (ch == '#') {
      const auto end = source.find('\n', i);
      if (end == std::string::npos) break;
      i = end;
      continue;
    }
    if (ch == '\'' || ch == '"') {
      const bool triple = i + 2U < source.size() && source[i + 1U] == ch && source[i + 2U] == ch;
      const std::string delimiter(triple ? 3U : 1U, ch);
      std::size_t j = i + delimiter.size();
      bool closed = false;
      while (j < source.size()) {
        if (source[j] == '\\') { j += 2U; continue; }
        if (source.compare(j, delimiter.size(), delimiter) == 0) {
          i = j + delimiter.size() - 1U;
          closed = true;
          break;
        }
        ++j;
      }
      if (!closed) return std::nullopt;
      continue;
    }
    if (stack.empty() && source.compare(i, marker.size(), marker) == 0 &&
        (i == 0U || source[i - 1U] == '\n')) {
      if (++matches != 1U) return std::nullopt;
      selected_start = i;
    }
    if (ch == '(' || ch == '[' || ch == '{') {
      if (stack.size() == 64U) return std::nullopt;
      stack.push_back(ch);
    } else if (ch == ')' || ch == ']' || ch == '}') {
      const char opening = ch == ')' ? '(' : ch == ']' ? '[' : '{';
      if (stack.empty() || stack.back() != opening) return std::nullopt;
      stack.pop_back();
      if (stack.empty() && selected_start != std::string::npos && selected_end == std::string::npos)
        selected_end = i + 1U;
    }
  }
  if (!stack.empty() || matches != 1U || selected_end == std::string::npos) return std::nullopt;
  const auto entry = source.substr(selected_start, selected_end - selected_start);
  std::string compact;
  for (char ch : entry) {
    if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') compact += ch;
  }
  constexpr std::string_view prefix =
      "make_entry(ID(\"85e42e\"),Description(\"Tableof\",ChebyshevT(n,x),\"for\",LessEqual(0,n,15)),"
      "Table(TableRelation(Tuple(n,p),Equal(ChebyshevT(n,x),p)),"
      "TableHeadings(n,ChebyshevT(n,x)),TableSplit(1),List(";
  constexpr std::string_view suffix = ")),Variables(x),Assumptions(Element(x,CC)))";
  if (!compact.starts_with(prefix) || !compact.ends_with(suffix)) return std::nullopt;
  const auto rows = compact.substr(prefix.size(), compact.size() - prefix.size() - suffix.size());
  std::size_t position = 0;
  contracts::Json coefficients = contracts::Json::array();
  std::string selected_expression;
  for (unsigned degree = 0; degree <= 15U; ++degree) {
    const auto row_prefix = "Tuple(" + std::to_string(degree) + ",";
    if (rows.compare(position, row_prefix.size(), row_prefix) != 0) return std::nullopt;
    position += row_prefix.size();
    const auto start = position;
    unsigned depth = 1;
    while (position < rows.size() && depth != 0U) {
      if (rows[position] == '(') ++depth;
      if (rows[position] == ')') --depth;
      if (depth != 0U) ++position;
    }
    if (depth != 0U) return std::nullopt;
    auto expression = rows.substr(start, position - start);
    const auto source_expression = expression;
    for (std::size_t power = 0; (power = expression.find("**", power)) != std::string::npos; ++power)
      expression.replace(power, 2U, "^");
    const auto parsed = parse_exact_polynomial_expression(expression, "x");
    if (!parsed || parsed->size() != degree + 1U) return std::nullopt;
    if (degree == 2U) {
      selected_expression = source_expression;
      for (const auto &coefficient : *parsed) coefficients.push_back(coefficient.get_str());
    }
    ++position;
    if (degree != 15U) {
      if (position >= rows.size() || rows[position++] != ',') return std::nullopt;
    }
  }
  if (position != rows.size()) return std::nullopt;
  return contracts::Json{
      {"version", "fungrim-chebyshev-quadratic-context-v1"},
      {"status", "SOURCE_CONTEXT_LOCATED_NOT_REVIEWED"},
      {"snapshot_id", fragment.at("snapshot_id")},
      {"fragment_signature", fragment.at("fragment_signature")},
      {"source_text_sha256", contracts::sha256_json(source)},
      {"entry_id", "85e42e"}, {"entry_byte_start", selected_start},
      {"entry_byte_end", selected_end}, {"entry_text", entry},
      {"entry_text_sha256", contracts::sha256_json(entry)},
      {"operation", "ChebyshevT"}, {"degree", 2},
      {"source_expression", selected_expression}, {"coefficients", coefficients},
      {"source_assumptions", "Element(x, CC)"},
      {"normalization", "ASCII whitespace removal and ** to ^ for bounded expression parsing"},
      {"mathematical_review_required", true}, {"qualification_claim", "NONE"},
      {"external_dependencies_expanded", false}, {"source_executed", false}};
}

// Compile the selected inert formula into the already supported native family.
// This deliberately returns a proposal, not a reviewed or registered candidate.
[[nodiscard]] inline std::optional<contracts::Json>
preview_fungrim_chebyshev_quadratic_translation(const contracts::Json &fragment) {
  const auto context = inspect_fungrim_chebyshev_quadratic_context(fragment);
  if (!context) return std::nullopt;
  std::vector<mpq_class> coefficients;
  for (const auto &value : context->at("coefficients"))
    coefficients.emplace_back(value.get<std::string>());
  const auto ir = internet_exact_polynomial_ir(coefficients);
  const auto program = internet_exact_polynomial_program(ir);
  return contracts::Json{
      {"translator_version", "fungrim-chebyshev-quadratic-horner-proposal-v1"},
      {"status", "TRANSLATED_PROPOSAL_REVIEW_REQUIRED"},
      {"source_context", *context}, {"proposed_saa_ir", ir},
      {"candidate_ir_sha256", contracts::sha256_json(ir)},
      {"execution_contract", internet_polynomial_contract(program)},
      {"semantic_inputs", contracts::Json::array({"x"})},
      {"semantic_outputs", contracts::Json::array({"y"})},
      {"scope_restrictions", contracts::Json::array({
          "Fixed degree two only; not the general Chebyshev generator",
          "Exact-rational input in [-1,1], a subset of the source complex domain",
          "Native coefficient, input and intermediate resource limits apply",
          "Dimensionless mathematical values; no physical-unit conversion"})},
      {"required_reviews", contracts::Json::array({
          "MATHEMATICAL_CONTEXT_REVIEW_REQUIRED",
          "DOMAIN_BRANCH_AND_ERROR_BOUNDS_NOT_QUALIFIED"})},
      {"qualification_claim", "NONE"}, {"admission", false},
      {"source_executed", false}, {"candidate_registered", false}};
}

[[nodiscard]] inline std::optional<contracts::Json>
bind_fungrim_quadratic_translation_to_snapshot(const contracts::Json &fragment,
                                              const contracts::Json &snapshot) {
  const auto proposal = preview_fungrim_chebyshev_quadratic_translation(fragment);
  if (!proposal) return std::nullopt;
  const auto &text = fragment.at("text").get_ref<const std::string &>();
  const auto body_hash = contracts::sha256_text(text);
  if (!fragment.contains("byte_start") || !fragment.contains("byte_end") ||
      fragment.at("byte_start") != 0 || fragment.at("byte_end") != text.size() ||
      !snapshot.contains("body_size") || snapshot.at("body_size") != text.size() ||
      !snapshot.contains("body_sha256") || snapshot.at("body_sha256") != body_hash)
    return std::nullopt;
  auto bound = *proposal;
  bound["source_body_sha256"] = body_hash;
  bound["source_binding"] = "WHOLE_FRAGMENT_BYTES_MATCH_SNAPSHOT_BODY";
  return bound;
}

} // namespace statewright::egcf
