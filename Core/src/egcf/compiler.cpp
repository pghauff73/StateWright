#include "statewright/egcf/compiler.hpp"

#include "ledger_support.hpp"
#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <ranges>
#include <set>
#include <string>
#include <tuple>
#include <utility>

namespace statewright::egcf {
namespace {

using Json = contracts::Json;

[[noreturn]] void compilation_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied,
                      "EGCF compilation: " + std::move(message));
}

[[nodiscard]] int ordered_rank(std::string_view value,
                               const std::map<std::string, int> &order,
                               std::string_view label) {
  const auto found = order.find(std::string(value));
  if (found == order.end()) {
    compilation_error("invalid " + std::string(label) + ": " +
                      std::string(value));
  }
  return found->second;
}

[[nodiscard]] std::string maximum_by_order(
    const std::vector<std::string> &values,
    const std::map<std::string, int> &order, std::string_view fallback,
    std::string_view label) {
  std::string result(fallback);
  int rank = ordered_rank(result, order, label);
  for (const auto &value : values) {
    const int candidate = ordered_rank(value, order, label);
    if (candidate > rank) {
      rank = candidate;
      result = value;
    }
  }
  return result;
}

[[nodiscard]] std::vector<std::string>
canonical_strings(std::vector<std::string> values) {
  std::ranges::sort(values);
  values.erase(std::unique(values.begin(), values.end()), values.end());
  return values;
}

[[nodiscard]] bool broad_scope(const std::vector<std::string> &scope) {
  return scope.size() == 1U &&
         (scope.front() == "**" || scope.front() == "*" ||
          scope.front() == ".");
}

[[nodiscard]] std::vector<std::string> scope_value(
    const Json &value, const std::vector<std::string> &fallback) {
  if (value.is_null()) {
    return fallback;
  }
  if (value.is_string()) {
    return {value.get<std::string>()};
  }
  if (value.is_array()) {
    return value.get<std::vector<std::string>>();
  }
  compilation_error("node scope must be a string or string array");
}

[[nodiscard]] bool type_matches(const Json &value, std::string_view type) {
  if (type == "object") {
    return value.is_object();
  }
  if (type == "array") {
    return value.is_array();
  }
  if (type == "string") {
    return value.is_string();
  }
  if (type == "integer") {
    return value.is_number_integer() || value.is_number_unsigned();
  }
  if (type == "number") {
    return value.is_number();
  }
  if (type == "boolean") {
    return value.is_boolean();
  }
  if (type == "null") {
    return value.is_null();
  }
  return false;
}

void validate_value(const Json &schema, const Json &value,
                    std::string_view field) {
  if (value.is_object() && value.contains("$from")) {
    return;
  }
  if (!schema.contains("type")) {
    return;
  }
  const Json &types = schema.at("type");
  bool matched = false;
  if (types.is_string()) {
    matched = type_matches(value, types.get_ref<const std::string &>());
  } else if (types.is_array()) {
    for (const auto &type : types) {
      if (type.is_string() &&
          type_matches(value, type.get_ref<const std::string &>())) {
        matched = true;
        break;
      }
    }
  }
  if (!matched) {
    compilation_error("input field has invalid type: " + std::string(field));
  }
}

void validate_inputs(const CommandDefinition &definition, const Json &inputs) {
  if (!inputs.is_object()) {
    compilation_error("command inputs must be an object");
  }
  const Json &properties = definition.input_schema.at("properties");
  for (const auto &required : definition.input_schema.at("required")) {
    const std::string field = required.get<std::string>();
    if (!inputs.contains(field)) {
      compilation_error("required command input is missing: " + field);
    }
  }
  for (const auto &[field, value] : inputs.items()) {
    const auto schema = properties.find(field);
    if (schema == properties.end()) {
      compilation_error("unknown command input: " + field);
    }
    validate_value(*schema, value, field);
  }
}

void collect_references(const Json &value, std::vector<Json> &references) {
  if (value.is_object()) {
    if (value.contains("$from")) {
      references.push_back(value);
    }
    for (const auto &[key, child] : value.items()) {
      static_cast<void>(key);
      collect_references(child, references);
    }
  } else if (value.is_array()) {
    for (const auto &child : value) {
      collect_references(child, references);
    }
  }
}

