#include "statewright/reasoning/certification.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <utility>

namespace statewright::reasoning {
namespace {

constexpr std::array<std::string_view, 5> reasoning_decisions{
    "ACCEPT", "REVISE", "REGENERATE", "STOP_UNRESOLVED", "STOP_NO_VALUE"};
constexpr std::array<std::string_view, 6> terminal_states{
    "SOLUTION", "EPISTEMIC_STOP", "INSUFFICIENT_EVIDENCE",
    "GOVERNANCE_STOP", "COMPUTE_BUDGET_EXHAUSTED",
    "NO_SURVIVING_HYPOTHESIS"};

[[noreturn]] void policy_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied, std::move(message));
}

template <std::size_t Size>
[[nodiscard]] bool contains(
    const std::array<std::string_view, Size> &values,
    std::string_view candidate) noexcept {
  return std::ranges::find(values, candidate) != values.end();
}

[[nodiscard]] std::string trim(std::string value) {
  const auto first = std::ranges::find_if_not(
      value, [](unsigned char character) { return std::isspace(character) != 0; });
  const auto last = std::find_if_not(
      value.rbegin(), value.rend(),
      [](unsigned char character) { return std::isspace(character) != 0; })
                        .base();
  if (first >= last) {
    return {};
  }
  return std::string(first, last);
}

[[nodiscard]] std::vector<std::string>
canonical_strings(const std::vector<std::string> &values) {
  std::set<std::string> unique;
  for (const auto &value : values) {
    if (!value.empty()) {
      unique.insert(value);
    }
  }
  return {unique.begin(), unique.end()};
}

void validate_score(int value, std::string_view label) {
  if (value < 0 || value > score_scale) {
    policy_error(std::string(label) + " must be 0..10000");
  }
}

[[nodiscard]] contracts::Json certificate_hash_material(
    const ReasoningCertificate &certificate) {
  auto material = to_json(certificate);
  material.erase("schema_version");
  material.erase("signature");
  return material;
}

[[nodiscard]] bool has_reason(const std::vector<std::string> &reasons,
                              std::string_view reason) {
  return std::ranges::find(reasons, reason) != reasons.end();
}

} // namespace

contracts::Json to_json(const ReasoningCertificate &value) {
  return {{"schema_version", value.schema_version},
          {"problem_hash", value.problem_hash},
          {"boundary_signature", value.boundary_signature},
          {"dimension_signature", value.dimension_signature},
          {"hypothesis_signature", value.hypothesis_signature},
          {"topology_signature", value.topology_signature},
          {"candidate_set_signature", value.candidate_set_signature},
          {"synthesis_signature", value.synthesis_signature},
          {"score_config_id", value.score_config_id},
          {"score_config_hash", value.score_config_hash},
          {"ablation_id", value.ablation_id},
          {"ablation_config_hash", value.ablation_config_hash},
          {"active_hypothesis_ids", value.active_hypothesis_ids},
          {"candidate_count", value.candidate_count},
          {"surviving_candidate_count", value.surviving_candidate_count},
          {"winning_candidate_id", value.winning_candidate_id},
          {"winning_path_id", value.winning_path_id},
          {"verifier_report_ids", value.verifier_report_ids},
          {"falsifier_report_ids", value.falsifier_report_ids},
          {"evidence_coverage_bp", value.evidence_coverage_bp},
          {"verifier_score_bp", value.verifier_score_bp},
          {"falsification_score_bp", value.falsification_score_bp},
          {"contradiction_count", value.contradiction_count},
          {"unresolved_contradiction_ids",
           value.unresolved_contradiction_ids},
          {"uncertainty_before_bp", value.uncertainty_before_bp},
          {"uncertainty_after_bp", value.uncertainty_after_bp},
          {"disagreement_bp", value.disagreement_bp},
          {"residual_risk_bp", value.residual_risk_bp},
          {"compute_spent_bp", value.compute_spent_bp},
          {"unresolved_assumptions", value.unresolved_assumptions},
          {"reasoning_topology_hash", value.reasoning_topology_hash},
          {"derived_confidence_bp", value.derived_confidence_bp},
          {"decision", value.decision},
          {"terminal_state", value.terminal_state},
          {"reasons", value.reasons},
          {"signature", value.signature}};
}

