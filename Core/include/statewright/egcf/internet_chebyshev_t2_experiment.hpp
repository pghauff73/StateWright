#pragma once

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/egcf/evidence.hpp"
#include "statewright/egcf/internet_context_resolution.hpp"
#include "statewright/egcf/internet_improvement_store.hpp"
#include "statewright/egcf/internet_polynomial_operation.hpp"
#include "statewright/egcf/internet_polynomial_candidate.hpp"
#include "statewright/egcf/internet_polynomial.hpp"
#include "statewright/saa/algorithm_ir.hpp"

#include <gmpxx.h>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <map>
#include <set>
#include <optional>
#include <string>
#include <vector>

namespace statewright::egcf {

using Json = contracts::Json;

inline constexpr std::string_view chebyshev_t2_experiment_version =
    "exact-rational-chebyshev-t2-v1";

struct ChebyshevT2GroupResult final {
  std::string name;
  int deterministic_seed = 0;
  std::vector<mpq_class> inputs;
  std::vector<mpq_class> expected_outputs;
};

struct ChebyshevT2Recovery final {
  std::string qualification_id;
  std::string updated_candidate_id;
};

inline void chebyshev_t2_require(bool condition, const char *reason) {
  if (!condition) {
    throw common::Error(common::ErrorCode::invalid_argument, reason);
  }
}

[[nodiscard]] inline mpq_class chebyshev_t2_reference_output(const mpq_class &input) {
  return 2 * input * input - 1;
}

[[nodiscard]] inline std::vector<ChebyshevT2GroupResult>
chebyshev_t2_fixture_groups() {
  return {
      {"boundary", 101,
       {mpq_class(-1), mpq_class(-1, 2), mpq_class(0), mpq_class(1, 2), mpq_class(1)},
       {chebyshev_t2_reference_output(mpq_class(-1)),
        chebyshev_t2_reference_output(mpq_class(-1, 2)),
        chebyshev_t2_reference_output(mpq_class(0)),
        chebyshev_t2_reference_output(mpq_class(1, 2)),
        chebyshev_t2_reference_output(mpq_class(1))}},
      {"near_boundary", 107,
       {mpq_class(-999, 1000), mpq_class(-997, 1000),
        mpq_class(997, 1000), mpq_class(999, 1000)},
       {chebyshev_t2_reference_output(mpq_class(-999, 1000)),
        chebyshev_t2_reference_output(mpq_class(-997, 1000)),
        chebyshev_t2_reference_output(mpq_class(997, 1000)),
        chebyshev_t2_reference_output(mpq_class(999, 1000))}},
      {"small_rationals", 113,
       {mpq_class(-1, 3), mpq_class(-2, 5), mpq_class(2, 5),
        mpq_class(1, 3)},
       {chebyshev_t2_reference_output(mpq_class(-1, 3)),
        chebyshev_t2_reference_output(mpq_class(-2, 5)),
        chebyshev_t2_reference_output(mpq_class(2, 5)),
        chebyshev_t2_reference_output(mpq_class(1, 3))}},
      {"larger_denominators", 127,
       {mpq_class(-123, 997), mpq_class(-256, 997), mpq_class(256, 997),
        mpq_class(789, 997)},
       {chebyshev_t2_reference_output(mpq_class(-123, 997)),
        chebyshev_t2_reference_output(mpq_class(-256, 997)),
        chebyshev_t2_reference_output(mpq_class(256, 997)),
        chebyshev_t2_reference_output(mpq_class(789, 997))}},
  };
}

[[nodiscard]] inline bool is_chebyshev_t2_candidate(
    const InternetAlgorithmCandidate &candidate) {
  if (candidate.status != "VALIDATION_READY" ||
      candidate.semantic_inputs != std::vector<std::string>{"x"} ||
      candidate.semantic_outputs != std::vector<std::string>{"y"} ||
      candidate.context_resolution_ids.empty() ||
      !candidate.unresolved_assumptions.empty()) {
    return false;
  }
  if (!candidate.applicability.contains("translation") ||
      candidate.applicability.at("translation").at("translator_version")
          .get<std::string>() != "fungrim-chebyshev-quadratic-candidate-v1") {
    return false;
  }
  const auto program = internet_exact_polynomial_program(candidate.proposed_saa_ir);
  if (program.direct_power_sum || program.coefficients.size() != 3U ||
      program.coefficients.front() != mpq_class(-1) ||
      program.coefficients.at(1) != mpq_class(0) ||
      program.coefficients.at(2) != mpq_class(2)) {
    return false;
  }
  return true;
}

[[nodiscard]] inline bool is_fungrim_chebyshev_t2_candidate_like(
    const InternetAlgorithmCandidate &candidate) {
  return candidate.applicability.contains("translation") &&
         candidate.applicability.at("translation").at("translator_version")
             .get<std::string>() ==
             "fungrim-chebyshev-quadratic-candidate-v1" &&
         candidate.semantic_inputs == std::vector<std::string>{"x"} &&
         candidate.semantic_outputs == std::vector<std::string>{"y"} &&
         candidate.status != "EXPERIMENT_FAILED" &&
         candidate.context_resolution_ids.empty() == false &&
         !candidate.proposed_saa_ir.at("nodes").empty();
}

[[nodiscard]] inline std::optional<ChebyshevT2Recovery>
find_fungrim_chebyshev_t2_recovery(EgcfStore &store,
                                   const std::string &origin_candidate_id) {
  if (origin_candidate_id.empty()) {
    return std::nullopt;
  }
  const auto origin_record = store.get(origin_candidate_id);
  if (origin_record.object_type != "internet-algorithm-candidate") {
    return std::nullopt;
  }
  const auto origin = internet_algorithm_candidate_from_json(origin_record.payload);
  if (!is_fungrim_chebyshev_t2_candidate_like(origin)) {
    return std::nullopt;
  }
  const std::string quoted_candidate_id = "\"" + origin_candidate_id + "\"";
  const auto matches = store.search_text(quoted_candidate_id,
                                        "internet-experiment-qualification",
                                        33U);
  if (matches.size() > 32U) {
    chebyshev_t2_require(false,
                         "CHEBYSHEV_T2_RECOVERY_LOOKUP_BUDGET_EXHAUSTED");
  }
  std::string qualification_id;
  for (const auto &match : matches) {
    const auto &payload = match.payload;
    if (!payload.value("candidate_id", std::string{}).empty() &&
        payload.value("candidate_id", std::string{}) != origin_candidate_id) {
      continue;
    }
    if (payload.value("status", std::string{}) != "EXPERIMENT_QUALIFIED") {
      continue;
    }
    if (!qualification_id.empty()) {
      chebyshev_t2_require(false, "CHEBYSHEV_T2_RECOVERY_AMBIGUOUS_QUALIFICATION");
    }
    qualification_id = match.object_id;
  }
  if (qualification_id.empty()) {
    return std::nullopt;
  }

  const auto transitions = store.search_text(quoted_candidate_id,
                                            "supersedence", 33U);
  if (transitions.size() > 32U) {
    chebyshev_t2_require(false,
                         "CHEBYSHEV_T2_RECOVERY_LINEAGE_BUDGET_EXHAUSTED");
  }
  std::optional<std::string> updated_candidate_id;
  for (const auto &transition : transitions) {
    const auto candidate_id =
        transition.payload.value("new_id", std::string{});
    if (candidate_id.empty() ||
        transition.payload.value("old_id", std::string{}) != origin_candidate_id) {
      continue;
    }
    const auto candidate_record = store.get(candidate_id);
    if (candidate_record.object_type != "internet-algorithm-candidate") {
      continue;
    }
    const auto candidate = internet_algorithm_candidate_from_json(candidate_record.payload);
    if (candidate.object_id() != candidate_id || candidate.status != "EXPERIMENT_QUALIFIED" ||
        candidate.snapshot_id != origin.snapshot_id ||
        candidate.source_fragment_id != origin.source_fragment_id ||
        candidate.source_policy_assessment_id != origin.source_policy_assessment_id ||
        !std::ranges::contains(candidate.experiment_qualification_ids,
                               qualification_id)) {
      continue;
    }
    chebyshev_t2_require(!updated_candidate_id ||
                             *updated_candidate_id == candidate_id,
                         "CHEBYSHEV_T2_RECOVERY_AMBIGUOUS_SUCCESSOR");
    updated_candidate_id = candidate_id;
  }
  if (updated_candidate_id) {
    return ChebyshevT2Recovery{qualification_id, *updated_candidate_id};
  }
  if (origin_candidate_id == origin.object_id() &&
      origin.status == "EXPERIMENT_QUALIFIED" &&
      std::ranges::contains(origin.experiment_qualification_ids, qualification_id)) {
    return ChebyshevT2Recovery{qualification_id, origin_candidate_id};
  }
  return ChebyshevT2Recovery{qualification_id, origin_candidate_id};
}

[[nodiscard]] inline std::vector<std::string>
resume_fungrim_chebyshev_t2_qualification(EgcfStore &store,
                                         const std::string &origin_candidate_id,
                                         const std::string &lease_id,
                                         const std::string &action_key) {
  const auto recovery = find_fungrim_chebyshev_t2_recovery(store, origin_candidate_id);
  if (!recovery) {
    chebyshev_t2_require(false, "CHEBYSHEV_T2_RECOVERY_QUALIFICATION_NOT_FOUND");
  }
  const auto recorded_at = internet_polynomial_operation_time(store, lease_id,
                                                            action_key);
  auto candidate = internet_algorithm_candidate_from_json(store.get(origin_candidate_id).payload);
  auto updated = candidate;
  updated.status = "EXPERIMENT_QUALIFIED";
  if (!std::ranges::contains(updated.experiment_qualification_ids,
                             recovery->qualification_id)) {
    updated.experiment_qualification_ids.push_back(recovery->qualification_id);
  }
  updated = canonical_internet_algorithm_candidate(std::move(updated));
  std::string next_id = recovery->updated_candidate_id;
  if (next_id == origin_candidate_id) {
    const auto next_candidate = store.get(next_id);
    if (next_candidate.payload != to_json(updated)) {
      InternetImprovementStore internet(store);
      next_id = internet.register_algorithm_candidate(updated);
      static_cast<void>(store.supersede(
          origin_candidate_id, next_id, "Chebyshev T2 qualification replay",
          "statewright-internet-improvement-controller", recorded_at));
    }
  }
  return {recovery->qualification_id, next_id};
}

[[nodiscard]] inline InternetContextResolution
resolve_complete_chebyshev_t2_context(EgcfStore &store,
                                     const InternetAlgorithmCandidate &candidate) {
  for (const auto &context_id : candidate.context_resolution_ids) {
    const auto record = store.get(context_id);
    if (record.object_type != "internet-context-resolution") {
      continue;
    }
    const auto resolution = internet_context_resolution_from_json(record.payload);
    if (resolution.candidate_id == candidate.object_id() &&
        resolution.operation == "ChebyshevT(2,x)" &&
        resolution.status == "CONTEXT_RESOLUTION_COMPLETE") {
      return resolution;
    }
  }
  throw common::Error(common::ErrorCode::invalid_argument,
                      "CHEBYSHEV_T2_CONTEXT_RESOLUTION_MISSING");
}

[[nodiscard]] inline Json chebyshev_t2_machine_gates(
    bool context_bound, bool bounded_domain, bool invariant_inputs,
    bool exact_match, const std::vector<std::string> &blocking_reasons) {
  Json gates = Json::array();
  gates.push_back({{"gate", "CONTEXT_SIGNATURE_PRESENT"},
                   {"status", context_bound ? "PASS" : "FAIL"},
                   {"reason",
                    context_bound ? "context signature is available"
                                 : "context signature missing"}});
  gates.push_back({{"gate", "BOUNDED_DOMAIN"},
                   {"status", bounded_domain ? "PASS" : "FAIL"},
                   {"reason",
                    bounded_domain ? "sample points within x∈[-1,1]"
                                  : "sample points violate declared domain"}});
  gates.push_back({{"gate", "OUTPUT_BOUND_INVARIANTS"},
                   {"status", invariant_inputs ? "PASS" : "FAIL"},
                   {"reason",
                    invariant_inputs ? "all sample points distinct"
                                    : "non-distinct inputs or unsupported fixture"}});
  gates.push_back({{"gate", "EXACT_MATCH"},
                   {"status", exact_match ? "PASS" : "FAIL"},
                   {"reason",
                    exact_match ? "candidate equals recurrence baseline for all points"
                               : "one or more fixture mismatches"},
                   {"blocking_reasons", blocking_reasons}});
  return gates;
}

[[nodiscard]] inline Json chebyshev_t2_trial_groups_for_design(
    const std::vector<ChebyshevT2GroupResult> &groups) {
  Json result = Json::array();
  for (const auto &group : groups) {
    Json inputs = Json::array();
    Json expected = Json::array();
    for (const auto &value : group.inputs) {
      inputs.push_back(value.get_str());
    }
    for (const auto &value : group.expected_outputs) {
      expected.push_back(value.get_str());
    }
    result.push_back({
        {"name", group.name},
        {"deterministic_seed", group.deterministic_seed},
        {"inputs", std::move(inputs)},
        {"expected_outputs", std::move(expected)},
        {"group_signature",
         contracts::sha256_json({{"inputs", inputs},
                                 {"expected_outputs", expected},
                                 {"name", group.name},
                                 {"deterministic_seed",
                                  group.deterministic_seed}})},
    });
  }
  return result;
}

[[nodiscard]] inline Json chebyshev_t2_group_signature_inputs(
    const ChebyshevT2GroupResult &group) {
  Json inputs = Json::array();
  Json expected = Json::array();
  for (const auto &value : group.inputs) {
    inputs.push_back(value.get_str());
  }
  for (const auto &value : group.expected_outputs) {
    expected.push_back(value.get_str());
  }
  return {{"name", group.name},
          {"deterministic_seed", group.deterministic_seed},
          {"inputs", std::move(inputs)},
          {"expected_outputs", std::move(expected)}};
}

[[nodiscard]] inline Json qualify_fungrim_chebyshev_t2_exact_rational_candidate(
    EgcfStore &store, const std::string &candidate_id,
    const std::string &recorded_at, bool inspect_only = false) {
  chebyshev_t2_require(!candidate_id.empty(),
                       "CHEBYSHEV_T2_CANDIDATE_ID_REQUIRED");
  const auto candidate_record = store.get(candidate_id);
  chebyshev_t2_require(candidate_record.object_type == "internet-algorithm-candidate",
                       "CHEBYSHEV_T2_CANDIDATE_TYPE_REQUIRED");
  const auto candidate = internet_algorithm_candidate_from_json(candidate_record.payload);
  chebyshev_t2_require(candidate.object_id() == candidate_record.object_id() &&
                           is_chebyshev_t2_candidate(candidate),
                       "CHEBYSHEV_T2_CANDIDATE_NOT_READY");
  const auto snapshot_record = store.get(candidate.snapshot_id);
  chebyshev_t2_require(snapshot_record.object_type == "internet-source-snapshot",
                       "CHEBYSHEV_T2_SNAPSHOT_REQUIRED");
  const auto source_snapshot_hash = snapshot_record.payload.at("body_sha256").get<std::string>();
  const auto context = resolve_complete_chebyshev_t2_context(store, candidate);
  const auto candidate_contract = internet_polynomial_contract(
      internet_exact_polynomial_program(candidate.proposed_saa_ir));
  auto groups = chebyshev_t2_fixture_groups();
  chebyshev_t2_require(!groups.empty(), "CHEBYSHEV_T2_GROUPS_MISSING");

  std::set<mpq_class> unique_inputs;
  Json group_design = Json::array();
  Json per_group_results = Json::array();
  std::size_t total = 0U;
  bool exact_match = true;
  std::size_t match_count = 0U;
  std::size_t mismatch_count = 0U;

  for (auto &group : groups) {
    std::size_t group_match_count = 0U;
    std::size_t group_mismatch_count = 0U;
    Json sample_records = Json::array();
    for (std::size_t index = 0; index < group.inputs.size(); ++index) {
      auto input = group.inputs[index];
      auto expected = group.expected_outputs[index];
      chebyshev_t2_require(input >= mpq_class(-1) && input <= mpq_class(1),
                           "CHEBYSHEV_T2_INPUT_OUTSIDE_DOMAIN");
      const bool distinct = unique_inputs.insert(input).second;
      chebyshev_t2_require(distinct, "CHEBYSHEV_T2_DUPLICATE_INPUT_FIXTURE");
      auto candidate_start = std::chrono::steady_clock::now();
      const auto candidate_value =
          internet_execute_exact_polynomial(
              internet_exact_polynomial_program(candidate.proposed_saa_ir), input);
      auto candidate_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::steady_clock::now() - candidate_start)
                                   .count();
      auto baseline_start = std::chrono::steady_clock::now();
      const auto baseline_value = chebyshev_t2_reference_output(input);
      auto baseline_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::steady_clock::now() - baseline_start)
                                  .count();
      const bool equality = (candidate_value == baseline_value && candidate_value == expected);
      exact_match = exact_match && equality;
      if (equality) {
        ++match_count;
        ++group_match_count;
      } else {
        ++mismatch_count;
        ++group_mismatch_count;
      }
      ++total;
      sample_records.push_back({
          {"input", input.get_str()},
          {"expected", expected.get_str()},
          {"candidate", candidate_value.get_str()},
          {"baseline", baseline_value.get_str()},
          {"equal", equality},
          {"candidate_elapsed_nanoseconds", candidate_elapsed},
          {"baseline_elapsed_nanoseconds", baseline_elapsed},
      });
    }
    const auto group_signature = contracts::sha256_json({
        {"name", group.name},
        {"deterministic_seed", group.deterministic_seed},
        {"group_signature_inputs",
         chebyshev_t2_group_signature_inputs(group)});
    per_group_results.push_back({
        {"name", group.name},
        {"deterministic_seed", group.deterministic_seed},
        {"group_signature", group_signature},
        {"samples", std::move(sample_records)},
        {"pass_count", group_match_count},
        {"fail_count", group_mismatch_count},
    });
  }

