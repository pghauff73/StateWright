#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view knowledge_integrity_version =
    "saa-longitudinal-knowledge-integrity-v1";

struct KnowledgeIntegritySnapshot final {
  int generation = 0;
  int canonical_knowledge_count = 0;
  int semantic_contradictions = 0;
  int semantic_drift_events = 0;
  int false_canonical_admissions = 0;
  int corrected_error_opportunities = 0;
  int corrected_error_recurrences = 0;
  int retrieval_queries = 0;
  int retrieval_correct_selections = 0;
  int equivalent_failure_opportunities = 0;
  int equivalent_failure_retries = 0;
  int contradiction_rate_bp = 0;
  int semantic_drift_rate_bp = 0;
  int false_admission_rate_bp = 0;
  int corrected_error_recurrence_rate_bp = 0;
  int retrieval_precision_bp = 0;
  int equivalent_failure_avoidance_bp = 0;
  std::string snapshot_signature;
};

struct KnowledgeIntegrityPolicy final {
  int max_contradiction_rate_bp = 500;
  int max_semantic_drift_rate_bp = 500;
  int max_false_admission_rate_bp = 0;
  int max_corrected_error_recurrence_rate_bp = 500;
  int min_retrieval_precision_bp = 9000;
  int min_equivalent_failure_avoidance_bp = 9000;
};

struct KnowledgeIntegrityTrajectory final {
  std::vector<std::string> snapshot_signatures;
  int latest_generation = 0;
  std::string status;
  std::vector<std::string> policy_violations;
  std::vector<std::string> degraded_dimensions;
  std::vector<std::string> improved_dimensions;
  bool knowledge_integrity_qualified = false;
  std::string trajectory_signature;
};

[[nodiscard]] KnowledgeIntegrityPolicy
canonical_knowledge_integrity_policy(KnowledgeIntegrityPolicy policy);
[[nodiscard]] KnowledgeIntegritySnapshot make_integrity_snapshot(
    int generation, int canonical_knowledge_count,
    int semantic_contradictions = 0, int semantic_drift_events = 0,
    int false_canonical_admissions = 0,
    int corrected_error_opportunities = 0,
    int corrected_error_recurrences = 0, int retrieval_queries = 0,
    int retrieval_correct_selections = 0,
    int equivalent_failure_opportunities = 0,
    int equivalent_failure_retries = 0);
[[nodiscard]] KnowledgeIntegrityTrajectory assess_integrity_trajectory(
    std::vector<KnowledgeIntegritySnapshot> snapshots,
    KnowledgeIntegrityPolicy policy = {});

[[nodiscard]] contracts::Json
to_json(const KnowledgeIntegritySnapshot &value);
[[nodiscard]] contracts::Json to_json(const KnowledgeIntegrityPolicy &value);
[[nodiscard]] contracts::Json
to_json(const KnowledgeIntegrityTrajectory &value);

} // namespace statewright::saa
