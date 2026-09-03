#include "statewright/reasoning/generator.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/reasoning/topology.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::reasoning {
namespace {

constexpr std::array<std::string_view, 8> independent_probe_modes{
    "constraint", "defeasible", "probabilistic", "deductive", "inductive",
    "abductive", "causal", "computational"};
constexpr std::array<std::string_view, 8> independent_probe_foci{
    "boundary conditions", "weakest premise", "evidence conflict",
    "alternative explanation", "scope qualification", "prediction failure",
    "dependency reversal", "finite consistency check"};

[[noreturn]] void policy_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied, std::move(message));
}

[[noreturn]] void provider_error(std::string message) {
  throw common::Error(common::ErrorCode::json_contract, std::move(message));
}

[[nodiscard]] std::string trim(std::string value) {
  const auto first = std::ranges::find_if_not(value, [](unsigned char byte) {
    return std::isspace(byte) != 0;
  });
  const auto last = std::find_if_not(value.rbegin(), value.rend(),
                                     [](unsigned char byte) {
                                       return std::isspace(byte) != 0;
                                     })
                        .base();
  if (first >= last) {
    return {};
  }
  return std::string(first, last);
}

[[nodiscard]] std::vector<std::string>
stable_strings(const std::vector<std::string> &values) {
  std::set<std::string> seen;
  std::vector<std::string> result;
  for (const auto &value : values) {
    if (!value.empty() && seen.insert(value).second) {
      result.push_back(value);
    }
  }
  return result;
}

[[nodiscard]] std::string scalar_string(const contracts::Json &value,
                                        std::string_view label) {
  if (value.is_string()) {
    return value.get<std::string>();
  }
  if (value.is_null()) {
    return "None";
  }
  if (value.is_boolean()) {
    return value.get<bool>() ? "True" : "False";
  }
  if (value.is_number()) {
    return value.dump();
  }
  provider_error(std::string(label) + " must contain scalar values");
}

[[nodiscard]] std::vector<std::string>
payload_strings(const contracts::Json &payload, std::string_view key) {
  const auto found = payload.find(std::string(key));
  if (found == payload.end()) {
    return {};
  }
  if (!found->is_array()) {
    provider_error("proposer response " + std::string(key) +
                   " must be an array");
  }
  std::vector<std::string> values;
  for (const auto &item : *found) {
    values.push_back(scalar_string(item, key));
  }
  return stable_strings(values);
}

[[nodiscard]] int payload_score(const contracts::Json &payload,
                                std::string_view key,
                                std::string_view label) {
  const auto found = payload.find(std::string(key));
  int value = 0;
  if (found != payload.end()) {
    if (found->is_number_integer()) {
      value = found->get<int>();
    } else if (found->is_string()) {
      try {
        std::size_t consumed = 0;
        const std::string text = found->get<std::string>();
        value = std::stoi(text, &consumed);
        if (consumed != text.size()) {
          provider_error(std::string(label) + " must be an integer");
        }
      } catch (const common::Error &) {
        throw;
      } catch (const std::exception &) {
        provider_error(std::string(label) + " must be an integer");
      }
    } else {
      provider_error(std::string(label) + " must be an integer");
    }
  }
  if (value < 0 || value > score_scale) {
    provider_error(std::string(label) + " must be 0..10000");
  }
  return value;
}

