#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <gmpxx.h>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view semantic_units_version =
    "saa-semantic-units-v1";
inline constexpr std::size_t physical_dimension_count = 7U;
inline constexpr std::array<std::string_view, physical_dimension_count>
    si_base_dimensions = {"length",
                          "mass",
                          "time",
                          "electric_current",
                          "thermodynamic_temperature",
                          "amount_of_substance",
                          "luminous_intensity"};

struct PhysicalDimensionVector final {
  std::array<int, physical_dimension_count> exponents{};

  explicit PhysicalDimensionVector(
      std::array<int, physical_dimension_count> exponents_value = {});

  [[nodiscard]] PhysicalDimensionVector
  operator*(const PhysicalDimensionVector &other) const;
  [[nodiscard]] PhysicalDimensionVector
  operator/(const PhysicalDimensionVector &other) const;
  [[nodiscard]] PhysicalDimensionVector operator^(int exponent) const;
  [[nodiscard]] bool dimensionless() const noexcept;
  [[nodiscard]] std::string signature() const;

  bool operator==(const PhysicalDimensionVector &) const = default;
};

extern const PhysicalDimensionVector DIMENSIONLESS;
extern const PhysicalDimensionVector LENGTH;
extern const PhysicalDimensionVector MASS;
extern const PhysicalDimensionVector TIME;
extern const PhysicalDimensionVector ELECTRIC_CURRENT;
extern const PhysicalDimensionVector TEMPERATURE;
extern const PhysicalDimensionVector AMOUNT;
extern const PhysicalDimensionVector LUMINOUS_INTENSITY;

struct PhysicalUnit final {
  std::string symbol;
  std::string name;
  PhysicalDimensionVector dimension;
  mpq_class scale_to_si{1};
  mpq_class offset_to_si{0};

  PhysicalUnit(std::string symbol_value, std::string name_value,
               PhysicalDimensionVector dimension_value,
               mpq_class scale_to_si_value = 1,
               mpq_class offset_to_si_value = 0);

  [[nodiscard]] std::string canonical_symbol() const;
  [[nodiscard]] std::string signature() const;
  [[nodiscard]] mpq_class to_si(const mpq_class &value) const;
  [[nodiscard]] mpq_class from_si(const mpq_class &value) const;

  bool operator==(const PhysicalUnit &) const = default;
};

extern const PhysicalUnit ONE;
extern const PhysicalUnit METRE;
extern const PhysicalUnit MILLIMETRE;
extern const PhysicalUnit CENTIMETRE;
extern const PhysicalUnit KILOMETRE;
extern const PhysicalUnit SECOND;
extern const PhysicalUnit KILOGRAM;
extern const PhysicalUnit AMPERE;
extern const PhysicalUnit KELVIN;
extern const PhysicalUnit CELSIUS;
extern const PhysicalUnit MOLE;
extern const PhysicalUnit CANDELA;
extern const PhysicalUnit HERTZ;
extern const PhysicalUnit NEWTON;
extern const PhysicalUnit PASCAL;
extern const PhysicalUnit JOULE;
extern const PhysicalUnit WATT;
extern const PhysicalUnit COULOMB;
extern const PhysicalUnit VOLT;
extern const PhysicalUnit OHM;

[[nodiscard]] const PhysicalUnit &physical_unit(std::string_view identifier);
[[nodiscard]] mpq_class convert_exact_value(const mpq_class &value,
                                            const PhysicalUnit &source_unit,
                                            const PhysicalUnit &target_unit);
[[nodiscard]] mpq_class convert_exact_value(const mpq_class &value,
                                            std::string_view source_unit,
                                            std::string_view target_unit);

struct SemanticConcept final {
  std::string canonical_name;
  std::string meaning;
  std::string domain;
  std::string quantity_kind;
  std::vector<std::string> aliases;
  std::optional<PhysicalDimensionVector> physical_dimension;
  std::optional<PhysicalUnit> canonical_unit;
  std::vector<std::string> evidence_ids;
  std::string semantic_status;
  std::string concept_signature;
  bool canonical_eligible = false;

  [[nodiscard]] bool physical() const noexcept;
};

[[nodiscard]] SemanticConcept make_semantic_concept(
    std::string name, std::string meaning, std::string domain,
    std::string quantity_kind, std::vector<std::string> aliases = {},
    std::optional<PhysicalDimensionVector> physical_dimension = std::nullopt,
    std::optional<PhysicalUnit> canonical_unit = std::nullopt,
    std::vector<std::string> evidence_ids = {},
    std::string semantic_status = "SEMANTICALLY_RESOLVED");

struct DimensionalConstraintAssessment final {
  std::string status;
  PhysicalDimensionVector expected_dimension;
  PhysicalDimensionVector observed_dimension;
  bool quantity_kind_consistent = false;
  bool canonical_semantic_eligible = false;
  std::string signature;
  std::vector<std::string> warnings;
};

[[nodiscard]] DimensionalConstraintAssessment assess_product_dimension(
    const SemanticConcept &output,
    const std::vector<std::pair<SemanticConcept, int>> &factors);
[[nodiscard]] DimensionalConstraintAssessment assess_additive_compatibility(
    const std::vector<SemanticConcept> &concepts);
[[nodiscard]] std::string physical_semantic_relation(
    const SemanticConcept &left, const SemanticConcept &right);

[[nodiscard]] contracts::Json to_json(const PhysicalDimensionVector &value);
[[nodiscard]] contracts::Json to_json(const PhysicalUnit &value);
[[nodiscard]] contracts::Json to_json(const SemanticConcept &value);
[[nodiscard]] contracts::Json
to_json(const DimensionalConstraintAssessment &value);

} // namespace statewright::saa
