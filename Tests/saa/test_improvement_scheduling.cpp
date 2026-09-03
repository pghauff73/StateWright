#include "statewright/saa/improvement_scheduling.hpp"

#include "statewright/common/error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

statewright::saa::ReasoningEvidenceResolver evidence_resolver() {
  using statewright::saa::ReasoningGroundingEvidence;
  const std::map<std::string, ReasoningGroundingEvidence> evidence = {
      {"evidence:a",
       {.object_type = "egcf-evidence",
        .success = true,
        .simulated = false,
        .producer = "deterministic-saa-12-governance-test",
        .method = "controlled-governance-fixture",
        .requirement_ids = {"schedule"},
        .independence_group = "schedule-a"}},
      {"evidence:b",
       {.object_type = "egcf-evidence",
        .success = true,
        .simulated = false,
        .producer = "deterministic-saa-12-governance-test",
        .method = "controlled-governance-fixture",
        .requirement_ids = {"schedule"},
        .independence_group = "schedule-b"}},
      {"evidence:c",
       {.object_type = "egcf-evidence",
        .success = true,
        .simulated = false,
        .producer = "deterministic-saa-12-governance-test",
        .method = "controlled-governance-fixture",
        .requirement_ids = {"schedule"},
        .independence_group = "schedule-c"}}};
  return [evidence](std::string_view evidence_id)
             -> std::optional<ReasoningGroundingEvidence> {
    const auto found = evidence.find(std::string(evidence_id));
    return found == evidence.end()
               ? std::nullopt
               : std::optional<ReasoningGroundingEvidence>(found->second);
  };
}

} // namespace

TEST_CASE("SAA improvement scheduling prioritizes evidence within budgets") {
  using namespace statewright::saa;
  const auto resolver = evidence_resolver();
  const auto good = make_improvement_opportunity(
      resolver, "fix-repeated-failure", "FAILURE_PATTERN",
      std::string(64, '1'), "investigate repeated solver failure", 9500,
      9000, 8000, 2500, 2000, {"evidence:a"});
  const auto risky = make_improvement_opportunity(
      resolver, "high-risk-rewrite", "INTEGRITY_SIGNAL",
      std::string(64, '2'), "investigate whole-store rewrite", 10000,
      10000, 10000, 3000, 9000, {"evidence:b"});
  const auto modest = make_improvement_opportunity(
      resolver, "benchmark-gap", "BENCHMARK_GAP", std::string(64, '3'),
      "investigate ProgressCert benchmark gap", 8500, 7000, 7000, 2000,
      1500, {"evidence:c"});

  REQUIRE(good.priority_bp == 9425);
  REQUIRE(good.opportunity_signature ==
          "ac4182fc045eeec690dead09ef0fe04e91f3fb51ee2911dc0a70f5e6a42991c6");
  REQUIRE(risky.opportunity_signature ==
          "acc610c51827de68218af99e32e06a0f5e0e17bca08ba8ef6723e2831ef11d19");
  REQUIRE(modest.opportunity_signature ==
          "bc24a5ccddae29c4971bdb3dfa8f5eef0f59f9786d4d6e08cbdabacf34730395");

  const auto schedule = schedule_improvements(
      {risky, modest, good},
      {.max_selected = 2,
       .total_cost_budget_bp = 5000,
       .maximum_risk_bp = 6000,
       .minimum_priority_bp = 1000});
  REQUIRE(schedule.status == "IMPROVEMENT_INVESTIGATIONS_SCHEDULED");
  REQUIRE(schedule.total_allocated_cost_bp == 4500);
  REQUIRE(schedule.selected.size() == 2U);
  REQUIRE(schedule.selected[0].opportunity_id == "fix-repeated-failure");
  REQUIRE(schedule.selected[1].opportunity_id == "benchmark-gap");
  REQUIRE(schedule.deferred ==
          std::vector<std::pair<std::string, std::string>>{
              {"high-risk-rewrite", "RISK_CEILING_EXCEEDED"}});
  REQUIRE(schedule.schedule_signature ==
          "86faf12211d7eae7e3d4c6153db2a627fe593868041227d28d56312719fb6b31");
}

TEST_CASE("SAA improvement scheduling rejects ungrounded evidence") {
  using namespace statewright::saa;
  REQUIRE_THROWS_AS(
      make_improvement_opportunity(
          evidence_resolver(), "missing", "RETRIEVAL_GAP",
          std::string(64, '4'), "investigate missing fit", 8000, 8000,
          8000, 1000, 1000, {"evidence:missing"}),
      statewright::common::Error);
}
