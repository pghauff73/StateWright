#pragma once

#include "statewright/saa/nonlinear_evidence.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view nonlinear_global_version =
    "saa-nonlinear-global-v1";
inline constexpr std::size_t max_global_domain_dimension = 4U;
inline constexpr std::size_t max_global_cover_cells = 128U;
inline constexpr std::size_t max_global_elementary_cells = 4096U;

struct GlobalEquivalenceCell final {
  std::vector<mpq_class> lower;
  std::vector<mpq_class> upper;
  std::vector<mpq_class> output_delta_upper;
  std::string semantic_signature;
  std::string certificate_id;
};

struct GlobalNonlinearEquivalenceCertificate final {
  int schema_version = 1;
  std::string global_version = std::string(nonlinear_global_version);
  std::string status;
  std::string claim_scope;
  std::vector<mpq_class> domain_lower;
  std::vector<mpq_class> domain_upper;
  bool mathematical_equivalence = false;
  bool semantic_equivalence = false;
  bool complete_domain_coverage = false;
  std::vector<mpq_class> output_delta_upper;
  bool global_equivalence_eligible = false;
  std::string proof_kind;
  std::string certificate_signature;
  std::vector<std::string> warnings;
};

[[nodiscard]] contracts::Json to_json(const GlobalEquivalenceCell &value);
[[nodiscard]] contracts::Json
 to_json(const GlobalNonlinearEquivalenceCertificate &value);

[[nodiscard]] GlobalNonlinearEquivalenceCertificate
certify_exact_polynomial_global_equivalence(
    const ExactPolynomialSystem &left, const ExactPolynomialSystem &right,
    std::string left_semantic_signature, std::string right_semantic_signature,
    std::vector<NumericCoefficient> domain_lower,
    std::vector<NumericCoefficient> domain_upper);

[[nodiscard]] GlobalEquivalenceCell make_global_equivalence_cell(
    std::vector<NumericCoefficient> lower,
    std::vector<NumericCoefficient> upper,
    std::vector<NumericCoefficient> output_delta_upper,
    std::string semantic_signature, std::string certificate_id);

[[nodiscard]] GlobalNonlinearEquivalenceCertificate
certify_regional_global_equivalence(
    std::vector<GlobalEquivalenceCell> cells,
    std::vector<NumericCoefficient> domain_lower,
    std::vector<NumericCoefficient> domain_upper);

} // namespace statewright::saa
