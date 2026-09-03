#include "statewright/saa/knowledge_integrity.hpp"
#include "statewright/saa/oiec_bench_gate.hpp"
#include "statewright/saa/promotion_governance.hpp"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view context_signature =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr std::string_view candidate_ref =
    "adapted-candidate:sha256:"
    "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";

std::vector<std::pair<std::string, int>> track_scores(int progress = 9000) {
  using statewright::saa::oiec_bench_tracks;
  std::vector<std::pair<std::string, int>> result;
  for (const auto track : oiec_bench_tracks) {
    result.emplace_back(track, track == "PROGRESSCERT" ? progress : 9000);
  }
  return result;
}

statewright::saa::OIECBenchGatePolicy policy() {
  using statewright::saa::oiec_bench_tracks;
  std::vector<std::pair<std::string, int>> scores;
  for (const auto track : oiec_bench_tracks) {
    scores.emplace_back(track, 8000);
  }
  return {.minimum_track_scores = std::move(scores),
          .minimum_independence_groups = 2};
}

statewright::saa::ReasoningEvidenceResolver evidence_resolver() {
  using statewright::saa::ReasoningGroundingEvidence;
  std::vector<std::string> requirements;
  for (const auto track : statewright::saa::oiec_bench_tracks) {
    std::string name(track);
    for (char &character : name) {
      character = static_cast<char>(
          std::tolower(static_cast<unsigned char>(character)));
    }
    requirements.push_back("oiec-bench:" + name);
  }
  const std::map<std::string, ReasoningGroundingEvidence> evidence = {
      {"evidence:a",
       {.object_type = "egcf-evidence",
        .success = true,
        .simulated = false,
        .producer = "deterministic-saa-12-governance-test",
        .method = "controlled-governance-fixture",
        .requirement_ids = requirements,
        .independence_group = "bench-a"}},
      {"evidence:b",
       {.object_type = "egcf-evidence",
        .success = true,
        .simulated = false,
        .producer = "deterministic-saa-12-governance-test",
        .method = "controlled-governance-fixture",
        .requirement_ids = requirements,
        .independence_group = "bench-b"}},
      {"evidence:partial",
       {.object_type = "egcf-evidence",
        .success = true,
        .simulated = false,
        .producer = "deterministic-saa-12-governance-test",
        .method = "controlled-governance-fixture",
        .requirement_ids = {requirements.begin(), requirements.begin() + 3},
        .independence_group = "bench-a"}}};
  return [evidence](std::string_view evidence_id)
             -> std::optional<ReasoningGroundingEvidence> {
    const auto found = evidence.find(std::string(evidence_id));
    return found == evidence.end()
               ? std::nullopt
               : std::optional<ReasoningGroundingEvidence>(found->second);
  };
}

statewright::saa::OIECBenchProfile passing_profile() {
  return statewright::saa::make_oiec_bench_profile(
      std::string(candidate_ref), std::string(context_signature),
      track_scores(), {"evidence:b", "evidence:a"});
}

} // namespace

TEST_CASE("SAA OIEC-Bench profile and promotion gate match oracle") {
  using namespace statewright::saa;
  const auto profile = passing_profile();
  REQUIRE(profile.profile_signature ==
          "f743991eb87f090f3c87cfcfe60c9349f4a2a4d996724ba302ee654e4b98bb28");
  const auto gate = qualify_oiec_bench_gate(evidence_resolver(), profile,
                                             policy(), true);
  REQUIRE(gate.status == "OIEC_BENCH_PROMOTION_GATE_PASSED");
  REQUIRE(gate.canonical_promotion_eligible);
  REQUIRE(gate.evidence_requirement_coverage_bp == 10000);
  REQUIRE(gate.policy_signature ==
          "6e47d43d2f5f29e7ec978413d1c9b091d6fa4291a57c7ad2a4a50356faa97fa4");
  REQUIRE(gate.assessment_signature ==
          "466ad76ddc7051a0e768f1fe590ed1d256a95813bd382e6b92452122d826d47f");
}

TEST_CASE("SAA OIEC-Bench gate preserves fail-closed precedence") {
  using namespace statewright::saa;
  const auto weak_profile = make_oiec_bench_profile(
      std::string(candidate_ref), std::string(context_signature),
      track_scores(7000), {"evidence:a", "evidence:b"});
  REQUIRE(weak_profile.profile_signature ==
          "6db45bfd806dbed6d87db3ff25cf66d4524e098e240e5a9c3f7949c05dbcf0cb");
  const auto weak = qualify_oiec_bench_gate(evidence_resolver(), weak_profile,
                                             policy(), true);
  REQUIRE(weak.status == "OIEC_BENCH_THRESHOLD_FAILURE");
  REQUIRE(weak.threshold_failures ==
          std::vector<std::string>{"PROGRESSCERT:7000<8000"});
  REQUIRE(weak.assessment_signature ==
          "26e5379eb43d453d1ad38fca9b1bcaa740e69e183b6cc959a7e97a7d0b6f586a");

  const auto partial_profile = make_oiec_bench_profile(
      std::string(candidate_ref), std::string(context_signature),
      track_scores(), {"evidence:partial"});
  const auto partial = qualify_oiec_bench_gate(
      evidence_resolver(), partial_profile, policy(), true);
  REQUIRE(partial.status == "OIEC_BENCH_EVIDENCE_INCOMPLETE");
  REQUIRE(partial.evidence_requirement_coverage_bp == 4285);
  REQUIRE(partial.assessment_signature ==
          "b3ed8e088c48d4ffbac7a26930e42651eb7c50ca774c1d830731ec2d0428a6f3");

  const auto review = qualify_oiec_bench_gate(
      evidence_resolver(), passing_profile(), policy(), false);
  REQUIRE(review.status == "OIEC_BENCH_REVIEW_REQUIRED");
  REQUIRE(review.assessment_signature ==
          "da96f5163f347329db36a4b33c38ec497f020bb60e9a4645662a0ea7d83cc4fc");
}

