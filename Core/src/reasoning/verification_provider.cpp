#include "statewright/reasoning/verification.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/canonical_json.hpp"
#include "statewright/reasoning/generator.hpp"

#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::reasoning {
namespace {

[[noreturn]] void provider_error(std::string message) {
  throw common::Error(common::ErrorCode::json_contract, std::move(message));
}

[[nodiscard]] bool repairable(const common::Error &error) noexcept {
  return error.code() == common::ErrorCode::json_contract;
}

[[nodiscard]] contracts::Json hypothesis_contexts(
    const std::vector<Hypothesis> &hypotheses) {
  auto result = contracts::Json::array();
  for (const auto &hypothesis : hypotheses) {
    result.push_back(provider_hypothesis_context(hypothesis));
  }
  return result;
}

[[nodiscard]] contracts::Json path_payloads(
    const std::vector<ReasoningPath> &paths) {
  auto result = contracts::Json::array();
  for (const auto &path : paths) {
    result.push_back(to_json(path));
  }
  return result;
}

[[nodiscard]] contracts::Json process_check_example() {
  auto result = contracts::Json::object();
  for (const auto &name : required_process_checks()) {
    result[name] = true;
  }
  return result;
}

[[nodiscard]] contracts::Json verifier_schema() {
  return {{"steps",
           {{{"step_id", "candidate step ID"},
             {"checks", process_check_example()},
             {"failures", {"concise failure"}}}}},
          {"contradictions", {"concise contradiction"}},
          {"missing_assumptions",
           {"implicit assumption that must be explicit"}}};
}

[[nodiscard]] contracts::Json falsifier_schema() {
  return {{"searched_falsifiers", {"condition searched"}},
          {"counterexamples", {"counterexample found"}},
          {"alternative_explanations", {"competing explanation"}},
          {"boundary_cases", {"boundary value or scope case"}},
          {"reversed_causal_directions", {"plausible reversed direction"}},
          {"invalid_invariants", {"incorrectly held invariant"}},
          {"evidence_reversal_conditions",
           {"evidence that reverses the conclusion"}},
          {"contradicted_step_ids", {"candidate step ID"}},
          {"unresolved_defeat_conditions", {"remaining test"}},
          {"unresolved_defeat_evidence_ids",
           {"declared evidence ID grounding a current unresolved defeat"}},
          {"critical", false},
          {"survival_bp", 0}};
}

} // namespace

contracts::Json verifier_request(
    const ReasoningProblem &problem, const ReasoningPath &path,
    const std::vector<Hypothesis> &hypotheses) {
  const contracts::Json content = {
      {"problem", provider_problem_context(problem)},
      {"hypotheses", hypothesis_contexts(hypotheses)},
      {"candidate", to_json(path)},
      {"verification_contract",
       {{"top_level_type", "object"},
        {"required_top_level_keys",
         {"steps", "contradictions", "missing_assumptions"}},
        {"problem_is_validated_premise", true},
        {"control_metadata_is_evidence", false},
        {"all_declared_evidence_required_per_step", false},
        {"external_unstated_context_allowed", false}}},
      {"required_checks", required_process_checks()},
      {"response_schema", verifier_schema()}};
  return {
      {"instructions",
       "Act only as an independent OIEC-SR process verifier. Inspect the supplied candidate without assuming it is correct. Call submit_oiec_reasoning_object exactly once with one object containing one entry per step and every named boolean check. Do not reveal private chain-of-thought, use any other tool, approve actions, or choose a final winner. Treat facts explicitly stated in the problem as an available validated premise named 'problem'. Only declared evidence IDs are evidence; governance hashes and signatures are not factual support. Do not require every declared evidence ID on every step, do not invent external evidence, and evaluate the closed supplied task rather than hypothetical unstated context. The problem statement supplies the task facts and declared evidence IDs are references to those facts; do not reject a step merely because raw artifact bodies are not embedded. A stated missing artifact can validly support a conclusion that a claim is not proven. Return exactly one top-level JSON object with keys 'steps', 'contradictions', and 'missing_assumptions'; never return a bare array or Markdown fence."},
      {"input_items",
       {{{"role", "user"}, {"content", contracts::canonical_json(content)}}}},
      {"tools",
       {reasoning_object_tool(
           {"steps", "contradictions", "missing_assumptions"})}}};
}

