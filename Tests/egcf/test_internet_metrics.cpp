#include "statewright/egcf/internet_metrics.hpp"

#include <catch2/catch_test_macros.hpp>

namespace {
using Json = statewright::contracts::Json;
void record(statewright::egcf::InternetImprovementState &state,
            const std::string &id, const std::string &type, Json payload) {
  state.internet_records.push_back({.object_id = id,
                                    .object_type = type,
                                    .digest = {},
                                    .payload = std::move(payload),
                                    .relative_path = {}});
}
} // namespace

TEST_CASE(
    "internet metrics distinguish current states from lifetime admissions") {
  using namespace statewright::egcf;
  InternetImprovementState state;
  state.event_head = "fixture-head";
  state.projection_digest = "fixture-digest";
  state.active_candidate_ids = {"current", "quarantined"};
  record(state, "old", "internet-algorithm-candidate",
         {{"status", "VALIDATION_READY"}});
  record(
      state, "current", "internet-algorithm-candidate",
      {{"status", "CANONICAL"}, {"canonical_algorithm_ids", {"algorithm-1"}}});
  record(state, "quarantined", "internet-algorithm-candidate",
         {{"status", "QUARANTINED"},
          {"unresolved_assumptions", {"UNSUPPORTED", "UNSUPPORTED"}}});
  record(state, "fetch-1", "internet-fetch-receipt",
         {{"status", "FETCH_SUCCEEDED"},
          {"compressed_bytes", 50},
          {"decompressed_bytes", 100},
          {"total_time_milliseconds", 30}});
  record(state, "fetch-2", "internet-fetch-receipt",
         {{"status", "NOT_MODIFIED"},
          {"compressed_bytes", 0},
          {"decompressed_bytes", 0},
          {"total_time_milliseconds", 10}});
  record(state, "fetch-3", "internet-fetch-receipt",
         {{"status", "FETCH_FAILED"}, {"failure_reason", "timeout"}});
  record(state, "admission-1", "internet-probation-admission",
         {{"admission_status", "PROBATIONARY_CANONICAL"},
          {"canonical_algorithm_ref", "algorithm-1"}});
  record(state, "admission-2", "internet-probation-admission",
         {{"admission_status", "PROBATIONARY_CANONICAL"},
          {"canonical_algorithm_ref", "algorithm-1"}});
  record(state, "qualification", "internet-experiment-qualification",
         {{"status", "EXPERIMENT_QUALIFIED"},
          {"blocking_reasons", Json::array()}});
  record(state, "duplicate", "internet-retrieval-receipt",
         {{"novelty_status", "DUPLICATE"},
          {"exact_match_ids", {"algorithm-1"}},
          {"equivalent_match_ids", Json::array()}});
  const auto before = state.internet_records.size();
  const auto metrics = internet_improvement_metrics(state);
  REQUIRE(state.internet_records.size() == before);
  REQUIRE_FALSE(metrics.at("candidates")
                    .at("current_by_status")
                    .contains("VALIDATION_READY"));
  REQUIRE(metrics.at("candidates").at("quarantine_reasons").at("UNSUPPORTED") ==
          1);
  REQUIRE(metrics.at("fetches").at("successful") == 1);
  REQUIRE(metrics.at("fetches").at("cache_hits") == 1);
  REQUIRE(metrics.at("fetches").at("failure_reasons").at("timeout") == 1);
  REQUIRE(metrics.at("algorithms").at("accepted_ever") == 1);
  REQUIRE(metrics.at("algorithms").at("currently_canonical") == 1);
  REQUIRE(metrics.at("cost_per_accepted_algorithm").at("fetch_milliseconds") ==
          40.0);
  REQUIRE(metrics.at("qualifications").at("passed") == 1);
  REQUIRE(metrics.at("discovery").at("retrievals_with_exact_matches") == 1);
}

TEST_CASE("internet metrics cost remains unknown before first admission") {
  statewright::egcf::InternetImprovementState state;
  const auto metrics = statewright::egcf::internet_improvement_metrics(state);
  REQUIRE(metrics.at("algorithms").at("accepted_ever") == 0);
  REQUIRE(metrics.at("cost_per_accepted_algorithm")
              .at("fetch_milliseconds")
              .is_null());
  REQUIRE(metrics.at("cost_per_accepted_algorithm")
              .at("compressed_bytes")
              .is_null());
}
