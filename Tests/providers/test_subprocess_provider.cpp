#include "statewright/providers/subprocess_provider.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

class TemporaryDirectory final {
public:
  TemporaryDirectory()
      : path_(std::filesystem::temp_directory_path() /
              ("statewright-provider-" +
               statewright::contracts::sha256_text(
                   std::to_string(
                       std::chrono::steady_clock::now().time_since_epoch().count()))
                   .substr(0, 16))) {
    std::filesystem::create_directories(path_);
  }

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return path_;
  }

private:
  std::filesystem::path path_;
};

[[nodiscard]] statewright::providers::SubprocessProviderConfig
provider_config(const std::filesystem::path &root,
                std::string model_name = "qwen3.8.gguf") {
  const auto model = root / model_name;
  std::ofstream output(model, std::ios::binary);
  output << "fake-qwen3.8-gguf";
  output.close();
  return {.model = "qwen3.8-27b-direct",
          .runner_path = STATEWRIGHT_FAKE_PROVIDER_RUNNER,
          .model_path = model,
          .expected_model_sha256 = statewright::contracts::sha256_file(model),
          .grammar_dir = std::filesystem::path(STATEWRIGHT_RESOURCE_ROOT) /
                         "grammars" / "providers",
          .context_budget_tokens = 6'000,
          .runtime_context_tokens = 8'192,
          .context_safety_margin_tokens = 512,
          .max_output_tokens = 2'048,
          .timeout_ms = 1'000,
          .max_reasoning_samples = 16,
          .llama_context_tokens = 8'192,
          .llama_gpu_layers = -1,
          .llama_threads = 0,
          .llama_seed = 1'234,
          .llama_temperature_bp = 1'000,
          .llama_top_p_bp = 9'500,
          .llama_top_k = 40};
}

[[nodiscard]] statewright::contracts::Json message_request(
    std::string instructions = "Answer") {
  return {{"instructions", std::move(instructions)},
          {"input_items",
           statewright::contracts::Json::array(
               {{{"role", "user"}, {"content", "hello"}}})},
          {"tools", statewright::contracts::Json::array()}};
}

} // namespace

TEST_CASE("subprocess provider binds model runner and grammar identity") {
  TemporaryDirectory temporary;
  statewright::providers::LlamaCppProcessProvider provider(
      provider_config(temporary.path()));
  const auto descriptor = provider.preflight();
  REQUIRE(descriptor.at("status") == "ready");
  REQUIRE(descriptor.at("provider") == "llama_cpp_process");
  REQUIRE(descriptor.at("model") == "qwen3.8-27b-direct");
  REQUIRE(descriptor.at("max_transport_retries") == 0);
  REQUIRE(descriptor.at("grammar_identity").size() == 3);
  auto material = descriptor;
  const auto signature = material.at("identity_signature").get<std::string>();
  material.erase("identity_signature");
  REQUIRE(signature == statewright::contracts::sha256_json(material));
}

TEST_CASE("subprocess provider returns Responses-compatible messages") {
  TemporaryDirectory temporary;
  statewright::providers::LlamaCppProcessProvider provider(
      provider_config(temporary.path()));
  const auto response = provider.create_response(message_request());
  REQUIRE(response.at("output_text") == "Qwen3.8 response");
  REQUIRE(response.at("output").at(0).at("type") == "message");
  REQUIRE(response.at("usage") ==
          statewright::contracts::Json{{"input_tokens", 10},
                                       {"output_tokens", 4},
                                       {"total_tokens", 14}});
  REQUIRE(response.at("temperature") == 0.1);
  REQUIRE(response.at("top_p") == 0.95);
  REQUIRE(response.at("provider_metadata").at("metrics").at("total_ms") ==
          5);
}

