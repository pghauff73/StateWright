#include "statewright/saa/semantic_units.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[noreturn]] void semantic_units_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument,
                      std::move(message));
}

[[nodiscard]] std::string normalized_text(std::string value) {
  std::istringstream input(value);
  std::ostringstream output;
  std::string word;
  bool first = true;
  while (input >> word) {
    if (!first) {
      output << ' ';
    }
    std::transform(word.begin(), word.end(), word.begin(),
                   [](unsigned char character) {
                     return static_cast<char>(std::tolower(character));
                   });
    output << word;
    first = false;
  }
  return output.str();
}

[[nodiscard]] std::string trimmed(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n\f\v");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n\f\v");
  return value.substr(first, last - first + 1U);
}

[[nodiscard]] std::string uppercase(std::string value) {
  value = trimmed(std::move(value));
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::toupper(character));
                 });
  return value;
}

[[nodiscard]] Json exponent_json(const PhysicalDimensionVector &value) {
  return std::vector<int>(value.exponents.begin(), value.exponents.end());
}

[[nodiscard]] Json fraction_json(const mpq_class &value) {
  if (value.get_num().fits_slong_p() == 0 ||
      value.get_den().fits_slong_p() == 0) {
    semantic_units_error(
        "exact unit fraction exceeds the supported JSON integer range");
  }
  return Json::array({value.get_num().get_si(), value.get_den().get_si()});
}

[[nodiscard]] std::vector<std::string>
normalized_texts(std::vector<std::string> values) {
  std::set<std::string> normalized;
  for (auto &value : values) {
    value = normalized_text(std::move(value));
    if (!value.empty()) {
      normalized.insert(std::move(value));
    }
  }
  return {normalized.begin(), normalized.end()};
}

[[nodiscard]] std::vector<std::string>
normalized_evidence(std::vector<std::string> values) {
  std::set<std::string> normalized;
  for (auto &value : values) {
    value = trimmed(std::move(value));
    if (!value.empty()) {
      normalized.insert(std::move(value));
    }
  }
  return {normalized.begin(), normalized.end()};
}

[[nodiscard]] const std::map<std::string, const PhysicalUnit *> &unit_map() {
  static const std::map<std::string, const PhysicalUnit *> units = {
      {"1", &ONE},
      {"unitless", &ONE},
      {"dimensionless", &ONE},
      {"m", &METRE},
      {"metre", &METRE},
      {"meter", &METRE},
      {"mm", &MILLIMETRE},
      {"millimetre", &MILLIMETRE},
      {"millimeter", &MILLIMETRE},
      {"cm", &CENTIMETRE},
      {"centimetre", &CENTIMETRE},
      {"centimeter", &CENTIMETRE},
      {"km", &KILOMETRE},
      {"kilometre", &KILOMETRE},
      {"kilometer", &KILOMETRE},
      {"s", &SECOND},
      {"second", &SECOND},
      {"kg", &KILOGRAM},
      {"kilogram", &KILOGRAM},
      {"a", &AMPERE},
      {"ampere", &AMPERE},
      {"k", &KELVIN},
      {"kelvin", &KELVIN},
      {"degc", &CELSIUS},
      {"degree celsius", &CELSIUS},
      {"celsius", &CELSIUS},
      {"mol", &MOLE},
      {"mole", &MOLE},
      {"cd", &CANDELA},
      {"candela", &CANDELA},
      {"hz", &HERTZ},
      {"hertz", &HERTZ},
      {"n", &NEWTON},
      {"newton", &NEWTON},
      {"pa", &PASCAL},
      {"pascal", &PASCAL},
      {"j", &JOULE},
      {"joule", &JOULE},
      {"w", &WATT},
      {"watt", &WATT},
      {"c", &COULOMB},
      {"coulomb", &COULOMB},
      {"v", &VOLT},
      {"volt", &VOLT},
      {"ohm", &OHM},
      {"Ω", &OHM},
  };
  return units;
}

} // namespace

