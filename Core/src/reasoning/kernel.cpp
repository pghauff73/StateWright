#include "statewright/reasoning/kernel.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/reasoning/topology.hpp"

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

void validate_score(int value, std::string_view label) {
  if (value < 0 || value > score_scale) {
    policy_error(std::string(label) + " must be 0..10000");
  }
}

[[nodiscard]] std::vector<std::string>
canonical_strings(const std::vector<std::string> &values) {
  std::set<std::string> result;
  for (const auto &value : values) {
    if (!value.empty()) {
      result.insert(value);
    }
  }
  return {result.begin(), result.end()};
}

[[nodiscard]] ReasoningBudget sign_budget(ReasoningBudget budget) {
  budget.signature.clear();
  budget = canonicalize_reasoning_budget(std::move(budget));
  auto material = to_json(budget);
  material.erase("schema_version");
  material.erase("signature");
  budget.signature = contracts::sha256_json(material);
  return budget;
}

} // namespace

SuperReasoningKernel::SuperReasoningKernel(SuperReasoningKernelOptions options)
    : max_candidates_(std::max(1, options.max_candidates)),
      max_provider_calls_(std::max(4, options.max_provider_calls)),
      minimum_voi_bp_(options.minimum_voi_bp),
      acceptance_confidence_bp_(options.acceptance_confidence_bp),
      acceptance_verifier_bp_(options.acceptance_verifier_bp),
      acceptance_falsifier_bp_(options.acceptance_falsifier_bp) {
  validate_score(minimum_voi_bp_, "minimum value of information");
  validate_score(acceptance_confidence_bp_,
                 "reasoning acceptance confidence");
  validate_score(acceptance_verifier_bp_, "reasoning acceptance verifier");
  validate_score(acceptance_falsifier_bp_,
                 "reasoning acceptance falsifier");
  if (options.ablation.has_value()) {
    ablation_ = make_ablation_configuration(*options.ablation);
  } else {
    AblationConfiguration profile;
    profile.path_count = max_candidates_;
    ablation_ = make_ablation_configuration(std::move(profile));
  }
}

ReasoningProblem SuperReasoningKernel::create_problem(
    std::string statement, std::string goal, std::string source_snapshot_hash,
    std::string boundary_signature, std::string dimension_signature,
    std::vector<std::string> evidence_ids, int uncertainty_bp,
    int difficulty_bp, bool mutually_exclusive_hypotheses) {
  return create_reasoning_problem(
      std::move(statement), std::move(goal), std::move(source_snapshot_hash),
      std::move(boundary_signature), std::move(dimension_signature),
      std::move(evidence_ids), uncertainty_bp, difficulty_bp,
      mutually_exclusive_hypotheses);
}

HypothesisSet SuperReasoningKernel::build_hypothesis_state(
    const std::vector<HypothesisProposal> &proposals, std::string problem_id,
    int max_hypotheses, bool mutually_exclusive) {
  return build_hypothesis_set(proposals, std::move(problem_id), max_hypotheses,
                              mutually_exclusive);
}

HypothesisSet SuperReasoningKernel::build_hypothesis_state(
    const std::vector<Hypothesis> &hypotheses, std::string problem_id,
    int max_hypotheses, bool mutually_exclusive) {
  return build_hypothesis_set(hypotheses, std::move(problem_id), max_hypotheses,
                              mutually_exclusive);
}

std::vector<Hypothesis> SuperReasoningKernel::build_hypotheses(
    const std::vector<HypothesisProposal> &proposals, int max_hypotheses,
    bool mutually_exclusive) {
  return build_hypothesis_set(proposals, "compatibility:hypothesis-pool",
                              max_hypotheses, mutually_exclusive)
      .hypotheses;
}

ReasoningBudget SuperReasoningKernel::derive_budget(
    const DimensionBudget &dimension_budget, const ReasoningProblem &problem,
    int verifier_disagreement,
    std::optional<int> provider_sample_cap) const {
  int candidate_cap = std::min(max_candidates_, ablation_.path_count);
  if (provider_sample_cap.has_value()) {
    candidate_cap =
        std::min(candidate_cap, std::max(1, *provider_sample_cap));
  }
  auto budget = derive_reasoning_budget(
      dimension_budget, problem.uncertainty_bp, problem.difficulty_bp,
      verifier_disagreement, candidate_cap, max_provider_calls_,
      minimum_voi_bp_);
  if (!ablation_.adaptive_compute_enabled) {
    const int candidate_count =
        std::min(ablation_.path_count, budget.maximum_candidates);
    budget.candidate_count = candidate_count;
    budget.verifier_count = candidate_count;
    budget.falsifier_count = !ablation_.falsifier_enabled
                                 ? candidate_count
                                 : std::min(2, candidate_count);
    budget.max_generation_attempts =
        !ablation_.diversity_filter_enabled
            ? candidate_count
            : std::min(budget.maximum_candidates, candidate_count * 2);
    budget = sign_budget(std::move(budget));
  } else if (!ablation_.falsifier_enabled) {
    budget.falsifier_count = budget.candidate_count;
    budget = sign_budget(std::move(budget));
  }
  return budget;
}

