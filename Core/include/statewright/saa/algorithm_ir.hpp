#pragma once

#include "statewright/contracts/canonical_json.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view canonicalizer_version =
    "saa-structural-ir-v1";

struct PrimitiveSpec final {
  std::string name;
  std::string category;
  bool commutative = false;
  bool associative = false;
};

struct OperandRef final {
  std::string kind;
  int position = -1;
  std::string node_id;
  int output_index = 0;
  contracts::Json value = nullptr;
};

struct PortSpec final {
  std::string role;
  int position = 0;
  std::string name;
  std::string data_type = "scalar";
  std::vector<int> shape;
  std::optional<OperandRef> source;
};

struct StateSpec final {
  int position = 0;
  std::string name;
  std::string data_type = "scalar";
  std::vector<int> shape;
  std::optional<OperandRef> initial;
  std::optional<OperandRef> update;
};

struct AlgorithmNodeSpec final {
  std::string node_id;
  std::string primitive;
  std::vector<OperandRef> operands;
  std::map<std::string, contracts::Json> attributes;
  int result_count = 1;
};

struct ControlEdgeSpec final {
  std::string source;
  std::string target;
  std::string kind = "next";
  std::string label;
};

struct AlgorithmStructureSpec final {
  std::string name;
  std::vector<PortSpec> inputs;
  std::vector<PortSpec> outputs;
  std::vector<PortSpec> parameters;
  std::vector<StateSpec> states;
  std::vector<AlgorithmNodeSpec> nodes;
  std::vector<ControlEdgeSpec> control_edges;
  std::vector<std::string> entry_nodes;
  std::vector<std::string> termination_nodes;
  contracts::Json metadata = contracts::Json::object();
};

struct CanonicalAlgorithmIR final {
  int schema_version = 1;
  std::string canonicalizer_version_value =
      std::string(canonicalizer_version);
  std::string structural_hash;
  contracts::Json canonical_payload = contracts::Json::object();
  std::string canonicalization_strength;
  std::uint64_t exact_permutations_considered = 0;
  std::vector<std::pair<std::string, int>> source_node_map;
  std::vector<std::string> warnings;
};

[[nodiscard]] PrimitiveSpec normalize_primitive(std::string value);
[[nodiscard]] std::vector<std::string> primitive_names();
[[nodiscard]] AlgorithmStructureSpec
structure_from_mapping(const contracts::Json &payload);
void validate_structure(const AlgorithmStructureSpec &spec);
[[nodiscard]] CanonicalAlgorithmIR canonicalize_structure(
    const AlgorithmStructureSpec &spec,
    std::uint64_t max_exact_permutations = 10'000U);
[[nodiscard]] CanonicalAlgorithmIR canonicalize_mapping(
    const contracts::Json &payload,
    std::uint64_t max_exact_permutations = 10'000U);
[[nodiscard]] contracts::Json to_json(const CanonicalAlgorithmIR &value);

} // namespace statewright::saa
