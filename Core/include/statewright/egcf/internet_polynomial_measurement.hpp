#pragma once

#include "statewright/egcf/evidence.hpp"
#include "statewright/egcf/internet_polynomial_store.hpp"
#include "statewright/egcf/internet_polynomial_performance.hpp"

namespace statewright::egcf {

inline constexpr std::string_view internet_polynomial_measurement_version =
    "internet-polynomial-raw-measurement-v1";

[[nodiscard]] inline contracts::Json internet_polynomial_measurement_design(
    const EgcfStore &store, const std::string &candidate_id,
    const std::vector<mpq_class> &inputs, std::size_t repetitions,
    const std::string &protocol_freeze_id = {}) {
  const auto record = store.get(candidate_id);
  if (record.object_type != "internet-algorithm-candidate" || inputs.empty() ||
      inputs.size() > 256U || repetitions < 2U || repetitions > 100U ||
      inputs.size() * repetitions > 4096U) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_MEASUREMENT_DESIGN_BOUNDS_INVALID");
  }
  const auto candidate = internet_algorithm_candidate_from_json(record.payload);
  const auto program = internet_exact_polynomial_program(candidate.proposed_saa_ir);
  const auto snapshot = store.get(candidate.snapshot_id);
  if (program.direct_power_sum || snapshot.object_type != "internet-source-snapshot") {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_MEASUREMENT_REQUIRES_NATIVE_HORNER_SOURCE_BINDING");
  }
  contracts::Json frozen = contracts::Json::array();
  for (const auto &input : inputs) {
    if (input.get_den() <= 0 || input < -1 || input > 1 ||
        mpz_sizeinbase(input.get_num().get_mpz_t(), 2) > 256U ||
        mpz_sizeinbase(input.get_den().get_mpz_t(), 2) > 256U) {
      throw common::Error(common::ErrorCode::invalid_argument,
                          "POLYNOMIAL_MEASUREMENT_INPUT_OUTSIDE_CONTRACT");
    }
    auto normalized = input;
    normalized.canonicalize();
    frozen.push_back(normalized.get_str());
  }
  contracts::Json design = {{"kind", "POLYNOMIAL_FROZEN_MEASUREMENT_DESIGN_V1"},
          {"candidate_id", candidate_id},
          {"candidate_ir", candidate.proposed_saa_ir},
          {"baseline_ir", internet_exact_polynomial_reference_ir(
              program.coefficients, program.maximum_integer_bits)},
          {"execution_contract", internet_polynomial_contract(program)},
          {"snapshot_id", candidate.snapshot_id},
          {"snapshot_record_sha256", contracts::sha256_json(snapshot.payload)},
          {"inputs", frozen}, {"repetitions", repetitions},
          {"qualification_claim", "NONE"}};
  if (!protocol_freeze_id.empty()) {
    const auto protocol_record = store.get(protocol_freeze_id);
    if (protocol_record.object_type != "egcf-evidence") {
      throw common::Error(common::ErrorCode::invalid_argument,
                          "POLYNOMIAL_MEASUREMENT_PROTOCOL_FREEZE_TYPE_INVALID");
    }
    const auto protocol = evidence_artifact_from_json(protocol_record.payload);
    if (protocol.simulated || protocol.category != "POLYNOMIAL_PROTOCOL_DESIGN" ||
        protocol.sha256 != contracts::sha256_json(protocol.content) ||
        protocol.content.at("candidate_id") != candidate_id ||
        protocol.content.at("execution_contract") != design.at("execution_contract")) {
      throw common::Error(common::ErrorCode::invalid_argument,
                          "POLYNOMIAL_MEASUREMENT_PROTOCOL_BINDING_INVALID");
    }
    const auto &frozen_protocol = protocol.content.at("protocol_design");
    contracts::Json protocol_inputs = contracts::Json::array();
    for (const auto &group : frozen_protocol.at("trial_groups")) {
      for (const auto &value : group.at("inputs")) {
        if (protocol_inputs.size() == 256U) {
          throw common::Error(common::ErrorCode::invalid_argument,
                              "POLYNOMIAL_PROTOCOL_MEASUREMENT_INPUT_BUDGET_EXCEEDED");
        }
        protocol_inputs.push_back(value);
      }
    }
    if (protocol_inputs != frozen ||
        frozen_protocol.at("source_provenance").at("grounded").at("measurement_repetitions") != repetitions) {
      throw common::Error(common::ErrorCode::invalid_argument,
                          "POLYNOMIAL_MEASUREMENT_WORKLOAD_DIFFERS_FROM_PROTOCOL");
    }
    design["polynomial_protocol_freeze_id"] = protocol_freeze_id;
  }
  return design;
}

