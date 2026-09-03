#include "statewright/saa/reasoning_semantics.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

const std::set<std::string> reasoning_state_kinds = {"ATOMIC", "COMPOSITE"};
const std::vector<std::string> governance_subsystems = {
    "EON", "OURD", "IURM", "CFEL", "BD_DL", "HYPOTHESIS_STATE",
    "ALGORITHM_STORE"};

[[noreturn]] void semantics_error(std::string message) {
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

[[nodiscard]] ReasoningSemanticIssue make_issue(
    std::string issue_kind, std::vector<std::string> dimension_ids,
    std::string label, std::vector<std::string> meanings, bool blocking,
    std::vector<std::string> questions) {
  for (auto &identifier : dimension_ids) {
    identifier = trimmed(std::move(identifier));
  }
  std::sort(dimension_ids.begin(), dimension_ids.end());
  meanings = normalized_texts(std::move(meanings));
  label = normalized_text(std::move(label));
  const Json material = {{"blocking", blocking},
                         {"dimension_ids", dimension_ids},
                         {"issue_kind", issue_kind},
                         {"label", label},
                         {"meanings", meanings},
                         {"version", reasoning_semantic_version}};
  const std::string signature = contracts::sha256_json(material);
  std::vector<std::string> normalized_questions;
  for (auto &question : questions) {
    question = trimmed(std::move(question));
    if (!question.empty()) {
      normalized_questions.push_back(std::move(question));
    }
  }
  return {.issue_id = "reasoning-semantic:" + signature.substr(0, 24),
          .issue_kind = std::move(issue_kind),
          .dimension_ids = std::move(dimension_ids),
          .label = std::move(label),
          .meanings = std::move(meanings),
          .blocking = blocking,
          .status = blocking ? "SEMANTIC_MISREPRESENTATION"
                             : "SEMANTIC_REVIEW_REQUIRED",
          .questions = std::move(normalized_questions),
          .issue_signature = signature};
}

struct ValidatedState final {
  std::vector<const ReasoningStateDimension *> dimensions;
  std::map<std::string, const ReasoningStateDimension *> by_id;
  std::vector<ReasoningStateDependency> dependencies;
};

[[nodiscard]] ValidatedState validate_state(const ReasoningStateModel &state) {
  if (state.dimensions.empty()) {
    semantics_error(
        "SAA-8.2 reasoning state must contain at least one dimension");
  }
  ValidatedState result;
  for (const auto &dimension : state.dimensions) {
    const std::string identifier = trimmed(dimension.dimension_id);
    if (identifier.empty() || result.by_id.contains(identifier)) {
      semantics_error(
          "SAA-8.2 reasoning state dimension IDs must be unique");
    }
    const std::string kind = uppercase(dimension.representation_kind);
    if (!reasoning_state_kinds.contains(kind)) {
      semantics_error(
          "unsupported SAA-8.2 reasoning representation kind: " + kind);
    }
    if (normalized_text(dimension.label).empty() ||
        normalized_text(dimension.meaning).empty()) {
      semantics_error(
          "SAA-8.2 reasoning dimensions require explicit label and meaning");
    }
    result.dimensions.push_back(&dimension);
    result.by_id.emplace(identifier, &dimension);
  }
  std::set<std::tuple<std::string, std::string, std::string>> seen;
  for (const auto &dependency : state.dependencies) {
    const std::string source = trimmed(dependency.source_dimension_id);
    const std::string target = trimmed(dependency.target_dimension_id);
    const std::string relation = uppercase(dependency.relation);
    if (!result.by_id.contains(source) || !result.by_id.contains(target)) {
      semantics_error(
          "SAA-8.2 dependency references unknown reasoning dimension");
    }
    if (source == target) {
      semantics_error(
          "SAA-8.2 reasoning dimension cannot depend on itself directly");
    }
    if (!seen.emplace(source, target, relation).second) {
      semantics_error("duplicate SAA-8.2 reasoning dependency");
    }
    result.dependencies.push_back(dependency);
  }
  return result;
}

} // namespace

