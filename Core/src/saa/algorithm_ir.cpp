#include "statewright/saa/algorithm_ir.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <sstream>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[noreturn]] void saa_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied, std::move(message));
}

[[nodiscard]] std::string trim(std::string value) {
  const auto non_space = [](char character) {
    return std::isspace(static_cast<unsigned char>(character)) == 0;
  };
  const auto first = std::find_if(value.begin(), value.end(), non_space);
  const auto last = std::find_if(value.rbegin(), value.rend(), non_space).base();
  if (first >= last) {
    return {};
  }
  return std::string(first, last);
}

[[nodiscard]] std::string lowercase(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

[[nodiscard]] std::string uppercase(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char character) {
    return static_cast<char>(std::toupper(character));
  });
  return value;
}

const std::map<std::string, PrimitiveSpec> &primitives() {
  static const std::map<std::string, PrimitiveSpec> values = {
      {"ABS", {"ABS", "arithmetic", false, false}},
      {"ADD", {"ADD", "arithmetic", true, true}},
      {"BACKTRACK", {"BACKTRACK", "reasoning", false, false}},
      {"BRANCH", {"BRANCH", "control", false, false}},
      {"CLAMP", {"CLAMP", "arithmetic", false, false}},
      {"COMPARE", {"COMPARE", "reasoning", true, false}},
      {"COMPARE_EQ", {"COMPARE_EQ", "predicate", true, false}},
      {"COMPARE_GE", {"COMPARE_GE", "predicate", false, false}},
      {"COMPARE_GT", {"COMPARE_GT", "predicate", false, false}},
      {"COMPARE_LE", {"COMPARE_LE", "predicate", false, false}},
      {"COMPARE_LT", {"COMPARE_LT", "predicate", false, false}},
      {"COMPARE_NE", {"COMPARE_NE", "predicate", true, false}},
      {"CONST", {"CONST", "data", false, false}},
      {"DIVIDE", {"DIVIDE", "arithmetic", false, false}},
      {"FALSIFY", {"FALSIFY", "reasoning", false, false}},
      {"GENERATE", {"GENERATE", "reasoning", false, false}},
      {"IDENTITY", {"IDENTITY", "data", false, false}},
      {"INVOKE", {"INVOKE", "execution", false, false}},
      {"ITERATE", {"ITERATE", "control", false, false}},
      {"MAX", {"MAX", "arithmetic", true, true}},
      {"MIN", {"MIN", "arithmetic", true, true}},
      {"MULTIPLY", {"MULTIPLY", "arithmetic", true, true}},
      {"NEGATE", {"NEGATE", "arithmetic", false, false}},
      {"OBSERVE", {"OBSERVE", "reasoning", false, false}},
      {"PREDICT", {"PREDICT", "reasoning", false, false}},
      {"PRUNE", {"PRUNE", "reasoning", false, false}},
      {"SELECT", {"SELECT", "control", false, false}},
      {"SUBTRACT", {"SUBTRACT", "arithmetic", false, false}},
      {"SYNTHESIZE", {"SYNTHESIZE", "reasoning", false, false}},
      {"TERMINATE", {"TERMINATE", "control", false, false}},
      {"VERIFY", {"VERIFY", "reasoning", false, false}},
  };
  return values;
}

const std::map<std::string, std::string> &aliases() {
  static const std::map<std::string, std::string> values = {
      {"+", "ADD"},          {"ADD", "ADD"},
      {"SUM", "ADD"},       {"-", "SUBTRACT"},
      {"SUB", "SUBTRACT"},  {"SUBTRACT", "SUBTRACT"},
      {"*", "MULTIPLY"},    {"MUL", "MULTIPLY"},
      {"MULTIPLY", "MULTIPLY"}, {"/", "DIVIDE"},
      {"DIV", "DIVIDE"},    {"DIVIDE", "DIVIDE"},
      {"==", "COMPARE_EQ"}, {"!=", "COMPARE_NE"},
      {"<", "COMPARE_LT"},  {"<=", "COMPARE_LE"},
      {">", "COMPARE_GT"},  {">=", "COMPARE_GE"},
  };
  return values;
}

void require_object(const Json &value, std::string_view label) {
  if (!value.is_object()) {
    saa_error(std::string(label) + " must be an object");
  }
}

void reject_unknown(const Json &value, const std::set<std::string> &allowed,
                    std::string_view label) {
  require_object(value, label);
  std::vector<std::string> unknown;
  for (const auto &[key, item] : value.items()) {
    static_cast<void>(item);
    if (!allowed.contains(key)) {
      unknown.push_back(key);
    }
  }
  if (!unknown.empty()) {
    std::ostringstream message;
    message << label << " has unknown fields:";
    for (const auto &key : unknown) {
      message << ' ' << key;
    }
    saa_error(message.str());
  }
}

