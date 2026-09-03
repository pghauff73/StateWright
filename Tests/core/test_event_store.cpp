#include "statewright/common/error.hpp"
#include "statewright/contracts/canonical_json.hpp"
#include "statewright/core/event_store.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

class EventFixture final {
public:
  EventFixture() {
    root = std::filesystem::temp_directory_path() /
           ("statewright-events-" + std::to_string(::getpid()) + "-" +
            std::to_string(std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count()));
    std::filesystem::create_directories(root);
  }
  ~EventFixture() { std::filesystem::remove_all(root); }
  std::filesystem::path root;
};

statewright::contracts::Json load_event_fixture() {
  std::ifstream input(STATEWRIGHT_ORACLE_FIXTURES);
  REQUIRE(input.is_open());
  return statewright::contracts::Json::parse(input).at("event_case");
}

} // namespace

TEST_CASE("event append matches the frozen Python oracle") {
  EventFixture fixture;
  statewright::core::EventStore store(fixture.root / "events.jsonl");
  const auto expected = load_event_fixture();
  const auto event = store.append(
      "test", expected.at("input_payload"),
      {.run_id = "run-1", .action_id = "action-1", .transaction_id = "tx-1"},
      statewright::core::EventStamp{
          .event_id = "12345678-1234-4234-9234-123456789abc",
          .timestamp = "2026-09-02T00:00:00Z"});
  REQUIRE(event == expected.at("event"));
  REQUIRE(store.head() ==
          expected.at("event").at("event_hash").get<std::string>());
  REQUIRE(store.validate_chain() == store.head());
}

TEST_CASE("event validation detects payload and previous-hash tampering") {
  EventFixture fixture;
  const auto path = fixture.root / "events.jsonl";
  statewright::core::EventStore store(path);
  const auto first = store.append("first", {{"value", 1}});
  const auto second = store.append("second", {{"value", 2}});
  REQUIRE(first.at("event_type") == "first");
  REQUIRE(second.at("event_type") == "second");
  auto events = store.events();
  events[0]["payload"]["value"] = 9;
  {
    std::ofstream output(path);
    for (const auto &event : events) {
      output << statewright::contracts::canonical_json(event) << '\n';
    }
  }
  REQUIRE_THROWS_AS(statewright::core::EventStore(path),
                    statewright::common::Error);

  events = store.events();
  events[0]["payload"]["value"] = 1;
  events[1]["previous_hash"] = "broken";
  {
    std::ofstream output(path);
    for (const auto &event : events) {
      output << statewright::contracts::canonical_json(event) << '\n';
    }
  }
  REQUIRE_THROWS_AS(statewright::core::EventStore(path),
                    statewright::common::Error);
}

TEST_CASE("event redaction preserves numeric token budgets and removes secrets") {
  const auto redacted = statewright::core::redact({
      {"max_tokens", 12000},
      {"tokens_before", 6100},
      {"access_token", "secret-value"},
      {"safe", "OPENAI_API_KEY=another-secret Bearer abcdef123456"},
  });
  REQUIRE(redacted.at("max_tokens") == 12000);
  REQUIRE(redacted.at("tokens_before") == 6100);
  REQUIRE(redacted.at("access_token") == "<redacted>");
  REQUIRE(redacted.at("safe").get<std::string>().find("another-secret") ==
          std::string::npos);
  REQUIRE(redacted.at("safe").get<std::string>().find("abcdef123456") ==
          std::string::npos);
}