[[nodiscard]] contracts::Json declared_perspective_contract(
    std::string_view name) {
  if (name == "direct") {
    return {{"contract_type", "declared_perspective"},
            {"objective",
             "Derive the shortest supported answer from explicit task facts."},
            {"primary_inference_mode", "deductive"},
            {"fallback_inference_modes", {"constraint", "defeasible"}},
            {"required_path_shape",
             {"Begin with the decisive supplied premise or constraint.",
              "Use no intermediate mechanism unless the conclusion requires one."}},
            {"falsifier_focus",
             "Identify the smallest supplied fact change that defeats the conclusion."}};
  }
  if (name == "mechanistic") {
    return {{"contract_type", "declared_perspective"},
            {"objective",
             "Explain the dependency or state-transition mechanism that produces the answer."},
            {"primary_inference_mode", "causal"},
            {"fallback_inference_modes", {"constraint", "deductive"}},
            {"required_path_shape",
             {"Identify an intermediate dependency, state transition, or enabling condition.",
              "Connect that mechanism to the conclusion without inventing external facts."}},
            {"falsifier_focus",
             "Identify the mechanism link whose failure would defeat the path."}};
  }
  if (name == "counterexample_first") {
    return {{"contract_type", "declared_perspective"},
            {"objective",
             "Test the strongest plausible answer against a task-grounded counterexample first."},
            {"primary_inference_mode", "defeasible"},
            {"fallback_inference_modes", {"deductive", "constraint"}},
            {"required_path_shape",
             {"State the strongest present counterexample or boundary case before the conclusion.",
              "Explain whether that challenge defeats, qualifies, or leaves the answer intact."}},
            {"falsifier_focus",
             "Use a concrete present counterexample, not hypothetical future evidence."}};
  }
  if (name == "assumption_inversion") {
    return {{"contract_type", "declared_perspective"},
            {"objective",
             "Invert one genuinely necessary candidate assumption and compare consequences."},
            {"primary_inference_mode", "abductive"},
            {"fallback_inference_modes", {"defeasible", "constraint"}},
            {"required_path_shape",
             {"Name no assumption when the supplied facts already settle the task.",
              "Otherwise compare the original and inverted assumption against the same supplied facts."}},
            {"falsifier_focus",
             "Identify an observation that makes the inverted assumption fit better."}};
  }
  if (name == "causal") {
    return {{"contract_type", "declared_perspective"},
            {"objective",
             "Test causal direction, intervention relevance, and possible confounding."},
            {"primary_inference_mode", "causal"},
            {"fallback_inference_modes", {"probabilistic", "constraint"}},
            {"required_path_shape",
             {"Separate association or sequence from an asserted causal relation.",
              "State whether an intervention claim is supported by the supplied task facts."}},
            {"falsifier_focus",
             "Identify reversed direction, confounding, or absent intervention support."}};
  }
  if (name == "mathematical") {
    return {{"contract_type", "declared_perspective"},
            {"objective",
             "Formalize the relevant quantities, relations, or finite constraints and check them."},
            {"primary_inference_mode", "computational"},
            {"fallback_inference_modes", {"deductive", "constraint"}},
            {"required_path_shape",
             {"Express at least one task-grounded relation or finite comparison explicitly.",
              "Do not invent numerical values when the task supplies none."}},
            {"falsifier_focus",
             "Identify a boundary value, counter-calculation, or violated relation."}};
  }
  if (name == "evidence_synthesis") {
    return {{"contract_type", "declared_perspective"},
            {"objective",
             "Compare the supplied evidence coverage, agreement, and conflict before answering."},
            {"primary_inference_mode", "inductive"},
            {"fallback_inference_modes", {"probabilistic", "defeasible"}},
            {"required_path_shape",
             {"Account for supporting and conflicting supplied evidence without fabricating artifacts.",
              "Calibrate the conclusion to the weakest material evidence link."}},
            {"falsifier_focus",
             "Identify the supplied or obtainable evidence class that would reverse the balance."}};
  }
  if (name == "abductive") {
    return {{"contract_type", "declared_perspective"},
            {"objective",
             "Compare competing explanations by fit, assumptions, and residual uncertainty."},
            {"primary_inference_mode", "abductive"},
            {"fallback_inference_modes", {"probabilistic", "defeasible"}},
            {"required_path_shape",
             {"Contrast at least two task-compatible explanations when two exist.",
              "Prefer the explanation requiring the fewest unsupported assumptions."}},
            {"falsifier_focus",
             "Identify an observation that would make an alternative explanation superior."}};
  }
  return nullptr;
}