[[nodiscard]] std::set<std::string> mutation_targets(const Json &node) {
  std::set<std::string> result;
  const Json &inputs = node.at("inputs");
  if (inputs.contains("targets") && inputs.at("targets").is_array()) {
    for (const auto &target : inputs.at("targets")) {
      if (target.is_string()) {
        result.insert(target.get<std::string>());
      }
    }
  }
  if (inputs.contains("changes") && inputs.at("changes").is_array()) {
    for (const auto &change : inputs.at("changes")) {
      if (change.is_object() && change.contains("path") &&
          change.at("path").is_string() &&
          !change.at("path").get_ref<const std::string &>().empty()) {
        result.insert(change.at("path").get<std::string>());
      }
    }
  }
  return result;
}

} // namespace

WorkflowCompiler::WorkflowCompiler(
    EgcfStore &store, const core::Workspace &workspace,
    const CommandRegistry &commands, const AlgorithmRegistry &algorithms,
    const core::AuthorityManifest &authority)
    : store_(store), workspace_(workspace), commands_(commands),
      algorithms_(algorithms), authority_(authority), selector_(algorithms) {}

Json WorkflowCompiler::compile_node(const WorkflowNode &node,
                                    const CommandContext &context) {
  static const std::map<std::string, int> capability_order = {
      {"C0", 0}, {"C1", 1}, {"C2", 2}, {"C3", 3}, {"C4", 4}, {"C5", 5}};
  static const std::map<std::string, int> risk_order = {
      {"L0", 0}, {"L1", 1}, {"L2", 2}};
  static const std::map<std::string, int> rollback_order = {
      {"none", 0}, {"best_effort", 1}, {"compensating", 2}, {"exact", 3}};
  static const std::map<std::string, int> approval_order = {
      {"automatic", 0}, {"policy", 1}, {"human", 2}, {"quorum", 3}};

  const auto &definition = commands_.resolve_exact(node.command_id);
  validate_inputs(definition, node.inputs);
  const std::vector<std::string> context_scope =
      broad_scope(context.scope)
          ? authority_.allowed_paths
          : narrow_scope(authority_.allowed_paths, context.scope);
  const auto requested_scope =
      node.inputs.contains("scope")
          ? scope_value(node.inputs.at("scope"), context_scope)
          : context_scope;
  const auto effective_scope = narrow_scope(context_scope, requested_scope);
  const auto command_capabilities = definition.capability_query.value(
      "facets", std::vector<std::string>{});
  const std::string command_level =
      definition.capability_query.value("level", "C0");
  if (ordered_rank(command_level, capability_order, "capability level") >= 4) {
    compilation_error(
        "C4 and C5 executors are fail-closed until explicitly qualified");
  }

  std::vector<std::string> allowed_capabilities = authority_.read_capabilities;
  allowed_capabilities.insert(allowed_capabilities.end(),
                              authority_.command_capabilities.begin(),
                              authority_.command_capabilities.end());
  allowed_capabilities.insert(allowed_capabilities.end(),
                              authority_.semantic_capabilities.begin(),
                              authority_.semantic_capabilities.end());
  allowed_capabilities = canonical_strings(std::move(allowed_capabilities));
  const Json selection_context = {
      {"allowed_capabilities", allowed_capabilities},
      {"budget", to_json(context.budget)},
      {"capability_ceiling", authority_.semantic_capability_ceiling},
      {"evidence_ids", canonical_strings(context.evidence)},
      {"inputs", node.inputs},
      {"scope", effective_scope},
      {"workspace_snapshot", workspace_.snapshot_hash()}};
  const auto selection = selector_.select(
      definition.command_id(), selection_context,
      authority_.semantic_capability_ceiling, allowed_capabilities,
      definition.invariants, ledger_support::utc_now());
  const std::string selection_id = store_.register_record(
      {.object_type = "selection-decision", .payload = to_json(selection)},
      "egcf_algorithm_selected");
  if (selection_id != selection.object_id()) {
    compilation_error("selection decision identity mismatch");
  }
  const auto &algorithm = algorithms_.resolve_exact(selection.selected_algorithm_id);
  auto requirements = command_capabilities;
  requirements.insert(requirements.end(),
                      algorithm.capability_requirements.begin(),
                      algorithm.capability_requirements.end());
  requirements = canonical_strings(std::move(requirements));
  const std::string required_level = maximum_by_order(
      {command_level, algorithm.capability_level}, capability_order, "C0",
      "capability level");
  if (ordered_rank(required_level, capability_order, "capability level") >
      ordered_rank(authority_.semantic_capability_ceiling, capability_order,
                   "capability ceiling")) {
    compilation_error("capability ceiling exceeded");
  }
  for (const auto &requirement : requirements) {
    if (std::ranges::find(allowed_capabilities, requirement) ==
        allowed_capabilities.end()) {
      compilation_error("missing capability: " + requirement);
    }
  }
  const std::string risk = maximum_by_order(
      {context.risk, definition.risk_policy, algorithm.risk_floor}, risk_order,
      "L0", "risk");
  const std::string rollback = maximum_by_order(
      {context.rollback, definition.rollback_policy, algorithm.rollback_class},
      rollback_order, "none", "rollback");
  const std::string approval = maximum_by_order(
      {context.approval, definition.approval_policy}, approval_order,
      "automatic", "approval");
  auto evidence = context.evidence;
  evidence.insert(evidence.end(), definition.evidence_requirements.begin(),
                  definition.evidence_requirements.end());
  evidence.insert(evidence.end(), algorithm.evidence_requirements.begin(),
                  algorithm.evidence_requirements.end());
  evidence = canonical_strings(std::move(evidence));
  return {{"algorithm_definition_id", algorithm.object_id()},
          {"algorithm_digest", algorithm.implementation_digest},
          {"algorithm_id", algorithm.algorithm_id()},
          {"approval_policy", approval},
          {"capability_level", required_level},
          {"capability_receipt",
           {{"authority_hash", authority_.authority_hash},
            {"capability_level", required_level},
            {"capability_requirements", requirements},
            {"scope", effective_scope}}},
          {"capability_requirements", requirements},
          {"checkpoint", node.checkpoint},
          {"command_definition_id", definition.object_id()},
          {"command_id", definition.command_id()},
          {"depends_on", canonical_strings(node.depends_on)},
          {"evidence_requirements", evidence},
          {"inputs", node.inputs},
          {"inputs_hash", contracts::sha256_json(node.inputs)},
          {"node_id", node.node_id},
          {"retry_limit", node.retry_limit},
          {"risk", risk},
          {"rollback_class", rollback},
          {"scope", effective_scope},
          {"selection_id", selection_id},
          {"when", node.when}};
}

