#pragma once

#include "statewright/saa/algorithm_ir.hpp"
#include "statewright/saa/normalization.hpp"
#include "statewright/saa/representative.hpp"
#include "statewright/saa/semantic.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view canonical_representative_version =
    "saa-canonical-representative-v1";
inline constexpr std::string_view representative_bound_policy =
    "EXACT_LINEAR_IMAGE_OF_NORMALIZED_SOURCE_BOX";
inline constexpr std::string_view structural_binding_policy =
    "CONSERVATIVE_SOURCE_STRUCTURE_BINDING";

struct RepresentativeCoordinateBoundary final {
  std::size_t candidate_input_index = 0;
  mpq_class raw_minimum;
  mpq_class raw_maximum;
  mpq_class raw_width;
  mpq_class normalized_minimum;
  mpq_class normalized_maximum;
  std::string bound_policy;
  std::string source_normalization_signature;
  std::string semantic_resolution_signature;
  std::string boundary_signature;

  RepresentativeCoordinateBoundary(
      std::size_t candidate_input_index_value, mpq_class raw_minimum_value,
      mpq_class raw_maximum_value, mpq_class raw_width_value,
      mpq_class normalized_minimum_value, mpq_class normalized_maximum_value,
      std::string bound_policy_value,
      std::string source_normalization_signature_value,
      std::string semantic_resolution_signature_value,
      std::string boundary_signature_value);

  [[nodiscard]] mpq_class normalize(const mpq_class &value) const;
  [[nodiscard]] mpq_class denormalize(const mpq_class &value) const;
};

struct CanonicalRepresentativeInput final {
  std::size_t canonical_position = 0;
  std::size_t candidate_input_index = 0;
  std::size_t paired_output_index = 0;
  std::string meaning;
  std::string canonical_meaning;
  std::vector<std::size_t> expected_output_indices;
  std::vector<std::size_t> excluded_output_indices;
  std::vector<std::size_t> source_input_indices;
  RationalPolynomial source_coefficients;
  std::string semantic_issue_id;
  std::string semantic_candidate_id;
  std::string semantic_candidate_signature;
  std::string semantic_resolution_signature;
  std::vector<std::string> semantic_evidence_ids;
  RepresentativeCoordinateBoundary boundary;
};

struct CanonicalRepresentativeAlgorithmForm final {
  int schema_version = 1;
  std::string representative_version_value =
      std::string(canonical_representative_version);
  std::string domain;
  std::string variable;
  std::size_t output_count = 0;
  std::size_t representative_input_count = 0;
  std::optional<mpq_class> normalized_sample_interval;
  std::string source_mimo_signature;
  std::string source_normalization_signature;
  std::string source_structural_hash;
  std::string source_structural_strength;
  std::string representative_search_audit_hash;
  std::string representative_candidate_signature;
  std::vector<std::size_t> canonical_input_permutation;
  std::vector<CanonicalRepresentativeInput> inputs;
  std::vector<std::vector<RationalChannel>> normalized_channels;
  std::string mathematical_representative_signature;
  std::string semantic_representative_signature;
  std::string representative_behavior_signature;
  std::string canonical_algorithm_signature;
  std::string structural_binding_policy_value;
  bool canonical_admission_eligible = false;
  std::string store_status;
  std::string audit_hash;
  std::vector<std::string> warnings;
};

[[nodiscard]] contracts::Json
to_json(const RepresentativeCoordinateBoundary &value);
[[nodiscard]] contracts::Json to_json(const CanonicalRepresentativeInput &value);
[[nodiscard]] contracts::Json
to_json(const CanonicalRepresentativeAlgorithmForm &value);

[[nodiscard]] CanonicalRepresentativeAlgorithmForm
canonicalize_representative_algorithm(
    const CanonicalAlgorithmIR &structural_ir,
    const NormalizationContract &source_normalization,
    const CanonicalMIMOCoupling &mimo,
    const RepresentativeInputSearch &representative_search,
    const std::vector<SemanticRepresentationIssue> &semantic_issues,
    const std::vector<SemanticCandidateMeaning> &semantic_candidates,
    const std::vector<SemanticResolution> &semantic_resolutions);

[[nodiscard]] mpq_class normalize_representative_value(
    const RepresentativeCoordinateBoundary &boundary,
    const mpq_class &value);
[[nodiscard]] mpq_class denormalize_representative_value(
    const RepresentativeCoordinateBoundary &boundary,
    const mpq_class &value);

} // namespace statewright::saa
