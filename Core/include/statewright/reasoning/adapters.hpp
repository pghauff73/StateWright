#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::reasoning {

using AdapterCounterexample =
    std::vector<std::pair<std::string, std::string>>;
using DecimalVariables = std::map<std::string, std::string>;
using DecimalPoints = std::vector<DecimalVariables>;
using FiniteDomains = std::map<std::string, std::vector<std::string>>;

struct AdapterResult final {
  int schema_version = 1;
  std::string adapter;
  std::string claim;
  std::string status = "INCONCLUSIVE";
  std::string result;
  std::string evidence_id;
  std::vector<std::string> details;
  std::string tolerance;
  AdapterCounterexample counterexample;
  std::string signature;
};

[[nodiscard]] const std::vector<std::string> &adapter_statuses();
[[nodiscard]] contracts::Json to_json(const AdapterResult &value);
[[nodiscard]] AdapterResult canonicalize_adapter_result(AdapterResult value);
void require_adapter_result_integrity(const AdapterResult &value);

[[nodiscard]] AdapterResult evaluate_decimal_expression(
    std::string expression, DecimalVariables variables = {},
    int precision = 50);
[[nodiscard]] AdapterResult symbolic_equivalence(std::string left,
                                                 std::string right);
[[nodiscard]] AdapterResult numerical_residual_check(
    std::string left, std::string right, const DecimalPoints &points,
    std::string tolerance = "1e-12");
[[nodiscard]] AdapterResult dimensional_equivalence(
    const std::map<std::string, int> &left_dimensions,
    const std::map<std::string, int> &right_dimensions,
    std::string equation = "equation");
[[nodiscard]] AdapterResult finite_domain_check(
    std::string predicate, const FiniteDomains &domains,
    std::size_t max_combinations = 10'000);

} // namespace statewright::reasoning
