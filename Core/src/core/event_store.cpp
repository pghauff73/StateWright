#include "statewright/core/event_store.hpp"

#include "statewright/common/error.hpp"
#include "statewright/common/utf8.hpp"
#include "statewright/contracts/hash.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <openssl/rand.h>
#include <regex>
#include <sstream>
#include <string>
#include <unistd.h>

extern char **environ;

namespace statewright::core {
namespace {

const std::regex sensitive_key(
    R"((api[_-]?key|token|secret|password|authorization))",
    std::regex_constants::icase);
const std::regex sensitive_assignment(
    R"(([A-Za-z0-9_-]*(api[_-]?key|access[_-]?token|refresh[_-]?token|secret|password|authorization))(\s*[:=]\s*)([^\s,;]+))",
    std::regex_constants::icase);
const std::regex bearer_token(R"(\bBearer\s+[A-Za-z0-9._~+/=-]+)",
                              std::regex_constants::icase);

[[nodiscard]] bool non_secret_token_key(std::string_view key) {
  static constexpr std::array<std::string_view, 8> names = {
      "input_tokens", "max_tokens",   "output_tokens", "token_budget",
      "token_count",  "tokens",       "total_tokens",  "tokens_after",
  };
  if (std::find(names.begin(), names.end(), key) != names.end()) {
    return true;
  }
  return key == "tokens_before";
}

[[nodiscard]] std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

[[nodiscard]] std::string redact_string(std::string value) {
  value = std::regex_replace(value, sensitive_assignment, "$1$3<redacted>");
  value = std::regex_replace(value, bearer_token, "Bearer <redacted>");

  const auto apply_configured = [&](const char *environment_name) {
    const char *configured = std::getenv(environment_name);
    if (configured == nullptr) {
      return;
    }
    std::stringstream patterns(configured);
    std::string pattern;
    while (std::getline(patterns, pattern, ',')) {
      if (pattern.empty()) {
        continue;
      }
      try {
        value = std::regex_replace(value, std::regex(pattern), "<redacted>");
      } catch (const std::regex_error &) {
        std::size_t position = 0;
        while ((position = value.find(pattern, position)) != std::string::npos) {
          value.replace(position, pattern.size(), "<redacted>");
          position += std::string_view("<redacted>").size();
        }
      }
    }
  };
  apply_configured("STATEWRIGHT_SECRET_PATTERNS");
  apply_configured("OURD_SECRET_PATTERNS");

  for (char **entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
    const std::string environment_entry(*entry);
    const auto separator = environment_entry.find('=');
    if (separator == std::string::npos) {
      continue;
    }
    const std::string name = environment_entry.substr(0, separator);
    const std::string secret = environment_entry.substr(separator + 1);
    if (secret.size() < 6U || !std::regex_search(name, sensitive_key)) {
      continue;
    }
    std::size_t position = 0;
    while ((position = value.find(secret, position)) != std::string::npos) {
      value.replace(position, secret.size(), "<redacted>");
      position += std::string_view("<redacted>").size();
    }
  }
  return value;
}

[[nodiscard]] std::string uuid_v4() {
  std::array<unsigned char, 16> bytes{};
  if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
    throw common::Error(common::ErrorCode::cryptographic_failure,
                        "cannot generate event UUID");
  }
  bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0FU) | 0x40U);
  bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3FU) | 0x80U);

  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    if (index == 4U || index == 6U || index == 8U || index == 10U) {
      output << '-';
    }
    output << std::setw(2) << static_cast<unsigned int>(bytes[index]);
  }
  return output.str();
}

[[nodiscard]] std::string utc_now() {
  const auto now = std::chrono::system_clock::now();
  const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
  const auto microseconds =
      std::chrono::duration_cast<std::chrono::microseconds>(now - seconds);
  const std::time_t time = std::chrono::system_clock::to_time_t(seconds);
  std::tm utc{};
  gmtime_r(&time, &utc);
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(6)
         << std::setfill('0') << microseconds.count() << 'Z';
  return output.str();
}

void append_durable(const std::filesystem::path &path, std::string_view text) {
  const int descriptor =
      ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
  if (descriptor < 0) {
    throw common::Error(common::ErrorCode::filesystem_failure,
                        "cannot open event ledger: " +
                            std::string(std::strerror(errno)));
  }
  std::size_t written = 0;
  while (written < text.size()) {
    const ssize_t count =
        ::write(descriptor, text.data() + written, text.size() - written);
    if (count < 0) {
      const std::string message = std::strerror(errno);
      ::close(descriptor);
      throw common::Error(common::ErrorCode::filesystem_failure,
                          "cannot append event ledger: " + message);
    }
    written += static_cast<std::size_t>(count);
  }
  if (::fsync(descriptor) != 0) {
    const std::string message = std::strerror(errno);
    ::close(descriptor);
    throw common::Error(common::ErrorCode::filesystem_failure,
                        "cannot synchronize event ledger: " + message);
  }
  if (::close(descriptor) != 0) {
    throw common::Error(common::ErrorCode::filesystem_failure,
                        "cannot close event ledger");
  }
}

} // namespace

