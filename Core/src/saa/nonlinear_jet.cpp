#include "statewright/saa/nonlinear_jet.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <map>
#include <numeric>
#include <set>
#include <tuple>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[noreturn]] void nonlinear_jet_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

void validate_parent(const CanonicalRepresentativeAlgorithmForm &form) {
  if (!form.canonical_admission_eligible) {
    nonlinear_jet_error(
        "SAA-7 requires a canonically eligible SAA-6 representative form");
  }
  if (form.store_status != "ELIGIBLE_CANONICAL_REPRESENTATIVE_FORM") {
    nonlinear_jet_error(
        "SAA-7 parent form is not eligible canonical representative knowledge");
  }
}

[[nodiscard]] mpq_class exact_value(const NumericCoefficient &value,
                                    std::string_view label) {
  if (!value.exact()) {
    nonlinear_jet_error(std::string(label) +
                        " must use an exact integer/Fraction/rational string, not float");
  }
  return value.value();
}

[[nodiscard]] Json rationals_json(const std::vector<mpq_class> &values) {
  Json result = Json::array();
  for (const auto &value : values) {
    result.push_back(rational_json(value));
  }
  return result;
}

[[nodiscard]] Json terms_json(
    const std::vector<CanonicalTaylorJetTerm> &terms) {
  Json result = Json::array();
  for (const auto &term : terms) {
    result.push_back(to_json(term));
  }
  return result;
}

[[nodiscard]] Json coupling_terms_json(
    const std::vector<NonlinearCouplingTerm> &terms) {
  Json result = Json::array();
  for (const auto &term : terms) {
    result.push_back(to_json(term));
  }
  return result;
}

[[nodiscard]] std::vector<CanonicalTaylorJetTerm>
canonical_terms(const TaylorJetSpec &spec) {
  if (spec.terms.size() > max_jet_terms) {
    nonlinear_jet_error("Taylor jet term count exceeds bounded cap " +
                        std::to_string(max_jet_terms));
  }
  using Key = std::pair<std::size_t, std::vector<int>>;
  std::map<Key, mpq_class> accumulator;
  for (const auto &raw : spec.terms) {
    if (raw.output_index >= spec.output_count) {
      nonlinear_jet_error("Taylor jet output index outside output dimension");
    }
    if (raw.powers.size() != spec.input_count) {
      nonlinear_jet_error("Taylor jet multi-index dimension mismatch");
    }
    if (std::ranges::any_of(raw.powers,
                            [](int value) { return value < 0; })) {
      nonlinear_jet_error("Taylor jet powers cannot be negative");
    }
    if (raw.degree() > static_cast<int>(spec.order)) {
      nonlinear_jet_error("Taylor jet term exceeds declared truncation order");
    }
    const mpq_class coefficient =
        exact_value(raw.coefficient, "Taylor jet coefficient");
    if (coefficient != 0) {
      accumulator[{raw.output_index, raw.powers}] += coefficient;
    }
  }
  std::vector<CanonicalTaylorJetTerm> result;
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
  if (result.size() > max_jet_terms) {
    nonlinear_jet_error("canonical Taylor jet term count exceeds cap " +
                        std::to_string(max_jet_terms));
  }
  return result;
}

} // namespace

int TaylorJetTerm::degree() const {
  return std::accumulate(powers.begin(), powers.end(), 0);
}

int CanonicalTaylorJetTerm::degree() const {
  return std::accumulate(powers.begin(), powers.end(), 0);
}

std::vector<mpq_class>
CanonicalTaylorJet::evaluate(const std::vector<NumericCoefficient> &values) const {
  if (values.size() != input_count) {
    nonlinear_jet_error("Taylor jet evaluation input dimension mismatch");
  }
  std::vector<mpq_class> exact_values;
  exact_values.reserve(values.size());
  for (std::size_t index = 0; index < values.size(); ++index) {
    exact_values.push_back(
        exact_value(values[index], "Taylor jet input " + std::to_string(index)));
    if (abs(exact_values.back() - center[index]) > validity_radius[index]) {
      nonlinear_jet_error("Taylor jet input " + std::to_string(index) +
                          " lies outside the qualified local validity box");
    }
  }
  std::vector<mpq_class> delta(input_count);
  for (std::size_t index = 0; index < input_count; ++index) {
    delta[index] = exact_values[index] - center[index];
  }
  std::vector<mpq_class> outputs(output_count, 0);
  for (const auto &term : terms) {
    mpq_class monomial = term.coefficient;
    for (std::size_t index = 0; index < term.powers.size(); ++index) {
      for (int power = 0; power < term.powers[index]; ++power) {
        monomial *= delta[index];
      }
    }
    outputs[term.output_index] += monomial;
  }
  return outputs;
}

