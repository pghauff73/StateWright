#pragma once

#include "statewright/egcf/exact_polynomial_expression.hpp"
#include "statewright/egcf/internet_polynomial.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/sources/records.hpp"
#include <map>

namespace statewright::egcf {

struct InternetPolynomialSourceTranslation final {
  std::string name;
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;
  contracts::Json saa_ir;
  std::vector<std::string> invariants;
  contracts::Json termination;
  contracts::Json provenance;
  contracts::Json units;
};

// Complete labeled procedures only. Source permissions, qualification and
// mathematical review remain separate gates; no missing context is inferred.
[[nodiscard]] inline std::optional<InternetPolynomialSourceTranslation>
translate_internet_polynomial_fragment(const sources::InternetSourceFragment &fragment) {
  if (fragment.fragment_kind != "ALGORITHM_DESCRIPTION" || fragment.text.size() > 8192U ||
      fragment.metadata.value("mathematical_context_review_required", false) ||
      fragment.metadata.value("section_completeness", std::string{}) == "INCOMPLETE" ||
      (fragment.metadata.value("document_extraction_truncated", false) &&
       fragment.metadata.value("section_completeness", std::string{}) != "COMPLETE")) return std::nullopt;
  const auto trim = [](std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string{};
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1U);
  };
  const auto lower = [](std::string value) {
    for (auto &character : value) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return value;
  };
  const auto separator = fragment.text.find(';');
  if (separator == std::string::npos) return std::nullopt;
  const auto name = trim(fragment.text.substr(0, separator));
  if (name.empty() || name.size() > 128U) return std::nullopt;
  std::map<std::string, std::string> fields;
  std::size_t offset = separator + 1U;
  while (offset < fragment.text.size()) {
    const auto next = fragment.text.find(';', offset);
    const auto field = fragment.text.substr(offset, next == std::string::npos ? next : next - offset);
    const auto colon = field.find(':');
    if (colon == std::string::npos) return std::nullopt;
    const auto label = lower(trim(field.substr(0, colon)));
    if (label != "inputs" && label != "outputs" && label != "domain" &&
        label != "arithmetic" && label != "units" && label != "procedure") return std::nullopt;
    if (!fields.emplace(label, trim(field.substr(colon + 1U))).second) return std::nullopt;
    if (next == std::string::npos) break;
    offset = next + 1U;
  }
  if (fields.size() != 6U || lower(fields.at("arithmetic")) != "exact rational" ||
      lower(fields.at("units")) != "dimensionless") return std::nullopt;
  auto domain = fields.at("domain");
  domain.erase(std::remove_if(domain.begin(), domain.end(), [](unsigned char c) { return std::isspace(c) != 0; }), domain.end());
  if (domain != "[-1,1]") return std::nullopt;
  const auto input = fields.at("inputs");
  const auto output = fields.at("outputs");
  const auto identifier = [](const std::string &value) {
    return !value.empty() && value.size() <= 64U &&
        (std::isalpha(static_cast<unsigned char>(value.front())) != 0 || value.front() == '_') &&
        std::ranges::all_of(value, [](unsigned char c) { return std::isalnum(c) != 0 || c == '_'; });
  };
  if (!identifier(input) || !identifier(output)) return std::nullopt;
  const auto &procedure = fields.at("procedure");
  const auto equals = procedure.find('=');
  if (equals == std::string::npos || trim(procedure.substr(0, equals)) != output) return std::nullopt;
  const auto expression = trim(procedure.substr(equals + 1U));
  const auto coefficients = parse_exact_polynomial_expression(expression, input);
  if (!coefficients) return std::nullopt;
  const auto ir = internet_exact_polynomial_ir(*coefficients);
  contracts::Json coefficient_values = contracts::Json::array();
  for (const auto &coefficient : *coefficients) coefficient_values.push_back(coefficient.get_str());
  const auto contract = internet_polynomial_contract(internet_exact_polynomial_program(ir));
  return InternetPolynomialSourceTranslation{
      name, {input}, {output}, ir,
      {"DECLARED_INPUT_DOMAIN_ENFORCED", "EXACT_RATIONAL_RESULT_ON_SUCCESS", "NO_APPROXIMATION_CLAIM"},
      {{"bounded", true}, {"maximum_arithmetic_steps", 2U * (coefficients->size() - 1U)},
       {"maximum_integer_bits", 16384}},
      {{"translator_version", "exact-polynomial-source-v1"},
       {"source_fragment_id", fragment.object_id()}, {"snapshot_id", fragment.snapshot_id},
       {"selector", fragment.selector}, {"source_text_sha256", contracts::sha256_json(fragment.text)},
       {"source_procedure", procedure}, {"source_expression", expression},
       {"source_derived_coefficients", coefficient_values}, {"execution_contract", contract}},
      {{"input", "dimensionless"}, {"output", "dimensionless"}, {"status", "SOURCE_DECLARED"}}};
}

} // namespace statewright::egcf
