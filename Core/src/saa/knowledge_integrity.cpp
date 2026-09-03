#include "statewright/saa/knowledge_integrity.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <array>
#include <ranges>
#include <set>
#include <string_view>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[noreturn]] void integrity_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

[[nodiscard]] int count(int value, std::string_view label) {
  if (value < 0) {
    integrity_error(std::string(label) + " must be a non-negative integer");
  }
  return value;
}

[[nodiscard]] int basis_points(int value, std::string_view label) {
  if (value < 0 || value > 10000) {
    integrity_error(std::string(label) +
                    " must be integer basis points in 0..10000");
  }
  return value;
}

[[nodiscard]] int rate_bp(int numerator, int denominator,
                          bool inverse = false) {
  if (denominator <= 0) {
    return inverse ? 10000 : 0;
  }
  const int raw = std::min(10000, (10000 * numerator) / denominator);
  return inverse ? 10000 - raw : raw;
}

} // namespace

KnowledgeIntegrityPolicy
canonical_knowledge_integrity_policy(KnowledgeIntegrityPolicy policy) {
  policy.max_contradiction_rate_bp = basis_points(
      policy.max_contradiction_rate_bp, "max_contradiction_rate_bp");
  policy.max_semantic_drift_rate_bp = basis_points(
      policy.max_semantic_drift_rate_bp, "max_semantic_drift_rate_bp");
  policy.max_false_admission_rate_bp = basis_points(
      policy.max_false_admission_rate_bp, "max_false_admission_rate_bp");
  policy.max_corrected_error_recurrence_rate_bp = basis_points(
      policy.max_corrected_error_recurrence_rate_bp,
      "max_corrected_error_recurrence_rate_bp");
  policy.min_retrieval_precision_bp = basis_points(
      policy.min_retrieval_precision_bp, "min_retrieval_precision_bp");
  policy.min_equivalent_failure_avoidance_bp = basis_points(
      policy.min_equivalent_failure_avoidance_bp,
      "min_equivalent_failure_avoidance_bp");
  return policy;
}

KnowledgeIntegritySnapshot make_integrity_snapshot(
    int generation, int canonical_knowledge_count, int semantic_contradictions,
    int semantic_drift_events, int false_canonical_admissions,
    int corrected_error_opportunities, int corrected_error_recurrences,
    int retrieval_queries, int retrieval_correct_selections,
    int equivalent_failure_opportunities, int equivalent_failure_retries) {
  generation = count(generation, "generation");
  canonical_knowledge_count =
      count(canonical_knowledge_count, "canonical knowledge count");
  semantic_contradictions =
      count(semantic_contradictions, "semantic contradictions");
  semantic_drift_events =
      count(semantic_drift_events, "semantic drift events");
  false_canonical_admissions =
      count(false_canonical_admissions, "false canonical admissions");
  corrected_error_opportunities = count(corrected_error_opportunities,
                                        "corrected error opportunities");
  corrected_error_recurrences =
      count(corrected_error_recurrences, "corrected error recurrences");
  retrieval_queries = count(retrieval_queries, "retrieval queries");
  retrieval_correct_selections = count(retrieval_correct_selections,
                                       "retrieval correct selections");
  equivalent_failure_opportunities = count(
      equivalent_failure_opportunities, "equivalent failure opportunities");
  equivalent_failure_retries =
      count(equivalent_failure_retries, "equivalent failure retries");
  if (corrected_error_recurrences > corrected_error_opportunities) {
    integrity_error(
        "corrected error recurrences cannot exceed opportunities");
  }
  if (retrieval_correct_selections > retrieval_queries) {
    integrity_error(
        "retrieval correct selections cannot exceed retrieval queries");
  }
  if (equivalent_failure_retries > equivalent_failure_opportunities) {
    integrity_error("equivalent failure retries cannot exceed opportunities");
  }
  const int contradiction_rate =
      rate_bp(semantic_contradictions, canonical_knowledge_count);
  const int drift_rate =
      rate_bp(semantic_drift_events, canonical_knowledge_count);
  const int false_rate =
      rate_bp(false_canonical_admissions, canonical_knowledge_count);
  const int recurrence_rate = rate_bp(corrected_error_recurrences,
                                      corrected_error_opportunities);
  const int precision =
      rate_bp(retrieval_correct_selections, retrieval_queries);
  const int avoidance = rate_bp(equivalent_failure_retries,
                                equivalent_failure_opportunities, true);
  const Json material =
      {{"canonical_knowledge_count", canonical_knowledge_count},
       {"contradiction_rate_bp", contradiction_rate},
       {"corrected_error_opportunities", corrected_error_opportunities},
       {"corrected_error_recurrence_rate_bp", recurrence_rate},
       {"corrected_error_recurrences", corrected_error_recurrences},
       {"equivalent_failure_avoidance_bp", avoidance},
       {"equivalent_failure_opportunities", equivalent_failure_opportunities},
       {"equivalent_failure_retries", equivalent_failure_retries},
       {"false_admission_rate_bp", false_rate},
       {"false_canonical_admissions", false_canonical_admissions},
       {"generation", generation},
       {"retrieval_correct_selections", retrieval_correct_selections},
       {"retrieval_precision_bp", precision},
       {"retrieval_queries", retrieval_queries},
       {"semantic_contradictions", semantic_contradictions},
       {"semantic_drift_events", semantic_drift_events},
       {"semantic_drift_rate_bp", drift_rate},
       {"version", knowledge_integrity_version}};
  return {.generation = generation,
          .canonical_knowledge_count = canonical_knowledge_count,
          .semantic_contradictions = semantic_contradictions,
          .semantic_drift_events = semantic_drift_events,
          .false_canonical_admissions = false_canonical_admissions,
          .corrected_error_opportunities = corrected_error_opportunities,
          .corrected_error_recurrences = corrected_error_recurrences,
          .retrieval_queries = retrieval_queries,
          .retrieval_correct_selections = retrieval_correct_selections,
          .equivalent_failure_opportunities =
              equivalent_failure_opportunities,
          .equivalent_failure_retries = equivalent_failure_retries,
          .contradiction_rate_bp = contradiction_rate,
          .semantic_drift_rate_bp = drift_rate,
          .false_admission_rate_bp = false_rate,
          .corrected_error_recurrence_rate_bp = recurrence_rate,
          .retrieval_precision_bp = precision,
          .equivalent_failure_avoidance_bp = avoidance,
          .snapshot_signature = contracts::sha256_json(material)};
}

