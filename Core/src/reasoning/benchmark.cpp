#include "statewright/reasoning/benchmark.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <limits>
#include <numeric>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace statewright::reasoning {
namespace {

constexpr int score_scale = 10'000;
constexpr std::array<std::string_view, 4> oracle_kinds{
    "exact", "contains", "hypothesis_label", "component_label"};
constexpr std::array<std::string_view, 6> terminal_states{
    "ANSWER", "EPISTEMIC_STOP", "INSUFFICIENT_EVIDENCE", "GOVERNANCE_STOP",
    "COMPUTE_BUDGET_EXHAUSTED", "NO_SURVIVING_HYPOTHESIS"};
constexpr std::array<std::string_view, 8> system_descriptor_keys{
    "system_id", "executor", "provider", "model", "reasoning_effort",
    "context_budget_tokens", "max_output_tokens", "decoding"};
constexpr std::array<std::string_view, 15> model_system_descriptor_keys{
    "system_id", "executor", "provider", "model", "reasoning_effort",
    "context_budget_tokens", "max_output_tokens", "decoding", "pipeline",
    "source_snapshot_hash", "prompt_template_hash", "evidence_context_mode",
    "provider_binding", "runtime_environment", "telemetry"};

[[noreturn]] void policy_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied, std::move(message));
}

template <std::size_t Size>
[[nodiscard]] bool contains(
    const std::array<std::string_view, Size> &values,
    std::string_view candidate) noexcept {
  return std::ranges::find(values, candidate) != values.end();
}

[[nodiscard]] std::string trim(std::string value) {
  const auto first = std::ranges::find_if_not(value, [](unsigned char byte) {
    return std::isspace(byte) != 0;
  });
  const auto last = std::find_if_not(
                        value.rbegin(), value.rend(), [](unsigned char byte) {
                          return std::isspace(byte) != 0;
                        })
                        .base();
  if (first >= last) {
    return {};
  }
  return std::string(first, last);
}

[[nodiscard]] std::vector<std::string>
canonical_strings(const std::vector<std::string> &values) {
  std::set<std::string> unique;
  for (const auto &value : values) {
    if (!value.empty()) {
      unique.insert(value);
    }
  }
  return {unique.begin(), unique.end()};
}

void validate_score(int value, std::string_view label) {
  if (value < 0 || value > score_scale) {
    policy_error(std::string(label) + " must be 0..10000");
  }
}

void validate_non_negative(std::int64_t value, std::string_view label) {
  if (value < 0) {
    policy_error(std::string(label) + " must be non-negative");
  }
}

[[nodiscard]] std::string signature_for(contracts::Json material) {
  material.erase("signature");
  return contracts::sha256_json(material);
}

void bind_signature(std::string &signature, const contracts::Json &value,
                    std::string_view label) {
  const auto expected = signature_for(value);
  if (!signature.empty() && signature != expected) {
    policy_error(std::string(label) + " signature mismatch");
  }
  signature = expected;
}

void require_exact_keys(const contracts::Json &value,
                        const std::set<std::string> &required,
                        const std::set<std::string> &optional,
                        std::string_view label) {
  if (!value.is_object()) {
    policy_error(std::string(label) + " must be an object");
  }
  std::set<std::string> actual;
  for (const auto &[key, ignored] : value.items()) {
    static_cast<void>(ignored);
    actual.insert(key);
  }
  std::vector<std::string> missing;
  std::ranges::set_difference(required, actual, std::back_inserter(missing));
  if (!missing.empty()) {
    policy_error(std::string(label) + " is missing required fields");
  }
  auto allowed = required;
  allowed.insert(optional.begin(), optional.end());
  std::vector<std::string> unknown;
  std::ranges::set_difference(actual, allowed, std::back_inserter(unknown));
  if (!unknown.empty()) {
    policy_error(std::string(label) + " contains unknown fields");
  }
}

template <std::size_t Size>
[[nodiscard]] std::set<std::string>
key_set(const std::array<std::string_view, Size> &keys) {
  std::set<std::string> result;
  for (const auto key : keys) {
    result.emplace(key);
  }
  return result;
}

[[nodiscard]] std::string required_string(const contracts::Json &value,
                                          std::string_view key,
                                          std::string_view label) {
  const auto &field = value.at(std::string(key));
  if (!field.is_string()) {
    policy_error(std::string(label) + " field must be a string: " +
                 std::string(key));
  }
  return field.get<std::string>();
}

[[nodiscard]] std::int64_t required_integer(const contracts::Json &value,
                                            std::string_view key,
                                            std::string_view label) {
  const auto &field = value.at(std::string(key));
  if (!field.is_number_integer()) {
    policy_error(std::string(label) + " field must be an integer: " +
                 std::string(key));
  }
  return field.get<std::int64_t>();
}

[[nodiscard]] int required_int(const contracts::Json &value,
                               std::string_view key,
                               std::string_view label) {
  const auto integer = required_integer(value, key, label);
  if (integer < std::numeric_limits<int>::min() ||
      integer > std::numeric_limits<int>::max()) {
    policy_error(std::string(label) + " integer is out of range: " +
                 std::string(key));
  }
  return static_cast<int>(integer);
}

[[nodiscard]] bool required_boolean(const contracts::Json &value,
                                    std::string_view key,
                                    std::string_view label) {
  const auto &field = value.at(std::string(key));
  if (!field.is_boolean()) {
    policy_error(std::string(label) + " field must be a boolean: " +
                 std::string(key));
  }
  return field.get<bool>();
}

[[nodiscard]] std::vector<std::string>
required_strings(const contracts::Json &value, std::string_view key,
                 std::string_view label) {
  const auto &field = value.at(std::string(key));
  if (!field.is_array()) {
    policy_error(std::string(label) + " field must be an array: " +
                 std::string(key));
  }
  std::vector<std::string> result;
  result.reserve(field.size());
  for (const auto &item : field) {
    if (!item.is_string()) {
      policy_error(std::string(label) + " array values must be strings: " +
                   std::string(key));
    }
    result.push_back(item.get<std::string>());
  }
  return result;
}

[[nodiscard]] bool valid_iso_date(std::string_view value) {
  if (!std::regex_match(std::string(value),
                        std::regex(R"(^[0-9]{4}-[0-9]{2}-[0-9]{2}$)"))) {
    return false;
  }
  const int year = std::stoi(std::string(value.substr(0, 4)));
  const unsigned int month =
      static_cast<unsigned int>(std::stoi(std::string(value.substr(5, 2))));
  const unsigned int day =
      static_cast<unsigned int>(std::stoi(std::string(value.substr(8, 2))));
  return std::chrono::year_month_day{
             std::chrono::year{year}, std::chrono::month{month},
             std::chrono::day{day}}
      .ok();
}

[[nodiscard]] std::string normalized_answer(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char byte) {
    return static_cast<char>(std::tolower(byte));
  });
  std::string result;
  bool separated = false;
  for (const char raw : value) {
    const auto byte = static_cast<unsigned char>(raw);
    if (std::isspace(byte) != 0) {
      separated = !result.empty();
      continue;
    }
    if (separated) {
      result.push_back(' ');
    }
    result.push_back(raw);
    separated = false;
  }
  return result;
}

[[nodiscard]] std::string strip_answer_punctuation(std::string value) {
  constexpr std::string_view punctuation = " .,:;!?\"'";
  const auto first = value.find_first_not_of(punctuation);
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(punctuation);
  return value.substr(first, last - first + 1);
}

[[nodiscard]] std::string hypothesis_label(std::string value) {
  value = strip_answer_punctuation(normalized_answer(std::move(value)));
  static const std::regex pattern(
      R"(^(?:hypothesis\s+)?([a-z0-9]+)(?:\s+is\s+(?:better|best|more\s+strongly)\s+supported)?$)");
  std::smatch match;
  if (!std::regex_match(value, match, pattern)) {
    return {};
  }
  return match[1].str();
}

[[nodiscard]] bool component_label_matches(std::string value,
                                           std::string expected) {
  const auto answer =
      strip_answer_punctuation(normalized_answer(std::move(value)));
  const auto label =
      strip_answer_punctuation(normalized_answer(std::move(expected)));
  return answer == label || answer == "the " + label ||
         answer == label + " is the earliest supported fault location" ||
         answer == "the " + label +
                       " is the earliest supported fault location" ||
         answer == label + " is the earliest fault location" ||
         answer == "the " + label + " is the earliest fault location";
}

