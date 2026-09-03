#include "statewright/saa/oiec_bench_gate.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <ranges>
#include <set>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[noreturn]] void bench_error(std::string message) {
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

[[nodiscard]] std::string uppercase(std::string value) {
  value = trimmed(std::move(value));
  std::ranges::transform(value, value.begin(), [](const char character) {
    return static_cast<char>(
        std::toupper(static_cast<unsigned char>(character)));
  });
  return value;
}

[[nodiscard]] std::string lowercase_trimmed(std::string value) {
  value = trimmed(std::move(value));
  std::ranges::transform(value, value.begin(), [](const char character) {
    return static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  });
  return value;
}

[[nodiscard]] int basis_points(int value, std::string_view label) {
  if (value < 0 || value > 10000) {
    bench_error(std::string(label) +
                " must be integer basis points in 0..10000");
  }
  return value;
}

[[nodiscard]] bool exact_tracks(const std::map<std::string, int> &values) {
  if (values.size() != oiec_bench_tracks.size()) {
    return false;
  }
  return std::ranges::all_of(oiec_bench_tracks, [&](const auto track) {
    return values.contains(std::string(track));
  });
}

[[nodiscard]] Json scores_json(
    const std::vector<std::pair<std::string, int>> &values) {
  Json result = Json::object();
  for (const auto &[name, score] : values) {
    result[name] = score;
  }
  return result;
}

} // namespace

OIECBenchGatePolicy canonical_oiec_bench_policy(OIECBenchGatePolicy policy) {
  std::map<std::string, int> supplied;
  for (auto &[name, score] : policy.minimum_track_scores) {
    name = uppercase(std::move(name));
    supplied[name] = basis_points(score, "benchmark threshold " + name);
  }
  if (!exact_tracks(supplied)) {
    bench_error(
        "SAA-12.2 benchmark policy must define every OIEC-Bench track "
        "exactly once");
  }
  if (policy.minimum_independence_groups < 1 ||
      policy.minimum_independence_groups > 16) {
    bench_error(
        "SAA-12.2 minimum independence groups outside bounded range");
  }
  return {.minimum_track_scores = {supplied.begin(), supplied.end()},
          .minimum_independence_groups =
              policy.minimum_independence_groups};
}

OIECBenchProfile make_oiec_bench_profile(
    std::string candidate_ref, std::string benchmark_context_signature,
    std::vector<std::pair<std::string, int>> track_scores,
    std::vector<std::string> evidence_ids) {
  candidate_ref = trimmed(std::move(candidate_ref));
  if (candidate_ref.empty()) {
    bench_error("SAA-12.2 benchmark profile requires candidate_ref");
  }
  benchmark_context_signature =
      lowercase_trimmed(std::move(benchmark_context_signature));
  if (benchmark_context_signature.size() != 64U ||
      !std::ranges::all_of(benchmark_context_signature,
                           [](const char character) {
                             return (character >= '0' && character <= '9') ||
                                    (character >= 'a' && character <= 'f');
                           })) {
    bench_error("SAA-12.2 benchmark context must be SHA-256");
  }
  std::map<std::string, int> scores;
  for (auto &[name, score] : track_scores) {
    name = uppercase(std::move(name));
    scores[name] = basis_points(score, "benchmark score " + name);
  }
  if (!exact_tracks(scores)) {
    bench_error("SAA-12.2 benchmark profile must report every required "
                "track and no extras");
  }
  std::set<std::string> evidence;
  for (auto &evidence_id : evidence_ids) {
    evidence_id = trimmed(std::move(evidence_id));
    if (!evidence_id.empty()) {
      evidence.insert(std::move(evidence_id));
    }
  }
  if (evidence.empty()) {
    bench_error("SAA-12.2 benchmark profile requires evidence references");
  }
  const std::vector<std::pair<std::string, int>> canonical_scores(
      scores.begin(), scores.end());
  const std::vector<std::string> canonical_evidence(evidence.begin(),
                                                     evidence.end());
  const Json payload =
      {{"benchmark_context_signature", benchmark_context_signature},
       {"candidate_ref", candidate_ref},
       {"evidence_ids", canonical_evidence},
       {"track_scores", scores_json(canonical_scores)},
       {"version", oiec_bench_gate_version}};
  return {.candidate_ref = std::move(candidate_ref),
          .benchmark_context_signature =
              std::move(benchmark_context_signature),
          .track_scores = canonical_scores,
          .evidence_ids = canonical_evidence,
          .profile_signature = contracts::sha256_json(payload)};
}