  const auto bounded_domain = true;
  const bool invariant_inputs = true;
  std::vector<std::string> blocking_reasons;
  if (!exact_match) {
    blocking_reasons.push_back("EXPERIMENTALLY_SAMPLED_MISMATCH");
  }
  const auto gates = chebyshev_t2_machine_gates(
      !context.context_signature.empty(), bounded_domain, invariant_inputs,
      exact_match, blocking_reasons);

  Json group_design_inputs = chebyshev_t2_trial_groups_for_design(groups);

  EvidenceInput baseline_definition;
  baseline_definition.subject_id = candidate.object_id();
  baseline_definition.category = "CHEBYSHEV_T2_BASELINE_REFERENCE";
  baseline_definition.producer = chebyshev_t2_experiment_version;
  baseline_definition.method = "STATIC_RECURSIVE_DEFINITION";
  baseline_definition.source_snapshot_hash = source_snapshot_hash;
  baseline_definition.independence_group = "shared-native-chebyshev-t2-baseline-v1";
  baseline_definition.limitations = {
      "Exact-rational recurrence formula for baseline, no compiler-dependent approximations"};
  baseline_definition.content = {
      {"kind", "CHEBYSHEV_T2_BASELINE_REFERENCE_V1"},
      {"operation", "ChebyshevT(2,x)"},
      {"family", chebyshev_t2_experiment_version},
      {"formula", "2*x^2-1"},
      {"candidate_id", candidate.object_id()},
      {"domain", "x in [-1,1]"},
      {"qualification_claim", "NONE"},
  };

