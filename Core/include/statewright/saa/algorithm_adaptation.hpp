#pragma once

#include "statewright/saa/retrieval_explanation.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view algorithm_adaptation_version =
    "saa-controlled-algorithm-adaptation-v1";
inline constexpr std::size_t max_adaptation_steps = 16U;

struct AdaptationStep final {
  int index = 0;
  std::string component;
  std::string dimension;
  std::string base_algorithm_id;
  std::string current_contract;
  std::string target_contract;
  contracts::Json proposed_change = contracts::Json::object();
  std::string step_signature;
};

struct ControlledAdaptationPlan final {
  int schema_version = 1;
  std::string adaptation_version = std::string(algorithm_adaptation_version);
  std::string source_explanation_signature;
  std::vector<AdaptationStep> steps;
  bool one_dimension_per_step = true;
  bool qualification_required = false;
  bool canonical_reuse_eligible = false;
  std::string plan_signature;
};

struct AdaptedAlgorithmCandidate final {
  int schema_version = 1;
  std::string adaptation_version = std::string(algorithm_adaptation_version);
  std::string base_algorithm_id;
  std::string component;
  std::string changed_dimension;
  contracts::Json change_material = contracts::Json::object();
  std::string parent_candidate_signature;
  std::string candidate_signature;
  std::string epistemic_status;
  bool qualification_required = true;
  bool canonical_reuse_eligible = false;
};

[[nodiscard]] bool allowed_adaptation_dimension(std::string_view dimension);
[[nodiscard]] ControlledAdaptationPlan build_controlled_adaptation_plan(
    const RetrievalExplanation &explanation,
    std::string selected_mathematical_algorithm_id = {},
    std::string selected_reasoning_id = {});
[[nodiscard]] AdaptedAlgorithmCandidate create_adapted_candidate(
    const AdaptationStep &step, contracts::Json change_material,
    std::string parent_candidate_signature = {});

[[nodiscard]] contracts::Json to_json(const AdaptationStep &value);
[[nodiscard]] contracts::Json to_json(const ControlledAdaptationPlan &value);
[[nodiscard]] contracts::Json to_json(const AdaptedAlgorithmCandidate &value);

} // namespace statewright::saa