std::pair<std::vector<std::string>, std::vector<Json>>
WorkflowCompiler::topological_order(const std::vector<WorkflowNode> &nodes) {
  std::map<std::string, const WorkflowNode *> by_id;
  for (const auto &node : nodes) {
    if (!by_id.emplace(node.node_id, &node).second) {
      compilation_error("workflow node IDs must be unique");
    }
  }
  std::map<std::string, int> incoming;
  std::map<std::string, std::vector<std::string>> outgoing;
  std::vector<Json> edges;
  for (const auto &node : nodes) {
    const auto dependencies = canonical_strings(node.depends_on);
    incoming[node.node_id] = static_cast<int>(dependencies.size());
    for (const auto &dependency : dependencies) {
      if (!by_id.contains(dependency)) {
        compilation_error("workflow dependency does not exist: " + dependency);
      }
      outgoing[dependency].push_back(node.node_id);
      edges.push_back({{"from", dependency}, {"to", node.node_id}});
    }
  }
  std::deque<std::string> ready;
  for (const auto &[node_id, count] : incoming) {
    if (count == 0) {
      ready.push_back(node_id);
    }
  }
  std::vector<std::string> order;
  while (!ready.empty()) {
    const std::string node_id = ready.front();
    ready.pop_front();
    order.push_back(node_id);
    auto &targets = outgoing[node_id];
    std::ranges::sort(targets);
    for (const auto &target : targets) {
      --incoming[target];
      if (incoming[target] == 0) {
        const auto position = std::ranges::lower_bound(ready, target);
        ready.insert(position, target);
      }
    }
  }
  if (order.size() != nodes.size()) {
    compilation_error("workflow graph contains a cycle");
  }
  std::ranges::sort(edges, [](const Json &left, const Json &right) {
    return std::tie(left.at("from"), left.at("to")) <
           std::tie(right.at("from"), right.at("to"));
  });
  return {std::move(order), std::move(edges)};
}

bool WorkflowCompiler::reachable(std::string_view source,
                                 std::string_view target,
                                 const std::vector<Json> &edges) {
  std::map<std::string, std::vector<std::string>> outgoing;
  for (const auto &edge : edges) {
    outgoing[edge.at("from").get<std::string>()].push_back(
        edge.at("to").get<std::string>());
  }
  std::vector<std::string> frontier{std::string(source)};
  std::set<std::string> visited;
  while (!frontier.empty()) {
    std::string current = std::move(frontier.back());
    frontier.pop_back();
    if (current == target) {
      return true;
    }
    if (!visited.insert(current).second) {
      continue;
    }
    const auto found = outgoing.find(current);
    if (found != outgoing.end()) {
      frontier.insert(frontier.end(), found->second.begin(), found->second.end());
    }
  }
  return false;
}