[[nodiscard]] inline std::string freeze_internet_polynomial_measurement(
    EgcfStore &store, const std::string &candidate_id,
    const std::vector<mpq_class> &inputs, std::size_t repetitions = 10U,
    const std::string &protocol_freeze_id = {}) {
  const auto design = internet_polynomial_measurement_design(
      store, candidate_id, inputs, repetitions, protocol_freeze_id);
  EvidenceInput evidence;
  evidence.subject_id = candidate_id;
  evidence.content = design;
  evidence.category = "POLYNOMIAL_MEASUREMENT_DESIGN";
  evidence.producer = internet_polynomial_measurement_version;
  evidence.method = "FREEZE_INPUTS_BEFORE_TIMING";
  evidence.source_snapshot_hash = design.at("snapshot_record_sha256");
  evidence.independence_group = "shared-native-polynomial-gmp-v1";
  evidence.limitations = {"Design only; not an experiment protocol or expected-result proof"};
  return EvidenceManager(store).collect(std::move(evidence));
}

// One bounded timing unit. A durable result is reused on resume. Interrupted
// work without a result is measured again; no timing or timestamp is invented.
[[nodiscard]] inline std::string internet_polynomial_measurement_work(
    EgcfStore &store, const std::string &design_id, bool existing_only) {
  const auto record = store.get(design_id);
  if (record.object_type != "egcf-evidence") {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_MEASUREMENT_REQUIRES_NATIVE_DESIGN");
  }
  const auto frozen = evidence_artifact_from_json(record.payload);
  const auto &design = frozen.content;
  if (frozen.category != "POLYNOMIAL_MEASUREMENT_DESIGN" || frozen.simulated ||
      frozen.sha256 != contracts::sha256_json(design) ||
      design.dump().size() > 131072U || !design.at("inputs").is_array() ||
      design.at("inputs").size() > 256U) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_MEASUREMENT_DESIGN_BINDING_INVALID");
  }
  std::vector<mpq_class> inputs;
  for (const auto &value : design.at("inputs")) {
    if (!value.is_string() || value.get_ref<const std::string &>().size() > 160U) {
      throw common::Error(common::ErrorCode::invalid_argument,
                          "POLYNOMIAL_MEASUREMENT_INPUT_ENCODING_INVALID");
    }
    inputs.emplace_back(value.get<std::string>());
  }
  const auto expected = internet_polynomial_measurement_design(
      store, frozen.subject_id, inputs, design.at("repetitions").get<std::size_t>(),
      design.value("polynomial_protocol_freeze_id", std::string{}));
  if (design != expected || frozen.source_snapshot_hash != design.at("snapshot_record_sha256").get<std::string>()) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_MEASUREMENT_FROZEN_DESIGN_MISMATCH");
  }
  const auto matches = store.search_text("\"" + design_id + "\"", "egcf-evidence", 33);
  if (matches.size() > 32U) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_MEASUREMENT_RESUME_BUDGET_EXHAUSTED");
  }
  std::string existing;
  for (const auto &match : matches) {
    const auto prior = evidence_artifact_from_json(match.payload);
    if (prior.category != "POLYNOMIAL_RAW_MEASUREMENT" ||
        prior.content.value("design_id", std::string{}) != design_id) continue;
    if (!existing.empty() || prior.simulated || prior.subject_id != frozen.subject_id ||
        prior.sha256 != contracts::sha256_json(prior.content) ||
        prior.content.at("design_sha256") != frozen.sha256) {
      throw common::Error(common::ErrorCode::invalid_argument,
                          "POLYNOMIAL_MEASUREMENT_RESUME_BINDING_INVALID");
    }
    existing = match.object_id;
  }
  if (!existing.empty()) return existing;
  if (existing_only) return {};
  EvidenceInput started;
  started.subject_id = frozen.subject_id;
  started.category = "POLYNOMIAL_MEASUREMENT_START";
  started.producer = internet_polynomial_measurement_version;
  started.method = "PERSIST_START_BEFORE_NATIVE_TIMINGS";
  started.source_snapshot_hash = frozen.source_snapshot_hash;
  started.independence_group = "shared-native-polynomial-gmp-v1";
  started.content = {{"kind", "POLYNOMIAL_MEASUREMENT_START_V1"},
      {"design_id", design_id}, {"design_sha256", frozen.sha256},
      {"polynomial_protocol_freeze_id", design.value("polynomial_protocol_freeze_id", std::string{})},
      {"qualification_claim", "NONE"}};
  started.limitations = {"Start receipt only; measurement may fail or be interrupted"};
  const auto start_id = EvidenceManager(store).collect(std::move(started));
  const auto measurement = internet_measure_polynomial_pair(
      design.at("candidate_ir"), design.at("baseline_ir"), inputs,
      design.at("repetitions").get<std::size_t>());
  EvidenceInput evidence;
  evidence.subject_id = frozen.subject_id;
  evidence.category = "POLYNOMIAL_RAW_MEASUREMENT";
  evidence.producer = internet_polynomial_measurement_version;
  evidence.method = "BOUNDED_NATIVE_PAIRED_TIMINGS";
  evidence.source_snapshot_hash = frozen.source_snapshot_hash;
  evidence.independence_group = "shared-native-polynomial-gmp-v1";
  evidence.content = {{"kind", "POLYNOMIAL_RAW_MEASUREMENT_V1"},
      {"design_id", design_id}, {"design_sha256", frozen.sha256},
      {"start_evidence_id", start_id},
      {"polynomial_protocol_freeze_id", design.value("polynomial_protocol_freeze_id", std::string{})},
      {"measurement", measurement}, {"qualification_claim", "NONE"}};
  evidence.limitations = {
      "Shared GMP, compiler, host, coefficient provenance and validation code",
      "Direct-power-sum baseline is not an independently sourced oracle",
      "Source admissibility and mathematical context are not established here",
      "No benchmark score, independent review or longitudinal observation",
      "Success is intentionally unset pending separate evidence assessment"};
  return EvidenceManager(store).collect(std::move(evidence));
}

