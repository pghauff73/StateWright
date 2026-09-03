#include "statewright/common/error.hpp"
#include "statewright/reasoning/verification.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>
#include <utility>

namespace {

statewright::contracts::Json load_fixtures() {
  std::ifstream input(STATEWRIGHT_ORACLE_FIXTURES);
  REQUIRE(input.is_open());
  return statewright::contracts::Json::parse(input);
}

statewright::reasoning::ReasoningPath path() {
  statewright::reasoning::ReasoningStep step;
  step.step_id = "p1:step";
  step.claim = "Evidence supports the bounded action";
  step.premises = {"problem", "h1"};
  step.evidence_ids = {"e1"};
  step.inference = "deductive";
  step.confidence_bp = 9'000;
  step.assumptions = {"Bounded input"};
  step.falsifier = "A counterexample defeats the claim.";
  statewright::reasoning::ReasoningPath result;
  result.path_id = "p1";
  result.perspective = "direct";
  result.hypothesis_ids = {"h1"};
  result.steps = {
      statewright::reasoning::canonicalize_reasoning_step(std::move(step))};
  result.conclusion = "Evidence supports bounded action";
  result.estimated_cost_bp = 500;
  result.goal_relevance_bp = 9'000;
  return statewright::reasoning::canonicalize_reasoning_path(std::move(result));
}

statewright::reasoning::Hypothesis hypothesis() {
  statewright::reasoning::HypothesisProposal proposal;
  proposal.hypothesis_id = "h1";
  proposal.proposition = "The bounded action is supported";
  proposal.prior_bp = 5'000;
  proposal.posterior_bp = 7'500;
  proposal.supporting_evidence = {"e1"};
  proposal.assumptions = {"Bounded input"};
  return statewright::reasoning::build_hypothesis_set({proposal}, "problem", 1)
      .hypotheses.front();
}

statewright::contracts::Json all_checks() {
  statewright::contracts::Json checks = statewright::contracts::Json::object();
  for (const auto &name : statewright::reasoning::required_process_checks()) {
    checks[name] = true;
  }
  return checks;
}

statewright::contracts::Json verifier_payload() {
  return {{"steps",
           {{{"step_id", "p1:step"},
             {"checks", all_checks()},
             {"failures", statewright::contracts::Json::array()}}}},
          {"contradictions", statewright::contracts::Json::array()},
          {"missing_assumptions", statewright::contracts::Json::array()}};
}

} // namespace

TEST_CASE("OIEC-SR verifier and falsifier reports match frozen fixtures") {
  const auto fixture = load_fixtures().at("reasoning_validation_case");
  REQUIRE(statewright::reasoning::required_process_checks() ==
          fixture.at("process_checks").get<std::vector<std::string>>());
  const auto verified = statewright::reasoning::verify_reasoning_path(
      path(), {hypothesis()}, {"e1"}, verifier_payload());
  REQUIRE(statewright::reasoning::to_json(verified) ==
          fixture.at("verified_report"));

  const auto alternative = statewright::reasoning::falsify_reasoning_path(
      path(), {{"searched_falsifiers", {"alternative"}},
               {"alternative_explanations", {"A second mechanism fits."}},
               {"survival_bp", 9'000}});
  REQUIRE(statewright::reasoning::to_json(alternative) ==
          fixture.at("alternative_report"));

  const auto future = statewright::reasoning::falsify_reasoning_path(
      path(),
      {{"searched_falsifiers", {"possible future confound"}},
       {"unresolved_defeat_conditions",
        {"A future confound may be discovered."}},
       {"evidence_reversal_conditions", statewright::contracts::Json::array()},
       {"survival_bp", 9'000}},
      std::vector<std::string>{"e1"});
  REQUIRE(statewright::reasoning::to_json(future) ==
          fixture.at("future_defeat_report"));
}

TEST_CASE("OIEC-SR verifier normalizes only the declared compatibility alias") {
  auto checks = all_checks();
  checks["alternative_consideered"] = checks.at("alternative_considered");
  checks.erase("alternative_considered");
  const auto normalized =
      statewright::reasoning::normalize_process_checks(checks);
  REQUIRE(normalized.at("alternative_considered"));

  auto unknown = all_checks();
  unknown["grounding_traceble"] = unknown.at("grounding_traceable");
  unknown.erase("grounding_traceable");
  REQUIRE_THROWS_AS(statewright::reasoning::normalize_process_checks(unknown),
                    statewright::common::Error);

  auto ambiguous = all_checks();
  ambiguous["alternative_consideered"] = true;
  REQUIRE_THROWS_AS(statewright::reasoning::normalize_process_checks(ambiguous),
                    statewright::common::Error);
}

TEST_CASE("OIEC-SR verifier overrides unsupported provider assertions") {
  auto invalid_path = path();
  invalid_path.steps.front().evidence_ids = {"undeclared"};
  const auto report = statewright::reasoning::verify_reasoning_path(
      invalid_path, {hypothesis()}, {"e1"}, verifier_payload());
  REQUIRE(report.verdict == "REJECT");
  REQUIRE(report.score_bp == 0);
  REQUIRE(report.failures ==
          std::vector<std::string>{"p1:step: undeclared evidence"});
}

TEST_CASE("OIEC-SR falsifier preserves epistemic defeat distinctions") {
  const auto alternative = statewright::reasoning::falsify_reasoning_path(
      path(), {{"alternative_explanations", {"A second mechanism fits."}},
               {"survival_bp", 9'000}});
  REQUIRE(alternative.verdict == "REVISE");
  REQUIRE(alternative.survival_bp == 6'000);

  const auto grounded = statewright::reasoning::falsify_reasoning_path(
      path(),
      {{"unresolved_defeat_conditions", {"The trace contains a conflict."}},
       {"unresolved_defeat_evidence_ids", {"e1"}},
       {"survival_bp", 9'000}},
      std::vector<std::string>{"e1"});
  REQUIRE(grounded.verdict == "SURVIVES");
  REQUIRE(grounded.unresolved_defeat_conditions ==
          std::vector<std::string>{"The trace contains a conflict."});
  REQUIRE(grounded.residual_uncertainty_bp == 1'000);

  REQUIRE_THROWS_AS(
      statewright::reasoning::falsify_reasoning_path(
          path(),
          {{"unresolved_defeat_conditions", {"Current conflict."}},
           {"unresolved_defeat_evidence_ids", {"unknown"}},
           {"survival_bp", 9'000}},
          std::vector<std::string>{"e1"}),
      statewright::common::Error);
}

TEST_CASE("OIEC-SR critical falsification is terminal for the candidate") {
  const auto report = statewright::reasoning::falsify_reasoning_path(
      path(), {{"critical", true}, {"survival_bp", 9'000}});
  REQUIRE(report.verdict == "REJECT");
  REQUIRE(report.survival_bp == 0);
  REQUIRE(report.severity_bp == 10'000);
}
