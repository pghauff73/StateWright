#pragma once

#include "statewright/core/workspace.hpp"
#include "statewright/egcf/execution.hpp"
#include "statewright/egcf/store.hpp"

#include <string>
#include <string_view>

namespace statewright::egcf {

class ApprovalManager final {
public:
  ApprovalManager(EgcfStore &store, const core::Workspace &workspace);

  [[nodiscard]] std::string authorize(
      std::string_view plan_id, std::string approver, std::string authority,
      contracts::Json constraints = contracts::Json::object(),
      std::string expires_at = {}, bool human = true, int use_limit = 1);
  [[nodiscard]] ApprovalRecord validate(const ExecutionPlan &plan,
                                        std::string_view approval_id) const;

private:
  EgcfStore &store_;
  const core::Workspace &workspace_;
};

} // namespace statewright::egcf