void validate_signature_object(const contracts::Json &value,
                               std::string_view label) {
  if (!value.is_object()) {
    policy_error(std::string(label) + " must be an object");
  }
  const auto signature = value.value("signature", std::string{});
  if (signature.empty()) {
    policy_error(std::string(label) + " signature must be non-empty");
  }
  if (signature != signature_for(value)) {
    policy_error(std::string(label) + " signature mismatch");
  }
}

[[nodiscard]] std::int64_t optional_integer(const contracts::Json &value,
                                            std::string_view key,
                                            std::int64_t default_value,
                                            std::string_view label) {
  if (!value.contains(key)) {
    return default_value;
  }
  return required_integer(value, key, label);
}

void validate_system_descriptor(const contracts::Json &descriptor,
                                std::string_view execution_mode,
                                std::string_view source_manifest_hash) {
  if (!descriptor.is_object()) {
    policy_error("benchmark system descriptor must be an object");
  }
  const auto expected = execution_mode == "provider_bound"
                            ? key_set(model_system_descriptor_keys)
                            : key_set(system_descriptor_keys);
  std::set<std::string> actual;
  for (const auto &[key, ignored] : descriptor.items()) {
    static_cast<void>(ignored);
    actual.insert(key);
  }
  if (actual != expected) {
    policy_error("benchmark system descriptor fields mismatch");
  }
  const auto system_id = required_string(descriptor, "system_id", "benchmark system descriptor");
  if (!contains(benchmark_system_ids, system_id)) {
    policy_error("benchmark system descriptor has an invalid system_id");
  }
  if (execution_mode != "provider_bound") {
    return;
  }
  if (required_string(descriptor, "source_snapshot_hash", "benchmark system descriptor") !=
      source_manifest_hash) {
    policy_error("model benchmark system is bound to the wrong source snapshot");
  }
  const auto &provider_binding = descriptor.at("provider_binding");
  const auto &runtime = descriptor.at("runtime_environment");
  const auto &telemetry = descriptor.at("telemetry");
  validate_signature_object(provider_binding, "provider binding");
  validate_signature_object(runtime, "runtime environment");
  if (provider_binding.value("status", std::string{}) != "ready") {
    policy_error("model benchmark provider binding is not ready");
  }
  if (provider_binding.value("model", std::string{}) !=
      required_string(descriptor, "model", "benchmark system descriptor")) {
    policy_error("model benchmark descriptor model binding mismatch");
  }
  if (provider_binding.value("provider", std::string{}) !=
      required_string(descriptor, "provider", "benchmark system descriptor")) {
    policy_error("model benchmark descriptor provider binding mismatch");
  }
  if (optional_integer(provider_binding, "max_transport_retries", -1,
                       "provider binding") != 0) {
    policy_error("model benchmark transport retries must remain disabled");
  }
  if (!telemetry.is_object()) {
    policy_error("model benchmark telemetry must be an object");
  }
  const auto provider_calls =
      optional_integer(telemetry, "provider_calls", -1, "benchmark telemetry");
  const auto &response_hashes = telemetry.contains("response_hashes")
                                    ? telemetry.at("response_hashes")
                                    : contracts::Json::array();
  if (provider_calls < 1 || !response_hashes.is_array()) {
    policy_error("model benchmark telemetry is incomplete");
  }
  if (response_hashes.size() > static_cast<std::size_t>(provider_calls)) {
    policy_error("model benchmark response hash count exceeds provider calls");
  }
  const auto &runtime_hashes = telemetry.contains("runtime_observation_hashes")
                                   ? telemetry.at("runtime_observation_hashes")
                                   : contracts::Json::array();
  if (!runtime_hashes.is_array() || runtime_hashes.empty()) {
    policy_error("model benchmark has no in-call runtime observation");
  }
  const auto failures = optional_integer(telemetry, "provider_failures", 0,
                                         "benchmark telemetry");
  const auto successful_calls = provider_calls - failures;
  if (successful_calls > 0) {
    if (!telemetry.contains("observed_temperatures") ||
        !telemetry.at("observed_temperatures").is_array() ||
        telemetry.at("observed_temperatures").empty() ||
        !telemetry.contains("observed_top_p") ||
        !telemetry.at("observed_top_p").is_array() ||
        telemetry.at("observed_top_p").empty()) {
      policy_error("model benchmark did not record observed sampling values");
    }
  }
  if (telemetry.value("nondeterminism_status", std::string{}) !=
      "single_run_not_reproducibility_evidence") {
    policy_error("model benchmark nondeterminism status is missing");
  }
  const auto repair_count = optional_integer(
      telemetry, "reasoning_validation_repairs", 0, "benchmark telemetry");
  const auto &repair_records = telemetry.contains("repair_records")
                                   ? telemetry.at("repair_records")
                                   : contracts::Json::array();
  if (repair_count < 0 || !repair_records.is_array()) {
    policy_error("model benchmark reasoning repair telemetry is invalid");
  }
  if (repair_records.size() != static_cast<std::size_t>(repair_count)) {
    policy_error("model benchmark reasoning repair count mismatch");
  }
  if (telemetry.contains("certificate_signatures") &&
      !telemetry.at("certificate_signatures").is_null()) {
    const auto &signatures = telemetry.at("certificate_signatures");
    if (!signatures.is_array()) {
      policy_error("model benchmark certificate signatures must be a sequence");
    }
    static const std::regex digest(R"(^[0-9a-f]{64}$)");
    for (const auto &signature : signatures) {
      if (!signature.is_string()) {
        policy_error("model benchmark certificate signature is invalid");
      }
      const auto text = signature.get<std::string>();
      if (!text.empty() && !std::regex_match(text, digest)) {
        policy_error("model benchmark certificate signature is invalid");
      }
    }
  }
}

[[nodiscard]] BenchmarkTask parse_task(const contracts::Json &value) {
  require_exact_keys(
      value,
      {"category", "oracle", "oracle_method", "problem_id", "prompt",
       "required_counterexamples", "required_evidence_ids", "schema_version",
       "source_refs"},
      {"signature"}, "benchmark task");
  const auto &oracle = value.at("oracle");
  require_exact_keys(oracle, {"expected", "kind"}, {}, "benchmark oracle");
  return canonicalize_benchmark_task(
      {.schema_version = required_int(value, "schema_version", "benchmark task"),
       .problem_id = required_string(value, "problem_id", "benchmark task"),
       .category = required_string(value, "category", "benchmark task"),
       .prompt = required_string(value, "prompt", "benchmark task"),
       .oracle =
           {.kind = required_string(oracle, "kind", "benchmark oracle"),
            .expected =
                required_string(oracle, "expected", "benchmark oracle")},
       .oracle_method =
           required_string(value, "oracle_method", "benchmark task"),
       .required_evidence_ids = required_strings(
           value, "required_evidence_ids", "benchmark task"),
       .required_counterexamples = required_strings(
           value, "required_counterexamples", "benchmark task"),
       .source_refs = required_strings(value, "source_refs", "benchmark task"),
       .signature = value.value("signature", std::string{})});
}

[[nodiscard]] BenchmarkObservation
parse_observation(const contracts::Json &value) {
  require_exact_keys(
      value,
      {"answer", "collisions", "confidence_bp", "counterexamples",
       "evidence_ids", "problem_id", "retries", "schema_version", "system_id",
       "terminal_state", "token_count", "tool_calls", "wall_time_ms"},
      {"signature"}, "benchmark observation");
  return canonicalize_benchmark_observation(
      {.schema_version =
           required_int(value, "schema_version", "benchmark observation"),
       .problem_id =
           required_string(value, "problem_id", "benchmark observation"),
       .system_id =
           required_string(value, "system_id", "benchmark observation"),
       .answer = required_string(value, "answer", "benchmark observation"),
       .confidence_bp =
           required_int(value, "confidence_bp", "benchmark observation"),
       .evidence_ids =
           required_strings(value, "evidence_ids", "benchmark observation"),
       .counterexamples = required_strings(value, "counterexamples",
                                            "benchmark observation"),
       .token_count =
           required_integer(value, "token_count", "benchmark observation"),
       .tool_calls =
           required_integer(value, "tool_calls", "benchmark observation"),
       .collisions =
           required_integer(value, "collisions", "benchmark observation"),
       .retries =
           required_integer(value, "retries", "benchmark observation"),
       .wall_time_ms =
           required_integer(value, "wall_time_ms", "benchmark observation"),
       .terminal_state = required_string(value, "terminal_state",
                                         "benchmark observation"),
       .signature = value.value("signature", std::string{})});
}

