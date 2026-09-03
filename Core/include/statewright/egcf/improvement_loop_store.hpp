#pragma once

#include "statewright/egcf/adaptation_lineage_store.hpp"
#include "statewright/egcf/store.hpp"
#include "statewright/saa/experiment_aggregation.hpp"
#include "statewright/saa/intelligence_loop.hpp"
#include "statewright/saa/multistep_evolution.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace statewright::egcf {

inline constexpr std::string_view improvement_loop_store_version =
    "saa-improvement-ledger-v1";
inline constexpr int improvement_loop_store_schema_version = 1;

class ImprovementLoopStore final {
public:
  ImprovementLoopStore(EgcfStore &egcf_store,
                       AdaptationLineageStore &adaptation_store);

  [[nodiscard]] const std::filesystem::path &root() const noexcept;
  [[nodiscard]] saa::ReasoningEvidenceResolver evidence_resolver() const;
  [[nodiscard]] std::string
  register_evolution_plan(const saa::MultiStepEvolutionPlan &plan);
  [[nodiscard]] std::string register_step_qualification(
      const saa::EvolutionStepQualification &qualification);
  [[nodiscard]] std::string register_evolution_assessment(
      const saa::MultiStepEvolutionAssessment &assessment);
  [[nodiscard]] std::string register_experiment_aggregate(
      const saa::RepeatedExperimentAggregate &aggregate);
  [[nodiscard]] std::string register_loop_decision(
      const saa::IntelligenceImprovementDecision &decision);
  [[nodiscard]] std::vector<contracts::Json> decisions() const;
  [[nodiscard]] std::vector<contracts::Json> aggregates() const;

  void rebuild_projection();

private:
  [[nodiscard]] contracts::Json
  plan_payload(std::string_view plan_signature) const;
  void verify_evidence(const std::vector<std::string> &evidence_ids) const;

  EgcfStore &egcf_store_;
  AdaptationLineageStore &adaptation_store_;
  std::filesystem::path state_root_;
  std::filesystem::path root_;
  std::filesystem::path plan_root_;
  std::filesystem::path qualification_root_;
  std::filesystem::path assessment_root_;
  std::filesystem::path aggregate_root_;
  std::filesystem::path decision_root_;
  std::filesystem::path projection_path_;
};

} // namespace statewright::egcf
