#include "statewright/reasoning/search.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/reasoning/certification.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace statewright::reasoning {
namespace {

[[noreturn]] void policy_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied, std::move(message));
}

[[nodiscard]] contracts::Json empty_array() {
  return contracts::Json::array();
}

template <typename Value, typename Member>
[[nodiscard]] std::map<std::string, Value>
index_unique(const std::vector<Value> &values, Member member,
             std::string_view label) {
  std::map<std::string, Value> result;
  for (const auto &value : values) {
    const std::string &identity = value.*member;
    if (!result.emplace(identity, value).second) {
      policy_error(std::string(label) + " identities must be unique");
    }
  }
  return result;
}

[[nodiscard]] VerifierReport signed_verifier_report(
    VerifierReport report, const contracts::Json &identity_material) {
  report.report_id = "verifier:" + contracts::sha256_json(identity_material);
  auto signature_material = to_json(report);
  signature_material.erase("signature");
  report.signature = contracts::sha256_json(signature_material);
  return canonicalize_verifier_report(std::move(report));
}

[[nodiscard]] FalsifierReport signed_falsifier_report(
    FalsifierReport report, const contracts::Json &identity_material,
    contracts::Json signature_material) {
  report.report_id = "falsifier:" + contracts::sha256_json(identity_material);
  signature_material["report_id"] = report.report_id;
  report.signature = contracts::sha256_json(signature_material);
  return canonicalize_falsifier_report(std::move(report));
}

} // namespace

FalsifierReport make_rejected_falsifier(const ReasoningPath &path_value) {
  const auto path = canonicalize_reasoning_path(path_value);
  const auto material = contracts::Json{
      {"path_id", path.path_id},
      {"searched_falsifiers", empty_array()},
      {"counterexamples", empty_array()},
      {"contradicted_step_ids", empty_array()},
      {"unresolved_defeat_conditions",
       {"candidate did not reach falsifier stage"}},
      {"survival_bp", 0},
      {"verdict", "REJECT"}};
  FalsifierReport report;
  report.path_id = path.path_id;
  report.unresolved_defeat_conditions = {
      "candidate did not reach falsifier stage"};
  report.survival_bp = 0;
  report.verdict = "REJECT";
  return signed_falsifier_report(std::move(report), material, material);
}

VerifierReport make_bypass_verifier(
    const ReasoningPath &path_value,
    const std::vector<std::string> &declared_evidence_ids) {
  const auto path = canonicalize_reasoning_path(path_value);
  const std::set<std::string> declared(declared_evidence_ids.begin(),
                                       declared_evidence_ids.end());
  std::set<std::string> referenced;
  for (const auto &step : path.steps) {
    referenced.insert(step.evidence_ids.begin(), step.evidence_ids.end());
  }
  std::vector<std::string> unknown;
  std::ranges::set_difference(referenced, declared,
                              std::back_inserter(unknown));
  if (!unknown.empty()) {
    policy_error(
        "qualification verifier ablation cannot admit unknown evidence");
  }

  ScorePairs step_scores;
  contracts::Json step_scores_json = contracts::Json::array();
  for (const auto &step : path.steps) {
    step_scores.emplace_back(step.step_id, score_scale);
    step_scores_json.push_back({step.step_id, score_scale});
  }
  const auto material = contracts::Json{
      {"path_id", path.path_id},
      {"step_scores", step_scores_json},
      {"failures", empty_array()},
      {"contradictions", empty_array()},
      {"unsupported_nodes", empty_array()},
      {"missing_assumptions", empty_array()},
      {"premise_validity_bp", score_scale},
      {"evidence_support_bp", referenced.empty() ? 0 : score_scale},
      {"inference_quality_bp", score_scale},
      {"consistency_bp", score_scale},
      {"completeness_bp", score_scale},
      {"weakest_step_bp", score_scale},
      {"score_bp", score_scale},
      {"verdict", "ACCEPT"},
      {"ablation", "without_verifier"}};
  VerifierReport report;
  report.path_id = path.path_id;
  report.step_scores = std::move(step_scores);
  report.premise_validity_bp = score_scale;
  report.evidence_support_bp = referenced.empty() ? 0 : score_scale;
  report.inference_quality_bp = score_scale;
  report.consistency_bp = score_scale;
  report.completeness_bp = score_scale;
  report.weakest_step_bp = score_scale;
  report.score_bp = score_scale;
  report.verdict = "ACCEPT";
  return signed_verifier_report(std::move(report), material);
}