ReasoningCertificate
canonicalize_reasoning_certificate(ReasoningCertificate value) {
  if (!contains(reasoning_decisions, value.decision)) {
    policy_error("invalid reasoning decision: " + value.decision);
  }
  if (!contains(terminal_states, value.terminal_state)) {
    policy_error("invalid reasoning terminal state: " + value.terminal_state);
  }
  if (value.candidate_count < 0 || value.surviving_candidate_count < 0 ||
      value.contradiction_count < 0) {
    policy_error("reasoning certificate counts cannot be negative");
  }
  if (value.surviving_candidate_count > value.candidate_count) {
    policy_error("surviving candidate count cannot exceed candidate count");
  }
  validate_score(value.evidence_coverage_bp, "evidence_coverage_bp");
  validate_score(value.verifier_score_bp, "verifier_score_bp");
  validate_score(value.falsification_score_bp, "falsification_score_bp");
  validate_score(value.uncertainty_before_bp, "uncertainty_before_bp");
  validate_score(value.uncertainty_after_bp, "uncertainty_after_bp");
  validate_score(value.disagreement_bp, "disagreement_bp");
  validate_score(value.residual_risk_bp, "residual_risk_bp");
  validate_score(value.compute_spent_bp, "compute_spent_bp");
  validate_score(value.derived_confidence_bp, "derived_confidence_bp");
  value.active_hypothesis_ids = canonical_strings(value.active_hypothesis_ids);
  value.verifier_report_ids = canonical_strings(value.verifier_report_ids);
  value.falsifier_report_ids = canonical_strings(value.falsifier_report_ids);
  value.unresolved_assumptions = canonical_strings(value.unresolved_assumptions);
  value.unresolved_contradiction_ids =
      canonical_strings(value.unresolved_contradiction_ids);
  value.reasons = canonical_strings(value.reasons);
  if (!value.winning_candidate_id.empty() && !value.winning_path_id.empty() &&
      value.winning_candidate_id != value.winning_path_id) {
    policy_error("reasoning certificate winning path projections conflict");
  }
  const std::string winning = value.winning_path_id.empty()
                                  ? value.winning_candidate_id
                                  : value.winning_path_id;
  value.winning_candidate_id = winning;
  value.winning_path_id = winning;
  if (!value.reasoning_topology_hash.empty() &&
      !value.topology_signature.empty() &&
      value.reasoning_topology_hash != value.topology_signature) {
    policy_error("reasoning certificate topology projections conflict");
  }
  const std::string topology = value.topology_signature.empty()
                                   ? value.reasoning_topology_hash
                                   : value.topology_signature;
  value.reasoning_topology_hash = topology;
  value.topology_signature = topology;
  return value;
}

ReasoningProblem create_reasoning_problem(
    std::string statement, std::string goal, std::string source_snapshot_hash,
    std::string boundary_signature, std::string dimension_signature,
    std::vector<std::string> evidence_ids, int uncertainty_bp,
    int difficulty_bp, bool mutually_exclusive_hypotheses) {
  statement = trim(std::move(statement));
  goal = trim(std::move(goal));
  validate_score(uncertainty_bp, "problem uncertainty");
  validate_score(difficulty_bp, "problem difficulty");
  evidence_ids = canonical_strings(evidence_ids);
  const auto material = contracts::Json{
      {"statement", statement},
      {"goal", goal},
      {"source_snapshot_hash", source_snapshot_hash},
      {"boundary_signature", boundary_signature},
      {"dimension_signature", dimension_signature},
      {"evidence_ids", evidence_ids},
      {"uncertainty_bp", uncertainty_bp},
      {"difficulty_bp", difficulty_bp},
      {"mutually_exclusive_hypotheses", mutually_exclusive_hypotheses}};
  const std::string problem_id =
      "problem:" + contracts::sha256_json(material);
  ReasoningProblem problem;
  problem.problem_id = problem_id;
  problem.statement = std::move(statement);
  problem.goal = std::move(goal);
  problem.source_snapshot_hash = std::move(source_snapshot_hash);
  problem.boundary_signature = std::move(boundary_signature);
  problem.dimension_signature = std::move(dimension_signature);
  problem.evidence_ids = std::move(evidence_ids);
  problem.uncertainty_bp = uncertainty_bp;
  problem.difficulty_bp = difficulty_bp;
  problem.mutually_exclusive_hypotheses = mutually_exclusive_hypotheses;
  auto signature_material = material;
  signature_material["problem_id"] = problem_id;
  problem.signature = contracts::sha256_json(signature_material);
  return canonicalize_reasoning_problem(std::move(problem));
}

CandidateSet sign_candidate_set(CandidateSet candidates) {
  candidates = canonicalize_candidate_set(std::move(candidates));
  candidates.signature.clear();
  auto material = to_json(candidates);
  material.erase("schema_version");
  material.erase("signature");
  candidates.signature = contracts::sha256_json(material);
  return candidates;
}