const PhysicalDimensionVector DIMENSIONLESS({0, 0, 0, 0, 0, 0, 0});
const PhysicalDimensionVector LENGTH({1, 0, 0, 0, 0, 0, 0});
const PhysicalDimensionVector MASS({0, 1, 0, 0, 0, 0, 0});
const PhysicalDimensionVector TIME({0, 0, 1, 0, 0, 0, 0});
const PhysicalDimensionVector ELECTRIC_CURRENT({0, 0, 0, 1, 0, 0, 0});
const PhysicalDimensionVector TEMPERATURE({0, 0, 0, 0, 1, 0, 0});
const PhysicalDimensionVector AMOUNT({0, 0, 0, 0, 0, 1, 0});
const PhysicalDimensionVector LUMINOUS_INTENSITY({0, 0, 0, 0, 0, 0, 1});

const PhysicalUnit ONE("1", "dimensionless", DIMENSIONLESS);
const PhysicalUnit METRE("m", "metre", LENGTH);
const PhysicalUnit MILLIMETRE("mm", "millimetre", LENGTH,
                              mpq_class(1, 1000));
const PhysicalUnit CENTIMETRE("cm", "centimetre", LENGTH,
                              mpq_class(1, 100));
const PhysicalUnit KILOMETRE("km", "kilometre", LENGTH, mpq_class(1000));
const PhysicalUnit SECOND("s", "second", TIME);
const PhysicalUnit KILOGRAM("kg", "kilogram", MASS);
const PhysicalUnit AMPERE("A", "ampere", ELECTRIC_CURRENT);
const PhysicalUnit KELVIN("K", "kelvin", TEMPERATURE);
const PhysicalUnit CELSIUS("degC", "degree celsius", TEMPERATURE,
                           mpq_class(1), mpq_class(27315, 100));
const PhysicalUnit MOLE("mol", "mole", AMOUNT);
const PhysicalUnit CANDELA("cd", "candela", LUMINOUS_INTENSITY);
const PhysicalUnit HERTZ("Hz", "hertz", TIME ^ -1);
const PhysicalUnit NEWTON("N", "newton", MASS * LENGTH / (TIME ^ 2));
const PhysicalUnit PASCAL("Pa", "pascal", MASS / LENGTH / (TIME ^ 2));
const PhysicalUnit JOULE("J", "joule", MASS * (LENGTH ^ 2) / (TIME ^ 2));
const PhysicalUnit WATT("W", "watt", MASS * (LENGTH ^ 2) / (TIME ^ 3));
const PhysicalUnit COULOMB("C", "coulomb", ELECTRIC_CURRENT * TIME);
const PhysicalUnit VOLT("V", "volt",
                        MASS * (LENGTH ^ 2) / (TIME ^ 3) /
                            ELECTRIC_CURRENT);
const PhysicalUnit OHM("ohm", "ohm",
                       MASS * (LENGTH ^ 2) / (TIME ^ 3) /
                           (ELECTRIC_CURRENT ^ 2));

PhysicalDimensionVector::PhysicalDimensionVector(
    std::array<int, physical_dimension_count> exponents_value)
    : exponents(exponents_value) {}

PhysicalDimensionVector PhysicalDimensionVector::operator*(
    const PhysicalDimensionVector &other) const {
  auto result = exponents;
  for (std::size_t index = 0; index < result.size(); ++index) {
    result[index] += other.exponents[index];
  }
  return PhysicalDimensionVector(result);
}

PhysicalDimensionVector PhysicalDimensionVector::operator/(
    const PhysicalDimensionVector &other) const {
  auto result = exponents;
  for (std::size_t index = 0; index < result.size(); ++index) {
    result[index] -= other.exponents[index];
  }
  return PhysicalDimensionVector(result);
}

PhysicalDimensionVector PhysicalDimensionVector::operator^(int exponent) const {
  auto result = exponents;
  for (auto &value : result) {
    value *= exponent;
  }
  return PhysicalDimensionVector(result);
}

