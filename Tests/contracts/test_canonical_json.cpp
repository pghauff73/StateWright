#include "statewright/common/error.hpp"
#include "statewright/contracts/canonical_json.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("canonical JSON sorts keys and preserves Unicode") {
  const auto result = statewright::contracts::canonicalize_json_text(
      R"({"z":1,"message":"München","a":[3,2,1]})");
  REQUIRE(result == R"({"a":[3,2,1],"message":"München","z":1})");
}

TEST_CASE("canonical JSON rejects malformed input") {
  REQUIRE_THROWS_AS(statewright::contracts::parse_json("{"),
                    statewright::common::Error);
}