int verifier_disagreement_bp(const CandidateSet &candidate_value) {
  const auto candidates = canonicalize_candidate_set(candidate_value);
  if (candidates.verifier_reports.empty()) {
    return 0;
  }
  std::map<std::string, int> counts;
  for (const auto &report : candidates.verifier_reports) {
    ++counts[report.verdict];
  }
  const auto majority = std::ranges::max_element(
      counts, {}, [](const auto &entry) { return entry.second; });
  return score_scale -
         majority->second * score_scale /
             static_cast<int>(candidates.verifier_reports.size());
}

std::string
hypothesis_collection_signature(const std::vector<Hypothesis> &hypotheses) {
  auto sorted = hypotheses;
  std::ranges::sort(sorted, {}, &Hypothesis::hypothesis_id);
  contracts::Json material = contracts::Json::array();
  for (const auto &hypothesis : sorted) {
    material.push_back(to_json(hypothesis));
  }
  return contracts::sha256_json(material);
}

ReasoningCertificate certify_reasoning(
    const ReasoningProblem &problem_value,
    const std::vector<Hypothesis> &hypotheses,
    const ReasoningBudget &budget_value, const CandidateSet &candidate_value,
    const ReasoningTopology &topology,
    const CertificationPolicy &policy_value,
    const std::optional<ReasoningCertificate> &previous_certificate) {
  require_problem_integrity(problem_value);
  require_candidate_integrity(candidate_value);
  require_reasoning_topology_integrity(topology);
  const auto problem = canonicalize_reasoning_problem(problem_value);
  const auto budget = canonicalize_reasoning_budget(budget_value);
  const auto candidates = canonicalize_candidate_set(candidate_value);
  auto ablation = make_ablation_configuration(policy_value.ablation);
  validate_score(policy_value.acceptance_confidence_bp,
                 "reasoning acceptance confidence");
  validate_score(policy_value.acceptance_verifier_bp,
                 "reasoning acceptance verifier");
  validate_score(policy_value.acceptance_falsifier_bp,
                 "reasoning acceptance falsifier");

  const auto selected_metrics = std::ranges::find(
      candidates.metrics, candidates.selected_path_id,
      &ReasoningMetrics::path_id);
  const bool has_selected_metrics = selected_metrics != candidates.metrics.end();
  const auto contradictions = build_contradiction_records(candidates);
  const auto critical = unresolved_critical_contradictions(contradictions);
  const int confidence = cap_confidence_for_contradictions(
      derive_reasoning_confidence_bp(candidates), contradictions);

  std::vector<std::string> selected_contradiction_ids;
  if (!candidates.selected_path_id.empty()) {
    if (std::ranges::find(candidates.verifier_reports,
                          candidates.selected_path_id,
                          &VerifierReport::path_id) ==
            candidates.verifier_reports.end() ||
        std::ranges::find(candidates.falsifier_reports,
                          candidates.selected_path_id,
                          &FalsifierReport::path_id) ==
            candidates.falsifier_reports.end()) {
      policy_error("selected reasoning path lacks verifier or falsifier report");
    }
    for (const auto &record : contradictions) {
      if (record.left_claim_id.starts_with(candidates.selected_path_id)) {
        selected_contradiction_ids.push_back(record.contradiction_id);
      }
    }
  } else {
    for (const auto &record : contradictions) {
      selected_contradiction_ids.push_back(record.contradiction_id);
    }
  }

  std::set<std::string> selected_hypothesis_ids;
  std::vector<std::string> unresolved_assumptions;
  for (const auto &path : candidates.paths) {
    if (path.path_id != candidates.selected_path_id) {
      continue;
    }
    selected_hypothesis_ids.insert(path.hypothesis_ids.begin(),
                                   path.hypothesis_ids.end());
    for (const auto &step : path.steps) {
      unresolved_assumptions.insert(unresolved_assumptions.end(),
                                    step.assumptions.begin(),
                                    step.assumptions.end());
    }
  }
  for (const auto &hypothesis : hypotheses) {
    if (selected_hypothesis_ids.contains(hypothesis.hypothesis_id)) {
      unresolved_assumptions.insert(unresolved_assumptions.end(),
                                    hypothesis.assumptions.begin(),
                                    hypothesis.assumptions.end());
    }
  }
  unresolved_assumptions = canonical_strings(unresolved_assumptions);

  int compute_spent = 0;
  for (const auto &path : candidates.paths) {
    compute_spent = std::min(score_scale, compute_spent + path.estimated_cost_bp);
  }
  const int uncertainty_after =
      has_selected_metrics ? selected_metrics->uncertainty_bp
                           : problem.uncertainty_bp;
  std::vector<std::string> reasons;
  std::string decision = "STOP_UNRESOLVED";
  const int disagreement = verifier_disagreement_bp(candidates);
  if (has_selected_metrics) {
    if (selected_metrics->verifier_bp >= policy_value.acceptance_verifier_bp) {
      reasons.push_back("process_verified");
    }
    if (selected_metrics->falsifier_bp >= policy_value.acceptance_falsifier_bp) {
      reasons.push_back("falsifier_survived");
    }
    if (confidence >= policy_value.acceptance_confidence_bp) {
      reasons.push_back("derived_confidence");
    }
    if (uncertainty_after <= problem.uncertainty_bp) {
      reasons.push_back("uncertainty_not_increased");
    }
    if (unresolved_assumptions.empty()) {
      reasons.push_back("assumptions_resolved");
    }
    if (candidates.synthesis && candidates.synthesis->verified) {
      reasons.push_back("synthesis_verified");
    } else if (candidates.synthesis &&
               (!ablation.synthesis_verification_enabled ||
                !ablation.verifier_enabled)) {
      reasons.push_back("synthesis_verification_disabled");
    }
    if (!critical.empty()) {
      reasons.push_back("critical_contradiction_unresolved");
    }
    const std::string synthesis_reason =
        ablation.synthesis_verification_enabled && ablation.verifier_enabled
            ? "synthesis_verified"
            : "synthesis_verification_disabled";
    const bool required =
        has_reason(reasons, "process_verified") &&
        has_reason(reasons, "falsifier_survived") &&
        has_reason(reasons, "derived_confidence") &&
        has_reason(reasons, "uncertainty_not_increased") &&
        has_reason(reasons, "assumptions_resolved") &&
        has_reason(reasons, synthesis_reason);
    if (required) {
      decision = critical.empty() ? "ACCEPT" : "STOP_UNRESOLVED";
    } else if (should_continue_reasoning(
                   budget,
                   std::max(0, problem.uncertainty_bp - uncertainty_after),
                   compute_spent, 25)) {
      decision = "REVISE";
    } else {
      decision = "STOP_NO_VALUE";
    }
  } else if (should_continue_reasoning(budget, problem.uncertainty_bp,
                                       compute_spent, 25)) {
    decision = "REGENERATE";
    reasons.push_back("no_surviving_candidate");
  } else {
    decision = "STOP_NO_VALUE";
    reasons.push_back("no_positive_value_of_information");
  }

  const auto next_operation = choose_reasoning_operation(
      budget,
      {{"GENERATE_HYPOTHESIS",
        candidates.selected_path_id.empty() ? problem.uncertainty_bp : 0},
       {"RETRIEVE_EVIDENCE",
        std::max(0, problem.uncertainty_bp -
                        (has_selected_metrics
                             ? selected_metrics->evidence_support_bp
                             : 0))},
       {"RUN_READ_ONLY_EXPERIMENT", std::max(0, uncertainty_after / 2)},
       {"VERIFY_AGAIN", disagreement},
       {"SEARCH_COUNTEREXAMPLE",
        std::max(0, score_scale -
                        (has_selected_metrics ? selected_metrics->falsifier_bp
                                              : 0))},
       {"REFINE_DIMENSION", std::max(0, problem.difficulty_bp / 2)}});
  if ((decision == "REVISE" || decision == "REGENERATE") &&
      next_operation.operation == "STOP") {
    decision = "STOP_NO_VALUE";
    reasons.push_back("no_positive_value_of_information");
  }
  if (decision == "ACCEPT" && previous_certificate &&
      previous_certificate->problem_hash == problem.signature &&
      previous_certificate->ablation_config_hash == ablation.signature) {
    const bool improved =
        (has_selected_metrics &&
         selected_metrics->evidence_support_bp >
             previous_certificate->evidence_coverage_bp) ||
        uncertainty_after < previous_certificate->uncertainty_after_bp ||
        static_cast<int>(selected_contradiction_ids.size()) <
            previous_certificate->contradiction_count ||
        confidence > previous_certificate->derived_confidence_bp;
    if (!improved) {
      decision = "STOP_NO_VALUE";
      reasons = {"no_reasoning_progress"};
    }
  }

  std::string terminal_state;
  if (decision == "ACCEPT") {
    terminal_state = "SOLUTION";
  } else if (compute_spent >= budget.max_compute_bp) {
    terminal_state = "COMPUTE_BUDGET_EXHAUSTED";
  } else if (candidates.selected_path_id.empty()) {
    terminal_state = "NO_SURVIVING_HYPOTHESIS";
  } else if (decision == "STOP_NO_VALUE") {
    terminal_state = "EPISTEMIC_STOP";
  } else {
    terminal_state = "INSUFFICIENT_EVIDENCE";
  }

  ReasoningCertificate certificate;
  certificate.problem_hash = problem.signature;
  certificate.boundary_signature = problem.boundary_signature;
  certificate.dimension_signature = problem.dimension_signature;
  certificate.hypothesis_signature =
      hypothesis_collection_signature(hypotheses);
  certificate.topology_signature = topology.signature;
  certificate.candidate_set_signature = candidates.signature;
  certificate.synthesis_signature =
      candidates.synthesis ? candidates.synthesis->signature : "";
  certificate.score_config_id = candidates.score_config_id;
  certificate.score_config_hash = candidates.score_config_hash;
  certificate.ablation_id = ablation.ablation_id;
  certificate.ablation_config_hash = ablation.signature;
  for (const auto &hypothesis : hypotheses) {
    if (hypothesis.status != "FALSIFIED") {
      certificate.active_hypothesis_ids.push_back(hypothesis.hypothesis_id);
    }
  }
  certificate.candidate_count = static_cast<int>(candidates.paths.size());
  certificate.surviving_candidate_count =
      static_cast<int>(candidates.surviving_path_ids.size());
  certificate.winning_candidate_id = candidates.selected_path_id;
  for (const auto &report : candidates.verifier_reports) {
    certificate.verifier_report_ids.push_back(report.report_id);
  }
  for (const auto &report : candidates.falsifier_reports) {
    certificate.falsifier_report_ids.push_back(report.report_id);
  }
  certificate.evidence_coverage_bp =
      has_selected_metrics ? selected_metrics->evidence_support_bp : 0;
  certificate.verifier_score_bp =
      has_selected_metrics ? selected_metrics->verifier_bp : 0;
  certificate.falsification_score_bp =
      has_selected_metrics ? selected_metrics->falsifier_bp : 0;
  certificate.contradiction_count =
      static_cast<int>(selected_contradiction_ids.size());
  certificate.unresolved_contradiction_ids = selected_contradiction_ids;
  certificate.uncertainty_before_bp = problem.uncertainty_bp;
  certificate.uncertainty_after_bp = uncertainty_after;
  certificate.disagreement_bp = disagreement;
  certificate.residual_risk_bp =
      has_selected_metrics ? selected_metrics->risk_bp : score_scale;
  certificate.compute_spent_bp = compute_spent;
  certificate.unresolved_assumptions = unresolved_assumptions;
  certificate.reasoning_topology_hash = topology.signature;
  certificate.derived_confidence_bp = confidence;
  certificate.decision = std::move(decision);
  certificate.terminal_state = std::move(terminal_state);
  certificate.reasons = std::move(reasons);
  certificate = canonicalize_reasoning_certificate(std::move(certificate));
  certificate.signature =
      contracts::sha256_json(certificate_hash_material(certificate));
  return certificate;
}

