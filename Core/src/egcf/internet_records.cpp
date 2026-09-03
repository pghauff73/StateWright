#include "statewright/egcf/internet_records.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/contracts/typed_id.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace statewright::egcf {
namespace {

[[noreturn]] void record_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

void require_nonempty(std::string_view value, std::string_view label) {
  if (value.empty()) {
    record_error(std::string(label) + " must not be empty");
  }
}

void canonical_strings(std::vector<std::string> &values) {
  for (const auto &value : values) {
    require_nonempty(value, "candidate record value");
  }
  std::ranges::sort(values);
  values.erase(std::unique(values.begin(), values.end()), values.end());
}

template <typename Value>
std::string signature_for(const Value &value, std::string_view key) {
  auto material = to_json(value);
  material.erase(std::string(key));
  return contracts::sha256_json(material);
}

} // namespace

std::string InternetKnowledgeSearchReceipt::object_id() const {
  return contracts::typed_id("internet-retrieval-receipt", to_json(*this));
}

std::string InternetAlgorithmCandidate::object_id() const {
  return contracts::typed_id("internet-algorithm-candidate", to_json(*this));
}

std::string InternetReasoningAnalysis::object_id() const {
  return contracts::typed_id("internet-reasoning-analysis", to_json(*this));
}

std::string InternetExperimentQualification::object_id() const {
  return contracts::typed_id("internet-experiment-qualification",
                             to_json(*this));
}

InternetKnowledgeSearchReceipt canonical_knowledge_search_receipt(
    InternetKnowledgeSearchReceipt receipt) {
  require_nonempty(receipt.snapshot_id, "retrieval snapshot ID");
  require_nonempty(receipt.source_fragment_id, "retrieval fragment ID");
  require_nonempty(receipt.brain_feed_batch_id, "retrieval brain-feed batch ID");
  require_nonempty(receipt.novelty_status, "retrieval novelty status");
  if (!receipt.canonical_search.is_object()) {
    record_error("canonical search receipt must be an object");
  }
  static const std::set<std::string> statuses = {
      "DUPLICATE", "EQUIVALENT_EXISTING", "RELATED_EXISTING",
      "TRANSFER_CANDIDATE", "ADAPTATION_CANDIDATE", "NOVEL_CANDIDATE",
      "QUARANTINED"};
  if (!statuses.contains(receipt.novelty_status) || !receipt.search_complete) {
    record_error("internet retrieval receipt is incomplete");
  }
  canonical_strings(receipt.exact_match_ids);
  canonical_strings(receipt.equivalent_match_ids);
  canonical_strings(receipt.related_match_ids);
  canonical_strings(receipt.transfer_match_ids);
  canonical_strings(receipt.adaptation_match_ids);
  canonical_strings(receipt.failure_match_ids);
  canonical_strings(receipt.exclusions);
  receipt.search_signature = signature_for(receipt, "search_signature");
  return receipt;
}

InternetAlgorithmCandidate
canonical_internet_algorithm_candidate(InternetAlgorithmCandidate candidate) {
  require_nonempty(candidate.source_fragment_id, "candidate fragment ID");
  require_nonempty(candidate.snapshot_id, "candidate snapshot ID");
  require_nonempty(candidate.source_policy_assessment_id,
                   "candidate source policy assessment ID");
  require_nonempty(candidate.retrieval_receipt_id,
                   "candidate retrieval receipt ID");
  require_nonempty(candidate.status, "candidate status");
  if (!candidate.proposed_saa_ir.is_object() || !candidate.units.is_object() ||
      !candidate.applicability.is_object() ||
      !candidate.termination_properties.is_object()) {
    record_error("internet candidate structured fields must be objects");
  }
  static const std::set<std::string> statuses = {
      "DUPLICATE", "EQUIVALENT_EXISTING", "RELATED_EXISTING",
      "TRANSFER_CANDIDATE", "ADAPTATION_CANDIDATE", "VALIDATION_READY",
      "EXPERIMENT_QUALIFIED", "EXPERIMENT_FAILED", "POLICY_QUALIFIED",
      "PROBATIONARY_CANONICAL", "CANONICAL", "DEMOTED",
      "QUARANTINED", "REJECTED", "RETRACTED", "SUPERSEDED"};
  if (!statuses.contains(candidate.status)) {
    record_error("internet candidate status is invalid");
  }
  canonical_strings(candidate.semantic_inputs);
  canonical_strings(candidate.semantic_outputs);
  canonical_strings(candidate.claimed_invariants);
  canonical_strings(candidate.exact_match_ids);
  canonical_strings(candidate.equivalent_match_ids);
  canonical_strings(candidate.related_match_ids);
  canonical_strings(candidate.transfer_match_ids);
  canonical_strings(candidate.failure_match_ids);
  canonical_strings(candidate.oiec_sr_proposal_ids);
  canonical_strings(candidate.oiec_sr_falsifier_ids);
  canonical_strings(candidate.experiment_qualification_ids);
  canonical_strings(candidate.promotion_assessment_ids);
  canonical_strings(candidate.probation_admission_ids);
  canonical_strings(candidate.probation_observation_ids);
  canonical_strings(candidate.promotion_decision_ids);
  canonical_strings(candidate.demotion_decision_ids);
  canonical_strings(candidate.canonical_algorithm_ids);
  canonical_strings(candidate.unresolved_assumptions);
  candidate.candidate_signature =
      signature_for(candidate, "candidate_signature");
  return candidate;
}

