#pragma once

#include "statewright/common/error.hpp"
#include "statewright/egcf/evidence.hpp"
#include "statewright/egcf/internet_improvement_store.hpp"
#include "statewright/egcf/internet_polynomial_fungrim_context.hpp"

namespace statewright::egcf {

inline void polynomial_candidate_require(bool condition, const char *reason) {
  if (!condition)
    throw common::Error(common::ErrorCode::invalid_argument, reason);
}

// One source translator serves registration and subsequent faithful-source
// verification. This is not mathematical review or a qualification verdict.
[[nodiscard]] inline InternetAlgorithmCandidate fungrim_quadratic_candidate_template(
    const sources::InternetSourceFragment &fragment) {
  const auto proposal = preview_fungrim_chebyshev_quadratic_translation(
      sources::to_json(fragment));
  polynomial_candidate_require(proposal.has_value(),
      "POLYNOMIAL_CANDIDATE_SOURCE_CONTEXT_UNSUPPORTED");
  polynomial_candidate_require(
      proposal->at("source_context").at("coefficients") ==
          contracts::Json::array({"-1", "0", "2"}),
      "POLYNOMIAL_CANDIDATE_REQUIRES_CHEBYSHEV_T2");
  InternetAlgorithmCandidate candidate;
  candidate.source_fragment_id = fragment.object_id();
  candidate.snapshot_id = fragment.snapshot_id;
  candidate.proposed_saa_ir = proposal->at("proposed_saa_ir");
  candidate.semantic_inputs = {"x"};
  candidate.semantic_outputs = {"y"};
  candidate.units = {{"x", "dimensionless"}, {"y", "dimensionless"}};
  candidate.applicability = {{"translation", {
      {"translator_version", "fungrim-chebyshev-quadratic-candidate-v1"},
      {"source_fragment_id", fragment.object_id()},
      {"snapshot_id", fragment.snapshot_id},
      {"source_context", proposal->at("source_context")},
      {"candidate_ir_sha256", proposal->at("candidate_ir_sha256")},
      {"execution_contract", proposal->at("execution_contract")},
      {"scope_restrictions", proposal->at("scope_restrictions")}}}};
  candidate.claimed_invariants = {
      "output equals 2*x^2 - 1 on the declared exact-rational domain"};
  candidate.termination_properties = {
      {"terminates", true}, {"maximum_degree", 2},
      {"method", "bounded exact-rational Horner evaluation"},
      {"execution_contract", proposal->at("execution_contract")}};
  candidate.unresolved_assumptions = {
      "DOMAIN_BRANCH_AND_ERROR_BOUNDS_NOT_QUALIFIED",
      "MATHEMATICAL_CONTEXT_REVIEW_REQUIRED"};
  candidate.status = "QUARANTINED";
  return candidate;
}

inline void verify_fungrim_quadratic_candidate_translation(
    const InternetAlgorithmCandidate &candidate,
    const sources::InternetSourceFragment &fragment) {
  const auto expected = fungrim_quadratic_candidate_template(fragment);
  polynomial_candidate_require(
      candidate.source_fragment_id == expected.source_fragment_id &&
      candidate.snapshot_id == expected.snapshot_id &&
      candidate.proposed_saa_ir == expected.proposed_saa_ir &&
      candidate.semantic_inputs == expected.semantic_inputs &&
      candidate.semantic_outputs == expected.semantic_outputs &&
      candidate.units == expected.units &&
      candidate.claimed_invariants == expected.claimed_invariants &&
      candidate.termination_properties == expected.termination_properties &&
      candidate.applicability.at("translation") == expected.applicability.at("translation"),
      "POLYNOMIAL_CANDIDATE_SOURCE_TRANSLATION_MISMATCH");
}

// Read-only preparation: callers cannot supply coefficients, executable IR,
// reviews, status, or a synthetic retrieval receipt through this interface.
[[nodiscard]] inline InternetAlgorithmCandidate prepare_fungrim_quadratic_candidate(
    EgcfStore &store, const std::string &proposal_id,
    const std::string &retrieval_id) {
  polynomial_candidate_require(!proposal_id.empty() && proposal_id.size() <= 256U &&
      !retrieval_id.empty() && retrieval_id.size() <= 256U,
      "POLYNOMIAL_CANDIDATE_NATIVE_IDS_REQUIRED");
  const auto record = store.get(proposal_id);
  polynomial_candidate_require(record.object_type == "egcf-evidence",
      "POLYNOMIAL_CANDIDATE_NATIVE_PROPOSAL_REQUIRED");
  const auto evidence = evidence_artifact_from_json(record.payload);
  const auto &content = evidence.content;
  polynomial_candidate_require(!evidence.simulated && !evidence.success.has_value() &&
      evidence.category == "POLYNOMIAL_TRANSLATION_PROPOSAL" &&
      evidence.producer == "fungrim-chebyshev-quadratic-horner-proposal-v1" &&
      evidence.method == "SOURCE_BOUND_HORNER_TRANSLATION_PROPOSAL" &&
      evidence.sha256 == contracts::sha256_json(content) &&
      content.at("kind") == "POLYNOMIAL_SOURCE_TRANSLATION_PROPOSAL_V1" &&
      content.at("qualification_claim") == "NONE" &&
      content.at("fragment_id") == evidence.subject_id,
      "POLYNOMIAL_CANDIDATE_PROPOSAL_INVALID");
  const auto fragment_record = store.get(evidence.subject_id);
  const auto snapshot_id = content.at("snapshot_id").get<std::string>();
  const auto assessment_id = content.at("policy_assessment_id").get<std::string>();
  const auto snapshot = store.get(snapshot_id);
  const auto assessment_record = store.get(assessment_id);
  const auto retrieval_record = store.get(retrieval_id);
  polynomial_candidate_require(fragment_record.object_type == "internet-source-fragment" &&
      snapshot.object_type == "internet-source-snapshot" &&
      assessment_record.object_type == "internet-policy-assessment" &&
      retrieval_record.object_type == "internet-retrieval-receipt",
      "POLYNOMIAL_CANDIDATE_NATIVE_SOURCE_AND_RETRIEVAL_REQUIRED");
  const auto fragment = sources::internet_source_fragment_from_json(fragment_record.payload);
  const auto assessment = sources::internet_policy_assessment_from_json(assessment_record.payload);
  const auto retrieval = internet_knowledge_search_receipt_from_json(retrieval_record.payload);
  polynomial_candidate_require(fragment.object_id() == evidence.subject_id &&
      fragment.snapshot_id == snapshot_id && assessment.admissible() &&
      assessment.snapshot_id == snapshot_id && retrieval.search_complete &&
      retrieval.source_fragment_id == fragment.object_id() &&
      retrieval.snapshot_id == snapshot_id &&
      retrieval.source_policy_assessment_id == assessment_id &&
      store.get(retrieval.brain_feed_batch_id).object_type == "brain-feed-batch",
      "POLYNOMIAL_CANDIDATE_SOURCE_RETRIEVAL_BINDING_MISMATCH");
  const auto bound = bind_fungrim_quadratic_translation_to_snapshot(
      fragment_record.payload, snapshot.payload);
  polynomial_candidate_require(bound.has_value() && *bound == content.at("proposal") &&
      evidence.source_snapshot_hash == snapshot.payload.at("body_sha256").get<std::string>(),
      "POLYNOMIAL_CANDIDATE_SOURCE_BYTES_OR_CONTRACT_MISMATCH");
  const auto &notices = fragment_record.payload.at("metadata").at("source_review").at("license_notices");
  polynomial_candidate_require(notices.is_array() && !notices.empty() &&
      notices == content.at("source_notices"),
      "POLYNOMIAL_CANDIDATE_SOURCE_NOTICES_MISMATCH");
  // Fail closed on conflicting policy history. A new source review must be
  // resolved explicitly; an old admissible assessment is not permission to
  // override a later policy block. No linked material is authorized here.
  const auto assessments = store.search_text("\"" + snapshot_id + "\"",
      "internet-policy-assessment", 33U);
  polynomial_candidate_require(assessments.size() <= 32U,
      "POLYNOMIAL_CANDIDATE_POLICY_INSPECTION_BUDGET_EXHAUSTED");
  for (const auto &entry : assessments) {
    const auto other = sources::internet_policy_assessment_from_json(entry.payload);
    polynomial_candidate_require(other.snapshot_id != snapshot_id || other.admissible(),
        "POLYNOMIAL_CANDIDATE_CONFLICTING_SOURCE_ASSESSMENT");
  }
  auto candidate = fungrim_quadratic_candidate_template(fragment);
  candidate.source_policy_assessment_id = assessment_id;
  candidate.retrieval_receipt_id = retrieval_id;
  candidate.exact_match_ids = retrieval.exact_match_ids;
  candidate.equivalent_match_ids = retrieval.equivalent_match_ids;
  candidate.related_match_ids = retrieval.related_match_ids;
  candidate.transfer_match_ids = retrieval.transfer_match_ids;
  candidate.failure_match_ids = retrieval.failure_match_ids;
  candidate.applicability["source_group"] = snapshot.payload.at("source_group");
  candidate.applicability["polynomial_registration"] = {
      {"version", "fungrim-quadratic-candidate-registration-v1"},
      {"proposal_evidence_id", proposal_id}, {"proposal_sha256", evidence.sha256},
      {"source_body_sha256", evidence.source_snapshot_hash},
      {"retrieval_receipt_id", retrieval_id},
      {"retrieval_scope", "Historical source retrieval; not a fresh novelty claim"},
      {"historical_quarantine_preserved", true}};
  candidate = canonical_internet_algorithm_candidate(std::move(candidate));
  // A conflicting receipt must not create a second registration of the same
  // proposal. Successor lifecycle records retain the immutable registration
  // binding; they are not overwritten or counted as another function.
  const auto matches = store.search_text("\"" + proposal_id + "\"",
      "internet-algorithm-candidate", 33U);
  polynomial_candidate_require(matches.size() <= 32U,
      "POLYNOMIAL_CANDIDATE_RESUME_BUDGET_EXHAUSTED");
  for (const auto &entry : matches) {
    const auto prior = internet_algorithm_candidate_from_json(entry.payload);
    if (!prior.applicability.contains("polynomial_registration")) continue;
    const auto &registration = prior.applicability.at("polynomial_registration");
    if (registration.value("proposal_evidence_id", std::string{}) != proposal_id) continue;
    polynomial_candidate_require(registration == candidate.applicability.at("polynomial_registration") &&
        prior.source_policy_assessment_id == candidate.source_policy_assessment_id &&
        prior.retrieval_receipt_id == retrieval_id,
        "POLYNOMIAL_CANDIDATE_CONFLICTING_REGISTRATION");
    verify_fungrim_quadratic_candidate_translation(prior, fragment);
  }
  return candidate;
}

[[nodiscard]] inline contracts::Json register_fungrim_quadratic_candidate(
    EgcfStore &store, const std::string &proposal_id,
    const std::string &retrieval_id, bool inspect_only = false) {
  const auto candidate = prepare_fungrim_quadratic_candidate(store, proposal_id, retrieval_id);
  const auto id = candidate.object_id();
  const bool exists = store.objects().contains(id);
  if (exists)
    polynomial_candidate_require(store.get(id).payload == to_json(candidate),
        "POLYNOMIAL_CANDIDATE_REGISTRATION_CONTENT_MISMATCH");
  if (!inspect_only && !exists)
    polynomial_candidate_require(InternetImprovementStore(store).register_algorithm_candidate(candidate) == id,
        "POLYNOMIAL_CANDIDATE_REGISTRATION_ID_MISMATCH");
  return {{"registration_candidate_id", id},
      {"candidate_registered", exists || !inspect_only},
      {"status", inspect_only ? "REGISTRATION_INSPECTED" : exists ? "ALREADY_REGISTERED" : "REGISTERED_REVIEW_REQUIRED"},
      {"initial_candidate_status", "QUARANTINED"},
      {"proposal_evidence_id", proposal_id}, {"retrieval_receipt_id", retrieval_id},
      {"required_reviews", candidate.unresolved_assumptions},
      {"protocol_family", internet_polynomial_protocol_version},
      {"execution_contract", candidate.applicability.at("translation").at("execution_contract")},
      {"protocol_preparation_requirements", contracts::Json::array({
          "Candidate-bound baseline and explicit adoption mode",
          "Approved benchmark workloads, score mapping and thresholds",
          "Fresh frozen experiment groups and independent reference provenance",
          "Authorized independent review and negative controls",
          "Real measurement and longitudinal evidence before qualification"})},
      {"protocol_preparation_requirements_assessed", false},
      {"qualification_claim", "NONE"}, {"admission", false},
      {"note", "Registration ID is immutable; it is not a current lifecycle or novelty verdict"}};
}

} // namespace statewright::egcf