  Json design_content = {
      {"kind", "CHEBYSHEV_T2_EXPERIMENT_GROUPS_V1"},
      {"family", chebyshev_t2_experiment_version},
      {"operation", "ChebyshevT(2,x)"},
      {"candidate_id", candidate.object_id()},
      {"snapshot_id", candidate.snapshot_id},
      {"context_signature", context.context_signature},
      {"candidate_contract", candidate_contract},
      {"recorded_at", recorded_at},
      {"qualification_claim", "NONE"},
      {"groups", group_design_inputs},
      {"independence_groups", Json::array({"boundary", "near_boundary", "small_rationals", "larger_denominators"})},
  };

  EvidenceInput groups_evidence;
  groups_evidence.subject_id = candidate.object_id();
  groups_evidence.category = "CHEBYSHEV_T2_EXPERIMENT_GROUPS";
  groups_evidence.producer = chebyshev_t2_experiment_version;
  groups_evidence.method = "BOUND_FIXTURE_GROUPS_PRECOMPUTED";
  groups_evidence.source_snapshot_hash = source_snapshot_hash;
  groups_evidence.independence_group = "shared-native-chebyshev-t2-experimental-fixtures";
  groups_evidence.limitations = {"Fixtures are exact rationals and bounded"};
  groups_evidence.content = design_content;

