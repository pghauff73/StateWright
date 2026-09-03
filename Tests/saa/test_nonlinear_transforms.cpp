#include "nonlinear_fixture.hpp"

#include "statewright/common/error.hpp"
#include "statewright/saa/nonlinear_transforms.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

statewright::saa::CanonicalTaylorJet polynomial_fixture(
    const statewright::saa::CanonicalRepresentativeAlgorithmForm &form) {
  return statewright::saa::canonicalize_taylor_jet(
      form,
      {.input_count = 2U,
       .output_count = 2U,
       .order = 3U,
       .center = {"1/2", "1/2"},
       .validity_radius = {"1/4", "1/4"},
       .terms = {{.output_index = 0U,
                  .powers = {1, 0},
                  .coefficient = 1},
                 {.output_index = 0U,
                  .powers = {0, 2},
                  .coefficient = 1},
                 {.output_index = 0U,
                  .powers = {0, 3},
                  .coefficient = 1},
                 {.output_index = 1U,
                  .powers = {0, 1},
                  .coefficient = 1}}});
}

statewright::saa::ExactPolynomialShear grouped_transform() {
  using namespace statewright;
  return saa::make_polynomial_shear(
      2U, 0U,
      {{{.powers = {0, 2}, .coefficient = 1},
        {.powers = {0, 3}, .coefficient = 1}}});
}

} // namespace

TEST_CASE("SAA nonlinear polynomial transform removes grouped coupling") {
  using namespace statewright;
  const auto form = tests::nonlinear_parent_form("temperature command",
                                                  "pressure command");
  const auto source = polynomial_fixture(form);
  const auto transform = grouped_transform();
  const auto transformed =
      saa::apply_polynomial_shear(form, source, transform);

  REQUIRE(source.local_behavior_signature ==
          "ecf50d5ee2fba3749dc5ffc5f00809324b253d8c23c6479829148e8fb7384770");
  REQUIRE(transform.transform_signature ==
          "7ea25f4687f871a73114bac96df06b30c2c83dcbc9701d122b3216c5571fbcbf");
  REQUIRE(transform.terms.size() == 2U);
  REQUIRE(transformed.coupling.representative);
  REQUIRE(transformed.terms.size() == 2U);
  REQUIRE(transformed.terms[0].output_index == 0U);
  REQUIRE(transformed.terms[0].powers == std::vector<int>{1, 0});
  REQUIRE(transformed.terms[0].coefficient == 1);
  REQUIRE(transformed.terms[1].output_index == 1U);
  REQUIRE(transformed.terms[1].powers == std::vector<int>{0, 1});
  REQUIRE(transformed.terms[1].coefficient == 1);
  REQUIRE(transformed.coefficient_signature ==
          "36e7b42374833bd2d205ae80ab1839dd21c7c413711674d7b5742cc15cb45890");
  REQUIRE(transformed.scope_signature ==
          "262141a0ff701d7915646546ff876c061e1d805e29998afcbeb55c4481fba1af");
  REQUIRE(transformed.local_behavior_signature ==
          "0981cc467792f037089e1ccb9110c6882d76618e9c57ca70d58210ca78156921");

  const auto automorphism =
      saa::make_polynomial_automorphism({transform});
  REQUIRE(automorphism.automorphism_signature ==
          "9db6588cbc874c0a13af0ce4bbf455658f5d3aa599b856df9bcc53ff9d3da18a");
  REQUIRE(saa::apply_polynomial_automorphism(form, source, automorphism)
              .local_behavior_signature ==
          transformed.local_behavior_signature);
}

TEST_CASE("SAA nonlinear polynomial transform search requires new semantics") {
  using namespace statewright;
  const auto form = tests::nonlinear_parent_form("temperature command",
                                                  "pressure command");
  const auto search =
      saa::search_polynomial_automorphisms(form, polynomial_fixture(form));

  REQUIRE(search.status == "POLYNOMIAL_REPRESENTATIVE_FORM_FOUND");
  REQUIRE(search.representative_found);
  REQUIRE(search.candidates_evaluated == 2);
  REQUIRE(search.search_depth == 1);
  REQUIRE(search.best_candidate.transforms.size() == 1U);
  REQUIRE(search.best_candidate.transforms.front().terms.size() == 2U);
  REQUIRE(search.best_candidate.candidate_signature ==
          "8c5e6ec66b4b2f322284150d609c74b7975eefe61987836be12cd09b0c75b846");
  REQUIRE(search.audit_hash ==
          "c7c7d5e31bbb2b60b524c3801a3c6f1732b9972cef9b47607962b1868a0afffc");
  REQUIRE(search.best_candidate.semantic_issues.size() == 1U);
  const auto &issue = search.best_candidate.semantic_issues.front();
  REQUIRE(issue.issue_id ==
          "polynomial-semantic:df3096a5ca194ab5a7add635");
  REQUIRE_THROWS_AS(
      saa::canonicalize_polynomial_representative(form, search),
      common::Error);

  const std::string falsifier =
      "new coordinate changes the excluded pressure output";
  const auto semantic_candidate = saa::make_semantic_candidate(
      issue, "temperature command with nonlinear pressure compensation", {0},
      {1}, {}, {falsifier});
  const auto resolution = saa::evaluate_semantic_candidate(
      issue, semantic_candidate, {"evidence:poly-semantic"},
      {{falsifier, "SURVIVED", "evidence:poly-semantic"}}, true);
  const auto local = saa::canonicalize_polynomial_representative(
      form, search, {semantic_candidate}, {resolution});

  REQUIRE(semantic_candidate.signature ==
          "955230751e3fb1eb45cc5999d1137c2a8b2690b4b7930ba8f8e6d7d6971d3839");
  REQUIRE(resolution.resolution_signature ==
          "a6e1f2c0b19cb41d8b047d1086614c2d7682897a3194f27b262fa9696ba21f9e");
  REQUIRE(local.semantic_signature ==
          "cba5894ff41c55cbf865792419425e8af7d7b1a6f9ef604ab28fafe90e7c228f");
  REQUIRE(local.local_representative_behavior_signature ==
          "071ae9d464b978202c8a069e9d71ff2beed96bf49cd9c5226a5054f89f54dbaa");
  REQUIRE(local.audit_hash ==
          "6051a9709b9bb0997bd140d40c6cfbc603034440a390f7cd4d56d0e4859a2227");
  REQUIRE(local.local_canonical_eligible);
  REQUIRE_FALSE(local.global_equivalence_eligible);
}

TEST_CASE("SAA nonlinear polynomial transform enforces bounded exact shears") {
  using namespace statewright;
  REQUIRE_THROWS_AS(saa::make_polynomial_shear(0U, 0U, {}), common::Error);
  REQUIRE_THROWS_AS(
      saa::make_polynomial_shear(
          2U, 0U, {{{.powers = {1, 1}, .coefficient = 1}}}),
      common::Error);
  REQUIRE_THROWS_AS(
      saa::make_polynomial_shear(
          2U, 0U, {{{.powers = {0, 2}, .coefficient = 0.5}}}),
      common::Error);
  REQUIRE_THROWS_AS(saa::make_polynomial_automorphism({}), common::Error);
}
