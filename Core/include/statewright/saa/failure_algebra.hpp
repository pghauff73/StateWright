#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view failure_algebra_version =
    "saa-failure-algebra-v1";

struct FailureObservation final {
  std::string source_kind;
  std::string component;
  std::string failure_class;
  std::string mechanism;
  std::vector<std::string> semantic_roles;
  std::vector<std::string> violated_invariants;
  std::string boundary_signature;
  std::string context_signature;
  std::vector<std::string> evidence_ids;
  std::string provenance_id;
  std::string observation_signature;
};

struct CanonicalFailurePattern final {
  std::string failure_class;
  std::string component;
  std::string mechanism;
  std::vector<std::string> semantic_roles;
  std::vector<std::string> violated_invariants;
  std::string boundary_signature;
  std::string context_signature;
  std::string pattern_signature;
};

struct FailureMatchAssessment final {
  std::string status;
  bool exact_match = false;
  bool retry_blocked = false;
  std::string matched_pattern_signature;
  std::vector<std::string> differences;
  std::string assessment_signature;
};

[[nodiscard]] FailureObservation make_failure_observation(
    std::string source_kind, std::string component,
    std::string failure_class, std::string mechanism,
    std::vector<std::string> semantic_roles,
    std::vector<std::string> violated_invariants,
    std::string boundary_signature, std::string context_signature,
    std::vector<std::string> evidence_ids, std::string provenance_id);
[[nodiscard]] CanonicalFailurePattern
canonicalize_failure(const FailureObservation &observation);
[[nodiscard]] FailureMatchAssessment compare_failure_to_pattern(
    const FailureObservation &observation,
    const CanonicalFailurePattern &pattern, int prior_occurrence_count = 1);

[[nodiscard]] contracts::Json to_json(const FailureObservation &value);
[[nodiscard]] contracts::Json to_json(const CanonicalFailurePattern &value);
[[nodiscard]] contracts::Json to_json(const FailureMatchAssessment &value);

} // namespace statewright::saa
