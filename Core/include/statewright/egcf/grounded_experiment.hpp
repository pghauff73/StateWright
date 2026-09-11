#pragma once

#include "statewright/egcf/automated_css_experiment.hpp"
#include "statewright/egcf/internet_polynomial_protocol_freeze.hpp"
#include "statewright/egcf/internet_polynomial_measurement.hpp"
#include "statewright/egcf/internet_polynomial_measurement_coverage.hpp"
#include "statewright/egcf/internet_polynomial_catalog.hpp"
#include "statewright/egcf/internet_reference_oracle.hpp"
#include "statewright/egcf/internet_chebyshev_comparison_store.hpp"

#include "statewright/egcf/internet_experiment.hpp"
#include "statewright/egcf/internet_feed.hpp"
#include "statewright/egcf/canonical_algorithm_store.hpp"
#include "statewright/egcf/evidence.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/core/file_io.hpp"
#include "statewright/common/error.hpp"

#include <openssl/evp.h>
#include <algorithm>
#include <filesystem>
#include <memory>
#include <set>

namespace statewright::egcf {
inline constexpr std::string_view grounded_experiment_version = "saa-grounded-experiment-v2";

inline void grounded_require(bool condition, std::string_view reason) {
  if (!condition) throw common::Error(common::ErrorCode::invalid_argument, std::string(reason));
}

inline std::vector<unsigned char> experiment_unhex(std::string_view text) {
  grounded_require(text.size() % 2 == 0, "INVALID_REVIEW_SIGNATURE_ENCODING");
  std::vector<unsigned char> result;
  const auto digit = [](char c) -> unsigned {
    if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
    grounded_require(false, "INVALID_REVIEW_SIGNATURE_ENCODING");
    return 0;
  };
  for (std::size_t i = 0; i < text.size(); i += 2)
    result.push_back(static_cast<unsigned char>(digit(text[i]) * 16 + digit(text[i + 1])));
  return result;
}

inline contracts::Json experiment_trust_policy(EgcfStore &store) {
  const auto path = store.workspace_root() / ".ourd-agent/egcf/experiment-trust.json";
  grounded_require(std::filesystem::is_regular_file(path), "EXPERIMENT_REVIEW_TRUST_NOT_CONFIGURED");
  grounded_require(std::filesystem::file_size(path) <= 65536, "EXPERIMENT_TRUST_POLICY_TOO_LARGE");
  const auto policy = contracts::parse_json(core::read_text(path));
  grounded_require(policy.at("schema_version") == 1 && policy.at("reviewer_public_keys").is_object(),
                   "INVALID_EXPERIMENT_TRUST_POLICY");
  return policy;
}

inline void verify_experiment_review(const contracts::Json &envelope,
                                     const contracts::Json &trust) {
  const auto &message = envelope.at("message");
  const auto reviewer = message.at("reviewer_id").get<std::string>();
  const auto &keys = trust.at("reviewer_public_keys");
  grounded_require(keys.contains(reviewer), "UNTRUSTED_EXPERIMENT_REVIEWER");
  const auto key_bytes = experiment_unhex(keys.at(reviewer).get<std::string>());
  const auto signature = experiment_unhex(envelope.at("signature_hex").get<std::string>());
  grounded_require(key_bytes.size() == 32 && signature.size() == 64, "INVALID_ED25519_REVIEW_MATERIAL");
  using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
  using Context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
  Key key(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, key_bytes.data(), key_bytes.size()), EVP_PKEY_free);
  Context context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  const auto bytes = contracts::canonical_json(message);
  grounded_require(key && context && EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, key.get()) == 1 &&
      EVP_DigestVerify(context.get(), signature.data(), signature.size(),
          reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size()) == 1,
      "EXPERIMENT_REVIEW_SIGNATURE_INVALID");
}

inline std::string experiment_review_binding(const InternetExperimentProtocol &protocol) {
  auto material = to_json(protocol);
  material.erase("protocol_signature");
  // Reviews attest the frozen protocol before their own immutable IDs exist.
  material["source_provenance"]["grounded"].erase("review_evidence_ids");
  return contracts::sha256_json(material);
}