[[nodiscard]] const Json &required(const Json &value, std::string_view key,
                                   std::string_view label) {
  const auto iterator = value.find(std::string(key));
  if (iterator == value.end()) {
    saa_error(std::string(label) + " requires field " + std::string(key));
  }
  return *iterator;
}

[[nodiscard]] int integer(const Json &value, std::string_view label) {
  if (!value.is_number_integer() && !value.is_number_unsigned()) {
    saa_error(std::string(label) + " must be an integer");
  }
  try {
    return value.get<int>();
  } catch (const Json::exception &) {
    saa_error(std::string(label) + " is outside the integer range");
  }
}

[[nodiscard]] std::vector<int> shape_from_json(const Json &value) {
  if (!value.is_array()) {
    saa_error("SAA shape must be an array");
  }
  std::vector<int> shape;
  for (const auto &dimension : value) {
    const int size = integer(dimension, "SAA shape dimension");
    if (size < 1) {
      saa_error("SAA shapes must contain positive dimensions");
    }
    shape.push_back(size);
  }
  return shape;
}

[[nodiscard]] OperandRef operand_from_mapping(const Json &value) {
  if (!value.is_object()) {
    if (value.is_array()) {
      saa_error("SAA constants must be scalar or tuple values");
    }
    return {.kind = "constant",
            .position = -1,
            .node_id = {},
            .output_index = 0,
            .value = value};
  }
  if (value.contains("input")) {
    reject_unknown(value, {"input"}, "SAA input reference");
    return {.kind = "input",
            .position = integer(value.at("input"), "SAA input position"),
            .node_id = {},
            .output_index = 0,
            .value = nullptr};
  }
  if (value.contains("parameter")) {
    reject_unknown(value, {"parameter"}, "SAA parameter reference");
    return {.kind = "parameter",
            .position =
                integer(value.at("parameter"), "SAA parameter position"),
            .node_id = {},
            .output_index = 0,
            .value = nullptr};
  }
  if (value.contains("state")) {
    reject_unknown(value, {"state"}, "SAA state reference");
    return {.kind = "state",
            .position = integer(value.at("state"), "SAA state position"),
            .node_id = {},
            .output_index = 0,
            .value = nullptr};
  }
  if (value.contains("node")) {
    reject_unknown(value, {"node", "output"}, "SAA node reference");
    return {.kind = "node",
            .position = -1,
            .node_id = value.at("node").get<std::string>(),
            .output_index = value.value("output", 0),
            .value = nullptr};
  }
  if (value.contains("constant")) {
    reject_unknown(value, {"constant"}, "SAA constant reference");
    const auto &constant = value.at("constant");
    if (constant.is_array() || constant.is_object()) {
      saa_error("SAA constants must be scalar or tuple values");
    }
    return {.kind = "constant",
            .position = -1,
            .node_id = {},
            .output_index = 0,
            .value = constant};
  }
  saa_error(
      "SAA operand mapping must identify input, parameter, state, node, or "
      "constant");
}

void validate_operand(OperandRef &operand) {
  operand.kind = lowercase(trim(operand.kind));
  if (operand.kind != "input" && operand.kind != "parameter" &&
      operand.kind != "state" && operand.kind != "node" &&
      operand.kind != "constant") {
    saa_error("unsupported SAA operand kind: " + operand.kind);
  }
  if ((operand.kind == "input" || operand.kind == "parameter" ||
       operand.kind == "state") &&
      operand.position < 0) {
    saa_error(operand.kind + " operand requires a non-negative position");
  }
  if (operand.kind == "node") {
    operand.node_id = trim(operand.node_id);
    if (operand.node_id.empty()) {
      saa_error("node operand requires node_id");
    }
    if (operand.output_index < 0) {
      saa_error("node output_index cannot be negative");
    }
  }
  if (operand.kind == "constant" &&
      (operand.value.is_array() || operand.value.is_object())) {
    saa_error("SAA constants must be scalar or tuple values");
  }
}

[[nodiscard]] PortSpec port_from_mapping(const Json &value,
                                         std::string role) {
  reject_unknown(value, {"position", "name", "data_type", "shape", "source"},
                 "SAA port");
  PortSpec port{.role = std::move(role),
                .position = integer(required(value, "position", "SAA port"),
                                    "SAA port position"),
                .name = value.value("name", ""),
                .data_type = value.value("data_type", "scalar"),
                .shape = value.contains("shape")
                             ? shape_from_json(value.at("shape"))
                             : std::vector<int>{},
                .source = std::nullopt};
  if (value.contains("source") && !value.at("source").is_null()) {
    port.source = operand_from_mapping(value.at("source"));
  }
  return port;
}