OIECBenchGateAssessment qualify_oiec_bench_gate(
    const ReasoningEvidenceResolver &evidence_resolver,
    const OIECBenchProfile &profile, OIECBenchGatePolicy policy,
    bool independent_review) {
  policy = canonical_oiec_bench_policy(std::move(policy));
  std::set<std::string> required;
  for (const auto track : oiec_bench_tracks) {
    required.insert("oiec-bench:" + lowercase_trimmed(std::string(track)));
  }
  std::set<std::string> covered;
  std::set<std::string> groups;
  for (const auto &evidence_id : profile.evidence_ids) {
    std::optional<ReasoningGroundingEvidence> record;
    try {
      record = evidence_resolver(evidence_id);
    } catch (...) {
      record = std::nullopt;
    }
    if (!record) {
      bench_error("SAA-12.2 benchmark evidence is not registered: " +
                  evidence_id);
    }
    if (record->object_type != "egcf-evidence") {
      bench_error(
          "SAA-12.2 benchmark evidence must reference EvidenceArtifact");
    }
    if (record->success != true || record->simulated) {
      bench_error(
          "SAA-12.2 benchmark evidence must be successful and non-simulated");
    }
    if ((!record->producer.starts_with("deterministic-") &&
         !record->producer.starts_with("human-")) ||
        record->method == "reported") {
      bench_error(
          "SAA-12.2 benchmark evidence must be deterministic/human grounded");
    }
    for (const auto &requirement : record->requirement_ids) {
      covered.insert(lowercase_trimmed(requirement));
    }
    if (!record->independence_group.empty()) {
      groups.insert(record->independence_group);
    }
  }
  const int coverage =
      (10000 * static_cast<int>(std::ranges::count_if(
                   required, [&](const auto &requirement) {
                     return covered.contains(requirement);
                   }))) /
      static_cast<int>(required.size());

  const std::map<std::string, int> scores(profile.track_scores.begin(),
                                           profile.track_scores.end());
  const std::map<std::string, int> thresholds(
      policy.minimum_track_scores.begin(), policy.minimum_track_scores.end());
  std::vector<std::string> failures;
  for (const auto track : oiec_bench_tracks) {
    const std::string name(track);
    if (scores.at(name) < thresholds.at(name)) {
      failures.push_back(name + ":" + std::to_string(scores.at(name)) + "<" +
                         std::to_string(thresholds.at(name)));
    }
  }
  std::ranges::sort(failures);

  const std::string policy_signature = contracts::sha256_json(
      Json{{"policy", to_json(policy)}, {"version", oiec_bench_gate_version}});
  std::string status;
  if (coverage != 10000) {
    status = "OIEC_BENCH_EVIDENCE_INCOMPLETE";
  } else if (groups.size() <
             static_cast<std::size_t>(policy.minimum_independence_groups)) {
    status = "OIEC_BENCH_INDEPENDENCE_INSUFFICIENT";
  } else if (!independent_review) {
    status = "OIEC_BENCH_REVIEW_REQUIRED";
  } else if (!failures.empty()) {
    status = "OIEC_BENCH_THRESHOLD_FAILURE";
  } else {
    status = "OIEC_BENCH_PROMOTION_GATE_PASSED";
  }
  const bool eligible = status == "OIEC_BENCH_PROMOTION_GATE_PASSED";
  const std::vector<std::string> independence(groups.begin(), groups.end());
  const Json payload =
      {{"candidate_ref", profile.candidate_ref},
       {"coverage", coverage},
       {"failures", failures},
       {"groups", independence},
       {"independent_review", independent_review},
       {"policy_signature", policy_signature},
       {"profile_signature", profile.profile_signature},
       {"status", status},
       {"version", oiec_bench_gate_version}};
  return {.candidate_ref = profile.candidate_ref,
          .profile_signature = profile.profile_signature,
          .policy_signature = policy_signature,
          .evidence_requirement_coverage_bp = coverage,
          .independence_groups = independence,
          .threshold_failures = std::move(failures),
          .independent_review = independent_review,
          .status = std::move(status),
          .canonical_promotion_eligible = eligible,
          .assessment_signature = contracts::sha256_json(payload)};
}

Json to_json(const OIECBenchGatePolicy &value) {
  return {{"minimum_independence_groups", value.minimum_independence_groups},
          {"minimum_track_scores", scores_json(value.minimum_track_scores)}};
}

Json to_json(const OIECBenchProfile &value) {
  return {{"benchmark_context_signature", value.benchmark_context_signature},
          {"candidate_ref", value.candidate_ref},
          {"evidence_ids", value.evidence_ids},
          {"profile_signature", value.profile_signature},
          {"track_scores", scores_json(value.track_scores)}};
}

Json to_json(const OIECBenchGateAssessment &value) {
  return {{"assessment_signature", value.assessment_signature},
          {"candidate_ref", value.candidate_ref},
          {"canonical_promotion_eligible",
           value.canonical_promotion_eligible},
          {"evidence_requirement_coverage_bp",
           value.evidence_requirement_coverage_bp},
          {"independence_groups", value.independence_groups},
          {"independent_review", value.independent_review},
          {"policy_signature", value.policy_signature},
          {"profile_signature", value.profile_signature},
          {"status", value.status},
          {"threshold_failures", value.threshold_failures}};
}

} // namespace statewright::saa
