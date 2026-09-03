#include "statewright/saa/semantic_alignment.hpp"

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

const std::set<std::string> alignment_relations = {
    "EXACT_EQUIVALENT", "SPECIALIZES", "GENERALIZES", "ANALOGOUS_TO",
    "RELATED_TO",      "NOT_EQUIVALENT"};

[[noreturn]] void alignment_error(std::string message) {
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
normalized_evidence_ids(std::vector<std::string> values) {
  std::set<std::string> normalized;
  for (auto &value : values) {
    value = trimmed(std::move(value));
    if (!value.empty()) {
      normalized.insert(std::move(value));
    }
  }
  return {normalized.begin(), normalized.end()};
}

[[nodiscard]] std::vector<std::string>
normalized_falsifiers(std::vector<std::string> values) {
  std::set<std::string> normalized;
  for (auto &value : values) {
    value = normalized_text(std::move(value));
    if (!value.empty()) {
      normalized.insert(std::move(value));
    }
  }
  return {normalized.begin(), normalized.end()};
}

void require_grounded_evidence(const SemanticEvidenceResolver &resolver,
                               std::string_view evidence_id) {
  std::optional<SemanticGroundingEvidence> evidence;
  try {
    evidence = resolver(evidence_id);
  } catch (const std::exception &) {
    evidence = std::nullopt;
  }
  if (!evidence) {
    alignment_error("semantic alignment evidence is not registered: " +
                    std::string(evidence_id));
  }
  if (evidence->object_type != "egcf-evidence") {
    alignment_error(
        "semantic alignment evidence ID does not reference EvidenceArtifact");
  }
  if (evidence->success != true || evidence->simulated) {
    alignment_error(
        "semantic alignment evidence must be successful and non-simulated");
  }
  if (!evidence->producer.starts_with("deterministic-") &&
      !evidence->producer.starts_with("human-")) {
    alignment_error(
        "semantic alignment evidence requires deterministic or human producer");
  }
  if (evidence->method == "reported" || evidence->method == "model-claimed" ||
      evidence->method == "model-generated-claim") {
    alignment_error(
        "reported/model-claimed evidence cannot establish semantic alignment");
  }
}

} // namespace

SemanticAlignmentFalsifierResult::SemanticAlignmentFalsifierResult(
    std::string falsifier_value, std::string outcome_value,
    std::string evidence_id_value)
    : falsifier(normalized_text(std::move(falsifier_value))),
      outcome(uppercase(std::move(outcome_value))),
      evidence_id(trimmed(std::move(evidence_id_value))) {}

SemanticAlignmentProposal propose_semantic_alignment(
    const SemanticConcept &left, const SemanticConcept &right,
    std::string relation, std::string shared_meaning,
    bool expected_effects_match, std::vector<std::string> evidence_ids,
    std::vector<std::string> falsifiers, bool independent_review) {
  relation = uppercase(std::move(relation));
  if (!alignment_relations.contains(relation)) {
    alignment_error("unsupported semantic alignment relation: '" + relation +
                    "'");
  }
  shared_meaning = normalized_text(std::move(shared_meaning));
  if (relation != "NOT_EQUIVALENT" && shared_meaning.empty()) {
    alignment_error(
        "semantic alignment requires an explicit shared-meaning proposition");
  }
  evidence_ids = normalized_evidence_ids(std::move(evidence_ids));
  falsifiers = normalized_falsifiers(std::move(falsifiers));
  const Json payload = {{"evidence_ids", evidence_ids},
                        {"expected_effects_match", expected_effects_match},
                        {"falsifiers", falsifiers},
                        {"independent_review", independent_review},
                        {"left", left.concept_signature},
                        {"relation", relation},
                        {"right", right.concept_signature},
                        {"shared_meaning", shared_meaning},
                        {"version", semantic_alignment_version}};
  return {.left_concept_signature = left.concept_signature,
          .right_concept_signature = right.concept_signature,
          .relation = std::move(relation),
          .shared_meaning = std::move(shared_meaning),
          .expected_effects_match = expected_effects_match,
          .evidence_ids = std::move(evidence_ids),
          .falsifiers = std::move(falsifiers),
          .independent_review = independent_review,
          .proposal_signature = contracts::sha256_json(payload)};
}

