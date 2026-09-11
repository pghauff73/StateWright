#pragma once

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/egcf/internet_polynomial.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace statewright::egcf {

inline constexpr std::string_view internet_polynomial_form_version =
    "internet-full-exact-polynomial-form-v1";

// A complete executable representation, not a qualification or admission claim.
// Keep it separate from the local, truncated Taylor-jet representation.
[[nodiscard]] inline contracts::Json make_internet_polynomial_form(
    const contracts::Json &execution_ir, std::string input_meaning,
    std::string output_meaning) {
  const auto fail = [](const char *reason) {
    throw common::Error(common::ErrorCode::invalid_argument, reason);
  };
  const auto meaningful = [](const std::string &text) {
    return text.size() <= 4096U &&
           text.find_first_not_of(" \t\r\n") != std::string::npos;
  };
  if (!meaningful(input_meaning) || !meaningful(output_meaning)) {
    fail("POLYNOMIAL_FORM_REQUIRES_BOUNDED_SEMANTIC_MEANINGS");
  }
  const auto program = internet_exact_polynomial_program(execution_ir);
  if (program.direct_power_sum) {
    fail("POLYNOMIAL_FORM_REQUIRES_HORNER_CANDIDATE");
  }
  auto coefficients = program.coefficients;
  while (coefficients.size() > 1U && coefficients.back() == 0) {
    coefficients.pop_back();
  }
  contracts::Json normalized = contracts::Json::array();
  for (const auto &coefficient : coefficients) {
    normalized.push_back(coefficient.get_str());
  }
  const auto execution_contract = internet_polynomial_contract(program);
  const contracts::Json mathematical_contract = {
      {"version", "internet-exact-polynomial-behavior-v1"},
      {"numeric_type", "exact_rational"},
      {"coefficients", normalized},
      {"coefficient_order", "ascending_power"},
      {"normalization", "remove_trailing_zero_coefficients_v1"},
      {"input_minimum", execution_contract.at("input_minimum")},
      {"input_maximum", execution_contract.at("input_maximum")},
      {"input_meaning", std::move(input_meaning)},
      {"output_meaning", std::move(output_meaning)}};
  contracts::Json form = {
      {"schema_version", 1},
      {"representation_version", internet_polynomial_form_version},
      {"representation_kind", "FULL_EXACT_RATIONAL_POLYNOMIAL"},
      {"mathematical_contract", mathematical_contract},
      {"mathematical_signature", contracts::sha256_json(mathematical_contract)},
      {"execution_ir", execution_ir},
      {"execution_ir_sha256", contracts::sha256_json(execution_ir)},
      {"execution_contract", execution_contract},
      {"execution_contract_sha256", contracts::sha256_json(execution_contract)}};
  form["representation_signature"] = contracts::sha256_json(form);
  return form;
}

// Regenerate every field, including all signatures. Unknown fields and claimed
// admission/independence flags are rejected, not silently imported as evidence.
[[nodiscard]] inline contracts::Json read_internet_polynomial_form(
    const contracts::Json &form) {
  const auto invalid = []() {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "INVALID_FULL_POLYNOMIAL_REPRESENTATION");
  };
  if (!form.is_object() || form.dump().size() > 131072U ||
      !form.contains("execution_ir") ||
      !form.contains("mathematical_contract") ||
      !form.at("mathematical_contract").is_object()) {
    invalid();
  }
  const auto &contract = form.at("mathematical_contract");
  if (!contract.contains("input_meaning") ||
      !contract.at("input_meaning").is_string() ||
      !contract.contains("output_meaning") ||
      !contract.at("output_meaning").is_string()) {
    invalid();
  }
  const auto expected = make_internet_polynomial_form(
      form.at("execution_ir"), contract.at("input_meaning").get<std::string>(),
      contract.at("output_meaning").get<std::string>());
  if (form != expected) {
    invalid();
  }
  return expected;
}

} // namespace statewright::egcf