InternetAlgorithmCandidate
internet_algorithm_candidate_from_json(const contracts::Json &value) {
  const bool legacy_lineage =
      !value.contains("probation_admission_ids") ||
      !value.contains("probation_observation_ids") ||
      !value.contains("promotion_decision_ids") ||
      !value.contains("demotion_decision_ids") ||
      !value.contains("canonical_algorithm_ids");
  InternetAlgorithmCandidate candidate{
      .schema_version = value.at("schema_version").get<int>(),
      .source_fragment_id =
          value.at("source_fragment_id").get<std::string>(),
      .snapshot_id = value.at("snapshot_id").get<std::string>(),
      .source_policy_assessment_id =
          value.at("source_policy_assessment_id").get<std::string>(),
      .proposed_saa_ir = value.at("proposed_saa_ir"),
      .semantic_inputs =
          value.at("semantic_inputs").get<std::vector<std::string>>(),
      .semantic_outputs =
          value.at("semantic_outputs").get<std::vector<std::string>>(),
      .units = value.at("units"),
      .applicability = value.at("applicability"),
      .claimed_invariants =
          value.at("claimed_invariants").get<std::vector<std::string>>(),
      .termination_properties = value.at("termination_properties"),
      .retrieval_receipt_id =
          value.at("retrieval_receipt_id").get<std::string>(),
      .exact_match_ids =
          value.at("exact_match_ids").get<std::vector<std::string>>(),
      .equivalent_match_ids =
          value.at("equivalent_match_ids").get<std::vector<std::string>>(),
      .related_match_ids =
          value.at("related_match_ids").get<std::vector<std::string>>(),
      .transfer_match_ids =
          value.at("transfer_match_ids").get<std::vector<std::string>>(),
      .failure_match_ids =
          value.at("failure_match_ids").get<std::vector<std::string>>(),
      .oiec_sr_proposal_ids =
          value.at("oiec_sr_proposal_ids").get<std::vector<std::string>>(),
      .oiec_sr_falsifier_ids =
          value.at("oiec_sr_falsifier_ids").get<std::vector<std::string>>(),
      .experiment_qualification_ids =
          value.at("experiment_qualification_ids")
              .get<std::vector<std::string>>(),
      .promotion_assessment_ids =
          value.at("promotion_assessment_ids")
              .get<std::vector<std::string>>(),
      .probation_admission_ids =
          value.value("probation_admission_ids", std::vector<std::string>{}),
      .probation_observation_ids =
          value.value("probation_observation_ids", std::vector<std::string>{}),
      .promotion_decision_ids =
          value.value("promotion_decision_ids", std::vector<std::string>{}),
      .demotion_decision_ids =
          value.value("demotion_decision_ids", std::vector<std::string>{}),
      .canonical_algorithm_ids =
          value.value("canonical_algorithm_ids", std::vector<std::string>{}),
      .unresolved_assumptions =
          value.at("unresolved_assumptions")
              .get<std::vector<std::string>>(),
      .status = value.at("status").get<std::string>(),
      .candidate_signature =
          value.at("candidate_signature").get<std::string>()};
  if (legacy_lineage) {
    auto legacy_material = value;
    legacy_material.erase("candidate_signature");
    if (candidate.candidate_signature !=
        contracts::sha256_json(legacy_material)) {
      record_error("persisted legacy internet candidate signature is invalid");
    }
    candidate.candidate_signature.clear();
    return canonical_internet_algorithm_candidate(std::move(candidate));
  }
  const auto canonical =
      canonical_internet_algorithm_candidate(std::move(candidate));
  if (to_json(canonical) != value) {
    record_error("persisted internet algorithm candidate is invalid");
  }
  return canonical;
}

