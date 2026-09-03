#include "statewright/reasoning/topology.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/reasoning/evaluation.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace statewright::reasoning {
namespace {

constexpr std::array<std::string_view, 14> node_kinds{
    "question",    "hypothesis", "premise",     "assumption",
    "claim",       "observation", "evidence",   "inference",
    "prediction",  "experiment", "counterexample", "constraint",
    "conclusion",  "decision"};

constexpr std::array<std::string_view, 13> edge_relations{
    "supports", "contradicts", "requires", "entails", "predicts",
    "tests",    "falsifies",   "qualifies", "explains", "causes",
    "depends_on", "undercuts", "rebuts"};

constexpr std::array<std::string_view, 11> inference_modes{
    "unspecified", "deductive", "inductive", "abductive", "causal",
    "analogical", "probabilistic", "authority", "defeasible",
    "constraint", "computational"};

constexpr std::array<std::string_view, 8> positive_relations{
    "supports", "requires", "entails", "predicts", "tests", "explains",
    "causes", "depends_on"};

constexpr std::array<std::string_view, 4> attack_relations{
    "contradicts", "falsifies", "undercuts", "rebuts"};

constexpr std::array<std::string_view, 5> branch_relations{
    "entails", "predicts", "tests", "causes", "depends_on"};

constexpr std::array<std::string_view, 3> grounding_kinds{
    "evidence", "observation", "assumption"};

constexpr std::array<std::string_view, 4> contribution_kinds{
    "hypothesis", "conclusion", "counterexample", "decision"};

[[noreturn]] void policy_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied, std::move(message));
}

template <std::size_t Size>
[[nodiscard]] bool contains(
    const std::array<std::string_view, Size> &values,
    std::string_view candidate) noexcept {
  return std::ranges::find(values, candidate) != values.end();
}

[[nodiscard]] std::string trim(std::string value) {
  const auto first = std::ranges::find_if_not(
      value, [](unsigned char character) { return std::isspace(character) != 0; });
  const auto last = std::find_if_not(
      value.rbegin(), value.rend(),
      [](unsigned char character) { return std::isspace(character) != 0; })
                        .base();
  if (first >= last) {
    return {};
  }
  return std::string(first, last);
}

[[nodiscard]] std::vector<std::string>
canonical_strings(const std::vector<std::string> &values) {
  std::set<std::string> unique;
  for (const auto &value : values) {
    if (!value.empty()) {
      unique.insert(value);
    }
  }
  return {unique.begin(), unique.end()};
}

void validate_node(ReasoningNode &node) {
  if (node.node_id.empty()) {
    policy_error("reasoning node ID must be non-empty");
  }
  if (!contains(node_kinds, node.kind)) {
    policy_error("invalid reasoning node kind: " + node.kind);
  }
  if (trim(node.content).empty()) {
    policy_error("reasoning node content must be non-empty");
  }
  if (node.confidence_bp < 0 || node.confidence_bp > 10'000) {
    policy_error("reasoning node confidence must be 0..10000");
  }
  if (node.validated && node.kind != "premise") {
    policy_error("only premise nodes may be marked validated");
  }
  if (node.material && node.kind != "conclusion" && node.kind != "decision") {
    policy_error("only conclusion or decision nodes may be material");
  }
  node.evidence_ids = canonical_strings(node.evidence_ids);
}

void validate_edge_shape(ReasoningEdge &edge) {
  if (edge.edge_id.empty() || edge.source_id.empty() || edge.target_id.empty()) {
    policy_error("reasoning edge IDs must be non-empty");
  }
  if (!contains(edge_relations, edge.relation)) {
    policy_error("invalid reasoning edge relation: " + edge.relation);
  }
  edge.inference_mode = canonical_inference_mode(edge.inference_mode, true);
}

[[nodiscard]] std::vector<ReasoningNode>
sorted_nodes(const std::vector<ReasoningNode> &nodes) {
  auto sorted = nodes;
  std::ranges::sort(sorted, {}, &ReasoningNode::node_id);
  return sorted;
}

[[nodiscard]] std::vector<ReasoningEdge>
sorted_edges(const std::vector<ReasoningEdge> &edges) {
  auto sorted = edges;
  std::ranges::sort(sorted, {}, &ReasoningEdge::edge_id);
  return sorted;
}

using Adjacency = std::map<std::string, std::vector<std::string>>;

