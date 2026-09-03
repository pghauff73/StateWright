#pragma once

#include "statewright/saa/semantic_units.hpp"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view semantic_alignment_version =
    "saa-semantic-alignment-v1";

struct SemanticGroundingEvidence final {
  std::string object_type;
  std::optional<bool> success;
  bool simulated = false;
  std::string producer;
  std::string method;
};

using SemanticEvidenceResolver =
    std::function<std::optional<SemanticGroundingEvidence>(std::string_view)>;

struct SemanticAlignmentProposal final {
  std::string left_concept_signature;
  std::string right_concept_signature;
  std::string relation;
  std::string shared_meaning;
  bool expected_effects_match = false;
  std::vector<std::string> evidence_ids;
  std::vector<std::string> falsifiers;
  bool independent_review = false;
  std::string proposal_signature;
};

struct SemanticAlignmentFalsifierResult final {
  std::string falsifier;
  std::string outcome;
  std::string evidence_id;

  SemanticAlignmentFalsifierResult(std::string falsifier_value,
                                   std::string outcome_value,
                                   std::string evidence_id_value = {});
};

struct SemanticAlignmentAssessment final {
  std::string left_concept_signature;
  std::string right_concept_signature;
  std::string relation;
  std::string status;
  std::string physical_relation;
  std::vector<std::string> evidence_ids;
  std::vector<SemanticAlignmentFalsifierResult> falsifier_results;
  bool independent_review = false;
  bool exact_substitution_eligible = false;
  bool canonical_alignment_eligible = false;
  std::string alignment_signature;
  std::vector<std::string> blocking_reasons;
};

[[nodiscard]] SemanticAlignmentProposal propose_semantic_alignment(
    const SemanticConcept &left, const SemanticConcept &right,
    std::string relation, std::string shared_meaning,
    bool expected_effects_match, std::vector<std::string> evidence_ids = {},
    std::vector<std::string> falsifiers = {},
    bool independent_review = false);

[[nodiscard]] SemanticAlignmentAssessment assess_semantic_alignment(
    const SemanticEvidenceResolver &evidence_resolver,
    const SemanticConcept &left, const SemanticConcept &right,
    const SemanticAlignmentProposal &proposal,
    std::vector<SemanticAlignmentFalsifierResult> falsifier_results = {});

[[nodiscard]] contracts::Json to_json(const SemanticAlignmentProposal &value);
[[nodiscard]] contracts::Json
to_json(const SemanticAlignmentFalsifierResult &value);
[[nodiscard]] contracts::Json
to_json(const SemanticAlignmentAssessment &value);

} // namespace statewright::saa
