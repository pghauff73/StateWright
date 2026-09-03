#pragma once

#include "statewright/contracts/canonical_json.hpp"
#include "statewright/egcf/context.hpp"

#include <string>
#include <vector>

namespace statewright::egcf {

struct WorkflowNode final {
  std::string node_id;
  std::string command_id;
  contracts::Json inputs = contracts::Json::object();
  std::vector<std::string> depends_on;
  contracts::Json when = contracts::Json::object();
  int retry_limit = 0;
  bool checkpoint = false;
};

struct WorkflowDefinition final {
  std::string name;
  int version = 1;
  contracts::Json parameters = contracts::Json::object();
  std::vector<WorkflowNode> nodes;
  contracts::Json outputs = contracts::Json::object();
  std::string description;

  [[nodiscard]] std::string workflow_id() const;
  [[nodiscard]] std::string object_id() const;
};

struct CompiledWorkflow final {
  std::string workflow_id;
  std::string source_snapshot_hash;
  CommandContext command_context;
  std::vector<contracts::Json> nodes;
  std::vector<contracts::Json> edges;
  std::vector<std::string> execution_order;
  std::string capability_level;
  std::vector<std::string> capability_requirements;
  std::string risk;
  std::vector<std::string> evidence_requirements;
  std::string approval_policy;
  Budget budget;
  contracts::Json rollback_graph = contracts::Json::object();
  std::vector<std::string> unresolved;
  std::string created_at;
  std::string graph_hash;

  [[nodiscard]] std::string object_id() const;
};

[[nodiscard]] contracts::Json to_json(const WorkflowNode &node);
[[nodiscard]] contracts::Json to_json(const WorkflowDefinition &workflow);
[[nodiscard]] contracts::Json to_json(const CompiledWorkflow &workflow);
[[nodiscard]] WorkflowNode workflow_node_from_json(const contracts::Json &value);
[[nodiscard]] WorkflowDefinition
workflow_definition_from_json(const contracts::Json &value);
[[nodiscard]] CompiledWorkflow
compiled_workflow_from_json(const contracts::Json &value);

} // namespace statewright::egcf
