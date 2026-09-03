#include "nonlinear_fixture.hpp"

#include "statewright/common/error.hpp"
#include "statewright/saa/nonlinear_jet.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

statewright::saa::TaylorJetSpec representative_jet_spec(
    std::vector<statewright::saa::TaylorJetTerm> terms,
    std::vector<statewright::saa::NumericCoefficient> radius = {"1/4",
                                                                "1/4"}) {
  return {.input_count = 2U,
          .output_count = 2U,
          .order = 2U,
          .center = {"1/2", "1/2"},
          .validity_radius = std::move(radius),
          .terms = std::move(terms)};
}

} // namespace

TEST_CASE("SAA nonlinear Taylor jets preserve frozen exact identities") {
  using namespace statewright;
  const auto form = tests::nonlinear_parent_form();
  const auto value = saa::canonicalize_taylor_jet(
      form, representative_jet_spec({
                {.output_index = 0U, .powers = {1, 0}, .coefficient = 1},
                {.output_index = 0U, .powers = {2, 0}, .coefficient = 2},
                {.output_index = 1U, .powers = {0, 1}, .coefficient = 1}}));

  REQUIRE(form.representative_behavior_signature ==
          "876de90acc3f44889ab18d2d5852bfaaf468dc82e20a3392a841ba3b3ce60b26");
  REQUIRE(value.coefficient_signature ==
          "abd29156a7528475d24ce8deaf807fb8d4a4f93d45bbc44107c56112b255a607");
  REQUIRE(value.scope_signature ==
          "850e58647469975a5058ef5ea9ae0b482fede3fa7d2b5d412f80c7442e8316f3");
  REQUIRE(value.coupling.signature ==
          "d319103e2878d8a5cd1c1ee5d6457464d6efd2caf971d8e53107993c97dd0480");
  REQUIRE(value.local_behavior_signature ==
          "76d81bab61d3203f5767eaee132f912a1d0b6951c6075122b3149afcbbcd6e81");
  REQUIRE(value.evaluate({"3/4", "1/2"}) ==
          std::vector<mpq_class>{mpq_class(3, 8), mpq_class(0)});
  REQUIRE(value.coupling.status == "NONLINEAR_REPRESENTATIVE");
  REQUIRE_FALSE(value.global_equivalence_eligible);
}

TEST_CASE("SAA nonlinear Taylor terms canonicalize and detect coupling") {
  using namespace statewright;
  const auto form = tests::nonlinear_parent_form();
  const auto left = saa::canonicalize_taylor_jet(
      form, representative_jet_spec({
                {.output_index = 0U,
                 .powers = {0, 2},
                 .coefficient = "1/2"},
                {.output_index = 1U, .powers = {0, 1}, .coefficient = 1},
                {.output_index = 0U, .powers = {1, 0}, .coefficient = 1},
                {.output_index = 0U,
                 .powers = {0, 2},
                 .coefficient = "1/2"}}));
  const auto right = saa::canonicalize_taylor_jet(
      form, representative_jet_spec({
                {.output_index = 0U, .powers = {1, 0}, .coefficient = 1},
                {.output_index = 0U, .powers = {0, 2}, .coefficient = 1},
                {.output_index = 1U, .powers = {0, 1}, .coefficient = 1}}));
  REQUIRE(left.coefficient_signature == right.coefficient_signature);
  REQUIRE(left.terms == right.terms);
  REQUIRE(left.coupling.status == "NONLINEAR_SEMANTIC_MISREPRESENTATION");
  REQUIRE(left.coupling.off_pair_terms.size() == 1U);

  const auto cross = saa::canonicalize_taylor_jet(
      form, representative_jet_spec({
                {.output_index = 0U, .powers = {1, 1}, .coefficient = 1},
                {.output_index = 1U, .powers = {0, 1}, .coefficient = 1}}));
  REQUIRE(cross.coupling.cross_terms.size() == 1U);
}

TEST_CASE("SAA nonlinear Taylor comparisons remain local and fail closed") {
  using namespace statewright;
  const auto form = tests::nonlinear_parent_form();
  const std::vector<saa::TaylorJetTerm> terms = {
      {.output_index = 0U, .powers = {1, 0}, .coefficient = 1},
      {.output_index = 0U, .powers = {2, 0}, .coefficient = 2},
      {.output_index = 1U, .powers = {0, 1}, .coefficient = 1}};
  const auto wide =
      saa::canonicalize_taylor_jet(form, representative_jet_spec(terms));
  const auto narrow = saa::canonicalize_taylor_jet(
      form, representative_jet_spec(terms, {"1/8", "1/8"}));
  const auto comparison = saa::compare_taylor_jets(wide, narrow);
  REQUIRE(comparison.status == "EXACT_LOCAL_JET_MATCH_ON_INTERSECTION");
  REQUIRE(comparison.overlap_radius ==
          std::vector<mpq_class>{mpq_class(1, 8), mpq_class(1, 8)});
  REQUIRE(comparison.signature ==
          "f00e7a78f5aebc2a3a638f8e02485afe4d86577c2c26f1777b4771fa512db9e6");
  REQUIRE_FALSE(comparison.global_equivalence_eligible);

  auto floating = representative_jet_spec({});
  floating.center = {0.5, "1/2"};
  REQUIRE_THROWS_AS(saa::canonicalize_taylor_jet(form, floating),
                    common::Error);
  auto escaped = representative_jet_spec({});
  escaped.center = {"1/10", "1/2"};
  REQUIRE_THROWS_AS(saa::canonicalize_taylor_jet(form, escaped),
                    common::Error);
  REQUIRE_THROWS_AS(wide.evaluate({"1", "1/2"}), common::Error);
}