TEST_CASE("SAA knowledge integrity trajectory matches oracle") {
  using namespace statewright::saa;
  const auto first = make_integrity_snapshot(10, 100, 4, 4, 0, 20, 2,
                                              20, 18, 10, 1);
  const auto second = make_integrity_snapshot(11, 120, 2, 2, 0, 30, 1,
                                               30, 29, 20, 1);
  REQUIRE(first.snapshot_signature ==
          "d811ba837b236d4d2a0305a2c3ccd381f1063dc754fdf4dbe6f4c8cae9e03984");
  REQUIRE(second.snapshot_signature ==
          "22b0149d6a03b1aa0bebe837b9218f2b3b7d0a2c999b385b92ce09dcdc12c425");
  const auto trajectory = assess_integrity_trajectory({second, first});
  REQUIRE(trajectory.status == "KNOWLEDGE_INTEGRITY_QUALIFIED_IMPROVING");
  REQUIRE(trajectory.knowledge_integrity_qualified);
  REQUIRE(trajectory.improved_dimensions ==
          std::vector<std::string>{
              "CONTRADICTION_RATE_BP",
              "CORRECTED_ERROR_RECURRENCE_RATE_BP",
              "EQUIVALENT_FAILURE_AVOIDANCE_BP", "RETRIEVAL_PRECISION_BP",
              "SEMANTIC_DRIFT_RATE_BP"});
  REQUIRE(trajectory.trajectory_signature ==
          "53c00f2e8a8b4f8222178e34deeaf05062484d7eaaa51f9187c040eb7d3aad66");
}

TEST_CASE("SAA knowledge integrity rejects false canonical admission") {
  using namespace statewright::saa;
  const auto snapshot =
      make_integrity_snapshot(2, 100, 0, 0, 1, 0, 0, 10, 10, 10, 0);
  REQUIRE(snapshot.snapshot_signature ==
          "e0d31d28ff8767e5b224d64169893bee79a4769df2276655918f2ef5c0b02c53");
  const auto trajectory = assess_integrity_trajectory({snapshot});
  REQUIRE(trajectory.status == "KNOWLEDGE_INTEGRITY_POLICY_VIOLATION");
  REQUIRE_FALSE(trajectory.knowledge_integrity_qualified);
  REQUIRE(trajectory.policy_violations ==
          std::vector<std::string>{"FALSE_ADMISSION_RATE:100:MAX:0"});
  REQUIRE(trajectory.trajectory_signature ==
          "a60cdb3199f5348a23ff8e90363168ae04a1e5c86deccb9486abc7a448184b8a");
}

TEST_CASE("SAA canonical promotion governance requires both gates") {
  using namespace statewright::saa;
  const auto gate = qualify_oiec_bench_gate(
      evidence_resolver(), passing_profile(), policy(), true);
  const auto snapshot =
      make_integrity_snapshot(5, 100, 0, 0, 0, 0, 0, 20, 20, 20, 0);
  const auto integrity = assess_integrity_trajectory({snapshot});
  REQUIRE(snapshot.snapshot_signature ==
          "7836ecc40f2a8fda9cb3f1995a1bb7f5e1a4e630e3890ceeb12d67221f205304");
  REQUIRE(integrity.trajectory_signature ==
          "8bf1bc77ef31e32fb0bb018508416b779c849a43742a62207454ff34a84c536d");
  const auto passed = assess_canonical_promotion_governance(
      std::string(candidate_ref), &gate, &integrity);
  REQUIRE(passed.status == "CANONICAL_PROMOTION_GOVERNANCE_PASSED");
  REQUIRE(passed.canonical_promotion_allowed);
  REQUIRE(passed.assessment_signature ==
          "d0235b3b2530c07c43514b26e497f4ebbbff98845dea77c7db81fcd9e3b7bc53");

  const auto missing =
      assess_canonical_promotion_governance(std::string(candidate_ref));
  REQUIRE(missing.blocking_reasons ==
          std::vector<std::string>{"OIEC_BENCH_GATE_MISSING",
                                   "KNOWLEDGE_INTEGRITY_GATE_MISSING"});
  REQUIRE(missing.assessment_signature ==
          "2209672e04c4eed3308a611e3eab297f10e9643cf2f8822ea17a892f9dbcb06a");
}
