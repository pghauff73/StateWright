#include "statewright/saa/nonlinear_global.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;
using Powers = std::vector<int>;
using PolynomialKey = std::pair<std::size_t, Powers>;
using CanonicalPolynomial = std::vector<std::pair<PolynomialKey, mpq_class>>;

[[noreturn]] void global_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

[[nodiscard]] mpq_class exact_value(const NumericCoefficient &value,
                                    std::string_view label) {
  if (!value.exact()) {
    global_error(std::string(label) + " must be exact and cannot be float");
  }
  return value.value();
}

[[nodiscard]] std::string trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n\f\v");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n\f\v");
  return value.substr(first, last - first + 1U);
}

[[nodiscard]] Json rationals_json(const std::vector<mpq_class> &values) {
  Json result = Json::array();
  for (const auto &value : values) {
    result.push_back(rational_json(value));
  }
  return result;
}

[[nodiscard]] std::vector<mpq_class>
exact_values(const std::vector<NumericCoefficient> &values,
             std::string_view label) {
  std::vector<mpq_class> result;
  result.reserve(values.size());
  for (const auto &value : values) {
    result.push_back(exact_value(value, label));
  }
  return result;
}

[[nodiscard]] CanonicalPolynomial
canonical_polynomial(const ExactPolynomialSystem &system) {
  std::map<PolynomialKey, mpq_class> accumulated;
  for (const auto &term : system.terms) {
    if (term.output_index >= system.output_count) {
      global_error("SAA-7.10 polynomial output outside dimension");
    }
    if (term.powers.size() != system.input_count ||
        std::ranges::any_of(term.powers,
                            [](int value) { return value < 0; })) {
      global_error("SAA-7.10 invalid polynomial multi-index");
    }
    accumulated[{term.output_index, term.powers}] +=
        exact_value(term.coefficient, "SAA-7.10 polynomial coefficient");
  }
  CanonicalPolynomial result;
  for (const auto &[key, coefficient] : accumulated) {
    if (coefficient != 0) {
      result.emplace_back(key, coefficient);
    }
  }
  return result;
}

[[nodiscard]] Json polynomial_json(const CanonicalPolynomial &polynomial) {
  Json result = Json::array();
  for (const auto &[key, coefficient] : polynomial) {
    result.push_back({{"coefficient", rational_json(coefficient)},
                      {"output_index", key.first},
                      {"powers", key.second}});
  }
  return result;
}

void validate_cell(const GlobalEquivalenceCell &cell) {
  if (cell.lower.size() != cell.upper.size()) {
    global_error("SAA-7.10 global cell dimension mismatch");
  }
  for (std::size_t index = 0; index < cell.lower.size(); ++index) {
    if (cell.upper[index] <= cell.lower[index]) {
      global_error("SAA-7.10 global cell must have positive width");
    }
  }
  if (std::ranges::any_of(cell.output_delta_upper,
                          [](const mpq_class &value) { return value < 0; })) {
    global_error("SAA-7.10 behavior delta bounds cannot be negative");
  }
}

[[nodiscard]] bool elementary_covered(
    const std::vector<GlobalEquivalenceCell> &cells,
    const std::vector<std::pair<mpq_class, mpq_class>> &elementary) {
  return std::ranges::any_of(cells, [&](const auto &cell) {
    for (std::size_t axis = 0; axis < elementary.size(); ++axis) {
      if (cell.lower[axis] > elementary[axis].first ||
          cell.upper[axis] < elementary[axis].second) {
        return false;
      }
    }
    return true;
  });
}

[[nodiscard]] bool enumerate_cover(
    const std::vector<GlobalEquivalenceCell> &cells,
    const std::vector<std::vector<mpq_class>> &endpoints, std::size_t axis,
    std::vector<std::pair<mpq_class, mpq_class>> &elementary) {
  if (axis == endpoints.size()) {
    return elementary_covered(cells, elementary);
  }
  for (std::size_t index = 0; index + 1U < endpoints[axis].size(); ++index) {
    const auto &low = endpoints[axis][index];
    const auto &high = endpoints[axis][index + 1U];
    if (high <= low) {
      continue;
    }
    elementary.emplace_back(low, high);
    if (!enumerate_cover(cells, endpoints, axis + 1U, elementary)) {
      return false;
    }
    elementary.pop_back();
  }
  return true;
}