[[nodiscard]] std::set<std::string> positive_grounding_kinds(
    const std::string &node_id, const std::map<std::string, ReasoningNode> &nodes,
    const Adjacency &incoming,
    std::map<std::string, std::set<std::string>> &memo) {
  if (const auto found = memo.find(node_id); found != memo.end()) {
    return found->second;
  }
  const auto &node = nodes.at(node_id);
  std::set<std::string> roots;
  if (contains(grounding_kinds, node.kind)) {
    roots.insert(node.kind);
  }
  if (node.kind == "premise" && node.validated) {
    roots.insert("validated_premise");
  }
  for (const auto &source_id : incoming.at(node_id)) {
    const auto source_roots =
        positive_grounding_kinds(source_id, nodes, incoming, memo);
    roots.insert(source_roots.begin(), source_roots.end());
  }
  memo.emplace(node_id, roots);
  return roots;
}

void add_node(std::map<std::string, ReasoningNode> &nodes,
              ReasoningNode node) {
  const auto found = nodes.find(node.node_id);
  if (found != nodes.end() && to_json(found->second) != to_json(node)) {
    policy_error("reasoning node identity collision: " + node.node_id);
  }
  nodes[node.node_id] = std::move(node);
}

void add_edge(std::map<std::string, ReasoningEdge> &edges,
              ReasoningEdge edge) {
  const auto found = edges.find(edge.edge_id);
  if (found != edges.end() && to_json(found->second) != to_json(edge)) {
    policy_error("reasoning edge identity collision: " + edge.edge_id);
  }
  edges[edge.edge_id] = std::move(edge);
}

[[nodiscard]] std::string assumption_node_id(std::string_view owner_id,
                                             std::string_view content) {
  return "assumption:" + contracts::sha256_json(
                             {{"owner_id", owner_id}, {"content", content}});
}

[[nodiscard]] std::string add_evidence_node(
    std::map<std::string, ReasoningNode> &nodes,
    const std::string &evidence_id) {
  const std::string node_id = "evidence:" + evidence_id;
  ReasoningNodeOptions options;
  options.evidence_ids = {evidence_id};
  add_node(nodes, make_reasoning_node(
                      node_id, "evidence",
                      "Evidence artifact " + evidence_id, std::move(options)));
  return node_id;
}

void mark_hypothetical_material_nodes(
    std::map<std::string, ReasoningNode> &nodes,
    const std::map<std::string, ReasoningEdge> &edges) {
  Adjacency incoming;
  for (const auto &[node_id, node] : nodes) {
    static_cast<void>(node);
    incoming[node_id] = {};
  }
  for (const auto &[edge_id, edge] : edges) {
    static_cast<void>(edge_id);
    if (contains(positive_relations, edge.relation)) {
      incoming.at(edge.target_id).push_back(edge.source_id);
    }
  }
  std::map<std::string, std::set<std::string>> memo;
  for (auto &[node_id, node] : nodes) {
    if (!node.material) {
      continue;
    }
    const auto roots = positive_grounding_kinds(node_id, nodes, incoming, memo);
    if (roots == std::set<std::string>{"assumption"} && !node.hypothetical) {
      node.hypothetical = true;
      auto material = to_json(node);
      material.erase("signature");
      node.signature = contracts::sha256_json(material);
    }
  }
}

} // namespace

contracts::Json to_json(const ReasoningNode &node) {
  return {{"node_id", node.node_id},
          {"kind", node.kind},
          {"content", node.content},
          {"evidence_ids", node.evidence_ids},
          {"confidence_bp", node.confidence_bp},
          {"path_id", node.path_id},
          {"validated", node.validated},
          {"hypothetical", node.hypothetical},
          {"material", node.material},
          {"signature", node.signature}};
}

contracts::Json to_json(const ReasoningEdge &edge) {
  return {{"edge_id", edge.edge_id},
          {"source_id", edge.source_id},
          {"target_id", edge.target_id},
          {"relation", edge.relation},
          {"inference_id", edge.inference_id},
          {"inference_mode", edge.inference_mode},
          {"signature", edge.signature}};
}

contracts::Json to_json(const ReasoningTopology &topology) {
  contracts::Json nodes = contracts::Json::array();
  for (const auto &node : sorted_nodes(topology.nodes)) {
    nodes.push_back(to_json(node));
  }
  contracts::Json edges = contracts::Json::array();
  for (const auto &edge : sorted_edges(topology.edges)) {
    edges.push_back(to_json(edge));
  }
  return {{"schema_version", topology.schema_version},
          {"problem_id", topology.problem_id},
          {"nodes", std::move(nodes)},
          {"edges", std::move(edges)},
          {"signature", topology.signature}};
}