contracts::Json verifier_repair_request(
    const ReasoningProblem &problem, const ReasoningPath &path,
    const std::vector<Hypothesis> &hypotheses, std::string invalid_response,
    std::string validation_error) {
  const contracts::Json content = {
      {"problem", provider_problem_context(problem)},
      {"hypotheses", hypothesis_contexts(hypotheses)},
      {"candidate", to_json(path)},
      {"validation_error", std::move(validation_error)},
      {"invalid_response", std::move(invalid_response)},
      {"required_checks", required_process_checks()},
      {"required_top_level_keys",
       {"steps", "contradictions", "missing_assumptions"}},
      {"response_schema", verifier_schema()}};
  return {
      {"instructions",
       "Act only as an OIEC-SR verifier schema repairer. The previous verifier response failed deterministic validation. Call submit_oiec_reasoning_object exactly once with one corrected top-level object. Preserve the previous boolean judgments and failure meaning; repair keys, types, step coverage, and object shape only. Do not add evidence, change the candidate, choose a winner, use any other tool, or reveal private chain-of-thought. Every step checks object must contain each exact required check name once, with a JSON boolean value."},
      {"input_items",
       {{{"role", "user"}, {"content", contracts::canonical_json(content)}}}},
      {"tools",
       {reasoning_object_tool(
           {"steps", "contradictions", "missing_assumptions"})}}};
}

contracts::Json verifier_batch_request(
    const ReasoningProblem &problem, const std::vector<ReasoningPath> &paths,
    const std::vector<Hypothesis> &hypotheses) {
  if (paths.empty()) {
    provider_error("structured verifier request requires at least one candidate");
  }
  const contracts::Json compact_schema = {
      {"reports",
       {{{"steps",
          {{{"step_id", "candidate step ID"},
            {"all_checks_evaluated", true},
            {"failed_checks", {"one required check name"}},
            {"failures", {"concise failure"}}}}},
         {"contradictions", {"concise contradiction"}},
         {"missing_assumptions",
          {"implicit assumption that must be explicit"}}}}}};
  const contracts::Json content = {
      {"problem", provider_problem_context(problem)},
      {"hypotheses", hypothesis_contexts(hypotheses)},
      {"candidates", path_payloads(paths)},
      {"verification_contract",
       {{"top_level_type", "object"},
        {"required_top_level_keys",
         {"steps", "contradictions", "missing_assumptions"}},
        {"problem_is_validated_premise", true},
        {"control_metadata_is_evidence", false},
        {"all_declared_evidence_required_per_step", false},
        {"external_unstated_context_allowed", false}}},
      {"required_checks", required_process_checks()},
      {"response_schema", compact_schema}};
  return {
      {"instructions",
       "Act only as an independent OIEC-SR process verifier micro-batch. Call submit_oiec_reasoning_batch exactly once with a 'reports' array. Verify every supplied candidate independently and return reports in the exact candidate order. Each array entry is the ordinary verifier object. Do not reorder or compare candidates, choose a winner, transfer support between paths, or use proposer confidence as authority. Evaluate every required check for every candidate step. In the compact wire format, list only failed check names; an empty list means all required checks passed, and all_checks_evaluated must be true. Do not reveal private chain-of-thought, use tools, or invent evidence."},
      {"input_items",
       {{{"role", "user"}, {"content", contracts::canonical_json(content)}}}},
      {"tools", {reasoning_batch_tool("reports")}},
      {"max_output_tokens", verifier_batch_max_output_tokens},
      {"allow_invalid_json", true}};
}

contracts::Json expand_compact_verifier_payload(
    const contracts::Json &payload) {
  if (!payload.is_object()) {
    provider_error("compact verifier response must be an object");
  }
  const auto found = payload.find("steps");
  if (found == payload.end() || !found->is_array()) {
    provider_error("compact verifier response steps must be an array");
  }
  const contracts::Json empty = contracts::Json::array();
  auto steps = contracts::Json::array();
  for (const auto &raw_step : *found) {
    if (!raw_step.is_object()) {
      provider_error("compact verifier step must be an object");
    }
    if (!raw_step.contains("all_checks_evaluated") ||
        !raw_step.at("all_checks_evaluated").is_boolean() ||
        !raw_step.at("all_checks_evaluated").get<bool>()) {
      provider_error("compact verifier must affirm evaluation of all checks");
    }
    const auto failed_value = raw_step.find("failed_checks");
    const contracts::Json &failed =
        failed_value == raw_step.end() ? empty : *failed_value;
    if (!failed.is_array()) {
      provider_error("compact verifier failed_checks must be an array");
    }
    std::set<std::string> failed_names;
    for (const auto &name : failed) {
      if (!name.is_string()) {
        provider_error("compact verifier failed_checks must contain strings");
      }
      const std::string value = name.get<std::string>();
      if (!failed_names.insert(value).second) {
        provider_error("compact verifier failed_checks contains duplicates");
      }
      if (std::ranges::find(required_process_checks(), value) ==
          required_process_checks().end()) {
        provider_error("compact verifier references an unknown process check");
      }
    }
    const auto failures_value = raw_step.find("failures");
    const contracts::Json &failures =
        failures_value == raw_step.end() ? empty : *failures_value;
    if (!failures.is_array()) {
      provider_error("compact verifier failures must be an array");
    }
    auto checks = contracts::Json::object();
    for (const auto &name : required_process_checks()) {
      checks[name] = !failed_names.contains(name);
    }
    steps.push_back(
        {{"step_id", raw_step.value("step_id", "")},
         {"checks", std::move(checks)},
         {"failures", failures}});
  }
  return {{"steps", std::move(steps)},
          {"contradictions", payload.value("contradictions", empty)},
          {"missing_assumptions", payload.value("missing_assumptions", empty)}};
}

