#include "statewright/saa/reasoning_composition.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[noreturn]] void composition_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument,
                      std::move(message));
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

[[nodiscard]] std::pair<bool, std::string>
negation_base(std::string value) {
  value = normalized_text(std::move(value));
  for (const std::string_view prefix : {std::string_view("not "),
                                        std::string_view("!"),
                                        std::string_view("¬")}) {
    if (value.starts_with(prefix)) {
      return {true, normalized_text(value.substr(prefix.size()))};
    }
  }
  return {false, value};
}

[[nodiscard]] std::vector<std::string> invariant_conflicts(
    const std::vector<std::string> &left,
    const std::vector<std::string> &right) {
  std::set<std::string> positive;
  std::set<std::string> negative;
  const auto add = [&](const std::vector<std::string> &values) {
    for (const auto &value : values) {
      auto [negated, base] = negation_base(value);
      if (!base.empty()) {
        (negated ? negative : positive).insert(std::move(base));
      }
    }
  };
  add(left);
  add(right);
  std::vector<std::string> result;
  std::set_intersection(positive.begin(), positive.end(), negative.begin(),
                        negative.end(), std::back_inserter(result));
  return result;
}

[[nodiscard]] std::string joined(const std::vector<std::string> &values) {
  std::ostringstream output;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0U) {
      output << ", ";
    }
    output << values[index];
  }
  return output.str();
}

[[nodiscard]] bool qualified_for(
    const ReasoningOutcomeQualification *qualification,
    const CanonicalReasoningAlgorithm &algorithm) {
  return qualification != nullptr &&
         qualification->canonical_reasoning_signature ==
             algorithm.canonical_reasoning_signature &&
         qualification->status == "QUALIFIED_REASONING_OUTCOME" &&
         qualification->canonical_reuse_eligible;
}

[[nodiscard]] int max_steps(const CanonicalReasoningAlgorithm &algorithm) {
  return algorithm.termination.is_object()
             ? algorithm.termination.value("max_steps", 0)
             : 0;
}

[[nodiscard]] std::vector<ReasoningNodeSpec> nodes_from_algorithm(
    std::string_view prefix, const CanonicalReasoningAlgorithm &algorithm) {
  std::vector<ReasoningNodeSpec> result;
  result.reserve(algorithm.canonical_nodes.size());
  for (std::size_t index = 0; index < algorithm.canonical_nodes.size();
       ++index) {
    const Json &node = algorithm.canonical_nodes[index];
    result.push_back(
        {.node_id = std::string(prefix) + std::to_string(index),
         .operator_name = node.at("operator").get<std::string>(),
         .semantic_inputs =
             node.value("semantic_inputs", std::vector<std::string>{}),
         .semantic_outputs =
             node.value("semantic_outputs", std::vector<std::string>{}),
         .public_claim_ids =
             node.value("public_claim_ids", std::vector<std::string>{}),
         .evidence_requirements = node.value(
             "evidence_requirements", std::vector<std::string>{}),
         .assumptions =
             node.value("assumptions", std::vector<std::string>{}),
         .falsifiers =
             node.value("falsifiers", std::vector<std::string>{}),
         .description = ""});
  }
  return result;
}

[[nodiscard]] std::vector<ReasoningEdgeSpec> edges_from_algorithm(
    std::string_view prefix, const CanonicalReasoningAlgorithm &algorithm) {
  std::vector<ReasoningEdgeSpec> result;
  result.reserve(algorithm.canonical_edges.size());
  for (const auto &edge : algorithm.canonical_edges) {
    result.push_back(
        {.source = std::string(prefix) +
                   std::to_string(edge.at("source").get<int>()),
         .target = std::string(prefix) +
                   std::to_string(edge.at("target").get<int>()),
         .relation = edge.at("relation").get<std::string>(),
         .condition = edge.value("condition", "")});
  }
  return result;
}

struct EntryExit final {
  std::vector<std::string> entries;
  std::vector<std::string> exits;
};

