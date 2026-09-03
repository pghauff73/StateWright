#include "statewright/common/error.hpp"
#include "statewright/contracts/typed_id.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("typed IDs normalize object type and bind payload") {
  const auto payload = statewright::contracts::parse_json(
      R"({"actor":"user","objective":"inspect"})");
  const auto first = statewright::contracts::typed_id(" Intent_Record ", payload);
  const auto second = statewright::contracts::typed_id("intent-record", payload);
  REQUIRE(first == second);
  REQUIRE(first.starts_with("intent-record:sha256:"));
  REQUIRE(statewright::contracts::parse_typed_id(first).object_type ==
          "intent-record");
}

TEST_CASE("typed IDs reject invalid object types and malformed IDs") {
  const auto payload = statewright::contracts::Json::object();
  REQUIRE_THROWS_AS(statewright::contracts::typed_id("bad:type", payload),
                    statewright::common::Error);
  REQUIRE_THROWS_AS(statewright::contracts::parse_typed_id("intent:sha256:short"),
                    statewright::common::Error);
}

