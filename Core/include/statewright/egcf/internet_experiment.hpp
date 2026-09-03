#pragma once

#include "statewright/egcf/internet_improvement_store.hpp"
#include "statewright/egcf/knowledge_governance_store.hpp"
#include "statewright/saa/algorithm_experiment.hpp"
#include "statewright/saa/algorithm_ir.hpp"
#include "statewright/saa/experiment_aggregation.hpp"

#include <gmpxx.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::egcf {

inline constexpr std::string_view internet_experiment_coordinator_version =
    "statewright-internet-experiment-coordinator-v1";

struct InternetScalarTrialGroup final {
  std::string independence_group;
  std::string baseline_context_signature;
  std::string candidate_context_signature;
  int deterministic_seed = 0;
  std::vector<mpq_class> inputs;
  std::vector<mpq_class> expected_outputs;
};

struct InternetExperimentRequest final {
  std::string baseline_ref;
  contracts::Json baseline_saa_ir = contracts::Json::object();
  std::string context_signature;
  std::vector<std::string> dataset_snapshot_ids;
  std::vector<InternetScalarTrialGroup> trial_groups;
  mpq_class minimum_material_effect{0};
  mpq_class minimum_output{-1000000};
  mpq_class maximum_output{1000000};
  int minimum_trials_per_group = 1;
  int minimum_experiments = 2;
  int minimum_independence_groups = 2;
  int maximum_total_trials = 10000;
  std::vector<std::pair<std::string, int>> benchmark_track_scores;
  saa::OIECBenchGatePolicy benchmark_policy;
  std::vector<saa::KnowledgeIntegritySnapshot> integrity_snapshots;
  saa::KnowledgeIntegrityPolicy integrity_policy;
  bool independent_review = true;
  std::string recorded_at;
};

struct InternetExperimentResult final {
  InternetExperimentQualification qualification;
  InternetAlgorithmCandidate updated_candidate;
  std::string qualification_id;
  std::string updated_candidate_id;
  std::string benchmark_gate_ref;
  std::string integrity_trajectory_ref;
  std::string improvement_schedule_ref;
  std::string result_signature;
};

class InternetExperimentCoordinator final {
public:
  explicit InternetExperimentCoordinator(EgcfStore &store);

  [[nodiscard]] InternetExperimentResult qualify(
      const InternetAlgorithmCandidate &candidate,
      InternetExperimentRequest request);

private:
  EgcfStore &store_;
  InternetImprovementStore internet_;
  KnowledgeGovernanceStore governance_;
};

[[nodiscard]] std::string internet_experiment_context_signature(
    std::vector<std::string> dataset_snapshot_ids,
    std::vector<InternetScalarTrialGroup> trial_groups);
[[nodiscard]] contracts::Json to_json(const InternetScalarTrialGroup &value);
[[nodiscard]] contracts::Json to_json(const InternetExperimentResult &value);

} // namespace statewright::egcf
