#include "statewright/contracts/canonical_json.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/contracts/typed_id.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

namespace {

statewright::contracts::Json load_fixtures() {
  std::ifstream input(STATEWRIGHT_ORACLE_FIXTURES);
  REQUIRE(input.is_open());
  return statewright::contracts::Json::parse(input);
}

} // namespace

TEST_CASE("C++ canonical JSON matches the frozen Python oracle") {
  const auto fixtures = load_fixtures();
  for (const auto &fixture : fixtures.at("canonical_json_cases")) {
    INFO(fixture.dump());
    REQUIRE(statewright::contracts::canonical_json(fixture.at("payload")) ==
            fixture.at("canonical").get<std::string>());
    REQUIRE(statewright::contracts::sha256_json(fixture.at("payload")) ==
            fixture.at("sha256").get<std::string>());
  }
}

TEST_CASE("C++ typed IDs match the frozen Python oracle") {
  const auto fixtures = load_fixtures();
  for (const auto &fixture : fixtures.at("typed_id_cases")) {
    INFO(fixture.dump());
    REQUIRE(statewright::contracts::typed_id(
                fixture.at("object_type").get<std::string>(),
                fixture.at("payload")) ==
            fixture.at("typed_id").get<std::string>());
  }
}