[[nodiscard]] bool complete_cover(
    const std::vector<GlobalEquivalenceCell> &cells,
    const std::vector<mpq_class> &lower,
    const std::vector<mpq_class> &upper) {
  std::vector<std::vector<mpq_class>> endpoints;
  endpoints.reserve(lower.size());
  for (std::size_t axis = 0; axis < lower.size(); ++axis) {
    std::set<mpq_class> values = {lower[axis], upper[axis]};
    for (const auto &cell : cells) {
      const auto clipped_low = std::max(lower[axis], cell.lower[axis]);
      const auto clipped_high = std::min(upper[axis], cell.upper[axis]);
      if (clipped_low < clipped_high) {
        values.insert(clipped_low);
        values.insert(clipped_high);
      }
    }
    endpoints.emplace_back(values.begin(), values.end());
  }
  std::size_t elementary_count = 1U;
  for (const auto &values : endpoints) {
    const std::size_t intervals = values.empty() ? 0U : values.size() - 1U;
    if (intervals != 0U &&
        elementary_count > max_global_elementary_cells / intervals) {
      global_error("SAA-7.10 regional coverage partition exceeds bounded cap");
    }
    elementary_count *= intervals;
  }
  if (elementary_count > max_global_elementary_cells) {
    global_error("SAA-7.10 regional coverage partition exceeds bounded cap");
  }
  std::vector<std::pair<mpq_class, mpq_class>> elementary;
  return enumerate_cover(cells, endpoints, 0U, elementary);
}

} // namespace

Json to_json(const GlobalEquivalenceCell &value) {
  return {{"certificate_id", value.certificate_id},
          {"lower", rationals_json(value.lower)},
          {"output_delta_upper", rationals_json(value.output_delta_upper)},
          {"semantic_signature", value.semantic_signature},
          {"upper", rationals_json(value.upper)}};
}

Json to_json(const GlobalNonlinearEquivalenceCertificate &value) {
  return {{"certificate_signature", value.certificate_signature},
          {"claim_scope", value.claim_scope},
          {"complete_domain_coverage", value.complete_domain_coverage},
          {"domain_lower", rationals_json(value.domain_lower)},
          {"domain_upper", rationals_json(value.domain_upper)},
          {"global_equivalence_eligible", value.global_equivalence_eligible},
          {"global_version", value.global_version},
          {"mathematical_equivalence", value.mathematical_equivalence},
          {"output_delta_upper",
           rationals_json(value.output_delta_upper)},
          {"proof_kind", value.proof_kind},
          {"schema_version", value.schema_version},
          {"semantic_equivalence", value.semantic_equivalence},
          {"status", value.status},
          {"warnings", value.warnings}};
}

