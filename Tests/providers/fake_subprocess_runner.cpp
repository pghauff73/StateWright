#include "statewright/contracts/canonical_json.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace {

[[nodiscard]] std::string argument_value(int argc, char **argv,
                                         const std::string &name) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (argv[index] == name) {
      return argv[index + 1];
    }
  }
  return {};
}

void increment_attempts(const std::filesystem::path &path) {
  int attempts = 0;
  std::ifstream input(path);
  if (input.is_open()) {
    input >> attempts;
  }
  std::ofstream output(path, std::ios::trunc);
  output << attempts + 1;
}

} // namespace

int main(int argc, char **argv) {
  const std::filesystem::path model_path =
      argument_value(argc, argv, "--model");
  const int context_tokens =
      std::stoi(argument_value(argc, argv, "--context"));
  const int gpu_layers =
      std::stoi(argument_value(argc, argv, "--gpu-layers"));
  if (model_path.filename().string().starts_with("noisy")) {
    for (int index = 0; index < 512; ++index) {
      std::cerr << "llama.cpp diagnostic " << std::string(1024, 'x') << '\n';
    }
    std::cerr.flush();
  }
  if (model_path.filename().string().starts_with("oom")) {
    std::cerr << "ggml_cuda_compute_forward: NV_ERR_NO_MEMORY\n";
    std::cerr.flush();
    return 1;
  }

  std::string line;
  while (std::getline(std::cin, line)) {
    const auto request = statewright::contracts::parse_json(line);
    const auto request_id = request.at("request_id").get<std::string>();
    const auto operation = request.at("op").get<std::string>();
    statewright::contracts::Json response = {
        {"protocol_version", 1},
        {"type", "result"},
        {"request_id", request_id},
        {"status", "ok"}};
    if (operation == "describe") {
      response["descriptor"] =
          {{"runner", "fake-statewright-llama-runner"},
           {"model_architecture", "qwen3.8"},
           {"parameter_count", 27'000'000'000LL},
           {"quantization", "Q2_K"},
           {"context_tokens", context_tokens},
           {"gpu_layers", gpu_layers},
           {"supports_grammar", true},
           {"supports_chat_template", true},
           {"supports_streaming", true},
           {"supports_deadline", true},
           {"fresh_context_per_completion", true},
           {"backend_devices",
            statewright::contracts::Json::array(
                {{{"index", 0},
                  {"name", "fake-gpu"},
                  {"device_id", "GPU-stable"},
#if !defined(_WIN32)
                  {"memory_free", static_cast<std::int64_t>(::getpid())},
#else
                  {"memory_free", 1},
#endif
                  {"memory_total", 16'000'000'000LL}}})}};
    } else if (operation == "complete") {
      increment_attempts(model_path.string() + ".attempts");
      const auto prompt = request.at("prompt").get<std::string>();
      if (prompt.find("SLOW") != std::string::npos) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
      }
      if (prompt.find("FORCE_ERROR") != std::string::npos) {
        response["status"] = "invalid_output";
        response["diagnostic"] = "forced failure";
        response["text"] = "{\"partial\"";
      } else if (prompt.find("UNKNOWN_TOOL") != std::string::npos) {
        response["response"] = {{"type", "function_call"},
                                {"name", "not_declared"},
                                {"arguments", statewright::contracts::Json::object()}};
      } else if (prompt.find("submit_oiec_reasoning_object") !=
                 std::string::npos) {
        if (request.at("grammar") != "oiec_compact_tool_response") {
          response["status"] = "invalid_output";
          response["diagnostic"] =
              "structured reasoning used the wrong grammar";
        } else {
          response["response"] =
              {{"type", "function_call"},
               {"name", "submit_oiec_reasoning_object"},
               {"arguments", {{"answer", "no"}}},
               {"call_id", "call-reasoning"}};
        }
      } else if (prompt.find("CALL_TOOL") != std::string::npos) {
        response["response"] = {{"type", "function_call"},
                                {"name", "read_file"},
                                {"arguments", {{"path", "README.md"}}},
                                {"call_id", "call-1"}};
      } else {
        response["response"] =
            {{"type", "message"}, {"content", "Qwen3.8 response"}};
      }
      response["metrics"] =
          {{"prompt_tokens", 10}, {"output_tokens", 4}, {"total_ms", 5}};
    } else if (operation == "shutdown") {
      std::cout << statewright::contracts::canonical_json(response) << '\n'
                << std::flush;
      return 0;
    }
    std::cout << statewright::contracts::canonical_json(response) << '\n'
              << std::flush;
  }
  return 0;
}
