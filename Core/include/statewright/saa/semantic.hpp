#pragma once

#include "statewright/saa/representative.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view semantic_version =
    "saa-semantic-representation-v1";

using SemanticMap = std::map<int, std::string>;

struct SemanticRepresentationIssue final {
  std::string issue_id;
  std::string issue_kind;
  std::string coordinate_kind;
  int coordinate_index = 0;
  std::string coordinate_label;
  std::vector<std::size_t> source_input_indices;
  RationalPolynomial source_coefficients;
  std::optional<std::string> declared_meaning;
  std::vector<std::size_t> affected_output_indices;
  std::vector<std::string> affected_output_meanings;
  std::string status;
  bool resolution_required = true;
  std::vector<std::string> questions;
  std::string source_representation_signature;
  std::string signature;
};

struct SemanticRepresentationAssessment final {
  int schema_version = 1;
  std::string semantic_version_value = std::string(semantic_version);
  RepresentationAssessment mathematical_assessment;
  std::string semantic_status;
  std::vector<SemanticRepresentationIssue> issues;
  bool mathematical_admission_eligible = false;
  bool canonical_admission_eligible = false;
  std::string assessment_signature;
  std::vector<std::string> warnings;
};

struct SemanticCandidateMeaning final {
  std::string candidate_id;
  std::string issue_id;
  std::string meaning;
  std::vector<std::size_t> expected_output_indices;
  std::vector<std::size_t> excluded_output_indices;
  std::vector<std::string> assumptions;
  std::vector<std::string> falsifiers;
  std::string epistemic_status;
  std::string signature;
};

struct SemanticFalsifierResult final {
  std::string falsifier;
  std::string outcome;
  std::optional<std::string> evidence_id;

  SemanticFalsifierResult(std::string falsifier_value,
                          std::string outcome_value,
                          std::optional<std::string> evidence_id_value =
                              std::nullopt);
};

struct SemanticResolution final {
  std::string issue_id;
  std::string candidate_id;
  std::string status;
  int semantic_fit_bp = 0;
  std::vector<std::string> evidence_ids;
  std::vector<SemanticFalsifierResult> falsifier_results;
  bool independent_review = false;
  bool canonical_semantic_eligible = false;
  std::string resolution_signature;
  std::vector<std::string> warnings;
};

struct SemanticPropagationDirective final {
  std::string issue_id;
  std::string subsystem;
  std::string action;
  bool blocking = false;
  bool question_required = true;
  contracts::Json payload = contracts::Json::object();
};

[[nodiscard]] contracts::Json to_json(const SemanticRepresentationIssue &value);
[[nodiscard]] contracts::Json
to_json(const SemanticRepresentationAssessment &value);
[[nodiscard]] contracts::Json to_json(const SemanticCandidateMeaning &value);
[[nodiscard]] contracts::Json to_json(const SemanticFalsifierResult &value);
[[nodiscard]] contracts::Json to_json(const SemanticResolution &value);
[[nodiscard]] contracts::Json to_json(const SemanticPropagationDirective &value);

[[nodiscard]] SemanticRepresentationAssessment assess_mimo_semantics(
    const CanonicalMIMOCoupling &mimo,
    const RepresentationAssessment *mathematical_assessment = nullptr,
    const SemanticMap &input_semantics = {},
    const SemanticMap &output_semantics = {});

[[nodiscard]] std::vector<SemanticRepresentationIssue>
assess_representative_candidate_semantics(
    const CanonicalMIMOCoupling &mimo,
    const RepresentativeInputSearch &search,
    const SemanticMap &input_semantics = {},
    const SemanticMap &output_semantics = {});

[[nodiscard]] SemanticCandidateMeaning make_semantic_candidate(
    const SemanticRepresentationIssue &issue, std::string meaning,
    std::vector<int> expected_output_indices,
    std::vector<int> excluded_output_indices = {},
    std::vector<std::string> assumptions = {},
    std::vector<std::string> falsifiers = {});

[[nodiscard]] SemanticResolution evaluate_semantic_candidate(
    const SemanticRepresentationIssue &issue,
    const SemanticCandidateMeaning &candidate,
    std::vector<std::string> evidence_ids = {},
    std::vector<SemanticFalsifierResult> falsifier_results = {},
    bool independent_review = false);

[[nodiscard]] bool canonical_semantic_admission(
    bool mathematical_eligible,
    const std::vector<SemanticRepresentationIssue> &issues,
    const std::vector<SemanticResolution> &resolutions);

[[nodiscard]] std::vector<SemanticPropagationDirective>
propagate_semantic_issues(
    const std::vector<SemanticRepresentationIssue> &issues);

[[nodiscard]] std::vector<std::string> semantic_followup_questions(
    const std::vector<SemanticRepresentationIssue> &issues);

} // namespace statewright::saa
