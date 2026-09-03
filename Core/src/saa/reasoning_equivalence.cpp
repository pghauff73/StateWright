#include "statewright/saa/reasoning_equivalence.hpp"

#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;
using Counter = std::map<std::string, std::size_t>;

[[nodiscard]] Counter operator_counter(
    const CanonicalReasoningAlgorithm &algorithm) {
  Counter result;
  for (const auto &node : algorithm.canonical_nodes) {
    ++result[node.at("operator").get<std::string>()];
  }
  return result;
}

[[nodiscard]] Counter relation_counter(
    const CanonicalReasoningAlgorithm &algorithm) {
  Counter result;
  for (const auto &edge : algorithm.canonical_edges) {
    ++result[edge.at("relation").get<std::string>()];
  }
  return result;
}

[[nodiscard]] std::pair<std::vector<std::string>, std::vector<std::string>>
expanded_counter_delta(const Counter &left, const Counter &right) {
  std::set<std::string> keys;
  for (const auto &[key, ignored] : left) {
    static_cast<void>(ignored);
    keys.insert(key);
  }
  for (const auto &[key, ignored] : right) {
    static_cast<void>(ignored);
    keys.insert(key);
  }
  std::vector<std::string> added;
  std::vector<std::string> removed;
  for (const auto &key : keys) {
    const std::size_t left_count = left.contains(key) ? left.at(key) : 0U;
    const std::size_t right_count = right.contains(key) ? right.at(key) : 0U;
    if (right_count > left_count) {
      added.insert(added.end(), right_count - left_count, key);
    } else if (left_count > right_count) {
      removed.insert(removed.end(), left_count - right_count, key);
    }
  }
  return {std::move(added), std::move(removed)};
}

[[nodiscard]] std::vector<std::string>
symmetric_difference(const std::vector<std::string> &left,
                     const std::vector<std::string> &right) {
  std::set<std::string> left_set(left.begin(), left.end());
  std::set<std::string> right_set(right.begin(), right.end());
  std::vector<std::string> result;
  std::set_symmetric_difference(left_set.begin(), left_set.end(),
                                right_set.begin(), right_set.end(),
                                std::back_inserter(result));
  return result;
}

[[nodiscard]] bool counter_subset(const Counter &left,
                                  const Counter &right) {
  return std::all_of(left.begin(), left.end(), [&](const auto &item) {
    const auto found = right.find(item.first);
    return found != right.end() && item.second <= found->second;
  });
}

[[nodiscard]] bool set_subset(const std::vector<std::string> &left,
                              const std::vector<std::string> &right) {
  const std::set<std::string> right_set(right.begin(), right.end());
  return std::all_of(left.begin(), left.end(), [&](const auto &item) {
    return right_set.contains(item);
  });
}

} // namespace

ReasoningTopologyDelta reasoning_topology_delta(
    const CanonicalReasoningAlgorithm &left,
    const CanonicalReasoningAlgorithm &right) {
  const auto [added_operators, removed_operators] =
      expanded_counter_delta(operator_counter(left), operator_counter(right));
  const auto [added_relations, removed_relations] =
      expanded_counter_delta(relation_counter(left), relation_counter(right));
  const auto input_delta =
      symmetric_difference(left.input_semantics, right.input_semantics);
  const auto output_delta =
      symmetric_difference(left.output_semantics, right.output_semantics);
  const Json material = {{"added_operators", added_operators},
                         {"added_relations", added_relations},
                         {"input_delta", input_delta},
                         {"output_delta", output_delta},
                         {"removed_operators", removed_operators},
                         {"removed_relations", removed_relations},
                         {"version", reasoning_equivalence_version}};
  return {.added_operators = added_operators,
          .removed_operators = removed_operators,
          .added_relations = added_relations,
          .removed_relations = removed_relations,
          .semantic_input_delta = input_delta,
          .semantic_output_delta = output_delta,
          .delta_signature = contracts::sha256_json(material)};
}

