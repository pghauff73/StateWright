#pragma once

#include "statewright/saa/reasoning_algebra.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view reasoning_semantic_version =
    "saa-reasoning-semantics-v1";

struct ReasoningStateDimension final {
  std::string dimension_id;
  std::string label;
  std::string meaning;
  std::string representation_kind = "ATOMIC";
  std::string epistemic_status = "UNVERIFIED_CONCEPT";
  std::vector<std::string> evidence_ids;
  bool declared_independent = true;
};

struct ReasoningStateDependency final {
  std::string source_dimension_id;
  std::string target_dimension_id;
  std::string relation = "CONTRIBUTES_TO";
  std::vector<std::string> evidence_ids;
};

struct ReasoningStateModel final {
  std::vector<ReasoningStateDimension> dimensions;
  std::vector<ReasoningStateDependency> dependencies;
};

struct ReasoningSemanticIssue final {
  std::string issue_id;
  std::string issue_kind;
  std::vector<std::string> dimension_ids;
  std::string label;
  std::vector<std::string> meanings;
  bool blocking = false;
  std::string status;
  std::vector<std::string> questions;
  std::string issue_signature;
};

struct ReasoningSemanticAssessment final {
  int schema_version = 1;
  std::string semantic_version = std::string(reasoning_semantic_version);
  std::string status;
  std::vector<ReasoningSemanticIssue> issues;
  std::string state_signature;
  bool canonical_reasoning_state_eligible = false;
  bool public_artifact_only = true;
  std::string assessment_signature;
};

struct ReasoningSemanticDirective final {
  std::string issue_id;
  std::string subsystem;
  std::string action;
  bool blocking = false;
  contracts::Json payload = contracts::Json::object();
};

[[nodiscard]] ReasoningSemanticAssessment assess_reasoning_state_semantics(
    const ReasoningStateModel &state,
    const CanonicalReasoningAlgorithm *algorithm = nullptr);
[[nodiscard]] std::vector<ReasoningSemanticDirective>
propagate_reasoning_semantic_issues(
    const std::vector<ReasoningSemanticIssue> &issues);

[[nodiscard]] contracts::Json to_json(const ReasoningStateDimension &value);
[[nodiscard]] contracts::Json to_json(const ReasoningStateDependency &value);
[[nodiscard]] contracts::Json to_json(const ReasoningStateModel &value);
[[nodiscard]] contracts::Json to_json(const ReasoningSemanticIssue &value);
[[nodiscard]] contracts::Json
to_json(const ReasoningSemanticAssessment &value);
[[nodiscard]] contracts::Json to_json(const ReasoningSemanticDirective &value);

} // namespace statewright::saa
