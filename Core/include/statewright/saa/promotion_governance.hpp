#pragma once

#include "statewright/saa/knowledge_integrity.hpp"
#include "statewright/saa/oiec_bench_gate.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view promotion_governance_version =
    "saa-canonical-promotion-governance-v1";

struct CanonicalPromotionGovernanceAssessment final {
  std::string candidate_ref;
  std::string benchmark_gate_signature;
  std::string integrity_trajectory_signature;
  bool benchmark_required = true;
  bool integrity_required = true;
  std::vector<std::string> blocking_reasons;
  std::string status;
  bool canonical_promotion_allowed = false;
  std::string assessment_signature;
};

[[nodiscard]] CanonicalPromotionGovernanceAssessment
assess_canonical_promotion_governance(
    std::string candidate_ref,
    const OIECBenchGateAssessment *benchmark_gate = nullptr,
    const KnowledgeIntegrityTrajectory *integrity_trajectory = nullptr,
    bool require_benchmark_gate = true, bool require_integrity_gate = true);

[[nodiscard]] contracts::Json
to_json(const CanonicalPromotionGovernanceAssessment &value);

} // namespace statewright::saa
