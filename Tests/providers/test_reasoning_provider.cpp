#include "statewright/providers/reasoning_provider.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/canonical_json.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

class RecordingProvider final : public statewright::providers::ReasoningProvider {
public:
  explicit RecordingProvider(
      std::vector<statewright::contracts::Json> supplied_responses,
      int supplied_batch_size = 1)
      : responses(std::move(supplied_responses)), batch_size(supplied_batch_size) {}

  [[nodiscard]] statewright::contracts::Json create_response(
      const statewright::contracts::Json &request) override {
    requests.push_back(request);
    if (next_response >= responses.size()) {
      throw std::runtime_error("no provider response remains");
    }
    return responses[next_response++];
  }

  [[nodiscard]] int reasoning_role_batch_size() const noexcept override {
    return batch_size;
  }

  void record_reasoning_repair(
      std::string_view role, std::string_view reason,
      const std::vector<std::string> &item_ids) override {
    repairs.push_back({{"role", role}, {"reason", reason}, {"item_ids", item_ids}});
  }

  std::vector<statewright::contracts::Json> responses;
  std::vector<statewright::contracts::Json> requests;
  std::vector<statewright::contracts::Json> repairs;
  std::size_t next_response = 0;
  int batch_size = 1;
};

statewright::contracts::Json metered_response(int input_tokens,
                                             int output_tokens,
                                             int tool_calls) {
  auto output = statewright::contracts::Json::array();
  for (int index = 0; index < tool_calls; ++index) {
    output.push_back({{"type", "function_call"},
                      {"arguments", {{"index", index}}}});
  }
  return {{"usage",
           {{"input_tokens", input_tokens},
            {"output_tokens", output_tokens},
            {"total_tokens", input_tokens + output_tokens}}},
          {"output", std::move(output)}};
}

} // namespace

TEST_CASE("bounded reasoning provider meters output separately from input") {
  RecordingProvider raw({metered_response(500, 20, 1)}, 4);
  statewright::providers::BoundedReasoningProvider bounded(raw, 1, 20, 1);

  const auto response = bounded.create_response({{"request", 1}});
  REQUIRE(response.at("usage").at("input_tokens") == 500);
  REQUIRE(bounded.calls_used() == 1);
  REQUIRE(bounded.tokens_used() == 20);
  REQUIRE(bounded.input_tokens_observed() == 500);
  REQUIRE(bounded.total_tokens_observed() == 520);
  REQUIRE(bounded.tool_calls_used() == 1);
  REQUIRE(bounded.reasoning_role_batch_size() == 4);
}

TEST_CASE("bounded reasoning provider fails closed at every hard cap") {
  RecordingProvider call_raw({metered_response(0, 0, 0)});
  statewright::providers::BoundedReasoningProvider call_bounded(call_raw, 0, 1, 1);
  REQUIRE_THROWS_AS(call_bounded.create_response({}), statewright::common::Error);

  RecordingProvider token_raw({metered_response(200, 21, 0)});
  statewright::providers::BoundedReasoningProvider token_bounded(token_raw, 1, 20, 1);
  REQUIRE_THROWS_AS(token_bounded.create_response({}), statewright::common::Error);

  RecordingProvider tool_raw({metered_response(0, 0, 2)});
  statewright::providers::BoundedReasoningProvider tool_bounded(tool_raw, 1, 1, 1);
  REQUIRE_THROWS_AS(tool_bounded.create_response({}), statewright::common::Error);
}

TEST_CASE("reasoning provider response text accepts supported response shapes") {
  REQUIRE(statewright::providers::response_text({{"output_text", "direct"}}) ==
          "direct");
  REQUIRE(statewright::providers::response_text(
              {{"output",
                {{{"type", "function_call"},
                  {"arguments", {{"b", 2}, {"a", 1}}}}}}}) ==
          "{\"a\":1,\"b\":2}");
  REQUIRE(statewright::providers::response_text(
              {{"output",
                {{{"type", "message"},
                  {"content",
                   {{{"type", "output_text"}, {"text", "left"}},
                    {{"type", "output_text"}, {"text", "right"}}}}}}}}) ==
          "leftright");

  const auto parsed = statewright::providers::parse_reasoning_json_object(
      "thinking</think>\n```json\n{\"answer\":true}\n```");
  REQUIRE(parsed == statewright::contracts::Json{{"answer", true}});
}

TEST_CASE("reasoning provider batch parsing preserves declared order") {
  const statewright::contracts::Json response = {
      {"output_text", "{\"items\":[{\"id\":\"a\"},{\"id\":\"b\"}]}"}};
  const auto items = statewright::providers::parse_ordered_role_batch_payloads(
      response, "items", 2);
  REQUIRE(items.at(0).at("id") == "a");
  REQUIRE(items.at(1).at("id") == "b");
  REQUIRE_THROWS_AS(
      statewright::providers::parse_ordered_role_batch_payloads(response, "items", 1),
      statewright::common::Error);
  REQUIRE(statewright::providers::reasoning_role_batch_size(
              RecordingProvider({}, 8), 3) == 3);
}