ReasoningEquivalenceAssessment compare_reasoning_algorithms(
    const CanonicalReasoningAlgorithm &left,
    const CanonicalReasoningAlgorithm &right) {
  const bool topology_match =
      left.topology_signature == right.topology_signature;
  const bool semantic_match =
      left.semantic_signature == right.semantic_signature;
  const bool exact = left.canonical_reasoning_signature ==
                     right.canonical_reasoning_signature;
  const bool conservative =
      left.canonicalization_strength !=
          "EXACT_BOUNDED_GRAPH_CANONICALIZATION" ||
      right.canonicalization_strength !=
          "EXACT_BOUNDED_GRAPH_CANONICALIZATION";
  auto delta = reasoning_topology_delta(left, right);
  std::vector<std::string> relations;
  std::string status;
  bool reuse = false;
  if (exact && !conservative) {
    status = "EXACT_REASONING_ALGORITHM_EQUIVALENCE";
    reuse = true;
    relations.emplace_back("EQUIVALENT_TO");
  } else if (exact) {
    status = "CONSERVATIVE_REASONING_IDENTITY_MATCH";
    relations.emplace_back("POTENTIAL_EQUIVALENT_TO");
  } else if (topology_match && !semantic_match) {
    status = "OPERATOR_TOPOLOGY_MATCH_SEMANTIC_DIFFERENCE";
    relations.emplace_back("NEAR_VARIANT_OF");
  } else if (semantic_match && !topology_match) {
    status = "SEMANTIC_GOAL_MATCH_TOPOLOGY_DIFFERENCE";
    relations.emplace_back("ALTERNATIVE_REASONING_TOPOLOGY");
  } else {
    const auto left_operators = operator_counter(left);
    const auto right_operators = operator_counter(right);
    const auto left_relations = relation_counter(left);
    const auto right_relations = relation_counter(right);
    const bool left_subset = counter_subset(left_operators, right_operators) &&
                             counter_subset(left_relations, right_relations);
    const bool right_subset = counter_subset(right_operators, left_operators) &&
                              counter_subset(right_relations, left_relations);
    if (left_subset &&
        set_subset(left.input_semantics, right.input_semantics) &&
        set_subset(left.output_semantics, right.output_semantics)) {
      status = "POTENTIAL_REASONING_SPECIALIZATION_EXTENSION";
      relations.emplace_back("POTENTIAL_SPECIALIZES");
    } else if (right_subset &&
               set_subset(right.input_semantics, left.input_semantics) &&
               set_subset(right.output_semantics, left.output_semantics)) {
      status = "POTENTIAL_REASONING_GENERALIZATION_RELATION";
      relations.emplace_back("POTENTIAL_GENERALIZES");
    } else {
      status = "DISTINCT_REASONING_ALGORITHMS";
    }
  }
  const Json material = {
      {"conservative", conservative},
      {"delta", delta.delta_signature},
      {"left", left.canonical_reasoning_signature},
      {"relations", relations},
      {"right", right.canonical_reasoning_signature},
      {"semantic_match", semantic_match},
      {"status", status},
      {"topology_match", topology_match},
      {"version", reasoning_equivalence_version}};
  return {.schema_version = 1,
          .equivalence_version = std::string(reasoning_equivalence_version),
          .status = std::move(status),
          .exact_equivalence = exact && !conservative,
          .topology_match = topology_match,
          .semantic_match = semantic_match,
          .conservative_source_binding = conservative,
          .relation_candidates = std::move(relations),
          .delta = std::move(delta),
          .canonical_reuse_eligible = reuse,
          .assessment_signature = contracts::sha256_json(material),
          .warnings = {
              "GENERALIZES/SPECIALIZES candidates are structural hypotheses only. They require separate evidence before becoming qualified Algorithm Store relations."}};
}

Json to_json(const ReasoningTopologyDelta &value) {
  return {{"added_operators", value.added_operators},
          {"added_relations", value.added_relations},
          {"delta_signature", value.delta_signature},
          {"removed_operators", value.removed_operators},
          {"removed_relations", value.removed_relations},
          {"semantic_input_delta", value.semantic_input_delta},
          {"semantic_output_delta", value.semantic_output_delta}};
}

Json to_json(const ReasoningEquivalenceAssessment &value) {
  return {{"assessment_signature", value.assessment_signature},
          {"canonical_reuse_eligible", value.canonical_reuse_eligible},
          {"conservative_source_binding",
           value.conservative_source_binding},
          {"delta", to_json(value.delta)},
          {"equivalence_version", value.equivalence_version},
          {"exact_equivalence", value.exact_equivalence},
          {"relation_candidates", value.relation_candidates},
          {"schema_version", value.schema_version},
          {"semantic_match", value.semantic_match},
          {"status", value.status},
          {"topology_match", value.topology_match},
          {"warnings", value.warnings}};
}

} // namespace statewright::saa