[[nodiscard]] inline std::string collect_internet_polynomial_measurement(
    EgcfStore &store, const std::string &design_id) {
  return internet_polynomial_measurement_work(store, design_id, false);
}

// Read-only discovery for reconciliation before lease ownership is resolved.
// This must never perform timings or write evidence.
[[nodiscard]] inline std::optional<std::string> find_internet_polynomial_measurement(
    EgcfStore &store, const std::string &design_id) {
  const auto existing = internet_polynomial_measurement_work(store, design_id, true);
  if (existing.empty()) return std::nullopt;
  return existing;
}

// Check provenance and chronology, not benchmark policy or reviewer authority.
inline void verify_internet_polynomial_measurement_chain(
    const EgcfStore &store, const std::string &measurement_id,
    const std::string &protocol_freeze_id, const std::string &recorded_at) {
  const auto read = [&](const std::string &id, const std::string &category) {
    const auto record = store.get(id);
    if (record.object_type != "egcf-evidence") {
      throw common::Error(common::ErrorCode::invalid_argument,
                          "POLYNOMIAL_MEASUREMENT_CHAIN_REQUIRES_NATIVE_EVIDENCE");
    }
    const auto evidence = evidence_artifact_from_json(record.payload);
    if (evidence.simulated || evidence.category != category ||
        evidence.sha256 != contracts::sha256_json(evidence.content)) {
      throw common::Error(common::ErrorCode::invalid_argument,
                          "POLYNOMIAL_MEASUREMENT_CHAIN_EVIDENCE_INVALID");
    }
    return evidence;
  };
  const auto result = read(measurement_id, "POLYNOMIAL_RAW_MEASUREMENT");
  const auto start = read(result.content.at("start_evidence_id").get<std::string>(),
                          "POLYNOMIAL_MEASUREMENT_START");
  const auto design_id = result.content.at("design_id").get<std::string>();
  const auto design = read(design_id, "POLYNOMIAL_MEASUREMENT_DESIGN");
  const auto protocol = read(protocol_freeze_id, "POLYNOMIAL_PROTOCOL_DESIGN");
  if (result.subject_id != design.subject_id || start.subject_id != design.subject_id ||
      protocol.subject_id != design.subject_id ||
      start.content.at("design_id") != design_id ||
      start.content.at("design_sha256") != design.sha256 ||
      result.content.at("design_sha256") != design.sha256 ||
      result.content.at("polynomial_protocol_freeze_id") != protocol_freeze_id ||
      start.content.at("polynomial_protocol_freeze_id") != protocol_freeze_id ||
      design.content.at("polynomial_protocol_freeze_id") != protocol_freeze_id ||
      protocol.created_at > design.created_at || design.created_at > start.created_at ||
      start.created_at > result.created_at || result.created_at > recorded_at ||
      result.source_snapshot_hash != design.source_snapshot_hash ||
      start.source_snapshot_hash != design.source_snapshot_hash) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_MEASUREMENT_CHAIN_BINDING_OR_ORDER_INVALID");
  }
  const auto &values = design.content.at("inputs");
  if (!values.is_array() || values.empty() || values.size() > 256U) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_MEASUREMENT_CHAIN_INPUT_BUDGET_INVALID");
  }
  std::vector<mpq_class> inputs;
  for (const auto &value : values) {
    if (!value.is_string() || value.get_ref<const std::string &>().size() > 160U) {
      throw common::Error(common::ErrorCode::invalid_argument,
                          "POLYNOMIAL_MEASUREMENT_CHAIN_INPUT_ENCODING_INVALID");
    }
    inputs.emplace_back(value.get<std::string>());
  }
  if (design.content != internet_polynomial_measurement_design(
      store, design.subject_id, inputs,
      design.content.at("repetitions").get<std::size_t>(), protocol_freeze_id)) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_MEASUREMENT_CHAIN_DESIGN_CHANGED");
  }
  static_cast<void>(summarize_internet_polynomial_performance(
      result.content.at("measurement"), design.content));
}

} // namespace statewright::egcf