[[nodiscard]] BenchmarkResult parse_result(const contracts::Json &value) {
  require_exact_keys(
      value,
      {"answer", "calibration_error_bp", "category", "collisions",
       "correctness_bp", "counterexample_detection_bp", "evidence_coverage_bp",
       "observation_signature", "problem_id", "retries", "signature",
       "system_id", "task_signature", "terminal_state", "token_count",
       "tool_calls", "wall_time_ms"},
      {}, "benchmark result");
  return canonicalize_benchmark_result(
      {.problem_id = required_string(value, "problem_id", "benchmark result"),
       .category = required_string(value, "category", "benchmark result"),
       .system_id = required_string(value, "system_id", "benchmark result"),
       .task_signature =
           required_string(value, "task_signature", "benchmark result"),
       .observation_signature =
           required_string(value, "observation_signature", "benchmark result"),
       .answer = required_string(value, "answer", "benchmark result"),
       .correctness_bp = required_int(value, "correctness_bp", "benchmark result"),
       .evidence_coverage_bp =
           required_int(value, "evidence_coverage_bp", "benchmark result"),
       .counterexample_detection_bp = required_int(
           value, "counterexample_detection_bp", "benchmark result"),
       .calibration_error_bp =
           required_int(value, "calibration_error_bp", "benchmark result"),
       .token_count = required_integer(value, "token_count", "benchmark result"),
       .tool_calls = required_integer(value, "tool_calls", "benchmark result"),
       .collisions = required_integer(value, "collisions", "benchmark result"),
       .retries = required_integer(value, "retries", "benchmark result"),
       .wall_time_ms =
           required_integer(value, "wall_time_ms", "benchmark result"),
       .terminal_state =
           required_string(value, "terminal_state", "benchmark result"),
       .signature = required_string(value, "signature", "benchmark result")});
}

[[nodiscard]] BenchmarkSystemSummary
parse_summary(const contracts::Json &value) {
  require_exact_keys(
      value,
      {"accuracy_bp", "counterexample_detection_bp", "evidence_coverage_bp",
       "mean_calibration_error_bp", "problem_count", "signature", "system_id",
       "total_collisions", "total_retries", "total_tokens", "total_tool_calls",
       "total_wall_time_ms"},
      {}, "benchmark system summary");
  return canonicalize_benchmark_summary(
      {.system_id =
           required_string(value, "system_id", "benchmark system summary"),
       .problem_count = required_integer(value, "problem_count",
                                         "benchmark system summary"),
       .accuracy_bp =
           required_int(value, "accuracy_bp", "benchmark system summary"),
       .evidence_coverage_bp = required_int(
           value, "evidence_coverage_bp", "benchmark system summary"),
       .counterexample_detection_bp = required_int(
           value, "counterexample_detection_bp", "benchmark system summary"),
       .mean_calibration_error_bp = required_int(
           value, "mean_calibration_error_bp", "benchmark system summary"),
       .total_tokens = required_integer(value, "total_tokens",
                                        "benchmark system summary"),
       .total_tool_calls = required_integer(value, "total_tool_calls",
                                            "benchmark system summary"),
       .total_collisions = required_integer(value, "total_collisions",
                                            "benchmark system summary"),
       .total_retries = required_integer(value, "total_retries",
                                         "benchmark system summary"),
       .total_wall_time_ms = required_integer(value, "total_wall_time_ms",
                                              "benchmark system summary"),
       .signature =
           required_string(value, "signature", "benchmark system summary")});
}

[[nodiscard]] SourceFileRecord parse_source_file(const contracts::Json &value) {
  require_exact_keys(value, {"path", "sha256"}, {}, "benchmark source file");
  return {.path = required_string(value, "path", "benchmark source file"),
          .sha256 =
              required_string(value, "sha256", "benchmark source file")};
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path &path,
                                         bool binary) {
  std::ifstream input(path, binary ? std::ios::binary : std::ios::in);
  if (!input.is_open()) {
    throw common::Error(common::ErrorCode::filesystem_failure,
                        "cannot open benchmark file: " + path.string());
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  if (input.bad()) {
    throw common::Error(common::ErrorCode::filesystem_failure,
                        "cannot read benchmark file: " + path.string());
  }
  return contents.str();
}

[[nodiscard]] std::vector<std::string> run_task_ids(const BenchmarkRun &run) {
  std::vector<std::string> task_ids;
  for (std::size_t offset = 0; offset < run.results.size();
       offset += benchmark_system_ids.size()) {
    if (offset + benchmark_system_ids.size() > run.results.size()) {
      policy_error("benchmark shard result group is incomplete");
    }
    std::set<std::string> problem_ids;
    std::set<std::string> task_signatures;
    for (std::size_t index = 0; index < benchmark_system_ids.size(); ++index) {
      const auto &result = run.results[offset + index];
      if (result.system_id != benchmark_system_ids[index]) {
        policy_error(
            "benchmark shard result systems are not in canonical order");
      }
      problem_ids.insert(result.problem_id);
      task_signatures.insert(result.task_signature);
    }
    if (problem_ids.size() != 1 || task_signatures.size() != 1) {
      policy_error("benchmark shard result group is inconsistent");
    }
    task_ids.push_back(*problem_ids.begin());
  }
  if (task_ids.size() != static_cast<std::size_t>(run.task_count) ||
      std::set<std::string>(task_ids.begin(), task_ids.end()).size() !=
          task_ids.size()) {
    policy_error("benchmark shard task identity is inconsistent");
  }
  return task_ids;
}

[[nodiscard]] contracts::Json
identity_descriptor_from_system(const contracts::Json &descriptor,
                                std::string_view execution_mode) {
  auto material = descriptor;
  if (execution_mode == "provider_bound") {
    material.erase("runtime_environment");
    material.erase("telemetry");
  }
  return material;
}

[[nodiscard]] contracts::Json array_or_empty(const contracts::Json &value,
                                             std::string_view key) {
  if (!value.contains(key)) {
    return contracts::Json::array();
  }
  if (!value.at(std::string(key)).is_array()) {
    policy_error("provider benchmark telemetry collection must be an array");
  }
  return value.at(std::string(key));
}

void append_array(contracts::Json &target, const contracts::Json &source) {
  for (const auto &item : source) {
    target.push_back(item);
  }
}

[[nodiscard]] contracts::Json unique_sorted_array(
    const std::vector<contracts::Json> &arrays) {
  std::map<std::string, contracts::Json> unique;
  for (const auto &array : arrays) {
    if (!array.is_array()) {
      policy_error("provider benchmark telemetry collection must be an array");
    }
    for (const auto &item : array) {
      unique[contracts::canonical_json(item)] = item;
    }
  }
  contracts::Json result = contracts::Json::array();
  for (auto &[ignored, item] : unique) {
    static_cast<void>(ignored);
    result.push_back(std::move(item));
  }
  return result;
}

[[nodiscard]] contracts::Json merge_provider_system_descriptors(
    const std::vector<contracts::Json> &descriptors) {
  if (descriptors.empty()) {
    policy_error("provider benchmark merge has no system descriptors");
  }
  auto identity = descriptors.front();
  if (!identity.is_object() || !identity.contains("runtime_environment") ||
      !identity.contains("telemetry")) {
    policy_error("provider benchmark shard descriptor is incomplete");
  }
  const auto runtime = identity.at("runtime_environment");
  identity.erase("runtime_environment");
  identity.erase("telemetry");
  std::vector<contracts::Json> telemetry_rows;
  for (const auto &descriptor : descriptors) {
    if (!descriptor.is_object() || !descriptor.contains("runtime_environment") ||
        !descriptor.contains("telemetry")) {
      policy_error("provider benchmark shard descriptor is incomplete");
    }
    auto candidate = descriptor;
    candidate.erase("runtime_environment");
    candidate.erase("telemetry");
    if (candidate != identity) {
      policy_error("provider benchmark shard identity mismatch");
    }
    if (descriptor.at("runtime_environment") != runtime) {
      policy_error("provider benchmark shard runtime mismatch");
    }
    const auto telemetry = descriptor.at("telemetry");
    if (!telemetry.is_object() ||
        telemetry.value("nondeterminism_status", std::string{}) !=
            "single_run_not_reproducibility_evidence") {
      policy_error("provider benchmark shard nondeterminism status mismatch");
    }
    telemetry_rows.push_back(telemetry);
  }
  contracts::Json failure_records = contracts::Json::array();
  contracts::Json repair_records = contracts::Json::array();
  contracts::Json response_hashes = contracts::Json::array();
  std::vector<contracts::Json> runtime_hash_rows;
  std::vector<contracts::Json> temperature_rows;
  std::vector<contracts::Json> top_p_rows;
  std::int64_t provider_calls = 0;
  std::int64_t input_tokens = 0;
  std::int64_t output_tokens = 0;
  std::int64_t total_tokens = 0;
  std::int64_t provider_failures = 0;
  std::int64_t reasoning_repairs = 0;
  bool any_certificates = false;
  bool all_certificates = true;
  contracts::Json certificate_signatures = contracts::Json::array();
  for (const auto &row : telemetry_rows) {
    provider_calls += required_integer(row, "provider_calls", "benchmark telemetry");
    input_tokens += required_integer(row, "input_tokens", "benchmark telemetry");
    output_tokens += required_integer(row, "output_tokens", "benchmark telemetry");
    total_tokens += required_integer(row, "total_tokens", "benchmark telemetry");
    provider_failures +=
        required_integer(row, "provider_failures", "benchmark telemetry");
    reasoning_repairs += optional_integer(
        row, "reasoning_validation_repairs", 0, "benchmark telemetry");
    append_array(failure_records, array_or_empty(row, "failure_records"));
    append_array(repair_records, array_or_empty(row, "repair_records"));
    append_array(response_hashes, array_or_empty(row, "response_hashes"));
    runtime_hash_rows.push_back(
        array_or_empty(row, "runtime_observation_hashes"));
    temperature_rows.push_back(array_or_empty(row, "observed_temperatures"));
    top_p_rows.push_back(array_or_empty(row, "observed_top_p"));
    any_certificates = any_certificates || row.contains("certificate_signatures");
    all_certificates = all_certificates && row.contains("certificate_signatures");
    if (row.contains("certificate_signatures")) {
      append_array(certificate_signatures,
                   array_or_empty(row, "certificate_signatures"));
    }
  }
  if (any_certificates && !all_certificates) {
    policy_error("provider benchmark shard certificate telemetry mismatch");
  }
  contracts::Json telemetry =
      {{"provider_calls", provider_calls},
       {"input_tokens", input_tokens},
       {"output_tokens", output_tokens},
       {"total_tokens", total_tokens},
       {"provider_failures", provider_failures},
       {"failure_records", std::move(failure_records)},
       {"reasoning_validation_repairs", reasoning_repairs},
       {"repair_records", std::move(repair_records)},
       {"response_hashes", std::move(response_hashes)},
       {"runtime_observation_hashes",
        unique_sorted_array(runtime_hash_rows)},
       {"observed_temperatures", unique_sorted_array(temperature_rows)},
       {"observed_top_p", unique_sorted_array(top_p_rows)},
       {"nondeterminism_status",
        "single_run_not_reproducibility_evidence"}};
  if (any_certificates) {
    telemetry["certificate_signatures"] = std::move(certificate_signatures);
  }
  auto merged = identity;
  merged["runtime_environment"] = runtime;
  merged["telemetry"] = std::move(telemetry);
  return merged;
}

} // namespace

