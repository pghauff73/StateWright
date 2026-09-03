#include "statewright/reasoning/search.hpp"

#include "statewright/reasoning/generator.hpp"
#include "statewright/reasoning/synthesis.hpp"
#include "statewright/reasoning/verification.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace statewright::reasoning {

CandidateSet search_reasoning_candidates(
    providers::ReasoningProvider &provider, const ReasoningProblem &problem,
    const std::vector<Hypothesis> &hypotheses,
    const std::vector<std::string> &declared_evidence_ids,
    const ReasoningBudget &budget,
    const AblationConfiguration &ablation_value) {
  const auto ablation = make_ablation_configuration(ablation_value);
  providers::BoundedReasoningProvider bounded_provider(
      provider, budget.max_provider_calls, budget.max_tokens,
      budget.max_tool_calls);
  const auto paths = generate_reasoning_paths(
      bounded_provider, problem, hypotheses, budget,
      ablation.diversity_filter_enabled, ablation.proposer_batch_size);

  std::vector<VerifierReport> verifier_reports;
  if (ablation.verifier_enabled) {
    verifier_reports = verify_reasoning_paths(
        bounded_provider, problem, paths, hypotheses, declared_evidence_ids,
        budget, ablation.verifier_batch_size);
  } else {
    for (const auto &path : paths) {
      verifier_reports.push_back(
          make_bypass_verifier(path, declared_evidence_ids));
    }
  }

  std::map<std::string, VerifierReport> verifier_by_path;
  for (const auto &report : verifier_reports) {
    verifier_by_path.emplace(report.path_id, report);
  }
  auto verifier_ranked = paths;
  std::ranges::sort(verifier_ranked,
                    [&verifier_by_path](const ReasoningPath &left,
                                        const ReasoningPath &right) {
                      const auto &left_report =
                          verifier_by_path.at(left.path_id);
                      const auto &right_report =
                          verifier_by_path.at(right.path_id);
                      const bool left_rejected = left_report.verdict == "REJECT";
                      const bool right_rejected =
                          right_report.verdict == "REJECT";
                      if (left_rejected != right_rejected) {
                        return !left_rejected;
                      }
                      if (left_report.score_bp != right_report.score_bp) {
                        return left_report.score_bp > right_report.score_bp;
                      }
                      return left.path_id < right.path_id;
                    });

  std::vector<ReasoningPath> falsifier_targets;
  for (const auto &path : verifier_ranked) {
    if (verifier_by_path.at(path.path_id).verdict == "REJECT") {
      continue;
    }
    if (falsifier_targets.size() >=
        static_cast<std::size_t>(budget.falsifier_count)) {
      break;
    }
    falsifier_targets.push_back(path);
  }

  std::vector<FalsifierReport> actual_falsifier_reports;
  if (ablation.falsifier_enabled && !falsifier_targets.empty()) {
    actual_falsifier_reports = falsify_reasoning_paths(
        bounded_provider, problem, falsifier_targets, budget,
        ablation.falsifier_batch_size);
  } else {
    for (const auto &path : falsifier_targets) {
      actual_falsifier_reports.push_back(make_bypass_falsifier(path));
    }
  }

  auto candidates = assemble_reasoning_candidates(
      problem, paths, verifier_reports, actual_falsifier_reports,
      declared_evidence_ids, ablation);
  if (candidates.selected_path_id.empty()) {
    return candidates;
  }

  std::map<std::string, ReasoningPath> path_by_id;
  for (const auto &path : paths) {
    path_by_id.emplace(path.path_id, path);
  }
  std::vector<ReasoningPath> survivors;
  for (const auto &path_id : candidates.surviving_path_ids) {
    survivors.push_back(path_by_id.at(path_id));
  }
  const auto synthesis = synthesize_verified_result(
      bounded_provider, problem, hypotheses, survivors.front(), survivors,
      verifier_reports, declared_evidence_ids, budget,
      ablation.synthesis_verification_enabled && ablation.verifier_enabled,
      ablation.verifier_batch_size);
  return assemble_reasoning_candidates(
      problem, paths, verifier_reports, actual_falsifier_reports,
      declared_evidence_ids, ablation, synthesis);
}

} // namespace statewright::reasoning
