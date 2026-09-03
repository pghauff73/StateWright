#include "statewright/reasoning/adapters.hpp"

#include "statewright/common/error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

namespace {

statewright::contracts::Json load_fixtures() {
  std::ifstream input(STATEWRIGHT_ORACLE_FIXTURES);
  REQUIRE(input.is_open());
  return statewright::contracts::Json::parse(input);
}

} // namespace

TEST_CASE("deterministic decimal adapter evaluates bounded expressions") {
  const auto arithmetic =
      statewright::reasoning::evaluate_decimal_expression("2 + 3 * 4");
  REQUIRE(arithmetic.status == "PASS");
  REQUIRE(arithmetic.result == "14");

  const auto predicate = statewright::reasoning::evaluate_decimal_expression(
      "x > 1 and x < 5", {{"x", "3"}});
  REQUIRE(predicate.status == "PASS");
  REQUIRE(predicate.result == "True");
  statewright::reasoning::require_adapter_result_integrity(predicate);
}

TEST_CASE("symbolic adapter proves bounded polynomial equivalence") {
  const auto result = statewright::reasoning::symbolic_equivalence(
      "(x + 1)^2", "x*x + 2*x + 1");
  REQUIRE(result.status == "PASS");
  REQUIRE(result.result == "0");

  const auto rejected =
      statewright::reasoning::symbolic_equivalence("f(x)", "x");
  REQUIRE(rejected.status == "INCONCLUSIVE");
}

TEST_CASE("numerical residual and finite-domain adapters expose witnesses") {
  const auto residual = statewright::reasoning::numerical_residual_check(
      "x*x", "x + 1", {{{"x", "2"}}});
  REQUIRE(residual.status == "FAIL");
  REQUIRE(residual.result == "residual 1 exceeds tolerance");
  REQUIRE(residual.tolerance == "1E-12");
  REQUIRE(residual.counterexample ==
          statewright::reasoning::AdapterCounterexample{{"x", "2"}});

  const auto finite = statewright::reasoning::finite_domain_check(
      "x + y < 3", {{{"x", {"0", "1"}}, {"y", {"0", "1"}}}});
  REQUIRE(finite.status == "PASS");
  REQUIRE(finite.result == "all 4 assignments passed");

  const auto counterexample = statewright::reasoning::finite_domain_check(
      "x < 1", {{{"x", {"0", "1"}}}});
  REQUIRE(counterexample.status == "FAIL");
  REQUIRE(counterexample.counterexample ==
          statewright::reasoning::AdapterCounterexample{{"x", "1"}});
}

TEST_CASE("dimensional adapter canonicalizes zero exponents") {
  const auto result = statewright::reasoning::dimensional_equivalence(
      {{"L", 1}, {"T", -2}, {"unused", 0}},
      {{"L", 1}, {"T", -2}}, "force per mass");
  REQUIRE(result.status == "PASS");
  REQUIRE(result.result ==
          "left=(('L', 1), ('T', -2)); right=(('L', 1), ('T', -2))");

  auto tampered = result;
  tampered.result = "forged";
  REQUIRE_THROWS_AS(
      statewright::reasoning::require_adapter_result_integrity(tampered),
      statewright::common::Error);
}

TEST_CASE("deterministic adapters match the frozen oracle") {
  const auto fixture = load_fixtures().at("reasoning_adapter_case");
  REQUIRE(statewright::reasoning::to_json(
              statewright::reasoning::evaluate_decimal_expression(
                  "2 + 3 * 4")) == fixture.at("arithmetic"));
  REQUIRE(statewright::reasoning::to_json(
              statewright::reasoning::evaluate_decimal_expression(
                  "x > 1 and x < 5", {{"x", "3"}})) ==
          fixture.at("predicate"));
  REQUIRE(statewright::reasoning::to_json(
              statewright::reasoning::symbolic_equivalence(
                  "(x + 1)^2", "x*x + 2*x + 1")) ==
          fixture.at("symbolic"));
  REQUIRE(statewright::reasoning::to_json(
              statewright::reasoning::numerical_residual_check(
                  "x*x", "x + 1", {{{"x", "2"}}})) ==
          fixture.at("residual"));
  REQUIRE(statewright::reasoning::to_json(
              statewright::reasoning::dimensional_equivalence(
                  {{"L", 1}, {"T", -2}, {"unused", 0}},
                  {{"L", 1}, {"T", -2}}, "force per mass")) ==
          fixture.at("dimensional"));
  REQUIRE(statewright::reasoning::to_json(
              statewright::reasoning::finite_domain_check(
                  "x + y < 3",
                  {{{"x", {"0", "1"}}, {"y", {"0", "1"}}}})) ==
          fixture.at("finite_domain"));
}
