#include "statewright/reasoning/causal.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::reasoning {
namespace {

[[noreturn]] void policy_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied, std::move(message));
}

[[nodiscard]] bool contains(const std::vector<std::string> &values,
                            std::string_view candidate) {
  return std::ranges::find(values, candidate) != values.end();
}

[[nodiscard]] std::vector<std::string>
canonical_strings(const std::vector<std::string> &values,
                  bool discard_empty = false) {
  std::set<std::string> unique;
  for (const auto &value : values) {
    if (!discard_empty || !value.empty()) {
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

[[nodiscard]] std::string join(const std::vector<std::string> &values) {
  std::string result;
  for (const auto &value : values) {
    if (!result.empty()) {
      result.append(", ");
    }
    result.append(value);
  }
  return result;
}

template <typename Value>
[[nodiscard]] Value finish_signature(Value value, std::string_view label) {
  auto material = to_json(value);
  material.erase("signature");
  const std::string expected = contracts::sha256_json(material);
  if (!value.signature.empty() && value.signature != expected) {
    policy_error(std::string(label) + " signature mismatch");
  }
  value.signature = expected;
  return value;
}

} // namespace

const std::vector<std::string> &causal_relations() {
  static const std::vector<std::string> values{
      "causes", "confounds", "correlates", "mediates", "moderates"};
  return values;
}

const std::vector<std::string> &causal_node_kinds() {
  static const std::vector<std::string> values{
      "intervention", "latent", "observed", "outcome"};
  return values;
}

contracts::Json to_json(const CausalNode &value) {
  return {{"node_id", value.node_id},
          {"variable", value.variable},
          {"kind", value.kind},
          {"signature", value.signature}};
}

contracts::Json to_json(const CausalEdge &value) {
  return {{"edge_id", value.edge_id},
          {"source_id", value.source_id},
          {"target_id", value.target_id},
          {"relation", value.relation},
          {"evidence_ids", value.evidence_ids},
          {"temporal_ordered", value.temporal_ordered},
          {"signature", value.signature}};
}

contracts::Json to_json(const Intervention &value) {
  return {{"intervention_id", value.intervention_id},
          {"variable_id", value.variable_id},
          {"assigned_value", value.assigned_value},
          {"assumptions", value.assumptions},
          {"signature", value.signature}};
}

contracts::Json to_json(const Counterfactual &value) {
  return {{"counterfactual_id", value.counterfactual_id},
          {"intervention_id", value.intervention_id},
          {"outcome_variable_id", value.outcome_variable_id},
          {"predicted_value", value.predicted_value},
          {"assumptions", value.assumptions},
          {"signature", value.signature}};
}

contracts::Json to_json(const CausalAssessment &value) {
  return {{"claim", value.claim},
          {"confidence_bp", value.confidence_bp},
          {"blockers", value.blockers},
          {"evidence_ids", value.evidence_ids},
          {"intervention_supported", value.intervention_supported},
          {"signature", value.signature}};
}

CausalNode make_causal_node(CausalNode value) {
  if (value.node_id.empty() || value.variable.empty()) {
    policy_error("causal node identity must be non-empty");
  }
  if (!contains(causal_node_kinds(), value.kind)) {
    policy_error("invalid causal node kind: " + value.kind);
  }
  return finish_signature(std::move(value), "causal node");
}

CausalEdge make_causal_edge(CausalEdge value) {
  if (value.edge_id.empty() || value.source_id.empty() ||
      value.target_id.empty()) {
    policy_error("causal edge identity must be non-empty");
  }
  if (!contains(causal_relations(), value.relation)) {
    policy_error("invalid causal relation: " + value.relation);
  }
  value.evidence_ids = canonical_strings(value.evidence_ids);
  return finish_signature(std::move(value), "causal edge");
}

Intervention make_intervention(Intervention value) {
  if (value.intervention_id.empty() || value.variable_id.empty()) {
    policy_error("intervention identity must be non-empty");
  }
  value.assumptions = canonical_strings(value.assumptions);
  return finish_signature(std::move(value), "intervention");
}

Counterfactual make_counterfactual(Counterfactual value) {
  if (value.counterfactual_id.empty() || value.intervention_id.empty() ||
      value.outcome_variable_id.empty()) {
    policy_error("counterfactual identity must be non-empty");
  }
  value.assumptions = canonical_strings(value.assumptions);
  return finish_signature(std::move(value), "counterfactual");
}

CausalAssessment make_causal_assessment(CausalAssessment value) {
  if (value.claim.empty()) {
    policy_error("causal assessment claim must be non-empty");
  }
  validate_score(value.confidence_bp, "causal confidence");
  value.blockers = canonical_strings(value.blockers);
  value.evidence_ids = canonical_strings(value.evidence_ids);
  return finish_signature(std::move(value), "causal assessment");
}

void require_causal_node_integrity(const CausalNode &value) {
  CausalNode rebuilt = value;
  rebuilt.signature.clear();
  if (to_json(make_causal_node(std::move(rebuilt))) != to_json(value)) {
    policy_error("causal node integrity check failed");
  }
}

void require_causal_edge_integrity(const CausalEdge &value) {
  CausalEdge rebuilt = value;
  rebuilt.signature.clear();
  if (to_json(make_causal_edge(std::move(rebuilt))) != to_json(value)) {
    policy_error("causal edge integrity check failed");
  }
}

void require_intervention_integrity(const Intervention &value) {
  Intervention rebuilt = value;
  rebuilt.signature.clear();
  if (to_json(make_intervention(std::move(rebuilt))) != to_json(value)) {
    policy_error("intervention integrity check failed");
  }
}

void require_counterfactual_integrity(const Counterfactual &value) {
  Counterfactual rebuilt = value;
  rebuilt.signature.clear();
  if (to_json(make_counterfactual(std::move(rebuilt))) != to_json(value)) {
    policy_error("counterfactual integrity check failed");
  }
}

void require_causal_assessment_integrity(const CausalAssessment &value) {
  CausalAssessment rebuilt = value;
  rebuilt.signature.clear();
  if (to_json(make_causal_assessment(std::move(rebuilt))) != to_json(value)) {
    policy_error("causal assessment integrity check failed");
  }
}

CausalAssessment assess_causal_claim(
    std::string claim, std::string source_id, std::string target_id,
    const std::vector<CausalEdge> &edges,
    const std::optional<Intervention> &intervention,
    std::vector<std::string> declared_confounders,
    std::vector<std::string> alternative_explanations,
    int proposed_confidence_bp) {
  validate_score(proposed_confidence_bp, "proposed causal confidence");
  std::vector<CausalEdge> relevant;
  for (const auto &edge : edges) {
    if (edge.source_id == source_id && edge.target_id == target_id) {
      relevant.push_back(edge);
    }
  }

  std::vector<std::string> blockers;
  std::set<std::string> evidence_ids;
  std::vector<CausalEdge> direct;
  for (const auto &edge : relevant) {
    evidence_ids.insert(edge.evidence_ids.begin(), edge.evidence_ids.end());
    if (edge.relation == "causes") {
      direct.push_back(edge);
    }
  }
  const bool correlation_only = !relevant.empty() && direct.empty();
  if (relevant.empty()) {
    blockers.emplace_back("no causal or observational edge supports the claim");
  }
  if (correlation_only) {
    blockers.emplace_back(
        "observational correlation does not establish intervention effect");
  }
  if (!direct.empty() &&
      !std::ranges::all_of(direct, &CausalEdge::temporal_ordered)) {
    blockers.emplace_back("causal temporal ordering is unresolved");
  }

  declared_confounders = canonical_strings(declared_confounders, true);
  if (!declared_confounders.empty()) {
    blockers.push_back("declared confounders remain unresolved: " +
                       join(declared_confounders));
  }
  alternative_explanations =
      canonical_strings(alternative_explanations, true);
  if (!alternative_explanations.empty()) {
    blockers.push_back("alternative explanations remain unresolved: " +
                       join(alternative_explanations));
  }

  const bool intervention_supported =
      intervention.has_value() && intervention->variable_id == source_id &&
      !direct.empty() && blockers.empty();
  int confidence_bp = proposed_confidence_bp;
  if (!blockers.empty()) {
    confidence_bp = std::min(confidence_bp, 4'999);
  }
  if (correlation_only) {
    confidence_bp = std::min(confidence_bp, 3'000);
  }
  if (evidence_ids.empty()) {
    confidence_bp = std::min(confidence_bp, 2'500);
  }
  CausalAssessment assessment;
  assessment.claim = std::move(claim);
  assessment.confidence_bp = confidence_bp;
  assessment.blockers = std::move(blockers);
  assessment.evidence_ids = {evidence_ids.begin(), evidence_ids.end()};
  assessment.intervention_supported = intervention_supported;
  return make_causal_assessment(std::move(assessment));
}

} // namespace statewright::reasoning