SemanticAlignmentAssessment assess_semantic_alignment(
    const SemanticEvidenceResolver &evidence_resolver,
    const SemanticConcept &left, const SemanticConcept &right,
    const SemanticAlignmentProposal &proposal,
    std::vector<SemanticAlignmentFalsifierResult> falsifier_results) {
  const std::set<std::string> concepts = {left.concept_signature,
                                          right.concept_signature};
  const std::set<std::string> proposed = {proposal.left_concept_signature,
                                         proposal.right_concept_signature};
  if (concepts != proposed) {
    alignment_error("semantic alignment proposal targets different concepts");
  }

  const std::string physical_relation =
      physical_semantic_relation(left, right);
  std::vector<std::string> blockers;
  std::vector<std::string> evidence;
  for (const auto &evidence_id : proposal.evidence_ids) {
    try {
      require_grounded_evidence(evidence_resolver, evidence_id);
      evidence.push_back(evidence_id);
    } catch (const common::Error &error) {
      blockers.emplace_back(error.what());
    }
  }
  if (proposal.relation != "NOT_EQUIVALENT" && evidence.empty()) {
    blockers.emplace_back(
        "qualified semantic alignment requires grounded evidence");
  }
  if (!proposal.independent_review) {
    blockers.emplace_back("independent semantic alignment review missing");
  }
  if (!left.canonical_eligible || !right.canonical_eligible) {
    blockers.emplace_back(
        "both aligned concepts must already be canonically resolved");
  }

  std::map<std::string, SemanticAlignmentFalsifierResult> by_falsifier;
  for (auto &result : falsifier_results) {
    result.falsifier = normalized_text(std::move(result.falsifier));
    result.outcome = uppercase(std::move(result.outcome));
    result.evidence_id = trimmed(std::move(result.evidence_id));
    by_falsifier.insert_or_assign(result.falsifier, std::move(result));
  }
  std::vector<SemanticAlignmentFalsifierResult> normalized_results;
  for (const auto &falsifier : proposal.falsifiers) {
    const auto found = by_falsifier.find(falsifier);
    if (found == by_falsifier.end()) {
      blockers.push_back("missing alignment falsifier result: " + falsifier);
      continue;
    }
    const auto &result = found->second;
    if (result.outcome != "SURVIVED") {
      blockers.push_back(
          "semantic alignment falsifier did not survive: " + falsifier);
    }
    if (!result.evidence_id.empty()) {
      try {
        require_grounded_evidence(evidence_resolver, result.evidence_id);
      } catch (const common::Error &error) {
        blockers.emplace_back(error.what());
      }
    }
    normalized_results.push_back(result);
  }

  if (proposal.relation == "EXACT_EQUIVALENT") {
    if (physical_relation == "DIMENSIONALLY_INCOMPATIBLE") {
      blockers.emplace_back(
          "exact semantic equivalence is dimensionally contradicted");
    }
    if (physical_relation == "SAME_DIMENSION_DIFFERENT_QUANTITY_KIND") {
      blockers.emplace_back(
          "same dimensions but different quantity kinds are not exact semantic equivalence");
    }
    if (!proposal.expected_effects_match) {
      blockers.emplace_back(
          "exact semantic equivalence requires matching expected effects");
    }
    if (proposal.shared_meaning.empty()) {
      blockers.emplace_back(
          "exact semantic equivalence requires shared meaning");
    }
  }

  std::string status;
  bool canonical_eligible = false;
  bool exact_eligible = false;
  if (!blockers.empty()) {
    if (physical_relation == "DIMENSIONALLY_INCOMPATIBLE") {
      status = "SEMANTIC_ALIGNMENT_CONTRADICTED";
    } else if (physical_relation ==
               "SAME_DIMENSION_DIFFERENT_QUANTITY_KIND") {
      status = "DIMENSION_COMPATIBLE_SEMANTICALLY_DISTINCT";
    } else {
      status = "SEMANTIC_ALIGNMENT_UNRESOLVED";
    }
  } else {
    static const std::map<std::string, std::string> statuses = {
        {"EXACT_EQUIVALENT", "EXACT_CROSS_DOMAIN_SEMANTIC_EQUIVALENCE"},
        {"SPECIALIZES", "QUALIFIED_SEMANTIC_SPECIALIZATION"},
        {"GENERALIZES", "QUALIFIED_SEMANTIC_GENERALIZATION"},
        {"ANALOGOUS_TO", "QUALIFIED_SEMANTIC_ANALOGY"},
        {"RELATED_TO", "QUALIFIED_SEMANTIC_RELATION"},
        {"NOT_EQUIVALENT", "QUALIFIED_SEMANTIC_NON_EQUIVALENCE"}};
    status = statuses.at(proposal.relation);
    canonical_eligible = true;
    exact_eligible = proposal.relation == "EXACT_EQUIVALENT";
  }

  std::sort(evidence.begin(), evidence.end());
  Json result_payload = Json::array();
  for (const auto &result : normalized_results) {
    result_payload.push_back(to_json(result));
  }
  const Json payload = {
      {"blocking_reasons", blockers},
      {"evidence_ids", evidence},
      {"exact_substitution_eligible", exact_eligible},
      {"falsifiers", result_payload},
      {"independent_review", proposal.independent_review},
      {"left", left.concept_signature},
      {"physical_relation", physical_relation},
      {"proposal", proposal.proposal_signature},
      {"relation", proposal.relation},
      {"right", right.concept_signature},
      {"status", status},
      {"version", semantic_alignment_version}};
  return {.left_concept_signature = left.concept_signature,
          .right_concept_signature = right.concept_signature,
          .relation = proposal.relation,
          .status = std::move(status),
          .physical_relation = physical_relation,
          .evidence_ids = std::move(evidence),
          .falsifier_results = std::move(normalized_results),
          .independent_review = proposal.independent_review,
          .exact_substitution_eligible = exact_eligible,
          .canonical_alignment_eligible = canonical_eligible,
          .alignment_signature = contracts::sha256_json(payload),
          .blocking_reasons = std::move(blockers)};
}