[[nodiscard]] StateSpec state_from_mapping(const Json &value) {
  reject_unknown(value,
                 {"position", "name", "data_type", "shape", "initial",
                  "update"},
                 "SAA state");
  StateSpec state{.position =
                      integer(required(value, "position", "SAA state"),
                              "SAA state position"),
                  .name = value.value("name", ""),
                  .data_type = value.value("data_type", "scalar"),
                  .shape = value.contains("shape")
                               ? shape_from_json(value.at("shape"))
                               : std::vector<int>{},
                  .initial = std::nullopt,
                  .update = std::nullopt};
  if (value.contains("initial") && !value.at("initial").is_null()) {
    state.initial = operand_from_mapping(value.at("initial"));
  }
  if (value.contains("update") && !value.at("update").is_null()) {
    state.update = operand_from_mapping(value.at("update"));
  }
  return state;
}

[[nodiscard]] AlgorithmNodeSpec node_from_mapping(const Json &value) {
  reject_unknown(value,
                 {"id", "primitive", "operands", "attributes",
                  "result_count"},
                 "SAA node");
  AlgorithmNodeSpec node{
      .node_id = required(value, "id", "SAA node").get<std::string>(),
      .primitive =
          required(value, "primitive", "SAA node").get<std::string>(),
      .operands = {},
      .attributes = {},
      .result_count = value.value("result_count", 1)};
  if (value.contains("operands")) {
    if (!value.at("operands").is_array()) {
      saa_error("SAA node operands must be an array");
    }
    for (const auto &operand : value.at("operands")) {
      node.operands.push_back(operand_from_mapping(operand));
    }
  }
  if (value.contains("attributes")) {
    require_object(value.at("attributes"), "SAA node attributes");
    for (const auto &[key, attribute] : value.at("attributes").items()) {
      const std::string name = trim(key);
      if (name.empty()) {
        saa_error("SAA attribute names must be non-empty");
      }
      node.attributes[name] = attribute;
    }
  }
  return node;
}

[[nodiscard]] ControlEdgeSpec edge_from_mapping(const Json &value) {
  reject_unknown(value, {"from", "to", "kind", "label"},
                 "SAA control edge");
  return {.source = required(value, "from", "SAA control edge")
                        .get<std::string>(),
          .target =
              required(value, "to", "SAA control edge").get<std::string>(),
          .kind = value.value("kind", "next"),
          .label = value.value("label", "")};
}

void validate_port(PortSpec &port) {
  port.role = uppercase(trim(port.role));
  if (port.role != "INPUT" && port.role != "OUTPUT" &&
      port.role != "PARAMETER") {
    saa_error("unsupported SAA port role: " + port.role);
  }
  if (port.position < 0) {
    saa_error("SAA port position cannot be negative");
  }
  for (const int dimension : port.shape) {
    if (dimension < 1) {
      saa_error("SAA shapes must contain positive dimensions");
    }
  }
  if (port.source) {
    validate_operand(*port.source);
  }
  if (port.role == "OUTPUT" && !port.source) {
    saa_error("SAA output ports require a source binding");
  }
  if (port.role != "OUTPUT" && port.source) {
    saa_error("only SAA output ports may bind a source");
  }
}

void validate_state(StateSpec &state) {
  if (state.position < 0) {
    saa_error("SAA state position cannot be negative");
  }
  for (const int dimension : state.shape) {
    if (dimension < 1) {
      saa_error("SAA shapes must contain positive dimensions");
    }
  }
  if (state.initial) {
    validate_operand(*state.initial);
  }
  if (state.update) {
    validate_operand(*state.update);
  }
}

void validate_node(AlgorithmNodeSpec &node) {
  node.node_id = trim(node.node_id);
  if (node.node_id.empty()) {
    saa_error("SAA node_id must be non-empty");
  }
  node.primitive = normalize_primitive(node.primitive).name;
  if (node.result_count < 0) {
    saa_error("SAA result_count cannot be negative");
  }
  for (auto &operand : node.operands) {
    validate_operand(operand);
  }
}

void validate_edge(ControlEdgeSpec &edge) {
  edge.source = trim(edge.source);
  edge.target = trim(edge.target);
  edge.kind = lowercase(trim(edge.kind));
  edge.label = trim(edge.label);
  if (edge.source.empty() || edge.target.empty()) {
    saa_error("SAA control edges require source and target");
  }
  static const std::set<std::string> kinds = {
      "next", "true", "false", "loop", "backtrack", "terminate",
      "exception"};
  if (!kinds.contains(edge.kind)) {
    saa_error("unsupported SAA control edge kind: " + edge.kind);
  }
}

template <typename Value>
[[nodiscard]] std::map<int, const Value *>
position_map(const std::vector<Value> &values, std::string_view role) {
  std::map<int, const Value *> result;
  for (const auto &value : values) {
    if (!result.emplace(value.position, &value).second) {
      saa_error("duplicate SAA " + std::string(role) + " position: " +
                std::to_string(value.position));
    }
  }
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (!result.contains(static_cast<int>(index))) {
      saa_error("SAA " + std::string(role) +
                " positions must be contiguous 0.." +
                std::to_string(values.empty() ? 0U : values.size() - 1U));
    }
  }
  return result;
}

