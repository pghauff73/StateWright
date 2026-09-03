#include "nonlinear_fixture.hpp"

#include "statewright/saa/nonlinear_control.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

struct Fixture final {
  statewright::saa::CanonicalRepresentativeAlgorithmForm form;
  statewright::saa::CanonicalTaylorJet jet;
};

Fixture control_fixture() {
  using namespace statewright;
  auto form =
      tests::nonlinear_parent_form("temperature command", "pressure command");
  auto jet = saa::canonicalize_taylor_jet(
      form,
      {.input_count = 2U,
       .output_count = 2U,
       .order = 2U,
       .center = {"1/2", "1/2"},
       .validity_radius = {"1/4", "1/4"},
       .terms = {{.output_index = 0U,
                  .powers = {1, 0},
                  .coefficient = 1},
                 {.output_index = 1U,
                  .powers = {0, 1},
                  .coefficient = 1}}});
  return {.form = std::move(form), .jet = std::move(jet)};
}

} // namespace

TEST_CASE("SAA nonlinear static maps never self-certify controllability") {
  const auto fixture = control_fixture();
  const auto assessment =
      statewright::saa::assess_representative_observability_controllability(
          fixture.form, fixture.jet);
  REQUIRE(assessment.representative_inputs_locally_observable);
  REQUIRE(assessment.status ==
          "OBSERVABLE_CONTROLLABILITY_REQUIRES_DYNAMIC_MODEL");
  REQUIRE_FALSE(assessment.dynamically_controllable.has_value());
  REQUIRE_FALSE(assessment.canonical_control_eligible);
  REQUIRE(assessment.assessment_signature ==
          "944c8d84f3c6c94b934023e514f447cb3924c096d1986a826d22ee194a96fae3");
}

TEST_CASE("SAA nonlinear exact dynamics qualify local control") {
  using namespace statewright;
  const auto fixture = control_fixture();
  const auto dynamic = saa::make_local_dynamic_linearization(
      fixture.form, {{0, 1}, {-1, 0}}, {{1, 0}, {0, 1}},
      {{1, 0}, {0, 1}}, {"thermal state", "pressure state"});
  REQUIRE(dynamic.linearization_signature ==
          "19ce4035946ba5557a03998bd4668bb088a5ea7aa96956a3116899d1048ee585");
  const auto assessment =
      saa::assess_representative_observability_controllability(
          fixture.form, fixture.jet, nullptr, &dynamic);
  REQUIRE(assessment.controllability_rank == 2U);
  REQUIRE(assessment.observability_rank == 2U);
  REQUIRE(assessment.dynamically_controllable == true);
  REQUIRE(assessment.dynamically_observable == true);
  REQUIRE(assessment.status == "LOCALLY_OBSERVABLE_AND_CONTROLLABLE");
  REQUIRE(assessment.canonical_control_eligible);
  REQUIRE(assessment.assessment_signature ==
          "5392cbdaceae5313162c64cb2004f85336236b5e4863a705128836af1f7e56c8");
}

TEST_CASE("SAA nonlinear uncontrollable dynamic states remain blocked") {
  using namespace statewright;
  const auto fixture = control_fixture();
  const auto dynamic = saa::make_local_dynamic_linearization(
      fixture.form, {{0, 0}, {0, 0}}, {{1, 0}, {0, 0}},
      {{1, 0}, {0, 1}}, {"thermal state", "pressure state"});
  const auto assessment =
      saa::assess_representative_observability_controllability(
          fixture.form, fixture.jet, nullptr, &dynamic);
  REQUIRE(assessment.controllability_rank == 1U);
  REQUIRE(assessment.observability_rank == 2U);
  REQUIRE(assessment.status == "DYNAMIC_UNCONTROLLABLE");
  REQUIRE_FALSE(assessment.canonical_control_eligible);
  REQUIRE(assessment.assessment_signature ==
          "7f8544b9f3d99c16ae3e40b436008e735b34743cc0939fa9ec8f3bbbf41a6037");
}