contracts::Json to_json(const BenchmarkOracle &value) {
  return {{"kind", value.kind}, {"expected", value.expected}};
}

contracts::Json to_json(const BenchmarkTask &value) {
  return {{"schema_version", value.schema_version},
          {"problem_id", value.problem_id},
          {"category", value.category},
          {"prompt", value.prompt},
          {"oracle", to_json(value.oracle)},
          {"oracle_method", value.oracle_method},
          {"required_evidence_ids", value.required_evidence_ids},
          {"required_counterexamples", value.required_counterexamples},
          {"source_refs", value.source_refs},
          {"signature", value.signature}};
}

contracts::Json to_json(const BenchmarkObservation &value) {
  return {{"schema_version", value.schema_version},
          {"problem_id", value.problem_id},
          {"system_id", value.system_id},
          {"answer", value.answer},
          {"confidence_bp", value.confidence_bp},
          {"evidence_ids", value.evidence_ids},
          {"counterexamples", value.counterexamples},
          {"token_count", value.token_count},
          {"tool_calls", value.tool_calls},
          {"collisions", value.collisions},
          {"retries", value.retries},
          {"wall_time_ms", value.wall_time_ms},
          {"terminal_state", value.terminal_state},
          {"signature", value.signature}};
}

contracts::Json to_json(const BenchmarkResult &value) {
  return {{"problem_id", value.problem_id},
          {"category", value.category},
          {"system_id", value.system_id},
          {"task_signature", value.task_signature},
          {"observation_signature", value.observation_signature},
          {"answer", value.answer},
          {"correctness_bp", value.correctness_bp},
          {"evidence_coverage_bp", value.evidence_coverage_bp},
          {"counterexample_detection_bp", value.counterexample_detection_bp},
          {"calibration_error_bp", value.calibration_error_bp},
          {"token_count", value.token_count},
          {"tool_calls", value.tool_calls},
          {"collisions", value.collisions},
          {"retries", value.retries},
          {"wall_time_ms", value.wall_time_ms},
          {"terminal_state", value.terminal_state},
          {"signature", value.signature}};
}

contracts::Json to_json(const BenchmarkSystemSummary &value) {
  return {{"system_id", value.system_id},
          {"problem_count", value.problem_count},
          {"accuracy_bp", value.accuracy_bp},
          {"evidence_coverage_bp", value.evidence_coverage_bp},
          {"counterexample_detection_bp", value.counterexample_detection_bp},
          {"mean_calibration_error_bp", value.mean_calibration_error_bp},
          {"total_tokens", value.total_tokens},
          {"total_tool_calls", value.total_tool_calls},
          {"total_collisions", value.total_collisions},
          {"total_retries", value.total_retries},
          {"total_wall_time_ms", value.total_wall_time_ms},
          {"signature", value.signature}};
}

contracts::Json to_json(const SourceFileRecord &value) {
  return {{"path", value.path}, {"sha256", value.sha256}};
}

contracts::Json to_json(const BenchmarkRun &value) {
  contracts::Json source_files = contracts::Json::array();
  for (const auto &item : value.source_files) {
    source_files.push_back(to_json(item));
  }
  contracts::Json results = contracts::Json::array();
  for (const auto &item : value.results) {
    results.push_back(to_json(item));
  }
  contracts::Json summaries = contracts::Json::array();
  for (const auto &item : value.summaries) {
    summaries.push_back(to_json(item));
  }
  return {{"schema_version", value.schema_version},
          {"benchmark_id", value.benchmark_id},
          {"generated_on", value.generated_on},
          {"execution_mode", value.execution_mode},
          {"qualification_status", value.qualification_status},
          {"performance_claim_allowed", value.performance_claim_allowed},
          {"package_version", value.package_version},
          {"git_head", value.git_head},
          {"worktree_dirty", value.worktree_dirty},
          {"source_manifest_hash", value.source_manifest_hash},
          {"source_files", std::move(source_files)},
          {"task_count", value.task_count},
          {"systems", value.systems},
          {"results", std::move(results)},
          {"summaries", std::move(summaries)},
          {"signature", value.signature}};
}

contracts::Json BenchmarkExecutor::identity_descriptor() const {
  return descriptor();
}

void BenchmarkExecutor::release_runtime() {}

