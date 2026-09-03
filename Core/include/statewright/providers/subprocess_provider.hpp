#pragma once

#include "statewright/contracts/canonical_json.hpp"
#include "statewright/providers/reasoning_provider.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace statewright::providers {

struct SubprocessProviderConfig final {
  std::string model;
  std::filesystem::path runner_path;
  std::filesystem::path model_path;
  std::string expected_model_sha256;
  std::filesystem::path grammar_dir;
  int context_budget_tokens = 6'000;
  int runtime_context_tokens = 0;
  int context_safety_margin_tokens = 512;
  int max_output_tokens = 2'048;
  int timeout_ms = 600'000;
  int max_reasoning_samples = 16;
  int llama_context_tokens = 8'192;
  int llama_gpu_layers = -1;
  int llama_threads = 0;
  int llama_seed = 1'234;
  int llama_temperature_bp = 1'000;
  int llama_top_p_bp = 9'500;
  int llama_top_k = 40;
};

class LlamaCppProcessProvider final : public ReasoningProvider {
public:
  explicit LlamaCppProcessProvider(SubprocessProviderConfig config);
  ~LlamaCppProcessProvider() override;

  LlamaCppProcessProvider(const LlamaCppProcessProvider &) = delete;
  LlamaCppProcessProvider &operator=(const LlamaCppProcessProvider &) = delete;

  [[nodiscard]] contracts::Json preflight();
  [[nodiscard]] contracts::Json
  create_response(const contracts::Json &request) override;
  [[nodiscard]] std::vector<contracts::Json> create_responses(
      const std::vector<contracts::Json> &requests,
      std::size_t max_responses) override;
  [[nodiscard]] int reasoning_role_batch_size() const noexcept override;

  [[nodiscard]] bool last_completion_request_sent() const noexcept;
  void close() noexcept;

private:
  [[nodiscard]] std::vector<std::string> runner_command() const;
  void ensure_process();
  void dispose_process(bool terminate) noexcept;
  [[nodiscard]] contracts::Json request(std::string_view operation,
                                        contracts::Json payload);
  [[nodiscard]] std::string next_request_id(
      std::string_view operation, const contracts::Json &payload);
  void write_line(const contracts::Json &payload);
  [[nodiscard]] std::string read_line_until(
      std::chrono::steady_clock::time_point deadline);
  void start_stderr_drain();
  [[nodiscard]] std::string stderr_diagnostic() const;

  SubprocessProviderConfig config_;
  mutable std::mutex mutex_;
  mutable std::mutex stderr_mutex_;
  std::thread stderr_thread_;
  std::uint64_t sequence_ = 0;
  int process_id_ = -1;
  int stdin_fd_ = -1;
  int stdout_fd_ = -1;
  int stderr_fd_ = -1;
  std::string stdout_buffer_;
  std::string stderr_buffer_;
  bool last_completion_request_sent_ = false;
};

[[nodiscard]] contracts::Json
validate_subprocess_output(const contracts::Json &payload,
                           const contracts::Json &tools);

} // namespace statewright::providers
