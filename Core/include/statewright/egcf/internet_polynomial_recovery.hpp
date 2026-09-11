#pragma once

#include "statewright/egcf/internet_polynomial_operation.hpp"
#include "statewright/egcf/internet_polynomial_store.hpp"

#include <optional>

namespace statewright::egcf {

struct InternetPolynomialRecovery final {
  std::string qualification_id;
  InternetAlgorithmCandidate qualified_candidate;
};

// Discovery is read-only: the orchestrator must acquire/validate ownership of
// its recovery lease before materializing the missing lifecycle writes.
[[nodiscard]] inline std::optional<InternetPolynomialRecovery>
find_internet_polynomial_recovery(EgcfStore &store,
                                 const std::string &origin_candidate_id,
                                 const std::string &protocol_id) {
  const auto origin = store.get(origin_candidate_id);
  if (origin.object_type != "internet-algorithm-candidate") return std::nullopt;
  const auto candidate = internet_algorithm_candidate_from_json(origin.payload);
  if (!candidate.proposed_saa_ir.contains("metadata") ||
      !candidate.proposed_saa_ir.at("metadata").is_object() ||
      !candidate.proposed_saa_ir.at("metadata").contains("internet_exact_polynomial")) {
    return std::nullopt;
  }
  const auto protocol_record = store.get(protocol_id);
  if (protocol_record.object_type != "internet-experiment-protocol") {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_RECOVERY_PROTOCOL_TYPE_MISMATCH");
  }
  const auto protocol = internet_experiment_protocol_from_json(protocol_record.payload);
  if (protocol.protocol_version != internet_polynomial_protocol_version) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_RECOVERY_REQUIRES_POLYNOMIAL_PROTOCOL");
  }
  // IDs have already passed native typed-ID validation. Quote them as FTS
  // phrases, bound materialization, then require exact structured bindings.
  const auto matches = store.search_text(
      "\"" + origin_candidate_id + "\"", "internet-experiment-qualification", 33);
  if (matches.size() > 32U) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_RECOVERY_LOOKUP_BUDGET_EXHAUSTED");
  }
  std::optional<InternetPolynomialRecovery> result;
  for (const auto &record : matches) {
    const auto &payload = record.payload;
    if (payload.at("candidate_id").get<std::string>() != origin_candidate_id ||
        payload.at("status") != "EXPERIMENT_QUALIFIED" ||
        payload.at("experiment_design").value("grounded_protocol_id", std::string{}) != protocol_id) {
      continue;
    }
    if (result) {
      throw common::Error(common::ErrorCode::invalid_argument,
                          "POLYNOMIAL_RECOVERY_AMBIGUOUS_QUALIFICATIONS");
    }
    auto qualified = candidate;
    qualified.status = "EXPERIMENT_QUALIFIED";
    qualified.experiment_qualification_ids.push_back(record.object_id);
    qualified = canonical_internet_algorithm_candidate(std::move(qualified));
    verify_internet_polynomial_qualification_binding(store, qualified, record.object_id);
    result = InternetPolynomialRecovery{record.object_id, std::move(qualified)};
  }
  return result;
}

[[nodiscard]] inline std::vector<std::string>
resume_internet_polynomial_qualification(
    EgcfStore &store, const std::string &origin_candidate_id,
    const std::string &protocol_id, const std::string &lease_id,
    const std::string &action_key) {
  const auto recovery = find_internet_polynomial_recovery(
      store, origin_candidate_id, protocol_id);
  if (!recovery) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_RECOVERY_QUALIFICATION_NOT_FOUND");
  }
  const auto recorded_at = internet_polynomial_operation_time(store, lease_id, action_key);
  const auto next_id = recovery->qualified_candidate.object_id();
  const auto transitions = store.search_text(
      "\"" + origin_candidate_id + "\"", "supersedence", 33);
  if (transitions.size() > 32U) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "POLYNOMIAL_RECOVERY_LINEAGE_BUDGET_EXHAUSTED");
  }
  bool linked = false;
  for (const auto &transition : transitions) {
    if (transition.payload.at("old_id").get<std::string>() != origin_candidate_id) continue;
    if (transition.payload.at("new_id").get<std::string>() != next_id) {
      throw common::Error(common::ErrorCode::invalid_argument,
                          "POLYNOMIAL_RECOVERY_COMPETING_SUCCESSOR");
    }
    linked = true;
  }
  InternetImprovementStore internet(store);
  static_cast<void>(internet.register_algorithm_candidate(recovery->qualified_candidate));
  if (!linked) {
    static_cast<void>(store.supersede(
        origin_candidate_id, next_id, "internet experiment qualified",
        "statewright-internet-improvement-controller", recorded_at));
  }
  const auto checkpoint = checkpoint_qualified_internet_polynomial_candidate(
      store, next_id, recovery->qualification_id, recorded_at);
  return {recovery->qualification_id, checkpoint.candidate_id, checkpoint.form_id};
}

} // namespace statewright::egcf