bool PhysicalDimensionVector::dimensionless() const noexcept {
  return std::all_of(exponents.begin(), exponents.end(),
                     [](int value) { return value == 0; });
}

std::string PhysicalDimensionVector::signature() const {
  return contracts::sha256_json(
      {{"base_dimensions",
        std::vector<std::string_view>(si_base_dimensions.begin(),
                                      si_base_dimensions.end())},
       {"exponents", exponent_json(*this)},
       {"version", semantic_units_version}});
}

PhysicalUnit::PhysicalUnit(std::string symbol_value, std::string name_value,
                           PhysicalDimensionVector dimension_value,
                           mpq_class scale_to_si_value,
                           mpq_class offset_to_si_value)
    : symbol(trimmed(std::move(symbol_value))),
      name(trimmed(std::move(name_value))), dimension(dimension_value),
      scale_to_si(std::move(scale_to_si_value)),
      offset_to_si(std::move(offset_to_si_value)) {
  scale_to_si.canonicalize();
  offset_to_si.canonicalize();
  if (symbol.empty() || name.empty()) {
    semantic_units_error("physical unit symbol and name must be non-empty");
  }
  if (scale_to_si <= 0) {
    semantic_units_error("physical unit scale must be positive");
  }
}

std::string PhysicalUnit::canonical_symbol() const { return trimmed(symbol); }

std::string PhysicalUnit::signature() const {
  return contracts::sha256_json(
      {{"dimension", exponent_json(dimension)},
       {"name", normalized_text(name)},
       {"offset_to_si", fraction_json(offset_to_si)},
       {"scale_to_si", fraction_json(scale_to_si)},
       {"symbol", canonical_symbol()},
       {"version", semantic_units_version}});
}

mpq_class PhysicalUnit::to_si(const mpq_class &value) const {
  return value * scale_to_si + offset_to_si;
}

mpq_class PhysicalUnit::from_si(const mpq_class &value) const {
  return (value - offset_to_si) / scale_to_si;
}

const PhysicalUnit &physical_unit(std::string_view identifier) {
  const auto key = normalized_text(std::string(identifier));
  const auto found = unit_map().find(key);
  if (found == unit_map().end()) {
    semantic_units_error("unknown physical unit: '" + std::string(identifier) +
                         "'");
  }
  return *found->second;
}

mpq_class convert_exact_value(const mpq_class &value,
                              const PhysicalUnit &source_unit,
                              const PhysicalUnit &target_unit) {
  if (source_unit.dimension != target_unit.dimension) {
    semantic_units_error(
        "cannot convert between dimensionally incompatible units");
  }
  return target_unit.from_si(source_unit.to_si(value));
}

mpq_class convert_exact_value(const mpq_class &value,
                              std::string_view source_unit,
                              std::string_view target_unit) {
  return convert_exact_value(value, physical_unit(source_unit),
                             physical_unit(target_unit));
}

bool SemanticConcept::physical() const noexcept {
  return physical_dimension.has_value();
}

