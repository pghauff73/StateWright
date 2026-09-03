#include "statewright/saa/nonlinear_evidence.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <tuple>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[noreturn]] void evidence_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

void validate_parent(const CanonicalRepresentativeAlgorithmForm &form) {
  if (!form.canonical_admission_eligible) {
    evidence_error("SAA-7.2 requires a qualified SAA-6 representative form");
  }
}

[[nodiscard]] mpq_class exact_value(const NumericCoefficient &value,
                                    std::string_view label) {
  if (!value.exact()) {
    evidence_error(std::string(label) + " must be exact and cannot be float");
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

[[nodiscard]] std::string lowercase(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

[[nodiscard]] std::string validate_digest(std::string value,
                                          std::string_view label) {
  value = lowercase(trim(std::move(value)));
  if (value.size() != 64U ||
      std::ranges::any_of(value, [](char character) {
        return !((character >= '0' && character <= '9') ||
                 (character >= 'a' && character <= 'f'));
      })) {
    evidence_error(std::string(label) + " must be an exact SHA-256 digest");
  }
  return value;
}

[[nodiscard]] Json rationals_json(const std::vector<mpq_class> &values) {
  Json result = Json::array();
  for (const auto &value : values) {
    result.push_back(rational_json(value));
  }
  return result;
}

[[nodiscard]] Json polynomial_terms_json(
    const std::vector<ExactPolynomialTerm> &terms) {
  Json result = Json::array();
  for (const auto &term : terms) {
    result.push_back(to_json(term));
  }
  return result;
}

[[nodiscard]] Json estimates_json(
    const std::vector<CanonicalBoundedDerivativeEstimate> &estimates) {
  Json result = Json::array();
  for (const auto &estimate : estimates) {
    result.push_back(to_json(estimate));
  }
  return result;
}

[[nodiscard]] std::pair<std::vector<mpq_class>, std::vector<mpq_class>>
normalize_center_radius(const CanonicalRepresentativeAlgorithmForm &form,
                        const std::vector<NumericCoefficient> &center,
                        const std::vector<NumericCoefficient> &radius) {
  if (center.size() != form.representative_input_count) {
    evidence_error("SAA-7.2 evidence center dimension mismatch");
  }
  if (radius.size() != form.representative_input_count) {
    evidence_error("SAA-7.2 evidence radius dimension mismatch");
  }
  std::vector<mpq_class> exact_center;
  std::vector<mpq_class> exact_radius;
  exact_center.reserve(center.size());
  exact_radius.reserve(radius.size());
  for (std::size_t index = 0; index < center.size(); ++index) {
    exact_center.push_back(
        exact_value(center[index], "evidence center " + std::to_string(index)));
    exact_radius.push_back(
        exact_value(radius[index], "evidence radius " + std::to_string(index)));
    if (exact_center.back() < 0 || exact_center.back() > 1) {
      evidence_error(
          "SAA-7.2 evidence center must lie in normalized [0,1]");
    }
    if (exact_radius.back() <= 0) {
      evidence_error("SAA-7.2 evidence radius must be positive");
    }
    if (exact_center.back() - exact_radius.back() < 0 ||
        exact_center.back() + exact_radius.back() > 1) {
      evidence_error("SAA-7.2 evidence box for coordinate " +
                     std::to_string(index) + " leaves [0,1]");
    }
  }
  return {std::move(exact_center), std::move(exact_radius)};
}

[[nodiscard]] bool qualified_producer(const std::string &producer,
                                      const std::string &method,
                                      bool independent) {
  const std::string normalized = lowercase(trim(producer));
  const std::string method_text = lowercase(trim(method));
  const bool allowed_prefix = normalized.starts_with("deterministic-") ||
                              normalized.starts_with("human-");
  const std::set<std::string> disallowed = {
      "reported", "model-claimed", "model-generated-claim"};
  return independent && allowed_prefix && !method_text.empty() &&
         !disallowed.contains(method_text);
}

[[nodiscard]] int degree(const std::vector<int> &powers) {
  int result = 0;
  for (const int power : powers) {
    result += power;
  }
  return result;
}

[[nodiscard]] mpz_class factorial(int value) {
  if (value < 0) {
    evidence_error("factorial requires a non-negative value");
  }
  mpz_class result;
  mpz_fac_ui(result.get_mpz_t(), static_cast<unsigned long>(value));
  return result;
}

[[nodiscard]] mpq_class rational_power(mpq_class value, int exponent) {
  mpq_class result = 1;
  for (int index = 0; index < exponent; ++index) {
    result *= value;
  }
  return result;
}

[[nodiscard]] mpz_class binomial(int power, int alpha) {
  if (power < 0 || alpha < 0 || alpha > power) {
    evidence_error("invalid polynomial binomial exponent");
  }
  mpz_class result;
  mpz_bin_uiui(result.get_mpz_t(), static_cast<unsigned long>(power),
               static_cast<unsigned long>(alpha));
  return result;
}

[[nodiscard]] std::vector<NumericCoefficient>
inputs_from_exact(const std::vector<mpq_class> &values) {
  std::vector<NumericCoefficient> result;
  result.reserve(values.size());
  for (const auto &value : values) {
    result.emplace_back(value);
  }
  return result;
}

[[nodiscard]] std::vector<ExactPolynomialTerm> validate_polynomial_system(
    const CanonicalRepresentativeAlgorithmForm &form,
    const ExactPolynomialSystem &system) {
  if (system.input_count != form.representative_input_count) {
    evidence_error(
        "symbolic polynomial input dimension mismatches representative form");
  }
  if (system.output_count != form.output_count) {
    evidence_error(
        "symbolic polynomial output dimension mismatches representative form");
  }
  std::vector<ExactPolynomialTerm> canonical;
  for (const auto &raw : system.terms) {
    if (raw.output_index >= form.output_count) {
      evidence_error("symbolic polynomial output index outside output dimension");
    }
    if (raw.powers.size() != form.representative_input_count ||
        std::ranges::any_of(raw.powers,
                            [](int value) { return value < 0; })) {
      evidence_error("symbolic polynomial powers are invalid");
    }
    const auto coefficient =
        exact_value(raw.coefficient, "symbolic polynomial coefficient");
    if (coefficient != 0) {
      canonical.push_back({.output_index = raw.output_index,
                           .powers = raw.powers,
                           .coefficient = coefficient});
    }
  }
  std::ranges::sort(canonical, [](const auto &left, const auto &right) {
    return std::tuple(left.output_index, degree(left.powers), left.powers,
                      left.coefficient.value()) <
           std::tuple(right.output_index, degree(right.powers), right.powers,
                      right.coefficient.value());
  });
  return canonical;
}

[[nodiscard]] std::vector<TaylorJetTerm> expand_polynomial_at_center(
    const std::vector<ExactPolynomialTerm> &terms,
    const std::vector<mpq_class> &center, std::size_t input_count,
    std::size_t order) {
  using Key = std::pair<std::size_t, std::vector<int>>;
  std::map<Key, mpq_class> accumulator;
  for (const auto &term : terms) {
    using Partial = std::pair<std::vector<int>, mpq_class>;
    std::vector<Partial> partial = {{{}, term.coefficient.value()}};
    for (std::size_t input = 0; input < term.powers.size(); ++input) {
      std::vector<Partial> next;
      const int power = term.powers[input];
      for (const auto &[prefix, prefix_coefficient] : partial) {
        for (int alpha = 0; alpha <= power; ++alpha) {
          auto powers = prefix;
          powers.push_back(alpha);
          if (degree(powers) <= static_cast<int>(order)) {
            const mpq_class factor =
                mpq_class(binomial(power, alpha)) *
                rational_power(center[input], power - alpha);
            next.emplace_back(std::move(powers), prefix_coefficient * factor);
          }
        }
      }
      partial = std::move(next);
    }
    for (const auto &[powers, coefficient] : partial) {
      if (powers.size() == input_count &&
          degree(powers) <= static_cast<int>(order) && coefficient != 0) {
        accumulator[{term.output_index, powers}] += coefficient;
      }
    }
  }
  std::vector<TaylorJetTerm> result;
  for (const auto &[key, coefficient] : accumulator) {
    if (coefficient != 0) {
      result.push_back({.output_index = key.first,
                        .powers = key.second,
                        .coefficient = coefficient});
    }
  }
  std::ranges::sort(result, [](const auto &left, const auto &right) {
    return std::tuple(left.output_index, left.degree(), left.powers) <
           std::tuple(right.output_index, right.degree(), right.powers);
  });
  return result;
}

} // namespace

Json to_json(const ExactDerivativeTerm &value) {
  return {{"derivative", rational_json(value.derivative.value())},
          {"output_index", value.output_index},
          {"powers", value.powers}};
}

Json to_json(const ExactPolynomialTerm &value) {
  return {{"coefficient", rational_json(value.coefficient.value())},
          {"output_index", value.output_index},
          {"powers", value.powers}};
}

Json to_json(const ExactPolynomialSystem &value) {
  return {{"input_count", value.input_count},
          {"output_count", value.output_count},
          {"terms", polynomial_terms_json(value.terms)}};
}

Json to_json(const CanonicalBoundedDerivativeEstimate &value) {
  return {{"lower", rational_json(value.lower)},
          {"output_index", value.output_index},
          {"powers", value.powers},
          {"upper", rational_json(value.upper)}};
}

Json to_json(const GovernedJetEvidence &value) {
  return {{"canonical_local_eligible", value.canonical_local_eligible},
          {"center", rationals_json(value.center)},
          {"estimated_derivatives", estimates_json(value.estimated_derivatives)},
          {"evidence_kind", value.evidence_kind},
          {"evidence_signature", value.evidence_signature},
          {"evidence_version", value.evidence_version},
          {"exact", value.exact},
          {"independent_acquisition", value.independent_acquisition},
          {"jet", value.jet ? to_json(*value.jet) : Json(nullptr)},
          {"method", value.method},
          {"order", value.order},
          {"parent_representative_behavior_signature",
           value.parent_representative_behavior_signature},
          {"producer", value.producer},
          {"schema_version", value.schema_version},
          {"source_snapshot_hash", value.source_snapshot_hash},
          {"validity_radius", rationals_json(value.validity_radius)},
          {"warnings", value.warnings}};
}

GovernedJetEvidence acquire_exact_derivative_jet(
    const CanonicalRepresentativeAlgorithmForm &form,
    std::vector<NumericCoefficient> center,
    std::vector<NumericCoefficient> validity_radius, std::size_t order,
    const std::vector<ExactDerivativeTerm> &derivatives,
    std::string source_snapshot_hash, std::string producer, std::string method,
    bool independent_acquisition) {
  validate_parent(form);
  auto [exact_center, radius] =
      normalize_center_radius(form, center, validity_radius);
  source_snapshot_hash = validate_digest(
      std::move(source_snapshot_hash), "SAA-7.2 source snapshot hash");
  if (order < 1U || order > 4U) {
    evidence_error("SAA-7.2 derivative acquisition order must lie in [1,4]");
  }
  std::vector<TaylorJetTerm> canonical_terms;
  Json evidence_rows = Json::array();
  for (const auto &item : derivatives) {
    if (item.output_index >= form.output_count) {
      evidence_error("SAA-7.2 derivative output index outside output dimension");
    }
    if (item.powers.size() != form.representative_input_count) {
      evidence_error("SAA-7.2 derivative multi-index dimension mismatch");
    }
    if (std::ranges::any_of(item.powers,
                            [](int value) { return value < 0; }) ||
        degree(item.powers) > static_cast<int>(order)) {
      evidence_error(
          "SAA-7.2 derivative multi-index outside declared order");
    }
    const mpq_class derivative = exact_value(item.derivative, "exact derivative");
    mpz_class divisor = 1;
    for (const int power : item.powers) {
      divisor *= factorial(power);
    }
    const mpq_class coefficient = derivative / divisor;
    if (coefficient != 0) {
      canonical_terms.push_back({.output_index = item.output_index,
                                 .powers = item.powers,
                                 .coefficient = coefficient});
    }
    evidence_rows.push_back({{"derivative", rational_json(derivative)},
                             {"output_index", item.output_index},
                             {"powers", item.powers}});
  }
  const auto jet = canonicalize_taylor_jet(
      form, {.input_count = form.representative_input_count,
             .output_count = form.output_count,
             .order = order,
             .center = inputs_from_exact(exact_center),
             .validity_radius = inputs_from_exact(radius),
             .terms = std::move(canonical_terms)});
  producer = trim(std::move(producer));
  method = trim(std::move(method));
  const bool eligible =
      qualified_producer(producer, method, independent_acquisition);
  const Json payload =
      {{"center", rationals_json(exact_center)},
       {"derivatives", evidence_rows},
       {"evidence_kind", "EXACT_DERIVATIVE_TABLE"},
       {"evidence_version", nonlinear_evidence_version},
       {"independent_acquisition", independent_acquisition},
       {"jet_local_behavior_signature", jet.local_behavior_signature},
       {"method", method},
       {"order", order},
       {"parent_representative_behavior_signature",
        form.representative_behavior_signature},
       {"producer", producer},
       {"schema_version", 1},
       {"source_snapshot_hash", source_snapshot_hash},
       {"validity_radius", rationals_json(radius)}};
  std::vector<std::string> warnings;
  if (!eligible) {
    warnings.emplace_back(
        "Exact coefficients were acquired, but provenance/independence is insufficient for canonical local admission.");
  }
  return {.schema_version = 1,
          .evidence_version = std::string(nonlinear_evidence_version),
          .evidence_kind = "EXACT_DERIVATIVE_TABLE",
          .parent_representative_behavior_signature =
              form.representative_behavior_signature,
          .source_snapshot_hash = std::move(source_snapshot_hash),
          .producer = std::move(producer),
          .method = std::move(method),
          .independent_acquisition = independent_acquisition,
          .exact = true,
          .canonical_local_eligible = eligible,
          .center = std::move(exact_center),
          .validity_radius = std::move(radius),
          .order = order,
          .jet = jet,
          .estimated_derivatives = {},
          .evidence_signature = contracts::sha256_json(payload),
          .warnings = std::move(warnings)};
}

GovernedJetEvidence acquire_exact_polynomial_jet(
    const CanonicalRepresentativeAlgorithmForm &form,
    const ExactPolynomialSystem &system,
    std::vector<NumericCoefficient> center,
    std::vector<NumericCoefficient> validity_radius, std::size_t order) {
  validate_parent(form);
  auto [exact_center, radius] =
      normalize_center_radius(form, center, validity_radius);
  if (order < 1U || order > 4U) {
    evidence_error("SAA-7.2 symbolic acquisition order must lie in [1,4]");
  }
  const auto terms = validate_polynomial_system(form, system);
  const Json source_payload =
      {{"input_count", form.representative_input_count},
       {"kind", "EXACT_POLYNOMIAL_SYSTEM"},
       {"output_count", form.output_count},
       {"schema_version", 1},
       {"terms", polynomial_terms_json(terms)}};
  const std::string source_digest = contracts::sha256_json(source_payload);
  auto jet_terms = expand_polynomial_at_center(
      terms, exact_center, form.representative_input_count, order);
  const auto jet = canonicalize_taylor_jet(
      form, {.input_count = form.representative_input_count,
             .output_count = form.output_count,
             .order = order,
             .center = inputs_from_exact(exact_center),
             .validity_radius = inputs_from_exact(radius),
             .terms = std::move(jet_terms)});
  const Json evidence_payload =
      {{"center", rationals_json(exact_center)},
       {"evidence_kind", "EXACT_SYMBOLIC_POLYNOMIAL"},
       {"evidence_version", nonlinear_evidence_version},
       {"jet_local_behavior_signature", jet.local_behavior_signature},
       {"method", "exact-binomial-taylor-expansion"},
       {"order", order},
       {"parent_representative_behavior_signature",
        form.representative_behavior_signature},
       {"producer", "deterministic-saa-polynomial-expander"},
       {"schema_version", 1},
       {"source_snapshot_hash", source_digest},
       {"validity_radius", rationals_json(radius)}};
  return {.schema_version = 1,
          .evidence_version = std::string(nonlinear_evidence_version),
          .evidence_kind = "EXACT_SYMBOLIC_POLYNOMIAL",
          .parent_representative_behavior_signature =
              form.representative_behavior_signature,
          .source_snapshot_hash = source_digest,
          .producer = "deterministic-saa-polynomial-expander",
          .method = "exact-binomial-taylor-expansion",
          .independent_acquisition = true,
          .exact = true,
          .canonical_local_eligible = true,
          .center = std::move(exact_center),
          .validity_radius = std::move(radius),
          .order = order,
          .jet = jet,
          .estimated_derivatives = {},
          .evidence_signature = contracts::sha256_json(evidence_payload),
          .warnings = {"Exact symbolic acquisition certifies exact Taylor coefficients to the declared truncation order, not global equality to the truncated jet."}};
}

GovernedJetEvidence acquire_bounded_estimated_derivatives(
    const CanonicalRepresentativeAlgorithmForm &form,
    std::vector<NumericCoefficient> center,
    std::vector<NumericCoefficient> validity_radius, std::size_t order,
    const std::vector<BoundedDerivativeEstimate> &estimates,
    std::string source_snapshot_hash, std::string producer,
    std::string method) {
  validate_parent(form);
  auto [exact_center, radius] =
      normalize_center_radius(form, center, validity_radius);
  source_snapshot_hash = validate_digest(std::move(source_snapshot_hash),
                                         "estimated evidence source hash");
  std::vector<CanonicalBoundedDerivativeEstimate> normalized;
  for (const auto &item : estimates) {
    const mpq_class lower =
        exact_value(item.lower, "estimated derivative lower bound");
    const mpq_class upper =
        exact_value(item.upper, "estimated derivative upper bound");
    if (upper < lower) {
      evidence_error(
          "bounded derivative estimate upper bound is below lower bound");
    }
    if (item.output_index >= form.output_count) {
      evidence_error("estimated derivative output index outside output dimension");
    }
    if (item.powers.size() != form.representative_input_count ||
        std::ranges::any_of(item.powers,
                            [](int value) { return value < 0; })) {
      evidence_error("estimated derivative powers are invalid");
    }
    if (degree(item.powers) > static_cast<int>(order)) {
      evidence_error("estimated derivative exceeds declared order");
    }
    normalized.push_back({.output_index = item.output_index,
                          .powers = item.powers,
                          .lower = lower,
                          .upper = upper});
  }
  std::ranges::sort(normalized, [](const auto &left, const auto &right) {
    return std::tuple(left.output_index, degree(left.powers), left.powers) <
           std::tuple(right.output_index, degree(right.powers), right.powers);
  });
  producer = trim(std::move(producer));
  method = trim(std::move(method));
  const Json payload =
      {{"center", rationals_json(exact_center)},
       {"estimates", estimates_json(normalized)},
       {"evidence_kind", "BOUNDED_ESTIMATED_DERIVATIVES"},
       {"evidence_version", nonlinear_evidence_version},
       {"method", method},
       {"order", order},
       {"parent_representative_behavior_signature",
        form.representative_behavior_signature},
       {"producer", producer},
       {"schema_version", 1},
       {"source_snapshot_hash", source_snapshot_hash},
       {"validity_radius", rationals_json(radius)}};
  return {.schema_version = 1,
          .evidence_version = std::string(nonlinear_evidence_version),
          .evidence_kind = "BOUNDED_ESTIMATED_DERIVATIVES",
          .parent_representative_behavior_signature =
              form.representative_behavior_signature,
          .source_snapshot_hash = std::move(source_snapshot_hash),
          .producer = std::move(producer),
          .method = std::move(method),
          .independent_acquisition = false,
          .exact = false,
          .canonical_local_eligible = false,
          .center = std::move(exact_center),
          .validity_radius = std::move(radius),
          .order = order,
          .jet = std::nullopt,
          .estimated_derivatives = std::move(normalized),
          .evidence_signature = contracts::sha256_json(payload),
          .warnings = {"Estimated or interval derivative evidence is retained for comparison and falsification but cannot enter exact SAA nonlinear identity."}};
}

} // namespace statewright::saa
