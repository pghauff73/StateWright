#pragma once

#include "statewright/saa/algorithm_transfer.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view retrieval_explanation_version =
    "saa-retrieval-explanation-v1";

struct CounterfactualFitChange final {
  std::string component;
  std::string dimension;
  std::string current;
  std::string required_change;
  bool would_remove_blocker = false;
};

struct RetrievalExplanation final {
  int schema_version = 1;
  std::string explanation_version =
      std::string(retrieval_explanation_version);
  std::string decision_signature;
  std::string status;
  std::vector<std::string> selected_reasons;
  std::vector<std::string> rejected_reasons;
  std::vector<CounterfactualFitChange> counterfactual_changes;
  std::vector<std::string> fit_gap_dimensions;
  std::string explanation_signature;
};

[[nodiscard]] RetrievalExplanation
explain_algorithm_transfer(const AlgorithmTransferAssessment &assessment);
[[nodiscard]] RetrievalExplanation explain_unified_retrieval(
    const UnifiedRetrievalDecision &decision,
    UnifiedProblemRequirements requirements);

[[nodiscard]] contracts::Json to_json(const CounterfactualFitChange &value);
[[nodiscard]] contracts::Json to_json(const RetrievalExplanation &value);

} // namespace statewright::saa
