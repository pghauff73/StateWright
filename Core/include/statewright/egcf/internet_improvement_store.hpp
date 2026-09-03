#pragma once

#include "statewright/egcf/store.hpp"
#include "statewright/egcf/internet_records.hpp"
#include "statewright/saa/autonomous_promotion_policy.hpp"
#include "statewright/saa/probation.hpp"
#include "statewright/sources/extraction.hpp"
#include "statewright/sources/records.hpp"
#include "statewright/sources/snapshot.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::egcf {

inline constexpr std::string_view internet_improvement_store_version =
    "statewright-internet-improvement-store-v1";

struct InternetCaptureResult final {
  std::string artifact_record_id;
  std::string artifact_bytes_id;
  std::string snapshot_id;
  std::string fetch_receipt_id;
};

class InternetImprovementStore final {
public:
  explicit InternetImprovementStore(EgcfStore &store);

  [[nodiscard]] std::string
  register_source_policy(const sources::InternetSourcePolicy &policy);
  [[nodiscard]] std::string register_watch(const sources::InternetWatch &watch);
  [[nodiscard]] std::string
  register_fetch_job(const sources::InternetFetchJob &job);
  [[nodiscard]] std::string
  register_fetch_lease(const sources::InternetFetchLease &lease);
  [[nodiscard]] InternetCaptureResult capture_success(
      std::string job_id, std::string lease_id,
      const sources::FetchResponse &response, std::string source_group);
  [[nodiscard]] std::string capture_not_modified(
      std::string job_id, std::string lease_id,
      const sources::FetchResponse &response, std::string snapshot_id);
  [[nodiscard]] std::string capture_failure(
      std::string job_id, std::string lease_id, std::string requested_url,
      std::string provider_identity, std::string reason);
  [[nodiscard]] std::string register_policy_assessment(
      const sources::InternetPolicyAssessment &assessment);
  [[nodiscard]] std::string register_source_fragment(
      const sources::InternetSourceFragment &fragment);
  [[nodiscard]] std::string register_extraction_receipt(
      const sources::InternetExtractionReceipt &receipt);
  [[nodiscard]] std::string register_extraction(
      const sources::InternetExtractionResult &extraction);
  [[nodiscard]] std::string register_retrieval_receipt(
      const InternetKnowledgeSearchReceipt &receipt);
  [[nodiscard]] std::string register_algorithm_candidate(
      const InternetAlgorithmCandidate &candidate);
  [[nodiscard]] std::string register_reasoning_analysis(
      const InternetReasoningAnalysis &analysis);
  [[nodiscard]] std::string register_experiment_qualification(
      const InternetExperimentQualification &qualification);
  [[nodiscard]] std::string register_promotion_policy(
      const saa::AutonomousPromotionPolicy &policy);
  [[nodiscard]] std::string register_promotion_assessment(
      std::string policy_id,
      const saa::AutonomousPromotionAssessment &assessment);
  [[nodiscard]] std::string register_probation_admission(
      const saa::ProbationPlan &plan, std::string canonical_algorithm_ref,
      std::string canonical_source_ref, std::string baseline_ref,
      int canonical_store_generation, std::string admission_status);
  [[nodiscard]] std::string register_probation_observation(
      std::string admission_id,
      const saa::ProbationObservation &observation);
  [[nodiscard]] std::string register_promotion_decision(
      std::string admission_id,
      const saa::AutomaticPromotionDecision &decision);
  [[nodiscard]] std::string register_demotion_decision(
      std::string admission_id,
      const saa::AutomaticDemotionDecision &decision);
  [[nodiscard]] std::string supersede_algorithm_candidate(
      std::string old_candidate_id,
      const InternetAlgorithmCandidate &replacement, std::string reason);
  [[nodiscard]] std::string
  migrate_algorithm_candidate(std::string candidate_id);

  [[nodiscard]] std::vector<StoredObject>
  list(std::string_view object_type);
  [[nodiscard]] std::vector<std::string> active_watch_ids();
  [[nodiscard]] std::optional<sources::InternetFetchLease>
  latest_lease(std::string_view job_id);
  [[nodiscard]] std::vector<std::byte>
  snapshot_bytes(std::string_view snapshot_id) const;

  void verify_integrity();
  void rebuild_projection();

private:
  void require_type(std::string_view object_id,
                    std::string_view object_type) const;

  EgcfStore &store_;
};

[[nodiscard]] contracts::Json to_json(const InternetCaptureResult &value);

} // namespace statewright::egcf
