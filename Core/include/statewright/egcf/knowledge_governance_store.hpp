#pragma once

#include "statewright/egcf/store.hpp"
#include "statewright/saa/failure_algebra.hpp"
#include "statewright/saa/improvement_scheduling.hpp"
#include "statewright/saa/knowledge_integrity.hpp"
#include "statewright/saa/oiec_bench_gate.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::egcf {

inline constexpr std::string_view knowledge_governance_store_version =
    "saa-knowledge-governance-store-v1";
inline constexpr int knowledge_governance_store_schema_version = 1;

struct FailureRegistration final {
  std::string pattern_ref;
  std::string occurrence_ref;
  bool repeated = false;
};

class KnowledgeGovernanceStore final {
public:
  explicit KnowledgeGovernanceStore(EgcfStore &egcf_store);

  [[nodiscard]] const std::filesystem::path &root() const noexcept;
  [[nodiscard]] saa::ReasoningEvidenceResolver evidence_resolver() const;
  [[nodiscard]] FailureRegistration
  register_failure_observation(const saa::FailureObservation &observation);
  [[nodiscard]] std::vector<contracts::Json> failure_patterns();
  [[nodiscard]] int
  failure_occurrence_count(std::string_view pattern_signature);
  [[nodiscard]] std::optional<saa::FailureMatchAssessment>
  assess_failure_retry(const saa::FailureObservation &observation);
  [[nodiscard]] std::string
  register_benchmark_gate(const saa::OIECBenchGateAssessment &assessment);
  [[nodiscard]] std::string register_integrity_snapshot(
      const saa::KnowledgeIntegritySnapshot &snapshot);
  [[nodiscard]] std::string register_integrity_trajectory(
      const saa::KnowledgeIntegrityTrajectory &trajectory);
  [[nodiscard]] std::string
  register_opportunity(const saa::ImprovementOpportunity &opportunity);
  [[nodiscard]] std::string
  register_schedule(const saa::ImprovementSchedule &schedule);
  [[nodiscard]] std::vector<contracts::Json>
  list_objects(std::string_view table, std::string_view ref_column);

  void rebuild_projection();

private:
  void ensure_evidence(const std::vector<std::string> &evidence_ids,
                       std::string_view label) const;

  EgcfStore &egcf_store_;
  std::filesystem::path state_root_;
  std::filesystem::path root_;
  std::filesystem::path pattern_root_;
  std::filesystem::path occurrence_root_;
  std::filesystem::path benchmark_root_;
  std::filesystem::path snapshot_root_;
  std::filesystem::path trajectory_root_;
  std::filesystem::path opportunity_root_;
  std::filesystem::path schedule_root_;
  std::filesystem::path projection_path_;
};

} // namespace statewright::egcf
