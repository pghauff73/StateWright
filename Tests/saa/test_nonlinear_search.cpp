#include "nonlinear_fixture.hpp"

#include "statewright/common/error.hpp"
#include "statewright/saa/nonlinear_search.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

statewright::saa::CanonicalTaylorJet coupled_jet(
    const statewright::saa::CanonicalRepresentativeAlgorithmForm &form) {
  return statewright::saa::canonicalize_taylor_jet(
      form,
      {.input_count = 2U,
       .output_count = 2U,
       .order = 2U,
       .center = {"1/2", "1/2"},
       .validity_radius = {"1/4", "1/4"},
       .terms = {{.output_index = 0U,
                  .powers = {1, 0},
                  .coefficient = 1},
                 {.output_index = 0U,
                  .powers = {0, 2},
                  .coefficient = 1},
                 {.output_index = 1U,
                  .powers = {0, 1},
                  .coefficient = 1}}});
}

} // namespace

TEST_CASE("SAA nonlinear search finds an exact triangular representative") {
  using namespace statewright;
  const auto form = tests::nonlinear_parent_form("temperature command",
                                                  "pressure command");
  const auto source = coupled_jet(form);
  const auto search =
      saa::search_nonlinear_representative_coordinates(form, source);

  REQUIRE(source.local_behavior_signature ==
          "9d9617a9f7b13110bab47e5ab65716c60b32a2ba7acc8ffcae9e459f434fe372");
  REQUIRE(search.status == "NONLINEAR_REPRESENTATIVE_FORM_FOUND");
  REQUIRE(search.representative_found);
  REQUIRE(search.candidates_evaluated == 1);
  REQUIRE(search.search_depth == 1);
  REQUIRE(search.best_candidate.has_value());
  const auto &candidate = *search.best_candidate;
  REQUIRE(candidate.coupling_score == 0U);
  REQUIRE(candidate.transforms.size() == 1U);
  REQUIRE(candidate.transforms.front().target_input_index == 0U);
  REQUIRE(candidate.transforms.front().monomial_powers ==
          std::vector<int>{0, 2});
  REQUIRE(candidate.transforms.front().coefficient == 1);
  REQUIRE(candidate.transforms.front().transform_signature ==
          "8bc11e3a2dcf67c35af7de230dd1704172b3a4f3f72cd7c9764c5bd4d959c53f");
  REQUIRE(candidate.transformed_jet.validity_radius.front() ==
          mpq_class("3/16"));
  REQUIRE(candidate.transformed_jet.local_behavior_signature ==
          "6c6389d9e32a7f6bffd45b4e8b60760d166e99c626600b194cc01f7e462a3880");
  REQUIRE(candidate.candidate_signature ==
          "7b9936301938a3b7247b34762c692976b3da11ab3ec83ae3fd55551ae5ac4121");
  REQUIRE(search.audit_hash ==
          "bc005369756b721ddd2e11177893a8c060e73cb1b11a62613cfe1ed2824a5746");
  REQUIRE(candidate.semantic_issues.size() == 1U);
  REQUIRE_FALSE(candidate.local_canonical_eligible);
}

TEST_CASE("SAA nonlinear canonical admission requires resolved semantics") {
  using namespace statewright;
  const auto form = tests::nonlinear_parent_form("temperature command",
                                                  "pressure command");
  const auto search = saa::search_nonlinear_representative_coordinates(
      form, coupled_jet(form));
  REQUIRE(search.best_candidate.has_value());
  const auto &issue = search.best_candidate->semantic_issues.front();
  REQUIRE(issue.issue_id ==
          "nonlinear-semantic:aba1fe3ed218ecc7bf118e6c");
  REQUIRE_THROWS_AS(saa::canonicalize_nonlinear_representative(form, search),
                    common::Error);

  const std::string falsifier =
      "new coordinate changes excluded pressure output";
  const auto candidate = saa::make_semantic_candidate(
      issue, "temperature command corrected for pressure curvature", {0}, {1},
      {}, {falsifier});
  const auto resolution = saa::evaluate_semantic_candidate(
      issue, candidate, {"evidence:nonlinear-coordinate"},
      {{falsifier, "SURVIVED", "evidence:nonlinear-coordinate"}}, true);
  const auto canonical = saa::canonicalize_nonlinear_representative(
      form, search, {candidate}, {resolution});

  REQUIRE(candidate.signature ==
          "5f81060231b0015321366db72cedae503af028fd9957aa4be391d2371811819b");
  REQUIRE(resolution.resolution_signature ==
          "fcf25814299158b75f6f7f942cc4e52cf8ed3f315e7ad17bfb4bde5dfa40864b");
  REQUIRE(canonical.resolved_input_meanings.front() ==
          "temperature command corrected for pressure curvature");
  REQUIRE(canonical.semantic_signature ==
          "c1e7164519150d962cb09ca89831a12c3c167cadd587687f525be0b2e43ec9bf");
  REQUIRE(canonical.local_representative_behavior_signature ==
          "dca42a6137d86c838e5d3ca05e73cbd96e1d7101ae85aa59204577043f597ccb");
  REQUIRE(canonical.audit_hash ==
          "d66c7d3fd0c6cfe45ee59996c8808b74a1a2f4339e477a8f21810a74ca61b516");
  REQUIRE(canonical.local_canonical_eligible);
  REQUIRE_FALSE(canonical.global_equivalence_eligible);
  REQUIRE(canonical.store_status ==
          "ELIGIBLE_LOCAL_NONLINEAR_REPRESENTATIVE_FORM");

  const auto directives =
      saa::propagate_semantic_issues(search.best_candidate->semantic_issues);
  REQUIRE(directives.size() == 7U);
  REQUIRE(std::ranges::any_of(directives, [](const auto &directive) {
    return directive.subsystem == "IURM" && directive.blocking;
  }));
  REQUIRE(std::ranges::any_of(directives, [](const auto &directive) {
    return directive.subsystem == "ALGORITHM_STORE" && directive.blocking;
  }));
}