void validate_ref(const OperandRef &ref,
                  const std::map<int, const PortSpec *> &inputs,
                  const std::map<int, const PortSpec *> &parameters,
                  const std::map<int, const StateSpec *> &states,
                  const std::map<std::string, const AlgorithmNodeSpec *> &nodes) {
  if (ref.kind == "input" && !inputs.contains(ref.position)) {
    saa_error("SAA reference uses unknown input position " +
              std::to_string(ref.position));
  }
  if (ref.kind == "parameter" && !parameters.contains(ref.position)) {
    saa_error("SAA reference uses unknown parameter position " +
              std::to_string(ref.position));
  }
  if (ref.kind == "state" && !states.contains(ref.position)) {
    saa_error("SAA reference uses unknown state position " +
              std::to_string(ref.position));
  }
  if (ref.kind == "node") {
    const auto iterator = nodes.find(ref.node_id);
    if (iterator == nodes.end()) {
      saa_error("SAA reference uses unknown node " + ref.node_id);
    }
    if (ref.output_index >= iterator->second->result_count) {
      saa_error("SAA reference output exceeds node result count");
    }
  }
}

[[nodiscard]] Json canonical_value(const Json &value) {
  if (value.is_null() || value.is_boolean() || value.is_string()) {
    return value;
  }
  if (value.is_number_integer() || value.is_number_unsigned()) {
    return {{"number", value.dump()}};
  }
  if (value.is_number_float()) {
    double number = value.get<double>();
    if (!std::isfinite(number)) {
      saa_error("SAA canonical values cannot contain NaN or infinity");
    }
    if (number == 0.0) {
      number = 0.0;
    }
    char buffer[128]{};
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), number,
                                      std::chars_format::general, 17);
    if (result.ec != std::errc{}) {
      saa_error("cannot canonicalize SAA floating-point value");
    }
    return {{"number", std::string(buffer, result.ptr)}};
  }
  if (value.is_array()) {
    Json result = Json::array();
    for (const auto &item : value) {
      result.push_back(canonical_value(item));
    }
    return result;
  }
  if (value.is_object()) {
    Json result = Json::object();
    for (const auto &[key, item] : value.items()) {
      result[key] = canonical_value(item);
    }
    return result;
  }
  saa_error("unsupported SAA canonical value type");
}

[[nodiscard]] Json attributes(const AlgorithmNodeSpec &node) {
  static const std::set<std::string> ignored = {
      "comment",     "description", "display_name", "name",
      "source_column", "source_file", "source_line"};
  Json result = Json::array();
  for (const auto &[key, value] : node.attributes) {
    if (!ignored.contains(key)) {
      result.push_back(Json::array({key, canonical_value(value)}));
    }
  }
  return result;
}

void sort_json(Json &values) {
  std::vector<Json> ordered(values.begin(), values.end());
  std::ranges::sort(ordered, [](const Json &left, const Json &right) {
    return contracts::canonical_json(left) < contracts::canonical_json(right);
  });
  values = std::move(ordered);
}

using Tags = std::map<std::string, Json>;

[[nodiscard]] Tags external_tags(const AlgorithmStructureSpec &spec) {
  Tags tags;
  for (const auto &output : spec.outputs) {
    if (output.source && output.source->kind == "node") {
      tags[output.source->node_id].push_back(
          Json::array({"output", output.position, output.source->output_index}));
    }
  }
  for (const auto &state : spec.states) {
    if (state.initial && state.initial->kind == "node") {
      tags[state.initial->node_id].push_back(Json::array(
          {"state_initial", state.position, state.initial->output_index}));
    }
    if (state.update && state.update->kind == "node") {
      tags[state.update->node_id].push_back(Json::array(
          {"state_update", state.position, state.update->output_index}));
    }
  }
  for (const auto &identity : spec.entry_nodes) {
    tags[identity].push_back(Json::array({"entry"}));
  }
  for (const auto &identity : spec.termination_nodes) {
    tags[identity].push_back(Json::array({"termination"}));
  }
  for (auto &[identity, values] : tags) {
    static_cast<void>(identity);
    sort_json(values);
  }
  return tags;
}

[[nodiscard]] Json ref_skeleton(const OperandRef &ref) {
  if (ref.kind == "node") {
    return Json::array({"node", ref.output_index});
  }
  if (ref.kind == "constant") {
    return Json::array({"constant", canonical_value(ref.value)});
  }
  const std::string prefix =
      ref.kind == "input" ? "u" : ref.kind == "parameter" ? "p" : "x";
  return Json::array({prefix, ref.position});
}

