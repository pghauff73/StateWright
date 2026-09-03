#include "statewright/common/error.hpp"
#include "statewright/reasoning/hypotheses.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

namespace {

statewright::contracts::Json load_fixtures() {
  std::ifstream input(STATEWRIGHT_ORACLE_FIXTURES);
  REQUIRE(input.is_open());
  return statewright::contracts::Json::parse(input);
}

statewright::reasoning::HypothesisProposal proposal(std::string identity,
                                                    std::string proposition,
                                                    int posterior) {
  return {.hypothesis_id = std::move(identity),
          .proposition = std::move(proposition),
          .prior_bp = posterior,
          .posterior_bp = posterior,
          .supporting_evidence = {},
          .conflicting_evidence = {},
          .assumptions = {},
          .predictions = {},
          .falsifiers = {},
          .status = "ACTIVE"};
}

} // namespace

TEST_CASE("OIEC-SR hypotheses match frozen normalization fixtures") {
  const auto fixture = load_fixtures().at("reasoning_hypothesis_case");
  const auto state = statewright::reasoning::build_hypothesis_set(
      {proposal("a", "A", 6'000), proposal("b", "B", 4'000)}, "problem",
      2, true);
  REQUIRE(statewright::reasoning::to_json(state) ==
          fixture.at("initial_state"));

  const auto reverse = statewright::reasoning::build_hypothesis_set(
      {proposal("b", "B", 4'000), proposal("a", "A", 6'000)}, "problem",
      2, true);
  REQUIRE(reverse.signature == state.signature);
}

TEST_CASE("OIEC-SR fixed-point updates preserve exact provenance") {
  const auto fixture = load_fixtures().at("reasoning_hypothesis_case");
  const auto state = statewright::reasoning::build_hypothesis_set(
      {proposal("a", "A", 5'000)}, "problem", 1);
  const auto updated = statewright::reasoning::update_hypothesis_state(
      state, {{"a", {8'000, 2'000}}}, {"e1"});
  REQUIRE(statewright::reasoning::to_json(updated.state) ==
          fixture.at("updated_state"));
  REQUIRE(updated.records.size() == 1U);
  REQUIRE(statewright::reasoning::to_json(updated.records.front()) ==
          fixture.at("update_record"));
  REQUIRE(updated.records.front().previous_posterior_bp == 5'000);
  REQUIRE(updated.records.front().updated_posterior_bp == 8'000);
}

TEST_CASE("OIEC-SR evidence binding is monotonic") {
  const auto initial = statewright::reasoning::build_hypothesis_set(
      {proposal("a", "A", 5'000)}, "problem", 1);
  const auto first = statewright::reasoning::update_hypothesis_state(
      initial, {{"a", {8'000, 2'000}}}, {"e1"});
  const auto second = statewright::reasoning::update_hypothesis_state(
      first.state, {{"a", {7'000, 3'000}}}, {"e2"});
  REQUIRE(second.state.hypotheses.front().supporting_evidence ==
          std::vector<std::string>{"e1", "e2"});
  REQUIRE(second.state.update_ids.size() == 2U);
}

TEST_CASE("OIEC-SR falsified hypotheses require new evidence to recover") {
  auto falsified = proposal("a", "A", 0);
  falsified.prior_bp = 5'000;
  falsified.conflicting_evidence = {"e1"};
  falsified.status = "FALSIFIED";
  const auto initial = statewright::reasoning::build_hypothesis_set(
      {falsified}, "problem", 1);
  const auto unchanged = statewright::reasoning::update_hypothesis_state(
      initial, {{"a", {10'000, 0}}}, {"e1"});
  REQUIRE(unchanged.records.empty());
  REQUIRE(unchanged.state.signature == initial.signature);
  const auto recovered = statewright::reasoning::update_hypothesis_state(
      initial, {{"a", {10'000, 0}}}, {"e2"});
  REQUIRE(recovered.state.hypotheses.front().posterior_bp > 0);
  REQUIRE(recovered.state.hypotheses.front().status == "WEAKENED");
}

TEST_CASE("OIEC-SR conflicting evidence retains prior support") {
  auto hypothesis = proposal("a", "A", 8'000);
  hypothesis.prior_bp = 5'000;
  hypothesis.supporting_evidence = {"support"};
  const auto state = statewright::reasoning::build_hypothesis_set(
      {hypothesis}, "problem", 1);
  const auto updated = statewright::reasoning::update_hypothesis_state(
      state, {{"a", {2'000, 8'000}}}, {"counterexample"});
  REQUIRE(updated.state.hypotheses.front().supporting_evidence ==
          std::vector<std::string>{"support"});
  REQUIRE(updated.state.hypotheses.front().conflicting_evidence ==
          std::vector<std::string>{"counterexample"});
}

TEST_CASE("OIEC-SR malformed updates fail closed") {
  const auto state = statewright::reasoning::build_hypothesis_set(
      {proposal("a", "A", 5'000), proposal("b", "B", 5'000)}, "problem",
      2, true);
  REQUIRE_THROWS_AS(statewright::reasoning::update_hypothesis_state(
                        state, {{"a", {8'000, 2'000}}}, {"e1"}),
                    statewright::common::Error);
  REQUIRE_THROWS_AS(statewright::reasoning::update_hypothesis_state(
                        state,
                        {{"a", {8'000, 2'000}}, {"missing", {2'000, 8'000}}},
                        {"e1"}),
                    statewright::common::Error);
}