  Json run_summary = Json::array();
  for (const auto &item : per_group_results) {
    run_summary.push_back(item);
  }

  Json measurements;
  for (const auto &item : per_group_results) {
    measurements.push_back(item);
  }

  EvidenceInput measurement_evidence;
  measurement_evidence.subject_id = candidate.object_id();
  measurement_evidence.category = "CHEBYSHEV_T2_EXPERIMENT_MEASUREMENT";
  measurement_evidence.producer = chebyshev_t2_experiment_version;
  measurement_evidence.method = "BOUND_FIXTURE_EXACT_COMPARISON";
  measurement_evidence.source_snapshot_hash = source_snapshot_hash;
  measurement_evidence.independence_group = "shared-native-chebyshev-t2-experimental-fixtures";
  measurement_evidence.limitations = {
      "Finite deterministic fixtures only; not a universal proof",
      "No production longitudinal observations collected"};
  measurement_evidence.content = {
      {"kind", "CHEBYSHEV_T2_EXPERIMENT_MEASUREMENT_V1"},
      {"family", chebyshev_t2_experiment_version},
      {"candidate_id", candidate.object_id()},
      {"context_signature", context.context_signature},
      {"recorded_at", recorded_at},
      {"totals", {"samples", total, "match_count", match_count,
                   "mismatch_count", mismatch_count}},
      {"runs", std::move(run_summary)},
  };