InternetReasoningAnalysis
canonical_internet_reasoning_analysis(InternetReasoningAnalysis analysis) {
  require_nonempty(analysis.candidate_id, "reasoning candidate ID");
  require_nonempty(analysis.request_signature, "reasoning request signature");
  require_nonempty(analysis.provider_identity, "reasoning provider identity");
  require_nonempty(analysis.model_identity, "reasoning model identity");
  require_nonempty(analysis.grammar_identity, "reasoning grammar identity");
  require_nonempty(analysis.parser_version, "reasoning parser version");
  require_nonempty(analysis.provider_output_signature,
                   "reasoning provider output signature");
  require_nonempty(analysis.status, "reasoning analysis status");
  if (!analysis.request.is_object() || !analysis.hypothesis_set.is_object() ||
      analysis.authoritative || analysis.snapshot_ids.empty() ||
      analysis.source_fragment_ids.empty()) {
    record_error("internet reasoning analysis is invalid");
  }
  static const std::set<std::string> statuses = {
      "PROVIDER_ADVISORY", "DETERMINISTIC_FALLBACK",
      "PROVIDER_FAILED_FALLBACK"};
  if (!statuses.contains(analysis.status)) {
    record_error("internet reasoning analysis status is invalid");
  }
  canonical_strings(analysis.snapshot_ids);
  canonical_strings(analysis.source_fragment_ids);
  canonical_strings(analysis.proposal_ids);
  canonical_strings(analysis.falsifier_ids);
  canonical_strings(analysis.missing_evidence_ids);
  canonical_strings(analysis.unresolved_assumptions);
  analysis.analysis_signature = signature_for(analysis, "analysis_signature");
  return analysis;
}

InternetExperimentQualification canonical_internet_experiment_qualification(
    InternetExperimentQualification qualification) {
  require_nonempty(qualification.candidate_id,
                   "experiment qualification candidate ID");
  require_nonempty(qualification.baseline_ref,
                   "experiment qualification baseline ref");
  require_nonempty(qualification.context_signature,
                   "experiment qualification context signature");
  require_nonempty(qualification.status,
                   "experiment qualification status");
  if (qualification.context_signature.size() != 64U ||
      qualification.dataset_snapshot_ids.empty() ||
      !qualification.canonical_candidate_ir.is_object() ||
      !qualification.canonical_baseline_ir.is_object() ||
      !qualification.experiment_design.is_object() ||
      !qualification.experiment_runs.is_array() ||
      !qualification.repeated_aggregate.is_object() ||
      !qualification.benchmark_profile.is_object() ||
      !qualification.benchmark_gate.is_object() ||
      !qualification.integrity_snapshots.is_array() ||
      !qualification.integrity_trajectory.is_object() ||
      !qualification.improvement_schedule.is_object() ||
      !qualification.internal_ir_only ||
      qualification.downloaded_code_executed) {
    record_error("internet experiment qualification is invalid");
  }
  static const std::set<std::string> statuses = {
      "EXPERIMENT_QUALIFIED", "EXPERIMENT_FAILED"};
  if (!statuses.contains(qualification.status)) {
    record_error("internet experiment qualification status is invalid");
  }
  const bool qualified = qualification.status == "EXPERIMENT_QUALIFIED";
  if (qualification.experiment_qualified != qualified ||
      (qualified && (!qualification.identical_frozen_contexts ||
                     !qualification.invariants_passed ||
                     qualification.known_failure_retry_blocked ||
                     !qualification.benchmark_passed ||
                     !qualification.integrity_passed ||
                     !qualification.blocking_reasons.empty()))) {
    record_error("internet experiment qualification state is inconsistent");
  }
  canonical_strings(qualification.dataset_snapshot_ids);
  canonical_strings(qualification.evidence_ids);
  canonical_strings(qualification.failure_observation_ids);
  canonical_strings(qualification.improvement_opportunity_ids);
  canonical_strings(qualification.blocking_reasons);
  qualification.qualification_signature =
      signature_for(qualification, "qualification_signature");
  return qualification;
}

contracts::Json to_json(const InternetKnowledgeSearchReceipt &value) {
  return {{"adaptation_match_ids", value.adaptation_match_ids},
          {"brain_feed_batch_id", value.brain_feed_batch_id},
          {"canonical_search", value.canonical_search},
          {"equivalent_match_ids", value.equivalent_match_ids},
          {"exact_match_ids", value.exact_match_ids},
          {"exclusions", value.exclusions},
          {"failure_match_ids", value.failure_match_ids},
          {"novelty_status", value.novelty_status},
          {"related_match_ids", value.related_match_ids},
          {"schema_version", value.schema_version},
          {"search_complete", value.search_complete},
          {"search_signature", value.search_signature},
          {"snapshot_id", value.snapshot_id},
          {"source_fragment_id", value.source_fragment_id},
          {"transfer_match_ids", value.transfer_match_ids}};
}

