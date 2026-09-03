#pragma once

#include "statewright/saa/mimo.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view representation_version =
    "saa-representative-inputs-v1";
inline constexpr std::size_t max_rank_vector_terms = 16384U;
inline constexpr std::size_t max_representative_transforms = 4096U;
inline constexpr std::size_t max_transform_coefficient_bits = 256U;

struct MinimalityAssessment final {
  std::size_t source_input_count = 0;
  std::size_t effective_input_rank = 0;
  std::size_t redundant_input_count = 0;
  std::vector<std::size_t> pivot_input_positions;
  std::vector<std::size_t> nonpivot_input_positions;
  RationalMatrix source_to_basis_projection;
  std::string status;
  bool exact = false;
};

struct RepresentationAssessment final {
  int schema_version = 1;
  std::string representation_version_value =
      std::string(representation_version);
  std::string status;
  std::string reason;
  std::optional<int> coupling_bp;
  std::optional<std::vector<std::size_t>>
      preferred_input_to_output_pairing;
  bool canonical_admission_eligible = false;
  bool requires_representative_search = true;
  std::optional<MinimalityAssessment> minimality;
  std::string assessment_signature;
  std::vector<std::string> warnings;
};

struct TransformAdmissibility final {
  std::string status;
  bool admissible = false;
  bool causal = true;
  bool stable = true;
  bool finite_real = true;
  std::string invertibility_status;
  std::size_t coefficient_bits = 1;
  std::size_t coefficient_bit_limit = max_transform_coefficient_bits;
  std::vector<std::string> warnings;
};

struct RepresentativeInputCandidate final {
  std::string candidate_id;
  std::string status;
  std::string transform_class;
  std::optional<mpq_class> algebraic_probe;
  std::vector<std::size_t> selected_output_rows;
  std::size_t source_input_count = 0;
  std::size_t representative_input_count = 0;
  RationalMatrix source_to_representative_projection;
  RationalMatrix representative_to_source_section;
  RationalMatrix basis_transform;
  std::vector<std::vector<RationalChannel>> representative_channels;
  int coupling_before_bp = 0;
  int coupling_after_bp = 0;
  std::optional<std::vector<std::size_t>>
      preferred_input_to_output_pairing;
  bool exact_decoupled = false;
  bool independent = false;
  bool minimal = false;
  bool requires_renormalization = false;
  TransformAdmissibility admissibility;
  std::string canonical_signature;
  std::vector<std::string> warnings;
};

struct RepresentativeInputSearch final {
  int schema_version = 1;
  std::string representation_version_value =
      std::string(representation_version);
  RepresentationAssessment source_assessment;
  std::optional<MinimalityAssessment> minimality;
  std::string search_status;
  std::size_t candidates_considered = 0;
  std::vector<RepresentativeInputCandidate> candidates;
  std::optional<RepresentativeInputCandidate> best_candidate;
  std::string audit_hash;
  std::vector<std::string> warnings;

  [[nodiscard]] bool representative_found() const noexcept;
};

[[nodiscard]] contracts::Json to_json(const MinimalityAssessment &value);
[[nodiscard]] contracts::Json to_json(const RepresentationAssessment &value);
[[nodiscard]] contracts::Json to_json(const TransformAdmissibility &value);
[[nodiscard]] contracts::Json to_json(const RepresentativeInputCandidate &value);
[[nodiscard]] contracts::Json to_json(const RepresentativeInputSearch &value);

[[nodiscard]] RepresentationAssessment assess_mimo_representation(
    const CanonicalMIMOCoupling &mimo,
    std::size_t rank_term_budget = max_rank_vector_terms);

[[nodiscard]] RepresentativeInputSearch discover_representative_inputs(
    const CanonicalMIMOCoupling &mimo,
    std::size_t rank_term_budget = max_rank_vector_terms,
    std::size_t transform_budget = max_representative_transforms,
    std::size_t coefficient_bit_budget = max_transform_coefficient_bits);

} // namespace statewright::saa
