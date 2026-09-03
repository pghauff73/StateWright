#include "statewright/saa/reasoning_algebra.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

const std::set<std::string> reasoning_operators = {
    "OBSERVE",      "CLASSIFY",   "DECOMPOSE", "GENERATE",
    "COMPARE",      "PREDICT",    "ABSTRACT",  "SPECIALIZE",
    "GENERALIZE",   "DEDUCE",     "INDUCE",    "ABDUCE",
    "FALSIFY",      "VERIFY",     "DISCRIMINATE",
    "OPTIMIZE",     "PRUNE",      "BACKTRACK", "SYNTHESIZE",
    "BOUND",        "TERMINATE"};
const std::set<std::string> reasoning_relations = {
    "NEXT",       "DEPENDS_ON",  "SUPPORTS",    "WARRANTS",
    "ATTACKS",    "REBUTS",     "QUALIFIES",   "LIMITS",
    "ENTAILS",    "FALSIFIES",  "BRANCH_TRUE", "BRANCH_FALSE",
    "BACKTRACK_TO"};

[[noreturn]] void reasoning_algebra_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument,
                      std::move(message));
}

[[nodiscard]] std::string trimmed(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n\f\v");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n\f\v");
  return value.substr(first, last - first + 1U);
}

[[nodiscard]] std::string normalized_text(std::string value) {
  std::istringstream input(value);
  std::ostringstream output;
  std::string word;
  bool first = true;
  while (input >> word) {
    if (!first) {
      output << ' ';
    }
    std::transform(word.begin(), word.end(), word.begin(),
                   [](unsigned char character) {
                     return static_cast<char>(std::tolower(character));
                   });
    output << word;
    first = false;
  }
  return output.str();
}

[[nodiscard]] std::string uppercase(std::string value) {
  value = trimmed(std::move(value));
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::toupper(character));
                 });
  return value;
}

[[nodiscard]] std::string normalized_operator(std::string value) {
  value = uppercase(std::move(value));
  if (!reasoning_operators.contains(value)) {
    reasoning_algebra_error("unsupported SAA-8 reasoning operator: '" + value +
                            "'");
  }
  return value;
}

[[nodiscard]] std::string normalized_relation(std::string value) {
  value = uppercase(std::move(value));
  if (!reasoning_relations.contains(value)) {
    reasoning_algebra_error(
        "unsupported SAA-8 reasoning edge relation: '" + value + "'");
  }
  return value;
}

[[nodiscard]] std::vector<std::string>
normalized_texts(std::vector<std::string> values) {
  std::set<std::string> result;
  for (auto &value : values) {
    value = normalized_text(std::move(value));
    if (!value.empty()) {
      result.insert(std::move(value));
    }
  }
  return {result.begin(), result.end()};
}

[[nodiscard]] Json node_intrinsic(const ReasoningNodeSpec &node) {
  return {{"assumptions", normalized_texts(node.assumptions)},
          {"evidence_requirements",
           normalized_texts(node.evidence_requirements)},
          {"falsifiers", normalized_texts(node.falsifiers)},
          {"operator", normalized_operator(node.operator_name)},
          {"public_claim_ids", normalized_texts(node.public_claim_ids)},
          {"semantic_inputs", normalized_texts(node.semantic_inputs)},
          {"semantic_outputs", normalized_texts(node.semantic_outputs)}};
}

[[nodiscard]] Json topology_intrinsic(const ReasoningNodeSpec &node) {
  return {{"operator", normalized_operator(node.operator_name)}};
}

struct ValidatedReasoning final {
  std::map<std::string, ReasoningNodeSpec> nodes;
  std::vector<ReasoningEdgeSpec> edges;
};

