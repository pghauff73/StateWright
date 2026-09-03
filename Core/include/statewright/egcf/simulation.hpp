#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <filesystem>

namespace statewright::egcf {

class SimulationEngine final {
public:
  [[nodiscard]] contracts::Json
  migration(const contracts::Json &before,
            const contracts::Json &operations) const;
  [[nodiscard]] contracts::Json
  worktree(const std::filesystem::path &root,
           const contracts::Json &changes) const;
  [[nodiscard]] contracts::Json
  rollback(const contracts::Json &simulation) const;
};

} // namespace statewright::egcf