std::vector<VerifierReport> verify_reasoning_paths(
    providers::ReasoningProvider &provider, const ReasoningProblem &problem,
    const std::vector<ReasoningPath> &paths,
    const std::vector<Hypothesis> &hypotheses,
    const std::vector<std::string> &declared_evidence_ids,
    const ReasoningBudget &budget, int role_batch_size) {
  const int batch_size =
      providers::reasoning_role_batch_size(provider, role_batch_size);
  const bool structured_batches = batch_size > 1;
  struct Received final {
    ReasoningPath path;
    contracts::Json payload;
    std::string raw_text;
  };
  std::vector<Received> received;
  for (std::size_t start = 0; start < paths.size();
       start += static_cast<std::size_t>(batch_size)) {
    const auto end = std::min(paths.size(),
                              start + static_cast<std::size_t>(batch_size));
    const std::vector<ReasoningPath> group(
        paths.begin() + static_cast<std::ptrdiff_t>(start),
        paths.begin() + static_cast<std::ptrdiff_t>(end));
    std::vector<contracts::Json> payloads;
    if (group.size() == 1 && !structured_batches) {
      const auto responses = providers::create_provider_responses(
          provider, {verifier_request(problem, group.front(), hypotheses)},
          static_cast<std::size_t>(budget.max_provider_calls));
      const std::string raw = providers::response_text(responses.front());
      received.push_back(
          {group.front(), providers::parse_reasoning_json_object(raw), raw});
      continue;
    }
    try {
      const auto responses = providers::create_provider_responses(
          provider, {verifier_batch_request(problem, group, hypotheses)},
          static_cast<std::size_t>(budget.max_provider_calls));
      payloads = providers::parse_ordered_role_batch_payloads(
          responses.front(), "reports", group.size());
      for (auto &payload : payloads) {
        payload = expand_compact_verifier_payload(payload);
      }
    } catch (const common::Error &error) {
      if (!repairable(error) || budget.max_verifier_passes < 2) {
        throw;
      }
      std::vector<std::string> path_ids;
      std::vector<contracts::Json> requests;
      for (const auto &path : group) {
        path_ids.push_back(path.path_id);
        requests.push_back(verifier_request(problem, path, hypotheses));
      }
      provider.record_reasoning_repair("verifier_batch", error.what(), path_ids);
      payloads.clear();
      const auto responses = providers::create_provider_responses(
          provider, requests,
          static_cast<std::size_t>(budget.max_provider_calls));
      for (const auto &response : responses) {
        payloads.push_back(providers::parse_reasoning_json_object(
            providers::response_text(response)));
      }
    }
    for (std::size_t index = 0; index < group.size(); ++index) {
      received.push_back(
          {group[index], payloads[index], payloads[index].dump()});
    }
  }

  std::vector<VerifierReport> reports;
  for (const auto &item : received) {
    try {
      reports.push_back(verify_reasoning_path(
          item.path, hypotheses, declared_evidence_ids, item.payload));
    } catch (const common::Error &error) {
      if (!repairable(error) || budget.max_verifier_passes < 2) {
        throw;
      }
      const auto responses = providers::create_provider_responses(
          provider,
          {verifier_repair_request(problem, item.path, hypotheses,
                                   item.raw_text, error.what())},
          static_cast<std::size_t>(budget.max_provider_calls));
      reports.push_back(verify_reasoning_path(
          item.path, hypotheses, declared_evidence_ids,
          providers::parse_reasoning_json_object(
              providers::response_text(responses.front()))));
    }
  }
  return reports;
}