inline contracts::Json exact_capability_search(EgcfStore &store,
                                               const InternetAlgorithmCandidate &candidate) {
  const auto program = internet_exact_scalar_program(candidate.proposed_saa_ir);
  if (program.polynomial) {
    auto result = search_internet_polynomial_catalog(store, candidate);
    auto coefficients = program.polynomial->coefficients;
    while (coefficients.size() > 1U && coefficients.back() == 0) coefficients.pop_back();
    if (coefficients.size() <= 2U) {
      InternetExactScalarProgram legacy;
      legacy.slope = coefficients.size() == 2U ? coefficients.at(1) : mpq_class(0);
      legacy.bias = coefficients.at(0);
      legacy.bounded_steps = 2;
      CanonicalAlgorithmQuery legacy_query;
      legacy_query.semantic_meanings = {internet_exact_scalar_meaning(legacy,
          candidate.semantic_inputs.at(0), candidate.semantic_outputs.at(0))};
      legacy_query.input_count = 1;
      legacy_query.output_count = 1;
      legacy_query.limit = 20;
      CanonicalAlgorithmStore legacy_store(store);
      const auto legacy_result = to_json(legacy_store.search(std::move(legacy_query)));
      result["legacy_scalar_search"] = legacy_result;
      for (const auto &match : legacy_result.at("candidates")) result["candidates"].push_back(match);
      result["status"] = result.at("candidates").empty() ? "NO_MATCH" : "RECORDED_CAPABILITY_MATCH";
      result.erase("result_signature");
      result["result_signature"] = contracts::sha256_json(result);
    }
    return result;
  }
  CanonicalAlgorithmQuery query;
  query.source_structural_hash = saa::canonicalize_mapping(candidate.proposed_saa_ir).structural_hash;
  query.semantic_meanings = {internet_exact_scalar_meaning(program,
      candidate.semantic_inputs.at(0), candidate.semantic_outputs.at(0))};
  query.input_count = 1;
  query.output_count = 1;
  query.limit = 20;
  CanonicalAlgorithmStore canonical(store);
  return to_json(canonical.search(std::move(query)));
}

inline std::string register_grounded_evidence(EgcfStore &store,
    const InternetAlgorithmCandidate &candidate, contracts::Json content,
    std::string method, std::string producer, std::string group, std::string at,
    bool success = true) {
  EvidenceArtifact evidence;
  evidence.subject_id = candidate.object_id();
  evidence.category = "controlled-experiment";
  evidence.producer = std::move(producer);
  evidence.method = std::move(method);
  evidence.source_snapshot_hash = store.get(candidate.snapshot_id).payload.at("body_sha256").get<std::string>();
  evidence.target = "internal-saa-ir";
  evidence.oracle = "source-bound-reviewed-protocol";
  evidence.algorithm_id = candidate.object_id();
  evidence.created_at = std::move(at);
  evidence.sha256 = contracts::sha256_json(content);
  evidence.success = success;
  evidence.independence_group = std::move(group);
  evidence.content = std::move(content);
  return store.register_record({.object_type = "egcf-evidence", .payload = to_json(evidence)},
                               "grounded_experiment_evidence_registered");
}

struct GroundedExperiment {
  bool enabled = false;
  bool new_capability = false;
  contracts::Json oracle_ir;
  std::vector<std::string> review_ids;
  std::string binding;
};