std::string canonical_inference_mode(std::string_view value,
                                     bool allow_unspecified) {
  std::string normalized = trim(std::string(value));
  std::ranges::transform(normalized, normalized.begin(), [](unsigned char character) {
    if (character == '-' || character == ' ') {
      return '_';
    }
    return static_cast<char>(std::tolower(character));
  });
  if (normalized == "empirical") {
    normalized = "inductive";
  } else if (normalized == "formal") {
    normalized = "deductive";
  } else if (normalized == "mathematical") {
    normalized = "computational";
  } else if (normalized == "mechanistic") {
    normalized = "causal";
  }
  if (!contains(inference_modes, normalized)) {
    policy_error("invalid reasoning inference mode: " + std::string(value));
  }
  if (normalized == "unspecified" && !allow_unspecified) {
    policy_error("reasoning inference mode must be explicit");
  }
  return normalized;
}

ReasoningNode make_reasoning_node(std::string node_id, std::string kind,
                                  std::string content,
                                  ReasoningNodeOptions options) {
  ReasoningNode node{.node_id = std::move(node_id),
                     .kind = std::move(kind),
                     .content = std::move(content),
                     .evidence_ids = std::move(options.evidence_ids),
                     .confidence_bp = options.confidence_bp,
                     .path_id = std::move(options.path_id),
                     .validated = options.validated,
                     .hypothetical = options.hypothetical,
                     .material = options.material,
                     .signature = {}};
  auto signature_material = to_json(node);
  signature_material.erase("signature");
  node.signature = contracts::sha256_json(signature_material);
  validate_node(node);
  return node;
}

std::string inference_identity(std::string_view source_id,
                               std::string_view target_id,
                               std::string_view relation,
                               std::string_view inference_mode) {
  const auto material = contracts::Json{
      {"source_id", source_id},
      {"target_id", target_id},
      {"relation", relation},
      {"inference_mode", canonical_inference_mode(inference_mode)}};
  return "inference:" + contracts::sha256_json(material);
}

ReasoningEdge make_reasoning_edge(std::string source_id, std::string target_id,
                                  std::string relation,
                                  std::string inference_mode) {
  const std::string mode = canonical_inference_mode(inference_mode);
  const std::string identity =
      inference_identity(source_id, target_id, relation, mode);
  const auto payload = contracts::Json{{"source_id", source_id},
                                       {"target_id", target_id},
                                       {"relation", relation},
                                       {"inference_id", identity},
                                       {"inference_mode", mode}};
  const std::string edge_id = "edge:" + contracts::sha256_json(payload);
  ReasoningEdge edge{.edge_id = edge_id,
                     .source_id = std::move(source_id),
                     .target_id = std::move(target_id),
                     .relation = std::move(relation),
                     .inference_id = identity,
                     .inference_mode = mode,
                     .signature = {}};
  auto signature_material = payload;
  signature_material["edge_id"] = edge_id;
  edge.signature = contracts::sha256_json(signature_material);
  validate_edge_shape(edge);
  return edge;
}

contracts::Json reasoning_topology_payload(const ReasoningTopology &topology) {
  contracts::Json nodes = contracts::Json::array();
  for (const auto &node : sorted_nodes(topology.nodes)) {
    nodes.push_back(to_json(node));
  }
  contracts::Json edges = contracts::Json::array();
  for (const auto &edge : sorted_edges(topology.edges)) {
    edges.push_back(to_json(edge));
  }
  return {{"problem_id", topology.problem_id},
          {"nodes", std::move(nodes)},
          {"edges", std::move(edges)}};
}

ReasoningTopology make_reasoning_topology(std::string problem_id,
                                          std::vector<ReasoningNode> nodes,
                                          std::vector<ReasoningEdge> edges,
                                          int schema_version) {
  ReasoningTopology topology{.schema_version = schema_version,
                             .problem_id = std::move(problem_id),
                             .nodes = sorted_nodes(nodes),
                             .edges = sorted_edges(edges),
                             .signature = {}};
  topology.signature =
      contracts::sha256_json(reasoning_topology_payload(topology));
  return topology;
}

