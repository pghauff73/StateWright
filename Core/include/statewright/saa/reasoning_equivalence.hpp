#pragma once

#include "statewright/saa/reasoning_algebra.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view reasoning_equivalence_version =
    "saa-reasoning-equivalence-v1";

struct ReasoningTopologyDelta final {
  std::vector<std::string> added_operators;
  std::vector<std::string> removed_operators;
  std::vector<std::string> added_relations;
  std::vector<std::string> removed_relations;
  std::vector<std::string> semantic_input_delta;
  std::vector<std::string> semantic_output_delta;
  std::string delta_signature;
};

struct ReasoningEquivalenceAssessment final {
  int schema_version = 1;
  std::string equivalence_version = std::string(reasoning_equivalence_version);
  std::string status;
  bool exact_equivalence = false;
  bool topology_match = false;
  bool semantic_match = false;
  bool conservative_source_binding = false;
  std::vector<std::string> relation_candidates;
  ReasoningTopologyDelta delta;
  bool canonical_reuse_eligible = false;
  std::string assessment_signature;
  std::vector<std::string> warnings;
};

[[nodiscard]] ReasoningTopologyDelta reasoning_topology_delta(
    const CanonicalReasoningAlgorithm &left,
    const CanonicalReasoningAlgorithm &right);
[[nodiscard]] ReasoningEquivalenceAssessment compare_reasoning_algorithms(
    const CanonicalReasoningAlgorithm &left,
    const CanonicalReasoningAlgorithm &right);

[[nodiscard]] contracts::Json to_json(const ReasoningTopologyDelta &value);
[[nodiscard]] contracts::Json
to_json(const ReasoningEquivalenceAssessment &value);

} // namespace statewright::saa
