#pragma once

#include "statewright/saa/reasoning_outcome.hpp"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view reasoning_composition_version =
    "saa-reasoning-composition-v1";

struct ReasoningCompositionAssessment final {
  std::string status;
  bool interface_eligible = false;
  bool invariant_eligible = false;
  bool termination_eligible = false;
  bool qualification_eligible = false;
  bool exact_component_identity = false;
  std::vector<std::string> blocking_gaps;
  std::string assessment_signature;

  [[nodiscard]] bool composition_eligible() const noexcept {
    return blocking_gaps.empty();
  }
};

struct CanonicalReasoningComposition final {
  int schema_version = 1;
  std::string composition_version =
      std::string(reasoning_composition_version);
  std::string left_signature;
  std::string right_signature;
  CanonicalReasoningAlgorithm composed_algorithm;
  std::vector<std::string> interface_semantics;
  std::array<std::string, 2> component_qualification_signatures;
  bool qualification_required = true;
  bool canonical_reuse_eligible = false;
  std::string composition_signature;
  std::vector<std::string> warnings;
};

[[nodiscard]] ReasoningCompositionAssessment assess_reasoning_composition(
    const CanonicalReasoningAlgorithm &left,
    const CanonicalReasoningAlgorithm &right,
    const ReasoningOutcomeQualification *left_qualification = nullptr,
    const ReasoningOutcomeQualification *right_qualification = nullptr);

[[nodiscard]] CanonicalReasoningComposition compose_reasoning_algorithms(
    const CanonicalReasoningAlgorithm &left,
    const CanonicalReasoningAlgorithm &right,
    const ReasoningOutcomeQualification &left_qualification,
    const ReasoningOutcomeQualification &right_qualification);

[[nodiscard]] contracts::Json
to_json(const ReasoningCompositionAssessment &value);
[[nodiscard]] contracts::Json
to_json(const CanonicalReasoningComposition &value);

} // namespace statewright::saa
