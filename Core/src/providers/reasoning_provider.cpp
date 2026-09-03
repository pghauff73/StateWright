#include "statewright/providers/reasoning_provider.hpp"

#include "statewright/common/error.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::providers {
namespace {

[[noreturn]] void policy_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied, std::move(message));
}

[[noreturn]] void provider_error(std::string message) {
  throw common::Error(common::ErrorCode::json_contract, std::move(message));
}

[[nodiscard]] int integer_value(const contracts::Json &value,
                                int default_value = 0) {
  if (value.is_null()) {
    return default_value;
  }
  if (value.is_number_integer()) {
    return value.get<int>();
  }
  if (value.is_string()) {
    try {
      std::size_t consumed = 0;
      const int result = std::stoi(value.get<std::string>(), &consumed);
      if (consumed == value.get_ref<const std::string &>().size()) {
        return result;
      }
    } catch (const std::exception &) {
    }
  }
  provider_error("reasoning provider usage must contain integer values");
}

[[nodiscard]] std::string trim(std::string value) {
  const auto first = std::ranges::find_if_not(value, [](unsigned char byte) {
    return std::isspace(byte) != 0;
  });
  const auto last = std::find_if_not(value.rbegin(), value.rend(),
                                     [](unsigned char byte) {
                                       return std::isspace(byte) != 0;
                                     })
                        .base();
  if (first >= last) {
    return {};
  }
  return std::string(first, last);
}

[[nodiscard]] std::string strip_reasoning_wrappers(std::string text) {
  text = trim(std::move(text));
  const std::string marker = "</think>";
  if (const auto found = text.rfind(marker); found != std::string::npos) {
    text = trim(text.substr(found + marker.size()));
  }
  if (text.starts_with("```")) {
    const auto first_newline = text.find('\n');
    if (first_newline == std::string::npos) {
      return {};
    }
    text.erase(0, first_newline + 1);
    text = trim(std::move(text));
    if (text.ends_with("```")) {
      text.erase(text.size() - 3);
      text = trim(std::move(text));
    }
  }
  return text;
}

} // namespace

std::vector<contracts::Json> ReasoningProvider::create_responses(
    const std::vector<contracts::Json> &requests,
    std::size_t max_responses) {
  if (requests.size() > max_responses) {
    policy_error("reasoning provider request count exceeds hard cap");
  }
  std::vector<contracts::Json> responses;
  responses.reserve(requests.size());
  for (const auto &request : requests) {
    try {
      responses.push_back(create_response(request));
    } catch (const std::exception &error) {
      responses.push_back(
          {{"type", "reasoning_error"},
           {"error", std::string("provider exception: ") + error.what()}});
    }
  }
  return responses;
}

int ReasoningProvider::reasoning_role_batch_size() const noexcept { return 1; }

void ReasoningProvider::record_reasoning_repair(
    std::string_view role, std::string_view reason,
    const std::vector<std::string> &item_ids) {
  static_cast<void>(role);
  static_cast<void>(reason);
  static_cast<void>(item_ids);
}

BoundedReasoningProvider::BoundedReasoningProvider(
    ReasoningProvider &provider, int max_calls, int max_tokens,
    int max_tool_calls)
    : provider_(provider), max_calls_(max_calls), max_tokens_(max_tokens),
      max_tool_calls_(max_tool_calls) {
  if (max_calls_ < 0 || max_tokens_ < 0 || max_tool_calls_ < 0) {
    policy_error("reasoning provider budgets must be non-negative");
  }
}

contracts::Json
BoundedReasoningProvider::create_response(const contracts::Json &request) {
  return create_responses({request}, 1).front();
}

std::vector<contracts::Json> BoundedReasoningProvider::create_responses(
    const std::vector<contracts::Json> &requests,
    std::size_t max_responses) {
  const int count = static_cast<int>(requests.size());
  if (requests.size() > max_responses || calls_used_ + count > max_calls_) {
    policy_error("reasoning provider call budget exceeded");
  }
  calls_used_ += count;
  auto responses = provider_.create_responses(requests, max_responses);
  if (responses.size() != requests.size()) {
    provider_error(
        "reasoning provider response count does not match request count");
  }
  for (const auto &response : responses) {
    const auto usage = response_usage(response);
    input_tokens_observed_ += usage.input_tokens;
    tokens_used_ += usage.output_tokens;
    total_tokens_observed_ += usage.total_tokens;
    tool_calls_used_ += usage.tool_calls;
  }
  if (tokens_used_ > max_tokens_) {
    policy_error("reasoning token budget exceeded");
  }
  if (tool_calls_used_ > max_tool_calls_) {
    policy_error("reasoning tool-call budget exceeded");
  }
  return responses;
}

int BoundedReasoningProvider::reasoning_role_batch_size() const noexcept {
  return std::max(1, provider_.reasoning_role_batch_size());
}

void BoundedReasoningProvider::record_reasoning_repair(
    std::string_view role, std::string_view reason,
    const std::vector<std::string> &item_ids) {
  provider_.record_reasoning_repair(role, reason, item_ids);
}