[[nodiscard]] contracts::Json proposer_schema() {
  return {{"conclusion", "concise candidate conclusion"},
          {"hypothesis_ids", {"declared hypothesis ID"}},
          {"provider_confidence_bp", 0},
          {"estimated_cost_bp", 0},
          {"goal_relevance_bp", 0},
          {"risk_bp", 0},
          {"steps",
           {{{"step_id", "step-01"},
             {"claim", "auditable claim"},
             {"premises", {"problem, hypothesis ID, or prior step ID"}},
             {"evidence_ids", {"declared evidence artifact ID"}},
             {"inference", "one declared inference mode"},
             {"confidence_bp", 0},
             {"assumptions",
              {"indispensable unstated assumption; empty when none"}},
             {"falsifier", "condition that would count against this step"}}}}};
}

[[nodiscard]] std::string numbered_probe(std::size_t number) {
  std::ostringstream output;
  output << "independent_probe_" << std::setw(2) << std::setfill('0') << number;
  return output.str();
}

[[nodiscard]] bool repairable_provider_error(const common::Error &error) {
  return error.code() == common::ErrorCode::json_contract;
}

} // namespace

const std::vector<std::string> &default_perspectives() {
  static const std::vector<std::string> values{
      "direct",          "mechanistic", "counterexample_first",
      "assumption_inversion", "causal",      "mathematical",
      "evidence_synthesis",   "abductive"};
  return values;
}

contracts::Json perspective_contract(std::string perspective) {
  perspective = trim(std::move(perspective));
  if (perspective.empty()) {
    policy_error("reasoning perspective must be non-empty");
  }
  contracts::Json declared = declared_perspective_contract(perspective);
  if (!declared.is_null()) {
    contracts::Json result = {{"contract_id",
                               "oiec-sr-perspective:" + perspective + ":v1"}};
    result.update(declared);
    return result;
  }
  const std::string selector_hash =
      contracts::sha256_json({{"perspective", perspective}});
  const auto selector = static_cast<std::size_t>(
      std::stoul(selector_hash.substr(0, 8), nullptr, 16));
  const std::string mode(independent_probe_modes[selector %
                                                 independent_probe_modes.size()]);
  const std::string focus(independent_probe_foci[selector %
                                                 independent_probe_foci.size()]);
  return {{"contract_id",
           "oiec-sr-perspective:independent-probe:" +
               contracts::sha256_json(perspective).substr(0, 12) + ":v1"},
          {"contract_type", "independent_probe"},
          {"objective",
           "Construct an independent task-grounded audit path rather than paraphrasing another path."},
          {"primary_inference_mode", mode},
          {"fallback_inference_modes", {"constraint", "defeasible"}},
          {"required_path_shape",
           {"Center the path on " + focus + ".",
            "Use only declared premises, hypotheses, and evidence identifiers."}},
          {"falsifier_focus",
           "State a concrete task-grounded defeat condition focused on " + focus + "."}};
}

std::vector<std::string> perspective_names(int count) {
  if (count < 1) {
    policy_error("at least one reasoning perspective is required");
  }
  std::vector<std::string> result;
  result.reserve(static_cast<std::size_t>(count));
  for (const auto &value : default_perspectives()) {
    if (result.size() >= static_cast<std::size_t>(count)) {
      break;
    }
    result.push_back(value);
  }
  while (result.size() < static_cast<std::size_t>(count)) {
    result.push_back(numbered_probe(result.size() + 1));
  }
  return result;
}

contracts::Json provider_problem_context(const ReasoningProblem &problem) {
  return {{"premise_id", "problem"},
          {"statement", problem.statement},
          {"goal", problem.goal},
          {"evidence_ids", problem.evidence_ids},
          {"uncertainty_bp", problem.uncertainty_bp},
          {"difficulty_bp", problem.difficulty_bp},
          {"mutually_exclusive_hypotheses",
           problem.mutually_exclusive_hypotheses}};
}

contracts::Json provider_hypothesis_context(const Hypothesis &hypothesis) {
  return {{"hypothesis_id", hypothesis.hypothesis_id},
          {"proposition", hypothesis.proposition},
          {"posterior_bp", hypothesis.posterior_bp},
          {"supporting_evidence", hypothesis.supporting_evidence},
          {"conflicting_evidence", hypothesis.conflicting_evidence},
          {"assumptions", hypothesis.assumptions},
          {"predictions", hypothesis.predictions},
          {"falsifiers", hypothesis.falsifiers},
          {"status", hypothesis.status}};
}

