#include "statewright/common/error.hpp"
#include "statewright/saa/nonlinear_global.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {

statewright::saa::ExactPolynomialSystem polynomial(
    std::vector<statewright::saa::ExactPolynomialTerm> terms) {
  return {.input_count = 1U,
          .output_count = 1U,
          .terms = std::move(terms)};
}

} // namespace

TEST_CASE("SAA nonlinear global polynomial identity preserves semantics") {
  using namespace statewright;
  const auto left = polynomial(
      {{.output_index = 0U, .powers = {1}, .coefficient = 1},
       {.output_index = 0U, .powers = {2}, .coefficient = 1},
       {.output_index = 0U, .powers = {2}, .coefficient = 2}});
  const auto right = polynomial(
      {{.output_index = 0U, .powers = {2}, .coefficient = 3},
       {.output_index = 0U, .powers = {1}, .coefficient = 1}});
  const auto exact = saa::certify_exact_polynomial_global_equivalence(
      left, right, "semantic:x", "semantic:x", {0}, {1});
  REQUIRE(exact.status == "EXACT_GLOBAL_POLYNOMIAL_EQUIVALENCE_ON_DOMAIN");
  REQUIRE(exact.mathematical_equivalence);
  REQUIRE(exact.semantic_equivalence);
  REQUIRE(exact.global_equivalence_eligible);
  REQUIRE(exact.certificate_signature ==
          "6328f71f18088780df5b6898f33e839b442feebb794715b34669b7d049f17dbf");

  const auto changed = saa::certify_exact_polynomial_global_equivalence(
      left, right, "temperature", "pressure", {0}, {1});
  REQUIRE(changed.status ==
          "GLOBAL_MATHEMATICAL_MATCH_SEMANTIC_DIFFERENCE");
  REQUIRE(changed.mathematical_equivalence);
  REQUIRE_FALSE(changed.semantic_equivalence);
  REQUIRE_FALSE(changed.global_equivalence_eligible);
  REQUIRE(changed.certificate_signature ==
          "e857b42c33f4cb5973d5c12bf18700b18252d84f8fbceb924540fdc2df767d21");
}

TEST_CASE("SAA nonlinear finite covers prove only explicit domains") {
  using namespace statewright;
  const std::vector<saa::GlobalEquivalenceCell> complete = {
      saa::make_global_equivalence_cell({0}, {"1/2"}, {0}, "semantic:x",
                                        "cell:a"),
      saa::make_global_equivalence_cell({"1/2"}, {1}, {0}, "semantic:x",
                                        "cell:b")};
  const auto covered =
      saa::certify_regional_global_equivalence(complete, {0}, {1});
  REQUIRE(covered.complete_domain_coverage);
  REQUIRE(covered.global_equivalence_eligible);
  REQUIRE(covered.certificate_signature ==
          "6173d04a2ef4f406415a7daf89db57d68fc2f954b9589241226bfa0ff190e2a0");

  const std::vector<saa::GlobalEquivalenceCell> gap = {
      saa::make_global_equivalence_cell({0}, {"2/5"}, {0}, "semantic:x",
                                        "cell:a"),
      saa::make_global_equivalence_cell({"3/5"}, {1}, {0}, "semantic:x",
                                        "cell:b")};
  const auto unresolved =
      saa::certify_regional_global_equivalence(gap, {0}, {1});
  REQUIRE(unresolved.status ==
          "GLOBAL_EQUIVALENCE_UNRESOLVED_INCOMPLETE_COVERAGE");
  REQUIRE_FALSE(unresolved.complete_domain_coverage);
  REQUIRE_FALSE(unresolved.global_equivalence_eligible);
  REQUIRE(unresolved.certificate_signature ==
          "d924d90318c00b02acc3c245b3731e2ada2ca37523d3a6c0aa4961524307c9c2");
}

TEST_CASE("SAA nonlinear global certificates reject invalid domains") {
  using namespace statewright;
  const auto system = polynomial(
      {{.output_index = 0U, .powers = {1}, .coefficient = 1}});
  REQUIRE_THROWS_AS(saa::certify_exact_polynomial_global_equivalence(
                        system, system, "semantic:x", "semantic:x", {0.0},
                        {1}),
                    common::Error);
  REQUIRE_THROWS_AS(saa::make_global_equivalence_cell(
                        {0}, {0}, {0}, "semantic:x", "cell"),
                    common::Error);
}