[[nodiscard]] Json static_material(const AlgorithmNodeSpec &node,
                                   const Tags &tags) {
  Json operands = Json::array();
  for (const auto &operand : node.operands) {
    operands.push_back(ref_skeleton(operand));
  }
  if (primitives().at(node.primitive).commutative) {
    sort_json(operands);
  }
  const auto tag_iterator = tags.find(node.node_id);
  return {{"attributes", attributes(node)},
          {"external_tags", tag_iterator == tags.end() ? Json::array()
                                                       : tag_iterator->second},
          {"operands", operands},
          {"primitive", node.primitive},
          {"result_count", node.result_count}};
}

[[nodiscard]] Json ref_color(const OperandRef &ref,
                             const std::map<std::string, std::string> &colors) {
  if (ref.kind == "node") {
    return Json::array(
        {"node", colors.at(ref.node_id), ref.output_index});
  }
  return ref_skeleton(ref);
}

[[nodiscard]] std::map<std::string, std::string>
refine_colors(const AlgorithmStructureSpec &spec) {
  const auto tags = external_tags(spec);
  std::map<std::string, const AlgorithmNodeSpec *> nodes;
  std::map<std::string, std::vector<ControlEdgeSpec>> incoming;
  std::map<std::string, std::vector<ControlEdgeSpec>> outgoing;
  for (const auto &node : spec.nodes) {
    nodes[node.node_id] = &node;
  }
  for (const auto &edge : spec.control_edges) {
    outgoing[edge.source].push_back(edge);
    incoming[edge.target].push_back(edge);
  }
  std::map<std::string, std::string> colors;
  for (const auto &[identity, node] : nodes) {
    colors[identity] = contracts::sha256_json(static_material(*node, tags));
  }
  const std::size_t rounds = std::max<std::size_t>(2U, nodes.size() + 1U);
  for (std::size_t round = 0; round < rounds; ++round) {
    std::map<std::string, std::string> revised;
    for (const auto &[identity, node] : nodes) {
      Json operands = Json::array();
      for (const auto &operand : node->operands) {
        operands.push_back(ref_color(operand, colors));
      }
      if (primitives().at(node->primitive).commutative) {
        sort_json(operands);
      }
      Json incoming_edges = Json::array();
      for (const auto &edge : incoming[identity]) {
        incoming_edges.push_back(
            Json::array({edge.kind, edge.label, colors.at(edge.source)}));
      }
      Json outgoing_edges = Json::array();
      for (const auto &edge : outgoing[identity]) {
        outgoing_edges.push_back(
            Json::array({edge.kind, edge.label, colors.at(edge.target)}));
      }
      sort_json(incoming_edges);
      sort_json(outgoing_edges);
      revised[identity] = contracts::sha256_json(
          {{"incoming", incoming_edges},
           {"operands", operands},
           {"outgoing", outgoing_edges},
           {"static", static_material(*node, tags)}});
    }
    if (revised == colors) {
      break;
    }
    colors = std::move(revised);
  }
  return colors;
}

[[nodiscard]] Json ref_token(const OperandRef &ref,
                             const std::map<std::string, int> &node_index) {
  if (ref.kind == "node") {
    return Json::array({"n", node_index.at(ref.node_id), ref.output_index});
  }
  if (ref.kind == "constant") {
    return Json::array({"c", canonical_value(ref.value)});
  }
  const std::string prefix =
      ref.kind == "input" ? "u" : ref.kind == "parameter" ? "p" : "x";
  return Json::array({prefix, ref.position});
}

[[nodiscard]] Json port_payload(const PortSpec &port) {
  return {{"data_type", port.data_type},
          {"position", port.position},
          {"shape", port.shape}};
}

template <typename Value>
[[nodiscard]] std::vector<Value> sorted_positions(std::vector<Value> values) {
  std::ranges::sort(values, {}, &Value::position);
  return values;
}

