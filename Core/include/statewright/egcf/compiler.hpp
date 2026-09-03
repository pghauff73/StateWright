#pragma once

#include "statewright/core/authority.hpp"
#include "statewright/core/workspace.hpp"
#include "statewright/egcf/registry.hpp"
#include "statewright/egcf/store.hpp"
#include "statewright/egcf/workflow.hpp"

#include <string>
#include <utility>
#include <vector>

namespace statewright::egcf {

class WorkflowCompiler final {
public:
  WorkflowCompiler(EgcfStore &store, const core::Workspace &workspace,
                   const CommandRegistry &commands,
                   const AlgorithmRegistry &algorithms,
                   const core::AuthorityManifest &authority);

  [[nodiscard]] CompiledWorkflow
  compile(const WorkflowDefinition &workflow,
          const CommandContext &context = CommandContext{});

private:
  [[nodiscard]] contracts::Json
  compile_node(const WorkflowNode &node, const CommandContext &context);
  [[nodiscard]] static std::pair<std::vector<std::string>,
                                 std::vector<contracts::Json>>
  topological_order(const std::vector<WorkflowNode> &nodes);
  static void validate_references(const WorkflowDefinition &workflow,
                                  const std::vector<contracts::Json> &edges);
  [[nodiscard]] static bool
  reachable(std::string_view source, std::string_view target,
            const std::vector<contracts::Json> &edges);
  [[nodiscard]] static std::vector<std::string>
  target_conflicts(const std::vector<contracts::Json> &nodes,
                   const std::vector<contracts::Json> &edges);

  EgcfStore &store_;
  const core::Workspace &workspace_;
  const CommandRegistry &commands_;
  const AlgorithmRegistry &algorithms_;
  const core::AuthorityManifest &authority_;
  SelectionEngine selector_;
};

} // namespace statewright::egcf