TEST_CASE("subprocess provider validates declared tool contracts") {
  TemporaryDirectory temporary;
  statewright::providers::LlamaCppProcessProvider provider(
      provider_config(temporary.path()));
  const statewright::contracts::Json tool =
      {{"type", "function"},
       {"name", "read_file"},
       {"parameters",
        {{"type", "object"},
         {"properties", {{"path", {{"type", "string"}}}}},
         {"required", {"path"}},
         {"additionalProperties", false}}}};
  auto request = message_request("CALL_TOOL");
  request["tools"] = statewright::contracts::Json::array({tool});
  const auto response = provider.create_response(request);
  const auto &call = response.at("output").at(0);
  REQUIRE(call.at("type") == "function_call");
  REQUIRE(call.at("name") == "read_file");
  REQUIRE(call.at("arguments") == "{\"path\":\"README.md\"}");

  REQUIRE_THROWS_AS(provider.create_response(message_request("UNKNOWN_TOOL")),
                    statewright::common::Error);
}

TEST_CASE("subprocess provider uses compact grammar for reasoning tools") {
  TemporaryDirectory temporary;
  statewright::providers::LlamaCppProcessProvider provider(
      provider_config(temporary.path()));
  auto request = message_request("Submit the structured reasoning answer.");
  request["tools"] = statewright::contracts::Json::array(
      {{{"type", "function"},
        {"name", "submit_oiec_reasoning_object"},
        {"parameters",
         {{"type", "object"},
          {"properties", {{"answer", {{"type", "string"}}}}},
          {"required", {"answer"}},
          {"additionalProperties", false}}}}});
  const auto response = provider.create_response(request);
  REQUIRE(response.at("output").at(0).at("name") ==
          "submit_oiec_reasoning_object");
  REQUIRE(response.at("output").at(0).at("arguments") ==
          "{\"answer\":\"no\"}");
}

TEST_CASE("subprocess provider rejects context and digest before completion") {
  TemporaryDirectory temporary;
  auto context_config = provider_config(temporary.path());
  context_config.context_budget_tokens = 1;
  statewright::providers::LlamaCppProcessProvider context_provider(
      context_config);
  REQUIRE_THROWS_AS(context_provider.create_response(message_request()),
                    statewright::common::Error);
  REQUIRE_FALSE(context_provider.last_completion_request_sent());

  auto digest_config = provider_config(temporary.path(), "digest.gguf");
  digest_config.expected_model_sha256 = std::string(64, '0');
  statewright::providers::LlamaCppProcessProvider digest_provider(digest_config);
  REQUIRE_THROWS_AS(digest_provider.preflight(), statewright::common::Error);
  REQUIRE_FALSE(digest_provider.last_completion_request_sent());
}

TEST_CASE("subprocess provider never retries failed completions") {
  TemporaryDirectory temporary;
  const auto config = provider_config(temporary.path());
  statewright::providers::LlamaCppProcessProvider provider(config);
  REQUIRE_THROWS_AS(provider.create_response(message_request("FORCE_ERROR")),
                    statewright::common::Error);
  std::ifstream attempts(config.model_path.string() + ".attempts");
  int count = 0;
  attempts >> count;
  REQUIRE(count == 1);
}

TEST_CASE("subprocess provider batches in order with bounded failures") {
  TemporaryDirectory temporary;
  statewright::providers::LlamaCppProcessProvider provider(
      provider_config(temporary.path()));
  const auto responses = provider.create_responses(
      {message_request("first"), message_request("FORCE_ERROR")}, 4);
  REQUIRE(responses.size() == 2);
  REQUIRE(responses.at(0).at("output_text") == "Qwen3.8 response");
  REQUIRE(responses.at(1).at("type") == "reasoning_error");
  REQUIRE(provider.reasoning_role_batch_size() == 2);
}

TEST_CASE("subprocess provider deadline terminates the runner") {
  TemporaryDirectory temporary;
  auto config = provider_config(temporary.path());
  config.timeout_ms = 50;
  statewright::providers::LlamaCppProcessProvider provider(config);
  REQUIRE_THROWS_AS(provider.create_response(message_request("SLOW")),
                    statewright::common::Error);
  REQUIRE(provider.last_completion_request_sent());
}