ReasoningTopology build_reasoning_topology(
    const ReasoningProblem &problem,
    const std::vector<Hypothesis> &hypotheses,
    const CandidateSet &candidates) {
  std::map<std::string, ReasoningNode> nodes;
  std::map<std::string, ReasoningEdge> edges;
  const std::string question_id = "question:" + problem.problem_id;
  const std::string premise_id = "premise:" + problem.problem_id;
  add_node(nodes,
           make_reasoning_node(question_id, "question", problem.goal));
  ReasoningNodeOptions premise_options;
  premise_options.validated = true;
  add_node(nodes, make_reasoning_node(premise_id, "premise",
                                      problem.statement,
                                      std::move(premise_options)));
  add_edge(edges, make_reasoning_edge(premise_id, question_id, "requires",
                                      "constraint"));

  std::map<std::string, Hypothesis> hypothesis_by_id;
  for (const auto &hypothesis : hypotheses) {
    hypothesis_by_id.emplace(hypothesis.hypothesis_id, hypothesis);
  }
  for (const auto &[hypothesis_id, hypothesis] : hypothesis_by_id) {
    const std::string node_id = "hypothesis:" + hypothesis_id;
    ReasoningNodeOptions options;
    options.evidence_ids = hypothesis.supporting_evidence;
    options.evidence_ids.insert(options.evidence_ids.end(),
                                hypothesis.conflicting_evidence.begin(),
                                hypothesis.conflicting_evidence.end());
    options.confidence_bp = hypothesis.posterior_bp;
    options.hypothetical = true;
    add_node(nodes, make_reasoning_node(node_id, "hypothesis",
                                        hypothesis.proposition,
                                        std::move(options)));
    add_edge(edges, make_reasoning_edge(node_id, question_id, "explains",
                                        "abductive"));
    for (const auto &evidence_id : hypothesis.supporting_evidence) {
      add_edge(edges,
               make_reasoning_edge(add_evidence_node(nodes, evidence_id),
                                   node_id, "supports", "inductive"));
    }
    for (const auto &evidence_id : hypothesis.conflicting_evidence) {
      add_edge(edges,
               make_reasoning_edge(add_evidence_node(nodes, evidence_id),
                                   node_id, "contradicts", "defeasible"));
    }
    for (const auto &assumption : hypothesis.assumptions) {
      const std::string assumption_id =
          assumption_node_id(node_id, assumption);
      add_node(nodes, make_reasoning_node(assumption_id, "assumption",
                                          assumption));
      add_edge(edges, make_reasoning_edge(assumption_id, node_id, "requires",
                                          "constraint"));
    }
    for (const auto &prediction : hypothesis.predictions) {
      const std::string prediction_id =
          "prediction:" + contracts::sha256_json(
                              {{"hypothesis_id", hypothesis_id},
                               {"prediction", prediction}});
      add_node(nodes,
               make_reasoning_node(prediction_id, "prediction", prediction));
      add_edge(edges, make_reasoning_edge(node_id, prediction_id, "predicts",
                                          "probabilistic"));
    }
  }

  std::map<std::string, VerifierReport> verifier_by_path;
  for (const auto &report : candidates.verifier_reports) {
    verifier_by_path.emplace(report.path_id, report);
  }
  std::map<std::string, ReasoningPath> path_by_id;
  for (const auto &path : candidates.paths) {
    path_by_id.emplace(path.path_id, path);
  }
  const std::set<std::string> admitted(candidates.surviving_path_ids.begin(),
                                       candidates.surviving_path_ids.end());
  std::map<std::pair<std::string, std::string>, std::string> step_node_ids;
  for (const auto &path : candidates.paths) {
    if (!admitted.contains(path.path_id)) {
      continue;
    }
    for (const auto &step : path.steps) {
      const std::string node_id =
          "step:" + path.path_id + ":" + step.step_id;
      step_node_ids[{path.path_id, step.step_id}] = node_id;
      ReasoningNodeOptions options;
      options.evidence_ids = step.evidence_ids;
      options.confidence_bp = step.confidence_bp;
      options.path_id = path.path_id;
      options.hypothetical = !step.assumptions.empty();
      add_node(nodes, make_reasoning_node(node_id, "claim", step.claim,
                                          std::move(options)));
      for (const auto &premise : step.premises) {
        std::string source_id;
        std::string relation;
        if (premise == "problem") {
          source_id = premise_id;
          relation = "requires";
        } else if (hypothesis_by_id.contains(premise)) {
          source_id = "hypothesis:" + premise;
          relation = "supports";
        } else if (const auto found =
                       step_node_ids.find({path.path_id, premise});
                   found != step_node_ids.end()) {
          source_id = found->second;
          relation = "entails";
        }
        if (!source_id.empty()) {
          add_edge(edges, make_reasoning_edge(source_id, node_id, relation,
                                              step.inference));
        }
      }
      for (const auto &evidence_id : step.evidence_ids) {
        add_edge(edges,
                 make_reasoning_edge(add_evidence_node(nodes, evidence_id),
                                     node_id, "supports", step.inference));
      }
      for (const auto &assumption : step.assumptions) {
        const std::string assumption_id =
            assumption_node_id(node_id, assumption);
        add_node(nodes, make_reasoning_node(assumption_id, "assumption",
                                            assumption));
        add_edge(edges, make_reasoning_edge(assumption_id, node_id, "requires",
                                            step.inference));
      }
    }
    const std::string conclusion_id = "conclusion:" + path.path_id;
    const auto report = verifier_by_path.find(path.path_id);
    ReasoningNodeOptions options;
    options.confidence_bp = path.provider_confidence_bp;
    options.path_id = path.path_id;
    options.material =
        (report != verifier_by_path.end() && report->second.verdict != "REJECT") ||
        path.path_id == candidates.selected_path_id;
    add_node(nodes, make_reasoning_node(conclusion_id, "conclusion",
                                        path.conclusion, std::move(options)));
    const auto &final_step = path.steps.back();
    add_edge(edges,
             make_reasoning_edge(
                 step_node_ids.at({path.path_id, final_step.step_id}),
                 conclusion_id, "entails", final_step.inference));
  }

  for (const auto &report : candidates.verifier_reports) {
    if (!admitted.contains(report.path_id)) {
      continue;
    }
    const auto &path = path_by_id.at(report.path_id);
    const std::string target =
        step_node_ids.at({path.path_id, path.steps.back().step_id});
    std::size_t index = 0;
    for (const auto &contradiction : report.contradictions) {
      ++index;
      std::ostringstream identity;
      identity << "counterexample:" << report.report_id << ':' << std::setw(2)
               << std::setfill('0') << index;
      ReasoningNodeOptions options;
      options.path_id = report.path_id;
      add_node(nodes, make_reasoning_node(identity.str(), "counterexample",
                                          contradiction,
                                          std::move(options)));
      add_edge(edges, make_reasoning_edge(identity.str(), target, "rebuts",
                                          "defeasible"));
    }
  }
  for (const auto &report : candidates.falsifier_reports) {
    if (!admitted.contains(report.path_id)) {
      continue;
    }
    const auto &path = path_by_id.at(report.path_id);
    std::size_t index = 0;
    for (const auto &counterexample : report.counterexamples) {
      ++index;
      std::ostringstream identity;
      identity << "counterexample:" << report.report_id << ':' << std::setw(2)
               << std::setfill('0') << index;
      ReasoningNodeOptions options;
      options.path_id = report.path_id;
      add_node(nodes, make_reasoning_node(identity.str(), "counterexample",
                                          counterexample,
                                          std::move(options)));
      std::vector<std::string> targets;
      if (!report.contradicted_step_ids.empty()) {
        for (const auto &step_id : report.contradicted_step_ids) {
          targets.push_back(step_node_ids.at({report.path_id, step_id}));
        }
      } else if (!path.hypothesis_ids.empty()) {
        for (const auto &hypothesis_id : path.hypothesis_ids) {
          targets.push_back("hypothesis:" + hypothesis_id);
        }
      } else {
        targets.push_back(
            step_node_ids.at({path.path_id, path.steps.back().step_id}));
      }
      for (const auto &target : targets) {
        add_edge(edges, make_reasoning_edge(identity.str(), target, "falsifies",
                                            "defeasible"));
      }
    }
    index = 0;
    for (const auto &condition : report.unresolved_defeat_conditions) {
      ++index;
      std::ostringstream identity;
      identity << "constraint:" << report.report_id << ':' << std::setw(2)
               << std::setfill('0') << index;
      ReasoningNodeOptions options;
      options.path_id = report.path_id;
      add_node(nodes, make_reasoning_node(identity.str(), "constraint",
                                          condition, std::move(options)));
      add_edge(edges,
               make_reasoning_edge(identity.str(),
                                   "conclusion:" + report.path_id, "qualifies",
                                   "defeasible"));
    }
  }
  if (!candidates.selected_path_id.empty()) {
    const auto &selected = path_by_id.at(candidates.selected_path_id);
    const std::string decision_id = "decision:" + problem.problem_id;
    ReasoningNodeOptions options;
    options.path_id = selected.path_id;
    options.material = true;
    add_node(nodes, make_reasoning_node(
                        decision_id, "decision",
                        candidates.synthesized_conclusion.empty()
                            ? selected.conclusion
                            : candidates.synthesized_conclusion,
                        std::move(options)));
    add_edge(edges,
             make_reasoning_edge("conclusion:" + selected.path_id, decision_id,
                                 "supports", "constraint"));
  }
  mark_hypothetical_material_nodes(nodes, edges);

  std::vector<ReasoningNode> node_values;
  std::vector<ReasoningEdge> edge_values;
  for (const auto &[identity, node] : nodes) {
    static_cast<void>(identity);
    node_values.push_back(node);
  }
  for (const auto &[identity, edge] : edges) {
    static_cast<void>(identity);
    edge_values.push_back(edge);
  }
  return make_reasoning_topology(problem.problem_id, std::move(node_values),
                                 std::move(edge_values), 2);
}