[[nodiscard]] Json serialize_exact(const AlgorithmStructureSpec &spec,
                                   const std::vector<std::string> &order) {
  std::map<std::string, const AlgorithmNodeSpec *> node_map;
  std::map<std::string, int> node_index;
  for (const auto &node : spec.nodes) {
    node_map[node.node_id] = &node;
  }
  for (std::size_t index = 0; index < order.size(); ++index) {
    node_index[order[index]] = static_cast<int>(index);
  }
  Json nodes = Json::array();
  for (const auto &identity : order) {
    const auto &node = *node_map.at(identity);
    Json operands = Json::array();
    for (const auto &operand : node.operands) {
      operands.push_back(ref_token(operand, node_index));
    }
    if (primitives().at(node.primitive).commutative) {
      sort_json(operands);
    }
    nodes.push_back({{"attributes", attributes(node)},
                     {"operands", operands},
                     {"primitive", node.primitive},
                     {"result_count", node.result_count}});
  }
  Json inputs = Json::array();
  for (const auto &port : sorted_positions(spec.inputs)) {
    inputs.push_back(port_payload(port));
  }
  Json parameters = Json::array();
  for (const auto &port : sorted_positions(spec.parameters)) {
    parameters.push_back(port_payload(port));
  }
  Json outputs = Json::array();
  for (const auto &port : sorted_positions(spec.outputs)) {
    auto payload = port_payload(port);
    payload["source"] = ref_token(*port.source, node_index);
    outputs.push_back(std::move(payload));
  }
  Json states = Json::array();
  for (const auto &state : sorted_positions(spec.states)) {
    states.push_back(
        {{"data_type", state.data_type},
         {"initial", state.initial ? ref_token(*state.initial, node_index)
                                    : Json(nullptr)},
         {"position", state.position},
         {"shape", state.shape},
         {"update", state.update ? ref_token(*state.update, node_index)
                                  : Json(nullptr)}});
  }
  Json edges = Json::array();
  for (const auto &edge : spec.control_edges) {
    edges.push_back(Json::array({node_index.at(edge.source),
                                 node_index.at(edge.target), edge.kind,
                                 edge.label}));
  }
  sort_json(edges);
  std::vector<int> entry;
  for (const auto &identity : spec.entry_nodes) {
    entry.push_back(node_index.at(identity));
  }
  std::ranges::sort(entry);
  std::vector<int> termination;
  for (const auto &identity : spec.termination_nodes) {
    termination.push_back(node_index.at(identity));
  }
  std::ranges::sort(termination);
  return {{"control_edges", edges},
          {"entry_nodes", entry},
          {"inputs", inputs},
          {"nodes", nodes},
          {"outputs", outputs},
          {"parameters", parameters},
          {"schema_version", 1},
          {"states", states},
          {"termination_nodes", termination}};
}

[[nodiscard]] Json serialize_refined(
    const AlgorithmStructureSpec &spec,
    const std::map<std::string, std::string> &colors) {
  const auto tags = external_tags(spec);
  Json nodes = Json::array();
  for (const auto &node : spec.nodes) {
    Json operands = Json::array();
    for (const auto &operand : node.operands) {
      operands.push_back(ref_color(operand, colors));
    }
    if (primitives().at(node.primitive).commutative) {
      sort_json(operands);
    }
    const auto iterator = tags.find(node.node_id);
    nodes.push_back(
        {{"attributes", attributes(node)},
         {"color", colors.at(node.node_id)},
         {"external_tags", iterator == tags.end() ? Json::array()
                                                   : iterator->second},
         {"operands", operands},
         {"primitive", node.primitive},
         {"result_count", node.result_count}});
  }
  sort_json(nodes);
  Json inputs = Json::array();
  for (const auto &port : sorted_positions(spec.inputs)) {
    inputs.push_back(port_payload(port));
  }
  Json parameters = Json::array();
  for (const auto &port : sorted_positions(spec.parameters)) {
    parameters.push_back(port_payload(port));
  }
  Json outputs = Json::array();
  for (const auto &port : spec.outputs) {
    auto payload = port_payload(port);
    if (port.source && port.source->kind == "node") {
      payload["source"] = Json::array(
          {"node-color", colors.at(port.source->node_id),
           port.source->output_index});
    } else {
      payload["source"] = port.source ? ref_skeleton(*port.source) : Json(nullptr);
    }
    outputs.push_back(std::move(payload));
  }
  sort_json(outputs);
  Json states = Json::array();
  for (const auto &state : spec.states) {
    const auto refined_ref = [&](const std::optional<OperandRef> &ref) -> Json {
      if (!ref) {
        return nullptr;
      }
      if (ref->kind == "node") {
        return Json::array(
            {"node-color", colors.at(ref->node_id), ref->output_index});
      }
      return ref_skeleton(*ref);
    };
    states.push_back({{"data_type", state.data_type},
                      {"initial", refined_ref(state.initial)},
                      {"position", state.position},
                      {"shape", state.shape},
                      {"update", refined_ref(state.update)}});
  }
  sort_json(states);
  Json edges = Json::array();
  for (const auto &edge : spec.control_edges) {
    edges.push_back(Json::array({colors.at(edge.source), colors.at(edge.target),
                                 edge.kind, edge.label}));
  }
  sort_json(edges);
  std::vector<std::string> entry;
  for (const auto &identity : spec.entry_nodes) {
    entry.push_back(colors.at(identity));
  }
  std::ranges::sort(entry);
  std::vector<std::string> termination;
  for (const auto &identity : spec.termination_nodes) {
    termination.push_back(colors.at(identity));
  }
  std::ranges::sort(termination);
  return {{"control_edges", edges},
          {"entry_nodes", entry},
          {"inputs", inputs},
          {"nodes", nodes},
          {"outputs", outputs},
          {"parameters", parameters},
          {"schema_version", 1},
          {"states", states},
          {"strength", "REFINED_FINGERPRINT"},
          {"termination_nodes", termination}};
}