FalsifierReport make_bypass_falsifier(const ReasoningPath &path_value) {
  const auto path = canonicalize_reasoning_path(path_value);
  const auto material = contracts::Json{
      {"path_id", path.path_id},
      {"searched_falsifiers", empty_array()},
      {"counterexamples", empty_array()},
      {"contradicted_step_ids", empty_array()},
      {"unresolved_defeat_conditions", empty_array()},
      {"alternative_explanations", empty_array()},
      {"boundary_cases", empty_array()},
      {"reversed_causal_directions", empty_array()},
      {"invalid_invariants", empty_array()},
      {"evidence_reversal_conditions", empty_array()},
      {"severity_bp", 0},
      {"survival_bp", score_scale},
      {"verdict", "SURVIVES"},
      {"ablation", "without_falsifier"}};
  auto report_material = material;
  report_material.erase("ablation");
  FalsifierReport report;
  report.path_id = path.path_id;
  report.severity_bp = 0;
  report.survival_bp = score_scale;
  report.verdict = "SURVIVES";
  return signed_falsifier_report(std::move(report), material,
                                 std::move(report_material));
}

CandidateSet assemble_reasoning_candidates(
    const ReasoningProblem &problem_value,
    const std::vector<ReasoningPath> &path_values,
    const std::vector<VerifierReport> &verifier_values,
    const std::vector<FalsifierReport> &actual_falsifier_values,
    const std::vector<std::string> &declared_evidence_ids,
    const AblationConfiguration &ablation_value,
    const std::optional<SynthesisResult> &synthesis_value) {
  const auto problem = canonicalize_reasoning_problem(problem_value);
  const auto ablation = make_ablation_configuration(ablation_value);
  std::vector<ReasoningPath> paths;
  paths.reserve(path_values.size());
  for (const auto &path : path_values) {
    paths.push_back(canonicalize_reasoning_path(path));
  }
  std::vector<VerifierReport> verifiers;
  verifiers.reserve(verifier_values.size());
  for (const auto &report : verifier_values) {
    verifiers.push_back(canonicalize_verifier_report(report));
  }
  std::vector<FalsifierReport> actual_falsifiers;
  actual_falsifiers.reserve(actual_falsifier_values.size());
  for (const auto &report : actual_falsifier_values) {
    actual_falsifiers.push_back(canonicalize_falsifier_report(report));
  }

  const auto verifier_by_path =
      index_unique(verifiers, &VerifierReport::path_id, "verifier report");
  for (const auto &path : paths) {
    if (!verifier_by_path.contains(path.path_id)) {
      policy_error("candidate path has no verifier report");
    }
  }
  auto falsifier_by_path = index_unique(
      actual_falsifiers, &FalsifierReport::path_id, "falsifier report");
  for (const auto &[path_id, report] : falsifier_by_path) {
    static_cast<void>(report);
    if (std::ranges::none_of(paths, [&path_id](const ReasoningPath &path) {
          return path.path_id == path_id;
        })) {
      policy_error("falsifier report references an unknown candidate path");
    }
  }
  for (const auto &path : paths) {
    falsifier_by_path.try_emplace(path.path_id,
                                  make_rejected_falsifier(path));
  }

  std::vector<FalsifierReport> falsifiers;
  std::vector<ReasoningMetrics> metrics;
  falsifiers.reserve(paths.size());
  metrics.reserve(paths.size());
  for (const auto &path : paths) {
    const auto &falsifier = falsifier_by_path.at(path.path_id);
    falsifiers.push_back(falsifier);
    metrics.push_back(score_reasoning_path(
        path, verifier_by_path.at(path.path_id), falsifier,
        declared_evidence_ids, default_score_configuration()));
  }

  const auto ranked =
      rank_reasoning_paths(paths, metrics, verifiers, falsifiers);
  std::vector<ReasoningPath> survivors;
  for (const auto &path : ranked) {
    if (verifier_by_path.at(path.path_id).verdict != "REJECT" &&
        falsifier_by_path.at(path.path_id).verdict == "SURVIVES") {
      survivors.push_back(path);
    }
  }
  const std::string selected_path_id =
      survivors.empty() ? std::string{} : survivors.front().path_id;

  CandidateSet candidates;
  candidates.problem_id = problem.problem_id;
  candidates.paths = paths;
  candidates.verifier_reports = verifiers;
  candidates.falsifier_reports = std::move(falsifiers);
  candidates.metrics = std::move(metrics);
  candidates.selected_path_id = selected_path_id;
  for (const auto &path : survivors) {
    candidates.surviving_path_ids.push_back(path.path_id);
  }
  for (const auto &path : paths) {
    if (path.path_id != selected_path_id) {
      candidates.rejected_path_ids.push_back(path.path_id);
    }
  }
  if (synthesis_value.has_value()) {
    candidates.synthesis = make_synthesis_result(*synthesis_value);
  }
  candidates.score_config_id = default_score_configuration().config_id;
  candidates.score_config_hash = default_score_configuration().signature;
  candidates.diversity_config_hash = contracts::sha256_json(
      {{"configuration", default_diversity_configuration().signature},
       {"filter_enabled", ablation.diversity_filter_enabled}});
  candidates.ablation_id = ablation.ablation_id;
  candidates.ablation_config_hash = ablation.signature;

  const auto contradictions = build_contradiction_records(candidates);
  for (const auto &contradiction : contradictions) {
    candidates.contradiction_ids.push_back(contradiction.contradiction_id);
  }
  return sign_candidate_set(std::move(candidates));
}

} // namespace statewright::reasoning
