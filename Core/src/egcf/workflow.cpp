#include "statewright/egcf/workflow.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/typed_id.hpp"

#include <set>
#include <string>
#include <utility>

namespace statewright::egcf {
namespace {

[[noreturn]] void workflow_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument,
                      "EGCF workflow: " + std::move(message));
}

void reject_unknown(const contracts::Json &value,
                    const std::set<std::string> &fields,
                    std::string_view label) {
  for (const auto &[key, unused] : value.items()) {
    static_cast<void>(unused);
    if (!fields.contains(key)) {
      workflow_error(std::string(label) + " has unknown field: " + key);
    }
  }
}

} // namespace

std::string WorkflowDefinition::workflow_id() const {
  return name + "@" + std::to_string(version);
}

std::string WorkflowDefinition::object_id() const {
  return contracts::typed_id("workflow-definition", to_json(*this));
}

std::string CompiledWorkflow::object_id() const {
  return contracts::typed_id("compiled-workflow", to_json(*this));
}

contracts::Json to_json(const WorkflowNode &node) {
  return {{"checkpoint", node.checkpoint},
          {"command_id", node.command_id},
          {"depends_on", node.depends_on},
          {"inputs", node.inputs},
          {"node_id", node.node_id},
          {"retry_limit", node.retry_limit},
          {"when", node.when}};
}

contracts::Json to_json(const WorkflowDefinition &workflow) {
  contracts::Json nodes = contracts::Json::array();
  for (const auto &node : workflow.nodes) {
    nodes.push_back(to_json(node));
  }
  return {{"description", workflow.description},
          {"name", workflow.name},
          {"nodes", std::move(nodes)},
          {"outputs", workflow.outputs},
          {"parameters", workflow.parameters},
          {"version", workflow.version}};
}

contracts::Json to_json(const CompiledWorkflow &workflow) {
  return {{"approval_policy", workflow.approval_policy},
          {"budget", to_json(workflow.budget)},
          {"capability_level", workflow.capability_level},
          {"capability_requirements", workflow.capability_requirements},
          {"command_context", to_json(workflow.command_context)},
          {"created_at", workflow.created_at},
          {"edges", workflow.edges},
          {"evidence_requirements", workflow.evidence_requirements},
          {"execution_order", workflow.execution_order},
          {"graph_hash", workflow.graph_hash},
          {"nodes", workflow.nodes},
          {"risk", workflow.risk},
          {"rollback_graph", workflow.rollback_graph},
          {"source_snapshot_hash", workflow.source_snapshot_hash},
          {"unresolved", workflow.unresolved},
          {"workflow_id", workflow.workflow_id}};
}

WorkflowNode workflow_node_from_json(const contracts::Json &value) {
  if (!value.is_object()) {
    workflow_error("node must be an object");
  }
  reject_unknown(value,
                 {"node_id", "command_id", "inputs", "depends_on", "when",
                  "retry_limit", "checkpoint"},
                 "node");
  if (!value.contains("node_id") || !value.contains("command_id") ||
      !value.contains("inputs")) {
    workflow_error("node requires node_id, command_id, and inputs");
  }
  WorkflowNode result = {
      .node_id = value.at("node_id").get<std::string>(),
      .command_id = value.at("command_id").get<std::string>(),
      .inputs = value.at("inputs"),
      .depends_on =
          value.value("depends_on", std::vector<std::string>{}),
      .when = value.value("when", contracts::Json::object()),
      .retry_limit = value.value("retry_limit", 0),
      .checkpoint = value.value("checkpoint", false)};
  if (result.node_id.empty() || result.command_id.empty() ||
      !result.inputs.is_object() || !result.when.is_object() ||
      result.retry_limit < 0) {
    workflow_error("node fields are invalid");
  }
  return result;
}

WorkflowDefinition workflow_definition_from_json(const contracts::Json &value) {
  if (!value.is_object()) {
    workflow_error("definition must be an object");
  }
  reject_unknown(value,
                 {"schema_version", "name", "version", "parameters", "nodes",
                  "outputs", "description"},
                 "definition");
  if (value.value("schema_version", 1) != 1 || !value.contains("name") ||
      !value.contains("nodes")) {
    workflow_error("definition requires schema version 1, name, and nodes");
  }
  if (!value.at("nodes").is_array() || value.at("nodes").empty()) {
    workflow_error("definition nodes must be a non-empty array");
  }
  WorkflowDefinition result;
  result.name = value.at("name").get<std::string>();
  result.version = value.value("version", 1);
  result.parameters = value.value("parameters", contracts::Json::object());
  result.outputs = value.value("outputs", contracts::Json::object());
  result.description = value.value("description", "");
  for (const auto &node : value.at("nodes")) {
    result.nodes.push_back(workflow_node_from_json(node));
  }
  if (result.name.empty() || result.version < 1 ||
      !result.parameters.is_object() || !result.outputs.is_object()) {
    workflow_error("definition fields are invalid");
  }
  return result;
}

CompiledWorkflow compiled_workflow_from_json(const contracts::Json &value) {
  if (!value.is_object()) {
    workflow_error("compiled workflow must be an object");
  }
  reject_unknown(value,
                 {"approval_policy", "budget", "capability_level",
                  "capability_requirements", "command_context", "created_at",
                  "edges", "evidence_requirements", "execution_order",
                  "graph_hash", "nodes", "risk", "rollback_graph",
                  "source_snapshot_hash", "unresolved", "workflow_id"},
                 "compiled workflow");
  try {
    CompiledWorkflow result = {
        .workflow_id = value.at("workflow_id"),
        .source_snapshot_hash = value.at("source_snapshot_hash"),
        .command_context =
            command_context_from_json(value.at("command_context")),
        .nodes = value.at("nodes"),
        .edges = value.at("edges"),
        .execution_order = value.at("execution_order"),
        .capability_level = value.at("capability_level"),
        .capability_requirements = value.at("capability_requirements"),
        .risk = value.at("risk"),
        .evidence_requirements = value.at("evidence_requirements"),
        .approval_policy = value.at("approval_policy"),
        .budget = budget_from_json(value.at("budget")),
        .rollback_graph = value.at("rollback_graph"),
        .unresolved = value.at("unresolved"),
        .created_at = value.at("created_at"),
        .graph_hash = value.at("graph_hash")};
    if (result.workflow_id.empty() || result.source_snapshot_hash.empty() ||
        !value.at("nodes").is_array() || result.nodes.empty() ||
        !value.at("edges").is_array() || result.execution_order.empty() ||
        !result.rollback_graph.is_object()) {
      workflow_error("compiled workflow fields are invalid");
    }
    return result;
  } catch (const common::Error &) {
    throw;
  } catch (const std::exception &exception) {
    workflow_error(std::string("invalid compiled workflow field: ") +
                   exception.what());
  }
}

} // namespace statewright::egcf