FixtureBenchmarkExecutor::FixtureBenchmarkExecutor(
    std::string system_id,
    std::map<std::string, BenchmarkObservation> observations)
    : system_id_(std::move(system_id)), observations_(std::move(observations)) {
  if (!contains(benchmark_system_ids, system_id_)) {
    policy_error("unknown benchmark system: " + system_id_);
  }
  for (auto &[problem_id, observation] : observations_) {
    observation = canonicalize_benchmark_observation(std::move(observation));
    if (problem_id != observation.problem_id ||
        observation.system_id != system_id_) {
      policy_error("fixture observation system or problem mismatch");
    }
  }
}

std::string_view FixtureBenchmarkExecutor::system_id() const noexcept {
  return system_id_;
}

contracts::Json FixtureBenchmarkExecutor::descriptor() const {
  return {{"system_id", system_id_},
          {"executor", "FixtureBenchmarkExecutor"},
          {"provider", "deterministic-fixture-v1"},
          {"model", "recorded-output"},
          {"reasoning_effort", "fixture"},
          {"context_budget_tokens", 0},
          {"max_output_tokens", 0},
          {"decoding", "recorded"}};
}

BenchmarkObservation
FixtureBenchmarkExecutor::execute(const BenchmarkTask &task) {
  const auto found = observations_.find(task.problem_id);
  if (found == observations_.end()) {
    policy_error("fixture system " + system_id_ +
                 " has no observation for " + task.problem_id);
  }
  if (found->second.system_id != system_id_) {
    policy_error("fixture observation system mismatch");
  }
  return found->second;
}

BenchmarkTask canonicalize_benchmark_task(BenchmarkTask value) {
  if (value.schema_version != 1) {
    policy_error("benchmark task schema_version must be 1");
  }
  if (trim(value.problem_id).empty() || trim(value.prompt).empty()) {
    policy_error("benchmark task identity and prompt must be non-empty");
  }
  if (!contains(benchmark_categories, value.category)) {
    policy_error("invalid benchmark category: " + value.category);
  }
  if (!contains(oracle_kinds, value.oracle.kind)) {
    policy_error("invalid benchmark oracle kind: " + value.oracle.kind);
  }
  if (trim(value.oracle.expected).empty()) {
    policy_error("benchmark oracle expected value must be non-empty");
  }
  if (trim(value.oracle_method).empty()) {
    policy_error("benchmark oracle method must be explicit");
  }
  value.required_evidence_ids = canonical_strings(value.required_evidence_ids);
  value.required_counterexamples =
      canonical_strings(value.required_counterexamples);
  value.source_refs = canonical_strings(value.source_refs);
  bind_signature(value.signature, to_json(value), "benchmark task");
  return value;
}

BenchmarkObservation
canonicalize_benchmark_observation(BenchmarkObservation value) {
  if (value.schema_version != 1) {
    policy_error("benchmark observation schema_version must be 1");
  }
  if (trim(value.problem_id).empty() ||
      !contains(benchmark_system_ids, value.system_id)) {
    policy_error("benchmark observation identity is invalid");
  }
  if (trim(value.answer).empty() && value.terminal_state == "ANSWER") {
    policy_error("answer terminal state requires a non-empty answer");
  }
  validate_score(value.confidence_bp, "benchmark confidence");
  validate_non_negative(value.token_count, "benchmark token_count");
  validate_non_negative(value.tool_calls, "benchmark tool_calls");
  validate_non_negative(value.collisions, "benchmark collisions");
  validate_non_negative(value.retries, "benchmark retries");
  validate_non_negative(value.wall_time_ms, "benchmark wall_time_ms");
  if (!contains(terminal_states, value.terminal_state)) {
    policy_error("invalid benchmark terminal state: " + value.terminal_state);
  }
  value.evidence_ids = canonical_strings(value.evidence_ids);
  value.counterexamples = canonical_strings(value.counterexamples);
  bind_signature(value.signature, to_json(value), "benchmark observation");
  return value;
}

BenchmarkResult canonicalize_benchmark_result(BenchmarkResult value) {
  if (!contains(benchmark_system_ids, value.system_id)) {
    policy_error("benchmark result system is invalid");
  }
  if (!contains(benchmark_categories, value.category)) {
    policy_error("benchmark result category is invalid");
  }
  if (!contains(terminal_states, value.terminal_state)) {
    policy_error("benchmark result terminal state is invalid");
  }
  validate_score(value.correctness_bp, "benchmark result correctness_bp");
  validate_score(value.evidence_coverage_bp,
                 "benchmark result evidence_coverage_bp");
  validate_score(value.counterexample_detection_bp,
                 "benchmark result counterexample_detection_bp");
  validate_score(value.calibration_error_bp,
                 "benchmark result calibration_error_bp");
  validate_non_negative(value.token_count, "benchmark result token_count");
  validate_non_negative(value.tool_calls, "benchmark result tool_calls");
  validate_non_negative(value.collisions, "benchmark result collisions");
  validate_non_negative(value.retries, "benchmark result retries");
  validate_non_negative(value.wall_time_ms, "benchmark result wall_time_ms");
  bind_signature(value.signature, to_json(value), "benchmark result");
  return value;
}

BenchmarkSystemSummary
canonicalize_benchmark_summary(BenchmarkSystemSummary value) {
  if (!contains(benchmark_system_ids, value.system_id) ||
      value.problem_count < 1) {
    policy_error("benchmark system summary identity is invalid");
  }
  validate_score(value.accuracy_bp, "benchmark summary accuracy_bp");
  validate_score(value.evidence_coverage_bp,
                 "benchmark summary evidence_coverage_bp");
  validate_score(value.counterexample_detection_bp,
                 "benchmark summary counterexample_detection_bp");
  validate_score(value.mean_calibration_error_bp,
                 "benchmark summary mean_calibration_error_bp");
  validate_non_negative(value.total_tokens, "benchmark summary total_tokens");
  validate_non_negative(value.total_tool_calls,
                        "benchmark summary total_tool_calls");
  validate_non_negative(value.total_collisions,
                        "benchmark summary total_collisions");
  validate_non_negative(value.total_retries,
                        "benchmark summary total_retries");
  validate_non_negative(value.total_wall_time_ms,
                        "benchmark summary total_wall_time_ms");
  bind_signature(value.signature, to_json(value), "benchmark system summary");
  return value;
}

BenchmarkRun canonicalize_benchmark_run(BenchmarkRun value) {
  if (value.schema_version != 1 || value.task_count < 1) {
    policy_error("benchmark run schema or task count is invalid");
  }
  if (!valid_iso_date(value.generated_on)) {
    policy_error("benchmark generated_on must be an ISO date");
  }
  if (value.execution_mode == "deterministic_fixture") {
    if (value.qualification_status != development_fixture_qualification_status) {
      policy_error("benchmark qualification status does not match execution mode");
    }
  } else if (value.execution_mode == "provider_bound") {
    if (value.qualification_status != development_model_qualification_status &&
        value.qualification_status != held_out_model_qualification_status) {
      policy_error("benchmark qualification status does not match execution mode");
    }
  } else {
    policy_error("benchmark execution mode is invalid");
  }
  if (value.performance_claim_allowed) {
    policy_error("development benchmark cannot authorize a performance claim");
  }
  std::ranges::sort(value.source_files, {}, &SourceFileRecord::path);
  for (auto &result : value.results) {
    result = canonicalize_benchmark_result(std::move(result));
  }
  for (auto &summary : value.summaries) {
    summary = canonicalize_benchmark_summary(std::move(summary));
  }
  std::ranges::sort(value.summaries, {}, &BenchmarkSystemSummary::system_id);
  if (value.results.size() !=
      static_cast<std::size_t>(value.task_count) * benchmark_system_ids.size()) {
    policy_error("benchmark result count does not match task/system cardinality");
  }
  for (std::size_t index = 0; index < benchmark_system_ids.size(); ++index) {
    if (index >= value.summaries.size() ||
        value.summaries[index].system_id != benchmark_system_ids[index]) {
      policy_error("benchmark summaries do not cover the canonical systems");
    }
  }
  if (value.summaries.size() != benchmark_system_ids.size()) {
    policy_error("benchmark summaries do not cover the canonical systems");
  }
  contracts::Json source_files = contracts::Json::array();
  for (const auto &record : value.source_files) {
    source_files.push_back(to_json(record));
  }
  if (value.source_manifest_hash != contracts::sha256_json(source_files)) {
    policy_error("benchmark source manifest hash mismatch");
  }
  if (value.systems.size() != benchmark_system_ids.size()) {
    policy_error("benchmark system descriptors must use canonical order");
  }
  for (std::size_t index = 0; index < value.systems.size(); ++index) {
    const auto &descriptor = value.systems[index];
    if (!descriptor.is_object() ||
        descriptor.value("system_id", std::string{}) !=
            benchmark_system_ids[index]) {
      policy_error("benchmark system descriptors must use canonical order");
    }
    validate_system_descriptor(descriptor, value.execution_mode,
                               value.source_manifest_hash);
  }
  bind_signature(value.signature, to_json(value), "benchmark run");
  return value;
}