ReasoningSemanticAssessment assess_reasoning_state_semantics(
    const ReasoningStateModel &state,
    const CanonicalReasoningAlgorithm *algorithm) {
  const auto validated = validate_state(state);
  std::vector<ReasoningSemanticIssue> issues;

  std::map<std::string, std::vector<const ReasoningStateDimension *>> labels;
  std::vector<std::string> label_order;
  for (const auto *dimension : validated.dimensions) {
    const std::string label = normalized_text(dimension->label);
    if (!labels.contains(label)) {
      label_order.push_back(label);
    }
    labels[label].push_back(dimension);
  }
  for (const auto &label : label_order) {
    const auto &dimensions = labels.at(label);
    std::set<std::string> meanings;
    for (const auto *dimension : dimensions) {
      meanings.insert(normalized_text(dimension->meaning));
    }
    if (meanings.size() > 1U) {
      std::vector<std::string> dimension_ids;
      for (const auto *dimension : dimensions) {
        dimension_ids.push_back(dimension->dimension_id);
      }
      issues.push_back(make_issue(
          "SEMANTIC_LABEL_COLLISION", std::move(dimension_ids), label,
          {meanings.begin(), meanings.end()}, true,
          {"Why is the label '" + label +
               "' being used for multiple distinct meanings?",
           "Should these state dimensions be renamed or resolved to one evidence-backed concept?"}));
    }
  }

  std::map<std::string, std::vector<const ReasoningStateDependency *>> incoming;
  for (const auto &[identifier, ignored] : validated.by_id) {
    static_cast<void>(ignored);
    incoming[identifier] = {};
  }
  for (const auto &dependency : validated.dependencies) {
    incoming[trimmed(dependency.target_dimension_id)].push_back(&dependency);
  }

  for (const auto *dimension : validated.dimensions) {
    const std::string identifier = trimmed(dimension->dimension_id);
    const auto &parents = incoming.at(identifier);
    std::set<std::string> parent_meaning_set;
    for (const auto *dependency : parents) {
      parent_meaning_set.insert(normalized_text(
          validated.by_id.at(trimmed(dependency->source_dimension_id))->meaning));
    }
    const std::vector<std::string> parent_meanings(parent_meaning_set.begin(),
                                                   parent_meaning_set.end());
    const std::string kind = uppercase(dimension->representation_kind);
    const std::string status = uppercase(dimension->epistemic_status);
    const auto evidence = normalized_texts(dimension->evidence_ids);
    std::vector<std::string> coupled_ids = {identifier};
    for (const auto *dependency : parents) {
      coupled_ids.push_back(dependency->source_dimension_id);
    }
    std::vector<std::string> coupled_meanings = {dimension->meaning};
    coupled_meanings.insert(coupled_meanings.end(), parent_meanings.begin(),
                            parent_meanings.end());
    std::ostringstream joined_meanings;
    for (std::size_t index = 0; index < parent_meanings.size(); ++index) {
      if (index != 0U) {
        joined_meanings << ", ";
      }
      joined_meanings << parent_meanings[index];
    }
    if (kind == "ATOMIC" && parent_meanings.size() > 1U) {
      issues.push_back(make_issue(
          "ATOMIC_DIMENSION_COUPLES_MULTIPLE_MEANINGS", coupled_ids,
          dimension->label, coupled_meanings, true,
          {"Does '" + dimension->label +
               "' really denote one independent quantity, or is it a mixture of " +
               joined_meanings.str() + "?",
           "Can the mixed state be decomposed into representative independent reasoning dimensions?"}));
    }
    if (dimension->declared_independent && !parents.empty()) {
      issues.push_back(make_issue(
          "DECLARED_INDEPENDENCE_CONTRADICTED_BY_DEPENDENCY", coupled_ids,
          dimension->label, coupled_meanings, true,
          {"Why is independently declared '" + dimension->label +
               "' derived from other reasoning-state dimensions?",
           "Should the dimension be reclassified as derived/composite, or should the dependency be removed?"}));
    }
    if (kind == "COMPOSITE" && !parents.empty() &&
        status != "SEMANTICALLY_RESOLVED") {
      issues.push_back(make_issue(
          "UNRESOLVED_COMPOSITE_REASONING_SEMANTICS", coupled_ids,
          dimension->label, coupled_meanings, true,
          {"What exact meaning does composite reasoning dimension '" +
               dimension->label + "' have?",
           "What evidence and falsifier distinguish this composite from its component concepts?"}));
    }
    if ((status == "FACT" || status == "VERIFIED_FACT" ||
         status == "SEMANTICALLY_RESOLVED") &&
        evidence.empty()) {
      issues.push_back(make_issue(
          "UNGROUNDED_REASONING_STATE", {identifier}, dimension->label,
          {dimension->meaning}, true,
          {"What evidence grounds the asserted status of '" +
               dimension->label + "'?",
           "Until evidence is attached, should this state be downgraded to an unverified concept or hypothesis?"}));
    }
  }

  if (algorithm != nullptr) {
    std::set<std::string> algorithm_semantics(algorithm->input_semantics.begin(),
                                              algorithm->input_semantics.end());
    algorithm_semantics.insert(algorithm->output_semantics.begin(),
                               algorithm->output_semantics.end());
    for (const auto &node : algorithm->canonical_nodes) {
      for (const auto &value : node.at("semantic_inputs")) {
        algorithm_semantics.insert(value.get<std::string>());
      }
      for (const auto &value : node.at("semantic_outputs")) {
        algorithm_semantics.insert(value.get<std::string>());
      }
    }
    for (const auto *dimension : validated.dimensions) {
      if (!algorithm_semantics.contains(normalized_text(dimension->meaning))) {
        issues.push_back(make_issue(
            "UNBOUND_REASONING_STATE_MEANING", {dimension->dimension_id},
            dimension->label, {dimension->meaning}, false,
            {"Where does reasoning-state meaning '" + dimension->meaning +
             "' participate in the canonical reasoning algorithm?"}));
      }
    }
  }

  std::vector<Json> dimensions_payload;
  for (const auto &[identifier, dimension] : validated.by_id) {
    dimensions_payload.push_back(
        {{"declared_independent", dimension->declared_independent},
         {"dimension_id", identifier},
         {"epistemic_status", uppercase(dimension->epistemic_status)},
         {"evidence_ids", normalized_texts(dimension->evidence_ids)},
         {"label", normalized_text(dimension->label)},
         {"meaning", normalized_text(dimension->meaning)},
         {"representation_kind", uppercase(dimension->representation_kind)}});
  }
  std::vector<Json> dependencies_payload;
  for (const auto &dependency : validated.dependencies) {
    dependencies_payload.push_back(
        {{"evidence_ids", normalized_texts(dependency.evidence_ids)},
         {"relation", uppercase(dependency.relation)},
         {"source", dependency.source_dimension_id},
         {"target", dependency.target_dimension_id}});
  }
  std::sort(dependencies_payload.begin(), dependencies_payload.end(),
            [](const Json &left, const Json &right) {
              return std::make_tuple(left.at("source").get<std::string>(),
                                     left.at("target").get<std::string>(),
                                     left.at("relation").get<std::string>()) <
                     std::make_tuple(right.at("source").get<std::string>(),
                                     right.at("target").get<std::string>(),
                                     right.at("relation").get<std::string>());
            });
  const std::string state_signature = contracts::sha256_json(
      {{"dependencies", dependencies_payload},
       {"dimensions", dimensions_payload},
       {"version", reasoning_semantic_version}});
  const bool blocking =
      std::any_of(issues.begin(), issues.end(),
                  [](const auto &issue) { return issue.blocking; });
  const std::string status =
      blocking ? "REASONING_STATE_SEMANTIC_MISREPRESENTATION"
               : (issues.empty() ? "REASONING_STATE_SEMANTICALLY_COHERENT"
                                 : "REASONING_STATE_SEMANTIC_REVIEW");
  std::vector<std::string> issue_signatures;
  for (const auto &issue : issues) {
    issue_signatures.push_back(issue.issue_signature);
  }
  const Json material = {
      {"algorithm_signature",
       algorithm == nullptr ? "" : algorithm->canonical_reasoning_signature},
      {"issues", issue_signatures},
      {"state_signature", state_signature},
      {"status", status},
      {"version", reasoning_semantic_version}};
  return {.schema_version = 1,
          .semantic_version = std::string(reasoning_semantic_version),
          .status = status,
          .issues = std::move(issues),
          .state_signature = state_signature,
          .canonical_reasoning_state_eligible = !blocking,
          .public_artifact_only = true,
          .assessment_signature = contracts::sha256_json(material)};
}