void require_problem_integrity(const ReasoningProblem &problem) {
  auto material = to_json(problem);
  material.erase("schema_version");
  const std::string signature = material.at("signature").get<std::string>();
  material.erase("signature");
  if (signature != contracts::sha256_json(material)) {
    policy_error("reasoning problem signature mismatch");
  }
}

void require_candidate_integrity(const CandidateSet &candidate_value) {
  const auto candidates = canonicalize_candidate_set(candidate_value);
  auto material = to_json(candidates);
  material.erase("schema_version");
  const std::string signature = material.at("signature").get<std::string>();
  material.erase("signature");
  if (signature != contracts::sha256_json(material)) {
    policy_error("reasoning candidate-set signature mismatch");
  }
}

void require_reasoning_topology_integrity(const ReasoningTopology &topology) {
  if (topology.signature !=
      contracts::sha256_json(reasoning_topology_payload(topology))) {
    policy_error("reasoning topology signature mismatch");
  }
}

void require_reasoning_certificate_integrity(
    const ReasoningCertificate &certificate_value) {
  const auto certificate =
      canonicalize_reasoning_certificate(certificate_value);
  if (certificate.signature !=
      contracts::sha256_json(certificate_hash_material(certificate))) {
    policy_error("reasoning certificate signature mismatch");
  }
}

} // namespace statewright::reasoning