Json to_json(const SemanticAlignmentProposal &value) {
  return {{"evidence_ids", value.evidence_ids},
          {"expected_effects_match", value.expected_effects_match},
          {"falsifiers", value.falsifiers},
          {"independent_review", value.independent_review},
          {"left_concept_signature", value.left_concept_signature},
          {"proposal_signature", value.proposal_signature},
          {"relation", value.relation},
          {"right_concept_signature", value.right_concept_signature},
          {"shared_meaning", value.shared_meaning}};
}

Json to_json(const SemanticAlignmentFalsifierResult &value) {
  return {{"evidence_id", trimmed(value.evidence_id)},
          {"falsifier", normalized_text(value.falsifier)},
          {"outcome", uppercase(value.outcome)}};
}

Json to_json(const SemanticAlignmentAssessment &value) {
  Json falsifiers = Json::array();
  for (const auto &result : value.falsifier_results) {
    falsifiers.push_back(to_json(result));
  }
  return {{"alignment_signature", value.alignment_signature},
          {"blocking_reasons", value.blocking_reasons},
          {"canonical_alignment_eligible",
           value.canonical_alignment_eligible},
          {"evidence_ids", value.evidence_ids},
          {"exact_substitution_eligible",
           value.exact_substitution_eligible},
          {"falsifier_results", falsifiers},
          {"independent_review", value.independent_review},
          {"left_concept_signature", value.left_concept_signature},
          {"physical_relation", value.physical_relation},
          {"relation", value.relation},
          {"right_concept_signature", value.right_concept_signature},
          {"status", value.status}};
}

} // namespace statewright::saa
