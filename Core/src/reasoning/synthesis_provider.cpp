#include "statewright/reasoning/synthesis.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/canonical_json.hpp"
#include "statewright/reasoning/generator.hpp"
#include "statewright/reasoning/verification.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace statewright::reasoning {
namespace {

[[noreturn]] void provider_error(std::string message) {
  throw common::Error(common::ErrorCode::json_contract, std::move(message));
}

[[nodiscard]] contracts::Json survivor_payloads(
    const std::vector<ReasoningPath> &survivors) {
  auto result = contracts::Json::array();
  for (const auto &path : survivors) {
    result.push_back(to_json(path));
  }
  return result;
}

[[nodiscard]] const VerifierReport &winner_verifier(
    const ReasoningPath &winner,
    const std::vector<VerifierReport> &verifier_reports) {
  const auto found = std::ranges::find(verifier_reports, winner.path_id,
                                       &VerifierReport::path_id);
  if (found == verifier_reports.end()) {
    provider_error("synthesis winner has no verifier report");
  }
  return *found;
}

} // namespace

contracts::Json synthesizer_request(
    const ReasoningProblem &problem, const ReasoningPath &winner,
    const std::vector<ReasoningPath> &survivors) {
  const contracts::Json content = {
      {"problem", provider_problem_context(problem)},
      {"selected_winner", winner.path_id},
      {"survivors", survivor_payloads(survivors)},
      {"response_schema",
       {{"conclusion", "shortest conclusion satisfying the problem goal"},
        {"source_path_ids", {"surviving path ID"}},
        {"accepted_step_ids", {"step ID from a source path"}},
        {"rejected_step_ids", {"step ID from a source path"}},
        {"remaining_uncertainties", {"explicit uncertainty"}},
        {"confidence_bp", 0}}}};
  return {
      {"instructions",
       "Act only as an OIEC-SR synthesizer. Use only claims and steps from the supplied verified surviving paths. Call submit_oiec_reasoning_object exactly once with one concise object. Do not invent a source path, evidence, premise, step, authority, approval, or tool result. Do not reveal private chain-of-thought or change the selected winner. Follow the problem goal's answer format exactly: for a yes/no question return only 'yes' or 'no' when the verified survivor supports one. Accepted and rejected step IDs must be disjoint, and a step used in the synthesized path cannot be listed as rejected. Do not use any other tool."},
      {"input_items",
       {{{"role", "user"}, {"content", contracts::canonical_json(content)}}}},
      {"tools",
       {reasoning_object_tool(
           {"conclusion", "source_path_ids", "accepted_step_ids",
            "rejected_step_ids", "remaining_uncertainties",
            "confidence_bp"})}}};
}

SynthesisResult synthesize_verified_result(
    providers::ReasoningProvider &provider, const ReasoningProblem &problem,
    const std::vector<Hypothesis> &hypotheses, const ReasoningPath &winner,
    const std::vector<ReasoningPath> &survivors,
    const std::vector<VerifierReport> &verifier_reports,
    const std::vector<std::string> &declared_evidence_ids,
    const ReasoningBudget &budget, bool verify_synthesis,
    int verifier_batch_size) {
  const auto &verified_winner = winner_verifier(winner, verifier_reports);
  try {
    const auto responses = providers::create_provider_responses(
        provider, {synthesizer_request(problem, winner, survivors)},
        static_cast<std::size_t>(budget.max_provider_calls));
    const auto payload = providers::parse_reasoning_json_object(
        providers::response_text(responses.front()));
    auto preliminary = validate_synthesis_payload(
        problem, winner, survivors, verified_winner, payload, false);
    if (!verify_synthesis) {
      return preliminary.result;
    }
    const auto reports = verify_reasoning_paths(
        provider, problem, {preliminary.path}, hypotheses,
        declared_evidence_ids, budget, verifier_batch_size);
    if (reports.empty()) {
      provider_error("synthesis verifier returned no report");
    }
    return validate_synthesis_payload(problem, winner, survivors,
                                      verified_winner, payload, true,
                                      reports.front())
        .result;
  } catch (const common::Error &error) {
    return fallback_to_verified_winner(winner, verified_winner, {error.what()});
  }
}

std::pair<std::string, std::vector<std::string>> synthesize_conclusion(
    providers::ReasoningProvider &provider, const ReasoningProblem &problem,
    const ReasoningPath &winner,
    const std::vector<ReasoningPath> &survivors,
    const ReasoningBudget &budget) {
  const auto responses = providers::create_provider_responses(
      provider, {synthesizer_request(problem, winner, survivors)},
      static_cast<std::size_t>(budget.max_provider_calls));
  return validate_synthesized_conclusion(
      winner, survivors, providers::parse_reasoning_json_object(
                             providers::response_text(responses.front())));
}

} // namespace statewright::reasoning
