#pragma once

#include "statewright/saa/reasoning_algebra.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view reasoning_outcome_version =
    "saa-reasoning-outcome-v1";

struct ReasoningGroundingEvidence final {
  std::string object_type;
  std::optional<bool> success;
  bool simulated = false;
  std::string producer;
  std::string method;
  std::vector<std::string> requirement_ids;
  std::string independence_group;
};

using ReasoningEvidenceResolver =
    std::function<std::optional<ReasoningGroundingEvidence>(std::string_view)>;

struct ReasoningExecutionOutcome final {
  int schema_version = 1;
  std::string outcome_version = std::string(reasoning_outcome_version);
  std::string canonical_reasoning_signature;
  std::string execution_id;
  std::vector<std::string> observed_output_semantics;
  std::vector<std::string> evidence_ids;
  std::vector<std::pair<std::string, bool>> invariant_results;
  std::vector<std::pair<std::string, std::string>> falsifier_results;
  bool termination_satisfied = false;
  int steps_used = 0;
  bool execution_success = false;
  bool independent_review = false;
  std::string outcome_signature;
};

struct ReasoningOutcomeQualification final {
  int schema_version = 1;
  std::string outcome_version = std::string(reasoning_outcome_version);
  std::string canonical_reasoning_signature;
  std::string outcome_signature;
  std::string status;
  int evidence_requirement_coverage_bp = 0;
  std::vector<std::string> grounded_evidence_ids;
  std::vector<std::string> independence_groups;
  bool invariant_eligible = false;
  bool falsifier_eligible = false;
  bool termination_eligible = false;
  bool output_contract_eligible = false;
  bool canonical_reuse_eligible = false;
  std::string qualification_signature;
  std::vector<std::string> warnings;
};

[[nodiscard]] std::vector<std::string>
reasoning_evidence_requirements(const CanonicalReasoningAlgorithm &algorithm);

[[nodiscard]] std::vector<std::string>
reasoning_falsifiers(const CanonicalReasoningAlgorithm &algorithm);

[[nodiscard]] ReasoningExecutionOutcome make_reasoning_execution_outcome(
    const CanonicalReasoningAlgorithm &algorithm, std::string execution_id,
    std::vector<std::string> observed_output_semantics,
    std::vector<std::string> evidence_ids,
    std::vector<std::pair<std::string, bool>> invariant_results,
    std::vector<std::pair<std::string, std::string>> falsifier_results,
    bool termination_satisfied, int steps_used, bool execution_success,
    bool independent_review);

[[nodiscard]] ReasoningOutcomeQualification qualify_reasoning_outcome(
    const ReasoningEvidenceResolver &evidence_resolver,
    const CanonicalReasoningAlgorithm &algorithm,
    const ReasoningExecutionOutcome &outcome);

[[nodiscard]] contracts::Json to_json(const ReasoningExecutionOutcome &value);
[[nodiscard]] contracts::Json
to_json(const ReasoningOutcomeQualification &value);

} // namespace statewright::saa