[[nodiscard]] std::uint64_t factorial(std::size_t value) {
  std::uint64_t result = 1;
  for (std::size_t factor = 2; factor <= value; ++factor) {
    if (result > std::numeric_limits<std::uint64_t>::max() / factor) {
      return std::numeric_limits<std::uint64_t>::max();
    }
    result *= factor;
  }
  return result;
}

} // namespace

PrimitiveSpec normalize_primitive(std::string value) {
  std::string key = uppercase(trim(std::move(value)));
  const auto alias = aliases().find(key);
  if (alias != aliases().end()) {
    key = alias->second;
  }
  const auto primitive = primitives().find(key);
  if (primitive == primitives().end()) {
    saa_error("unknown SAA primitive: " + key);
  }
  return primitive->second;
}

std::vector<std::string> primitive_names() {
  std::vector<std::string> result;
  for (const auto &[name, primitive] : primitives()) {
    static_cast<void>(primitive);
    result.push_back(name);
  }
  return result;
}

AlgorithmStructureSpec structure_from_mapping(const Json &payload) {
  reject_unknown(payload,
                 {"name", "inputs", "outputs", "parameters", "states",
                  "nodes", "control_edges", "entry_nodes",
                  "termination_nodes", "metadata"},
                 "SAA structure");
  AlgorithmStructureSpec spec;
  spec.name = payload.value("name", "anonymous");
  const auto read_array = [&](std::string_view key,
                              const auto &consumer) {
    const auto iterator = payload.find(std::string(key));
    if (iterator == payload.end()) {
      return;
    }
    if (!iterator->is_array()) {
      saa_error("SAA " + std::string(key) + " must be an array");
    }
    for (const auto &item : *iterator) {
      consumer(item);
    }
  };
  read_array("inputs", [&](const Json &item) {
    spec.inputs.push_back(port_from_mapping(item, "INPUT"));
  });
  read_array("outputs", [&](const Json &item) {
    spec.outputs.push_back(port_from_mapping(item, "OUTPUT"));
  });
  read_array("parameters", [&](const Json &item) {
    spec.parameters.push_back(port_from_mapping(item, "PARAMETER"));
  });
  read_array("states", [&](const Json &item) {
    spec.states.push_back(state_from_mapping(item));
  });
  read_array("nodes", [&](const Json &item) {
    spec.nodes.push_back(node_from_mapping(item));
  });
  read_array("control_edges", [&](const Json &item) {
    spec.control_edges.push_back(edge_from_mapping(item));
  });
  read_array("entry_nodes", [&](const Json &item) {
    spec.entry_nodes.push_back(item.get<std::string>());
  });
  read_array("termination_nodes", [&](const Json &item) {
    spec.termination_nodes.push_back(item.get<std::string>());
  });
  spec.metadata = payload.value("metadata", Json::object());
  for (auto &port : spec.inputs) {
    validate_port(port);
  }
  for (auto &port : spec.outputs) {
    validate_port(port);
  }
  for (auto &port : spec.parameters) {
    validate_port(port);
  }
  for (auto &state : spec.states) {
    validate_state(state);
  }
  for (auto &node : spec.nodes) {
    validate_node(node);
  }
  for (auto &edge : spec.control_edges) {
    validate_edge(edge);
  }
  return spec;
}

void validate_structure(const AlgorithmStructureSpec &spec) {
  const auto inputs = position_map(spec.inputs, "input");
  const auto outputs = position_map(spec.outputs, "output");
  const auto parameters = position_map(spec.parameters, "parameter");
  const auto states = position_map(spec.states, "state");
  for (const auto &port : spec.inputs) {
    if (port.role != "INPUT") {
      saa_error("SAA inputs must use INPUT port role");
    }
  }
  for (const auto &port : spec.outputs) {
    if (port.role != "OUTPUT") {
      saa_error("SAA outputs must use OUTPUT port role");
    }
  }
  for (const auto &port : spec.parameters) {
    if (port.role != "PARAMETER") {
      saa_error("SAA parameters must use PARAMETER port role");
    }
  }
  std::map<std::string, const AlgorithmNodeSpec *> nodes;
  for (const auto &node : spec.nodes) {
    if (!nodes.emplace(node.node_id, &node).second) {
      saa_error("SAA node IDs must be unique");
    }
  }
  if (nodes.empty()) {
    saa_error("SAA structural IR requires at least one node");
  }
  for (const auto &node : spec.nodes) {
    for (const auto &operand : node.operands) {
      validate_ref(operand, inputs, parameters, states, nodes);
    }
  }
  for (const auto &[position, output] : outputs) {
    static_cast<void>(position);
    validate_ref(*output->source, inputs, parameters, states, nodes);
  }
  for (const auto &[position, state] : states) {
    static_cast<void>(position);
    if (state->initial) {
      validate_ref(*state->initial, inputs, parameters, states, nodes);
    }
    if (state->update) {
      validate_ref(*state->update, inputs, parameters, states, nodes);
    }
  }
  for (const auto &edge : spec.control_edges) {
    if (!nodes.contains(edge.source) || !nodes.contains(edge.target)) {
      saa_error("SAA control edge references an unknown node");
    }
  }
  for (const auto &identity : spec.entry_nodes) {
    if (!nodes.contains(identity)) {
      saa_error("SAA external node reference does not exist: " + identity);
    }
  }
  for (const auto &identity : spec.termination_nodes) {
    const auto iterator = nodes.find(identity);
    if (iterator == nodes.end()) {
      saa_error("SAA external node reference does not exist: " + identity);
    }
    if (iterator->second->primitive != "TERMINATE") {
      saa_error("SAA termination_nodes must identify TERMINATE primitives");
    }
  }
}