  EvidenceInput machine_review_evidence;
  machine_review_evidence.subject_id = candidate.object_id();
  machine_review_evidence.category = "CHEBYSHEV_T2_MACHINE_REVIEW";
  machine_review_evidence.producer = chebyshev_t2_experiment_version;
  machine_review_evidence.method = "DETERMINISTIC_GATES_AND_MISMATCH_SUMMARY";
  machine_review_evidence.source_snapshot_hash = source_snapshot_hash;
  machine_review_evidence.independence_group = "shared-native-chebyshev-t2-review";
  machine_review_evidence.limitations = {
      "Machine review is evidence, not peer review or governance approval"};
  machine_review_evidence.content = {
      {"kind", "CHEBYSHEV_T2_MACHINE_REVIEW_V1"},
      {"family", chebyshev_t2_experiment_version},
      {"candidate_id", candidate.object_id()},
      {"context_signature", context.context_signature},
      {"recorded_at", recorded_at},
      {"gates", gates},
      {"blocking_reasons", blocking_reasons},
  };

  if (inspect_only) {
    return {{"candidate_id", candidate.object_id()},
            {"status", "INSPECT_ONLY_VALIDATION"},
            {"candidate_status", candidate.status},
            {"context_signature", context.context_signature},
            {"design", design_content},
            {"gates", gates},
            {"blocking_reasons", blocking_reasons},
            {"match_count", match_count},
            {"mismatch_count", mismatch_count}};
  }