[[nodiscard]] EntryExit entry_exit(
    std::string_view prefix, const CanonicalReasoningAlgorithm &algorithm) {
  std::map<std::string, int> incoming;
  std::map<std::string, int> outgoing;
  for (std::size_t index = 0; index < algorithm.canonical_nodes.size();
       ++index) {
    const std::string id = std::string(prefix) + std::to_string(index);
    incoming.emplace(id, 0);
    outgoing.emplace(id, 0);
  }
  for (const auto &edge : algorithm.canonical_edges) {
    const std::string source =
        std::string(prefix) + std::to_string(edge.at("source").get<int>());
    const std::string target =
        std::string(prefix) + std::to_string(edge.at("target").get<int>());
    ++outgoing.at(source);
    ++incoming.at(target);
  }
  EntryExit result;
  for (const auto &[node_id, count] : incoming) {
    if (count == 0) {
      result.entries.push_back(node_id);
    }
  }
  for (const auto &[node_id, count] : outgoing) {
    if (count == 0) {
      result.exits.push_back(node_id);
    }
  }
  if (result.entries.empty() || result.exits.empty()) {
    composition_error(
        "SAA-8.6 composition requires component entry and exit nodes");
  }
  return result;
}

} // namespace

ReasoningCompositionAssessment assess_reasoning_composition(
    const CanonicalReasoningAlgorithm &left,
    const CanonicalReasoningAlgorithm &right,
    const ReasoningOutcomeQualification *left_qualification,
    const ReasoningOutcomeQualification *right_qualification) {
  std::vector<std::string> blockers;
  const bool exact_components =
      left.canonicalization_strength ==
          "EXACT_BOUNDED_GRAPH_CANONICALIZATION" &&
      right.canonicalization_strength ==
          "EXACT_BOUNDED_GRAPH_CANONICALIZATION";
  if (!exact_components) {
    blockers.push_back(
        "composition requires exact bounded canonical component identities");
  }
  const std::set<std::string> left_outputs(left.output_semantics.begin(),
                                           left.output_semantics.end());
  const std::set<std::string> right_inputs(right.input_semantics.begin(),
                                            right.input_semantics.end());
  const bool interface = std::includes(
      left_outputs.begin(), left_outputs.end(), right_inputs.begin(),
      right_inputs.end());
  if (!interface) {
    std::vector<std::string> missing;
    std::set_difference(right_inputs.begin(), right_inputs.end(),
                        left_outputs.begin(), left_outputs.end(),
                        std::back_inserter(missing));
    blockers.push_back(
        "downstream inputs not supplied by upstream outputs: " +
        joined(missing));
  }
  const auto conflicts = invariant_conflicts(left.invariants, right.invariants);
  const bool invariant_eligible = conflicts.empty();
  if (!invariant_eligible) {
    blockers.push_back("contradictory component invariants: " +
                       joined(conflicts));
  }
  const int combined_steps = max_steps(left) + max_steps(right);
  const bool termination_eligible =
      combined_steps >= 1 && combined_steps <= max_reasoning_steps;
  if (!termination_eligible) {
    blockers.push_back("combined termination budget " +
                       std::to_string(combined_steps) +
                       " exceeds bounded cap " +
                       std::to_string(max_reasoning_steps));
  }
  const bool qualification_eligible =
      qualified_for(left_qualification, left) &&
      qualified_for(right_qualification, right);
  if (!qualification_eligible) {
    blockers.push_back(
        "both component algorithms require canonical-reuse-qualified outcomes");
  }
  const std::string status = blockers.empty()
                                 ? "SAFE_REASONING_COMPOSITION"
                                 : "BLOCKED_REASONING_COMPOSITION";
  const Json payload = {{"blocking_gaps", blockers},
                        {"exact_component_identity", exact_components},
                        {"interface_eligible", interface},
                        {"invariant_eligible", invariant_eligible},
                        {"left", left.canonical_reasoning_signature},
                        {"qualification_eligible", qualification_eligible},
                        {"right", right.canonical_reasoning_signature},
                        {"termination_eligible", termination_eligible},
                        {"version", reasoning_composition_version}};
  return {.status = status,
          .interface_eligible = interface,
          .invariant_eligible = invariant_eligible,
          .termination_eligible = termination_eligible,
          .qualification_eligible = qualification_eligible,
          .exact_component_identity = exact_components,
          .blocking_gaps = std::move(blockers),
          .assessment_signature = contracts::sha256_json(payload)};
}

