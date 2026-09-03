#include "nonlinear_fixture.hpp"

#include "statewright/saa/nonlinear_geometry.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

statewright::saa::CanonicalTaylorJet jet(
    const statewright::saa::CanonicalRepresentativeAlgorithmForm &form,
    std::vector<statewright::saa::TaylorJetTerm> terms) {
  return statewright::saa::canonicalize_taylor_jet(
      form, {.input_count = 2U,
             .output_count = 2U,
             .order = 2U,
             .center = {"1/2", "1/2"},
             .validity_radius = {"1/4", "1/4"},
             .terms = std::move(terms)});
}

} // namespace

TEST_CASE("SAA nonlinear geometry detects full-rank local coordinates") {
  using namespace statewright;
  const auto form =
      tests::nonlinear_parent_form("temperature command", "pressure command");
  const auto value = jet(
      form, {{.output_index = 0U, .powers = {1, 0}, .coefficient = 1},
             {.output_index = 0U, .powers = {2, 0}, .coefficient = 1},
             {.output_index = 1U, .powers = {0, 1}, .coefficient = 1}});
  const auto geometry = saa::assess_nonlinear_geometry(form, value);
  REQUIRE(geometry.jacobian_rank == 2U);
  REQUIRE(geometry.local_diffeomorphism);
  REQUIRE(geometry.invariant_distribution_dimension == 0U);
  REQUIRE(geometry.status == "FULL_RANK_LOCAL_GEOMETRY");
  REQUIRE(geometry.canonical_geometry_eligible);
  REQUIRE(geometry.assessment_signature ==
          "54a175c36b90e44750d2f95254fbcd431ff9250319c1af402a5e0dedcd3a3a71");
}

TEST_CASE("SAA nonlinear geometry detects exact invariant directions") {
  using namespace statewright;
  const auto form =
      tests::nonlinear_parent_form("temperature command", "pressure command");
  const auto value = jet(
      form, {{.output_index = 0U, .powers = {1, 0}, .coefficient = 1}});
  const auto geometry = saa::assess_nonlinear_geometry(form, value);
  REQUIRE(geometry.invariant_distribution_dimension == 1U);
  REQUIRE(geometry.invariant_distribution_basis.front() ==
          std::vector<mpq_class>{mpq_class(0), mpq_class(1)});
  REQUIRE(geometry.invariant_distribution_integrable);
  REQUIRE(geometry.status == "INVARIANT_REDUNDANT_DIRECTION_DETECTED");
  REQUIRE_FALSE(geometry.canonical_geometry_eligible);
  REQUIRE(geometry.assessment_signature ==
          "6c44646eaac5e28a7e36d948cc90fa6ef43f114cc029d7a07de877badb069b20");
}

TEST_CASE("SAA nonlinear geometry counts exact cross curvature") {
  using namespace statewright;
  const auto form =
      tests::nonlinear_parent_form("temperature command", "pressure command");
  const auto value = jet(
      form, {{.output_index = 0U, .powers = {1, 0}, .coefficient = 1},
             {.output_index = 0U, .powers = {1, 1}, .coefficient = 2},
             {.output_index = 1U, .powers = {0, 1}, .coefficient = 1}});
  const auto geometry = saa::assess_nonlinear_geometry(form, value);
  REQUIRE(geometry.cross_curvature_count == 1U);
  REQUIRE(geometry.hessian[0][0][1] == 2);
  REQUIRE(geometry.assessment_signature ==
          "4ab263a3cb9e03b2188f1c8c87ca363d566cf6636d09ff1b5d0b5c78321bfdf0");
  REQUIRE(saa::exact_matrix_rank({{1, 2}, {2, 4}}) == 1U);
  REQUIRE(saa::exact_nullspace({}, 2U) ==
          saa::RationalMatrix{{1, 0}, {0, 1}});
}
