#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::core {

struct EventLineage final {
  std::string run_id;
  std::string action_id;
  std::string transaction_id;
};

struct EventStamp final {
  std::string event_id;
  std::string timestamp;
};

[[nodiscard]] contracts::Json redact(const contracts::Json &payload);

class EventStore final {
public:
  explicit EventStore(std::filesystem::path path);

  [[nodiscard]] const std::filesystem::path &path() const noexcept;
  [[nodiscard]] const std::string &head() const noexcept;
  [[nodiscard]] std::vector<contracts::Json> events() const;
  [[nodiscard]] std::string validate_chain() const;

  [[nodiscard]] contracts::Json append(
      std::string_view event_type, const contracts::Json &payload,
      const EventLineage &lineage = {},
      const std::optional<EventStamp> &fixed_stamp = std::nullopt);

private:
  std::filesystem::path path_;
  std::string head_;
};

} // namespace statewright::core

