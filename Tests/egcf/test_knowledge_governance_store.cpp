#include "statewright/contracts/hash.hpp"
#include "statewright/egcf/knowledge_governance_store.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>

namespace {

std::filesystem::path temporary_directory() {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  auto path = std::filesystem::temp_directory_path() /
              ("statewright-knowledge-governance-" +
               std::to_string(suffix));
  std::filesystem::create_directories(path);
  return path;
}

statewright::egcf::EgcfRecord evidence(std::string group,
                                       std::string requirement) {
  using statewright::contracts::Json;
  const Json content = {{"group", group}, {"requirement", requirement}};
  return {.object_type = "egcf-evidence",
          .payload =
              {{"algorithm_id", "knowledge-governance-fixture"},
               {"category", "qualification"},
               {"claim_ids", Json::array()},
               {"command_id", "assurance.generate@1"},
               {"content", content},
               {"created_at", "2026-09-02T00:00:00Z"},
               {"environment", {{"suite", "knowledge-governance"}}},
               {"independence_group", std::move(group)},
               {"limitations", Json::array()},
               {"method", "direct-observation"},
               {"oracle", "deterministic-test-oracle"},
               {"path", ""},
               {"producer", "deterministic-saa-12-store-test"},
               {"requirement_ids", Json::array({std::move(requirement)})},
               {"sha256", statewright::contracts::sha256_json(content)},
               {"simulated", false},
               {"source_snapshot_hash",
                statewright::contracts::sha256_json({{"source", content}})},
               {"subject_id", "knowledge-governance:test"},
               {"success", true},
               {"target", "store"}}};
}

} // namespace

TEST_CASE("EGCF knowledge governance projection rebuilds immutable records") {
  using namespace statewright;
  const auto root = temporary_directory();
  egcf::EgcfStore egcf_store(root, STATEWRIGHT_RESOURCE_ROOT);
  egcf::KnowledgeGovernanceStore store(egcf_store);
  const auto evidence_id =
      egcf_store.register_record(evidence("schedule-a", "schedule"));
  const auto opportunity = saa::make_improvement_opportunity(
      store.evidence_resolver(), "benchmark-gap", "BENCHMARK_GAP",
      std::string(64, '3'), "investigate ProgressCert benchmark gap", 8500,
      7000, 7000, 2000, 1500, {evidence_id});
  REQUIRE(store.register_opportunity(opportunity).starts_with(
      "improvement-opportunity:sha256:"));
  const auto schedule = saa::schedule_improvements({opportunity});
  REQUIRE(store.register_schedule(schedule).starts_with(
      "improvement-schedule:sha256:"));
  const auto first = saa::make_integrity_snapshot(1, 20, 0, 0, 0, 0, 0,
                                                   10, 10, 10, 0);
  const auto second = saa::make_integrity_snapshot(2, 30, 0, 0, 0, 0, 0,
                                                    20, 20, 20, 0);
  static_cast<void>(store.register_integrity_snapshot(first));
  static_cast<void>(store.register_integrity_snapshot(second));
  const auto trajectory = saa::assess_integrity_trajectory({first, second});
  static_cast<void>(store.register_integrity_trajectory(trajectory));
  store.rebuild_projection();
  REQUIRE(store.list_objects("saa_improvement_opportunities",
                             "opportunity_ref")
              .size() == 1U);
  REQUIRE(store.list_objects("saa_improvement_schedules", "schedule_ref")
              .size() == 1U);
  REQUIRE(store.list_objects("saa_integrity_snapshots", "snapshot_ref")
              .size() == 2U);
  REQUIRE(store.list_objects("saa_integrity_trajectories", "trajectory_ref")
              .size() == 1U);
  std::filesystem::remove_all(root);
}
