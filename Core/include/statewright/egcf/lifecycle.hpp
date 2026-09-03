#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace statewright::egcf {

[[nodiscard]] const std::vector<std::string> &canonical_lifecycle_stages();
[[nodiscard]] bool is_terminal_lifecycle_state(std::string_view state);

class Lifecycle final {
public:
  explicit Lifecycle(std::string initial = "DISCOVERED");

  [[nodiscard]] const std::string &state() const noexcept;
  [[nodiscard]] const std::vector<std::string> &history() const noexcept;
  [[nodiscard]] std::string transition(std::string target);
  [[nodiscard]] std::vector<std::string>
  compress(const std::vector<std::string> &stages);
  [[nodiscard]] contracts::Json projection() const;

private:
  std::string state_;
  std::vector<std::string> history_;
};

} // namespace statewright::egcf