std::vector<ReasoningSemanticDirective> propagate_reasoning_semantic_issues(
    const std::vector<ReasoningSemanticIssue> &issues) {
  static const std::map<std::string, std::string> actions = {
      {"EON", "SURFACE_REASONING_SEMANTIC_ISSUE"},
      {"OURD", "CREATE_REASONING_SEMANTIC_RESOLUTION_OBJECTIVE"},
      {"IURM", "BLOCK_MISREPRESENTED_REASONING_DIMENSION"},
      {"CFEL", "REGISTER_REASONING_SEMANTIC_COLLISION"},
      {"BD_DL", "DETERMINE_REASONING_SEMANTIC_BOUNDARY"},
      {"HYPOTHESIS_STATE", "STORE_REASONING_MEANING_AS_UNVERIFIED"},
      {"ALGORITHM_STORE", "BLOCK_REASONING_CANONICAL_ADMISSION"}};
  std::vector<ReasoningSemanticDirective> directives;
  for (const auto &issue : issues) {
    const Json payload = {{"dimension_ids", issue.dimension_ids},
                          {"issue_kind", issue.issue_kind},
                          {"label", issue.label},
                          {"meanings", issue.meanings},
                          {"questions", issue.questions}};
    for (const auto &subsystem : governance_subsystems) {
      directives.push_back(
          {.issue_id = issue.issue_id,
           .subsystem = subsystem,
           .action = actions.at(subsystem),
           .blocking = issue.blocking &&
                       (subsystem == "IURM" || subsystem == "ALGORITHM_STORE"),
           .payload = payload});
    }
  }
  return directives;
}

