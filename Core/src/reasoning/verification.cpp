#include "statewright/reasoning/verification.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace statewright::reasoning {
namespace {

constexpr std::array<std::string_view, 9> process_checks{
    "premises_available",      "evidence_declared",
    "evidence_relevant",       "inference_supported",
    "grounding_traceable",     "assumptions_explicit",
    "counterexample_addressed", "alternative_considered",
    "conclusion_not_overstated"};

constexpr std::array<std::string_view, 4> evidence_inference_modes{
    "inductive", "causal", "probabilistic", "authority"};

[[noreturn]] void provider_error(std::string message) {
  throw common::Error(common::ErrorCode::json_contract, std::move(message));
}

template <std::size_t Size>
[[nodiscard]] bool contains(
    const std::array<std::string_view, Size> &values,
    std::string_view candidate) noexcept {
  return std::ranges::find(values, candidate) != values.end();
}

[[nodiscard]] std::vector<std::string>
payload_strings(const contracts::Json &payload, std::string_view key) {
  const auto found = payload.find(std::string(key));
  if (found == payload.end()) {
    return {};
  }
  if (!found->is_array()) {
    provider_error("reasoning response " + std::string(key) +
                   " must be an array");
  }
  std::vector<std::string> values;
  for (const auto &item : *found) {
    if (!item.is_string()) {
      provider_error("reasoning response arrays must contain strings");
    }
    const std::string value = item.get<std::string>();
    if (!value.empty()) {
      values.push_back(value);
    }
  }
  return values;
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

[[nodiscard]] bool has_declared_evidence(
    const ReasoningStep &step, const std::set<std::string> &declared) {
  return std::ranges::any_of(step.evidence_ids, [&](const std::string &identity) {
    return declared.contains(identity);
  });
}

[[nodiscard]] contracts::Json verifier_material(const VerifierReport &report) {
  return {{"path_id", report.path_id},
          {"step_scores", report.step_scores},
          {"failures", report.failures},
          {"contradictions", report.contradictions},
          {"unsupported_nodes", report.unsupported_nodes},
          {"missing_assumptions", report.missing_assumptions},
          {"premise_validity_bp", report.premise_validity_bp},
          {"evidence_support_bp", report.evidence_support_bp},
          {"inference_quality_bp", report.inference_quality_bp},
          {"consistency_bp", report.consistency_bp},
          {"completeness_bp", report.completeness_bp},
          {"weakest_step_bp", report.weakest_step_bp},
          {"score_bp", report.score_bp},
          {"verdict", report.verdict}};
}

[[nodiscard]] contracts::Json falsifier_material(const FalsifierReport &report) {
  return {{"path_id", report.path_id},
          {"searched_falsifiers", report.searched_falsifiers},
          {"counterexamples", report.counterexamples},
          {"contradicted_step_ids", report.contradicted_step_ids},
          {"unresolved_defeat_conditions",
           report.unresolved_defeat_conditions},
          {"unresolved_defeat_evidence_ids",
           report.unresolved_defeat_evidence_ids},
          {"alternative_explanations", report.alternative_explanations},
          {"boundary_cases", report.boundary_cases},
          {"reversed_causal_directions", report.reversed_causal_directions},
          {"invalid_invariants", report.invalid_invariants},
          {"evidence_reversal_conditions",
           report.evidence_reversal_conditions},
          {"severity_bp", report.severity_bp},
          {"survival_bp", report.survival_bp},
          {"residual_uncertainty_bp", report.residual_uncertainty_bp},
          {"verdict", report.verdict}};
}

} // namespace

const std::vector<std::string> &required_process_checks() {
  static const std::vector<std::string> checks(process_checks.begin(),
                                                process_checks.end());
  return checks;
}

ProcessChecks normalize_process_checks(const contracts::Json &value) {
  if (!value.is_object()) {
    provider_error("verifier response checks must be an object");
  }
  ProcessChecks normalized;
  for (const auto &[raw_name, raw_value] : value.items()) {
    std::string name = raw_name;
    if (name == "alternative_consideered") {
      name = "alternative_considered";
    }
    if (normalized.contains(name)) {
      provider_error("verifier response contains ambiguous process checks");
    }
    if (!raw_value.is_boolean()) {
      provider_error("verifier process checks must be booleans");
    }
    normalized[name] = raw_value.get<bool>();
  }
  if (normalized.size() != process_checks.size() ||
      std::ranges::any_of(process_checks, [&](std::string_view name) {
        return !normalized.contains(std::string(name));
      })) {
    provider_error("verifier response must provide every process check");
  }
  return normalized;
}

VerifierReport verify_reasoning_path(
    const ReasoningPath &path_value,
    const std::vector<Hypothesis> &hypotheses,
    const std::vector<std::string> &declared_evidence_ids,
    const contracts::Json &payload) {
  const auto path = canonicalize_reasoning_path(path_value);
  const auto raw_steps = payload.find("steps");
  if (raw_steps == payload.end() || !raw_steps->is_array()) {
    provider_error("verifier response steps must be an array");
  }
  std::map<std::string, contracts::Json> reported;
  for (const auto &item : *raw_steps) {
    if (!item.is_object()) {
      provider_error("verifier response steps must contain objects");
    }
    const std::string step_id = item.value("step_id", "");
    if (!reported.emplace(step_id, item).second) {
      provider_error(
          "verifier response must cover every candidate step exactly once");
    }
  }
  std::set<std::string> expected_steps;
  for (const auto &step : path.steps) {
    expected_steps.insert(step.step_id);
  }
  std::set<std::string> reported_steps;
  for (const auto &[step_id, item] : reported) {
    static_cast<void>(item);
    reported_steps.insert(step_id);
  }
  if (reported_steps != expected_steps) {
    provider_error(
        "verifier response must cover every candidate step exactly once");
  }

  const std::set<std::string> declared(declared_evidence_ids.begin(),
                                       declared_evidence_ids.end());
  std::set<std::string> known_hypotheses;
  std::map<std::string, std::set<std::string>> hypothesis_grounding;
  for (const auto &hypothesis : hypotheses) {
    if (!known_hypotheses.insert(hypothesis.hypothesis_id).second) {
      provider_error("verifier hypothesis identities must be unique");
    }
    std::set<std::string> roots;
    if (std::ranges::any_of(
            hypothesis.supporting_evidence,
            [&](const std::string &identity) { return declared.contains(identity); })) {
      roots.insert("evidence");
    }
    if (!hypothesis.assumptions.empty()) {
      roots.insert("assumption");
    }
    hypothesis_grounding[hypothesis.hypothesis_id] = std::move(roots);
  }

  std::set<std::string> prior_steps;
  std::map<std::string, std::set<std::string>> prior_grounding;
  ScorePairs scores;
  std::vector<ProcessChecks> checks_by_step;
  std::vector<std::string> failures;
  std::vector<std::string> unsupported_nodes;
  std::vector<std::string> missing_assumptions =
      payload_strings(payload, "missing_assumptions");
  bool critical = false;
  for (const auto &step : path.steps) {
    const auto &item = reported.at(step.step_id);
    const auto checks = item.find("checks");
    if (checks == item.end()) {
      provider_error("verifier response checks must be an object");
    }
    auto normalized = normalize_process_checks(*checks);
    std::set<std::string> allowed_premises{"problem"};
    allowed_premises.insert(known_hypotheses.begin(), known_hypotheses.end());
    allowed_premises.insert(prior_steps.begin(), prior_steps.end());
    if (std::ranges::any_of(step.premises, [&](const std::string &premise) {
          return !allowed_premises.contains(premise);
        })) {
      normalized["premises_available"] = false;
    }
    if (std::ranges::any_of(step.evidence_ids, [&](const std::string &identity) {
          return !declared.contains(identity);
        })) {
      normalized["evidence_declared"] = false;
    }
    if (contains(evidence_inference_modes, step.inference) &&
        !has_declared_evidence(step, declared)) {
      normalized["evidence_relevant"] = false;
    }
    std::set<std::string> grounding;
    for (const auto &premise : step.premises) {
      if (premise == "problem") {
        grounding.insert("validated_premise");
      } else if (const auto found = hypothesis_grounding.find(premise);
                 found != hypothesis_grounding.end()) {
        grounding.insert(found->second.begin(), found->second.end());
      } else if (const auto prior = prior_grounding.find(premise);
                 prior != prior_grounding.end()) {
        grounding.insert(prior->second.begin(), prior->second.end());
      }
    }
    if (has_declared_evidence(step, declared)) {
      grounding.insert("evidence");
    }
    if (!step.assumptions.empty()) {
      grounding.insert("assumption");
    }
    if (grounding.empty()) {
      normalized["grounding_traceable"] = false;
    }
    const int passed = static_cast<int>(std::ranges::count_if(
        normalized, [](const auto &entry) { return entry.second; }));
    scores.emplace_back(step.step_id,
                        passed * score_scale /
                            static_cast<int>(process_checks.size()));
    checks_by_step.push_back(normalized);
    for (const auto &failure : payload_strings(item, "failures")) {
      failures.push_back(step.step_id + ": " + failure);
    }
    if (!normalized.at("premises_available")) {
      failures.push_back(step.step_id + ": unavailable premise");
      critical = true;
    }
    if (!normalized.at("evidence_declared")) {
      failures.push_back(step.step_id + ": undeclared evidence");
      critical = true;
    }
    if (!normalized.at("inference_supported")) {
      failures.push_back(step.step_id + ": unsupported inference");
      critical = true;
    }
    if (!normalized.at("grounding_traceable")) {
      failures.push_back(step.step_id + ": no grounding trace");
      critical = true;
    }
    if (!normalized.at("evidence_relevant")) {
      failures.push_back(step.step_id + ": missing factual evidence");
      unsupported_nodes.push_back(step.step_id);
      critical = true;
    }
    if (!normalized.at("assumptions_explicit")) {
      missing_assumptions.push_back(step.step_id + ": implicit assumption");
    }
    if (!normalized.at("conclusion_not_overstated")) {
      failures.push_back(step.step_id + ": conclusion stronger than premises");
      unsupported_nodes.push_back(step.step_id);
      critical = true;
    }
    if (!normalized.at("premises_available") ||
        !normalized.at("evidence_declared") ||
        !normalized.at("inference_supported") ||
        !normalized.at("grounding_traceable")) {
      unsupported_nodes.push_back(step.step_id);
    }
    prior_steps.insert(step.step_id);
    prior_grounding[step.step_id] = std::move(grounding);
  }

  VerifierReport report;
  report.path_id = path.path_id;
  report.step_scores = scores;
  report.failures = failures;
  report.contradictions = payload_strings(payload, "contradictions");
  report.unsupported_nodes = unsupported_nodes;
  report.missing_assumptions = missing_assumptions;
  report.score_bp = std::ranges::min(scores, {}, &ScorePairs::value_type::second)
                        .second;
  int premise_total = 0;
  int evidence_total = 0;
  int inference_total = 0;
  int completeness_total = 0;
  for (const auto &checks : checks_by_step) {
    premise_total += checks.at("premises_available") ? score_scale : 0;
    evidence_total +=
        ((checks.at("evidence_declared") ? score_scale : 0) +
         (checks.at("evidence_relevant") ? score_scale : 0)) /
        2;
    inference_total +=
        ((checks.at("inference_supported") ? score_scale : 0) +
         (checks.at("conclusion_not_overstated") ? score_scale : 0)) /
        2;
    completeness_total +=
        ((checks.at("grounding_traceable") ? score_scale : 0) +
         (checks.at("assumptions_explicit") ? score_scale : 0) +
         (checks.at("counterexample_addressed") ? score_scale : 0)) /
        3;
  }
  const int count = static_cast<int>(checks_by_step.size());
  report.premise_validity_bp = premise_total / count;
  report.evidence_support_bp = evidence_total / count;
  report.inference_quality_bp = inference_total / count;
  report.consistency_bp = std::max(
      0, score_scale - static_cast<int>(report.contradictions.size()) * 2'000);
  report.completeness_bp = completeness_total / count;
  if (critical) {
    report.score_bp = 0;
    report.verdict = "REJECT";
  } else if (report.score_bp >= 7'500 && report.contradictions.empty()) {
    report.verdict = "ACCEPT";
  } else if (report.score_bp >= 5'000) {
    report.verdict = "REVISE";
  } else {
    report.verdict = "REJECT";
  }
  report.weakest_step_bp = report.score_bp;
  const auto material = verifier_material(report);
  report.report_id = "verifier:" + contracts::sha256_json(material);
  auto signature_material = material;
  signature_material["report_id"] = report.report_id;
  report.signature = contracts::sha256_json(signature_material);
  return canonicalize_verifier_report(std::move(report));
}

FalsifierReport falsify_reasoning_path(
    const ReasoningPath &path_value, const contracts::Json &payload,
    const std::optional<std::vector<std::string>> &declared_evidence_ids) {
  const auto path = canonicalize_reasoning_path(path_value);
  FalsifierReport report;
  report.path_id = path.path_id;
  report.searched_falsifiers = payload_strings(payload, "searched_falsifiers");
  report.counterexamples = payload_strings(payload, "counterexamples");
  report.contradicted_step_ids =
      payload_strings(payload, "contradicted_step_ids");
  report.unresolved_defeat_conditions =
      payload_strings(payload, "unresolved_defeat_conditions");
  report.unresolved_defeat_evidence_ids =
      payload_strings(payload, "unresolved_defeat_evidence_ids");
  report.alternative_explanations =
      payload_strings(payload, "alternative_explanations");
  report.boundary_cases = payload_strings(payload, "boundary_cases");
  report.reversed_causal_directions =
      payload_strings(payload, "reversed_causal_directions");
  report.invalid_invariants = payload_strings(payload, "invalid_invariants");
  report.evidence_reversal_conditions =
      payload_strings(payload, "evidence_reversal_conditions");

  if (declared_evidence_ids) {
    const std::set<std::string> declared(declared_evidence_ids->begin(),
                                         declared_evidence_ids->end());
    if (std::ranges::any_of(
            report.unresolved_defeat_evidence_ids,
            [&](const std::string &identity) { return !declared.contains(identity); })) {
      provider_error("falsifier response grounds defeat in undeclared evidence");
    }
    if (!report.unresolved_defeat_evidence_ids.empty() &&
        report.unresolved_defeat_conditions.empty()) {
      provider_error("falsifier defeat evidence has no unresolved condition");
    }
    if (!report.unresolved_defeat_conditions.empty() &&
        report.unresolved_defeat_evidence_ids.empty()) {
      report.evidence_reversal_conditions.insert(
          report.evidence_reversal_conditions.end(),
          report.unresolved_defeat_conditions.begin(),
          report.unresolved_defeat_conditions.end());
      report.evidence_reversal_conditions =
          stable_strings(report.evidence_reversal_conditions);
      report.unresolved_defeat_conditions.clear();
    }
  }
  std::set<std::string> step_ids;
  for (const auto &step : path.steps) {
    step_ids.insert(step.step_id);
  }
  if (std::ranges::any_of(report.contradicted_step_ids,
                          [&](const std::string &identity) {
                            return !step_ids.contains(identity);
                          })) {
    provider_error("falsifier response references an unknown candidate step");
  }
  const auto survival = payload.find("survival_bp");
  const int proposed_survival =
      survival == payload.end() ? 0 : survival->get<int>();
  if (proposed_survival < 0 || proposed_survival > score_scale) {
    provider_error("falsifier proposed survival must be 0..10000");
  }
  const auto critical_value = payload.find("critical");
  if (critical_value != payload.end() && !critical_value->is_boolean()) {
    provider_error("falsifier critical flag must be boolean");
  }
  const bool critical = critical_value != payload.end() &&
                        critical_value->get<bool>();
  if (critical || !report.contradicted_step_ids.empty()) {
    report.survival_bp = 0;
    report.severity_bp = score_scale;
    report.verdict = "REJECT";
  } else if (!report.counterexamples.empty()) {
    report.survival_bp = std::min(4'000, proposed_survival);
    report.severity_bp = std::max(6'000, score_scale - report.survival_bp);
    report.verdict = "REVISE";
  } else if (!report.alternative_explanations.empty() ||
             !report.boundary_cases.empty() ||
             !report.reversed_causal_directions.empty() ||
             !report.invalid_invariants.empty()) {
    report.survival_bp = std::min(6'000, proposed_survival);
    report.severity_bp = std::max(4'000, score_scale - report.survival_bp);
    report.verdict = "REVISE";
  } else {
    report.survival_bp = std::max(5'000, proposed_survival);
    report.severity_bp = std::max(0, score_scale - report.survival_bp);
    report.verdict = "SURVIVES";
  }
  report.residual_uncertainty_bp =
      std::max(0, score_scale - report.survival_bp);
  const auto material = falsifier_material(report);
  report.report_id = "falsifier:" + contracts::sha256_json(material);
  auto signature_material = material;
  signature_material["report_id"] = report.report_id;
  report.signature = contracts::sha256_json(signature_material);
  return canonicalize_falsifier_report(std::move(report));
}

} // namespace statewright::reasoning
