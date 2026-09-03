#pragma once

#include "statewright/saa/retrieval_explanation.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view retrieve_first_version =
    "saa-retrieve-first-policy-v1";

struct RetrieveFirstReceipt final {
  int schema_version = 1;
  std::string policy_version = std::string(retrieve_first_version);
  std::string status;
  bool retrieval_attempted = false;
  bool required_search_completed = false;
  bool new_algorithm_generation_allowed = false;
  bool adaptation_allowed = false;
  std::vector<std::string> generation_scope;
  std::optional<std::string> selected_mathematical_algorithm_id;
  std::optional<std::string> selected_reasoning_id;
  std::string retrieval_decision_signature;
  std::string explanation_signature;
  std::vector<std::string> fit_gap_dimensions;
  std::vector<std::string> guidance;
  std::string receipt_signature;
};

class RetrieveFirstController final {
public:
  RetrieveFirstController(MathematicalAlgorithmCatalog *mathematical_catalog,
                          const ReasoningAlgorithmCatalog *reasoning_catalog,
                          SemanticMeaningEquivalence *ontology = nullptr);

  [[nodiscard]] RetrieveFirstReceipt
  evaluate(UnifiedProblemRequirements requirements) const;

private:
  MathematicalAlgorithmCatalog *mathematical_catalog_;
  const ReasoningAlgorithmCatalog *reasoning_catalog_;
  SemanticMeaningEquivalence *ontology_;
};

[[nodiscard]] contracts::Json to_json(const RetrieveFirstReceipt &value);

} // namespace statewright::saa
