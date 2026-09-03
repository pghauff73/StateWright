#include "statewright/saa/nonlinear_remainder.hpp"

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
using Powers = std::vector<int>;
using PolynomialMap = std::map<Powers, mpq_class>;

[[noreturn]] void remainder_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

[[nodiscard]] mpq_class exact_value(const NumericCoefficient &value,
                                    std::string_view label) {
  if (!value.exact()) {
    remainder_error(std::string(label) + " must be exact and cannot be float");
  }
  return value.value();
}

[[nodiscard]] int degree(const Powers &powers) {
  int result = 0;
  for (const int power : powers) {
    result += power;
  }
  return result;
}

[[nodiscard]] Json rationals_json(const std::vector<mpq_class> &values) {
  Json result = Json::array();
  for (const auto &value : values) {
    result.push_back(rational_json(value));
  }
  return result;
}

[[nodiscard]] mpq_class rational_power(mpq_class value, int exponent) {
  mpq_class result = 1;
  for (int index = 0; index < exponent; ++index) {
    result *= value;
  }
  return result;
}

[[nodiscard]] mpz_class factorial(int value) {
  if (value < 0) {
    remainder_error("factorial requires a non-negative value");
  }
  mpz_class result;
  mpz_fac_ui(result.get_mpz_t(), static_cast<unsigned long>(value));
  return result;
}

[[nodiscard]] mpz_class binomial(int power, int alpha) {
  if (power < 0 || alpha < 0 || alpha > power) {
    remainder_error("invalid polynomial binomial exponent");
  }
  mpz_class result;
  mpz_bin_uiui(result.get_mpz_t(), static_cast<unsigned long>(power),
               static_cast<unsigned long>(alpha));
  return result;
}

[[nodiscard]] Powers add_powers(const Powers &left, const Powers &right) {
  Powers result(left.size());
  for (std::size_t index = 0; index < left.size(); ++index) {
    result[index] = left[index] + right[index];
  }
  return result;
}

[[nodiscard]] PolynomialMap multiply(const PolynomialMap &left,
                                     const PolynomialMap &right) {
  PolynomialMap result;
  for (const auto &[left_powers, left_coefficient] : left) {
    for (const auto &[right_powers, right_coefficient] : right) {
      result[add_powers(left_powers, right_powers)] +=
          left_coefficient * right_coefficient;
    }
  }
  for (auto iterator = result.begin(); iterator != result.end();) {
    if (iterator->second == 0) {
      iterator = result.erase(iterator);
    } else {
      ++iterator;
    }
  }
  if (result.size() > max_remainder_terms) {
    remainder_error(
        "SAA-7.9 expansion exceeds bounded remainder term budget");
  }
  return result;
}

[[nodiscard]] PolynomialMap shift_monomial(
    const Powers &powers, const std::vector<mpq_class> &center,
    const mpq_class &coefficient) {
  PolynomialMap result;
  result[Powers(center.size(), 0)] = coefficient;
  for (std::size_t coordinate = 0; coordinate < powers.size(); ++coordinate) {
    PolynomialMap factor;
    for (int local_power = 0; local_power <= powers[coordinate];
         ++local_power) {
      Powers local(center.size(), 0);
      local[coordinate] = local_power;
      factor[std::move(local)] =
          mpq_class(binomial(powers[coordinate], local_power)) *
          rational_power(center[coordinate],
                         powers[coordinate] - local_power);
    }
    result = multiply(result, factor);
  }
  return result;
}

[[nodiscard]] std::vector<PolynomialMap>
shift_system(const ExactPolynomialSystem &system,
             const std::vector<mpq_class> &center) {
  if (system.input_count != center.size()) {
    remainder_error(
        "SAA-7.9 polynomial system dimension differs from Taylor center");
  }
  std::vector<PolynomialMap> outputs(system.output_count);
  for (const auto &item : system.terms) {
    if (item.output_index >= system.output_count) {
      remainder_error(
          "SAA-7.9 polynomial output index outside output dimension");
    }
    if (item.powers.size() != system.input_count ||
        std::ranges::any_of(item.powers,
                            [](int value) { return value < 0; })) {
      remainder_error("SAA-7.9 invalid polynomial multi-index");
    }
    const auto coefficient =
        exact_value(item.coefficient, "SAA-7.9 polynomial coefficient");
    const auto shifted = shift_monomial(item.powers, center, coefficient);
    auto &target = outputs[item.output_index];
    for (const auto &[powers, value] : shifted) {
      target[powers] += value;
      if (target[powers] == 0) {
        target.erase(powers);
      }
    }
  }
  return outputs;
}

