#pragma once

#include "statewright/saa/reasoning_outcome.hpp"

#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view oiec_bench_gate_version =
    "saa-oiec-bench-gate-v1";
inline constexpr std::array<std::string_view, 7U> oiec_bench_tracks = {
    "TRUTHGROUND", "MEANINGPATH", "SEMANTICREP", "MEANINGGROUND",
    "WORKGROUND", "PROGRESSCERT", "AGENTWORK"};

struct OIECBenchGatePolicy final {
  std::vector<std::pair<std::string, int>> minimum_track_scores;
  int minimum_independence_groups = 2;
};

struct OIECBenchProfile final {
  std::string candidate_ref;
  std::string benchmark_context_signature;
  std::vector<std::pair<std::string, int>> track_scores;
  std::vector<std::string> evidence_ids;
  std::string profile_signature;
};

struct OIECBenchGateAssessment final {
  std::string candidate_ref;
  std::string profile_signature;
  std::string policy_signature;
  int evidence_requirement_coverage_bp = 0;
  std::vector<std::string> independence_groups;
  std::vector<std::string> threshold_failures;
  bool independent_review = false;
  std::string status;
  bool canonical_promotion_eligible = false;
  std::string assessment_signature;
};

[[nodiscard]] OIECBenchGatePolicy
canonical_oiec_bench_policy(OIECBenchGatePolicy policy);
[[nodiscard]] OIECBenchProfile make_oiec_bench_profile(
    std::string candidate_ref, std::string benchmark_context_signature,
    std::vector<std::pair<std::string, int>> track_scores,
    std::vector<std::string> evidence_ids);
[[nodiscard]] OIECBenchGateAssessment qualify_oiec_bench_gate(
    const ReasoningEvidenceResolver &evidence_resolver,
    const OIECBenchProfile &profile, OIECBenchGatePolicy policy,
    bool independent_review);

[[nodiscard]] contracts::Json to_json(const OIECBenchGatePolicy &value);
[[nodiscard]] contracts::Json to_json(const OIECBenchProfile &value);
[[nodiscard]] contracts::Json
to_json(const OIECBenchGateAssessment &value);

} // namespace statewright::saa