contracts::Json reasoning_batch_tool(std::string collection_key) {
  return {{"name", reasoning_batch_tool_name},
          {"description",
           "Submit one deterministic OIEC-SR role micro-batch result."},
          {"parameters",
           {{"type", "object"},
            {"properties",
             {{std::move(collection_key),
               {{"type", "array"}, {"items", contracts::Json::object()}}}}},
            {"required", contracts::Json::array()},
            {"additionalProperties", false}}}};
}

contracts::Json reasoning_object_tool(
    std::vector<std::string> property_keys,
    std::vector<std::string> required_keys) {
  property_keys = stable_strings(property_keys);
  required_keys = stable_strings(required_keys);
  if (property_keys.empty()) {
    policy_error("reasoning object tool requires declared properties");
  }
  const std::set<std::string> properties(property_keys.begin(),
                                          property_keys.end());
  if (std::ranges::any_of(required_keys, [&properties](const std::string &key) {
        return !properties.contains(key);
      })) {
    policy_error(
        "reasoning object tool required keys must be declared properties");
  }
  contracts::Json declared = contracts::Json::object();
  for (const auto &key : property_keys) {
    declared[key] = contracts::Json::object();
  }
  return {{"type", "function"},
          {"name", reasoning_object_tool_name},
          {"description",
           "Submit one deterministic OIEC-SR structured reasoning object."},
          {"parameters",
           {{"type", "object"},
            {"properties", std::move(declared)},
            {"required", required_keys},
            {"additionalProperties", false}}}};
}

contracts::Json proposer_request(
    const ReasoningProblem &problem,
    const std::vector<Hypothesis> &hypotheses, std::string perspective,
    const ReasoningBudget &budget) {
  const auto contract = perspective_contract(perspective);
  const auto schema = proposer_schema();
  contracts::Json hypothesis_context = contracts::Json::array();
  for (const auto &hypothesis : hypotheses) {
    hypothesis_context.push_back(provider_hypothesis_context(hypothesis));
  }
  const contracts::Json content = {
      {"problem", provider_problem_context(problem)},
      {"hypotheses", std::move(hypothesis_context)},
      {"perspective", perspective},
      {"perspective_contract", contract},
      {"inference_modes",
       {"abductive", "analogical", "authority", "causal", "computational",
        "constraint", "deductive", "defeasible", "inductive",
        "probabilistic"}},
      {"maximum_steps", budget.max_steps_per_path},
      {"premise_contract",
       {{"problem", "validated facts explicitly stated in the problem"},
        {"hypothesis", "one exact declared hypothesis_id"},
        {"prior_step", "one exact earlier step_id"}}},
      {"schema", schema}};
  return {{"instructions",
           "Act only as an OIEC-SR proposer. Call submit_oiec_reasoning_object exactly once with one object matching the provided schema. Give concise claims, premises, evidence references, assumptions, and falsifiers. Do not reveal private chain-of-thought, use any other tool, approve actions, or claim authority. Use the exact premise ID 'problem' for facts stated in the problem; never emit a problem hash, dotted problem field, source hash, boundary signature, or dimension signature as a premise or as evidence. The assumptions array is only for indispensable propositions not stated by the problem, goal, hypotheses, or declared evidence semantics. Do not restate a supplied task fact or the ordinary meaning of the question as an assumption; use an empty array when no genuinely unstated assumption is required. Follow the supplied perspective contract as an analysis method, not as evidence. Use its primary inference mode when semantically valid; otherwise use one declared fallback mode. Do not force an invalid causal, mathematical, or empirical claim. Make the path materially different through its inference structure, evidence comparison, assumption test, or falsifier; paraphrase alone is not an independent path."},
          {"input_items",
           {{{"role", "user"},
             {"content", contracts::canonical_json(content)}}}},
          {"tools", {reasoning_object_tool(
                        {"conclusion", "hypothesis_ids",
                         "provider_confidence_bp", "estimated_cost_bp",
                         "goal_relevance_bp", "risk_bp", "steps"})}}};
}