GlobalNonlinearEquivalenceCertificate
certify_exact_polynomial_global_equivalence(
    const ExactPolynomialSystem &left, const ExactPolynomialSystem &right,
    std::string left_semantic_signature, std::string right_semantic_signature,
    std::vector<NumericCoefficient> domain_lower,
    std::vector<NumericCoefficient> domain_upper) {
  if (left.input_count != right.input_count ||
      left.output_count != right.output_count) {
    global_error(
        "SAA-7.10 exact polynomial systems have incompatible dimensions");
  }
  const std::size_t dimension = left.input_count;
  if (dimension < 1U || dimension > max_global_domain_dimension) {
    global_error("SAA-7.10 exact global proof dimension outside bounded range");
  }
  if (domain_lower.size() != dimension || domain_upper.size() != dimension) {
    global_error("SAA-7.10 exact global domain dimension mismatch");
  }
  auto lower = exact_values(domain_lower, "SAA-7.10 domain lower");
  auto upper = exact_values(domain_upper, "SAA-7.10 domain upper");
  for (std::size_t index = 0; index < dimension; ++index) {
    if (upper[index] <= lower[index]) {
      global_error("SAA-7.10 exact global domain must have positive width");
    }
  }
  const auto left_poly = canonical_polynomial(left);
  const auto right_poly = canonical_polynomial(right);
  const bool math_equal = left_poly == right_poly;
  const bool semantic_equal = !left_semantic_signature.empty() &&
                              left_semantic_signature == right_semantic_signature;
  std::string status;
  bool eligible = false;
  if (math_equal && semantic_equal) {
    status = "EXACT_GLOBAL_POLYNOMIAL_EQUIVALENCE_ON_DOMAIN";
    eligible = true;
  } else if (math_equal) {
    status = "GLOBAL_MATHEMATICAL_MATCH_SEMANTIC_DIFFERENCE";
  } else {
    status = "GLOBAL_POLYNOMIAL_DIFFERENCE";
  }
  std::vector<mpq_class> zeros(left.output_count, 0);
  const Json material =
      {{"domain_lower", rationals_json(lower)},
       {"domain_upper", rationals_json(upper)},
       {"left", polynomial_json(left_poly)},
       {"left_semantic", left_semantic_signature},
       {"proof_kind", "EXACT_POLYNOMIAL_IDENTITY"},
       {"right", polynomial_json(right_poly)},
       {"right_semantic", right_semantic_signature},
       {"status", status},
       {"version", nonlinear_global_version}};
  return {.schema_version = 1,
          .global_version = std::string(nonlinear_global_version),
          .status = std::move(status),
          .claim_scope =
              "EXACT_POLYNOMIAL_INPUT_OUTPUT_IDENTITY_ON_DECLARED_DOMAIN",
          .domain_lower = std::move(lower),
          .domain_upper = std::move(upper),
          .mathematical_equivalence = math_equal,
          .semantic_equivalence = semantic_equal,
          .complete_domain_coverage = true,
          .output_delta_upper = std::move(zeros),
          .global_equivalence_eligible = eligible,
          .proof_kind = "EXACT_POLYNOMIAL_IDENTITY",
          .certificate_signature = contracts::sha256_json(material),
          .warnings = {"This certificate proves equality of the exact polynomial input-output maps and resolved semantics on the declared domain; it does not prove hidden-state realization equivalence outside that claim scope."}};
}

GlobalEquivalenceCell make_global_equivalence_cell(
    std::vector<NumericCoefficient> lower,
    std::vector<NumericCoefficient> upper,
    std::vector<NumericCoefficient> output_delta_upper,
    std::string semantic_signature, std::string certificate_id) {
  GlobalEquivalenceCell result{
      .lower = exact_values(lower, "SAA-7.10 cell lower"),
      .upper = exact_values(upper, "SAA-7.10 cell upper"),
      .output_delta_upper =
          exact_values(output_delta_upper, "SAA-7.10 cell behavior bound"),
      .semantic_signature = trim(std::move(semantic_signature)),
      .certificate_id = trim(std::move(certificate_id))};
  validate_cell(result);
  return result;
}