CanonicalReasoningComposition compose_reasoning_algorithms(
    const CanonicalReasoningAlgorithm &left,
    const CanonicalReasoningAlgorithm &right,
    const ReasoningOutcomeQualification &left_qualification,
    const ReasoningOutcomeQualification &right_qualification) {
  const auto assessment = assess_reasoning_composition(
      left, right, &left_qualification, &right_qualification);
  if (!assessment.composition_eligible()) {
    composition_error("unsafe reasoning composition: " +
                      joined(assessment.blocking_gaps));
  }
  auto nodes = nodes_from_algorithm("l", left);
  auto right_nodes = nodes_from_algorithm("r", right);
  nodes.insert(nodes.end(), std::make_move_iterator(right_nodes.begin()),
               std::make_move_iterator(right_nodes.end()));
  auto edges = edges_from_algorithm("l", left);
  auto right_edges = edges_from_algorithm("r", right);
  edges.insert(edges.end(), std::make_move_iterator(right_edges.begin()),
               std::make_move_iterator(right_edges.end()));
  const auto left_boundary = entry_exit("l", left);
  const auto right_boundary = entry_exit("r", right);
  for (const auto &source : left_boundary.exits) {
    for (const auto &target : right_boundary.entries) {
      edges.push_back({source, target, "NEXT", "qualified semantic handoff"});
    }
  }
  std::set<std::string> invariants(left.invariants.begin(),
                                   left.invariants.end());
  invariants.insert(right.invariants.begin(), right.invariants.end());
  std::set<std::string> applicability(left.applicability.begin(),
                                      left.applicability.end());
  applicability.insert(right.applicability.begin(), right.applicability.end());
  const auto composed = canonicalize_reasoning_algorithm(
      {.name = "qualified reasoning composition",
       .inputs = left.input_semantics,
       .outputs = right.output_semantics,
       .nodes = std::move(nodes),
       .edges = std::move(edges),
       .invariants = {invariants.begin(), invariants.end()},
       .termination = {"bounded-composition",
                       "all component termination predicates satisfied",
                       max_steps(left) + max_steps(right)},
       .applicability = {applicability.begin(), applicability.end()}});
  if (composed.canonicalization_strength !=
      "EXACT_BOUNDED_GRAPH_CANONICALIZATION") {
    composition_error(
        "composed reasoning graph exceeded exact canonicalization budget");
  }
  std::set<std::string> interface_values(right.input_semantics.begin(),
                                         right.input_semantics.end());
  std::vector<std::string> interface(interface_values.begin(),
                                     interface_values.end());
  const Json payload = {
      {"assessment", assessment.assessment_signature},
      {"composed", composed.canonical_reasoning_signature},
      {"interface", interface},
      {"left", left.canonical_reasoning_signature},
      {"left_qualification", left_qualification.qualification_signature},
      {"qualification_required", true},
      {"right", right.canonical_reasoning_signature},
      {"right_qualification", right_qualification.qualification_signature},
      {"version", reasoning_composition_version}};
  return {.schema_version = 1,
          .composition_version = std::string(reasoning_composition_version),
          .left_signature = left.canonical_reasoning_signature,
          .right_signature = right.canonical_reasoning_signature,
          .composed_algorithm = composed,
          .interface_semantics = std::move(interface),
          .component_qualification_signatures =
              {left_qualification.qualification_signature,
               right_qualification.qualification_signature},
          .qualification_required = true,
          .canonical_reuse_eligible = false,
          .composition_signature = contracts::sha256_json(payload),
          .warnings = {
              "Qualified components do not automatically qualify their composition. The composed algorithm requires its own SAA-8.5 outcome evidence before SAA-8.3 admission."}};
}

Json to_json(const ReasoningCompositionAssessment &value) {
  return {{"status", value.status},
          {"interface_eligible", value.interface_eligible},
          {"invariant_eligible", value.invariant_eligible},
          {"termination_eligible", value.termination_eligible},
          {"qualification_eligible", value.qualification_eligible},
          {"exact_component_identity", value.exact_component_identity},
          {"blocking_gaps", value.blocking_gaps},
          {"assessment_signature", value.assessment_signature},
          {"composition_eligible", value.composition_eligible()}};
}

Json to_json(const CanonicalReasoningComposition &value) {
  return {{"schema_version", value.schema_version},
          {"composition_version", value.composition_version},
          {"left_signature", value.left_signature},
          {"right_signature", value.right_signature},
          {"composed_algorithm", to_json(value.composed_algorithm)},
          {"interface_semantics", value.interface_semantics},
          {"component_qualification_signatures",
           value.component_qualification_signatures},
          {"qualification_required", value.qualification_required},
          {"canonical_reuse_eligible", value.canonical_reuse_eligible},
          {"composition_signature", value.composition_signature},
          {"warnings", value.warnings}};
}

} // namespace statewright::saa
