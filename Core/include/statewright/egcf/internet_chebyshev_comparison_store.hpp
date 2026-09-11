#pragma once

#include "statewright/egcf/evidence.hpp"
#include "statewright/egcf/internet_chebyshev_comparison.hpp"
#include "statewright/egcf/internet_reference_oracle.hpp"

namespace statewright::egcf {

inline void verify_chebyshev_protocol_reference_design(
    const contracts::Json &protocol_design, const std::string &design_id,
    const EvidenceArtifact &design, const EvidenceArtifact &proposal) {
  const auto require = [](bool condition) {
    if (!condition) throw common::Error(common::ErrorCode::invalid_argument,
        "CHEBYSHEV_PROTOCOL_REFERENCE_DESIGN_MISMATCH");
  };
  const auto &ground = protocol_design.at("source_provenance").at("grounded");
  require(protocol_design.at("protocol_version") == internet_polynomial_protocol_version &&
      ground.at("reference_design_evidence_id") == design_id &&
      ground.at("candidate_ir_sha256") == design.content.at("candidate_ir_sha256") &&
      ground.at("source_fragment_id") == proposal.content.at("fragment_id") &&
      ground.at("source_body_sha256") == proposal.source_snapshot_hash &&
      ground.at("reference_oracle_ir") == chebyshev_reference_oracle_descriptor(
          design.content.at("degree").get<int>()));
  const auto &groups = protocol_design.at("trial_groups");
  require(groups.is_array() && groups.size() == design.content.at("groups").size());
  std::set<std::string> names;
  for (const auto &group : groups) {
    const auto name = group.at("independence_group").get<std::string>();
    require(names.insert(name).second);
    bool matched = false;
    for (const auto &reference : design.content.at("groups")) {
      if (reference.at("name") != name) continue;
      require(group.at("inputs") == reference.at("inputs") &&
          group.at("expected_outputs") == reference.at("expected_outputs"));
      matched = true;
    }
    require(matched);
  }
}

// Receipt checks deliberately do not execute the candidate. The stored outputs
// are observations, not a new correctness verdict or independent attestation.
inline void verify_chebyshev_comparison_observations(
    const contracts::Json &result, const contracts::Json &design) {
  const auto require = [](bool condition) {
    if (!condition) throw common::Error(common::ErrorCode::invalid_argument,
        "CHEBYSHEV_COMPARISON_RECORDED_OBSERVATIONS_INVALID");
  };
  require(result.at("kind") == "CHEBYSHEV_REFERENCE_COMPARISON_V1" &&
      result.at("design_sha256") == contracts::sha256_json(design) &&
      result.at("proposal_evidence_id") == design.at("proposal_evidence_id") &&
      result.at("proposal_evidence_sha256") == design.at("proposal_evidence_sha256") &&
      result.at("candidate_ir_sha256") == design.at("candidate_ir_sha256") &&
      result.at("candidate_evaluated") == true &&
      result.at("qualification_claim") == "NONE" &&
      result.at("independence_status") == "NOT_ESTABLISHED" &&
      result.at("case_count") == design.at("case_count"));
  const auto &groups = result.at("groups");
  require(groups.is_array() && groups.size() == design.at("groups").size());
  std::size_t mismatches = 0;
  for (std::size_t g = 0; g < groups.size(); ++g) {
    const auto &frozen = design.at("groups").at(g);
    const auto &recorded = groups.at(g);
    const auto &cases = recorded.at("cases");
    require(recorded.at("name") == frozen.at("name") && cases.is_array() &&
        cases.size() == frozen.at("inputs").size());
    for (std::size_t i = 0; i < cases.size(); ++i) {
      const auto &item = cases.at(i);
      require(item.at("input") == frozen.at("inputs").at(i) &&
          item.at("expected") == frozen.at("expected_outputs").at(i) &&
          item.at("actual").is_string() &&
          item.at("actual").get_ref<const std::string &>().size() <= 10000U);
      const auto actual = item.at("actual").get<std::string>();
      mpq_class normalized(actual);
      require(normalized.get_den() > 0);
      normalized.canonicalize();
      require(normalized.get_str() == actual);
      const bool matches = item.at("actual") == item.at("expected");
      require(item.at("matches") == matches);
      if (!matches) ++mismatches;
    }
  }
  require(result.at("mismatch_count") == mismatches &&
      result.at("status") == (mismatches == 0U ? "SAMPLED_MATCH" : "SAMPLED_MISMATCH"));
}

// Caller must own the native action lease before allowing writes. Read-only
// discovery is safe before lease acquisition. An interrupted attempt without a
// completed receipt is executed anew; no missing observation is reconstructed.
[[nodiscard]] inline std::string chebyshev_comparison_work(
    EgcfStore &store, const std::string &design_id, bool existing_only,
    const std::string &expected_proposal_id = {},
    const std::string &protocol_freeze_id = {}) {
  const auto require = [](bool condition, const char *reason) {
    if (!condition) throw common::Error(common::ErrorCode::invalid_argument, reason);
  };
  const auto read = [&](const std::string &id, const char *category) {
    const auto record = store.get(id);
    require(record.object_type == "egcf-evidence", "CHEBYSHEV_COMPARISON_NATIVE_EVIDENCE_REQUIRED");
    const auto evidence = evidence_artifact_from_json(record.payload);
    require(!evidence.simulated && evidence.category == category &&
        evidence.sha256 == contracts::sha256_json(evidence.content),
        "CHEBYSHEV_COMPARISON_NATIVE_EVIDENCE_INVALID");
    return evidence;
  };
  const auto frozen = read(design_id, "POLYNOMIAL_REFERENCE_DESIGN");
  require(expected_proposal_id.empty() || frozen.subject_id == expected_proposal_id,
          "CHEBYSHEV_COMPARISON_PROPOSAL_SCOPE_MISMATCH");
  const auto proposal = read(frozen.subject_id, "POLYNOMIAL_TRANSLATION_PROPOSAL");
  require(proposal.content.at("kind") == "POLYNOMIAL_SOURCE_TRANSLATION_PROPOSAL_V1" &&
      frozen.source_snapshot_hash == proposal.source_snapshot_hash &&
      frozen.created_at >= proposal.created_at, "CHEBYSHEV_COMPARISON_SOURCE_BINDING_INVALID");
  verify_chebyshev_reference_design(frozen.subject_id, proposal.sha256,
      proposal.content.at("proposal"), frozen.content);
  std::optional<EvidenceArtifact> protocol_freeze;
  if (!protocol_freeze_id.empty()) {
    protocol_freeze = read(protocol_freeze_id, "POLYNOMIAL_PROTOCOL_DESIGN");
    require(protocol_freeze->content.at("kind") == "POLYNOMIAL_PROTOCOL_DESIGN_FREEZE_V1" &&
        protocol_freeze->content.at("snapshot_id") == proposal.content.at("snapshot_id") &&
        protocol_freeze->source_snapshot_hash == frozen.source_snapshot_hash &&
        protocol_freeze->created_at >= frozen.created_at,
        "CHEBYSHEV_COMPARISON_PROTOCOL_FREEZE_BINDING_INVALID");
    verify_chebyshev_protocol_reference_design(protocol_freeze->content.at("protocol_design"),
        design_id, frozen, proposal);
  }
  const auto matches = store.search_text("\"" + design_id + "\"", "egcf-evidence", 33);
  require(matches.size() <= 32U, "CHEBYSHEV_COMPARISON_RESUME_BUDGET_EXHAUSTED");
  std::string existing;
  for (const auto &match : matches) {
    const auto prior = evidence_artifact_from_json(match.payload);
    if (prior.category != "POLYNOMIAL_REFERENCE_COMPARISON" ||
        prior.content.value("design_id", std::string{}) != design_id) continue;
    require(existing.empty() && !prior.simulated && prior.subject_id == design_id &&
        prior.sha256 == contracts::sha256_json(prior.content) &&
        prior.source_snapshot_hash == frozen.source_snapshot_hash &&
        prior.created_at >= frozen.created_at && !prior.success.has_value(),
        "CHEBYSHEV_COMPARISON_RESUME_BINDING_INVALID");
    verify_chebyshev_comparison_observations(prior.content.at("comparison"), frozen.content);
    if (protocol_freeze) {
      require(prior.content.value("polynomial_protocol_freeze_id", std::string{}) == protocol_freeze_id &&
          prior.content.contains("start_evidence_id"),
          "CHEBYSHEV_EXPLORATORY_COMPARISON_IS_NOT_POST_FREEZE_EVIDENCE");
    }
    // Earlier direct comparisons had no start receipt. Retain that history
    // without inventing one; new comparisons always bind a real start record.
    if (prior.content.contains("start_evidence_id")) {
      const auto start = read(prior.content.at("start_evidence_id").get<std::string>(),
          "POLYNOMIAL_REFERENCE_COMPARISON_START");
      require(start.subject_id == design_id && start.content.at("design_id") == design_id &&
          start.content.at("design_sha256") == frozen.sha256 &&
          start.source_snapshot_hash == frozen.source_snapshot_hash &&
          start.created_at >= frozen.created_at && start.created_at <= prior.created_at,
          "CHEBYSHEV_COMPARISON_START_BINDING_INVALID");
      if (protocol_freeze) {
        require(start.content.value("polynomial_protocol_freeze_id", std::string{}) == protocol_freeze_id &&
            start.created_at >= protocol_freeze->created_at,
            "CHEBYSHEV_COMPARISON_START_PRECEDES_PROTOCOL_FREEZE");
      }
    }
    existing = match.object_id;
  }
  if (!existing.empty() || existing_only) return existing;
  EvidenceInput started;
  started.subject_id = design_id;
  started.category = "POLYNOMIAL_REFERENCE_COMPARISON_START";
  started.producer = "chebyshev-reference-comparison-v1";
  started.method = "PERSIST_START_BEFORE_NATIVE_COMPARISON";
  started.source_snapshot_hash = frozen.source_snapshot_hash;
  started.independence_group = "shared-native-chebyshev-reference-design-v1";
  started.content = {{"design_id", design_id}, {"design_sha256", frozen.sha256},
      {"qualification_claim", "NONE"}};
  if (protocol_freeze) started.content["polynomial_protocol_freeze_id"] = protocol_freeze_id;
  started.limitations = {"Start only; evaluation may fail or be interrupted"};
  const auto start_id = EvidenceManager(store).collect(std::move(started));
  const auto comparison = compare_chebyshev_reference_design(frozen.subject_id,
      proposal.sha256, proposal.content.at("proposal"), frozen.content);
  EvidenceInput input;
  input.subject_id = design_id;
  input.category = "POLYNOMIAL_REFERENCE_COMPARISON";
  input.producer = "chebyshev-reference-comparison-v1";
  input.method = "FROZEN_INTEGER_REFERENCE_VS_NATIVE_GMP_HORNER";
  input.source_snapshot_hash = frozen.source_snapshot_hash;
  input.independence_group = "shared-native-chebyshev-reference-design-v1";
  input.limitations = {"Finite pointwise results, not a qualification verdict",
      "Shared author, compiler and host; independent review remains required"};
  input.content = {{"design_id", design_id}, {"start_evidence_id", start_id},
      {"comparison", comparison}};
  if (protocol_freeze) input.content["polynomial_protocol_freeze_id"] = protocol_freeze_id;
  return EvidenceManager(store).collect(std::move(input));
}

// Called only after the native candidate and full protocol freeze are checked.
// This adds receipt provenance and observation gates, not reviewer authority.
inline void verify_chebyshev_qualification_comparison(EgcfStore &store,
    const contracts::Json &ground, const std::string &recorded_at) {
  const auto design_id = ground.at("reference_design_evidence_id").get<std::string>();
  const auto comparison_id = ground.at("reference_comparison_evidence_id").get<std::string>();
  const auto found = chebyshev_comparison_work(store, design_id, true, {},
      ground.at("freeze_evidence_id").get<std::string>());
  if (found.empty() || found != comparison_id) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "CHEBYSHEV_QUALIFICATION_COMPARISON_CHAIN_REQUIRED");
  }
  const auto comparison = evidence_artifact_from_json(store.get(found).payload);
  if (comparison.created_at > recorded_at ||
      comparison.content.at("comparison").at("mismatch_count") != 0) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "CHEBYSHEV_QUALIFICATION_COMPARISON_FAILED_OR_IN_FUTURE");
  }
}

} // namespace statewright::egcf