SemanticConcept make_semantic_concept(
    std::string name, std::string meaning, std::string domain,
    std::string quantity_kind, std::vector<std::string> aliases,
    std::optional<PhysicalDimensionVector> physical_dimension,
    std::optional<PhysicalUnit> canonical_unit,
    std::vector<std::string> evidence_ids, std::string semantic_status) {
  name = normalized_text(std::move(name));
  meaning = normalized_text(std::move(meaning));
  domain = normalized_text(std::move(domain));
  quantity_kind = normalized_text(std::move(quantity_kind));
  if (name.empty() || meaning.empty() || domain.empty() ||
      quantity_kind.empty()) {
    semantic_units_error(
        "semantic concept name, meaning, domain and quantity kind are required");
  }
  if (canonical_unit && !physical_dimension) {
    physical_dimension = canonical_unit->dimension;
  }
  if (canonical_unit && canonical_unit->dimension != physical_dimension) {
    semantic_units_error(
        "semantic concept unit contradicts declared physical dimension");
  }
  aliases = normalized_texts(std::move(aliases));
  evidence_ids = normalized_evidence(std::move(evidence_ids));
  semantic_status = uppercase(std::move(semantic_status));
  const bool eligible = semantic_status == "SEMANTICALLY_RESOLVED" &&
                        !evidence_ids.empty();
  const Json payload = {
      {"aliases", aliases},
      {"canonical_name", name},
      {"canonical_unit_signature",
       canonical_unit ? Json(canonical_unit->signature()) : Json(nullptr)},
      {"domain", domain},
      {"evidence_ids", evidence_ids},
      {"meaning", meaning},
      {"physical_dimension",
       physical_dimension ? exponent_json(*physical_dimension) : Json(nullptr)},
      {"quantity_kind", quantity_kind},
      {"semantic_status", semantic_status},
      {"version", semantic_units_version}};
  return {.canonical_name = std::move(name),
          .meaning = std::move(meaning),
          .domain = std::move(domain),
          .quantity_kind = std::move(quantity_kind),
          .aliases = std::move(aliases),
          .physical_dimension = std::move(physical_dimension),
          .canonical_unit = std::move(canonical_unit),
          .evidence_ids = std::move(evidence_ids),
          .semantic_status = std::move(semantic_status),
          .concept_signature = contracts::sha256_json(payload),
          .canonical_eligible = eligible};
}

DimensionalConstraintAssessment assess_product_dimension(
    const SemanticConcept &output,
    const std::vector<std::pair<SemanticConcept, int>> &factors) {
  if (!output.physical_dimension) {
    semantic_units_error(
        "SAA-9.1 dimensional product assessment requires physical output concept");
  }
  PhysicalDimensionVector observed = DIMENSIONLESS;
  Json factor_payload = Json::array();
  for (const auto &[factor_concept, power] : factors) {
    if (!factor_concept.physical_dimension) {
      semantic_units_error(
          "SAA-9.1 dimensional product factor is not physically dimensioned");
    }
    observed = observed * (*factor_concept.physical_dimension ^ power);
    factor_payload.push_back(
        Json::array({factor_concept.concept_signature, power}));
  }
  const auto expected = *output.physical_dimension;
  const bool dimension_match = observed == expected;
  const Json payload = {{"expected", exponent_json(expected)},
                        {"factors", factor_payload},
                        {"observed", exponent_json(observed)},
                        {"output", output.concept_signature},
                        {"quantity_kind", output.quantity_kind},
                        {"version", semantic_units_version}};
  std::vector<std::string> warnings;
  if (dimension_match) {
    warnings.emplace_back(
        "Dimensional coherence is necessary but not sufficient for semantic equivalence; equal dimensions can represent different quantity kinds.");
  }
  return {.status = dimension_match
                        ? "DIMENSIONALLY_COHERENT"
                        : "DIMENSIONAL_SEMANTIC_MISREPRESENTATION",
          .expected_dimension = expected,
          .observed_dimension = observed,
          .quantity_kind_consistent = true,
          .canonical_semantic_eligible =
              dimension_match && output.canonical_eligible,
          .signature = contracts::sha256_json(payload),
          .warnings = std::move(warnings)};
}

