#include "statewright/common/error.hpp"
#include "statewright/egcf/lifecycle.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("EGCF lifecycle matches canonical compressed success path") {
  statewright::egcf::Lifecycle lifecycle;
  REQUIRE(lifecycle.compress({"INTERPRETED", "MODELLED", "RESOLVED",
                              "QUALIFIED", "COMPILED"}) ==
          std::vector<std::string>{"DISCOVERED", "INTERPRETED", "MODELLED",
                                   "RESOLVED", "QUALIFIED", "COMPILED"});
  REQUIRE(lifecycle.transition("COMPLETED") == "COMPLETED");
  const auto projection = lifecycle.projection();
  REQUIRE(projection.size() ==
          statewright::egcf::canonical_lifecycle_stages().size());
  for (const auto &entry : projection) {
    REQUIRE((entry.at("status") == "completed" ||
             entry.at("status") == "not_required"));
  }
}

TEST_CASE("EGCF lifecycle fails closed on illegal transitions") {
  statewright::egcf::Lifecycle lifecycle;
  REQUIRE_THROWS_AS(lifecycle.transition("AUTHORIZED"),
                    statewright::common::Error);
  REQUIRE(lifecycle.transition("REFUSED") == "REFUSED");
  REQUIRE_THROWS_AS(lifecycle.transition("DISCOVERED"),
                    statewright::common::Error);
  REQUIRE_THROWS_AS(statewright::egcf::Lifecycle("UNKNOWN"),
                    statewright::common::Error);
}

TEST_CASE("EGCF lifecycle projections preserve terminal control states") {
  statewright::egcf::Lifecycle lifecycle("SUPERSEDED");
  const auto projection = lifecycle.projection();
  REQUIRE(projection.back() ==
          statewright::contracts::Json{{"reason", "terminal or control state"},
                                       {"stage", "SUPERSEDED"},
                                       {"status", "completed"}});
  REQUIRE(projection.front().at("status") == "not_required");
}
