#include "statewright/reasoning/certification.hpp"
#include "statewright/reasoning/hypotheses.hpp"
#include "statewright/reasoning/verification.hpp"

#include "statewright/common/error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

statewright::reasoning::ReasoningProblem problem() {
  return statewright::reasoning::create_reasoning_problem(
      "Evidence e1 supports the bounded action.", "Return the supported answer.",
      "snapshot", "boundary", "dimension", {"e1"}, 2'000, 1'000);
}

statewright::reasoning::Hypothesis hypothesis() {
  statewright::reasoning::HypothesisProposal proposal;
  proposal.hypothesis_id = "h1";
  proposal.proposition = "The bounded action is supported";
  proposal.prior_bp = 5'000;
  proposal.posterior_bp = 7'500;
  proposal.supporting_evidence = {"e1"};
  return statewright::reasoning::build_hypothesis_set({proposal}, problem().problem_id, 1)
      .hypotheses.front();
}

statewright::reasoning::ReasoningPath path(std::string path_id,
                                           std::string step_id) {
  statewright::reasoning::ReasoningStep step;
  step.step_id = std::move(step_id);
  step.claim = "Evidence supports the bounded action";
  step.premises = {"problem", "h1"};
  step.evidence_ids = {"e1"};
  step.inference = "deductive";
  step.confidence_bp = 9'000;
  step.falsifier = "A declared counterexample defeats the claim.";
  statewright::reasoning::ReasoningPath result;
  result.path_id = std::move(path_id);
  result.perspective = "direct";
  result.hypothesis_ids = {"h1"};
  result.steps = {
      statewright::reasoning::canonicalize_reasoning_step(std::move(step))};
  result.conclusion = "The bounded action is supported.";
  result.estimated_cost_bp = 500;
  result.goal_relevance_bp = 9'000;
  return statewright::reasoning::canonicalize_reasoning_path(std::move(result));
}

statewright::reasoning::ReasoningBudget budget(int candidate_count = 2) {
  statewright::reasoning::ReasoningBudget value;
  value.maximum_candidates = 4;
  value.candidate_count = candidate_count;
  value.max_generation_attempts = candidate_count;
  value.verifier_count = candidate_count;
  value.falsifier_count = candidate_count;
  value.max_provider_calls = 8;
  value.max_verifier_passes = 2;
  return statewright::reasoning::canonicalize_reasoning_budget(std::move(value));
}

statewright::contracts::Json checks() {
  auto result = statewright::contracts::Json::object();
  for (const auto &name : statewright::reasoning::required_process_checks()) {
    result[name] = true;
  }
  return result;
}

statewright::contracts::Json verifier_payload(const std::string &step_id) {
  return {{"steps",
           {{{"step_id", step_id},
             {"checks", checks()},
             {"failures", statewright::contracts::Json::array()}}}},
          {"contradictions", statewright::contracts::Json::array()},
          {"missing_assumptions", statewright::contracts::Json::array()}};
}

statewright::contracts::Json compact_payload(const std::string &step_id) {
  return {{"steps",
           {{{"step_id", step_id},
             {"all_checks_evaluated", true},
             {"failed_checks", statewright::contracts::Json::array()},
             {"failures", statewright::contracts::Json::array()}}}},
          {"contradictions", statewright::contracts::Json::array()},
          {"missing_assumptions", statewright::contracts::Json::array()}};
}

statewright::contracts::Json falsifier_payload() {
  return {{"searched_falsifiers", {"declared counterexample"}},
          {"survival_bp", 9'000}};
}

statewright::contracts::Json text_response(const statewright::contracts::Json &payload) {
  return {{"output_text", statewright::contracts::canonical_json(payload)}};
}

class RecordingProvider final : public statewright::providers::ReasoningProvider {
public:
  explicit RecordingProvider(
      std::vector<statewright::contracts::Json> supplied_responses,
      int supplied_batch_size)
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

} // namespace

TEST_CASE("compact verifier reports expand into complete process checks") {
  const auto expanded = statewright::reasoning::expand_compact_verifier_payload(
      compact_payload("p1:step"));
  REQUIRE(expanded.at("steps").front().at("checks") == checks());

  auto duplicate = compact_payload("p1:step");
  duplicate["steps"].front()["failed_checks"] =
      {"evidence_relevant", "evidence_relevant"};
  REQUIRE_THROWS_AS(
      statewright::reasoning::expand_compact_verifier_payload(duplicate),
      statewright::common::Error);
}

TEST_CASE("verifier micro-batch falls back to split requests after schema failure") {
  const auto first = path("p1", "p1:step");
  const auto second = path("p2", "p2:step");
  RecordingProvider provider(
      {text_response({{"reports", {{{"steps", "invalid"}},
                                     {{"steps", "invalid"}}}}}),
       text_response(verifier_payload("p1:step")),
       text_response(verifier_payload("p2:step"))},
      2);

  const auto reports = statewright::reasoning::verify_reasoning_paths(
      provider, problem(), {first, second}, {hypothesis()}, {"e1"}, budget(), 2);
  REQUIRE(reports.size() == 2);
  REQUIRE(reports.at(0).path_id == "p1");
  REQUIRE(reports.at(1).path_id == "p2");
  REQUIRE(reports.at(0).verdict == "ACCEPT");
  REQUIRE(reports.at(1).verdict == "ACCEPT");
  REQUIRE(provider.requests.size() == 3);
  REQUIRE(provider.repairs.size() == 1);
  REQUIRE(provider.repairs.front().at("role") == "verifier_batch");
}

TEST_CASE("verifier performs one bounded deterministic repair pass") {
  RecordingProvider provider(
      {text_response(verifier_payload("unknown-step")),
       text_response(verifier_payload("p1:step"))},
      1);
  const auto reports = statewright::reasoning::verify_reasoning_paths(
      provider, problem(), {path("p1", "p1:step")}, {hypothesis()}, {"e1"},
      budget(1), 1);
  REQUIRE(reports.size() == 1);
  REQUIRE(reports.front().verdict == "ACCEPT");
  REQUIRE(provider.requests.size() == 2);
  REQUIRE(provider.requests.back().at("instructions").get<std::string>().find(
              "schema repairer") != std::string::npos);
}

TEST_CASE("falsifier micro-batch preserves candidate ordering") {
  const auto first = path("p1", "p1:step");
  const auto second = path("p2", "p2:step");
  RecordingProvider provider(
      {text_response({{"reports", {falsifier_payload(), falsifier_payload()}}})},
      2);
  const auto reports = statewright::reasoning::falsify_reasoning_paths(
      provider, problem(), {first, second}, budget(), 2);
  REQUIRE(reports.size() == 2);
  REQUIRE(reports.at(0).path_id == "p1");
  REQUIRE(reports.at(1).path_id == "p2");
  REQUIRE(reports.at(0).verdict == "SURVIVES");
  REQUIRE(reports.at(1).verdict == "SURVIVES");
  REQUIRE(provider.requests.size() == 1);
}
