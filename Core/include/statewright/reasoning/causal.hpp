#pragma once

#include "statewright/contracts/canonical_json.hpp"
#include "statewright/reasoning/hypotheses.hpp"

#include <optional>
#include <string>
#include <vector>

namespace statewright::reasoning {

struct CausalNode final {
  std::string node_id;
  std::string variable;
  std::string kind = "observed";
  std::string signature;
};

struct CausalEdge final {
  std::string edge_id;
  std::string source_id;
  std::string target_id;
  std::string relation;
  std::vector<std::string> evidence_ids;
  bool temporal_ordered = false;
  std::string signature;
};

struct Intervention final {
  std::string intervention_id;
  std::string variable_id;
  std::string assigned_value;
  std::vector<std::string> assumptions;
  std::string signature;
};

struct Counterfactual final {
  std::string counterfactual_id;
  std::string intervention_id;
  std::string outcome_variable_id;
  std::string predicted_value;
  std::vector<std::string> assumptions;
  std::string signature;
};

struct CausalAssessment final {
  std::string claim;
  int confidence_bp = 0;
  std::vector<std::string> blockers;
  std::vector<std::string> evidence_ids;
  bool intervention_supported = false;
  std::string signature;
};

[[nodiscard]] const std::vector<std::string> &causal_relations();
[[nodiscard]] const std::vector<std::string> &causal_node_kinds();

[[nodiscard]] contracts::Json to_json(const CausalNode &value);
[[nodiscard]] contracts::Json to_json(const CausalEdge &value);
[[nodiscard]] contracts::Json to_json(const Intervention &value);
[[nodiscard]] contracts::Json to_json(const Counterfactual &value);
[[nodiscard]] contracts::Json to_json(const CausalAssessment &value);

[[nodiscard]] CausalNode make_causal_node(CausalNode value);
[[nodiscard]] CausalEdge make_causal_edge(CausalEdge value);
[[nodiscard]] Intervention make_intervention(Intervention value);
[[nodiscard]] Counterfactual make_counterfactual(Counterfactual value);
[[nodiscard]] CausalAssessment make_causal_assessment(CausalAssessment value);

void require_causal_node_integrity(const CausalNode &value);
void require_causal_edge_integrity(const CausalEdge &value);
void require_intervention_integrity(const Intervention &value);
void require_counterfactual_integrity(const Counterfactual &value);
void require_causal_assessment_integrity(const CausalAssessment &value);

[[nodiscard]] CausalAssessment assess_causal_claim(
    std::string claim, std::string source_id, std::string target_id,
    const std::vector<CausalEdge> &edges,
    const std::optional<Intervention> &intervention = std::nullopt,
    std::vector<std::string> declared_confounders = {},
    std::vector<std::string> alternative_explanations = {},
    int proposed_confidence_bp = score_scale);

} // namespace statewright::reasoning