[[nodiscard]] std::vector<PolynomialMap>
jet_maps(const CanonicalTaylorJet &jet) {
  std::vector<PolynomialMap> result(jet.output_count);
  for (const auto &term : jet.terms) {
    result[term.output_index][term.powers] = term.coefficient;
  }
  return result;
}

[[nodiscard]] mpq_class box_bound(const PolynomialMap &polynomial,
                                  const std::vector<mpq_class> &radius) {
  mpq_class bound = 0;
  for (const auto &[powers, coefficient] : polynomial) {
    mpq_class term = abs(coefficient);
    for (std::size_t index = 0; index < powers.size(); ++index) {
      if (powers[index] != 0) {
        term *= rational_power(radius[index], powers[index]);
      }
    }
    bound += term;
  }
  return bound;
}

[[nodiscard]] std::string validate_digest(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n\f\v");
  if (first == std::string::npos) {
    remainder_error(
        "SAA-7.9 derivative remainder source snapshot must be SHA-256");
  }
  const auto last = value.find_last_not_of(" \t\r\n\f\v");
  value = value.substr(first, last - first + 1U);
  std::ranges::transform(value, value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  if (value.size() != 64U ||
      std::ranges::any_of(value, [](char character) {
        return !((character >= '0' && character <= '9') ||
                 (character >= 'a' && character <= 'f'));
      })) {
    remainder_error(
        "SAA-7.9 derivative remainder source snapshot must be SHA-256");
  }
  return value;
}

} // namespace

Json to_json(const DerivativeRemainderTerm &value) {
  return {{"absolute_upper", rational_json(value.absolute_upper.value())},
          {"output_index", value.output_index},
          {"powers", value.powers}};
}

Json to_json(const ValidatedTaylorRemainder &value) {
  return {{"center", rationals_json(value.center)},
          {"certificate_signature", value.certificate_signature},
          {"exact_containment", value.exact_containment},
          {"global_equivalence_eligible", value.global_equivalence_eligible},
          {"independent_validation", value.independent_validation},
          {"order", value.order},
          {"output_absolute_upper",
           rationals_json(value.output_absolute_upper)},
          {"parent_jet_signature", value.parent_jet_signature},
          {"proof_kind", value.proof_kind},
          {"remainder_version", value.remainder_version},
          {"schema_version", value.schema_version},
          {"source_snapshot_hash", value.source_snapshot_hash},
          {"validity_radius", rationals_json(value.validity_radius)},
          {"warnings", value.warnings}};
}

Json to_json(const LocalBehaviorDeltaBound &value) {
  return {{"center", rationals_json(value.center)},
          {"exact_zero_difference", value.exact_zero_difference},
          {"global_equivalence_eligible", value.global_equivalence_eligible},
          {"local_equivalence_eligible", value.local_equivalence_eligible},
          {"output_absolute_upper",
           rationals_json(value.output_absolute_upper)},
          {"overlap_radius", rationals_json(value.overlap_radius)},
          {"signature", value.signature},
          {"status", value.status}};
}