KnowledgeIntegrityTrajectory assess_integrity_trajectory(
    std::vector<KnowledgeIntegritySnapshot> snapshots,
    KnowledgeIntegrityPolicy policy) {
  if (snapshots.empty()) {
    integrity_error(
        "SAA-12.3 integrity assessment requires at least one snapshot");
  }
  std::ranges::sort(snapshots, {}, &KnowledgeIntegritySnapshot::generation);
  if (std::ranges::adjacent_find(snapshots, std::ranges::equal_to{},
                                 &KnowledgeIntegritySnapshot::generation) !=
      snapshots.end()) {
    integrity_error("SAA-12.3 integrity generations must be unique");
  }
  policy = canonical_knowledge_integrity_policy(policy);
  const auto &latest = snapshots.back();
  std::vector<std::string> violations;
  const auto maximum_check = [&](std::string_view name, int observed,
                                 int threshold) {
    if (observed > threshold) {
      violations.push_back(std::string(name) + ":" +
                           std::to_string(observed) + ":MAX:" +
                           std::to_string(threshold));
    }
  };
  const auto minimum_check = [&](std::string_view name, int observed,
                                 int threshold) {
    if (observed < threshold) {
      violations.push_back(std::string(name) + ":" +
                           std::to_string(observed) + ":MIN:" +
                           std::to_string(threshold));
    }
  };
  maximum_check("CONTRADICTION_RATE", latest.contradiction_rate_bp,
                policy.max_contradiction_rate_bp);
  maximum_check("SEMANTIC_DRIFT_RATE", latest.semantic_drift_rate_bp,
                policy.max_semantic_drift_rate_bp);
  maximum_check("FALSE_ADMISSION_RATE", latest.false_admission_rate_bp,
                policy.max_false_admission_rate_bp);
  maximum_check("CORRECTED_ERROR_RECURRENCE_RATE",
                latest.corrected_error_recurrence_rate_bp,
                policy.max_corrected_error_recurrence_rate_bp);
  minimum_check("RETRIEVAL_PRECISION", latest.retrieval_precision_bp,
                policy.min_retrieval_precision_bp);
  minimum_check("EQUIVALENT_FAILURE_AVOIDANCE",
                latest.equivalent_failure_avoidance_bp,
                policy.min_equivalent_failure_avoidance_bp);

  std::vector<std::string> degraded;
  std::vector<std::string> improved;
  if (snapshots.size() >= 2U) {
    const auto &previous = snapshots[snapshots.size() - 2U];
    const auto lower = [&](std::string name, int before, int after) {
      if (after > before) {
        degraded.push_back(std::move(name));
      } else if (after < before) {
        improved.push_back(std::move(name));
      }
    };
    const auto higher = [&](std::string name, int before, int after) {
      if (after < before) {
        degraded.push_back(std::move(name));
      } else if (after > before) {
        improved.push_back(std::move(name));
      }
    };
    lower("CONTRADICTION_RATE_BP", previous.contradiction_rate_bp,
          latest.contradiction_rate_bp);
    lower("SEMANTIC_DRIFT_RATE_BP", previous.semantic_drift_rate_bp,
          latest.semantic_drift_rate_bp);
    lower("FALSE_ADMISSION_RATE_BP", previous.false_admission_rate_bp,
          latest.false_admission_rate_bp);
    lower("CORRECTED_ERROR_RECURRENCE_RATE_BP",
          previous.corrected_error_recurrence_rate_bp,
          latest.corrected_error_recurrence_rate_bp);
    higher("RETRIEVAL_PRECISION_BP", previous.retrieval_precision_bp,
           latest.retrieval_precision_bp);
    higher("EQUIVALENT_FAILURE_AVOIDANCE_BP",
           previous.equivalent_failure_avoidance_bp,
           latest.equivalent_failure_avoidance_bp);
  }
  const bool qualified = violations.empty();
  std::string status;
  if (!qualified) {
    status = "KNOWLEDGE_INTEGRITY_POLICY_VIOLATION";
  } else if (!degraded.empty()) {
    status = "KNOWLEDGE_INTEGRITY_QUALIFIED_WITH_DEGRADATION_SIGNAL";
  } else if (!improved.empty()) {
    status = "KNOWLEDGE_INTEGRITY_QUALIFIED_IMPROVING";
  } else {
    status = "KNOWLEDGE_INTEGRITY_QUALIFIED_STABLE";
  }
  std::vector<std::string> signatures;
  signatures.reserve(snapshots.size());
  for (const auto &snapshot : snapshots) {
    signatures.push_back(snapshot.snapshot_signature);
  }
  const Json payload =
      {{"degraded", degraded},
       {"improved", improved},
       {"latest_generation", latest.generation},
       {"policy", to_json(policy)},
       {"snapshot_signatures", signatures},
       {"status", status},
       {"version", knowledge_integrity_version},
       {"violations", violations}};
  std::ranges::sort(degraded);
  std::ranges::sort(improved);
  return {.snapshot_signatures = std::move(signatures),
          .latest_generation = latest.generation,
          .status = std::move(status),
          .policy_violations = std::move(violations),
          .degraded_dimensions = std::move(degraded),
          .improved_dimensions = std::move(improved),
          .knowledge_integrity_qualified = qualified,
          .trajectory_signature = contracts::sha256_json(payload)};
}

