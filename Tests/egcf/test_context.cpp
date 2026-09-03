#include "statewright/egcf/context.hpp"

#include "statewright/common/error.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("EGCF command context inherits universal modifiers monotonically") {
  using namespace statewright::egcf;
  const auto parent = command_context_from_json(
      {{"dry_run", true},
       {"why", true},
       {"scope", {"src/**"}},
       {"evidence", {"parent"}},
       {"approval", "policy"},
       {"risk", "L1"},
       {"rollback", "compensating"},
       {"budget", {{"actions", 5}, {"retries", 2}}},
       {"timeout", 60},
       {"trace", true},
       {"json", true},
       {"graph", true},
       {"record", true},
       {"replay", "plan:sha256:abc"},
       {"strict", true},
       {"simulate", true}});
  const auto child = command_context_from_json(
      {{"scope", {"src/parser.py"}},
       {"evidence", {"child"}},
       {"approval", "human"},
       {"risk", "L2"},
       {"rollback", "exact"},
       {"budget", {{"actions", 3}, {"retries", 1}}},
       {"timeout", 30}});
  const auto effective = parent.inherit(child);
  REQUIRE(effective.scope == std::vector<std::string>{"src/parser.py"});
  REQUIRE(effective.evidence ==
          std::vector<std::string>{"parent", "child"});
  REQUIRE(effective.approval == "human");
  REQUIRE(effective.risk == "L2");
  REQUIRE(effective.rollback == "exact");
  REQUIRE(effective.budget.actions == 3);
  REQUIRE(effective.budget.retries == 1);
  REQUIRE(effective.timeout == 30.0);
  REQUIRE(effective.dry_run);
  REQUIRE(effective.why);
  REQUIRE(effective.trace);
  REQUIRE(effective.json_output);
  REQUIRE(effective.graph);
  REQUIRE(effective.record);
  REQUIRE(effective.strict);
  REQUIRE(effective.simulate);
  CommandContext outside;
  outside.scope = {"outside/**"};
  REQUIRE_THROWS_AS(parent.inherit(outside),
                    statewright::common::Error);
}

TEST_CASE("EGCF command context validates scope and budgets fail closed") {
  using namespace statewright::egcf;
  REQUIRE(scope_contains("src/**", "src/parser.cpp"));
  REQUIRE_FALSE(scope_contains("src/**", "tests/parser.cpp"));
  REQUIRE_THROWS_AS(command_context_from_json({{"risk", "L3"}}),
                    statewright::common::Error);
  REQUIRE_THROWS_AS(
      command_context_from_json({{"budget", {{"actions", -1}}}}),
      statewright::common::Error);
  REQUIRE_THROWS_AS(command_context_from_json({{"unknown", true}}),
                    statewright::common::Error);
}