void require_benchmark_run_integrity(const BenchmarkRun &run) {
  static_cast<void>(canonicalize_benchmark_run(run));
}

BenchmarkResult score_observation(const BenchmarkTask &task_value,
                                  const BenchmarkObservation &observation_value) {
  const auto task = canonicalize_benchmark_task(task_value);
  const auto observation =
      canonicalize_benchmark_observation(observation_value);
  if (observation.problem_id != task.problem_id) {
    policy_error("benchmark observation problem mismatch");
  }
  const auto answer = normalized_answer(observation.answer);
  const auto expected = normalized_answer(task.oracle.expected);
  bool correct = false;
  if (task.oracle.kind == "exact") {
    correct = answer == expected;
  } else if (task.oracle.kind == "contains") {
    correct = answer.find(expected) != std::string::npos;
  } else if (task.oracle.kind == "hypothesis_label") {
    correct = hypothesis_label(observation.answer) == expected;
  } else {
    correct = component_label_matches(observation.answer, expected);
  }
  correct = correct && observation.terminal_state == "ANSWER";
  const int correctness = correct ? score_scale : 0;
  const std::set<std::string> required_evidence(task.required_evidence_ids.begin(),
                                                task.required_evidence_ids.end());
  const std::set<std::string> reported_evidence(observation.evidence_ids.begin(),
                                                observation.evidence_ids.end());
  std::vector<std::string> unknown_evidence;
  std::ranges::set_difference(reported_evidence, required_evidence,
                              std::back_inserter(unknown_evidence));
  if (!unknown_evidence.empty()) {
    policy_error("benchmark observation cites undeclared evidence");
  }
  std::vector<std::string> evidence_intersection;
  std::ranges::set_intersection(required_evidence, reported_evidence,
                                std::back_inserter(evidence_intersection));
  const int evidence_coverage = required_evidence.empty()
                                    ? score_scale
                                    : static_cast<int>(
                                          evidence_intersection.size() *
                                          static_cast<std::size_t>(score_scale) /
                                          required_evidence.size());
  std::set<std::string> required_counterexamples;
  for (const auto &item : task.required_counterexamples) {
    required_counterexamples.insert(normalized_answer(item));
  }
  std::set<std::string> reported_counterexamples;
  for (const auto &item : observation.counterexamples) {
    reported_counterexamples.insert(normalized_answer(item));
  }
  std::vector<std::string> counterexample_intersection;
  std::ranges::set_intersection(required_counterexamples,
                                reported_counterexamples,
                                std::back_inserter(counterexample_intersection));
  const int counterexample_detection =
      required_counterexamples.empty()
          ? score_scale
          : static_cast<int>(counterexample_intersection.size() *
                             static_cast<std::size_t>(score_scale) /
                             required_counterexamples.size());
  return canonicalize_benchmark_result(
      {.problem_id = task.problem_id,
       .category = task.category,
       .system_id = observation.system_id,
       .task_signature = task.signature,
       .observation_signature = observation.signature,
       .answer = observation.answer,
       .correctness_bp = correctness,
       .evidence_coverage_bp = evidence_coverage,
       .counterexample_detection_bp = counterexample_detection,
       .calibration_error_bp =
           std::abs(observation.confidence_bp - correctness),
       .token_count = observation.token_count,
       .tool_calls = observation.tool_calls,
       .collisions = observation.collisions,
       .retries = observation.retries,
       .wall_time_ms = observation.wall_time_ms,
       .terminal_state = observation.terminal_state,
       .signature = {}});
}

BenchmarkSystemSummary
summarize_results(std::string_view system_id,
                  const std::vector<BenchmarkResult> &results) {
  if (!contains(benchmark_system_ids, system_id)) {
    policy_error("unknown benchmark system: " + std::string(system_id));
  }
  std::vector<BenchmarkResult> selected;
  for (const auto &result : results) {
    const auto canonical = canonicalize_benchmark_result(result);
    if (canonical.system_id == system_id) {
      selected.push_back(canonical);
    }
  }
  if (selected.empty()) {
    policy_error("benchmark has no results for system: " +
                 std::string(system_id));
  }
  const auto count = static_cast<std::int64_t>(selected.size());
  const auto sum_score = [&selected](auto member) {
    return std::accumulate(
        selected.begin(), selected.end(), std::int64_t{0},
        [member](std::int64_t total, const BenchmarkResult &result) {
          return total + result.*member;
        });
  };
  const auto sum_count = [&selected](auto member) {
    return std::accumulate(
        selected.begin(), selected.end(), std::int64_t{0},
        [member](std::int64_t total, const BenchmarkResult &result) {
          return total + result.*member;
        });
  };
  return canonicalize_benchmark_summary(
      {.system_id = std::string(system_id),
       .problem_count = count,
       .accuracy_bp = static_cast<int>(
           sum_score(&BenchmarkResult::correctness_bp) / count),
       .evidence_coverage_bp = static_cast<int>(
           sum_score(&BenchmarkResult::evidence_coverage_bp) / count),
       .counterexample_detection_bp = static_cast<int>(
           sum_score(&BenchmarkResult::counterexample_detection_bp) / count),
       .mean_calibration_error_bp = static_cast<int>(
           sum_score(&BenchmarkResult::calibration_error_bp) / count),
       .total_tokens = sum_count(&BenchmarkResult::token_count),
       .total_tool_calls = sum_count(&BenchmarkResult::tool_calls),
       .total_collisions = sum_count(&BenchmarkResult::collisions),
       .total_retries = sum_count(&BenchmarkResult::retries),
       .total_wall_time_ms = sum_count(&BenchmarkResult::wall_time_ms),
       .signature = {}});
}

std::vector<BenchmarkTask>
load_benchmark_tasks(const std::filesystem::path &task_root) {
  std::vector<std::filesystem::path> paths;
  std::error_code error;
  if (std::filesystem::is_regular_file(task_root, error) && !error) {
    paths.push_back(task_root);
  } else if (std::filesystem::is_directory(task_root, error) && !error) {
    for (const auto &entry : std::filesystem::directory_iterator(task_root)) {
      if (entry.is_regular_file() && entry.path().extension() == ".jsonl") {
        paths.push_back(entry.path());
      }
    }
    std::ranges::sort(paths);
  } else {
    throw common::Error(common::ErrorCode::filesystem_failure,
                        "benchmark task path is missing: " +
                            task_root.string());
  }
  std::set<std::string> seen;
  std::vector<BenchmarkTask> tasks;
  for (const auto &path : paths) {
    std::istringstream lines(read_text_file(path, false));
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(lines, line)) {
      ++line_number;
      if (trim(line).empty()) {
        continue;
      }
      try {
        const auto payload = contracts::parse_json(line);
        if (!payload.is_object()) {
          policy_error(path.string() + ":" + std::to_string(line_number) +
                       " benchmark task must be an object");
        }
        auto task = parse_task(payload);
        if (!seen.insert(task.problem_id).second) {
          policy_error("duplicate benchmark problem_id: " + task.problem_id);
        }
        tasks.push_back(std::move(task));
      } catch (const common::Error &error_value) {
        policy_error(path.string() + ":" + std::to_string(line_number) +
                     ": " + error_value.what());
      }
    }
  }
  if (tasks.empty()) {
    policy_error("benchmark task set is empty");
  }
  return tasks;
}