Json to_json(const KnowledgeIntegritySnapshot &value) {
  return {{"canonical_knowledge_count", value.canonical_knowledge_count},
          {"contradiction_rate_bp", value.contradiction_rate_bp},
          {"corrected_error_opportunities",
           value.corrected_error_opportunities},
          {"corrected_error_recurrence_rate_bp",
           value.corrected_error_recurrence_rate_bp},
          {"corrected_error_recurrences",
           value.corrected_error_recurrences},
          {"equivalent_failure_avoidance_bp",
           value.equivalent_failure_avoidance_bp},
          {"equivalent_failure_opportunities",
           value.equivalent_failure_opportunities},
          {"equivalent_failure_retries", value.equivalent_failure_retries},
          {"false_admission_rate_bp", value.false_admission_rate_bp},
          {"false_canonical_admissions", value.false_canonical_admissions},
          {"generation", value.generation},
          {"retrieval_correct_selections",
           value.retrieval_correct_selections},
          {"retrieval_precision_bp", value.retrieval_precision_bp},
          {"retrieval_queries", value.retrieval_queries},
          {"semantic_contradictions", value.semantic_contradictions},
          {"semantic_drift_events", value.semantic_drift_events},
          {"semantic_drift_rate_bp", value.semantic_drift_rate_bp},
          {"snapshot_signature", value.snapshot_signature}};
}

Json to_json(const KnowledgeIntegrityPolicy &value) {
  return {{"max_contradiction_rate_bp", value.max_contradiction_rate_bp},
          {"max_corrected_error_recurrence_rate_bp",
           value.max_corrected_error_recurrence_rate_bp},
          {"max_false_admission_rate_bp",
           value.max_false_admission_rate_bp},
          {"max_semantic_drift_rate_bp",
           value.max_semantic_drift_rate_bp},
          {"min_equivalent_failure_avoidance_bp",
           value.min_equivalent_failure_avoidance_bp},
          {"min_retrieval_precision_bp",
           value.min_retrieval_precision_bp}};
}

Json to_json(const KnowledgeIntegrityTrajectory &value) {
  return {{"degraded_dimensions", value.degraded_dimensions},
          {"improved_dimensions", value.improved_dimensions},
          {"knowledge_integrity_qualified",
           value.knowledge_integrity_qualified},
          {"latest_generation", value.latest_generation},
          {"policy_violations", value.policy_violations},
          {"snapshot_signatures", value.snapshot_signatures},
          {"status", value.status},
          {"trajectory_signature", value.trajectory_signature}};
}

} // namespace statewright::saa