ValidatedTaylorRemainder certify_polynomial_remainder(
    const GovernedJetEvidence &evidence,
    const ExactPolynomialSystem &full_polynomial) {
  if (!evidence.jet.has_value()) {
    remainder_error(
        "SAA-7.9 polynomial remainder requires exact governed jet evidence");
  }
  if (!evidence.exact || !evidence.canonical_local_eligible) {
    remainder_error(
        "SAA-7.9 cannot certify exact remainder from non-qualified evidence");
  }
  const auto &jet = *evidence.jet;
  if (full_polynomial.input_count != jet.input_count ||
      full_polynomial.output_count != jet.output_count) {
    remainder_error(
        "SAA-7.9 full polynomial dimensions differ from governed jet");
  }
  const auto shifted = shift_system(full_polynomial, jet.center);
  const auto retained = jet_maps(jet);
  std::vector<PolynomialMap> residuals;
  for (std::size_t output = 0; output < jet.output_count; ++output) {
    PolynomialMap expected_low;
    for (const auto &[powers, coefficient] : shifted[output]) {
      if (degree(powers) <= static_cast<int>(jet.order)) {
        expected_low[powers] = coefficient;
      }
    }
    std::set<Powers> keys;
    for (const auto &[powers, coefficient] : expected_low) {
      static_cast<void>(coefficient);
      keys.insert(powers);
    }
    for (const auto &[powers, coefficient] : retained[output]) {
      static_cast<void>(coefficient);
      keys.insert(powers);
    }
    for (const auto &powers : keys) {
      const auto expected = expected_low.find(powers);
      const auto actual = retained[output].find(powers);
      const mpq_class expected_value =
          expected == expected_low.end() ? mpq_class(0) : expected->second;
      const mpq_class actual_value =
          actual == retained[output].end() ? mpq_class(0) : actual->second;
      if (expected_value != actual_value) {
        remainder_error(
            "SAA-7.9 governed jet does not match exact polynomial through retained order");
      }
    }
    PolynomialMap residual;
    for (const auto &[powers, coefficient] : shifted[output]) {
      if (degree(powers) > static_cast<int>(jet.order) && coefficient != 0) {
        residual[powers] = coefficient;
      }
    }
    residuals.push_back(std::move(residual));
  }
  std::vector<mpq_class> bounds;
  bounds.reserve(residuals.size());
  for (const auto &residual : residuals) {
    bounds.push_back(box_bound(residual, jet.validity_radius));
  }
  const Json material =
      {{"bounds", rationals_json(bounds)},
       {"jet", jet.local_behavior_signature},
       {"proof_kind", "EXACT_POLYNOMIAL_RESIDUAL_BOUND"},
       {"source", evidence.source_snapshot_hash},
       {"version", nonlinear_remainder_version}};
  return {.schema_version = 1,
          .remainder_version = std::string(nonlinear_remainder_version),
          .proof_kind = "EXACT_POLYNOMIAL_RESIDUAL_BOUND",
          .parent_jet_signature = jet.local_behavior_signature,
          .center = jet.center,
          .validity_radius = jet.validity_radius,
          .order = jet.order,
          .output_absolute_upper = std::move(bounds),
          .exact_containment = true,
          .source_snapshot_hash = evidence.source_snapshot_hash,
          .independent_validation = evidence.independent_acquisition,
          .global_equivalence_eligible = false,
          .certificate_signature = contracts::sha256_json(material),
          .warnings = {"The enclosure is rigorous for the certified local box but remains local unless a later coverage proof spans the full claimed domain."}};
}

ValidatedTaylorRemainder certify_derivative_remainder(
    const CanonicalTaylorJet &jet,
    const std::vector<DerivativeRemainderTerm> &derivative_bounds,
    std::string source_snapshot_hash, bool independent_validation) {
  if (!jet.exact) {
    remainder_error(
        "SAA-7.9 derivative remainder requires an exact canonical Taylor jet");
  }
  source_snapshot_hash = validate_digest(std::move(source_snapshot_hash));
  if (!independent_validation) {
    remainder_error(
        "SAA-7.9 derivative remainder requires independent validation");
  }
  const int required_degree = static_cast<int>(jet.order) + 1;
  std::vector<mpq_class> bounds(jet.output_count, 0);
  std::set<std::pair<std::size_t, Powers>> seen;
  for (const auto &item : derivative_bounds) {
    const mpq_class upper =
        exact_value(item.absolute_upper, "SAA-7.9 derivative remainder bound");
    if (upper < 0) {
      remainder_error("SAA-7.9 derivative remainder bound cannot be negative");
    }
    if (item.output_index >= jet.output_count) {
      remainder_error("SAA-7.9 derivative remainder output outside dimension");
    }
    if (item.powers.size() != jet.input_count ||
        degree(item.powers) != required_degree) {
      remainder_error(
          "SAA-7.9 derivative remainder requires complete order p+1 multi-indices");
    }
    if (!seen.emplace(item.output_index, item.powers).second) {
      remainder_error(
          "duplicate SAA-7.9 derivative remainder multi-index");
    }
    mpz_class divisor = 1;
    for (const int power : item.powers) {
      divisor *= factorial(power);
    }
    mpq_class contribution = upper / divisor;
    for (std::size_t index = 0; index < item.powers.size(); ++index) {
      if (item.powers[index] != 0) {
        contribution *= rational_power(jet.validity_radius[index],
                                       item.powers[index]);
      }
    }
    bounds[item.output_index] += contribution;
  }
  const Json material =
      {{"bounds", rationals_json(bounds)},
       {"jet", jet.local_behavior_signature},
       {"proof_kind", "VALIDATED_DERIVATIVE_ENVELOPE"},
       {"source", source_snapshot_hash},
       {"version", nonlinear_remainder_version}};
  return {.schema_version = 1,
          .remainder_version = std::string(nonlinear_remainder_version),
          .proof_kind = "VALIDATED_DERIVATIVE_ENVELOPE",
          .parent_jet_signature = jet.local_behavior_signature,
          .center = jet.center,
          .validity_radius = jet.validity_radius,
          .order = jet.order,
          .output_absolute_upper = std::move(bounds),
          .exact_containment = true,
          .source_snapshot_hash = std::move(source_snapshot_hash),
          .independent_validation = true,
          .global_equivalence_eligible = false,
          .certificate_signature = contracts::sha256_json(material),
          .warnings = {"Derivative envelopes must be valid throughout the entire certified local box; the certificate does not infer that validity from point samples."}};
}

