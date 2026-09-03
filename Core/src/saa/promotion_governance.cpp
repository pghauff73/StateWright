#include "statewright/saa/promotion_governance.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[noreturn]] void promotion_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

[[nodiscard]] std::string trimmed(std::string value) {
  const auto first = value.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\n\r\f\v");
  return value.substr(first, last - first + 1U);
}

} // namespace

CanonicalPromotionGovernanceAssessment assess_canonical_promotion_governance(
    std::string candidate_ref,
    const OIECBenchGateAssessment *benchmark_gate,
    const KnowledgeIntegrityTrajectory *integrity_trajectory,
    bool require_benchmark_gate, bool require_integrity_gate) {
  candidate_ref = trimmed(std::move(candidate_ref));
  if (candidate_ref.empty()) {
    promotion_error("canonical promotion governance requires candidate_ref");
  }
  std::vector<std::string> blockers;
  std::string benchmark_signature;
  std::string integrity_signature;
  if (require_benchmark_gate) {
    if (benchmark_gate == nullptr) {
      blockers.emplace_back("OIEC_BENCH_GATE_MISSING");
    } else {
      if (benchmark_gate->candidate_ref != candidate_ref) {
        promotion_error("OIEC-Bench gate belongs to a different candidate");
      }
      benchmark_signature = benchmark_gate->assessment_signature;
      if (!benchmark_gate->canonical_promotion_eligible) {
        blockers.push_back("OIEC_BENCH_GATE:" + benchmark_gate->status);
      }
    }
  } else if (benchmark_gate != nullptr) {
    if (benchmark_gate->candidate_ref != candidate_ref) {
      promotion_error("OIEC-Bench gate belongs to a different candidate");
    }
    benchmark_signature = benchmark_gate->assessment_signature;
  }
  if (require_integrity_gate) {
    if (integrity_trajectory == nullptr) {
      blockers.emplace_back("KNOWLEDGE_INTEGRITY_GATE_MISSING");
    } else {
      integrity_signature = integrity_trajectory->trajectory_signature;
      if (!integrity_trajectory->knowledge_integrity_qualified) {
        blockers.push_back("KNOWLEDGE_INTEGRITY:" +
                           integrity_trajectory->status);
      }
    }
  } else if (integrity_trajectory != nullptr) {
    integrity_signature = integrity_trajectory->trajectory_signature;
  }
  const bool allowed = blockers.empty();
  const std::string status = allowed
                                 ? "CANONICAL_PROMOTION_GOVERNANCE_PASSED"
                                 : "CANONICAL_PROMOTION_GOVERNANCE_BLOCKED";
  const Json payload =
      {{"benchmark_gate_signature", benchmark_signature},
       {"benchmark_required", require_benchmark_gate},
       {"blocking_reasons", blockers},
       {"candidate_ref", candidate_ref},
       {"integrity_required", require_integrity_gate},
       {"integrity_trajectory_signature", integrity_signature},
       {"status", status},
       {"version", promotion_governance_version}};
  return {.candidate_ref = std::move(candidate_ref),
          .benchmark_gate_signature = std::move(benchmark_signature),
          .integrity_trajectory_signature = std::move(integrity_signature),
          .benchmark_required = require_benchmark_gate,
          .integrity_required = require_integrity_gate,
          .blocking_reasons = std::move(blockers),
          .status = status,
          .canonical_promotion_allowed = allowed,
          .assessment_signature = contracts::sha256_json(payload)};
}

Json to_json(const CanonicalPromotionGovernanceAssessment &value) {
  return {{"assessment_signature", value.assessment_signature},
          {"benchmark_gate_signature", value.benchmark_gate_signature},
          {"benchmark_required", value.benchmark_required},
          {"blocking_reasons", value.blocking_reasons},
          {"candidate_ref", value.candidate_ref},
          {"canonical_promotion_allowed",
           value.canonical_promotion_allowed},
          {"integrity_required", value.integrity_required},
          {"integrity_trajectory_signature",
           value.integrity_trajectory_signature},
          {"status", value.status}};
}

} // namespace statewright::saa