std::vector<BenchmarkTask>
select_benchmark_tasks(const std::vector<BenchmarkTask> &tasks,
                       std::size_t start, std::optional<std::size_t> count) {
  if (start >= tasks.size()) {
    policy_error("benchmark task start is outside the task set");
  }
  const auto end = count.has_value() ? start + *count : tasks.size();
  if (count.has_value() && *count < 1) {
    policy_error("benchmark task count must be positive");
  }
  if (end < start || end > tasks.size()) {
    policy_error("benchmark task slice exceeds the task set");
  }
  return {tasks.begin() + static_cast<std::ptrdiff_t>(start),
          tasks.begin() + static_cast<std::ptrdiff_t>(end)};
}

FixtureObservations
load_fixture_observations(const std::filesystem::path &path,
                          const std::vector<BenchmarkTask> &tasks) {
  const auto payload = contracts::parse_json(read_text_file(path, false));
  require_exact_keys(payload,
                     {"fixture_id", "observations", "qualification_status",
                      "schema_version"},
                     {}, "benchmark fixture");
  if (required_int(payload, "schema_version", "benchmark fixture") != 1) {
    policy_error("benchmark fixture schema_version must be 1");
  }
  if (required_string(payload, "qualification_status", "benchmark fixture") !=
      development_fixture_qualification_status) {
    policy_error(
        "fixture qualification status must remain development_fixture_only");
  }
  if (!payload.at("observations").is_array()) {
    policy_error("benchmark fixture observations must be an array");
  }
  std::set<std::string> task_ids;
  for (const auto &task_value : tasks) {
    task_ids.insert(canonicalize_benchmark_task(task_value).problem_id);
  }
  FixtureObservations by_system;
  for (const auto system_id : benchmark_system_ids) {
    by_system.emplace(std::string(system_id),
                      std::map<std::string, BenchmarkObservation>{});
  }
  for (const auto &raw_observation : payload.at("observations")) {
    auto observation = parse_observation(raw_observation);
    if (!task_ids.contains(observation.problem_id)) {
      policy_error("fixture references unknown benchmark problem: " +
                   observation.problem_id);
    }
    auto &observations = by_system.at(observation.system_id);
    if (!observations.emplace(observation.problem_id, observation).second) {
      policy_error("duplicate fixture observation: " + observation.system_id +
                   "/" + observation.problem_id);
    }
  }
  for (const auto system_id : benchmark_system_ids) {
    const auto &observations = by_system.at(std::string(system_id));
    for (const auto &task_id : task_ids) {
      if (!observations.contains(task_id)) {
        policy_error("fixture system " + std::string(system_id) +
                     " is missing tasks");
      }
    }
  }
  return by_system;
}

std::vector<SourceFileRecord>
build_source_manifest(const std::filesystem::path &root,
                      const std::vector<std::string> &relative_paths) {
  const std::set<std::string> paths(relative_paths.begin(),
                                    relative_paths.end());
  std::vector<SourceFileRecord> records;
  for (const auto &relative_path : paths) {
    const auto path = root / relative_path;
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
      policy_error("benchmark source file is missing: " + relative_path);
    }
    records.push_back(
        {.path = relative_path, .sha256 = contracts::sha256_file(path)});
  }
  return records;
}

BenchmarkRun run_benchmark(
    const std::vector<BenchmarkTask> &raw_tasks,
    const std::vector<BenchmarkExecutor *> &executors,
    std::string generated_on, std::string package_version,
    std::string git_head, bool worktree_dirty,
    std::vector<SourceFileRecord> source_files, std::string execution_mode,
    std::string qualification_status) {
  if (raw_tasks.empty()) {
    policy_error("cannot run an empty benchmark");
  }
  if (executors.size() != benchmark_system_ids.size()) {
    policy_error("benchmark executors must use canonical system order");
  }
  for (std::size_t index = 0; index < executors.size(); ++index) {
    if (executors[index] == nullptr ||
        executors[index]->system_id() != benchmark_system_ids[index]) {
      policy_error("benchmark executors must use canonical system order");
    }
  }
  std::vector<BenchmarkTask> tasks;
  tasks.reserve(raw_tasks.size());
  std::set<std::string> task_ids;
  for (const auto &task : raw_tasks) {
    auto canonical = canonicalize_benchmark_task(task);
    if (!task_ids.insert(canonical.problem_id).second) {
      policy_error("duplicate benchmark problem_id: " + canonical.problem_id);
    }
    tasks.push_back(std::move(canonical));
  }
  std::vector<BenchmarkResult> results;
  for (const auto &task : tasks) {
    for (auto *executor : executors) {
      results.push_back(score_observation(task, executor->execute(task)));
    }
    for (auto *executor : executors) {
      executor->release_runtime();
    }
  }
  std::vector<BenchmarkSystemSummary> summaries;
  for (const auto system_id : benchmark_system_ids) {
    summaries.push_back(summarize_results(system_id, results));
  }
  std::ranges::sort(source_files, {}, &SourceFileRecord::path);
  contracts::Json source_material = contracts::Json::array();
  for (const auto &record : source_files) {
    source_material.push_back(to_json(record));
  }
  const auto source_manifest_hash = contracts::sha256_json(source_material);
  std::vector<contracts::Json> descriptors;
  std::vector<contracts::Json> identity_descriptors;
  for (auto *executor : executors) {
    descriptors.push_back(executor->descriptor());
    identity_descriptors.push_back(executor->identity_descriptor());
  }
  std::vector<std::string> task_signatures;
  for (const auto &task : tasks) {
    task_signatures.push_back(task.signature);
  }
  const auto benchmark_id = contracts::sha256_json(
      {{"task_signatures", task_signatures},
       {"systems", identity_descriptors},
       {"source_manifest_hash", source_manifest_hash},
       {"execution_mode", execution_mode}});
  return canonicalize_benchmark_run(
      {.schema_version = 1,
       .benchmark_id = benchmark_id,
       .generated_on = std::move(generated_on),
       .execution_mode = std::move(execution_mode),
       .qualification_status = std::move(qualification_status),
       .performance_claim_allowed = false,
       .package_version = std::move(package_version),
       .git_head = std::move(git_head),
       .worktree_dirty = worktree_dirty,
       .source_manifest_hash = source_manifest_hash,
       .source_files = std::move(source_files),
       .task_count = static_cast<std::int64_t>(tasks.size()),
       .systems = std::move(descriptors),
       .results = std::move(results),
       .summaries = std::move(summaries),
       .signature = {}});
}

