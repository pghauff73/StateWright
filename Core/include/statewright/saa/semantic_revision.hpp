#pragma once

#include "statewright/saa/semantic_alignment.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view semantic_revision_version =
    "saa-semantic-revision-v1";

struct SemanticContradiction final {
  std::string contradiction_id;
  std::string concept_signature;
  std::string contradiction_kind;
  std::string observed_statement;
  std::string observed_meaning;
  std::string observed_quantity_kind;
  std::optional<PhysicalDimensionVector> observed_dimension;
  std::string observed_unit_symbol;
  std::vector<std::string> evidence_ids;
  int severity_bp = 0;
  std::string status;
  std::string contradiction_signature;
};

struct SemanticRevisionProposal final {
  std::string proposal_id;
  std::string source_concept_signature;
  std::vector<std::string> contradiction_signatures;
  std::string proposed_name;
  std::string proposed_meaning;
  std::string proposed_domain;
  std::string proposed_quantity_kind;
  std::vector<std::string> proposed_aliases;
  std::optional<PhysicalDimensionVector> proposed_dimension;
  std::string proposed_unit_symbol;
  std::vector<std::string> assumptions;
  std::vector<std::string> falsifiers;
  std::string epistemic_status;
  std::string proposal_signature;
};

struct SemanticRevisionFalsifierResult final {
  std::string falsifier;
  std::string outcome;
  std::string evidence_id;

  SemanticRevisionFalsifierResult(std::string falsifier_value,
                                  std::string outcome_value,
                                  std::string evidence_id_value = {});
};

struct SemanticRequalification final {
  std::string source_concept_signature;
  std::optional<SemanticConcept> replacement_concept;
  std::string proposal_signature;
  std::vector<std::string> contradiction_signatures;
  std::vector<std::string> evidence_ids;
  std::vector<SemanticRevisionFalsifierResult> falsifier_results;
  bool independent_review = false;
  std::string status;
  bool canonical_replacement_eligible = false;
  std::string requalification_signature;
  std::vector<std::string> blocking_reasons;
};

struct SemanticRevisionDirective final {
  std::string subsystem;
  std::string action;
  bool blocking = false;
  std::string contradiction_signature;
  std::string rationale;
};

[[nodiscard]] SemanticContradiction detect_semantic_contradiction(
    const SemanticConcept &source_concept, std::string observed_statement,
    std::vector<std::string> evidence_ids,
    std::string observed_meaning = {},
    std::string observed_quantity_kind = {},
    std::optional<PhysicalDimensionVector> observed_dimension = std::nullopt,
    std::optional<PhysicalUnit> observed_unit = std::nullopt,
    int severity_bp = 7500);

[[nodiscard]] SemanticRevisionProposal propose_semantic_revision(
    const SemanticConcept &source_concept,
    const std::vector<SemanticContradiction> &contradictions,
    std::string meaning,
    std::optional<std::string> quantity_kind = std::nullopt,
    std::optional<std::string> name = std::nullopt,
    std::optional<std::string> domain = std::nullopt,
    std::optional<std::vector<std::string>> aliases = std::nullopt,
    std::optional<PhysicalDimensionVector> physical_dimension = std::nullopt,
    std::optional<PhysicalUnit> canonical_unit = std::nullopt,
    std::vector<std::string> assumptions = {},
    std::vector<std::string> falsifiers = {});

[[nodiscard]] SemanticRequalification requalify_semantic_revision(
    const SemanticEvidenceResolver &evidence_resolver,
    const SemanticConcept &source, const SemanticRevisionProposal &proposal,
    std::vector<std::string> evidence_ids,
    std::vector<SemanticRevisionFalsifierResult> falsifier_results,
    bool independent_review);

[[nodiscard]] std::vector<SemanticRevisionDirective>
propagate_semantic_contradiction(const SemanticContradiction &contradiction);

[[nodiscard]] contracts::Json to_json(const SemanticContradiction &value);
[[nodiscard]] contracts::Json to_json(const SemanticRevisionProposal &value);
[[nodiscard]] contracts::Json
to_json(const SemanticRevisionFalsifierResult &value);
[[nodiscard]] contracts::Json to_json(const SemanticRequalification &value);
[[nodiscard]] contracts::Json to_json(const SemanticRevisionDirective &value);

} // namespace statewright::saa