[[nodiscard]] ValidatedReasoning validate(const ReasoningAlgorithmSpec &spec) {
  if (spec.nodes.empty() || spec.nodes.size() > max_reasoning_nodes) {
    reasoning_algebra_error("SAA-8 node count outside bounded range");
  }
  if (spec.edges.size() > max_reasoning_edges) {
    reasoning_algebra_error("SAA-8 edge count exceeds bounded cap");
  }
  ValidatedReasoning result;
  for (const auto &node : spec.nodes) {
    const std::string node_id = trimmed(node.node_id);
    if (node_id.empty() || result.nodes.contains(node_id)) {
      reasoning_algebra_error(
          "SAA-8 reasoning node IDs must be unique and non-empty");
    }
    static_cast<void>(normalized_operator(node.operator_name));
    result.nodes.emplace(node_id, node);
  }
  std::set<std::tuple<std::string, std::string, std::string, std::string>>
      seen_edges;
  for (const auto &edge : spec.edges) {
    const std::string source = trimmed(edge.source);
    const std::string target = trimmed(edge.target);
    if (!result.nodes.contains(source) || !result.nodes.contains(target)) {
      reasoning_algebra_error(
          "SAA-8 reasoning edge references unknown node");
    }
    const std::string relation = normalized_relation(edge.relation);
    const std::string condition = normalized_text(edge.condition);
    const auto key = std::make_tuple(source, target, relation, condition);
    if (!seen_edges.insert(key).second) {
      reasoning_algebra_error("duplicate SAA-8 reasoning edge");
    }
    result.edges.push_back({source, target, relation, condition});
  }
  if (spec.termination.max_steps < 1 ||
      spec.termination.max_steps > max_reasoning_steps) {
    reasoning_algebra_error(
        "SAA-8 termination step bound outside supported range");
  }
  if (normalized_text(spec.termination.kind).empty() ||
      normalized_text(spec.termination.predicate).empty()) {
    reasoning_algebra_error(
        "SAA-8 termination kind and predicate must be explicit");
  }
  return result;
}

using IntrinsicMap = std::map<std::string, Json>;

[[nodiscard]] std::map<std::string, std::string> refined_colors(
    const IntrinsicMap &intrinsic, const std::vector<ReasoningEdgeSpec> &edges,
    bool include_condition) {
  std::map<std::string, std::string> colors;
  for (const auto &[node_id, payload] : intrinsic) {
    colors.emplace(node_id, contracts::sha256_json(payload));
  }
  for (std::size_t iteration = 0; iteration < intrinsic.size() + 1U;
       ++iteration) {
    std::map<std::string, std::string> updated;
    for (const auto &[node_id, payload] : intrinsic) {
      std::vector<std::array<std::string, 3>> incoming;
      std::vector<std::array<std::string, 3>> outgoing;
      for (const auto &edge : edges) {
        if (edge.target == node_id) {
          incoming.push_back(
              {edge.relation, include_condition ? edge.condition : "",
               colors.at(edge.source)});
        }
        if (edge.source == node_id) {
          outgoing.push_back(
              {edge.relation, include_condition ? edge.condition : "",
               colors.at(edge.target)});
        }
      }
      std::sort(incoming.begin(), incoming.end());
      std::sort(outgoing.begin(), outgoing.end());
      Json incoming_json = Json::array();
      Json outgoing_json = Json::array();
      for (const auto &item : incoming) {
        incoming_json.push_back(Json::array({item[0], item[1], item[2]}));
      }
      for (const auto &item : outgoing) {
        outgoing_json.push_back(Json::array({item[0], item[1], item[2]}));
      }
      updated.emplace(node_id,
                      contracts::sha256_json({{"incoming", incoming_json},
                                              {"intrinsic", payload},
                                              {"outgoing", outgoing_json}}));
    }
    if (updated == colors) {
      break;
    }
    colors = std::move(updated);
  }
  return colors;
}

[[nodiscard]] std::size_t factorial_bounded(std::size_t value,
                                            std::size_t current) {
  for (std::size_t factor = 2; factor <= value; ++factor) {
    if (current > max_reasoning_canonical_permutations / factor) {
      return max_reasoning_canonical_permutations + 1U;
    }
    current *= factor;
  }
  return current;
}

[[nodiscard]] std::size_t permutation_budget(
    const std::vector<std::vector<std::string>> &groups) {
  std::size_t result = 1U;
  for (const auto &group : groups) {
    result = factorial_bounded(group.size(), result);
    if (result > max_reasoning_canonical_permutations) {
      return result;
    }
  }
  return result;
}

