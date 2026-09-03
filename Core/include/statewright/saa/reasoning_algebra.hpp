#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view reasoning_algebra_version =
    "saa-reasoning-algebra-v1";
inline constexpr std::size_t max_reasoning_nodes = 32U;
inline constexpr std::size_t max_reasoning_edges = 128U;
inline constexpr std::size_t max_reasoning_canonical_permutations = 4096U;
inline constexpr int max_reasoning_steps = 1024;

struct ReasoningNodeSpec final {
  std::string node_id;
  std::string operator_name;
  std::vector<std::string> semantic_inputs;
  std::vector<std::string> semantic_outputs;
  std::vector<std::string> public_claim_ids;
  std::vector<std::string> evidence_requirements;
  std::vector<std::string> assumptions;
  std::vector<std::string> falsifiers;
  std::string description;
};

struct ReasoningEdgeSpec final {
  std::string source;
  std::string target;
  std::string relation = "NEXT";
  std::string condition;
};

struct ReasoningTerminationSpec final {
  std::string kind;
  std::string predicate;
  int max_steps = 0;
};

struct ReasoningAlgorithmSpec final {
  std::string name;
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;
  std::vector<ReasoningNodeSpec> nodes;
  std::vector<ReasoningEdgeSpec> edges;
  std::vector<std::string> invariants;
  ReasoningTerminationSpec termination;
  std::vector<std::string> applicability;
};

struct CanonicalReasoningAlgorithm final {
  int schema_version = 1;
  std::string reasoning_version = std::string(reasoning_algebra_version);
  std::vector<std::string> input_semantics;
  std::vector<std::string> output_semantics;
  std::vector<contracts::Json> canonical_nodes;
  std::vector<contracts::Json> canonical_edges;
  std::vector<std::string> invariants;
  contracts::Json termination = contracts::Json::object();
  std::vector<std::string> applicability;
  std::string topology_signature;
  std::string semantic_signature;
  std::string canonical_reasoning_signature;
  std::string canonicalization_strength;
  std::size_t canonical_permutations_evaluated = 0;
  bool public_artifact_only = true;
  std::vector<std::string> warnings;
};

[[nodiscard]] CanonicalReasoningAlgorithm
canonicalize_reasoning_algorithm(const ReasoningAlgorithmSpec &spec);

[[nodiscard]] contracts::Json to_json(const ReasoningNodeSpec &value);
[[nodiscard]] contracts::Json to_json(const ReasoningEdgeSpec &value);
[[nodiscard]] contracts::Json to_json(const ReasoningTerminationSpec &value);
[[nodiscard]] contracts::Json to_json(const ReasoningAlgorithmSpec &value);
[[nodiscard]] contracts::Json
to_json(const CanonicalReasoningAlgorithm &value);

} // namespace statewright::saa