TEST_CASE("SAA nonlinear identity representative inherits semantics") {
  using namespace statewright;
  const auto form = tests::nonlinear_parent_form("temperature command",
                                                  "pressure command");
  const auto source = saa::canonicalize_taylor_jet(
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
                  .powers = {2, 0},
                  .coefficient = 1},
                 {.output_index = 1U,
                  .powers = {0, 1},
                  .coefficient = 1},
                 {.output_index = 1U,
                  .powers = {0, 3},
                  .coefficient = "1/2"}}});
  const auto search =
      saa::search_nonlinear_representative_coordinates(form, source);
  const auto canonical =
      saa::canonicalize_nonlinear_representative(form, search);

  REQUIRE(search.status == "NONLINEAR_REPRESENTATIVE_ALREADY_FOUND");
  REQUIRE(search.candidates_evaluated == 0);
  REQUIRE(search.audit_hash ==
          "a5903b398b4fa05bde45f15c0d3b9bf8dce77258ed07298ac9a2fb11d0cac14e");
  REQUIRE(canonical.resolved_input_meanings ==
          std::vector<std::string>{"temperature command", "pressure command"});
  REQUIRE(canonical.audit_hash ==
          "322691834814594c49486f5f678b196d0327c6df7ea30a50bbb9e9c7445e6760");
}

TEST_CASE("SAA nonlinear search leaves target-dependent cross terms unresolved") {
  using namespace statewright;
  const auto form = tests::nonlinear_parent_form("temperature command",
                                                  "pressure command");
  const auto source = saa::canonicalize_taylor_jet(
      form,
      {.input_count = 2U,
       .output_count = 2U,
       .order = 2U,
       .center = {"1/2", "1/2"},
       .validity_radius = {"1/4", "1/4"},
       .terms = {{.output_index = 0U,
                  .powers = {1, 0},
                  .coefficient = 1},
                 {.output_index = 0U,
                  .powers = {1, 1},
                  .coefficient = 1},
                 {.output_index = 1U,
                  .powers = {0, 1},
                  .coefficient = 1}}});
  const auto first =
      saa::search_nonlinear_representative_coordinates(form, source);
  const auto second =
      saa::search_nonlinear_representative_coordinates(form, source);

  REQUIRE(first.status == "NONLINEAR_REPRESENTATION_UNRESOLVED");
  REQUIRE_FALSE(first.representative_found);
  REQUIRE(first.candidates_evaluated == 0);
  REQUIRE(first.audit_hash ==
          "2eb94398c4bcbea5c2da8d2ceaa189e07e1b052b0482ba97015748563eb0558e");
  REQUIRE(first.audit_hash == second.audit_hash);
  REQUIRE(saa::to_json(first) == saa::to_json(second));
}

TEST_CASE("SAA nonlinear search enforces bounded exact transforms") {
  using namespace statewright;
  const auto form = tests::nonlinear_parent_form();
  const auto source = coupled_jet(form);
  REQUIRE_THROWS_AS(saa::make_nonlinear_shear(0U, {1, 0}, 1),
                    common::Error);
  REQUIRE_THROWS_AS(saa::make_nonlinear_shear(0U, {1, 1}, 1),
                    common::Error);
  REQUIRE_THROWS_AS(saa::make_nonlinear_shear(0U, {0, 2}, 0.5),
                    common::Error);
  REQUIRE_THROWS_AS(saa::search_nonlinear_representative_coordinates(
                        form, source, 0, 1),
                    common::Error);
  REQUIRE_THROWS_AS(saa::search_nonlinear_representative_coordinates(
                        form, source, 1, saa::max_nonlinear_search_depth + 1),
                    common::Error);
}