struct Serialization final {
  std::vector<Json> nodes;
  std::vector<Json> edges;
  std::string text;
};

[[nodiscard]] Serialization serialize_order(
    const std::vector<std::string> &order,
    const std::map<std::string, ReasoningNodeSpec> &nodes,
    const std::vector<ReasoningEdgeSpec> &edges, bool topology_only) {
  std::map<std::string, std::size_t> position;
  std::vector<Json> canonical_nodes;
  canonical_nodes.reserve(order.size());
  for (std::size_t index = 0; index < order.size(); ++index) {
    position.emplace(order[index], index);
    canonical_nodes.push_back(topology_only
                                  ? topology_intrinsic(nodes.at(order[index]))
                                  : node_intrinsic(nodes.at(order[index])));
  }
  std::vector<Json> canonical_edges;
  canonical_edges.reserve(edges.size());
  for (const auto &edge : edges) {
    Json item = {{"relation", edge.relation},
                 {"source", position.at(edge.source)},
                 {"target", position.at(edge.target)}};
    if (!topology_only) {
      item["condition"] = edge.condition;
    }
    canonical_edges.push_back(std::move(item));
  }
  std::sort(canonical_edges.begin(), canonical_edges.end(),
            [topology_only](const Json &left, const Json &right) {
              const auto left_key = std::make_tuple(
                  left.at("source").get<std::size_t>(),
                  left.at("target").get<std::size_t>(),
                  left.at("relation").get<std::string>(),
                  topology_only ? std::string()
                                : left.at("condition").get<std::string>());
              const auto right_key = std::make_tuple(
                  right.at("source").get<std::size_t>(),
                  right.at("target").get<std::size_t>(),
                  right.at("relation").get<std::string>(),
                  topology_only ? std::string()
                                : right.at("condition").get<std::string>());
              return left_key < right_key;
            });
  const Json graph = {{"edges", canonical_edges},
                      {"nodes", canonical_nodes}};
  return {std::move(canonical_nodes), std::move(canonical_edges),
          contracts::canonical_json(graph)};
}

void enumerate_group_orders(
    const std::vector<std::vector<std::string>> &groups,
    std::size_t group_index, std::vector<std::string> &order,
    const std::function<void(const std::vector<std::string> &)> &callback) {
  if (group_index == groups.size()) {
    callback(order);
    return;
  }
  auto permutation = groups[group_index];
  std::sort(permutation.begin(), permutation.end());
  do {
    const std::size_t old_size = order.size();
    order.insert(order.end(), permutation.begin(), permutation.end());
    enumerate_group_orders(groups, group_index + 1U, order, callback);
    order.resize(old_size);
  } while (std::next_permutation(permutation.begin(), permutation.end()));
}

[[nodiscard]] std::vector<std::vector<std::string>> color_groups(
    const std::map<std::string, std::string> &colors) {
  std::map<std::string, std::vector<std::string>> grouped;
  for (const auto &[node_id, color] : colors) {
    grouped[color].push_back(node_id);
  }
  std::vector<std::vector<std::string>> result;
  for (auto &[color, group] : grouped) {
    static_cast<void>(color);
    result.push_back(std::move(group));
  }
  return result;
}

struct TopologyIdentity final {
  std::string signature;
  bool exact = false;
  std::size_t evaluated = 0;
};

