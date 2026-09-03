#include "nonlinear_fixture.hpp"

#include "statewright/common/error.hpp"
#include "statewright/saa/nonlinear_evidence.hpp"
#include "statewright/saa/nonlinear_stability.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

struct RegionalFixture final {
  statewright::saa::GovernedJetEvidence evidence;
  statewright::saa::NonlinearRepresentativeSearch search;
  statewright::saa::CanonicalNonlinearRepresentativeForm local;
  statewright::saa::NonlinearRegionalObservation observation;
};

RegionalFixture regional_fixture(
    const statewright::saa::CanonicalRepresentativeAlgorithmForm &form,
    std::vector<statewright::saa::NumericCoefficient> center,
    std::vector<statewright::saa::NumericCoefficient> radius) {
  using namespace statewright;
  const saa::ExactPolynomialSystem system =
      {.input_count = 2U,
       .output_count = 2U,
       .terms = {{.output_index = 0U,
                  .powers = {2, 0},
                  .coefficient = 1},
                 {.output_index = 1U,
                  .powers = {0, 1},
                  .coefficient = 1}}};
  auto evidence = saa::acquire_exact_polynomial_jet(
      form, system, std::move(center), std::move(radius), 2U);
  auto search = saa::search_nonlinear_representative_coordinates(
      form, *evidence.jet);
  auto local = saa::canonicalize_nonlinear_representative(form, search);
  auto observation = saa::make_regional_observation(
      local, &search, evidence.evidence_signature);
  return {.evidence = std::move(evidence),
          .search = std::move(search),
          .local = std::move(local),
          .observation = std::move(observation)};
}

} // namespace

TEST_CASE("SAA nonlinear stability admits a connected regional meaning") {
  using namespace statewright;
  const auto form = tests::nonlinear_parent_form("temperature command",
                                                  "pressure command");
  const auto left = regional_fixture(form, {"2/5", "1/2"},
                                     {"1/5", "1/4"});
  const auto right = regional_fixture(form, {"3/5", "1/2"},
                                      {"1/5", "1/4"});
  const auto assessment = saa::assess_semantic_stability(
      {left.observation, right.observation});

  REQUIRE(left.observation.transform_family_signature ==
          "72179ce14a66f7d1295c8dcbb08d65b8c4bae291e20af257a22a75ab16e0a4bb");
  REQUIRE(right.observation.transform_family_signature ==
          left.observation.transform_family_signature);
  REQUIRE(assessment.status == "REGIONALLY_STABLE_SEMANTICS");
  REQUIRE(assessment.connected_region);
  REQUIRE(assessment.meanings_stable);
  REQUIRE(assessment.representation_family_stable);
  REQUIRE(assessment.regional_semantic_eligible);
  REQUIRE(assessment.adjacency ==
          std::vector<std::vector<std::size_t>>{{1U}, {0U}});
  REQUIRE(assessment.assessment_signature ==
          "123148046472096afe863b4dce7d48ce5679844b85468a8e9848242c199d7018");
}

TEST_CASE("SAA nonlinear stability rejects disconnected semantic islands") {
  using namespace statewright;
  const auto form = tests::nonlinear_parent_form("temperature command",
                                                  "pressure command");
  const auto left = regional_fixture(form, {"1/4", "1/2"},
                                     {"1/10", "1/4"});
  const auto right = regional_fixture(form, {"3/4", "1/2"},
                                      {"1/10", "1/4"});
  const auto assessment = saa::assess_semantic_stability(
      {left.observation, right.observation});

  REQUIRE(assessment.status == "MULTI_REGION_SEMANTICS_UNRESOLVED");
  REQUIRE_FALSE(assessment.connected_region);
  REQUIRE_FALSE(assessment.regional_semantic_eligible);
  REQUIRE(assessment.assessment_signature ==
          "0ddb57ab978c85605413e76e3be44150b284ae405f0390b5b70ec3eadde2f6c0");
}

TEST_CASE("SAA nonlinear stability detects a transform-family regime change") {
  using namespace statewright;
  const auto form = tests::nonlinear_parent_form("temperature command",
                                                  "pressure command");
  const auto left = regional_fixture(form, {"1/2", "1/2"},
                                     {"1/4", "1/4"});
  const auto right = regional_fixture(form, {"3/5", "1/2"},
                                      {"1/4", "1/4"});
  const saa::NonlinearRegionalObservation left_observation = {
      .local_form = left.local,
      .transform_family_signature = std::string(64U, 'a'),
      .evidence_signature = {}};
  const saa::NonlinearRegionalObservation right_observation = {
      .local_form = right.local,
      .transform_family_signature = std::string(64U, 'b'),
      .evidence_signature = {}};
  const auto assessment = saa::assess_semantic_stability(
      {left_observation, right_observation});

  REQUIRE(assessment.status == "REPRESENTATION_REGIME_CHANGE");
  REQUIRE(assessment.connected_region);
  REQUIRE_FALSE(assessment.representation_family_stable);
  REQUIRE_FALSE(assessment.regional_semantic_eligible);
  REQUIRE(assessment.assessment_signature ==
          "61591a59da276c55228c073bf95df74638e74d851a7746c2d7ea3e2b4a8d5317");
}

TEST_CASE("SAA nonlinear stability keeps one observation local only") {
  using namespace statewright;
  const auto form = tests::nonlinear_parent_form("temperature command",
                                                  "pressure command");
  const auto fixture = regional_fixture(form, {"1/2", "1/2"},
                                        {"1/4", "1/4"});
  const auto observation = saa::make_regional_observation(fixture.local);
  const auto assessment = saa::assess_semantic_stability({observation});

  REQUIRE(assessment.status == "LOCALLY_STABLE_SEMANTICS");
  REQUIRE_FALSE(assessment.regional_semantic_eligible);
  REQUIRE(assessment.assessment_signature ==
          "56e535a58cea9e1edbaec3d4070dc2a51f2c4e3a2deda27f1fa686b664597f9d");
  REQUIRE_THROWS_AS(saa::assess_semantic_stability({}), common::Error);
}