int BoundedReasoningProvider::calls_used() const noexcept { return calls_used_; }
int BoundedReasoningProvider::tokens_used() const noexcept {
  return tokens_used_;
}
int BoundedReasoningProvider::input_tokens_observed() const noexcept {
  return input_tokens_observed_;
}
int BoundedReasoningProvider::total_tokens_observed() const noexcept {
  return total_tokens_observed_;
}
int BoundedReasoningProvider::tool_calls_used() const noexcept {
  return tool_calls_used_;
}

ProviderUsage response_usage(const contracts::Json &response) {
  ProviderUsage result;
  const contracts::Json empty = contracts::Json::object();
  const contracts::Json &usage =
      response.is_object() && response.contains("usage") &&
              response.at("usage").is_object()
          ? response.at("usage")
          : empty;
  result.input_tokens = std::max(
      0, usage.contains("input_tokens")
             ? integer_value(usage.at("input_tokens"))
             : 0);
  const bool has_output = usage.contains("output_tokens") &&
                          !usage.at("output_tokens").is_null();
  result.total_tokens = std::max(
      0, usage.contains("total_tokens")
             ? integer_value(usage.at("total_tokens"))
             : 0);
  result.output_tokens =
      has_output
          ? std::max(0, integer_value(usage.at("output_tokens")))
          : std::max(0, result.total_tokens - result.input_tokens);
  if (result.total_tokens == 0) {
    result.total_tokens = result.input_tokens + result.output_tokens;
  }
  if (response.is_object() && response.contains("output") &&
      response.at("output").is_array()) {
    for (const auto &item : response.at("output")) {
      if (!item.is_object()) {
        continue;
      }
      const std::string type = item.value("type", "");
      if (type == "function_call" || type == "tool_call") {
        ++result.tool_calls;
      }
    }
  }
  return result;
}

std::string response_text(const contracts::Json &response) {
  if (!response.is_object()) {
    provider_error("reasoning provider response must be an object");
  }
  if (response.value("type", "") == "reasoning_error") {
    provider_error(response.value("error", "reasoning provider request failed"));
  }
  const std::string direct = response.value("output_text", "");
  if (!direct.empty()) {
    return direct;
  }
  std::string result;
  if (!response.contains("output") || !response.at("output").is_array()) {
    return result;
  }
  for (const auto &item : response.at("output")) {
    if (!item.is_object()) {
      continue;
    }
    const std::string type = item.value("type", "");
    if (type == "function_call" || type == "tool_call") {
      const auto found = item.find("arguments");
      if (found != item.end()) {
        if (found->is_object()) {
          return contracts::canonical_json(*found);
        }
        if (found->is_string() && !trim(found->get<std::string>()).empty()) {
          return found->get<std::string>();
        }
      }
      continue;
    }
    if (type != "message" || !item.contains("content") ||
        !item.at("content").is_array()) {
      continue;
    }
    for (const auto &content : item.at("content")) {
      if (content.is_object() && content.value("type", "") == "output_text") {
        result.append(content.value("text", ""));
      }
    }
  }
  return result;
}

contracts::Json parse_reasoning_json_object(std::string_view text) {
  const std::string stripped = strip_reasoning_wrappers(std::string(text));
  contracts::Json payload;
  try {
    payload = contracts::parse_json(stripped);
  } catch (const common::Error &error) {
    provider_error(std::string("reasoning response is not valid JSON: ") +
                   error.what());
  }
  if (!payload.is_object()) {
    provider_error("reasoning response must be a JSON object");
  }
  return payload;
}

std::vector<contracts::Json> parse_ordered_role_batch_payloads(
    const contracts::Json &response, std::string_view collection_key,
    std::size_t expected_count) {
  const auto payload = parse_reasoning_json_object(response_text(response));
  const auto found = payload.find(std::string(collection_key));
  if (found == payload.end() || !found->is_array()) {
    provider_error("reasoning batch response " + std::string(collection_key) +
                   " must be an array");
  }
  if (found->size() != expected_count) {
    provider_error("reasoning batch response count does not match the request");
  }
  std::vector<contracts::Json> result;
  result.reserve(found->size());
  for (const auto &item : *found) {
    if (!item.is_object()) {
      provider_error("reasoning batch response entries must be JSON objects");
    }
    result.push_back(item);
  }
  return result;
}

std::vector<contracts::Json> create_provider_responses(
    ReasoningProvider &provider,
    const std::vector<contracts::Json> &requests,
    std::size_t max_responses) {
  if (requests.size() > max_responses) {
    policy_error("reasoning provider request count exceeds hard cap");
  }
  auto responses = provider.create_responses(requests, max_responses);
  if (responses.size() != requests.size()) {
    provider_error(
        "reasoning provider response count does not match request count");
  }
  return responses;
}

int reasoning_role_batch_size(const ReasoningProvider &provider,
                              int configured_batch_size) {
  return std::min(std::max(1, configured_batch_size),
                  std::max(1, provider.reasoning_role_batch_size()));
}

} // namespace statewright::providers
