#include "statewright/reasoning/certification.hpp"
#include "statewright/reasoning/generator.hpp"
#include "statewright/reasoning/hypotheses.hpp"

#include "statewright/common/error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

statewright::contracts::Json load_fixtures() {
  std::ifstream input(STATEWRIGHT_ORACLE_FIXTURES);
  REQUIRE(input.is_open());
  return statewright::contracts::Json::parse(input);
}

statewright::reasoning::ReasoningProblem problem() {
  return statewright::reasoning::create_reasoning_problem(
      "A bounded claim is supported by e1.",
      "Return the supported conclusion.", "snapshot", "boundary", "dimension",
      {"e1"}, 2'000, 1'000);
}

statewright::reasoning::Hypothesis hypothesis() {
  statewright::reasoning::HypothesisProposal proposal;
  proposal.hypothesis_id = "h1";
  proposal.proposition = "The bounded claim is supported";
  proposal.prior_bp = 5'000;
  proposal.posterior_bp = 8'000;
  proposal.supporting_evidence = {"e1"};
  return statewright::reasoning::build_hypothesis_set({proposal}, problem().problem_id, 1)
      .hypotheses.front();
}

statewright::reasoning::ReasoningBudget budget(int candidate_count = 1,
                                               int attempts = 2) {
  statewright::reasoning::ReasoningBudget value;
  value.maximum_candidates = 4;
  value.candidate_count = candidate_count;
  value.max_generation_attempts = attempts;
  value.verifier_count = candidate_count;
  value.falsifier_count = 0;
  value.max_provider_calls = 8;
  value.max_steps_per_path = 4;
  return statewright::reasoning::canonicalize_reasoning_budget(std::move(value));
}

statewright::contracts::Json candidate_payload(std::string step_id,
                                                std::string claim,
                                                std::string inference = "deductive") {
  return {{"conclusion", "The bounded claim is supported."},
          {"hypothesis_ids", {"h1"}},
          {"provider_confidence_bp", 8'500},
          {"estimated_cost_bp", 500},
          {"goal_relevance_bp", 9'000},
          {"risk_bp", 1'000},
          {"steps",
           {{{"step_id", std::move(step_id)},
             {"claim", std::move(claim)},
             {"premises", {"problem.statement", "h1"}},
             {"evidence_ids", {"e1"}},
             {"inference", std::move(inference)},
             {"confidence_bp", 9'000},
             {"assumptions", statewright::contracts::Json::array()},
             {"falsifier",
              "A conflicting declared observation defeats the claim."}}}}};
}

class RecordingProvider final : public statewright::providers::ReasoningProvider {
public:
  explicit RecordingProvider(
      std::vector<statewright::contracts::Json> supplied_responses,
      int supplied_batch_size = 1)
      : responses(std::move(supplied_responses)), batch_size(supplied_batch_size) {}

  [[nodiscard]] statewright::contracts::Json create_response(
      const statewright::contracts::Json &request) override {
    requests.push_back(request);
    return responses.at(next_response++);
  }

  [[nodiscard]] int reasoning_role_batch_size() const noexcept override {
    return batch_size;
  }

  void record_reasoning_repair(
      std::string_view role, std::string_view reason,
      const std::vector<std::string> &item_ids) override {
    repairs.push_back({{"role", role}, {"reason", reason}, {"item_ids", item_ids}});
  }

  std::vector<statewright::contracts::Json> responses;
  std::vector<statewright::contracts::Json> requests;
  std::vector<statewright::contracts::Json> repairs;
  std::size_t next_response = 0;
  int batch_size = 1;
};

statewright::contracts::Json text_response(const statewright::contracts::Json &payload) {
  return {{"output_text", statewright::contracts::canonical_json(payload)}};
}

} // namespace

TEST_CASE("reasoning generator contracts and identities match frozen oracle") {
  const auto fixture = load_fixtures().at("reasoning_generator_case");
  const auto test_problem = problem();
  const auto test_hypothesis = hypothesis();

  REQUIRE(statewright::reasoning::perspective_names(10) ==
          fixture.at("perspective_names").get<std::vector<std::string>>());
  REQUIRE(statewright::reasoning::perspective_contract("direct") ==
          fixture.at("direct_contract"));
  REQUIRE(statewright::reasoning::perspective_contract("independent_probe_09") ==
          fixture.at("independent_probe_contract"));
  REQUIRE(statewright::reasoning::provider_problem_context(test_problem) ==
          fixture.at("problem_context"));
  REQUIRE(statewright::reasoning::provider_hypothesis_context(test_hypothesis) ==
          fixture.at("hypothesis_context"));
  REQUIRE(statewright::reasoning::reasoning_batch_tool("candidates") ==
          fixture.at("batch_tool"));
  REQUIRE(statewright::reasoning::reasoning_object_tool(
              {"conclusion", "steps"}, {"conclusion"}) ==
          fixture.at("object_tool"));
  REQUIRE(statewright::reasoning::to_json(
              statewright::reasoning::parse_reasoning_path(
                  candidate_payload("candidate:step",
                                    "e1 supports the bounded claim"),
                  test_problem, {test_hypothesis}, "direct", budget())) ==
          fixture.at("parsed_path"));
}

TEST_CASE("reasoning path parser normalizes problem aliases and fails closed") {
  const auto parsed = statewright::reasoning::parse_reasoning_path(
      candidate_payload("step", "Supported"), problem(), {hypothesis()}, "direct",
      budget());
  REQUIRE(parsed.steps.front().premises ==
          std::vector<std::string>{"problem", "h1"});

  auto unknown = candidate_payload("step", "Supported");
  unknown["hypothesis_ids"] = {"unknown"};
  REQUIRE_THROWS_AS(statewright::reasoning::parse_reasoning_path(
                        unknown, problem(), {hypothesis()}, "direct", budget()),
                    statewright::common::Error);

  auto duplicate = candidate_payload("step", "Supported");
  duplicate["steps"].push_back(duplicate["steps"].front());
  REQUIRE_THROWS_AS(statewright::reasoning::parse_reasoning_path(
                        duplicate, problem(), {hypothesis()}, "direct", budget()),
                    statewright::common::Error);
}

TEST_CASE("reasoning generation uses ordered micro-batches") {
  const auto first = candidate_payload("direct-step", "Direct support");
  const auto second = candidate_payload("mechanistic-step", "Mechanistic support",
                                        "constraint");
  RecordingProvider provider(
      {text_response({{"candidates", {first, second}}})}, 2);

  const auto paths = statewright::reasoning::generate_reasoning_paths(
      provider, problem(), {hypothesis()}, budget(2, 2), false, 2);
  REQUIRE(paths.size() == 2);
  REQUIRE(paths.at(0).perspective == "direct");
  REQUIRE(paths.at(1).perspective == "mechanistic");
  REQUIRE(provider.requests.size() == 1);
  REQUIRE(provider.repairs.empty());
}

TEST_CASE("reasoning generation records repair and uses next perspective") {
  RecordingProvider provider(
      {{{"output_text", "not-json"}},
       text_response(candidate_payload("mechanistic-step", "Recovered support",
                                       "constraint"))});

  const auto paths = statewright::reasoning::generate_reasoning_paths(
      provider, problem(), {hypothesis()}, budget(1, 2), false, 1);
  REQUIRE(paths.size() == 1);
  REQUIRE(paths.front().perspective == "mechanistic");
  REQUIRE(provider.repairs.size() == 1);
  REQUIRE(provider.repairs.front().at("role") == "proposer");
  REQUIRE(provider.repairs.front().at("item_ids") ==
          statewright::contracts::Json::array({"direct"}));
}
