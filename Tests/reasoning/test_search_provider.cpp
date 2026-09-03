#include "statewright/reasoning/certification.hpp"
#include "statewright/reasoning/generator.hpp"
#include "statewright/reasoning/hypotheses.hpp"
#include "statewright/reasoning/search.hpp"
#include "statewright/reasoning/verification.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace {

statewright::reasoning::ReasoningProblem problem() {
  return statewright::reasoning::create_reasoning_problem(
      "Evidence e1 supports the bounded conclusion.",
      "Return the supported conclusion.", "snapshot", "boundary", "dimension",
      {"e1"}, 2'000, 1'000);
}

statewright::reasoning::Hypothesis hypothesis() {
  statewright::reasoning::HypothesisProposal proposal;
  proposal.hypothesis_id = "h1";
  proposal.proposition = "The bounded conclusion is supported";
  proposal.prior_bp = 5'000;
  proposal.posterior_bp = 8'000;
  proposal.supporting_evidence = {"e1"};
  return statewright::reasoning::build_hypothesis_set({proposal}, problem().problem_id, 1)
      .hypotheses.front();
}

statewright::reasoning::ReasoningBudget budget() {
  statewright::reasoning::ReasoningBudget value;
  value.maximum_candidates = 2;
  value.candidate_count = 1;
  value.max_generation_attempts = 1;
  value.verifier_count = 1;
  value.falsifier_count = 1;
  value.max_provider_calls = 8;
  value.max_verifier_passes = 1;
  return statewright::reasoning::canonicalize_reasoning_budget(std::move(value));
}