contracts::Json proposer_batch_request(
    const ReasoningProblem &problem,
    const std::vector<Hypothesis> &hypotheses,
    const std::vector<std::string> &perspectives,
    const ReasoningBudget &budget) {
  if (perspectives.size() < 2) {
    policy_error("batched proposer request requires at least two perspectives");
  }
  contracts::Json requests = contracts::Json::array();
  for (const auto &perspective : perspectives) {
    requests.push_back({{"perspective", perspective},
                        {"perspective_contract",
                         perspective_contract(perspective)}});
  }
  contracts::Json hypotheses_json = contracts::Json::array();
  for (const auto &hypothesis : hypotheses) {
    hypotheses_json.push_back(provider_hypothesis_context(hypothesis));
  }
  const contracts::Json content = {
      {"problem", provider_problem_context(problem)},
      {"hypotheses", std::move(hypotheses_json)},
      {"inference_modes",
       {"abductive", "analogical", "authority", "causal", "computational",
        "constraint", "deductive", "defeasible", "inductive",
        "probabilistic"}},
      {"maximum_steps", budget.max_steps_per_path},
      {"premise_contract",
       {{"problem", "validated facts explicitly stated in the problem"},
        {"hypothesis", "one exact declared hypothesis_id"},
        {"prior_step", "one exact earlier step_id"}}},
      {"requests", std::move(requests)},
      {"response_schema", {{"candidates", {proposer_schema()}}}}};
  return {{"instructions",
           "Act only as an OIEC-SR proposer micro-batch. Call submit_oiec_reasoning_batch exactly once with a 'candidates' array. Produce one independently structured candidate for every requested perspective in the exact request order. Each array entry is the ordinary proposer object. Do not omit, duplicate, reorder, merge, compare, or cross-reference candidates. Apply the supplied proposer contract to every payload. Use the exact premise ID 'problem' for supplied facts and only declared hypothesis or evidence IDs. Do not reveal private chain-of-thought, use tools, approve actions, or claim authority."},
          {"input_items",
           {{{"role", "user"},
             {"content", contracts::canonical_json(content)}}}},
          {"tools", {reasoning_batch_tool("candidates")}},
          {"max_output_tokens", proposer_batch_max_output_tokens}};
}