[[nodiscard]] TopologyIdentity canonical_topology_signature(
    const ValidatedReasoning &validated, std::string_view termination_kind) {
  IntrinsicMap intrinsic;
  for (const auto &[node_id, node] : validated.nodes) {
    intrinsic.emplace(node_id, topology_intrinsic(node));
  }
  const auto colors = refined_colors(intrinsic, validated.edges, false);
  const auto groups = color_groups(colors);
  const auto budget = permutation_budget(groups);
  if (budget <= max_reasoning_canonical_permutations) {
    std::string best;
    std::size_t evaluated = 0;
    std::vector<std::string> order;
    enumerate_group_orders(groups, 0U, order,
                           [&](const std::vector<std::string> &candidate) {
      const auto serialized = serialize_order(candidate, validated.nodes,
                                              validated.edges, true);
      ++evaluated;
      if (best.empty() || serialized.text < best) {
        best = serialized.text;
      }
    });
    return {contracts::sha256_json(
                {{"graph", best},
                 {"termination_kind", std::string(termination_kind)},
                 {"version", reasoning_algebra_version}}),
            true, evaluated};
  }
  std::vector<std::string> order;
  for (const auto &[node_id, ignored] : validated.nodes) {
    static_cast<void>(ignored);
    order.push_back(node_id);
  }
  std::sort(order.begin(), order.end(), [&](const auto &left,
                                            const auto &right) {
    return std::make_tuple(colors.at(left),
                           contracts::canonical_json(intrinsic.at(left)), left) <
           std::make_tuple(colors.at(right),
                           contracts::canonical_json(intrinsic.at(right)),
                           right);
  });
  return {contracts::sha256_json(
              {{"graph",
                serialize_order(order, validated.nodes, validated.edges, true)
                    .text},
               {"source_node_ids", order},
               {"termination_kind", std::string(termination_kind)},
               {"version", reasoning_algebra_version}}),
          false, 0U};
}

} // namespace

CanonicalReasoningAlgorithm canonicalize_reasoning_algorithm(
    const ReasoningAlgorithmSpec &spec) {
  const auto validated = validate(spec);
  IntrinsicMap intrinsic;
  for (const auto &[node_id, node] : validated.nodes) {
    intrinsic.emplace(node_id, node_intrinsic(node));
  }
  const auto colors = refined_colors(intrinsic, validated.edges, true);
  const auto groups = color_groups(colors);
  const auto budget = permutation_budget(groups);
  std::size_t evaluated = 0U;
  std::vector<std::string> warnings;
  std::vector<Json> canonical_nodes;
  std::vector<Json> canonical_edges;
  std::vector<std::string> source_binding;
  bool semantic_exact = false;
  if (budget <= max_reasoning_canonical_permutations) {
    std::string best;
    std::vector<std::string> order;
    enumerate_group_orders(groups, 0U, order,
                           [&](const std::vector<std::string> &candidate) {
      auto serialized = serialize_order(candidate, validated.nodes,
                                        validated.edges, false);
      ++evaluated;
      if (best.empty() || serialized.text < best) {
        best = serialized.text;
        canonical_nodes = std::move(serialized.nodes);
        canonical_edges = std::move(serialized.edges);
      }
    });
    semantic_exact = true;
  } else {
    std::vector<std::string> order;
    for (const auto &[node_id, ignored] : validated.nodes) {
      static_cast<void>(ignored);
      order.push_back(node_id);
    }
    std::sort(order.begin(), order.end(), [&](const auto &left,
                                              const auto &right) {
      return std::make_tuple(colors.at(left),
                             contracts::canonical_json(intrinsic.at(left)),
                             left) <
             std::make_tuple(colors.at(right),
                             contracts::canonical_json(intrinsic.at(right)),
                             right);
    });
    auto serialized =
        serialize_order(order, validated.nodes, validated.edges, false);
    canonical_nodes = std::move(serialized.nodes);
    canonical_edges = std::move(serialized.edges);
    source_binding = order;
    warnings.emplace_back(
        "Reasoning graph symmetry exceeded the exact semantic permutation budget. Source node IDs remain conservatively bound to avoid false equivalence.");
  }

  const Json termination = {
      {"kind", normalized_text(spec.termination.kind)},
      {"max_steps", spec.termination.max_steps},
      {"predicate", normalized_text(spec.termination.predicate)}};
  const auto topology = canonical_topology_signature(
      validated, termination.at("kind").get_ref<const std::string &>());
  evaluated += topology.evaluated;
  if (!topology.exact) {
    warnings.emplace_back(
        "Reasoning operator topology exceeded the exact topology permutation budget. Topology identity is conservatively source-bound.");
  }
  const std::string strength = semantic_exact && topology.exact
                                   ? "EXACT_BOUNDED_GRAPH_CANONICALIZATION"
                                   : "CONSERVATIVE_RENAMING_BOUND";
  const auto input_semantics = normalized_texts(spec.inputs);
  const auto output_semantics = normalized_texts(spec.outputs);
  const auto invariants = normalized_texts(spec.invariants);
  const auto applicability = normalized_texts(spec.applicability);
  const Json semantic_payload = {
      {"applicability", applicability},
      {"edges", canonical_edges},
      {"inputs", input_semantics},
      {"invariants", invariants},
      {"nodes", canonical_nodes},
      {"outputs", output_semantics},
      {"source_binding", source_binding},
      {"termination", termination},
      {"version", reasoning_algebra_version}};
  const std::string semantic_signature =
      contracts::sha256_json(semantic_payload);
  const std::string canonical_signature = contracts::sha256_json(
      {{"canonicalization_strength", strength},
       {"semantic_signature", semantic_signature},
       {"topology_signature", topology.signature},
       {"version", reasoning_algebra_version}});
  return {.schema_version = 1,
          .reasoning_version = std::string(reasoning_algebra_version),
          .input_semantics = input_semantics,
          .output_semantics = output_semantics,
          .canonical_nodes = std::move(canonical_nodes),
          .canonical_edges = std::move(canonical_edges),
          .invariants = invariants,
          .termination = termination,
          .applicability = applicability,
          .topology_signature = topology.signature,
          .semantic_signature = semantic_signature,
          .canonical_reasoning_signature = canonical_signature,
          .canonicalization_strength = strength,
          .canonical_permutations_evaluated = evaluated,
          .public_artifact_only = true,
          .warnings = std::move(warnings)};
}

