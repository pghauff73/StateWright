#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::providers {

struct ProviderUsage final {
  int input_tokens = 0;
  int output_tokens = 0;
  int total_tokens = 0;
  int tool_calls = 0;
};

class ReasoningProvider {
public:
  virtual ~ReasoningProvider() = default;

  [[nodiscard]] virtual contracts::Json
  create_response(const contracts::Json &request) = 0;
  [[nodiscard]] virtual std::vector<contracts::Json> create_responses(
      const std::vector<contracts::Json> &requests,
      std::size_t max_responses);
  [[nodiscard]] virtual int reasoning_role_batch_size() const noexcept;
  virtual void record_reasoning_repair(
      std::string_view role, std::string_view reason,
      const std::vector<std::string> &item_ids);
};

class BoundedReasoningProvider final : public ReasoningProvider {
public:
  BoundedReasoningProvider(ReasoningProvider &provider, int max_calls,
                           int max_tokens, int max_tool_calls);

  [[nodiscard]] contracts::Json
  create_response(const contracts::Json &request) override;
  [[nodiscard]] std::vector<contracts::Json> create_responses(
      const std::vector<contracts::Json> &requests,
      std::size_t max_responses) override;
  [[nodiscard]] int reasoning_role_batch_size() const noexcept override;
  void record_reasoning_repair(
      std::string_view role, std::string_view reason,
      const std::vector<std::string> &item_ids) override;

  [[nodiscard]] int calls_used() const noexcept;
  [[nodiscard]] int tokens_used() const noexcept;
  [[nodiscard]] int input_tokens_observed() const noexcept;
  [[nodiscard]] int total_tokens_observed() const noexcept;
  [[nodiscard]] int tool_calls_used() const noexcept;

private:
  ReasoningProvider &provider_;
  int max_calls_;
  int max_tokens_;
  int max_tool_calls_;
  int calls_used_ = 0;
  int tokens_used_ = 0;
  int input_tokens_observed_ = 0;
  int total_tokens_observed_ = 0;
  int tool_calls_used_ = 0;
};

[[nodiscard]] ProviderUsage
response_usage(const contracts::Json &response);
[[nodiscard]] std::string response_text(const contracts::Json &response);
[[nodiscard]] contracts::Json parse_reasoning_json_object(
    std::string_view text);
[[nodiscard]] std::vector<contracts::Json> parse_ordered_role_batch_payloads(
    const contracts::Json &response, std::string_view collection_key,
    std::size_t expected_count);
[[nodiscard]] std::vector<contracts::Json> create_provider_responses(
    ReasoningProvider &provider,
    const std::vector<contracts::Json> &requests,
    std::size_t max_responses);
[[nodiscard]] int reasoning_role_batch_size(
    const ReasoningProvider &provider, int configured_batch_size);

} // namespace statewright::providers