CanonicalAlgorithmIR canonicalize_structure(
    const AlgorithmStructureSpec &spec,
    std::uint64_t max_exact_permutations) {
  validate_structure(spec);
  if (max_exact_permutations < 1U) {
    saa_error("max_exact_permutations must be positive");
  }
  const auto colors = refine_colors(spec);
  std::map<std::string, std::vector<std::string>> grouped;
  for (const auto &[identity, color] : colors) {
    grouped[color].push_back(identity);
  }
  std::vector<std::vector<std::string>> groups;
  std::uint64_t permutation_count = 1;
  for (auto &[color, identities] : grouped) {
    static_cast<void>(color);
    std::ranges::sort(identities);
    const std::uint64_t group_count = factorial(identities.size());
    if (permutation_count >
        std::numeric_limits<std::uint64_t>::max() / group_count) {
      permutation_count = std::numeric_limits<std::uint64_t>::max();
    } else {
      permutation_count *= group_count;
    }
    groups.push_back(std::move(identities));
  }

  if (permutation_count <= max_exact_permutations) {
    std::optional<std::string> best_json;
    Json best_payload;
    std::vector<std::string> best_order;
    std::vector<std::string> current;
    const std::function<void(std::size_t)> visit = [&](std::size_t index) {
      if (index == groups.size()) {
        const Json payload = serialize_exact(spec, current);
        const std::string serialized = contracts::canonical_json(payload);
        if (!best_json || serialized < *best_json) {
          best_json = serialized;
          best_payload = payload;
          best_order = current;
        }
        return;
      }
      auto permutation = groups[index];
      do {
        const auto prior_size = current.size();
        current.insert(current.end(), permutation.begin(), permutation.end());
        visit(index + 1U);
        current.resize(prior_size);
      } while (std::next_permutation(permutation.begin(), permutation.end()));
    };
    visit(0);
    std::map<std::string, int> node_index;
    for (std::size_t index = 0; index < best_order.size(); ++index) {
      node_index[best_order[index]] = static_cast<int>(index);
    }
    CanonicalAlgorithmIR result;
    result.structural_hash = contracts::sha256_json(best_payload);
    result.canonical_payload = std::move(best_payload);
    result.canonicalization_strength = "EXACT_STRUCTURAL";
    result.exact_permutations_considered = permutation_count;
    for (const auto &[identity, index] : node_index) {
      result.source_node_map.emplace_back(identity, index);
    }
    return result;
  }

  const Json payload = serialize_refined(spec, colors);
  CanonicalAlgorithmIR result;
  result.structural_hash = contracts::sha256_json(payload);
  result.canonical_payload = payload;
  result.canonicalization_strength = "REFINED_FINGERPRINT";
  result.exact_permutations_considered = 0;
  std::vector<std::pair<std::string, std::string>> ordered_colors;
  for (const auto &[identity, color] : colors) {
    ordered_colors.emplace_back(color, identity);
  }
  std::ranges::sort(ordered_colors);
  for (std::size_t index = 0; index < ordered_colors.size(); ++index) {
    result.source_node_map.emplace_back(ordered_colors[index].second,
                                        static_cast<int>(index));
  }
  result.warnings.push_back(
      "exact structural canonicalization would require " +
      std::to_string(permutation_count) +
      " permutations; using invariant refinement fingerprint");
  return result;
}

CanonicalAlgorithmIR canonicalize_mapping(const Json &payload,
                                           std::uint64_t max_exact_permutations) {
  return canonicalize_structure(structure_from_mapping(payload),
                                max_exact_permutations);
}

Json to_json(const CanonicalAlgorithmIR &value) {
  Json node_map = Json::array();
  for (const auto &[identity, index] : value.source_node_map) {
    node_map.push_back(Json::array({identity, index}));
  }
  return {{"canonical_payload", value.canonical_payload},
          {"canonicalization_strength", value.canonicalization_strength},
          {"canonicalizer_version", value.canonicalizer_version_value},
          {"exact_permutations_considered",
           value.exact_permutations_considered},
          {"schema_version", value.schema_version},
          {"source_node_map", node_map},
          {"structural_hash", value.structural_hash},
          {"warnings", value.warnings}};
}

} // namespace statewright::saa
