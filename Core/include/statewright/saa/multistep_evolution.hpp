#pragma once

#include "statewright/saa/algorithm_adaptation.hpp"
#include "statewright/saa/reasoning_outcome.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view multistep_evolution_version =
    "saa-multistep-evolution-v1";
inline constexpr std::size_t max_evolution_steps = 16U;

class AdaptationLineageCatalog {
public:
  virtual ~AdaptationLineageCatalog() = default;
  [[nodiscard]] virtual std::vector<std::string>
  ancestors(std::string_view candidate_ref) const = 0;
  [[nodiscard]] virtual contracts::Json
  get_candidate(std::string_view candidate_ref) const = 0;
  [[nodiscard]] virtual std::vector<contracts::Json> lineage_edges() const = 0;
};

struct EvolutionStepDescriptor final {
  int index = 0;
  std::string parent_ref;
  std::string candidate_ref;
  std::string changed_dimension;
  std::string edge_signature;
  std::string candidate_signature;
};

struct MultiStepEvolutionPlan final {
  int schema_version = 1;
  std::string evolution_version = std::string(multistep_evolution_version);
  std::string root_algorithm_ref;
  std::string final_candidate_ref;
  std::vector<std::string> frozen_invariants;
  std::vector<std::string> allowed_dimensions;
  std::vector<EvolutionStepDescriptor> steps;
  bool one_dimension_per_step = true;
  std::string plan_signature;
};

struct EvolutionStepQualification final {
  int schema_version = 1;
  std::string evolution_version = std::string(multistep_evolution_version);
  std::string plan_signature;
  std::string candidate_ref;
  std::string changed_dimension;
  std::vector<std::pair<std::string, bool>> invariant_results;
  std::vector<std::string> grounded_evidence_ids;
  std::vector<std::string> independence_groups;
  bool independent_review = false;
  std::string status;
  bool step_qualified = false;
  std::string qualification_signature;
};

struct MultiStepEvolutionAssessment final {
  int schema_version = 1;
  std::string evolution_version = std::string(multistep_evolution_version);
  std::string plan_signature;
  std::string final_candidate_ref;
  std::vector<std::string> qualification_signatures;
  int qualified_step_count = 0;
  int total_step_count = 0;
  bool invariant_preservation_complete = false;
  std::string status;
  bool evolution_qualified = false;
  std::vector<std::string> blocking_steps;
  std::string assessment_signature;
};

[[nodiscard]] MultiStepEvolutionPlan make_multistep_evolution_plan(
    const AdaptationLineageCatalog &lineage_catalog,
    std::string final_candidate_ref,
    std::vector<std::string> frozen_invariants,
    std::vector<std::string> allowed_dimensions = {},
    std::size_t max_steps = max_evolution_steps);
[[nodiscard]] EvolutionStepQualification qualify_evolution_step(
    const ReasoningEvidenceResolver &evidence_resolver,
    const MultiStepEvolutionPlan &plan, std::string candidate_ref,
    std::vector<std::pair<std::string, bool>> invariant_results,
    std::vector<std::string> evidence_ids, bool independent_review);
[[nodiscard]] MultiStepEvolutionAssessment assess_multistep_evolution(
    const MultiStepEvolutionPlan &plan,
    const std::vector<EvolutionStepQualification> &qualifications);

[[nodiscard]] contracts::Json to_json(const EvolutionStepDescriptor &value);
[[nodiscard]] contracts::Json to_json(const MultiStepEvolutionPlan &value);
[[nodiscard]] contracts::Json
to_json(const EvolutionStepQualification &value);
[[nodiscard]] contracts::Json
to_json(const MultiStepEvolutionAssessment &value);

} // namespace statewright::saa
