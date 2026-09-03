#include "statewright/common/error.hpp"
#include "statewright/saa/semantic_units.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

statewright::saa::SemanticConcept semantic_concept(
    std::string evidence_id, std::string name, std::string meaning,
    std::string quantity,
    std::optional<statewright::saa::PhysicalDimensionVector> dimension,
    std::optional<statewright::saa::PhysicalUnit> unit = std::nullopt,
    std::string domain = "mechanics") {
  return statewright::saa::make_semantic_concept(
      std::move(name), std::move(meaning), std::move(domain),
      std::move(quantity), {}, std::move(dimension), std::move(unit),
      {std::move(evidence_id)});
}

} // namespace

TEST_CASE("SAA semantic units preserve exact SI identities") {
  using namespace statewright::saa;
  REQUIRE(LENGTH.signature() ==
          "a74165b6ff21d3b80f57380dda887705cbea8b7148990c4b65024bddfd5e23e9");
  REQUIRE(NEWTON.dimension.signature() ==
          "3bed239783ea65ddf82647328ab8e8b0975270c7627142e47314ab966d9bbb7b");
  REQUIRE(NEWTON.signature() ==
          "92f584eeb6510f79e54eac93e2fffc444bede6f633ef62fda7ed39876cb72164");
  REQUIRE(convert_exact_value(mpq_class(1000), "mm", "m") == 1);
  REQUIRE(convert_exact_value(mpq_class(0), "degC", "K") ==
          mpq_class(5463, 20));
  REQUIRE(physical_unit("Ω") == OHM);
  REQUIRE_THROWS_AS(convert_exact_value(mpq_class(1), "m", "s"),
                    statewright::common::Error);
  const mpq_class oversized(mpz_class(1) << 256, 1);
  const PhysicalUnit oversized_unit("huge", "huge", DIMENSIONLESS,
                                    oversized);
  REQUIRE_THROWS_AS(to_json(oversized_unit), statewright::common::Error);
}

TEST_CASE("SAA semantic concepts retain exact grounded signatures") {
  using namespace statewright::saa;
  const auto force = semantic_concept("e:f", "force", "net force", "force",
                                      MASS * LENGTH / (TIME ^ 2), NEWTON);
  REQUIRE(force.canonical_eligible);
  REQUIRE(force.concept_signature ==
          "0de8fb444ff6f079a0a1312388e9e8d92bab3db96ac35c86183e95df1c32e22d");
  REQUIRE(to_json(force).at("canonical_unit").at("symbol") == "N");

  const auto unresolved = make_semantic_concept(
      "Candidate", "possible meaning", "test", "quantity");
  REQUIRE_FALSE(unresolved.canonical_eligible);
}

TEST_CASE("SAA product dimensions are necessary but not sufficient") {
  using namespace statewright::saa;
  const auto force = semantic_concept(
      "e:f", "force", "net force", "force", MASS * LENGTH / (TIME ^ 2),
      NEWTON);
  const auto mass =
      semantic_concept("e:m", "mass", "inertial mass", "mass", MASS,
                       KILOGRAM);
  const auto acceleration = semantic_concept(
      "e:a", "acceleration", "rate of change of velocity", "acceleration",
      LENGTH / (TIME ^ 2));
  const auto assessment =
      assess_product_dimension(force, {{mass, 1}, {acceleration, 1}});
  REQUIRE(assessment.status == "DIMENSIONALLY_COHERENT");
  REQUIRE(assessment.canonical_semantic_eligible);
  REQUIRE(assessment.signature ==
          "66c84cb14481487266be867141ecea48bb99f348ad97ee1e3888738d3f895e0a");
}

TEST_CASE("SAA equal dimensions do not erase quantity meaning") {
  using namespace statewright::saa;
  const auto shared = MASS * (LENGTH ^ 2) / (TIME ^ 2);
  const auto energy = semantic_concept(
      "e:energy", "energy", "capacity to perform work", "energy", shared,
      JOULE);
  const auto torque = semantic_concept(
      "e:torque", "torque", "moment of force about an axis", "torque",
      shared);
  REQUIRE(physical_semantic_relation(energy, torque) ==
          "SAME_DIMENSION_DIFFERENT_QUANTITY_KIND");
  const auto assessment = assess_additive_compatibility({energy, torque});
  REQUIRE(assessment.status == "ADDITIVE_SEMANTIC_MISREPRESENTATION");
  REQUIRE_FALSE(assessment.canonical_semantic_eligible);
  REQUIRE(assessment.signature ==
          "75beb83e88573769329c7ecc0d5ddef6e50ade15e96d347d5afb29674f43290d");
}