inline GroundedExperiment validate_grounded_experiment(EgcfStore &store,
    const InternetAlgorithmCandidate &candidate, const InternetExperimentRequest &request) {
  GroundedExperiment result;
  const auto candidate_program = internet_exact_scalar_program(candidate.proposed_saa_ir);
  const bool polynomial = candidate_program.polynomial.has_value();
  grounded_require(!polynomial || !candidate_program.polynomial->direct_power_sum,
                   "POLYNOMIAL_CANDIDATE_REQUIRES_HORNER_EXECUTION");
  const auto trust_path = store.workspace_root() / ".ourd-agent/egcf/experiment-trust.json";
  const bool require_grounding = std::filesystem::is_regular_file(trust_path) &&
      experiment_trust_policy(store).value("require_grounded_protocols", true);
  if (request.protocol_id.empty()) {
    grounded_require(!polynomial, "POLYNOMIAL_VERSIONED_GROUNDED_PROTOCOL_REQUIRED");
    grounded_require(!require_grounding, "GROUNDED_PROTOCOL_V2_REQUIRED");
    return result;
  }
  const auto record = store.get(request.protocol_id);
  grounded_require(record.object_type == "internet-experiment-protocol", "EXPERIMENT_PROTOCOL_TYPE_INVALID");
  const auto protocol = internet_experiment_protocol_from_json(record.payload);
  grounded_require(!polynomial || protocol.protocol_version == internet_polynomial_protocol_version,
                   "POLYNOMIAL_PROTOCOL_FAMILY_MISMATCH");
  grounded_require(polynomial || protocol.protocol_version != internet_polynomial_protocol_version,
                   "POLYNOMIAL_PROTOCOL_REQUIRES_POLYNOMIAL_CANDIDATE");
  if (protocol.protocol_version != grounded_experiment_version &&
      protocol.protocol_version != internet_polynomial_protocol_version) {
    grounded_require(!require_grounding, "LEGACY_PROTOCOL_NOT_AUTHORIZED_FOR_THIS_STORE");
    return result;
  }
  const auto active = store.active_ids("internet-experiment-protocol");
  grounded_require(std::ranges::find(active, record.object_id()) != active.end(), "EXPERIMENT_PROTOCOL_SUPERSEDED");
  grounded_require(request.recorded_at >= protocol.valid_from &&
      (protocol.valid_until.empty() || request.recorded_at <= protocol.valid_until), "EXPERIMENT_PROTOCOL_EXPIRED_OR_NOT_YET_VALID");
  const auto &ground = protocol.source_provenance.at("grounded");
  if (polynomial) {
    const auto freeze_id = ground.value("freeze_evidence_id", std::string{});
    grounded_require(!freeze_id.empty(), "POLYNOMIAL_PROTOCOL_FREEZE_REQUIRED");
    verify_internet_polynomial_protocol_freeze(store, protocol, freeze_id);
    grounded_require(store.get(freeze_id).payload.at("created_at").get<std::string>() <= request.recorded_at,
                     "POLYNOMIAL_PROTOCOL_FREEZE_IS_IN_THE_FUTURE");
    grounded_require(ground.value("review_mode", std::string{}) != "AUTOMATED_CSS_V1",
                     "CSS_REVIEW_NOT_VALID_FOR_POLYNOMIALS");
    grounded_require(ground.at("execution_contract_sha256") ==
        contracts::sha256_json(internet_polynomial_contract(*candidate_program.polynomial)),
        "POLYNOMIAL_EXECUTION_CONTRACT_MISMATCH");
  }
  const auto mode = ground.at("adoption_mode").get<std::string>();
  grounded_require(mode == "REPLACEMENT" || mode == "NEW_CAPABILITY", "EXPERIMENT_ADOPTION_MODE_INVALID");
  const auto trust = experiment_trust_policy(store);
  const auto modes = trust.at("allowed_adoption_modes").get<std::vector<std::string>>();
  grounded_require(std::ranges::find(modes, mode) != modes.end(), "ADOPTION_MODE_NOT_AUTHORIZED");
  result.enabled = true;
  result.new_capability = mode == "NEW_CAPABILITY";
  result.oracle_ir = ground.at("reference_oracle_ir");
  result.binding = experiment_review_binding(protocol);
  grounded_require(ground.at("candidate_id") == candidate.object_id() &&
      ground.at("source_fragment_id") == candidate.source_fragment_id &&
      ground.at("source_body_sha256") == store.get(candidate.snapshot_id).payload.at("body_sha256") &&
      ground.at("candidate_ir_sha256") == contracts::sha256_json(candidate.proposed_saa_ir),
      "EXPERIMENT_SOURCE_OR_CANDIDATE_BINDING_MISMATCH");
  const auto &claim = ground.at("claim");
  grounded_require(claim.at("inputs") == candidate.semantic_inputs &&
      claim.at("outputs") == candidate.semantic_outputs && claim.at("units") == candidate.units &&
      !claim.at("domain").get<std::string>().empty() && !claim.at("exclusions").empty(),
      "EXPERIMENT_SEMANTIC_SCOPE_MISMATCH");
  grounded_require(request.baseline_ref == protocol.baseline_ref && request.baseline_saa_ir == protocol.baseline_saa_ir &&
      request.dataset_snapshot_ids == protocol.dataset_snapshot_ids && request.minimum_material_effect == mpq_class(protocol.minimum_material_effect) &&
      request.minimum_output == mpq_class(protocol.minimum_output) && request.maximum_output == mpq_class(protocol.maximum_output) &&
      request.minimum_trials_per_group == protocol.minimum_trials_per_group && request.minimum_experiments == protocol.minimum_experiments &&
      request.minimum_independence_groups == protocol.minimum_independence_groups && request.maximum_total_trials == protocol.maximum_total_trials,
      "EXPERIMENT_REQUEST_DIFFERS_FROM_FROZEN_PROTOCOL");
  contracts::Json scores = contracts::Json::object();
  for (const auto &[name, score] : request.benchmark_track_scores) scores[name] = score;
  contracts::Json integrity = contracts::Json::array();
  for (const auto &snapshot : request.integrity_snapshots) integrity.push_back(saa::to_json(snapshot));
  grounded_require(scores == protocol.benchmark_track_scores && integrity == protocol.integrity_snapshots,
      "EXPERIMENT_MEASUREMENTS_DIFFER_FROM_PROTOCOL");
  grounded_require(saa::to_json(request.benchmark_policy) == protocol.benchmark_policy &&
      saa::to_json(request.integrity_policy) == protocol.integrity_policy,
      "EXPERIMENT_POLICIES_DIFFER_FROM_FROZEN_PROTOCOL");
  std::vector<InternetScalarTrialGroup> frozen;
  for (const auto &value : protocol.trial_groups) {
    InternetScalarTrialGroup group;
    group.independence_group = value.at("independence_group").get<std::string>();
    group.deterministic_seed = value.at("deterministic_seed").get<int>();
    for (const auto &input : value.at("inputs")) group.inputs.emplace_back(input.get<std::string>());
    for (const auto &output : value.at("expected_outputs")) group.expected_outputs.emplace_back(output.get<std::string>());
    for (auto &input : group.inputs) { grounded_require(input.get_den() != 0, "ZERO_DATASET_DENOMINATOR"); input.canonicalize(); }
    for (auto &output : group.expected_outputs) { grounded_require(output.get_den() != 0, "ZERO_DATASET_DENOMINATOR"); output.canonicalize(); }
    frozen.push_back(std::move(group));
  }
  grounded_require(request.context_signature == internet_experiment_context_signature(protocol.dataset_snapshot_ids, frozen),
                   "EXPERIMENT_FROZEN_DATASET_MISMATCH");
  const auto oracle = internet_reference_oracle_program(result.oracle_ir);
  verify_internet_reference_oracle_scope(oracle, candidate_program);
  if (oracle.chebyshev_degree) {
    verify_chebyshev_qualification_comparison(store, ground, request.recorded_at);
  }
  for (const auto &group : request.trial_groups)
    for (std::size_t i = 0; i < group.inputs.size(); ++i)
      grounded_require(internet_execute_reference_oracle(oracle, group.inputs[i]) == group.expected_outputs.at(i), "EXPECTED_OUTPUT_NOT_BOUND_TO_REVIEWED_ORACLE");
  const auto baseline = store.get(protocol.baseline_ref);
  if (result.new_capability) {
    grounded_require(protocol.baseline_saa_ir.empty() && baseline.object_type == "egcf-evidence" &&
        baseline.payload.at("content").at("kind") == "CANONICAL_CATALOG_UNSUPPORTED_BASELINE_V1" &&
        baseline.payload.at("content").at("candidate_id") == candidate.object_id() &&
        baseline.payload.at("content").at("workspace") == store.workspace_root().string() &&
        baseline.payload.at("content").at("search").at("candidates").empty(),
        "NEW_CAPABILITY_BASELINE_IS_NOT_RECORDED_UNSUPPORTED_LOOKUP");
    grounded_require(exact_capability_search(store, candidate).at("candidates").empty(), "CAPABILITY_ALREADY_PRESENT_IN_THIS_CATALOG");
    grounded_require(ground.at("baseline_rationale") == "CANONICAL_CATALOG_LOOKUP_ONLY", "UNSUPPORTED_BASELINE_SCOPE");
  } else {
    grounded_require(!ground.at("baseline_rationale").get<std::string>().empty() &&
        baseline.object_type == "egcf-evidence" && baseline.payload.at("content").at("kind") == "DEPLOYED_BASELINE_CAPTURE_V1" &&
        baseline.payload.at("content").at("saa_ir") == protocol.baseline_saa_ir &&
        !baseline.payload.at("content").at("deployment_reference").get<std::string>().empty(),
        "REPLACEMENT_REQUIRES_CAPTURED_DEPLOYED_BASELINE");
  }
  const auto measurements = store.get(ground.at("measurement_evidence_id").get<std::string>());
  if (polynomial) {
    const auto coverage = internet_polynomial_measurement_coverage(measurements.payload.at("content"));
    grounded_require(coverage.at("status") == "REQUIRES_EVIDENCE_VALIDATION",
        "POLYNOMIAL_MEASUREMENT_FIELDS_INCOMPLETE: " + coverage.dump());
    const auto freeze_id = ground.at("freeze_evidence_id").get<std::string>();
    grounded_require(measurements.payload.at("content").value("polynomial_protocol_freeze_id", std::string{}) == freeze_id &&
        measurements.payload.at("created_at").get<std::string>() >= store.get(freeze_id).payload.at("created_at").get<std::string>() &&
        measurements.payload.at("created_at").get<std::string>() <= request.recorded_at,
        "POLYNOMIAL_MEASUREMENTS_REQUIRE_MATCHING_PRIOR_PROTOCOL_FREEZE");
    const auto raw_ids = measurements.payload.at("content").value(
        "polynomial_raw_measurement_ids", std::vector<std::string>{});
    grounded_require(!raw_ids.empty() && raw_ids.size() <= 32U &&
        std::set<std::string>(raw_ids.begin(), raw_ids.end()).size() == raw_ids.size(),
        "POLYNOMIAL_RAW_MEASUREMENT_CHAIN_REQUIRED");
    for (const auto &raw_id : raw_ids) {
      verify_internet_polynomial_measurement_chain(store, raw_id, freeze_id,
          measurements.payload.at("created_at").get<std::string>());
    }
  }
  grounded_require(measurements.object_type == "egcf-evidence" && !measurements.payload.at("simulated").get<bool>() &&
      measurements.payload.at("content").at("benchmark_track_scores") == scores &&
      measurements.payload.at("content").at("integrity_snapshots") == integrity,
      "MEASURED_BENCHMARK_AND_INTEGRITY_EVIDENCE_REQUIRED");
  result.review_ids = ground.at("review_evidence_ids").get<std::vector<std::string>>();
  if (ground.value("review_mode", std::string{}) == "AUTOMATED_CSS_V1") {
    validate_automated_css_review(store, protocol, result.binding, trust);
    return result;
  }
  std::set<std::string> reviewers, groups, methods, reviewer_keys;
  for (const auto &id : result.review_ids) {
    const auto evidence = store.get(id);
    grounded_require(evidence.object_type == "egcf-evidence" && !evidence.payload.at("simulated").get<bool>(), "INDEPENDENT_REVIEW_EVIDENCE_REQUIRED");
    const auto &envelope = evidence.payload.at("content");
    verify_experiment_review(envelope, trust);
    const auto &message = envelope.at("message");
    const auto reviewer = message.at("reviewer_id").get<std::string>();
    grounded_require(message.at("protocol_binding_sha256") == result.binding && message.at("verdict") == "APPROVE" &&
        reviewer != ground.at("author_identity").get<std::string>() &&
        !message.at("derivation").get<std::string>().empty() && !message.at("shared_dependencies").empty() &&
        message.at("reviewed_at").get<std::string>() <= request.recorded_at,
        "REVIEW_DOES_NOT_INDEPENDENTLY_APPROVE_THIS_PROTOCOL");
    reviewers.insert(reviewer);
    reviewer_keys.insert(trust.at("reviewer_public_keys").at(reviewer).get<std::string>());
    groups.insert(message.at("independence_group").get<std::string>());
    methods.insert(message.at("method").get<std::string>());
    const auto controls = message.at("negative_control_evidence_ids").get<std::vector<std::string>>();
    grounded_require(!controls.empty(), "NEGATIVE_CONTROL_EVIDENCE_REQUIRED");
    for (const auto &control_id : controls) {
      const auto control = store.get(control_id);
      grounded_require(control.object_type == "egcf-evidence" && !control.payload.at("simulated").get<bool>() &&
          control.payload.at("success") == true, "NEGATIVE_CONTROL_EVIDENCE_INVALID");
    }
  }
  grounded_require(reviewers.size() >= 2 && reviewer_keys.size() >= 2 && groups.size() >= 2 && methods.size() >= 2 && !methods.contains(""),
                   "TWO_INDEPENDENT_REVIEWED_METHODS_REQUIRED");
  for (const auto &group : request.trial_groups)
    grounded_require(groups.contains(group.independence_group), "TRIAL_GROUP_LACKS_SIGNED_REVIEW");
  return result;
}
} // namespace statewright::egcf