ReasoningRunResult SuperReasoningKernel::run(
    providers::ReasoningProvider &provider, const ReasoningProblem &problem,
    const HypothesisSet &hypotheses, const DimensionBudget &dimension_budget,
    std::vector<std::string> declared_evidence_ids,
    const std::optional<ReasoningCertificate> &previous_certificate,
    const std::optional<CandidateSet> &previous_candidates,
    std::optional<int> provider_sample_cap) const {
  declared_evidence_ids = canonical_strings(declared_evidence_ids);
  const std::set<std::string> declared(declared_evidence_ids.begin(),
                                       declared_evidence_ids.end());
  if (std::ranges::any_of(
          problem.evidence_ids, [&declared](const std::string &identity) {
            return !declared.contains(identity);
          })) {
    policy_error("reasoning problem references unavailable evidence");
  }
  const int disagreement = previous_candidates.has_value()
                               ? verifier_disagreement_bp(*previous_candidates)
                               : 0;
  const auto budget = derive_budget(dimension_budget, problem, disagreement,
                                    provider_sample_cap);

  HypothesisSet active_state;
  if (!ablation_.hypothesis_state_enabled) {
    HypothesisProposal direct;
    direct.hypothesis_id =
        "hypothesis:" + contracts::sha256_json(
                            {{"problem_id", problem.problem_id},
                             {"proposition", problem.statement}});
    direct.proposition = problem.statement;
    direct.prior_bp = score_scale;
    direct.posterior_bp = score_scale;
    direct.supporting_evidence = problem.evidence_ids;
    active_state = build_hypothesis_state({direct}, problem.problem_id, 1);
  } else {
    require_hypothesis_set_integrity(hypotheses);
    if (hypotheses.problem_id != problem.problem_id) {
      policy_error("hypothesis state conflicts with reasoning problem");
    }
    if (hypotheses.hypotheses.size() >
        static_cast<std::size_t>(budget.max_hypotheses)) {
      policy_error("hypothesis state exceeds the derived reasoning budget");
    }
    if (hypotheses.mutually_exclusive !=
        problem.mutually_exclusive_hypotheses) {
      policy_error(
          "hypothesis state exclusivity conflicts with reasoning problem");
    }
    active_state = hypotheses;
  }

  auto active = active_state.hypotheses;
  auto candidates = search_reasoning_candidates(
      provider, problem, active, declared_evidence_ids, budget, ablation_);
  if (ablation_.hypothesis_state_enabled && ablation_.falsifier_enabled) {
    const auto update = apply_falsifier_updates(active_state, candidates);
    active_state = update.state;
    active = active_state.hypotheses;
    if (!update.records.empty()) {
      candidates.hypothesis_updates = update.records;
      candidates = sign_candidate_set(std::move(candidates));
    }
  }

  const auto topology =
      build_reasoning_topology(problem, active, candidates);
  auto topology_evidence = declared_evidence_ids;
  for (const auto &report : candidates.verifier_reports) {
    topology_evidence.push_back(report.report_id);
  }
  for (const auto &report : candidates.falsifier_reports) {
    topology_evidence.push_back(report.report_id);
  }
  topology_evidence = canonical_strings(topology_evidence);
  TopologyBudget topology_budget;
  topology_budget.max_topology_nodes =
      static_cast<std::size_t>(budget.max_topology_nodes);
  topology_budget.max_topology_edges =
      static_cast<std::size_t>(budget.max_topology_edges);
  topology_budget.max_branch_factor =
      static_cast<std::size_t>(budget.max_branch_factor);
  validate_reasoning_topology(topology, topology_budget, topology_evidence);

  auto certificate = certify(problem, active, budget, candidates, topology,
                             previous_certificate);
  if (certificate.decision == "ACCEPT" &&
      !candidates.selected_path_id.empty() &&
      ablation_.hypothesis_state_enabled) {
    active = support_selected_hypotheses(active, candidates);
    certificate = certify(problem, active, budget, candidates, topology,
                          previous_certificate);
  }
  return {.hypotheses = std::move(active),
          .budget = budget,
          .candidates = std::move(candidates),
          .topology = topology,
          .certificate = std::move(certificate)};
}

