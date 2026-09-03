#include "statewright/contracts/hash.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("SHA-256 matches the standard empty-string vector") {
  REQUIRE(statewright::contracts::sha256_text("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("JSON hashing uses canonical JSON") {
  const auto first = statewright::contracts::parse_json(R"({"b":2,"a":1})");
  const auto second = statewright::contracts::parse_json(R"({"a":1,"b":2})");
  REQUIRE(statewright::contracts::sha256_json(first) ==
          statewright::contracts::sha256_json(second));
}

