#include "nonlinear_fixture.hpp"

#include "statewright/common/error.hpp"
#include "statewright/saa/nonlinear_evidence.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("SAA nonlinear exact polynomial evidence matches frozen oracle") {
  using namespace statewright;
  const auto form =
      tests::nonlinear_parent_form("temperature command", "pressure command");
  const saa::ExactPolynomialSystem system = {
      .input_count = 2U,
      .output_count = 2U,
      .terms = {{.output_index = 0U,
                 .powers = {2, 0},
                 .coefficient = 1},
                {.output_index = 1U,
                 .powers = {0, 1},
                 .coefficient = 1}}};
  const auto evidence = saa::acquire_exact_polynomial_jet(
      form, system, {"1/2", "1/2"}, {"1/4", "1/4"}, 2U);

  REQUIRE(form.representative_behavior_signature ==
          "d9004d5e9eacc4b108af181740f85e01fd0bb7b2da8f5c922d9288045bbaf144");
  REQUIRE(evidence.exact);
  REQUIRE(evidence.canonical_local_eligible);
  REQUIRE(evidence.evidence_kind == "EXACT_SYMBOLIC_POLYNOMIAL");
  REQUIRE(evidence.source_snapshot_hash ==
          "3f17e11933a84810039c19f105a3f8f8c7c32face549a66e0c0bf20d7a9da73c");
  REQUIRE(evidence.jet.has_value());
  REQUIRE(evidence.jet->local_behavior_signature ==
          "9b43a735af0bca0c994fd79c36f86d37ddaf7f9594dafbb8098c7adbb1ee652b");
  REQUIRE(evidence.evidence_signature ==
          "70f062700ae16206d66e293862dcad09b9583ae2ceff227bedfb3d68c67beebb");
  REQUIRE(evidence.jet->evaluate({"3/4", "1/2"}) ==
          std::vector<mpq_class>{mpq_class(9, 16), mpq_class(1, 2)});
}

TEST_CASE("SAA nonlinear derivative evidence applies multi-factorials") {
  using namespace statewright;
  const auto form =
      tests::nonlinear_parent_form("temperature command", "pressure command");
  const auto evidence = saa::acquire_exact_derivative_jet(
      form, {"1/2", "1/2"}, {"1/4", "1/4"}, 2U,
      {{.output_index = 0U, .powers = {1, 0}, .derivative = 1},
       {.output_index = 0U, .powers = {2, 0}, .derivative = 2},
       {.output_index = 1U, .powers = {0, 1}, .derivative = 1}},
      std::string(64U, 'a'), "deterministic-test-oracle");
  REQUIRE(evidence.canonical_local_eligible);
  REQUIRE(evidence.jet.has_value());
  const auto found = std::ranges::find_if(
      evidence.jet->terms, [](const auto &term) {
        return term.output_index == 0U && term.powers == std::vector<int>{2, 0};
      });
  REQUIRE(found != evidence.jet->terms.end());
  REQUIRE(found->coefficient == 1);
  REQUIRE(evidence.jet->local_behavior_signature ==
          "5aa8fd90d3e15642754c403de1d438e06648d6f77f59cd7f01a817ea50c78fbd");
  REQUIRE(evidence.evidence_signature ==
          "e5ebca73c9c423f2d137a54646fce8953eb063c9f41d5ca4e5fa92ad2681804d");
}

TEST_CASE("SAA nonlinear evidence gates provenance and exact identity") {
  using namespace statewright;
  const auto form =
      tests::nonlinear_parent_form("temperature command", "pressure command");
  const auto reported = saa::acquire_exact_derivative_jet(
      form, {"1/2", "1/2"}, {"1/4", "1/4"}, 1U,
      {{.output_index = 0U, .powers = {1, 0}, .derivative = 1}},
      std::string(64U, 'b'), "model-output", "reported", false);
  REQUIRE(reported.exact);
  REQUIRE_FALSE(reported.canonical_local_eligible);
  REQUIRE(reported.evidence_signature ==
          "48af380d84b6adf9d7af548d46159116a4c69b72adc65bccf717d96f82477433");

  const auto estimated = saa::acquire_bounded_estimated_derivatives(
      form, {"1/2", "1/2"}, {"1/4", "1/4"}, 2U,
      {{.output_index = 0U,
        .powers = {1, 0},
        .lower = "99/100",
        .upper = "101/100"}},
      std::string(64U, 'c'), "human-lab", "bounded-measurement");
  REQUIRE_FALSE(estimated.exact);
  REQUIRE_FALSE(estimated.canonical_local_eligible);
  REQUIRE_FALSE(estimated.jet.has_value());
  REQUIRE(estimated.evidence_signature ==
          "b527997081276d2ecff6a03cddb84d7c3b2099c0064c23755865be4501355db3");

  auto invalid = saa::ExactPolynomialSystem{
      .input_count = 2U,
      .output_count = 2U,
      .terms = {{.output_index = 0U,
                 .powers = {1, 0},
                 .coefficient = 0.5}}};
  REQUIRE_THROWS_AS(saa::acquire_exact_polynomial_jet(
                        form, invalid, {"1/2", "1/2"}, {"1/4", "1/4"}, 2U),
                    common::Error);
}