contracts::Json redact(const contracts::Json &payload) {
  if (payload.is_object()) {
    contracts::Json result = contracts::Json::object();
    for (const auto &[key, value] : payload.items()) {
      const std::string folded = lowercase(key);
      const bool numeric_tokens =
          value.is_number() && !value.is_boolean() && folded.ends_with("_tokens");
      if (std::regex_search(key, sensitive_key) &&
          !non_secret_token_key(folded) && !numeric_tokens) {
        result[key] = "<redacted>";
      } else {
        result[key] = redact(value);
      }
    }
    return result;
  }
  if (payload.is_array()) {
    contracts::Json result = contracts::Json::array();
    for (const auto &value : payload) {
      result.push_back(redact(value));
    }
    return result;
  }
  if (payload.is_string()) {
    return redact_string(payload.get<std::string>());
  }
  return payload;
}

EventStore::EventStore(std::filesystem::path path) : path_(std::move(path)) {
  std::filesystem::create_directories(path_.parent_path());
  head_ = validate_chain();
}

const std::filesystem::path &EventStore::path() const noexcept { return path_; }

const std::string &EventStore::head() const noexcept { return head_; }

std::vector<contracts::Json> EventStore::events() const {
  if (!std::filesystem::exists(path_)) {
    return {};
  }
  std::ifstream input(path_);
  if (!input.is_open()) {
    throw common::Error(common::ErrorCode::filesystem_failure,
                        "cannot read event ledger");
  }
  std::vector<contracts::Json> result;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.find_first_not_of(" \t\r\n") == std::string::npos) {
      continue;
    }
    try {
      result.push_back(contracts::parse_json(line));
    } catch (const common::Error &error) {
      throw common::Error(
          common::ErrorCode::json_parse,
          "invalid event JSON at line " + std::to_string(line_number) + ": " +
              error.what());
    }
  }
  return result;
}

std::string EventStore::validate_chain() const {
  std::string previous;
  for (const auto &event : events()) {
    if (!event.is_object()) {
      throw common::Error(common::ErrorCode::json_contract,
                          "event ledger entry must be an object");
    }
    const std::string payload_hash = contracts::sha256_json(event.at("payload"));
    if (event.value("payload_hash", std::string()) != payload_hash) {
      throw common::Error(common::ErrorCode::json_contract,
                          "event payload hash mismatch");
    }
    contracts::Json material = {
        {"event_id", event.value("event_id", contracts::Json())},
        {"event_type", event.value("event_type", contracts::Json())},
        {"payload_hash", payload_hash},
        {"previous_hash", event.value("previous_hash", contracts::Json())},
        {"timestamp", event.value("timestamp", contracts::Json())},
    };
    if (event.contains("run_id") || event.contains("action_id") ||
        event.contains("transaction_id")) {
      material["run_id"] = event.value("run_id", "");
      material["action_id"] = event.value("action_id", "");
      material["transaction_id"] = event.value("transaction_id", "");
    }
    const std::string expected_hash = contracts::sha256_json(material);
    if (event.value("previous_hash", std::string()) != previous) {
      throw common::Error(common::ErrorCode::json_contract,
                          "event previous hash mismatch");
    }
    if (event.value("event_hash", std::string()) != expected_hash) {
      throw common::Error(common::ErrorCode::json_contract,
                          "event hash mismatch");
    }
    previous = expected_hash;
  }
  return previous;
}

contracts::Json EventStore::append(
    std::string_view event_type, const contracts::Json &payload,
    const EventLineage &lineage, const std::optional<EventStamp> &fixed_stamp) {
  if (event_type.empty()) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "event type must be non-empty");
  }
  common::require_valid_utf8(event_type);
  const contracts::Json safe_payload = redact(payload);
  const std::string event_id = fixed_stamp ? fixed_stamp->event_id : uuid_v4();
  const std::string timestamp = fixed_stamp ? fixed_stamp->timestamp : utc_now();
  const std::string payload_hash = contracts::sha256_json(safe_payload);
  const contracts::Json material = {
      {"action_id", lineage.action_id},
      {"event_id", event_id},
      {"event_type", event_type},
      {"payload_hash", payload_hash},
      {"previous_hash", head_},
      {"run_id", lineage.run_id},
      {"timestamp", timestamp},
      {"transaction_id", lineage.transaction_id},
  };
  const std::string event_hash = contracts::sha256_json(material);
  contracts::Json event = material;
  event["event_hash"] = event_hash;
  event["payload"] = safe_payload;
  append_durable(path_, contracts::canonical_json(event) + "\n");
  head_ = event_hash;
  return event;
}

} // namespace statewright::core