Json to_json(const TaylorJetTerm &value) {
  return {{"coefficient", rational_json(value.coefficient.value())},
          {"degree", value.degree()},
          {"output_index", value.output_index},
          {"powers", value.powers}};
}

Json to_json(const CanonicalTaylorJetTerm &value) {
  return {{"coefficient", rational_json(value.coefficient)},
          {"degree", value.degree()},
          {"output_index", value.output_index},
          {"powers", value.powers}};
}

Json to_json(const NonlinearCouplingTerm &value) {
  return {{"coefficient", rational_json(value.coefficient)},
          {"input_support", value.input_support},
          {"output_index", value.output_index},
          {"powers", value.powers},
          {"reason", value.reason}};
}

Json to_json(const NonlinearCouplingAssessment &value) {
  return {{"coupling_score", value.coupling_score},
          {"cross_terms", coupling_terms_json(value.cross_terms)},
          {"dependency_by_input", value.dependency_by_input},
          {"off_pair_terms", coupling_terms_json(value.off_pair_terms)},
          {"representative", value.representative},
          {"signature", value.signature},
          {"status", value.status}};
}

Json to_json(const CanonicalTaylorJet &value) {
  return {{"center", rationals_json(value.center)},
          {"coefficient_signature", value.coefficient_signature},
          {"coupling", to_json(value.coupling)},
          {"exact", value.exact},
          {"global_equivalence_eligible", value.global_equivalence_eligible},
          {"input_count", value.input_count},
          {"jet_version", value.jet_version},
          {"local_behavior_signature", value.local_behavior_signature},
          {"local_equivalence_scope", value.local_equivalence_scope},
          {"order", value.order},
          {"output_count", value.output_count},
          {"parent_representative_behavior_signature",
           value.parent_representative_behavior_signature},
          {"parent_semantic_signature", value.parent_semantic_signature},
          {"schema_version", value.schema_version},
          {"scope_signature", value.scope_signature},
          {"terms", terms_json(value.terms)},
          {"validity_radius", rationals_json(value.validity_radius)},
          {"warnings", value.warnings}};
}

Json to_json(const LocalJetComparison &value) {
  return {{"coefficient_match", value.coefficient_match},
          {"global_equivalence_eligible", value.global_equivalence_eligible},
          {"overlap_radius", rationals_json(value.overlap_radius)},
          {"same_expansion_point", value.same_expansion_point},
          {"signature", value.signature},
          {"status", value.status}};
}

