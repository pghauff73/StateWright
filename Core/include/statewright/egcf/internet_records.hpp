#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace statewright::egcf {

inline constexpr std::string_view internet_candidate_records_version =
    "statewright-internet-candidate-records-v1";

struct InternetKnowledgeSearchReceipt final {
  int schema_version = 1;
  std::string snapshot_id;
  std::string source_fragment_id;
  std::string brain_feed_batch_id;
  std::string source_policy_assessment_id;
  contracts::Json canonical_search = contracts::Json::object();
  std::vector<std::string> exact_match_ids;
  std::vector<std::string> equivalent_match_ids;
  std::vector<std::string> related_match_ids;
  std::vector<std::string> transfer_match_ids;
  std::vector<std::string> adaptation_match_ids;
  std::vector<std::string> failure_match_ids;
  std::vector<std::string> exclusions;
  bool search_complete = false;
  std::string novelty_status;
  std::string search_signature;

  [[nodiscard]] std::string object_id() const;
};

struct InternetAlgorithmCandidate final {
  int schema_version = 1;
  std::string source_fragment_id;
  std::string snapshot_id;
  std::string source_policy_assessment_id;
  contracts::Json proposed_saa_ir = contracts::Json::object();
  std::vector<std::string> semantic_inputs;
  std::vector<std::string> semantic_outputs;
  contracts::Json units = contracts::Json::object();
  contracts::Json applicability = contracts::Json::object();
  std::vector<std::string> claimed_invariants;
  contracts::Json termination_properties = contracts::Json::object();
  std::string retrieval_receipt_id;
  std::vector<std::string> exact_match_ids;
  std::vector<std::string> equivalent_match_ids;
  std::vector<std::string> related_match_ids;
  std::vector<std::string> transfer_match_ids;
  std::vector<std::string> failure_match_ids;
  std::vector<std::string> oiec_sr_proposal_ids;
  std::vector<std::string> oiec_sr_falsifier_ids;
  std::vector<std::string> reasoning_analysis_ids;
  std::vector<std::string> experiment_qualification_ids;
  std::vector<std::string> promotion_assessment_ids;
  std::vector<std::string> probation_admission_ids;
  std::vector<std::string> probation_observation_ids;
  std::vector<std::string> promotion_decision_ids;
  std::vector<std::string> demotion_decision_ids;
  std::vector<std::string> canonical_algorithm_ids;
  std::vector<std::string> unresolved_assumptions;
  std::string status;
  std::string candidate_signature;

  [[nodiscard]] std::string object_id() const;
};

struct InternetReasoningAnalysis final {
  int schema_version = 1;
  std::string candidate_id;
  std::vector<std::string> snapshot_ids;
  std::vector<std::string> source_fragment_ids;
  contracts::Json request = contracts::Json::object();
  std::string request_signature;
  std::string provider_identity;
  std::string model_identity;
  std::string grammar_identity;
  std::string parser_version;
  bool provider_available = false;
  std::string provider_output_signature;
  contracts::Json hypothesis_set = contracts::Json::object();
  std::vector<std::string> proposal_ids;
  std::vector<std::string> falsifier_ids;
  std::vector<std::string> missing_evidence_ids;
  std::vector<std::string> unresolved_assumptions;
  bool authoritative = false;
  std::string status;
  std::string analysis_signature;

  [[nodiscard]] std::string object_id() const;
};

struct InternetExperimentQualification final {
  int schema_version = 1;
  std::string candidate_id;
  std::string baseline_ref;
  std::vector<std::string> dataset_snapshot_ids;
  std::string context_signature;
  contracts::Json canonical_candidate_ir = contracts::Json::object();
  contracts::Json canonical_baseline_ir = contracts::Json::object();
  contracts::Json experiment_design = contracts::Json::object();
  contracts::Json experiment_runs = contracts::Json::array();
  contracts::Json repeated_aggregate = contracts::Json::object();
  contracts::Json benchmark_profile = contracts::Json::object();
  contracts::Json benchmark_gate = contracts::Json::object();
  contracts::Json integrity_snapshots = contracts::Json::array();
  contracts::Json integrity_trajectory = contracts::Json::object();
  std::vector<std::string> evidence_ids;
  std::vector<std::string> failure_observation_ids;
  std::vector<std::string> improvement_opportunity_ids;
  contracts::Json improvement_schedule = contracts::Json::object();
  std::vector<std::string> blocking_reasons;
  bool internal_ir_only = true;
  bool downloaded_code_executed = false;
  bool identical_frozen_contexts = false;
  bool invariants_passed = false;
  bool known_failure_retry_blocked = false;
  bool experiment_qualified = false;
  bool benchmark_passed = false;
  bool integrity_passed = false;
  std::string status;
  std::string qualification_signature;

  [[nodiscard]] std::string object_id() const;
};

[[nodiscard]] InternetKnowledgeSearchReceipt
canonical_knowledge_search_receipt(InternetKnowledgeSearchReceipt receipt);
[[nodiscard]] InternetKnowledgeSearchReceipt
internet_knowledge_search_receipt_from_json(const contracts::Json &value);
[[nodiscard]] InternetAlgorithmCandidate
canonical_internet_algorithm_candidate(InternetAlgorithmCandidate candidate);
[[nodiscard]] InternetAlgorithmCandidate
internet_algorithm_candidate_from_json(const contracts::Json &value);
[[nodiscard]] InternetReasoningAnalysis
canonical_internet_reasoning_analysis(InternetReasoningAnalysis analysis);
[[nodiscard]] InternetExperimentQualification
canonical_internet_experiment_qualification(
    InternetExperimentQualification qualification);
[[nodiscard]] contracts::Json
to_json(const InternetKnowledgeSearchReceipt &value);
[[nodiscard]] contracts::Json to_json(const InternetAlgorithmCandidate &value);
[[nodiscard]] contracts::Json to_json(const InternetReasoningAnalysis &value);
[[nodiscard]] contracts::Json
to_json(const InternetExperimentQualification &value);

} // namespace statewright::egcf