ReasoningPath parse_reasoning_path(
    const contracts::Json &payload, const ReasoningProblem &problem_value,
    const std::vector<Hypothesis> &hypotheses, std::string perspective,
    const ReasoningBudget &budget_value) {
  if (!payload.is_object()) {
    provider_error("proposer response must be a JSON object");
  }
  const auto problem = canonicalize_reasoning_problem(problem_value);
  const auto budget = canonicalize_reasoning_budget(budget_value);
  const auto conclusion_value = payload.find("conclusion");
  const std::string conclusion =
      conclusion_value == payload.end()
          ? std::string{}
          : trim(scalar_string(*conclusion_value, "conclusion"));
  const auto steps_value = payload.find("steps");
  if (conclusion.empty() || steps_value == payload.end() ||
      !steps_value->is_array() || steps_value->empty()) {
    provider_error(
        "proposer response requires a conclusion and non-empty steps");
  }
  if (steps_value->size() >
      static_cast<std::size_t>(budget.max_steps_per_path)) {
    provider_error("proposer response exceeds the step budget");
  }

  std::set<std::string> known_hypotheses;
  for (const auto &hypothesis : hypotheses) {
    known_hypotheses.insert(hypothesis.hypothesis_id);
  }
  const auto hypothesis_ids = payload_strings(payload, "hypothesis_ids");
  if (std::ranges::any_of(
          hypothesis_ids, [&known_hypotheses](const std::string &identity) {
            return !known_hypotheses.contains(identity);
          })) {
    provider_error("proposer response references an unknown hypothesis");
  }

  const std::set<std::string> problem_aliases{
      "problem", "problem.statement", problem.problem_id};
  contracts::Json normalized_steps = contracts::Json::array();
  std::vector<ReasoningStep> steps;
  std::set<std::string> step_ids;
  std::size_t index = 0;
  for (const auto &raw : *steps_value) {
    ++index;
    if (!raw.is_object()) {
      provider_error("proposer reasoning steps must be JSON objects");
    }
    std::ostringstream default_identity;
    default_identity << "step-" << std::setw(2) << std::setfill('0') << index;
    const auto step_id_value = raw.find("step_id");
    const std::string step_id =
        trim(step_id_value == raw.end()
                 ? default_identity.str()
                 : scalar_string(*step_id_value, "step_id"));
    if (step_id.empty()) {
      provider_error("proposer reasoning step IDs must be non-empty");
    }
    if (!step_ids.insert(step_id).second) {
      provider_error("proposer reasoning step IDs must be unique");
    }
    const auto claim_value = raw.find("claim");
    const std::string claim =
        claim_value == raw.end()
            ? std::string{}
            : trim(scalar_string(*claim_value, "claim"));
    const auto inference_value = raw.find("inference");
    std::string inference;
    try {
      inference = canonical_inference_mode(
          inference_value == raw.end()
              ? std::string{}
              : scalar_string(*inference_value, "inference"));
    } catch (const common::Error &error) {
      provider_error(error.what());
    }
    auto premises = payload_strings(raw, "premises");
    for (auto &premise : premises) {
      if (problem_aliases.contains(trim(premise))) {
        premise = "problem";
      }
    }
    premises = stable_strings(premises);
    const auto evidence_ids = payload_strings(raw, "evidence_ids");
    const auto assumptions = payload_strings(raw, "assumptions");
    const auto falsifier_value = raw.find("falsifier");
    const std::string falsifier =
        falsifier_value == raw.end()
            ? std::string{}
            : trim(scalar_string(*falsifier_value, "falsifier"));
    const int confidence =
        payload_score(raw, "confidence_bp", "provider step confidence");
    const contracts::Json step_payload = {
        {"step_id", step_id},       {"claim", claim},
        {"premises", premises},     {"evidence_ids", evidence_ids},
        {"inference", inference},   {"confidence_bp", confidence},
        {"assumptions", assumptions}, {"falsifier", falsifier}};
    normalized_steps.push_back(step_payload);

    ReasoningStep step;
    step.step_id = step_id;
    step.claim = claim;
    step.premises = std::move(premises);
    step.evidence_ids = evidence_ids;
    step.inference = inference;
    step.confidence_bp = confidence;
    step.assumptions = assumptions;
    step.falsifier = falsifier;
    step.signature = contracts::sha256_json(step_payload);
    steps.push_back(canonicalize_reasoning_step(std::move(step)));
  }

  const int provider_confidence = payload_score(
      payload, "provider_confidence_bp", "provider path confidence");
  const int estimated_cost =
      payload_score(payload, "estimated_cost_bp", "provider estimated cost");
  const int goal_relevance =
      payload_score(payload, "goal_relevance_bp", "provider goal relevance");
  const int risk = payload_score(payload, "risk_bp", "provider path risk");
  const contracts::Json raw_material = {
      {"problem_id", problem.problem_id},
      {"perspective", perspective},
      {"hypothesis_ids", hypothesis_ids},
      {"steps", normalized_steps},
      {"conclusion", conclusion},
      {"provider_confidence_bp", provider_confidence},
      {"estimated_cost_bp", estimated_cost},
      {"goal_relevance_bp", goal_relevance},
      {"risk_bp", risk}};

  ReasoningPath path;
  path.path_id = "path-pending";
  path.perspective = std::move(perspective);
  path.hypothesis_ids = hypothesis_ids;
  path.steps = std::move(steps);
  path.conclusion = conclusion;
  path.provider_confidence_bp = provider_confidence;
  path.estimated_cost_bp = estimated_cost;
  path.goal_relevance_bp = goal_relevance;
  path.risk_bp = risk;
  path.structure_signature = path_structure_signature(path);
  path.path_id = "path:" + path.structure_signature;

  contracts::Json signed_steps = contracts::Json::array();
  for (const auto &step : path.steps) {
    signed_steps.push_back(to_json(step));
  }
  contracts::Json path_payload = raw_material;
  path_payload["path_id"] = path.path_id;
  path_payload["steps"] = std::move(signed_steps);
  path.signature = contracts::sha256_json(path_payload);
  return canonicalize_reasoning_path(std::move(path));
}