DimensionalConstraintAssessment assess_additive_compatibility(
    const std::vector<SemanticConcept> &concepts) {
  if (concepts.size() < 2U) {
    semantic_units_error(
        "additive semantic compatibility requires at least two concepts");
  }
  if (std::any_of(concepts.begin(), concepts.end(), [](const auto &item) {
        return !item.physical_dimension;
      })) {
    semantic_units_error(
        "additive semantic compatibility requires physical concepts");
  }
  const auto expected = *concepts.front().physical_dimension;
  const bool dimensions_match =
      std::all_of(concepts.begin(), concepts.end(), [&](const auto &item) {
        return item.physical_dimension == expected;
      });
  const bool quantity_match =
      std::all_of(concepts.begin(), concepts.end(), [&](const auto &item) {
        return item.quantity_kind == concepts.front().quantity_kind;
      });
  Json concept_signatures = Json::array();
  for (const auto &item : concepts) {
    concept_signatures.push_back(item.concept_signature);
  }
  const Json payload = {{"concepts", concept_signatures},
                        {"dimensions_match", dimensions_match},
                        {"quantity_match", quantity_match},
                        {"version", semantic_units_version}};
  std::vector<std::string> warnings;
  if (dimensions_match && !quantity_match) {
    warnings.emplace_back(
        "Matching physical dimensions do not make different quantity kinds additive.");
  }
  const bool all_eligible =
      std::all_of(concepts.begin(), concepts.end(), [](const auto &item) {
        return item.canonical_eligible;
      });
  return {.status = dimensions_match && quantity_match
                        ? "ADDITIVELY_SEMANTICALLY_COHERENT"
                        : "ADDITIVE_SEMANTIC_MISREPRESENTATION",
          .expected_dimension = expected,
          .observed_dimension = dimensions_match ? expected : DIMENSIONLESS,
          .quantity_kind_consistent = quantity_match,
          .canonical_semantic_eligible =
              dimensions_match && quantity_match && all_eligible,
          .signature = contracts::sha256_json(payload),
          .warnings = std::move(warnings)};
}

std::string physical_semantic_relation(const SemanticConcept &left,
                                       const SemanticConcept &right) {
  if (!left.physical_dimension || !right.physical_dimension) {
    return "NONPHYSICAL_OR_UNRESOLVED_DIMENSION";
  }
  if (left.physical_dimension != right.physical_dimension) {
    return "DIMENSIONALLY_INCOMPATIBLE";
  }
  if (left.quantity_kind != right.quantity_kind) {
    return "SAME_DIMENSION_DIFFERENT_QUANTITY_KIND";
  }
  if (left.meaning == right.meaning) {
    return "PHYSICALLY_AND_SEMANTICALLY_COMPATIBLE";
  }
  return "PHYSICALLY_COMPATIBLE_MEANING_UNRESOLVED";
}

Json to_json(const PhysicalDimensionVector &value) {
  return {{"base_dimensions",
           std::vector<std::string_view>(si_base_dimensions.begin(),
                                         si_base_dimensions.end())},
          {"dimensionless", value.dimensionless()},
          {"exponents", exponent_json(value)},
          {"signature", value.signature()}};
}

Json to_json(const PhysicalUnit &value) {
  return {{"dimension", to_json(value.dimension)},
          {"name", normalized_text(value.name)},
          {"offset_to_si", fraction_json(value.offset_to_si)},
          {"scale_to_si", fraction_json(value.scale_to_si)},
          {"signature", value.signature()},
          {"symbol", value.canonical_symbol()}};
}

Json to_json(const SemanticConcept &value) {
  return {{"aliases", value.aliases},
          {"canonical_eligible", value.canonical_eligible},
          {"canonical_name", value.canonical_name},
          {"canonical_unit",
           value.canonical_unit ? to_json(*value.canonical_unit)
                                : Json(nullptr)},
          {"concept_signature", value.concept_signature},
          {"domain", value.domain},
          {"evidence_ids", value.evidence_ids},
          {"meaning", value.meaning},
          {"physical", value.physical()},
          {"physical_dimension",
           value.physical_dimension ? to_json(*value.physical_dimension)
                                    : Json(nullptr)},
          {"quantity_kind", value.quantity_kind},
          {"semantic_status", value.semantic_status}};
}

Json to_json(const DimensionalConstraintAssessment &value) {
  return {{"canonical_semantic_eligible",
           value.canonical_semantic_eligible},
          {"expected_dimension", to_json(value.expected_dimension)},
          {"observed_dimension", to_json(value.observed_dimension)},
          {"quantity_kind_consistent", value.quantity_kind_consistent},
          {"signature", value.signature},
          {"status", value.status},
          {"warnings", value.warnings}};
}

} // namespace statewright::saa
