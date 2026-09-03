#pragma once

#include "statewright/saa/nonlinear_search.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view nonlinear_transform_version =
    "saa-exact-polynomial-automorphism-v1";
inline constexpr std::size_t max_polynomial_shear_terms = 32U;
inline constexpr int max_polynomial_automorphism_depth = 4;
inline constexpr int max_polynomial_automorphism_candidates = 128;
inline constexpr std::size_t max_polynomial_transform_coefficient_bits = 48U;

struct PolynomialShearTerm final {
  std::vector<int> powers;
  NumericCoefficient coefficient = 0;
};

struct CanonicalPolynomialShearTerm final {
  std::vector<int> powers;
  mpq_class coefficient;
};

struct ExactPolynomialShear final {
  std::size_t input_count = 0U;
  std::size_t target_input_index = 0U;
  std::vector<CanonicalPolynomialShearTerm> terms;
  std::string transform_signature;
};

struct ExactPolynomialAutomorphism final {
  std::size_t input_count = 0U;
  std::vector<ExactPolynomialShear> transforms;
  std::string automorphism_signature;
};

struct PolynomialSemanticIssue final {
  std::string issue_id;
  std::size_t coordinate_index = 0U;
  std::vector<std::size_t> affected_output_indices;
  std::vector<std::size_t> source_input_indices;
  std::string previous_meaning;
  std::vector<std::string> transform_signatures;
  std::string status;
  std::string signature;
  std::vector<std::string> questions;
};

struct PolynomialAutomorphismCandidate final {
  CanonicalTaylorJet transformed_jet;
  std::vector<ExactPolynomialShear> transforms;
  std::size_t source_coupling_score = 0U;
  std::size_t coupling_score = 0U;
  bool exact_invertible = true;
  bool mathematical_eligible = false;
  std::vector<PolynomialSemanticIssue> semantic_issues;
  std::string candidate_signature;
};

struct PolynomialAutomorphismSearch final {
  int schema_version = 1;
  std::string transform_version = std::string(nonlinear_transform_version);
  std::string source_jet_signature;
  std::string status;
  bool representative_found = false;
  PolynomialAutomorphismCandidate best_candidate;
  int candidates_evaluated = 0;
  int search_depth = 0;
  bool budget_exhausted = false;
  std::string audit_hash;
  std::vector<std::string> warnings;
};

[[nodiscard]] ExactPolynomialShear make_polynomial_shear(
    std::size_t input_count, std::size_t target_input_index,
    const std::vector<PolynomialShearTerm> &terms);
[[nodiscard]] CanonicalTaylorJet apply_polynomial_shear(
    const CanonicalRepresentativeAlgorithmForm &form,
    const CanonicalTaylorJet &jet, const ExactPolynomialShear &transform);
[[nodiscard]] ExactPolynomialAutomorphism make_polynomial_automorphism(
    std::vector<ExactPolynomialShear> transforms);
[[nodiscard]] CanonicalTaylorJet apply_polynomial_automorphism(
    const CanonicalRepresentativeAlgorithmForm &form,
    const CanonicalTaylorJet &jet,
    const ExactPolynomialAutomorphism &automorphism);
[[nodiscard]] PolynomialAutomorphismSearch search_polynomial_automorphisms(
    const CanonicalRepresentativeAlgorithmForm &form,
    const CanonicalTaylorJet &jet,
    int max_candidates = max_polynomial_automorphism_candidates,
    int max_depth = max_polynomial_automorphism_depth);

[[nodiscard]] SemanticRepresentationIssue
as_semantic_issue(const PolynomialSemanticIssue &issue);
[[nodiscard]] SemanticCandidateMeaning make_semantic_candidate(
    const PolynomialSemanticIssue &issue, std::string meaning,
    std::vector<int> expected_output_indices,
    std::vector<int> excluded_output_indices = {},
    std::vector<std::string> assumptions = {},
    std::vector<std::string> falsifiers = {});
[[nodiscard]] SemanticResolution evaluate_semantic_candidate(
    const PolynomialSemanticIssue &issue,
    const SemanticCandidateMeaning &candidate,
    std::vector<std::string> evidence_ids = {},
    std::vector<SemanticFalsifierResult> falsifier_results = {},
    bool independent_review = false);

[[nodiscard]] CanonicalNonlinearRepresentativeForm
canonicalize_polynomial_representative(
    const CanonicalRepresentativeAlgorithmForm &form,
    const PolynomialAutomorphismSearch &search,
    const std::vector<SemanticCandidateMeaning> &semantic_candidates = {},
    const std::vector<SemanticResolution> &semantic_resolutions = {});

[[nodiscard]] contracts::Json to_json(const PolynomialShearTerm &value);
[[nodiscard]] contracts::Json
to_json(const CanonicalPolynomialShearTerm &value);
[[nodiscard]] contracts::Json to_json(const ExactPolynomialShear &value);
[[nodiscard]] contracts::Json to_json(const ExactPolynomialAutomorphism &value);
[[nodiscard]] contracts::Json to_json(const PolynomialSemanticIssue &value);
[[nodiscard]] contracts::Json
to_json(const PolynomialAutomorphismCandidate &value);
[[nodiscard]] contracts::Json
to_json(const PolynomialAutomorphismSearch &value);

} // namespace statewright::saa