void WorkflowCompiler::validate_references(
    const WorkflowDefinition &workflow, const std::vector<Json> &edges) {
  std::set<std::string> node_ids;
  for (const auto &node : workflow.nodes) {
    node_ids.insert(node.node_id);
  }
  for (const auto &node : workflow.nodes) {
    const std::string base_command =
        node.command_id.substr(0, node.command_id.find('@'));
    const bool nested = base_command == "workflow.create" ||
                        base_command == "workflow.compile" ||
                        base_command == "algorithm.compose";
    Json source = {{"when", node.when}};
    if (!nested) {
      source["inputs"] = node.inputs;
    }
    std::vector<Json> references;
    collect_references(source, references);
    for (const auto &reference : references) {
      for (const auto &[key, unused] : reference.items()) {
        static_cast<void>(unused);
        if (key != "$from" && key != "path" && key != "default") {
          compilation_error("reference has unknown field: " + key);
        }
      }
      const std::string reference_source = reference.value("$from", "");
      if (!node_ids.contains(reference_source)) {
        compilation_error("reference source does not exist: " +
                          reference_source);
      }
      if (reference_source == node.node_id ||
          !reachable(reference_source, node.node_id, edges)) {
        compilation_error("node " + node.node_id +
                          " references non-dependent output " +
                          reference_source);
      }
      if (reference.contains("path") && !reference.at("path").is_array()) {
        compilation_error("reference path must be an array");
      }
      if (reference.contains("path")) {
        for (const auto &item : reference.at("path")) {
          if (!item.is_string() && !item.is_number_integer() &&
              !item.is_number_unsigned()) {
            compilation_error(
                "reference path must contain strings or integers");
          }
        }
      }
    }
  }
  std::vector<Json> output_references;
  collect_references(workflow.outputs, output_references);
  for (const auto &reference : output_references) {
    const std::string source = reference.value("$from", "");
    if (!node_ids.contains(source)) {
      compilation_error("workflow output reference does not exist: " + source);
    }
  }
}

std::vector<std::string> WorkflowCompiler::target_conflicts(
    const std::vector<Json> &nodes, const std::vector<Json> &edges) {
  std::vector<std::string> conflicts;
  for (std::size_t left_index = 0; left_index < nodes.size(); ++left_index) {
    const auto left_targets = mutation_targets(nodes[left_index]);
    if (left_targets.empty()) {
      continue;
    }
    for (std::size_t right_index = left_index + 1U; right_index < nodes.size();
         ++right_index) {
      const auto right_targets = mutation_targets(nodes[right_index]);
      std::vector<std::string> overlap;
      std::ranges::set_intersection(left_targets, right_targets,
                                    std::back_inserter(overlap));
      if (overlap.empty()) {
        continue;
      }
      const std::string left_id = nodes[left_index].at("node_id");
      const std::string right_id = nodes[right_index].at("node_id");
      if (!reachable(left_id, right_id, edges) &&
          !reachable(right_id, left_id, edges)) {
        conflicts.push_back("mutation targets " +
                            contracts::canonical_json(overlap) +
                            " overlap between " + left_id + " and " +
                            right_id);
      }
    }
  }
  return conflicts;
}

