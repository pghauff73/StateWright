#pragma once

#include "statewright/saa/reasoning_fit.hpp"
#include "statewright/saa/semantic_units.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view unified_retrieval_version =
    "saa-unified-retrieval-v1";
inline constexpr std::size_t max_unified_mathematical_results = 64U;

class MathematicalAlgorithmCatalog {
public:
  virtual ~MathematicalAlgorithmCatalog() = default;
  [[nodiscard]] virtual std::vector<contracts::Json>
  list_mathematical_algorithms() = 0;
};

class SemanticMeaningEquivalence {
public:
  virtual ~SemanticMeaningEquivalence() = default;
  [[nodiscard]] virtual bool meanings_equivalent(std::string_view left,
                                                 std::string_view right) = 0;
};

struct UnifiedProblemRequirements final {
  std::string problem_id;
  std::vector<SemanticConcept> input_concepts;
  int desired_mathematical_output_count = 0;
  std::string mathematical_domain;
  std::vector<std::string> reasoning_desired_outputs;
  std::vector<std::string> reasoning_applicability;
  std::vector<std::string> required_invariants;
  std::vector<std::string> available_evidence_requirements;
  int max_reasoning_steps = statewright::saa::max_reasoning_steps;
  bool require_mathematical_algorithm = true;
  bool require_reasoning_algorithm = true;
};

struct MathematicalFitAssessment final {
  std::string canonical_algorithm_id;
  std::string status;
  int fit_score_bp = 0;
  int semantic_input_fit_bp = 0;
  int output_shape_fit_bp = 0;
  int domain_fit_bp = 0;
  std::vector<std::string> matched_input_meanings;
  std::vector<std::string> unmatched_input_meanings;
  std::vector<std::string> blocking_gaps;
  std::string fit_signature;

  [[nodiscard]] bool eligible() const noexcept {
    return blocking_gaps.empty();
  }
};

struct UnifiedRetrievalDecision final {
  int schema_version = 1;
  std::string retrieval_version = std::string(unified_retrieval_version);
  std::string problem_signature;
  std::vector<MathematicalFitAssessment> mathematical_candidates;
  std::optional<std::string> selected_mathematical_algorithm_id;
  std::optional<ReasoningRetrievalResult> reasoning_result;
  std::optional<std::string> selected_reasoning_id;
  bool required_components_satisfied = false;
  std::vector<std::string> missing_components;
  std::string status;
  std::string decision_signature;
};

[[nodiscard]] UnifiedProblemRequirements canonical_unified_requirements(
    UnifiedProblemRequirements requirements);
[[nodiscard]] MathematicalFitAssessment evaluate_mathematical_fit(
    const contracts::Json &item, UnifiedProblemRequirements requirements,
    SemanticMeaningEquivalence *ontology = nullptr);
[[nodiscard]] UnifiedRetrievalDecision retrieve_unified_solution(
    MathematicalAlgorithmCatalog *mathematical_catalog,
    const ReasoningAlgorithmCatalog *reasoning_catalog,
    UnifiedProblemRequirements requirements,
    SemanticMeaningEquivalence *ontology = nullptr,
    std::size_t mathematical_limit = 10U,
    std::size_t reasoning_limit = 10U);

[[nodiscard]] contracts::Json to_json(const UnifiedProblemRequirements &value);
[[nodiscard]] contracts::Json to_json(const MathematicalFitAssessment &value);
[[nodiscard]] contracts::Json to_json(const UnifiedRetrievalDecision &value);

} // namespace statewright::saa