Json to_json(const ReasoningNodeSpec &value) {
  return {{"assumptions", value.assumptions},
          {"description", value.description},
          {"evidence_requirements", value.evidence_requirements},
          {"falsifiers", value.falsifiers},
          {"node_id", value.node_id},
          {"operator", value.operator_name},
          {"public_claim_ids", value.public_claim_ids},
          {"semantic_inputs", value.semantic_inputs},
          {"semantic_outputs", value.semantic_outputs}};
}

Json to_json(const ReasoningEdgeSpec &value) {
  return {{"condition", value.condition},
          {"relation", value.relation},
          {"source", value.source},
          {"target", value.target}};
}

Json to_json(const ReasoningTerminationSpec &value) {
  return {{"kind", value.kind},
          {"max_steps", value.max_steps},
          {"predicate", value.predicate}};
}

Json to_json(const ReasoningAlgorithmSpec &value) {
  Json nodes = Json::array();
  for (const auto &node : value.nodes) {
    nodes.push_back(to_json(node));
  }
  Json edges = Json::array();
  for (const auto &edge : value.edges) {
    edges.push_back(to_json(edge));
  }
  return {{"applicability", value.applicability},
          {"edges", edges},
          {"inputs", value.inputs},
          {"invariants", value.invariants},
          {"name", value.name},
          {"nodes", nodes},
          {"outputs", value.outputs},
          {"termination", to_json(value.termination)}};
}

Json to_json(const CanonicalReasoningAlgorithm &value) {
  return {{"applicability", value.applicability},
          {"canonical_edges", value.canonical_edges},
          {"canonical_nodes", value.canonical_nodes},
          {"canonical_permutations_evaluated",
           value.canonical_permutations_evaluated},
          {"canonical_reasoning_signature",
           value.canonical_reasoning_signature},
          {"canonicalization_strength", value.canonicalization_strength},
          {"input_semantics", value.input_semantics},
          {"invariants", value.invariants},
          {"output_semantics", value.output_semantics},
          {"public_artifact_only", value.public_artifact_only},
          {"reasoning_version", value.reasoning_version},
          {"schema_version", value.schema_version},
          {"semantic_signature", value.semantic_signature},
          {"termination", value.termination},
          {"topology_signature", value.topology_signature},
          {"warnings", value.warnings}};
}

} // namespace statewright::saa