NonlinearCouplingAssessment assess_nonlinear_coupling(
    const CanonicalRepresentativeAlgorithmForm &form,
    const std::vector<CanonicalTaylorJetTerm> &terms) {
  validate_parent(form);
  std::map<std::size_t, std::size_t> pairing;
  for (const auto &input : form.inputs) {
    pairing.emplace(input.canonical_position, input.paired_output_index);
  }
  std::vector<std::set<std::size_t>> dependencies(
      form.representative_input_count);
  std::vector<NonlinearCouplingTerm> cross_terms;
  std::vector<NonlinearCouplingTerm> off_pair_terms;
  std::set<std::tuple<std::size_t, std::vector<int>, std::vector<std::size_t>,
                      std::string>>
      seen_off_pair;
  for (const auto &term : terms) {
    if (term.degree() == 0) {
      continue;
    }
    std::vector<std::size_t> support;
    for (std::size_t index = 0; index < term.powers.size(); ++index) {
      if (term.powers[index] != 0) {
        support.push_back(index);
        dependencies[index].insert(term.output_index);
      }
    }
    if (support.size() > 1U) {
      cross_terms.push_back({.output_index = term.output_index,
                             .powers = term.powers,
                             .coefficient = term.coefficient,
                             .input_support = support,
                             .reason = "MULTI_INPUT_NONLINEAR_INTERACTION"});
    }
    for (const auto input_index : support) {
      const auto paired = pairing.find(input_index);
      if (paired != pairing.end() && paired->second != term.output_index) {
        NonlinearCouplingTerm item{
            .output_index = term.output_index,
            .powers = term.powers,
            .coefficient = term.coefficient,
            .input_support = {input_index},
            .reason = "INPUT_AFFECTS_NON_PAIRED_OUTPUT"};
        const auto key = std::make_tuple(item.output_index, item.powers,
                                         item.input_support, item.reason);
        if (seen_off_pair.insert(key).second) {
          off_pair_terms.push_back(std::move(item));
        }
      }
    }
  }
  std::vector<std::vector<std::size_t>> dependency_by_input;
  dependency_by_input.reserve(dependencies.size());
  for (const auto &dependency : dependencies) {
    dependency_by_input.emplace_back(dependency.begin(), dependency.end());
  }
  const std::size_t score = cross_terms.size() + off_pair_terms.size();
  const bool representative = score == 0U;
  const Json material =
      {{"coupling_score", score},
       {"cross_terms", coupling_terms_json(cross_terms)},
       {"dependency_by_input", dependency_by_input},
       {"jet_version", nonlinear_jet_version},
       {"off_pair_terms", coupling_terms_json(off_pair_terms)},
       {"parent_representative_behavior_signature",
        form.representative_behavior_signature},
       {"schema_version", 1}};
  return {.status = representative ? "NONLINEAR_REPRESENTATIVE"
                                   : "NONLINEAR_SEMANTIC_MISREPRESENTATION",
          .dependency_by_input = std::move(dependency_by_input),
          .cross_terms = std::move(cross_terms),
          .off_pair_terms = std::move(off_pair_terms),
          .coupling_score = score,
          .representative = representative,
          .signature = contracts::sha256_json(material)};
}

