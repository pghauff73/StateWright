#pragma once

#include "statewright/saa/nonlinear_jet.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view nonlinear_representation_version =
    "saa-nonlinear-representation-v1";
inline constexpr int max_nonlinear_search_candidates = 128;
inline constexpr int max_nonlinear_search_depth = 2;
inline constexpr std::size_t max_nonlinear_transform_coefficient_bits = 32U;

struct NonlinearShearTransform final {
  std::size_t target_input_index = 0U;
  std::vector<int> monomial_powers;
  mpq_class coefficient;
  std::string transform_signature;

  [[nodiscard]] int degree() const;
};

struct NonlinearSemanticRepresentationIssue final {
  std::string issue_id;
  std::string issue_kind;
  std::string coordinate_kind;
  std::size_t coordinate_index = 0U;
  std::string coordinate_label;
  std::string previous_meaning;
  std::vector<std::string> transform_signatures;
  std::vector<std::size_t> source_input_indices;
  std::vector<std::size_t> affected_output_indices;
  std::string status;
  bool resolution_required = true;
  std::vector<std::string> questions;
  std::string source_representation_signature;
  std::string signature;
};

struct NonlinearRepresentativeCandidate final {
  std::string source_jet_signature;
  CanonicalTaylorJet transformed_jet;
  std::vector<NonlinearShearTransform> transforms;
  std::size_t source_coupling_score = 0U;
  std::size_t coupling_score = 0U;
  bool exact_invertible = true;
  bool mathematical_eligible = false;
  std::vector<NonlinearSemanticRepresentationIssue> semantic_issues;
  bool local_canonical_eligible = false;
  std::string candidate_signature;
};

struct NonlinearRepresentativeSearch final {
  int schema_version = 1;
  std::string representation_version =
      std::string(nonlinear_representation_version);
  std::string source_jet_signature;
  std::string status;
  bool representative_found = false;
  std::optional<NonlinearRepresentativeCandidate> best_candidate;
  int candidates_evaluated = 0;
  int search_depth = 0;
  int search_budget = max_nonlinear_search_candidates;
  bool budget_exhausted = false;
  std::vector<std::string> explored_signatures;
  std::string audit_hash;
  std::vector<std::string> warnings;
};

struct CanonicalNonlinearRepresentativeForm final {
  int schema_version = 1;
  std::string representation_version =
      std::string(nonlinear_representation_version);
  std::string parent_representative_behavior_signature;
  std::string source_jet_signature;
  CanonicalTaylorJet transformed_jet;
  std::vector<std::string> transform_signatures;
  std::vector<std::string> resolved_input_meanings;
  std::string semantic_signature;
  std::string local_representative_behavior_signature;
  bool local_canonical_eligible = false;
  bool global_equivalence_eligible = false;
  std::string store_status;
  std::string audit_hash;
  std::vector<std::string> warnings;
};

[[nodiscard]] NonlinearShearTransform make_nonlinear_shear(
    std::size_t target_input_index, std::vector<int> monomial_powers,
    NumericCoefficient coefficient);
[[nodiscard]] CanonicalTaylorJet apply_nonlinear_shear(
    const CanonicalRepresentativeAlgorithmForm &form,
    const CanonicalTaylorJet &jet, const NonlinearShearTransform &transform);
[[nodiscard]] NonlinearRepresentativeSearch
search_nonlinear_representative_coordinates(
    const CanonicalRepresentativeAlgorithmForm &form,
    const CanonicalTaylorJet &jet,
    int max_candidates = max_nonlinear_search_candidates,
    int max_depth = max_nonlinear_search_depth);

[[nodiscard]] SemanticRepresentationIssue as_semantic_issue(
    const NonlinearSemanticRepresentationIssue &issue);
[[nodiscard]] SemanticCandidateMeaning make_semantic_candidate(
    const NonlinearSemanticRepresentationIssue &issue, std::string meaning,
    std::vector<int> expected_output_indices,
    std::vector<int> excluded_output_indices = {},
    std::vector<std::string> assumptions = {},
    std::vector<std::string> falsifiers = {});
[[nodiscard]] SemanticResolution evaluate_semantic_candidate(
    const NonlinearSemanticRepresentationIssue &issue,
    const SemanticCandidateMeaning &candidate,
    std::vector<std::string> evidence_ids = {},
    std::vector<SemanticFalsifierResult> falsifier_results = {},
    bool independent_review = false);
[[nodiscard]] std::vector<SemanticPropagationDirective>
propagate_semantic_issues(
    const std::vector<NonlinearSemanticRepresentationIssue> &issues);

[[nodiscard]] CanonicalNonlinearRepresentativeForm
canonicalize_nonlinear_representative(
    const CanonicalRepresentativeAlgorithmForm &form,
    const NonlinearRepresentativeSearch &search,
    const std::vector<SemanticCandidateMeaning> &semantic_candidates = {},
    const std::vector<SemanticResolution> &semantic_resolutions = {});

[[nodiscard]] contracts::Json to_json(const NonlinearShearTransform &value);
[[nodiscard]] contracts::Json
to_json(const NonlinearSemanticRepresentationIssue &value);
[[nodiscard]] contracts::Json
to_json(const NonlinearRepresentativeCandidate &value);
[[nodiscard]] contracts::Json
to_json(const NonlinearRepresentativeSearch &value);
[[nodiscard]] contracts::Json
to_json(const CanonicalNonlinearRepresentativeForm &value);

} // namespace statewright::saa
