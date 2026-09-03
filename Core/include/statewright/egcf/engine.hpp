#pragma once

#include "statewright/contracts/canonical_json.hpp"
#include "statewright/core/authority.hpp"
#include "statewright/core/event_store.hpp"
#include "statewright/core/transaction.hpp"
#include "statewright/core/workspace.hpp"
#include "statewright/egcf/approval.hpp"
#include "statewright/egcf/compiler.hpp"
#include "statewright/egcf/evidence.hpp"
#include "statewright/egcf/governance.hpp"
#include "statewright/egcf/registry.hpp"
#include "statewright/egcf/reasoning_service.hpp"
#include "statewright/egcf/assurance.hpp"
#include "statewright/egcf/simulation.hpp"
#include "statewright/egcf/store.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace statewright::egcf {

class EgcfEngine final {
public:
  EgcfEngine(std::filesystem::path root, std::filesystem::path resource_root,
             core::AuthorityManifest authority, std::string actor = "user",
             bool recovery = false);

  [[nodiscard]] const core::Workspace &workspace() const noexcept;
  [[nodiscard]] const core::AuthorityManifest &authority() const noexcept;
  [[nodiscard]] EgcfStore &store() noexcept;
  [[nodiscard]] const CommandRegistry &commands() const noexcept;
  [[nodiscard]] const AlgorithmRegistry &algorithms() const noexcept;
  [[nodiscard]] const std::string &capability_grant_id() const noexcept;
  [[nodiscard]] EvidenceManager &evidence() noexcept;
  [[nodiscard]] Ieps &ieps() noexcept;
  [[nodiscard]] InvariantManager &invariants() noexcept;
  [[nodiscard]] DecisionManager &decisions() noexcept;
  [[nodiscard]] OiecSrProposalService &reasoning() noexcept;

  [[nodiscard]] CompiledWorkflow
  compile(const WorkflowDefinition &workflow,
          const CommandContext &context = CommandContext{});
  [[nodiscard]] ExecutionPlan
  create_execution_plan(const CompiledWorkflow &compiled,
                        bool prepare_mutations);
  [[nodiscard]] std::string
  authorize(std::string_view plan_id, std::string approver,
            std::string authority_statement,
            contracts::Json constraints = contracts::Json::object(),
            std::string expires_at = {}, int use_limit = 1);
  [[nodiscard]] contracts::Json
  execute_plan(std::string_view plan_id, std::string_view approval_id = {},
               bool pause_at_checkpoint = false, bool resume = false);
  [[nodiscard]] contracts::Json verify_plan(std::string_view plan_id);
  [[nodiscard]] contracts::Json rollback_plan(std::string_view plan_id);
  [[nodiscard]] contracts::Json
  invoke(std::string_view command_id,
         contracts::Json inputs = contracts::Json::object(),
         const CommandContext &context = CommandContext{});
  [[nodiscard]] contracts::Json replay(std::string_view plan_id,
                                       const CommandContext &context);

private:
  void bootstrap_algorithms();
  [[nodiscard]] std::string register_capability_grant();
  [[nodiscard]] ExecutionPlan load_plan(std::string_view plan_id) const;
  [[nodiscard]] CompiledWorkflow
  load_compiled(const ExecutionPlan &plan) const;
  void validate_plan(const ExecutionPlan &plan,
                     const CompiledWorkflow &compiled,
                     bool require_current_source) const;
  [[nodiscard]] std::vector<std::string>
  register_evidence(const ExecutionPlan &plan, const contracts::Json &node,
                    const contracts::Json &verification,
                    bool simulated);

  std::filesystem::path resource_root_;
  core::Workspace workspace_;
  core::AuthorityManifest authority_;
  EgcfStore store_;
  CommandRegistry commands_;
  AlgorithmRegistry algorithms_;
  WorkflowCompiler compiler_;
  ApprovalManager approvals_;
  EvidenceManager evidence_;
  Ieps ieps_;
  InvariantManager invariants_;
  DecisionManager decisions_;
  OiecSrProposalService reasoning_;
  AssuranceManager assurance_;
  SimulationEngine simulation_;
  core::EventStore transaction_events_;
  core::TransactionManager transactions_;
  std::string actor_;
  std::string grant_id_;
};

} // namespace statewright::egcf