std::vector<ReasoningPath> generate_reasoning_paths(
    providers::ReasoningProvider &provider, const ReasoningProblem &problem,
    const std::vector<Hypothesis> &hypotheses, const ReasoningBudget &budget,
    bool diversity_filter_enabled, int role_batch_size) {
  const auto perspectives = perspective_names(budget.max_generation_attempts);
  const auto initial_count = std::min<std::size_t>(
      perspectives.size(), static_cast<std::size_t>(budget.candidate_count));
  std::vector<std::string> initial(
      perspectives.begin(),
      perspectives.begin() + static_cast<std::ptrdiff_t>(initial_count));
  const int batch_size =
      providers::reasoning_role_batch_size(provider, role_batch_size);
  std::vector<std::pair<std::string, contracts::Json>> generated;
  for (std::size_t start = 0; start < initial.size();
       start += static_cast<std::size_t>(batch_size)) {
    const std::size_t end =
        std::min(initial.size(), start + static_cast<std::size_t>(batch_size));
    std::vector<std::string> group(initial.begin() +
                                       static_cast<std::ptrdiff_t>(start),
                                   initial.begin() +
                                       static_cast<std::ptrdiff_t>(end));
    try {
      std::vector<contracts::Json> payloads;
      if (group.size() == 1) {
        const auto responses = providers::create_provider_responses(
            provider,
            {proposer_request(problem, hypotheses, group.front(), budget)},
            static_cast<std::size_t>(budget.max_provider_calls));
        payloads.push_back(providers::parse_reasoning_json_object(
            providers::response_text(responses.front())));
      } else {
        const auto responses = providers::create_provider_responses(
            provider, {proposer_batch_request(problem, hypotheses, group, budget)},
            static_cast<std::size_t>(budget.max_provider_calls));
        payloads = providers::parse_ordered_role_batch_payloads(
            responses.front(), "candidates", group.size());
      }
      for (std::size_t item = 0; item < group.size(); ++item) {
        generated.emplace_back(group[item], payloads[item]);
      }
    } catch (const common::Error &error) {
      if (!repairable_provider_error(error)) {
        throw;
      }
      provider.record_reasoning_repair("proposer", error.what(), group);
    }
  }

  std::vector<ReasoningPath> paths;
  for (const auto &[perspective, payload] : generated) {
    try {
      auto candidate =
          parse_reasoning_path(payload, problem, hypotheses, perspective, budget);
      if (!diversity_filter_enabled ||
          !is_structural_duplicate(candidate, paths,
                                   default_diversity_configuration())) {
        paths.push_back(std::move(candidate));
      }
    } catch (const common::Error &error) {
      if (!repairable_provider_error(error)) {
        throw;
      }
      provider.record_reasoning_repair("proposer", error.what(), {perspective});
    }
  }

  for (std::size_t index = static_cast<std::size_t>(budget.candidate_count);
       index < perspectives.size() &&
       paths.size() < static_cast<std::size_t>(budget.candidate_count);
       ++index) {
    const std::string &perspective = perspectives[index];
    try {
      const auto responses = providers::create_provider_responses(
          provider, {proposer_request(problem, hypotheses, perspective, budget)},
          static_cast<std::size_t>(budget.max_generation_attempts));
      auto candidate = parse_reasoning_path(
          providers::parse_reasoning_json_object(
              providers::response_text(responses.front())),
          problem, hypotheses, perspective, budget);
      if (!diversity_filter_enabled ||
          !is_structural_duplicate(candidate, paths,
                                   default_diversity_configuration())) {
        paths.push_back(std::move(candidate));
      }
    } catch (const common::Error &error) {
      if (!repairable_provider_error(error)) {
        throw;
      }
      provider.record_reasoning_repair("proposer", error.what(), {perspective});
    }
  }
  if (paths.size() < static_cast<std::size_t>(budget.candidate_count)) {
    provider_error(
        "proposer responses did not produce the required materially distinct paths");
  }
  return bind_diversity_scores(paths, default_diversity_configuration());
}

} // namespace statewright::reasoning
