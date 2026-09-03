#pragma once

#include "statewright/saa/reasoning_outcome.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view reasoning_fit_version =
    "saa-reasoning-fit-v1";
inline constexpr std::size_t max_reasoning_retrieval_results = 64U;

struct ReasoningTaskRequirements final {
  std::vector<std::string> available_inputs;
  std::vector<std::string> desired_outputs;
  std::vector<std::string> required_applicability;
  std::vector<std::string> required_invariants;
  std::vector<std::string> available_evidence_requirements;
  int max_steps = max_reasoning_steps;
};

struct ReasoningFitAssessment final {
  std::string reasoning_id;
  std::string canonical_reasoning_signature;
  std::string status;
  int fit_score_bp = 0;
  int input_fit_bp = 0;
  int output_fit_bp = 0;
  int applicability_fit_bp = 0;
  int invariant_fit_bp = 0;
  int evidence_fit_bp = 0;
  int termination_fit_bp = 0;
  std::vector<std::string> blocking_gaps;
  std::vector<std::string> adaptation_gaps;
  std::string fit_signature;

  [[nodiscard]] bool eligible() const noexcept {
    return blocking_gaps.empty();
  }
};

struct ReasoningRetrievalResult final {
  int schema_version = 1;
  std::string fit_version = std::string(reasoning_fit_version);
  std::string requirements_signature;
  std::vector<ReasoningFitAssessment> candidates;
  std::optional<std::string> selected_reasoning_id;
  int selected_fit_score_bp = 0;
  std::string search_scope =
      "QUALIFIED_CANONICAL_REASONING_STORE_ONLY";
  std::string result_signature;
};

class ReasoningAlgorithmCatalog {
public:
  virtual ~ReasoningAlgorithmCatalog() = default;
  [[nodiscard]] virtual std::vector<std::string>
  list_reasoning_ids() const = 0;
  [[nodiscard]] virtual CanonicalReasoningAlgorithm
  load_reasoning_algorithm(std::string_view reasoning_id) const = 0;
};

[[nodiscard]] ReasoningTaskRequirements
canonical_reasoning_task_requirements(ReasoningTaskRequirements requirements);

[[nodiscard]] ReasoningFitAssessment evaluate_reasoning_fit(
    std::string reasoning_id, const CanonicalReasoningAlgorithm &algorithm,
    ReasoningTaskRequirements requirements);

[[nodiscard]] ReasoningRetrievalResult retrieve_reasoning_algorithms(
    const ReasoningAlgorithmCatalog &catalog,
    ReasoningTaskRequirements requirements, std::size_t limit = 10U,
    bool include_ineligible = false);

[[nodiscard]] contracts::Json to_json(const ReasoningTaskRequirements &value);
[[nodiscard]] contracts::Json to_json(const ReasoningFitAssessment &value);
[[nodiscard]] contracts::Json to_json(const ReasoningRetrievalResult &value);

} // namespace statewright::saa