GlobalNonlinearEquivalenceCertificate certify_regional_global_equivalence(
    std::vector<GlobalEquivalenceCell> cells,
    std::vector<NumericCoefficient> domain_lower,
    std::vector<NumericCoefficient> domain_upper) {
  if (cells.empty() || cells.size() > max_global_cover_cells) {
    global_error(
        "SAA-7.10 regional proof requires a non-empty bounded cell set");
  }
  for (const auto &cell : cells) {
    validate_cell(cell);
  }
  const std::size_t dimension = cells.front().lower.size();
  if (dimension < 1U || dimension > max_global_domain_dimension) {
    global_error("SAA-7.10 regional domain dimension outside bounded range");
  }
  if (std::ranges::any_of(cells, [dimension](const auto &cell) {
        return cell.lower.size() != dimension;
      })) {
    global_error("SAA-7.10 regional cells have inconsistent dimensions");
  }
  auto lower = exact_values(domain_lower, "SAA-7.10 regional domain lower");
  auto upper = exact_values(domain_upper, "SAA-7.10 regional domain upper");
  if (lower.size() != dimension || upper.size() != dimension) {
    global_error("SAA-7.10 regional target domain dimension mismatch");
  }
  for (std::size_t index = 0; index < dimension; ++index) {
    if (upper[index] <= lower[index]) {
      global_error(
          "SAA-7.10 regional target domain must have positive width");
    }
  }
  const bool coverage = complete_cover(cells, lower, upper);
  std::set<std::string> semantics;
  for (const auto &cell : cells) {
    semantics.insert(cell.semantic_signature);
  }
  const bool semantic_equal =
      semantics.size() == 1U && !semantics.contains("");
  const std::size_t output_count = cells.front().output_delta_upper.size();
  if (std::ranges::any_of(cells, [output_count](const auto &cell) {
        return cell.output_delta_upper.size() != output_count;
      })) {
    global_error("SAA-7.10 regional behavior bound dimensions differ");
  }
  std::vector<mpq_class> worst(output_count, 0);
  for (std::size_t output = 0; output < output_count; ++output) {
    worst[output] = cells.front().output_delta_upper[output];
    for (const auto &cell : cells) {
      worst[output] = std::max(worst[output], cell.output_delta_upper[output]);
    }
  }
  const bool zero = std::ranges::all_of(
      worst, [](const mpq_class &value) { return value == 0; });
  std::string status;
  bool eligible = false;
  if (coverage && semantic_equal && zero) {
    status = "CERTIFIED_GLOBAL_EQUIVALENCE_ON_COVERED_DOMAIN";
    eligible = true;
  } else if (coverage && semantic_equal) {
    status = "CERTIFIED_GLOBAL_BEHAVIORAL_DELTA_BOUND";
  } else if (!coverage) {
    status = "GLOBAL_EQUIVALENCE_UNRESOLVED_INCOMPLETE_COVERAGE";
  } else {
    status = "GLOBAL_EQUIVALENCE_UNRESOLVED_SEMANTIC_CHANGE";
  }
  std::ranges::sort(cells, [](const auto &left, const auto &right) {
    return std::tie(left.lower, left.upper, left.certificate_id) <
           std::tie(right.lower, right.upper, right.certificate_id);
  });
  Json cell_json = Json::array();
  for (const auto &cell : cells) {
    cell_json.push_back(to_json(cell));
  }
  const Json material =
      {{"cells", cell_json},
       {"coverage", coverage},
       {"domain_lower", rationals_json(lower)},
       {"domain_upper", rationals_json(upper)},
       {"proof_kind", "FINITE_VALIDATED_REGIONAL_COVER"},
       {"semantic_equal", semantic_equal},
       {"status", status},
       {"version", nonlinear_global_version},
       {"worst", rationals_json(worst)}};
  return {.schema_version = 1,
          .global_version = std::string(nonlinear_global_version),
          .status = std::move(status),
          .claim_scope =
              "VALIDATED_BEHAVIORAL_EQUIVALENCE_ON_FINITE_CERTIFIED_DOMAIN_COVER",
          .domain_lower = std::move(lower),
          .domain_upper = std::move(upper),
          .mathematical_equivalence = coverage && zero,
          .semantic_equivalence = semantic_equal,
          .complete_domain_coverage = coverage,
          .output_delta_upper = std::move(worst),
          .global_equivalence_eligible = eligible,
          .proof_kind = "FINITE_VALIDATED_REGIONAL_COVER",
          .certificate_signature = contracts::sha256_json(material),
          .warnings = {"Global eligibility is restricted to the explicitly covered finite domain. No extrapolation outside the cover is permitted."}};
}

} // namespace statewright::saa
