#include "statewright/providers/subprocess_provider.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace statewright::providers {
namespace {

constexpr int protocol_version = 1;
constexpr std::array<std::string_view, 3> grammar_names{
    "oiec_compact_tool_response", "oiec_reasoning_response",
    "oiec_tool_response"};
constexpr std::array<std::string_view, 3> final_event_types{
    "result", "error", "cancelled"};

[[noreturn]] void provider_error(std::string message) {
  throw common::Error(common::ErrorCode::json_contract, std::move(message));
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

[[nodiscard]] contracts::Json file_identity(
    const std::filesystem::path &path,
    const std::filesystem::path &display_base = {}) {
  std::error_code error;
  const auto resolved = std::filesystem::canonical(path, error);
  if (error || !std::filesystem::is_regular_file(resolved, error) || error) {
    provider_error("provider identity file is missing: " + path.string());
  }
  auto display = resolved.string();
  if (!display_base.empty()) {
    const auto base = std::filesystem::canonical(display_base, error);
    if (!error) {
      const auto relative = std::filesystem::relative(resolved, base, error);
      if (!error && !relative.empty() &&
          !relative.native().starts_with("..")) {
        display = relative.string();
      }
    }
  }
  const auto size = std::filesystem::file_size(resolved, error);
  if (error) {
    provider_error("cannot stat provider identity file: " + path.string());
  }
  return {{"path", display},
          {"size", size},
          {"sha256", contracts::sha256_file(resolved)}};
}

[[nodiscard]] contracts::Json
stable_runner_descriptor(const contracts::Json &raw) {
  if (!raw.is_object()) {
    provider_error("runner descriptor must be an object");
  }
  auto descriptor = raw;
  if (descriptor.contains("backend_devices")) {
    auto &devices = descriptor["backend_devices"];
    if (!devices.is_array()) {
      provider_error("runner backend device descriptor must be an array");
    }
    for (auto &device : devices) {
      if (!device.is_object()) {
        provider_error("runner backend device descriptor must be an object");
      }
      device.erase("memory_free");
    }
  }
  return descriptor;
}

void validate_json_schema(const contracts::Json &value,
                          const contracts::Json &schema,
                          const std::string &path = "arguments") {
  if (!schema.is_object()) {
    provider_error(path + " schema is malformed");
  }
  const auto schema_type = schema.value("type", std::string{});
  if (schema_type == "object") {
    if (!value.is_object()) {
      provider_error(path + " must be an object");
    }
    const auto properties = schema.value("properties", contracts::Json::object());
    const auto required = schema.value("required", contracts::Json::array());
    if (!properties.is_object() || !required.is_array()) {
      provider_error(path + " schema is malformed");
    }
    for (const auto &name : required) {
      if (!name.is_string() || !value.contains(name.get<std::string>())) {
        provider_error(path + " is missing required fields");
      }
    }
    if (schema.value("additionalProperties", true) == false) {
      for (const auto &[name, ignored] : value.items()) {
        static_cast<void>(ignored);
        if (!properties.contains(name)) {
          provider_error(path + " has unknown fields");
        }
      }
    }
    for (const auto &[name, item] : value.items()) {
      if (properties.contains(name) && properties.at(name).is_object()) {
        validate_json_schema(item, properties.at(name), path + "." + name);
      }
    }
  } else if (schema_type == "array") {
    if (!value.is_array()) {
      provider_error(path + " must be an array");
    }
    if (schema.contains("items") && schema.at("items").is_object()) {
      std::size_t index = 0;
      for (const auto &item : value) {
        validate_json_schema(item, schema.at("items"),
                             path + "[" + std::to_string(index) + "]");
        ++index;
      }
    }
  } else if (schema_type == "string" && !value.is_string()) {
    provider_error(path + " must be string");
  } else if (schema_type == "integer" && !value.is_number_integer()) {
    provider_error(path + " must be integer");
  } else if (schema_type == "number" && !value.is_number()) {
    provider_error(path + " must be number");
  } else if (schema_type == "boolean" && !value.is_boolean()) {
    provider_error(path + " must be boolean");
  } else if (schema_type == "null" && !value.is_null()) {
    provider_error(path + " must be null");
  }
  if (schema.contains("enum")) {
    if (!schema.at("enum").is_array() ||
        std::ranges::find(schema.at("enum"), value) == schema.at("enum").end()) {
      provider_error(path + " is outside the allowed enum");
    }
  }
}

[[nodiscard]] std::string provider_prompt(const contracts::Json &request) {
  const contracts::Json response_contract = {
      {"message", {{"type", "message"}, {"content", "concise response"}}},
      {"function_call",
       {{"type", "function_call"},
        {"name", "one declared tool name"},
        {"arguments", {{"declared", "arguments"}}},
        {"call_id", "stable-call-id"}}}};
  return contracts::canonical_json(
      {{"system",
        "Return exactly one JSON object and no markdown. Do not expose private "
        "chain-of-thought. Select only a declared tool and only when needed."},
       {"instructions", request.value("instructions", std::string{})},
       {"input", request.value("input_items", contracts::Json::array())},
       {"tools", request.value("tools", contracts::Json::array())},
       {"response_contract", response_contract}});
}

[[nodiscard]] int estimate_tokens(const contracts::Json &value) {
  const auto size = value.dump(-1, ' ', false,
                               contracts::Json::error_handler_t::strict)
                        .size();
  return std::max(1, static_cast<int>((size + 3U) / 4U));
}

[[nodiscard]] int effective_input_budget(const SubprocessProviderConfig &config,
                                         int reserved_output_tokens) {
  const int configured = std::max(0, config.context_budget_tokens);
  const int runtime = std::max(
      0, config.runtime_context_tokens == 0 ? config.llama_context_tokens
                                           : config.runtime_context_tokens);
  if (runtime == 0) {
    return configured;
  }
  const int available =
      std::max(0, runtime - std::max(0, reserved_output_tokens) -
                      std::max(0, config.context_safety_margin_tokens));
  return std::min(configured, available);
}

[[nodiscard]] int metric_integer(const contracts::Json &metrics,
                                 std::string_view key, int default_value = 0) {
  if (!metrics.contains(key)) {
    return default_value;
  }
  const auto &value = metrics.at(std::string(key));
  if (!value.is_number_integer()) {
    provider_error("invalid local-model metric: " + std::string(key));
  }
  const int result = value.get<int>();
  if (result < 0) {
    provider_error("local-model metric cannot be negative: " +
                   std::string(key));
  }
  return result;
}

} // namespace

contracts::Json validate_subprocess_output(const contracts::Json &payload,
                                           const contracts::Json &tools) {
  if (!payload.is_object()) {
    provider_error("llama.cpp completion response must be an object");
  }
  const auto output_type = payload.value("type", std::string{});
  if (output_type == "message") {
    if (!payload.contains("content") || !payload.at("content").is_string()) {
      provider_error("llama.cpp message content must be non-empty");
    }
    const auto content = trim(payload.at("content").get<std::string>());
    if (content.empty()) {
      provider_error("llama.cpp message content must be non-empty");
    }
    return {{"output",
             contracts::Json::array(
                 {{{"type", "message"},
                   {"content", contracts::Json::array(
                                   {{{"type", "output_text"},
                                     {"text", content}}})}}})},
            {"output_text", content}};
  }
  if (output_type != "function_call") {
    provider_error("llama.cpp output type must be message or function_call");
  }
  if (!payload.contains("name") || !payload.at("name").is_string() ||
      !payload.contains("arguments") || !payload.at("arguments").is_object()) {
    provider_error("llama.cpp function call is malformed");
  }
  if (!tools.is_array()) {
    provider_error("declared tools must be an array");
  }
  const auto name = payload.at("name").get<std::string>();
  const auto &arguments = payload.at("arguments");
  const contracts::Json *declared_tool = nullptr;
  for (const auto &tool : tools) {
    if (tool.is_object() && tool.value("name", std::string{}) == name) {
      declared_tool = &tool;
      break;
    }
  }
  if (declared_tool == nullptr) {
    provider_error("llama.cpp selected undeclared tool: " + name);
  }
  if (!declared_tool->contains("parameters") ||
      !declared_tool->at("parameters").is_object()) {
    provider_error("declared tool schema is malformed: " + name);
  }
  validate_json_schema(arguments, declared_tool->at("parameters"));
  auto call_id = payload.value("call_id", std::string{});
  if (call_id.empty()) {
    call_id = "call-" +
              contracts::sha256_json(contracts::Json::array({name, arguments}))
                  .substr(0, 16);
  }
  return {{"output",
           contracts::Json::array(
               {{{"type", "function_call"},
                 {"name", name},
                 {"arguments", contracts::canonical_json(arguments)},
                 {"call_id", call_id}}})},
          {"output_text", ""}};
}

LlamaCppProcessProvider::LlamaCppProcessProvider(
    SubprocessProviderConfig config)
    : config_(std::move(config)) {}

LlamaCppProcessProvider::~LlamaCppProcessProvider() { close(); }

std::vector<std::string> LlamaCppProcessProvider::runner_command() const {
  if (config_.runner_path.empty()) {
    provider_error("llama.cpp process provider requires runner_path");
  }
  if (config_.model_path.empty()) {
    provider_error("llama.cpp process provider requires model_path");
  }
  if (config_.grammar_dir.empty()) {
    provider_error("llama.cpp process provider requires grammar_dir");
  }
  return {config_.runner_path.string(),
          "--model",
          config_.model_path.string(),
          "--context",
          std::to_string(std::max(256, config_.llama_context_tokens)),
          "--gpu-layers",
          std::to_string(config_.llama_gpu_layers),
          "--threads",
          std::to_string(std::max(0, config_.llama_threads)),
          "--grammar-dir",
          config_.grammar_dir.string()};
}

void LlamaCppProcessProvider::ensure_process() {
#if defined(_WIN32)
  provider_error("llama.cpp subprocess provider is unavailable on Windows");
#else
  if (process_id_ > 0) {
    int status = 0;
    if (::waitpid(process_id_, &status, WNOHANG) == 0) {
      return;
    }
    dispose_process(false);
  }
  const auto command = runner_command();
  int input_pipe[2] = {-1, -1};
  int output_pipe[2] = {-1, -1};
  int error_pipe[2] = {-1, -1};
  if (::pipe(input_pipe) != 0 || ::pipe(output_pipe) != 0 ||
      ::pipe(error_pipe) != 0) {
    for (const int descriptor : {input_pipe[0], input_pipe[1], output_pipe[0],
                                 output_pipe[1], error_pipe[0], error_pipe[1]}) {
      if (descriptor >= 0) {
        ::close(descriptor);
      }
    }
    provider_error("cannot create llama.cpp runner pipes");
  }
  const pid_t child = ::fork();
  if (child < 0) {
    for (const int descriptor : {input_pipe[0], input_pipe[1], output_pipe[0],
                                 output_pipe[1], error_pipe[0], error_pipe[1]}) {
      ::close(descriptor);
    }
    provider_error("cannot launch llama.cpp runner");
  }
  if (child == 0) {
    static_cast<void>(::dup2(input_pipe[0], STDIN_FILENO));
    static_cast<void>(::dup2(output_pipe[1], STDOUT_FILENO));
    static_cast<void>(::dup2(error_pipe[1], STDERR_FILENO));
    for (const int descriptor : {input_pipe[0], input_pipe[1], output_pipe[0],
                                 output_pipe[1], error_pipe[0], error_pipe[1]}) {
      ::close(descriptor);
    }
    std::vector<char *> arguments;
    arguments.reserve(command.size() + 1U);
    for (const auto &argument : command) {
      arguments.push_back(const_cast<char *>(argument.c_str()));
    }
    arguments.push_back(nullptr);
    ::execv(arguments.front(), arguments.data());
    _exit(127);
  }
  ::close(input_pipe[0]);
  ::close(output_pipe[1]);
  ::close(error_pipe[1]);
  process_id_ = static_cast<int>(child);
  stdin_fd_ = input_pipe[1];
  stdout_fd_ = output_pipe[0];
  stderr_fd_ = error_pipe[0];
  stdout_buffer_.clear();
  start_stderr_drain();
#endif
}

void LlamaCppProcessProvider::start_stderr_drain() {
#if !defined(_WIN32)
  {
    std::scoped_lock lock(stderr_mutex_);
    stderr_buffer_.clear();
  }
  const int descriptor = stderr_fd_;
  stderr_thread_ = std::thread([this, descriptor]() {
    std::array<char, 4096> buffer{};
    while (true) {
      const auto count = ::read(descriptor, buffer.data(), buffer.size());
      if (count <= 0) {
        return;
      }
      std::scoped_lock lock(stderr_mutex_);
      stderr_buffer_.append(buffer.data(), static_cast<std::size_t>(count));
      if (stderr_buffer_.size() > 65'536U) {
        stderr_buffer_.erase(0, stderr_buffer_.size() - 65'536U);
      }
    }
  });
#endif
}

std::string LlamaCppProcessProvider::stderr_diagnostic() const {
  std::scoped_lock lock(stderr_mutex_);
  return trim(stderr_buffer_);
}

void LlamaCppProcessProvider::dispose_process(bool terminate) noexcept {
#if !defined(_WIN32)
  const int child = process_id_;
  process_id_ = -1;
  if (child > 0) {
    int status = 0;
    if (terminate && ::waitpid(child, &status, WNOHANG) == 0) {
      static_cast<void>(::kill(child, SIGTERM));
      const auto deadline = std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(500);
      while (std::chrono::steady_clock::now() < deadline &&
             ::waitpid(child, &status, WNOHANG) == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      if (::waitpid(child, &status, WNOHANG) == 0) {
        static_cast<void>(::kill(child, SIGKILL));
      }
    }
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
  }
  if (stdin_fd_ >= 0) {
    ::close(stdin_fd_);
    stdin_fd_ = -1;
  }
  if (stdout_fd_ >= 0) {
    ::close(stdout_fd_);
    stdout_fd_ = -1;
  }
  if (stderr_thread_.joinable()) {
    stderr_thread_.join();
  }
  if (stderr_fd_ >= 0) {
    ::close(stderr_fd_);
    stderr_fd_ = -1;
  }
  stdout_buffer_.clear();
#else
  static_cast<void>(terminate);
#endif
}

void LlamaCppProcessProvider::write_line(const contracts::Json &payload) {
#if defined(_WIN32)
  static_cast<void>(payload);
  provider_error("llama.cpp subprocess provider is unavailable on Windows");
#else
  auto line = contracts::canonical_json(payload);
  line.push_back('\n');
  std::size_t written = 0;
  while (written < line.size()) {
    const auto count =
        ::write(stdin_fd_, line.data() + written, line.size() - written);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      provider_error("cannot write llama.cpp runner request");
    }
    written += static_cast<std::size_t>(count);
  }
#endif
}

std::string LlamaCppProcessProvider::read_line_until(
    std::chrono::steady_clock::time_point deadline) {
#if defined(_WIN32)
  static_cast<void>(deadline);
  provider_error("llama.cpp subprocess provider is unavailable on Windows");
#else
  while (true) {
    if (const auto newline = stdout_buffer_.find('\n');
        newline != std::string::npos) {
      auto line = stdout_buffer_.substr(0, newline);
      stdout_buffer_.erase(0, newline + 1U);
      return line;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      provider_error("llama.cpp runner deadline exceeded");
    }
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
    pollfd descriptor{.fd = stdout_fd_, .events = POLLIN, .revents = 0};
    const int timeout = std::max(1, static_cast<int>(remaining.count()));
    const int ready = ::poll(&descriptor, 1, timeout);
    if (ready < 0 && errno == EINTR) {
      continue;
    }
    if (ready < 0) {
      provider_error("cannot poll llama.cpp runner output");
    }
    if (ready == 0) {
      continue;
    }
    std::array<char, 4096> buffer{};
    const auto count = ::read(stdout_fd_, buffer.data(), buffer.size());
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      provider_error("llama.cpp runner exited before a result");
    }
    stdout_buffer_.append(buffer.data(), static_cast<std::size_t>(count));
  }
#endif
}

std::string LlamaCppProcessProvider::next_request_id(
    std::string_view operation, const contracts::Json &payload) {
  ++sequence_;
  return std::string(operation) + "-" + std::to_string(sequence_) + "-" +
         contracts::sha256_json(payload).substr(0, 20);
}

contracts::Json LlamaCppProcessProvider::request(std::string_view operation,
                                                 contracts::Json payload) {
  std::scoped_lock lock(mutex_);
  ensure_process();
  const auto request_id = next_request_id(operation, payload);
  payload["protocol_version"] = protocol_version;
  payload["op"] = operation;
  payload["request_id"] = request_id;
  try {
    write_line(payload);
    if (operation == "complete") {
      last_completion_request_sent_ = true;
    }
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(std::max(1, config_.timeout_ms));
    while (true) {
      const auto line = read_line_until(deadline);
      const auto event = contracts::parse_json(line);
      if (!event.is_object()) {
        provider_error("llama.cpp runner event must be an object");
      }
      if (event.value("protocol_version", 0) != protocol_version) {
        provider_error("llama.cpp runner protocol version mismatch");
      }
      if (event.value("request_id", std::string{}) != request_id) {
        provider_error("llama.cpp runner response identity mismatch");
      }
      const auto event_type = event.value("type", std::string{});
      if (event_type == "stream") {
        if (!event.contains("text") || !event.at("text").is_string()) {
          provider_error("llama.cpp stream event text must be a string");
        }
        continue;
      }
      if (std::ranges::find(final_event_types, event_type) ==
          final_event_types.end()) {
        provider_error("llama.cpp runner returned an unknown event type");
      }
      return event;
    }
  } catch (const common::Error &error) {
    const auto diagnostic = stderr_diagnostic();
    dispose_process(true);
    if (!diagnostic.empty()) {
      provider_error(std::string(error.what()) + ": " + diagnostic);
    }
    throw;
  }
}

contracts::Json LlamaCppProcessProvider::preflight() {
  std::error_code error;
  const auto model_path = std::filesystem::canonical(config_.model_path, error);
  if (error || !std::filesystem::is_regular_file(model_path, error) || error) {
    provider_error("GGUF model is missing: " + config_.model_path.string());
  }
  const auto observed_digest = contracts::sha256_file(model_path);
  auto expected = trim(config_.expected_model_sha256);
  std::ranges::transform(expected, expected.begin(), [](unsigned char byte) {
    return static_cast<char>(std::tolower(byte));
  });
  if (!expected.empty() && observed_digest != expected) {
    provider_error("GGUF digest mismatch: expected " + expected +
                   ", observed " + observed_digest);
  }
  contracts::Json grammar_identity = contracts::Json::array();
  for (const auto grammar_name : grammar_names) {
    grammar_identity.push_back(file_identity(
        config_.grammar_dir / (std::string(grammar_name) + ".gbnf"),
        config_.grammar_dir));
  }
  const auto event = request("describe", {});
  if (event.value("status", std::string{}) != "ok" ||
      !event.contains("descriptor") || !event.at("descriptor").is_object()) {
    provider_error(event.value("diagnostic", std::string{"runner describe failed"}));
  }
  auto descriptor = stable_runner_descriptor(event.at("descriptor"));
  descriptor["status"] = "ready";
  descriptor["provider"] = "llama_cpp_process";
  descriptor["model"] = config_.model;
  descriptor["model_path"] = model_path.string();
  descriptor["model_digest"] = observed_digest;
  descriptor["model_file_size"] = std::filesystem::file_size(model_path);
  descriptor["max_transport_retries"] = 0;
  descriptor["max_reasoning_samples"] = config_.max_reasoning_samples;
  descriptor["context_budget_tokens"] = config_.context_budget_tokens;
  descriptor["runtime_context_tokens"] =
      config_.runtime_context_tokens == 0 ? config_.llama_context_tokens
                                         : config_.runtime_context_tokens;
  descriptor["context_safety_margin_tokens"] =
      config_.context_safety_margin_tokens;
  descriptor["max_output_tokens"] = config_.max_output_tokens;
  descriptor["protocol_version"] = protocol_version;
  descriptor["supports_cancellation"] = true;
  descriptor["supports_deadline"] = true;
  descriptor["supports_json_grammar"] = true;
  descriptor["runner_identity"] = file_identity(config_.runner_path);
  descriptor["grammar_identity"] = std::move(grammar_identity);
  descriptor["sampling_contract"] =
      {{"seed", config_.llama_seed},
       {"temperature_bp", config_.llama_temperature_bp},
       {"top_p_bp", config_.llama_top_p_bp},
       {"top_k", config_.llama_top_k},
       {"context_tokens", config_.llama_context_tokens},
       {"gpu_layers", config_.llama_gpu_layers},
       {"threads", config_.llama_threads},
       {"max_output_tokens", config_.max_output_tokens}};
  descriptor["identity_signature"] = contracts::sha256_json(descriptor);
  return descriptor;
}

contracts::Json
LlamaCppProcessProvider::create_response(const contracts::Json &provider_request) {
  last_completion_request_sent_ = false;
  if (!provider_request.is_object()) {
    provider_error("reasoning provider request must be an object");
  }
  const auto input_items =
      provider_request.value("input_items", contracts::Json::array());
  const auto tools = provider_request.value("tools", contracts::Json::array());
  if (!input_items.is_array() || !tools.is_array()) {
    provider_error("reasoning provider input_items and tools must be arrays");
  }
  const int output_limit = std::max(
      1, provider_request.value("max_output_tokens", config_.max_output_tokens));
  const int estimated = estimate_tokens(
      {{"instructions", provider_request.value("instructions", std::string{})},
       {"input", input_items},
       {"tools", tools}});
  const int budget = effective_input_budget(config_, output_limit);
  if (estimated > budget) {
    provider_error("provider input exceeds configured context budget: estimated " +
                   std::to_string(estimated) + ", budget " +
                   std::to_string(budget));
  }
  bool compact_reasoning_tool = false;
  for (const auto &tool : tools) {
    const auto name = tool.is_object() ? tool.value("name", std::string{}) : "";
    compact_reasoning_tool =
        compact_reasoning_tool || name == "submit_oiec_reasoning_batch" ||
        name == "submit_oiec_reasoning_object";
  }
  const auto grammar = compact_reasoning_tool
                           ? "oiec_compact_tool_response"
                           : (tools.empty() ? "oiec_reasoning_response"
                                            : "oiec_tool_response");
  const auto event = request(
      "complete",
      {{"prompt", provider_prompt(provider_request)},
       {"context_tokens", std::max(256, config_.llama_context_tokens)},
       {"max_output_tokens", output_limit},
       {"deadline_ms", std::max(1, config_.timeout_ms)},
       {"seed", config_.llama_seed},
       {"temperature",
        static_cast<double>(std::max(0, config_.llama_temperature_bp)) /
            10'000.0},
       {"top_p", static_cast<double>(std::max(0, config_.llama_top_p_bp)) /
                     10'000.0},
       {"top_k", std::max(1, config_.llama_top_k)},
       {"grammar", grammar},
       {"use_chat_template", true}});
  const auto status = event.value("status", std::string{"provider_error"});
  if (status != "ok") {
    auto diagnostic = event.value("diagnostic", std::string{});
    const auto generated = event.value("text", std::string{});
    if (!generated.empty()) {
      diagnostic += "; generated=" + generated.substr(0, 512);
    }
    provider_error("llama.cpp completion failed: " + status + ": " +
                   diagnostic);
  }
  if (!event.contains("metrics") || !event.at("metrics").is_object()) {
    provider_error("llama.cpp completion metrics must be an object");
  }
  const auto &metrics = event.at("metrics");
  const int input_tokens = metric_integer(metrics, "prompt_tokens");
  const int output_tokens = metric_integer(metrics, "output_tokens");
  if (input_tokens < 1 || output_tokens < 1) {
    provider_error("llama.cpp completion omitted positive token usage");
  }
  contracts::Json generated;
  if (event.contains("response")) {
    if (!event.at("response").is_object()) {
      provider_error("llama.cpp completion response must be an object");
    }
    generated = event.at("response");
  } else {
    const auto text = event.value("text", std::string{});
    if (text.empty()) {
      provider_error("llama.cpp completion omitted structured output");
    }
    generated = contracts::parse_json(text);
    if (!generated.is_object()) {
      provider_error("llama.cpp completion returned invalid JSON object");
    }
  }
  auto response = validate_subprocess_output(generated, tools);
  response["usage"] = {{"input_tokens", input_tokens},
                       {"output_tokens", output_tokens},
                       {"total_tokens", input_tokens + output_tokens}};
  response["temperature"] =
      static_cast<double>(std::max(0, config_.llama_temperature_bp)) / 10'000.0;
  response["top_p"] =
      static_cast<double>(std::max(0, config_.llama_top_p_bp)) / 10'000.0;
  response["provider_metadata"] =
      {{"provider", "llama_cpp_process"},
       {"request_id", event.value("request_id", std::string{})},
       {"metrics", metrics}};
  return response;
}

std::vector<contracts::Json> LlamaCppProcessProvider::create_responses(
    const std::vector<contracts::Json> &requests,
    std::size_t max_responses) {
  const auto hard_cap = std::min(
      std::max<std::size_t>(1, max_responses),
      static_cast<std::size_t>(std::max(1, config_.max_reasoning_samples)));
  if (requests.empty()) {
    provider_error("multi-response request must be non-empty");
  }
  if (requests.size() > hard_cap) {
    provider_error("multi-response request exceeds configured sample cap");
  }
  std::vector<contracts::Json> responses;
  responses.reserve(requests.size());
  for (const auto &provider_request : requests) {
    try {
      responses.push_back(create_response(provider_request));
    } catch (const common::Error &error) {
      responses.push_back(
          {{"type", "reasoning_error"}, {"error", error.what()}});
    }
  }
  return responses;
}

int LlamaCppProcessProvider::reasoning_role_batch_size() const noexcept {
  return 2;
}

bool LlamaCppProcessProvider::last_completion_request_sent() const noexcept {
  return last_completion_request_sent_;
}

void LlamaCppProcessProvider::close() noexcept {
  std::scoped_lock lock(mutex_);
  if (process_id_ <= 0) {
    return;
  }
  try {
    write_line({{"protocol_version", protocol_version},
                {"op", "shutdown"},
                {"request_id", "shutdown"}});
  } catch (...) {
  }
  dispose_process(true);
}

} // namespace statewright::providers