Json to_json(const ReasoningStateDimension &value) {
  return {{"declared_independent", value.declared_independent},
          {"dimension_id", value.dimension_id},
          {"epistemic_status", value.epistemic_status},
          {"evidence_ids", value.evidence_ids},
          {"label", value.label},
          {"meaning", value.meaning},
          {"representation_kind", value.representation_kind}};
}

Json to_json(const ReasoningStateDependency &value) {
  return {{"evidence_ids", value.evidence_ids},
          {"relation", value.relation},
          {"source_dimension_id", value.source_dimension_id},
          {"target_dimension_id", value.target_dimension_id}};
}

Json to_json(const ReasoningStateModel &value) {
  Json dimensions = Json::array();
  for (const auto &dimension : value.dimensions) {
    dimensions.push_back(to_json(dimension));
  }
  Json dependencies = Json::array();
  for (const auto &dependency : value.dependencies) {
    dependencies.push_back(to_json(dependency));
  }
  return {{"dependencies", dependencies}, {"dimensions", dimensions}};
}

Json to_json(const ReasoningSemanticIssue &value) {
  return {{"blocking", value.blocking},
          {"dimension_ids", value.dimension_ids},
          {"issue_id", value.issue_id},
          {"issue_kind", value.issue_kind},
          {"issue_signature", value.issue_signature},
          {"label", value.label},
          {"meanings", value.meanings},
          {"questions", value.questions},
          {"status", value.status}};
}

Json to_json(const ReasoningSemanticAssessment &value) {
  Json issues = Json::array();
  for (const auto &issue : value.issues) {
    issues.push_back(to_json(issue));
  }
  return {{"assessment_signature", value.assessment_signature},
          {"canonical_reasoning_state_eligible",
           value.canonical_reasoning_state_eligible},
          {"issues", issues},
          {"public_artifact_only", value.public_artifact_only},
          {"schema_version", value.schema_version},
          {"semantic_version", value.semantic_version},
          {"state_signature", value.state_signature},
          {"status", value.status}};
}

Json to_json(const ReasoningSemanticDirective &value) {
  return {{"action", value.action},
          {"blocking", value.blocking},
          {"issue_id", value.issue_id},
          {"payload", value.payload},
          {"subsystem", value.subsystem}};
}

} // namespace statewright::saa