BenchmarkRun merge_benchmark_runs(const std::vector<BenchmarkRun> &raw_shards,
                                  const std::vector<BenchmarkTask> &raw_tasks) {
  if (raw_shards.empty()) {
    policy_error("benchmark merge requires at least one shard");
  }
  if (raw_tasks.empty()) {
    policy_error("benchmark merge requires the complete task set");
  }
  std::vector<BenchmarkTask> tasks;
  std::map<std::string, std::pair<std::size_t, BenchmarkTask>> expected_by_id;
  for (std::size_t index = 0; index < raw_tasks.size(); ++index) {
    auto task = canonicalize_benchmark_task(raw_tasks[index]);
    if (!expected_by_id.emplace(task.problem_id,
                                std::pair{index, task})
             .second) {
      policy_error("benchmark merge task identities are not unique");
    }
    tasks.push_back(std::move(task));
  }
  std::vector<BenchmarkRun> shards;
  shards.reserve(raw_shards.size());
  for (const auto &shard : raw_shards) {
    shards.push_back(canonicalize_benchmark_run(shard));
  }
  const auto &first = shards.front();
  struct OrderedShard final {
    std::size_t start = 0;
    BenchmarkRun run;
    std::vector<std::string> task_ids;
  };
  std::vector<OrderedShard> ordered_shards;
  for (const auto &shard : shards) {
    if (shard.generated_on != first.generated_on ||
        shard.execution_mode != first.execution_mode ||
        shard.qualification_status != first.qualification_status ||
        shard.performance_claim_allowed != first.performance_claim_allowed ||
        shard.package_version != first.package_version ||
        shard.git_head != first.git_head ||
        shard.worktree_dirty != first.worktree_dirty ||
        shard.source_manifest_hash != first.source_manifest_hash ||
        shard.source_files != first.source_files) {
      policy_error("benchmark shard metadata mismatch");
    }
    const auto task_ids = run_task_ids(shard);
    std::vector<std::size_t> indexes;
    for (const auto &problem_id : task_ids) {
      const auto found = expected_by_id.find(problem_id);
      if (found == expected_by_id.end()) {
        policy_error("benchmark shard references unknown task: " + problem_id);
      }
      indexes.push_back(found->second.first);
      std::set<std::string> actual_signatures;
      for (const auto &result : shard.results) {
        if (result.problem_id == problem_id) {
          actual_signatures.insert(result.task_signature);
        }
      }
      if (actual_signatures !=
          std::set<std::string>{found->second.second.signature}) {
        policy_error("benchmark shard task signature mismatch");
      }
    }
    for (std::size_t index = 0; index < indexes.size(); ++index) {
      if (indexes[index] != indexes.front() + index) {
        policy_error(
            "benchmark shard tasks must form one ordered contiguous slice");
      }
    }
    ordered_shards.push_back(
        {.start = indexes.front(), .run = shard, .task_ids = task_ids});
  }
  std::ranges::sort(ordered_shards, {}, &OrderedShard::start);
  std::vector<std::string> merged_task_ids;
  for (const auto &shard : ordered_shards) {
    merged_task_ids.insert(merged_task_ids.end(), shard.task_ids.begin(),
                           shard.task_ids.end());
  }
  std::vector<std::string> expected_task_ids;
  for (const auto &task : tasks) {
    expected_task_ids.push_back(task.problem_id);
  }
  if (merged_task_ids != expected_task_ids) {
    policy_error(
        "benchmark shards do not exactly cover the complete task set");
  }
  std::vector<contracts::Json> systems;
  for (std::size_t system_index = 0;
       system_index < benchmark_system_ids.size(); ++system_index) {
    std::vector<contracts::Json> descriptors;
    for (const auto &shard : ordered_shards) {
      const auto &descriptor = shard.run.systems.at(system_index);
      if (descriptor.value("system_id", std::string{}) !=
          benchmark_system_ids[system_index]) {
        policy_error("benchmark shard system descriptor order mismatch");
      }
      descriptors.push_back(descriptor);
    }
    if (first.execution_mode == "provider_bound") {
      systems.push_back(merge_provider_system_descriptors(descriptors));
    } else {
      for (const auto &descriptor : descriptors) {
        if (descriptor != descriptors.front()) {
          policy_error("fixture benchmark shard descriptor mismatch");
        }
      }
      systems.push_back(descriptors.front());
    }
  }
  std::vector<BenchmarkResult> results;
  for (const auto &shard : ordered_shards) {
    results.insert(results.end(), shard.run.results.begin(),
                   shard.run.results.end());
  }
  std::vector<BenchmarkSystemSummary> summaries;
  for (const auto system_id : benchmark_system_ids) {
    summaries.push_back(summarize_results(system_id, results));
  }
  std::vector<contracts::Json> identity_descriptors;
  for (const auto &descriptor : systems) {
    identity_descriptors.push_back(
        identity_descriptor_from_system(descriptor, first.execution_mode));
  }
  std::vector<std::string> task_signatures;
  for (const auto &task : tasks) {
    task_signatures.push_back(task.signature);
  }
  const auto benchmark_id = contracts::sha256_json(
      {{"task_signatures", task_signatures},
       {"systems", identity_descriptors},
       {"source_manifest_hash", first.source_manifest_hash},
       {"execution_mode", first.execution_mode}});
  return canonicalize_benchmark_run(
      {.schema_version = 1,
       .benchmark_id = benchmark_id,
       .generated_on = first.generated_on,
       .execution_mode = first.execution_mode,
       .qualification_status = first.qualification_status,
       .performance_claim_allowed = false,
       .package_version = first.package_version,
       .git_head = first.git_head,
       .worktree_dirty = first.worktree_dirty,
       .source_manifest_hash = first.source_manifest_hash,
       .source_files = first.source_files,
       .task_count = static_cast<std::int64_t>(tasks.size()),
       .systems = std::move(systems),
       .results = std::move(results),
       .summaries = std::move(summaries),
       .signature = {}});
}

BenchmarkRun load_benchmark_run(const std::filesystem::path &path) {
  try {
    const auto payload = contracts::parse_json(read_text_file(path, false));
    require_exact_keys(
        payload,
        {"benchmark_id", "execution_mode", "generated_on", "git_head",
         "package_version", "performance_claim_allowed", "qualification_status",
         "results", "schema_version", "signature", "source_files",
         "source_manifest_hash", "summaries", "systems", "task_count",
         "worktree_dirty"},
        {}, "benchmark run");
    if (!payload.at("source_files").is_array() ||
        !payload.at("systems").is_array() ||
        !payload.at("results").is_array() ||
        !payload.at("summaries").is_array()) {
      policy_error("benchmark run collection fields must be arrays");
    }
    BenchmarkRun run{
        .schema_version = required_int(payload, "schema_version", "benchmark run"),
        .benchmark_id = required_string(payload, "benchmark_id", "benchmark run"),
        .generated_on = required_string(payload, "generated_on", "benchmark run"),
        .execution_mode =
            required_string(payload, "execution_mode", "benchmark run"),
        .qualification_status =
            required_string(payload, "qualification_status", "benchmark run"),
        .performance_claim_allowed = required_boolean(
            payload, "performance_claim_allowed", "benchmark run"),
        .package_version =
            required_string(payload, "package_version", "benchmark run"),
        .git_head = required_string(payload, "git_head", "benchmark run"),
        .worktree_dirty =
            required_boolean(payload, "worktree_dirty", "benchmark run"),
        .source_manifest_hash =
            required_string(payload, "source_manifest_hash", "benchmark run"),
        .source_files = {},
        .task_count = required_integer(payload, "task_count", "benchmark run"),
        .systems = {},
        .results = {},
        .summaries = {},
        .signature = required_string(payload, "signature", "benchmark run")};
    for (const auto &item : payload.at("source_files")) {
      run.source_files.push_back(parse_source_file(item));
    }
    for (const auto &descriptor : payload.at("systems")) {
      run.systems.push_back(descriptor);
    }
    for (const auto &item : payload.at("results")) {
      run.results.push_back(parse_result(item));
    }
    for (const auto &item : payload.at("summaries")) {
      run.summaries.push_back(parse_summary(item));
    }
    return canonicalize_benchmark_run(std::move(run));
  } catch (const common::Error &) {
    throw;
  } catch (const nlohmann::json::exception &error) {
    policy_error(std::string("benchmark run is invalid: ") + error.what());
  } catch (const std::exception &error) {
    policy_error(std::string("benchmark run is invalid: ") + error.what());
  }
}

std::string verify_benchmark_checksum(const std::filesystem::path &path,
                                      const std::filesystem::path &checksum_path) {
  std::istringstream checksum_stream(read_text_file(checksum_path, false));
  std::string expected;
  checksum_stream >> expected;
  if (expected.size() != 64) {
    policy_error("benchmark checksum file is invalid");
  }
  const auto actual = contracts::sha256_file(path);
  if (actual != expected) {
    policy_error("benchmark artifact checksum mismatch");
  }
  return actual;
}

std::string benchmark_json(const BenchmarkRun &run) {
  const auto checked = canonicalize_benchmark_run(run);
  return to_json(checked).dump(2, ' ', false,
                               contracts::Json::error_handler_t::strict) +
         "\n";
}

void write_benchmark_run(const std::filesystem::path &path,
                         const BenchmarkRun &run) {
  std::error_code error;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
      throw common::Error(common::ErrorCode::filesystem_failure,
                          "cannot create benchmark directory: " +
                              path.parent_path().string());
    }
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    throw common::Error(common::ErrorCode::filesystem_failure,
                        "cannot write benchmark file: " + path.string());
  }
  output << benchmark_json(run);
  if (!output.good()) {
    throw common::Error(common::ErrorCode::filesystem_failure,
                        "cannot write benchmark file: " + path.string());
  }
}

} // namespace statewright::reasoning