CompiledWorkflow WorkflowCompiler::compile(const WorkflowDefinition &workflow,
                                           const CommandContext &context) {
  static const std::map<std::string, int> capability_order = {
      {"C0", 0}, {"C1", 1}, {"C2", 2}, {"C3", 3}, {"C4", 4}, {"C5", 5}};
  static const std::map<std::string, int> risk_order = {
      {"L0", 0}, {"L1", 1}, {"L2", 2}};
  static const std::map<std::string, int> approval_order = {
      {"automatic", 0}, {"policy", 1}, {"human", 2}, {"quorum", 3}};
  context.validate();
  if (workflow.nodes.empty()) {
    compilation_error("workflow must contain at least one node");
  }
  const auto [order, edges] = topological_order(workflow.nodes);
  validate_references(workflow, edges);
  if (context.budget.actions &&
      static_cast<std::int64_t>(workflow.nodes.size()) >
          *context.budget.actions) {
    compilation_error("workflow node count exceeds action budget");
  }
  if (context.budget.retries) {
    std::int64_t retries = 0;
    for (const auto &node : workflow.nodes) {
      retries += node.retry_limit;
    }
    if (retries > *context.budget.retries) {
      compilation_error("workflow retry count exceeds retry budget");
    }
  }
  std::map<std::string, const WorkflowNode *> definitions;
  for (const auto &node : workflow.nodes) {
    definitions[node.node_id] = &node;
  }
  std::vector<Json> compiled_nodes;
  for (const auto &node_id : order) {
    compiled_nodes.push_back(compile_node(*definitions.at(node_id), context));
  }
  for (const auto &node : compiled_nodes) {
    const int level = ordered_rank(node.at("capability_level").get<std::string>(),
                                   capability_order, "capability level");
    if (level >= 3 && node.at("retry_limit").get<int>() != 0) {
      compilation_error(
          "mutating nodes cannot retry without a new candidate and approval");
    }
    if (level >= 3 && node.at("checkpoint").get<bool>()) {
      compilation_error(
          "checkpoints must occur before, not after, mutating nodes");
    }
    if (level >= 3 && node.at("rollback_class") == "none") {
      compilation_error("mutating node lacks rollback: " +
                        node.at("node_id").get<std::string>());
    }
  }
  auto unresolved = target_conflicts(compiled_nodes, edges);
  if (context.strict && !unresolved.empty()) {
    compilation_error("strict compilation has unresolved conflicts: " +
                      contracts::canonical_json(unresolved));
  }
  std::vector<std::string> levels;
  std::vector<std::string> risks;
  std::vector<std::string> approvals;
  std::vector<std::string> capabilities;
  std::vector<std::string> evidence;
  Json rollback_graph = Json::object();
  for (auto iterator = compiled_nodes.rbegin(); iterator != compiled_nodes.rend();
       ++iterator) {
    const Json &node = *iterator;
    levels.push_back(node.at("capability_level").get<std::string>());
    risks.push_back(node.at("risk").get<std::string>());
    approvals.push_back(node.at("approval_policy").get<std::string>());
    const auto node_capabilities =
        node.at("capability_requirements").get<std::vector<std::string>>();
    capabilities.insert(capabilities.end(), node_capabilities.begin(),
                        node_capabilities.end());
    const auto node_evidence =
        node.at("evidence_requirements").get<std::vector<std::string>>();
    evidence.insert(evidence.end(), node_evidence.begin(), node_evidence.end());
    if (ordered_rank(node.at("capability_level").get<std::string>(),
                     capability_order, "capability level") >= 3) {
      rollback_graph[node.at("node_id").get<std::string>()] =
          {{"class", node.at("rollback_class")},
           {"depends_on", node.at("depends_on")}};
    }
  }
  capabilities = canonical_strings(std::move(capabilities));
  evidence = canonical_strings(std::move(evidence));
  CompiledWorkflow compiled = {
      .workflow_id = workflow.workflow_id(),
      .source_snapshot_hash = workspace_.snapshot_hash(),
      .command_context = context,
      .nodes = compiled_nodes,
      .edges = edges,
      .execution_order = order,
      .capability_level = maximum_by_order(
          levels, capability_order, "C0", "capability level"),
      .capability_requirements = capabilities,
      .risk = maximum_by_order(risks, risk_order, "L0", "risk"),
      .evidence_requirements = evidence,
      .approval_policy = maximum_by_order(
          approvals, approval_order, "automatic", "approval"),
      .budget = context.budget,
      .rollback_graph = std::move(rollback_graph),
      .unresolved = std::move(unresolved),
      .created_at = ledger_support::utc_now(),
      .graph_hash = {}};
  Json graph_material = to_json(compiled);
  graph_material.erase("created_at");
  graph_material.erase("graph_hash");
  const Json context_json = to_json(context);
  graph_material["command_context"] =
      {{"approval", context_json.at("approval")},
       {"budget", context_json.at("budget")},
       {"evidence", context_json.at("evidence")},
       {"risk", context_json.at("risk")},
       {"rollback", context_json.at("rollback")},
       {"scope", context_json.at("scope")},
       {"simulate", context_json.at("simulate")},
       {"strict", context_json.at("strict")},
       {"timeout", context_json.at("timeout")}};
  Json graph_nodes = Json::array();
  for (auto node : compiled.nodes) {
    node.erase("selection_id");
    graph_nodes.push_back(std::move(node));
  }
  graph_material["nodes"] = std::move(graph_nodes);
  compiled.graph_hash = contracts::sha256_json(graph_material);

  const std::string workflow_id = store_.register_record(
      {.object_type = "workflow-definition", .payload = to_json(workflow)},
      "egcf_workflow_registered");
  if (workflow_id != workflow.object_id()) {
    compilation_error("workflow definition identity mismatch");
  }
  const std::string compiled_id = store_.register_record(
      {.object_type = "compiled-workflow", .payload = to_json(compiled)},
      "egcf_workflow_compiled");
  if (compiled_id != compiled.object_id()) {
    compilation_error("compiled workflow identity mismatch");
  }
  return compiled;
}

} // namespace statewright::egcf