CanonicalTaylorJet canonicalize_taylor_jet(
    const CanonicalRepresentativeAlgorithmForm &form, const TaylorJetSpec &spec) {
  validate_parent(form);
  if (spec.input_count != form.representative_input_count) {
    nonlinear_jet_error(
        "SAA-7 jet input count must match SAA-6 representative input count");
  }
  if (spec.output_count != form.output_count) {
    nonlinear_jet_error("SAA-7 jet output count must match SAA-6 output count");
  }
  if (spec.input_count > max_jet_inputs) {
    nonlinear_jet_error("SAA-7 input dimension exceeds cap " +
                        std::to_string(max_jet_inputs));
  }
  if (spec.output_count < 1U || spec.output_count > max_jet_outputs) {
    nonlinear_jet_error("SAA-7 output dimension exceeds cap " +
                        std::to_string(max_jet_outputs));
  }
  if (spec.order < 1U || spec.order > max_jet_order) {
    nonlinear_jet_error("SAA-7 Taylor order must lie in [1," +
                        std::to_string(max_jet_order) + "]");
  }
  if (spec.center.size() != spec.input_count ||
      spec.validity_radius.size() != spec.input_count) {
    nonlinear_jet_error("SAA-7 center/radius dimension mismatch");
  }
  std::vector<mpq_class> center;
  std::vector<mpq_class> radius;
  center.reserve(spec.input_count);
  radius.reserve(spec.input_count);
  for (std::size_t index = 0; index < spec.input_count; ++index) {
    center.push_back(exact_value(spec.center[index],
                                 "Taylor center " + std::to_string(index)));
    radius.push_back(exact_value(
        spec.validity_radius[index],
        "Taylor validity radius " + std::to_string(index)));
    if (center.back() < 0 || center.back() > 1) {
      nonlinear_jet_error(
          "SAA-7 expansion points must lie in normalized [0,1] coordinates");
    }
    if (radius.back() <= 0) {
      nonlinear_jet_error(
          "SAA-7 requires a positive exact local validity radius");
    }
    if (center.back() - radius.back() < 0 ||
        center.back() + radius.back() > 1) {
      nonlinear_jet_error("SAA-7 local validity box for coordinate " +
                          std::to_string(index) +
                          " leaves normalized [0,1]");
    }
  }
  const auto terms = canonical_terms(spec);
  const Json coefficient_payload =
      {{"center", rationals_json(center)},
       {"claim_scope", "EXACT_FINITE_TAYLOR_JET_AT_FIXED_REPRESENTATIVE_POINT"},
       {"input_count", spec.input_count},
       {"jet_version", nonlinear_jet_version},
       {"order", spec.order},
       {"output_count", spec.output_count},
       {"parent_representative_behavior_signature",
        form.representative_behavior_signature},
       {"parent_semantic_signature", form.semantic_representative_signature},
       {"schema_version", 1},
       {"terms", terms_json(terms)}};
  const std::string coefficient_signature =
      contracts::sha256_json(coefficient_payload);
  const Json scope_payload =
      {{"coefficient_signature", coefficient_signature},
       {"jet_version", nonlinear_jet_version},
       {"schema_version", 1},
       {"scope", "LOCAL_BOX_ONLY"},
       {"validity_radius", rationals_json(radius)}};
  const std::string scope_signature = contracts::sha256_json(scope_payload);
  auto coupling = assess_nonlinear_coupling(form, terms);
  const Json behavior_payload =
      {{"coefficient_signature", coefficient_signature},
       {"coupling_signature", coupling.signature},
       {"jet_version", nonlinear_jet_version},
       {"schema_version", 1},
       {"scope_signature", scope_signature}};
  std::vector<std::string> warnings = {
      "A finite Taylor jet certifies only local truncated behavior around its exact expansion point; it never proves global nonlinear equivalence."};
  if (std::ranges::none_of(terms,
                           [](const auto &term) { return term.degree() >= 2; })) {
    warnings.emplace_back(
        "Jet contains no nonlinear term at the declared truncation order.");
  }
  return {.schema_version = 1,
          .jet_version = std::string(nonlinear_jet_version),
          .parent_representative_behavior_signature =
              form.representative_behavior_signature,
          .parent_semantic_signature = form.semantic_representative_signature,
          .input_count = spec.input_count,
          .output_count = spec.output_count,
          .order = spec.order,
          .center = std::move(center),
          .validity_radius = std::move(radius),
          .terms = terms,
          .coefficient_signature = coefficient_signature,
          .scope_signature = scope_signature,
          .local_behavior_signature =
              contracts::sha256_json(behavior_payload),
          .coupling = std::move(coupling),
          .local_equivalence_scope = "LOCAL_TRUNCATED_JET_ONLY",
          .global_equivalence_eligible = false,
          .exact = true,
          .warnings = std::move(warnings)};
}

LocalJetComparison compare_taylor_jets(const CanonicalTaylorJet &left,
                                       const CanonicalTaylorJet &right) {
  const bool same_center = left.center == right.center;
  const bool same_parent =
      left.parent_representative_behavior_signature ==
          right.parent_representative_behavior_signature &&
      left.parent_semantic_signature == right.parent_semantic_signature;
  const bool coefficient_match =
      same_parent && left.coefficient_signature == right.coefficient_signature;
  std::vector<mpq_class> overlap;
  if (same_center && left.input_count == right.input_count) {
    overlap.reserve(left.validity_radius.size());
    for (std::size_t index = 0; index < left.validity_radius.size(); ++index) {
      overlap.push_back(
          std::min(left.validity_radius[index], right.validity_radius[index]));
    }
  }
  std::string status;
  if (coefficient_match && same_center &&
      (!overlap.empty() || left.input_count == 0U)) {
    status = "EXACT_LOCAL_JET_MATCH_ON_INTERSECTION";
  } else if (!same_parent) {
    status = "INCOMPARABLE_PARENT_REPRESENTATION";
  } else if (!same_center) {
    status = "DIFFERENT_EXPANSION_POINT";
  } else {
    status = "DIFFERENT_LOCAL_JET";
  }
  const Json material =
      {{"jet_version", nonlinear_jet_version},
       {"left", left.local_behavior_signature},
       {"overlap_radius", rationals_json(overlap)},
       {"right", right.local_behavior_signature},
       {"schema_version", 1},
       {"status", status}};
  return {.status = std::move(status),
          .coefficient_match = coefficient_match,
          .same_expansion_point = same_center,
          .overlap_radius = std::move(overlap),
          .global_equivalence_eligible = false,
          .signature = contracts::sha256_json(material)};
}

} // namespace statewright::saa