void validate_reasoning_topology(
    const ReasoningTopology &topology, const TopologyBudget &budget,
    const std::vector<std::string> &declared_evidence_ids) {
  if (budget.max_topology_nodes < 1 || budget.max_topology_edges < 1 ||
      budget.max_branch_factor < 1) {
    policy_error("reasoning budget limits must be positive");
  }
  if (topology.nodes.size() > budget.max_topology_nodes) {
    policy_error("reasoning topology node budget exceeded");
  }
  if (topology.edges.size() > budget.max_topology_edges) {
    policy_error("reasoning topology edge budget exceeded");
  }

  std::map<std::string, ReasoningNode> nodes;
  for (auto node : topology.nodes) {
    validate_node(node);
    if (!nodes.emplace(node.node_id, std::move(node)).second) {
      policy_error("reasoning topology contains duplicate node IDs");
    }
  }
  std::set<std::string> edge_ids;
  for (auto edge : topology.edges) {
    validate_edge_shape(edge);
    if (!edge_ids.insert(edge.edge_id).second) {
      policy_error("reasoning topology contains duplicate edge IDs");
    }
  }

  const auto canonical_evidence = canonical_strings(declared_evidence_ids);
  const std::set<std::string> declared(canonical_evidence.begin(),
                                       canonical_evidence.end());
  Adjacency positive_adjacency;
  Adjacency positive_incoming;
  std::map<std::string, std::set<std::string>> undirected;
  std::map<std::string, std::size_t> branch_counts;
  for (const auto &[node_id, node] : nodes) {
    static_cast<void>(node);
    positive_adjacency[node_id] = {};
    positive_incoming[node_id] = {};
    undirected[node_id] = {};
    branch_counts[node_id] = 0;
  }

  for (const auto relation : positive_relations) {
    if (contains(attack_relations, relation)) {
      policy_error("attack relations overlap positive reasoning relations");
    }
  }
  for (const auto &edge_value : topology.edges) {
    auto edge = edge_value;
    validate_edge_shape(edge);
    if (!nodes.contains(edge.source_id) || !nodes.contains(edge.target_id)) {
      policy_error("reasoning topology edge references an unknown node");
    }
    if (topology.schema_version >= 2) {
      if (edge.inference_mode == "unspecified" || edge.inference_id.empty()) {
        policy_error(
            "reasoning topology edge lacks explicit inference metadata");
      }
      const std::string expected_identity = inference_identity(
          edge.source_id, edge.target_id, edge.relation, edge.inference_mode);
      if (edge.inference_id != expected_identity) {
        policy_error("reasoning topology inference identity mismatch");
      }
      const auto expected = make_reasoning_edge(
          edge.source_id, edge.target_id, edge.relation, edge.inference_mode);
      if (edge.edge_id != expected.edge_id || edge.signature != expected.signature) {
        policy_error("reasoning topology edge content address mismatch");
      }
    }
    undirected[edge.source_id].insert(edge.target_id);
    undirected[edge.target_id].insert(edge.source_id);
    if (contains(positive_relations, edge.relation)) {
      positive_adjacency[edge.source_id].push_back(edge.target_id);
      positive_incoming[edge.target_id].push_back(edge.source_id);
    }
    if (contains(branch_relations, edge.relation)) {
      ++branch_counts[edge.source_id];
    }
  }
  if (std::ranges::any_of(branch_counts, [&](const auto &entry) {
        return entry.second > budget.max_branch_factor;
      })) {
    policy_error("reasoning topology branch factor exceeded");
  }

  for (const auto &[node_id, node] : nodes) {
    static_cast<void>(node_id);
    for (const auto &evidence_id : node.evidence_ids) {
      if (!declared.contains(evidence_id)) {
        policy_error("reasoning topology references undeclared evidence");
      }
    }
    if (node.kind == "evidence") {
      if (node.evidence_ids.size() != 1) {
        policy_error("reasoning evidence node must bind exactly one artifact");
      }
      const auto &evidence_id = node.evidence_ids.front();
      if (!declared.contains(evidence_id) ||
          node.node_id != "evidence:" + evidence_id) {
        policy_error(
            "reasoning evidence node lies outside the finite universe");
      }
    }
    if (node.kind == "decision") {
      if (node.path_id.empty()) {
        policy_error("reasoning decision lacks a candidate path");
      }
      const std::string conclusion_id = "conclusion:" + node.path_id;
      const bool traceable = nodes.contains(conclusion_id) &&
                             std::ranges::any_of(
                                 topology.edges, [&](const ReasoningEdge &edge) {
                                   return edge.source_id == conclusion_id &&
                                          edge.target_id == node.node_id &&
                                          edge.relation == "supports";
                                 });
      if (!traceable) {
        policy_error(
            "reasoning decision lacks a traceable candidate conclusion");
      }
    }
    if (node.kind == "counterexample") {
      bool has_attack = false;
      bool valid_targets = true;
      for (const auto &edge : topology.edges) {
        if (edge.source_id == node.node_id &&
            contains(attack_relations, edge.relation)) {
          has_attack = true;
          const auto &target_kind = nodes.at(edge.target_id).kind;
          if (target_kind != "hypothesis" && target_kind != "claim") {
            valid_targets = false;
          }
        }
      }
      if (!has_attack || !valid_targets) {
        policy_error(
            "counterexample is not bound to a hypothesis or reasoning step");
      }
    }
  }

  std::map<std::string, int> state;
  std::function<void(const std::string &)> visit = [&](const std::string &node_id) {
    const int marker = state[node_id];
    if (marker == 1) {
      policy_error("positive reasoning topology contains a cycle");
    }
    if (marker == 2) {
      return;
    }
    state[node_id] = 1;
    for (const auto &target_id : positive_adjacency.at(node_id)) {
      visit(target_id);
    }
    state[node_id] = 2;
  };
  for (const auto &[node_id, node] : nodes) {
    static_cast<void>(node);
    visit(node_id);
  }

  std::map<std::string, std::set<std::string>> grounding_memo;
  for (const auto &[node_id, node] : nodes) {
    if (!node.material) {
      continue;
    }
    const auto roots = positive_grounding_kinds(
        node_id, nodes, positive_incoming, grounding_memo);
    if (roots.empty()) {
      policy_error("material reasoning conclusion lacks a grounding trace");
    }
    if (roots == std::set<std::string>{"assumption"} && !node.hypothetical) {
      policy_error("assumption-only conclusion must remain hypothetical");
    }
  }

  std::set<std::string> anchors;
  for (const auto &[node_id, node] : nodes) {
    if (contains(contribution_kinds, node.kind)) {
      anchors.insert(node_id);
    }
  }
  for (const auto &[start_id, node] : nodes) {
    static_cast<void>(node);
    if (anchors.contains(start_id)) {
      continue;
    }
    std::set<std::string> seen{start_id};
    std::vector<std::string> frontier{start_id};
    bool contributes = false;
    while (!frontier.empty()) {
      const std::string current = std::move(frontier.back());
      frontier.pop_back();
      if (anchors.contains(current)) {
        contributes = true;
        break;
      }
      for (const auto &adjacent : undirected.at(current)) {
        if (seen.insert(adjacent).second) {
          frontier.push_back(adjacent);
        }
      }
    }
    if (!contributes) {
      policy_error("reasoning topology contains an unconnected branch");
    }
  }
}

} // namespace statewright::reasoning