LocalBehaviorDeltaBound bound_local_behavior_difference(
    const CanonicalTaylorJet &left, const CanonicalTaylorJet &right,
    const ValidatedTaylorRemainder &left_remainder,
    const ValidatedTaylorRemainder &right_remainder) {
  if (left.input_count != right.input_count ||
      left.output_count != right.output_count) {
    remainder_error("SAA-7.9 local comparison dimension mismatch");
  }
  if (left.center != right.center) {
    remainder_error(
        "SAA-7.9 local behavior bound currently requires a common exact expansion point");
  }
  if (left_remainder.parent_jet_signature != left.local_behavior_signature) {
    remainder_error("left SAA-7.9 remainder does not belong to left jet");
  }
  if (right_remainder.parent_jet_signature != right.local_behavior_signature) {
    remainder_error("right SAA-7.9 remainder does not belong to right jet");
  }
  if (left_remainder.output_absolute_upper.size() != left.output_count ||
      right_remainder.output_absolute_upper.size() != right.output_count) {
    remainder_error("SAA-7.9 remainder output dimension mismatch");
  }
  std::vector<mpq_class> overlap;
  overlap.reserve(left.validity_radius.size());
  for (std::size_t index = 0; index < left.validity_radius.size(); ++index) {
    overlap.push_back(
        std::min(left.validity_radius[index], right.validity_radius[index]));
  }
  const auto left_map = jet_maps(left);
  const auto right_map = jet_maps(right);
  std::vector<mpq_class> output_bounds;
  output_bounds.reserve(left.output_count);
  for (std::size_t output = 0; output < left.output_count; ++output) {
    PolynomialMap difference;
    std::set<Powers> keys;
    for (const auto &[powers, coefficient] : left_map[output]) {
      static_cast<void>(coefficient);
      keys.insert(powers);
    }
    for (const auto &[powers, coefficient] : right_map[output]) {
      static_cast<void>(coefficient);
      keys.insert(powers);
    }
    for (const auto &powers : keys) {
      const auto left_found = left_map[output].find(powers);
      const auto right_found = right_map[output].find(powers);
      const mpq_class left_value =
          left_found == left_map[output].end() ? mpq_class(0)
                                               : left_found->second;
      const mpq_class right_value =
          right_found == right_map[output].end() ? mpq_class(0)
                                                 : right_found->second;
      if (left_value != right_value) {
        difference[powers] = left_value - right_value;
      }
    }
    output_bounds.push_back(
        box_bound(difference, overlap) +
        left_remainder.output_absolute_upper[output] +
        right_remainder.output_absolute_upper[output]);
  }
  const bool exact_zero = std::ranges::all_of(
      output_bounds, [](const mpq_class &value) { return value == 0; });
  const std::string status =
      exact_zero ? "EXACT_LOCAL_BEHAVIOR_MATCH_WITH_VALIDATED_REMAINDER"
                 : "VALIDATED_LOCAL_BEHAVIOR_DELTA_BOUND";
  const Json material =
      {{"bounds", rationals_json(output_bounds)},
       {"left", left.local_behavior_signature},
       {"overlap", rationals_json(overlap)},
       {"right", right.local_behavior_signature},
       {"version", nonlinear_remainder_version}};
  return {.status = status,
          .center = left.center,
          .overlap_radius = std::move(overlap),
          .output_absolute_upper = std::move(output_bounds),
          .exact_zero_difference = exact_zero,
          .local_equivalence_eligible = exact_zero,
          .global_equivalence_eligible = false,
          .signature = contracts::sha256_json(material)};
}

} // namespace statewright::saa