contracts::Json falsifier_request(const ReasoningProblem &problem,
                                   const ReasoningPath &path) {
  const contracts::Json content = {
      {"problem", provider_problem_context(problem)},
      {"candidate", to_json(path)},
      {"falsification_contract",
       {{"closed_supplied_task", true},
        {"not_proven_is_not_proven_false", true},
        {"future_reversal_is_current_counterexample", false},
        {"defeat_must_be_present_in_task", true},
        {"alternate_definition_is_current_defeat", false},
        {"top_level_type", "object"}}},
      {"response_schema", falsifier_schema()}};
  return {
      {"instructions",
       "Act only as an adversarial OIEC-SR falsifier. Search for observations, counterexamples, interpretations, or tests that would make the candidate wrong. Call submit_oiec_reasoning_object exactly once with one concise object. Do not reveal private chain-of-thought, use any other tool, approve actions, or protect the candidate. Evaluate only the closed supplied task. Distinguish 'not proven' from 'proven false': observing that missing evidence prevents verification supports a 'not proven' conclusion and is not a reversed causal direction or alternative explanation. A future artifact that would reverse the conclusion belongs only in evidence_reversal_conditions and is not a current counterexample. Do not weaken the problem's word 'proven' into plausible, useful, or sufficient for a different purpose. Populate defeat fields only with a condition already supported by the supplied task that actually defeats the candidate. Return exactly one top-level JSON object, never a bare array or Markdown fence."},
      {"input_items",
       {{{"role", "user"}, {"content", contracts::canonical_json(content)}}}},
      {"tools",
       {reasoning_object_tool(
           {"searched_falsifiers", "counterexamples",
            "alternative_explanations", "boundary_cases",
            "reversed_causal_directions", "invalid_invariants",
            "evidence_reversal_conditions", "contradicted_step_ids",
            "unresolved_defeat_conditions",
            "unresolved_defeat_evidence_ids", "critical", "survival_bp"})}}};
}

contracts::Json falsifier_batch_request(
    const ReasoningProblem &problem, const std::vector<ReasoningPath> &paths) {
  if (paths.size() < 2) {
    provider_error("batched falsifier request requires at least two candidates");
  }
  const contracts::Json content = {
      {"problem", provider_problem_context(problem)},
      {"candidates", path_payloads(paths)},
      {"falsification_contract",
       {{"closed_supplied_task", true},
        {"not_proven_is_not_proven_false", true},
        {"future_reversal_is_current_counterexample", false},
        {"defeat_must_be_present_in_task", true},
        {"alternate_definition_is_current_defeat", false},
        {"top_level_type", "object"}}},
      {"response_schema", {{"reports", {falsifier_schema()}}}}};
  return {
      {"instructions",
       "Act only as an adversarial OIEC-SR falsifier micro-batch. Call submit_oiec_reasoning_batch exactly once with a 'reports' array. Challenge every supplied candidate independently and return reports in the exact candidate order. Each array entry is the ordinary falsifier object. Do not reorder or compare candidates, protect one candidate because another is weaker, transfer a defeat between paths, or invent evidence. Evaluate only the closed supplied task. Do not reveal private chain-of-thought, use tools, approve actions, or claim authority."},
      {"input_items",
       {{{"role", "user"}, {"content", contracts::canonical_json(content)}}}},
      {"tools", {reasoning_batch_tool("reports")}},
      {"max_output_tokens", falsifier_batch_max_output_tokens}};
}

std::vector<FalsifierReport> falsify_reasoning_paths(
    providers::ReasoningProvider &provider, const ReasoningProblem &problem,
    const std::vector<ReasoningPath> &paths, const ReasoningBudget &budget,
    int role_batch_size) {
  const int batch_size =
      providers::reasoning_role_batch_size(provider, role_batch_size);
  std::vector<std::pair<ReasoningPath, contracts::Json>> received;
  for (std::size_t start = 0; start < paths.size();
       start += static_cast<std::size_t>(batch_size)) {
    const auto end = std::min(paths.size(),
                              start + static_cast<std::size_t>(batch_size));
    const std::vector<ReasoningPath> group(
        paths.begin() + static_cast<std::ptrdiff_t>(start),
        paths.begin() + static_cast<std::ptrdiff_t>(end));
    std::vector<contracts::Json> payloads;
    if (group.size() == 1) {
      const auto responses = providers::create_provider_responses(
          provider, {falsifier_request(problem, group.front())},
          static_cast<std::size_t>(budget.max_provider_calls));
      payloads.push_back(providers::parse_reasoning_json_object(
          providers::response_text(responses.front())));
    } else {
      const auto responses = providers::create_provider_responses(
          provider, {falsifier_batch_request(problem, group)},
          static_cast<std::size_t>(budget.max_provider_calls));
      payloads = providers::parse_ordered_role_batch_payloads(
          responses.front(), "reports", group.size());
    }
    for (std::size_t index = 0; index < group.size(); ++index) {
      received.emplace_back(group[index], payloads[index]);
    }
  }
  std::vector<FalsifierReport> reports;
  for (const auto &[path, payload] : received) {
    reports.push_back(falsify_reasoning_path(path, payload, problem.evidence_ids));
  }
  return reports;
}

} // namespace statewright::reasoning
