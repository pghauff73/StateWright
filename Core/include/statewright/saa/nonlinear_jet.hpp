#pragma once

#include "statewright/saa/dynamics.hpp"
#include "statewright/saa/representative_form.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view nonlinear_jet_version =
    "saa-nonlinear-jet-v1";
inline constexpr std::size_t max_jet_order = 4U;
inline constexpr std::size_t max_jet_inputs = 8U;
inline constexpr std::size_t max_jet_outputs = 8U;
inline constexpr std::size_t max_jet_terms = 512U;

struct TaylorJetTerm final {
  std::size_t output_index = 0U;
  std::vector<int> powers;
  NumericCoefficient coefficient = 0;

  [[nodiscard]] int degree() const;
};

struct TaylorJetSpec final {
  std::size_t input_count = 0U;
  std::size_t output_count = 0U;
  std::size_t order = 0U;
  std::vector<NumericCoefficient> center;
  std::vector<NumericCoefficient> validity_radius;
  std::vector<TaylorJetTerm> terms;
};

struct NonlinearCouplingTerm final {
  std::size_t output_index = 0U;
  std::vector<int> powers;
  mpq_class coefficient;
  std::vector<std::size_t> input_support;
  std::string reason;
};

struct NonlinearCouplingAssessment final {
  std::string status;
  std::vector<std::vector<std::size_t>> dependency_by_input;
  std::vector<NonlinearCouplingTerm> cross_terms;
  std::vector<NonlinearCouplingTerm> off_pair_terms;
  std::size_t coupling_score = 0U;
  bool representative = false;
  std::string signature;
};

struct CanonicalTaylorJetTerm final {
  std::size_t output_index = 0U;
  std::vector<int> powers;
  mpq_class coefficient;

  [[nodiscard]] int degree() const;
  bool operator==(const CanonicalTaylorJetTerm &) const = default;
};

struct CanonicalTaylorJet final {
  int schema_version = 1;
  std::string jet_version = std::string(nonlinear_jet_version);
  std::string parent_representative_behavior_signature;
  std::string parent_semantic_signature;
  std::size_t input_count = 0U;
  std::size_t output_count = 0U;
  std::size_t order = 0U;
  std::vector<mpq_class> center;
  std::vector<mpq_class> validity_radius;
  std::vector<CanonicalTaylorJetTerm> terms;
  std::string coefficient_signature;
  std::string scope_signature;
  std::string local_behavior_signature;
  NonlinearCouplingAssessment coupling;
  std::string local_equivalence_scope;
  bool global_equivalence_eligible = false;
  bool exact = true;
  std::vector<std::string> warnings;

  [[nodiscard]] std::vector<mpq_class>
  evaluate(const std::vector<NumericCoefficient> &values) const;
};

struct LocalJetComparison final {
  std::string status;
  bool coefficient_match = false;
  bool same_expansion_point = false;
  std::vector<mpq_class> overlap_radius;
  bool global_equivalence_eligible = false;
  std::string signature;
};

[[nodiscard]] contracts::Json to_json(const TaylorJetTerm &value);
[[nodiscard]] contracts::Json to_json(const CanonicalTaylorJetTerm &value);
[[nodiscard]] contracts::Json to_json(const NonlinearCouplingTerm &value);
[[nodiscard]] contracts::Json
 to_json(const NonlinearCouplingAssessment &value);
[[nodiscard]] contracts::Json to_json(const CanonicalTaylorJet &value);
[[nodiscard]] contracts::Json to_json(const LocalJetComparison &value);

[[nodiscard]] NonlinearCouplingAssessment assess_nonlinear_coupling(
    const CanonicalRepresentativeAlgorithmForm &form,
    const std::vector<CanonicalTaylorJetTerm> &terms);
[[nodiscard]] CanonicalTaylorJet canonicalize_taylor_jet(
    const CanonicalRepresentativeAlgorithmForm &form, const TaylorJetSpec &spec);
[[nodiscard]] LocalJetComparison compare_taylor_jets(
    const CanonicalTaylorJet &left, const CanonicalTaylorJet &right);

} // namespace statewright::saa