statewright::contracts::Json candidate_payload(std::string evidence_id = "e1") {
  return {{"conclusion", "The bounded conclusion is supported."},
          {"hypothesis_ids", {"h1"}},
          {"provider_confidence_bp", 10'000},
          {"estimated_cost_bp", 500},
          {"goal_relevance_bp", 9'000},
          {"risk_bp", 500},
          {"steps",
           {{{"step_id", "candidate:step"},
             {"claim", "Evidence supports the bounded conclusion"},
             {"premises", {"problem", "h1"}},
             {"evidence_ids", {std::move(evidence_id)}},
             {"inference", "deductive"},
             {"confidence_bp", 10'000},
             {"assumptions", statewright::contracts::Json::array()},
             {"falsifier", "A declared contradiction defeats the claim."}}}}};
}

statewright::contracts::Json checks() {
  auto result = statewright::contracts::Json::object();
  for (const auto &name : statewright::reasoning::required_process_checks()) {
    result[name] = true;
  }
  return result;
}

statewright::contracts::Json verifier_payload() {
  return {{"steps",
           {{{"step_id", "candidate:step"},
             {"checks", checks()},
             {"failures", statewright::contracts::Json::array()}}}},
          {"contradictions", statewright::contracts::Json::array()},
          {"missing_assumptions", statewright::contracts::Json::array()}};
}

statewright::contracts::Json falsifier_payload() {
  return {{"searched_falsifiers", {"declared contradiction"}},
          {"survival_bp", 9'000}};
}

statewright::contracts::Json synthesis_payload(const std::string &path_id) {
  return {{"conclusion", "The bounded conclusion is supported."},
          {"source_path_ids", {path_id}},
          {"accepted_step_ids", {"candidate:step"}},
          {"rejected_step_ids", statewright::contracts::Json::array()},
          {"remaining_uncertainties", statewright::contracts::Json::array()},
          {"confidence_bp", 8'500}};
}

statewright::contracts::Json text_response(const statewright::contracts::Json &payload) {
  return {{"output_text", statewright::contracts::canonical_json(payload)}};
}

class RecordingProvider final : public statewright::providers::ReasoningProvider {
public:
  explicit RecordingProvider(
      std::vector<statewright::contracts::Json> supplied_responses)
      : responses(std::move(supplied_responses)) {}

  [[nodiscard]] statewright::contracts::Json create_response(
      const statewright::contracts::Json &request) override {
    requests.push_back(request);
    return responses.at(next_response++);
  }

  std::vector<statewright::contracts::Json> responses;
  std::vector<statewright::contracts::Json> requests;
  std::size_t next_response = 0;
};

} // namespace

TEST_CASE("bounded provider search completes verified OIEC-SR pipeline") {
  const auto test_budget = budget();
  const auto proposed = candidate_payload();
  const auto expected_path = statewright::reasoning::parse_reasoning_path(
      proposed, problem(), {hypothesis()}, "direct", test_budget);
  RecordingProvider provider(
      {text_response(proposed), text_response(verifier_payload()),
       text_response(falsifier_payload()),
       text_response(synthesis_payload(expected_path.path_id)),
       text_response(verifier_payload())});

  const auto candidates = statewright::reasoning::search_reasoning_candidates(
      provider, problem(), {hypothesis()}, {"e1"}, test_budget);
  REQUIRE(candidates.selected_path_id == expected_path.path_id);
  REQUIRE(candidates.surviving_path_ids ==
          std::vector<std::string>{expected_path.path_id});
  REQUIRE(candidates.verifier_reports.front().verdict == "ACCEPT");
  REQUIRE(candidates.falsifier_reports.front().verdict == "SURVIVES");
  REQUIRE(candidates.synthesis.has_value());
  REQUIRE(candidates.synthesis->verified);
  REQUIRE_FALSE(candidates.synthesis->fallback_used);
  REQUIRE(provider.requests.size() == 5);
  statewright::reasoning::require_candidate_integrity(candidates);
}

TEST_CASE("provider confidence cannot self-admit undeclared evidence") {
  const auto proposed = candidate_payload("e2");
  RecordingProvider provider(
      {text_response(proposed), text_response(verifier_payload())});
  const auto candidates = statewright::reasoning::search_reasoning_candidates(
      provider, problem(), {hypothesis()}, {"e1"}, budget());

  REQUIRE(candidates.selected_path_id.empty());
  REQUIRE(candidates.surviving_path_ids.empty());
  REQUIRE(candidates.verifier_reports.front().verdict == "REJECT");
  REQUIRE(candidates.falsifier_reports.front().verdict == "REJECT");
  REQUIRE_FALSE(candidates.synthesis.has_value());
  REQUIRE(provider.requests.size() == 2);
}

TEST_CASE("verifier ablation remains explicit in synthesized output") {
  const auto test_budget = budget();
  const auto proposed = candidate_payload();
  const auto expected_path = statewright::reasoning::parse_reasoning_path(
      proposed, problem(), {hypothesis()}, "direct", test_budget);
  statewright::reasoning::AblationConfiguration profile;
  profile.ablation_id = "without_verifier";
  profile.path_count = 1;
  profile.verifier_enabled = false;
  profile.synthesis_verification_enabled = false;
  profile = statewright::reasoning::make_ablation_configuration(std::move(profile));
  RecordingProvider provider(
      {text_response(proposed), text_response(falsifier_payload()),
       text_response(synthesis_payload(expected_path.path_id))});

  const auto candidates = statewright::reasoning::search_reasoning_candidates(
      provider, problem(), {hypothesis()}, {"e1"}, test_budget, profile);
  REQUIRE(candidates.selected_path_id == expected_path.path_id);
  REQUIRE(candidates.verifier_reports.front().verdict == "ACCEPT");
  REQUIRE(candidates.synthesis.has_value());
  REQUIRE_FALSE(candidates.synthesis->verified);
  REQUIRE(candidates.synthesis->failure_reasons ==
          std::vector<std::string>{
              "synthesis verification disabled by qualification ablation"});
  REQUIRE(candidates.ablation_id == "without_verifier");
  REQUIRE(provider.requests.size() == 3);
}