contracts::Json to_json(const InternetAlgorithmCandidate &value) {
  return {{"applicability", value.applicability},
          {"candidate_signature", value.candidate_signature},
          {"claimed_invariants", value.claimed_invariants},
          {"equivalent_match_ids", value.equivalent_match_ids},
          {"exact_match_ids", value.exact_match_ids},
          {"experiment_qualification_ids",
           value.experiment_qualification_ids},
          {"failure_match_ids", value.failure_match_ids},
          {"canonical_algorithm_ids", value.canonical_algorithm_ids},
          {"demotion_decision_ids", value.demotion_decision_ids},
          {"oiec_sr_falsifier_ids", value.oiec_sr_falsifier_ids},
          {"oiec_sr_proposal_ids", value.oiec_sr_proposal_ids},
          {"proposed_saa_ir", value.proposed_saa_ir},
          {"promotion_assessment_ids", value.promotion_assessment_ids},
          {"promotion_decision_ids", value.promotion_decision_ids},
          {"probation_admission_ids", value.probation_admission_ids},
          {"probation_observation_ids", value.probation_observation_ids},
          {"related_match_ids", value.related_match_ids},
          {"retrieval_receipt_id", value.retrieval_receipt_id},
          {"schema_version", value.schema_version},
          {"semantic_inputs", value.semantic_inputs},
          {"semantic_outputs", value.semantic_outputs},
          {"snapshot_id", value.snapshot_id},
          {"source_fragment_id", value.source_fragment_id},
          {"source_policy_assessment_id",
           value.source_policy_assessment_id},
          {"status", value.status},
          {"termination_properties", value.termination_properties},
          {"transfer_match_ids", value.transfer_match_ids},
          {"units", value.units},
          {"unresolved_assumptions", value.unresolved_assumptions}};
}

contracts::Json to_json(const InternetReasoningAnalysis &value) {
  return {{"analysis_signature", value.analysis_signature},
          {"authoritative", value.authoritative},
          {"candidate_id", value.candidate_id},
          {"falsifier_ids", value.falsifier_ids},
          {"grammar_identity", value.grammar_identity},
          {"hypothesis_set", value.hypothesis_set},
          {"missing_evidence_ids", value.missing_evidence_ids},
          {"model_identity", value.model_identity},
          {"parser_version", value.parser_version},
          {"proposal_ids", value.proposal_ids},
          {"provider_available", value.provider_available},
          {"provider_identity", value.provider_identity},
          {"provider_output_signature", value.provider_output_signature},
          {"request", value.request},
          {"request_signature", value.request_signature},
          {"schema_version", value.schema_version},
          {"snapshot_ids", value.snapshot_ids},
          {"source_fragment_ids", value.source_fragment_ids},
          {"status", value.status},
          {"unresolved_assumptions", value.unresolved_assumptions}};
}

contracts::Json to_json(const InternetExperimentQualification &value) {
  return {{"baseline_ref", value.baseline_ref},
          {"benchmark_gate", value.benchmark_gate},
          {"benchmark_passed", value.benchmark_passed},
          {"benchmark_profile", value.benchmark_profile},
          {"blocking_reasons", value.blocking_reasons},
          {"candidate_id", value.candidate_id},
          {"canonical_baseline_ir", value.canonical_baseline_ir},
          {"canonical_candidate_ir", value.canonical_candidate_ir},
          {"context_signature", value.context_signature},
          {"dataset_snapshot_ids", value.dataset_snapshot_ids},
          {"downloaded_code_executed", value.downloaded_code_executed},
          {"evidence_ids", value.evidence_ids},
          {"experiment_design", value.experiment_design},
          {"experiment_qualified", value.experiment_qualified},
          {"experiment_runs", value.experiment_runs},
          {"failure_observation_ids", value.failure_observation_ids},
          {"identical_frozen_contexts", value.identical_frozen_contexts},
          {"improvement_opportunity_ids",
           value.improvement_opportunity_ids},
          {"improvement_schedule", value.improvement_schedule},
          {"integrity_passed", value.integrity_passed},
          {"integrity_snapshots", value.integrity_snapshots},
          {"integrity_trajectory", value.integrity_trajectory},
          {"internal_ir_only", value.internal_ir_only},
          {"invariants_passed", value.invariants_passed},
          {"known_failure_retry_blocked",
           value.known_failure_retry_blocked},
          {"qualification_signature", value.qualification_signature},
          {"repeated_aggregate", value.repeated_aggregate},
          {"schema_version", value.schema_version},
          {"status", value.status}};
}

} // namespace statewright::egcf