ReasoningRunResult SuperReasoningKernel::run(
    providers::ReasoningProvider &provider, const ReasoningProblem &problem,
    const std::vector<Hypothesis> &hypotheses,
    const DimensionBudget &dimension_budget,
    std::vector<std::string> declared_evidence_ids,
    const std::optional<ReasoningCertificate> &previous_certificate,
    const std::optional<CandidateSet> &previous_candidates,
    std::optional<int> provider_sample_cap) const {
  const auto state = build_hypothesis_state(
      hypotheses, problem.problem_id,
      std::min(max_candidates_, dimension_budget.max_active_hypotheses),
      problem.mutually_exclusive_hypotheses);
  return run(provider, problem, state, dimension_budget,
             std::move(declared_evidence_ids), previous_certificate,
             previous_candidates, provider_sample_cap);
}

ReasoningCertificate SuperReasoningKernel::certify(
    const ReasoningProblem &problem,
    const std::vector<Hypothesis> &hypotheses,
    const ReasoningBudget &budget, const CandidateSet &candidates,
    const ReasoningTopology &topology,
    const std::optional<ReasoningCertificate> &previous_certificate) const {
  CertificationPolicy policy;
  policy.acceptance_confidence_bp = acceptance_confidence_bp_;
  policy.acceptance_verifier_bp = acceptance_verifier_bp_;
  policy.acceptance_falsifier_bp = acceptance_falsifier_bp_;
  policy.ablation = ablation_;
  return certify_reasoning(problem, hypotheses, budget, candidates, topology,
                           policy, previous_certificate);
}

HypothesisStateUpdate SuperReasoningKernel::apply_falsifier_updates(
    const HypothesisSet &state, const CandidateSet &candidates) {
  std::map<std::string, ReasoningPath> paths;
  for (const auto &path : candidates.paths) {
    paths.emplace(path.path_id, path);
  }
  auto current = state;
  std::vector<HypothesisUpdateRecord> updates;
  for (const auto &report : candidates.falsifier_reports) {
    std::vector<std::string> challenges = report.counterexamples;
    challenges.insert(challenges.end(), report.alternative_explanations.begin(),
                      report.alternative_explanations.end());
    challenges.insert(challenges.end(), report.boundary_cases.begin(),
                      report.boundary_cases.end());
    challenges.insert(challenges.end(),
                      report.reversed_causal_directions.begin(),
                      report.reversed_causal_directions.end());
    challenges.insert(challenges.end(), report.invalid_invariants.begin(),
                      report.invalid_invariants.end());
    if (challenges.empty() && report.contradicted_step_ids.empty()) {
      continue;
    }
    const auto found = paths.find(report.path_id);
    if (found == paths.end()) {
      policy_error("falsifier report references an unknown candidate path");
    }
    auto update = apply_collision_update(
        current, found->second.hypothesis_ids,
        challenges.empty() ? "contradicted reasoning step" : challenges.front(),
        {report.report_id}, "reasoning-collision:" + report.signature,
        report.severity_bp);
    current = std::move(update.state);
    updates.insert(updates.end(), update.records.begin(), update.records.end());
  }
  std::ranges::sort(updates, {}, &HypothesisUpdateRecord::update_id);
  return {.state = std::move(current), .records = std::move(updates)};
}

std::vector<Hypothesis> SuperReasoningKernel::support_selected_hypotheses(
    const std::vector<Hypothesis> &hypotheses,
    const CandidateSet &candidates) {
  const auto selected = std::ranges::find(
      candidates.paths, candidates.selected_path_id, &ReasoningPath::path_id);
  if (selected == candidates.paths.end()) {
    policy_error("selected candidate path is unavailable");
  }
  std::set<std::string> selected_evidence;
  for (const auto &step : selected->steps) {
    selected_evidence.insert(step.evidence_ids.begin(), step.evidence_ids.end());
  }
  std::vector<Hypothesis> updated = hypotheses;
  for (auto &hypothesis : updated) {
    if (std::ranges::find(selected->hypothesis_ids, hypothesis.hypothesis_id) ==
        selected->hypothesis_ids.end()) {
      continue;
    }
    std::set<std::string> supporting(hypothesis.supporting_evidence.begin(),
                                     hypothesis.supporting_evidence.end());
    supporting.insert(selected_evidence.begin(), selected_evidence.end());
    hypothesis.supporting_evidence = {supporting.begin(), supporting.end()};
    hypothesis.status =
        hypothesis.assumptions.empty() ? "SUPPORTED" : "UNRESOLVED";
    hypothesis.signature.clear();
  }
  return build_hypothesis_set(updated, "compatibility:selected",
                              std::max(1, static_cast<int>(updated.size())))
      .hypotheses;
}

int SuperReasoningKernel::max_candidates() const noexcept {
  return max_candidates_;
}

int SuperReasoningKernel::max_provider_calls() const noexcept {
  return max_provider_calls_;
}

const AblationConfiguration &SuperReasoningKernel::ablation() const noexcept {
  return ablation_;
}

} // namespace statewright::reasoning