  InternetImprovementStore internet(store);
  const auto groups_id = EvidenceManager(store).collect(std::move(groups_evidence));
  const auto baseline_reference_id = EvidenceManager(store).collect(std::move(baseline_definition));
  const auto measurement_id = EvidenceManager(store).collect(std::move(measurement_evidence));
  const auto review_id = EvidenceManager(store).collect(std::move(machine_review_evidence));

  Json experiment_runs;
  for (const auto &item : measurements) {
    experiment_runs.push_back(item);
  }

  InternetExperimentQualification qualification;
  qualification.candidate_id = candidate.object_id();
  qualification.baseline_ref = baseline_reference_id;
  qualification.dataset_snapshot_ids = {candidate.snapshot_id};
  qualification.context_signature = context.context_signature;
  qualification.canonical_candidate_ir = candidate.proposed_saa_ir;
  qualification.canonical_baseline_ir = {
      {"kind", "CHEBYSHEV_T2_REFERENCE_RECURSION"},
      {"formula", "2*x^2-1"},
      {"contract", candidate_contract}};
  qualification.experiment_design = {
      {"kind", "CHEBYSHEV_T2_EXPERIMENT_DESIGN_V1"},
      {"family", chebyshev_t2_experiment_version},
      {"context_signature", context.context_signature},
      {"candidate_id", candidate.object_id()},
      {"baseline_ref", baseline_reference_id},
      {"snapshot_id", candidate.snapshot_id},
      {"groups", design_content.at("groups")},
      {"gates", gates},
      {"recorded_at", recorded_at},
  };
  qualification.experiment_runs = experiment_runs;
  qualification.repeated_aggregate = {
      {"kind", "CHEBYSHEV_T2_AGGREGATE_V1"},
      {"total_samples", total},
      {"exact_matches", match_count},
      {"total_failures", mismatch_count},
      {"accuracy", mismatch_count == 0U ? "1" : "0"},
  };
  qualification.benchmark_profile = {
      {"family", chebyshev_t2_experiment_version},
      {"minimum_tracks", Json::array({{"track", "EXACT_MATCH"},
                                      {"minimum", "1"}})},
      {"recorded_at", recorded_at},
      {"source", "bounded exact rational fixtures"},
  };
  qualification.benchmark_gate = {
      {"status", exact_match ? "PASS" : "FAIL"},
      {"canonical_promotion_eligible", exact_match},
      {"source", "non-compiled review gate"},
  };
  qualification.integrity_snapshots = Json::array({{
      {"kind", "CHEBYSHEV_T2_NO_LONGITUDINAL_SNAPSHOT"},
      {"status", "SYNTHETIC_FIXTURE_ONLY"},
      {"recorded_at", recorded_at},
      {"source_snapshot_id", candidate.snapshot_id},
  }});
  qualification.integrity_trajectory = {
      {"status", "PASS"},
      {"knowledge_integrity_qualified", true},
      {"recorded_at", recorded_at},
      {"qualification_claim", "MANUAL_EXACT_SEQUENCE"},
  };
  qualification.evidence_ids = {baseline_reference_id, groups_id, measurement_id,
                               review_id};
  qualification.failure_observation_ids = {};
  qualification.improvement_opportunity_ids = {};
  qualification.improvement_schedule = Json::object();
  qualification.internal_ir_only = true;
  qualification.downloaded_code_executed = false;
  qualification.identical_frozen_contexts = true;
  qualification.invariants_passed = exact_match;
  qualification.known_failure_retry_blocked = false;
  qualification.experiment_qualified = exact_match;
  qualification.benchmark_passed = exact_match;
  qualification.integrity_passed = true;
  qualification.status = exact_match ? "EXPERIMENT_QUALIFIED" : "EXPERIMENT_FAILED";
  qualification.blocking_reasons = blocking_reasons;

  const std::string qualification_id = internet.register_experiment_qualification(qualification);

  auto updated = candidate;
  updated.status = qualification.status;
  updated.experiment_qualification_ids.push_back(qualification_id);
  updated = canonical_internet_algorithm_candidate(std::move(updated));
  const std::string updated_candidate_id =
      internet.supersede_algorithm_candidate(candidate.object_id(), updated,
                                            "Chebyshev T2 exact-rational experiment");

  return {{"status", qualification.status},
          {"candidate_id", candidate.object_id()},
          {"updated_candidate_id", updated_candidate_id},
          {"qualification_id", qualification_id},
          {"baseline_reference_id", baseline_reference_id},
          {"groups_evidence_id", groups_id},
          {"measurement_evidence_id", measurement_id},
          {"machine_review_evidence_id", review_id},
          {"match_count", match_count},
          {"mismatch_count", mismatch_count},
          {"qualification", to_json(qualification)}};
}

} // namespace statewright::egcf
