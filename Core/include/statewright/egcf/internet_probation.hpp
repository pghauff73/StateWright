#pragma once

#include "statewright/egcf/canonical_algorithm_store.hpp"
#include "statewright/egcf/internet_improvement_store.hpp"
#include "statewright/egcf/knowledge_governance_store.hpp"
#include "statewright/egcf/internet_polynomial_canonical.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::egcf {

inline constexpr std::string_view internet_probation_controller_version =
    "statewright-internet-probation-controller-v1";

struct InternetProbationAdmissionResult final {
  saa::ProbationPlan plan;
  CanonicalAdmissionResult canonical_admission;
  InternetAlgorithmCandidate updated_candidate;
  std::string admission_id;
  std::string updated_candidate_id;
  std::string result_signature;
  std::optional<InternetPolynomialCanonicalAdmission> polynomial_admission = std::nullopt;
};

struct InternetProbationSelection final {
  std::string admission_id;
  std::string plan_signature;
  std::string query_signature;
  std::string selected_canonical_ref;
  std::string baseline_ref;
  bool candidate_selected = false;
  std::string explanation;
  std::string selection_signature;
};

struct InternetProbationObservationRequest final {
  std::string query_signature;
  std::string context_signature;
  std::string observed_at;
  int window_index = 0;
  bool candidate_correct = false;
  bool baseline_correct = false;
  bool invariant_passed = false;
  bool benchmark_passed = false;
  bool integrity_passed = false;
  bool source_valid = false;
  bool reproduction_passed = false;
  std::vector<std::string> evidence_ids;
  saa::ProbationRegressionSignals regression_signals;
};

struct InternetProbationObservationResult final {
  saa::ProbationObservation observation;
  saa::ProbationAssessment assessment;
  std::optional<saa::AutomaticPromotionDecision> promotion_decision;
  std::optional<saa::AutomaticDemotionDecision> demotion_decision;
  InternetAlgorithmCandidate updated_candidate;
  std::string observation_id;
  std::string promotion_decision_id;
  std::string demotion_decision_id;
  std::string failure_observation_ref;
  std::string reevaluation_schedule_ref;
  std::string updated_candidate_id;
  std::string result_signature;
};

class InternetProbationController final {
public:
  explicit InternetProbationController(EgcfStore &store);

  [[nodiscard]] InternetProbationAdmissionResult
  admit(const InternetAlgorithmCandidate &candidate,
        std::string previous_preferred_canonical_ref,
        std::string current_timestamp);
  [[nodiscard]] InternetProbationSelection
  select(const InternetAlgorithmCandidate &candidate,
         std::string query_signature);
  [[nodiscard]] InternetProbationObservationResult
  observe(const InternetAlgorithmCandidate &candidate,
          InternetProbationObservationRequest request);
  // No fallback execution, measurement receipts or qualification claims.
  [[nodiscard]] std::optional<mpq_class> execute_selected_polynomial(
      const InternetAlgorithmCandidate &candidate, std::string query_signature,
      const mpq_class &input, std::string current_timestamp);

private:
  EgcfStore &store_;
  InternetImprovementStore internet_;
  CanonicalAlgorithmStore canonical_;
  KnowledgeGovernanceStore governance_;
};

[[nodiscard]] contracts::Json
to_json(const InternetProbationAdmissionResult &value);
[[nodiscard]] contracts::Json to_json(const InternetProbationSelection &value);
[[nodiscard]] contracts::Json
to_json(const InternetProbationObservationRequest &value);
[[nodiscard]] contracts::Json
to_json(const InternetProbationObservationResult &value);

} // namespace statewright::egcf
