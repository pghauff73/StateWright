#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::reasoning {

struct CandidateSet;
struct Hypothesis;
struct ReasoningProblem;

struct ReasoningNode final {
  std::string node_id;
  std::string kind;
  std::string content;
  std::vector<std::string> evidence_ids;
  int confidence_bp = 0;
  std::string path_id;
  bool validated = false;
  bool hypothetical = false;
  bool material = false;
  std::string signature;
};

struct ReasoningNodeOptions final {
  std::vector<std::string> evidence_ids;
  int confidence_bp = 0;
  std::string path_id;
  bool validated = false;
  bool hypothetical = false;
  bool material = false;
};

struct ReasoningEdge final {
  std::string edge_id;
  std::string source_id;
  std::string target_id;
  std::string relation;
  std::string inference_id;
  std::string inference_mode = "unspecified";
  std::string signature;
};

struct ReasoningTopology final {
  int schema_version = 2;
  std::string problem_id;
  std::vector<ReasoningNode> nodes;
  std::vector<ReasoningEdge> edges;
  std::string signature;
};

struct TopologyBudget final {
  std::size_t max_topology_nodes = 256;
  std::size_t max_topology_edges = 512;
  std::size_t max_branch_factor = 16;
};

[[nodiscard]] contracts::Json to_json(const ReasoningNode &node);
[[nodiscard]] contracts::Json to_json(const ReasoningEdge &edge);
[[nodiscard]] contracts::Json to_json(const ReasoningTopology &topology);

[[nodiscard]] std::string canonical_inference_mode(
    std::string_view value, bool allow_unspecified = false);

[[nodiscard]] ReasoningNode make_reasoning_node(
    std::string node_id, std::string kind, std::string content,
    ReasoningNodeOptions options = {});

[[nodiscard]] std::string inference_identity(
    std::string_view source_id, std::string_view target_id,
    std::string_view relation, std::string_view inference_mode);

[[nodiscard]] ReasoningEdge make_reasoning_edge(
    std::string source_id, std::string target_id, std::string relation,
    std::string inference_mode);

[[nodiscard]] contracts::Json
reasoning_topology_payload(const ReasoningTopology &topology);

[[nodiscard]] ReasoningTopology make_reasoning_topology(
    std::string problem_id, std::vector<ReasoningNode> nodes,
    std::vector<ReasoningEdge> edges, int schema_version = 2);

[[nodiscard]] ReasoningTopology build_reasoning_topology(
    const ReasoningProblem &problem,
    const std::vector<Hypothesis> &hypotheses,
    const CandidateSet &candidates);

void validate_reasoning_topology(
    const ReasoningTopology &topology, const TopologyBudget &budget,
    const std::vector<std::string> &declared_evidence_ids);

} // namespace statewright::reasoning
