#pragma once

#include "statewright/saa/algorithm_ir.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view normalizer_version =
    "saa-normalization-v1";

using ProvenanceItems = std::vector<std::pair<std::string, std::string>>;

struct NumericBound final {
  double minimum;
  double maximum;
  std::string kind;
  std::string unit;
  ProvenanceItems provenance;

  NumericBound(double minimum_value, double maximum_value,
               std::string kind_value = "EXACT_BOUND",
               std::string unit_value = {},
               ProvenanceItems provenance_value = {});

  [[nodiscard]] double width() const noexcept;
  [[nodiscard]] std::string strength() const;
  [[nodiscard]] contracts::Json audit_payload() const;
  [[nodiscard]] contracts::Json canonical_payload() const;
};

struct NormalizationBinding final {
  std::string role;
  int position;
  std::string data_type;
  std::vector<int> shape;
  NumericBound bound;

  NormalizationBinding(std::string role_value, int position_value,
                       std::string data_type_value,
                       std::vector<int> shape_value, NumericBound bound_value);

  [[nodiscard]] std::string canonical_data_type() const;
  [[nodiscard]] std::string strength() const;
  [[nodiscard]] contracts::Json audit_payload() const;
  [[nodiscard]] contracts::Json canonical_payload() const;
};

struct TimeNormalization final {
  double characteristic_time;
  std::string kind;
  std::string unit;
  ProvenanceItems provenance;

  explicit TimeNormalization(double characteristic_time_value,
                             std::string kind_value = "EXACT_BOUND",
                             std::string unit_value = {},
                             ProvenanceItems provenance_value = {});

  [[nodiscard]] std::string strength() const;
  [[nodiscard]] contracts::Json audit_payload() const;
  [[nodiscard]] contracts::Json canonical_payload() const;
};

struct NormalizationContract final {
  int schema_version = 1;
  std::string normalizer_version_value = std::string(normalizer_version);
  std::vector<NormalizationBinding> bindings;
  std::optional<TimeNormalization> time;
  std::string normalization_strength;
  std::string contract_hash;
  std::string canonical_signature;
  std::vector<std::string> warnings;

  [[nodiscard]] const NormalizationBinding &binding(std::string_view role,
                                                    int position) const;
};

using BoundMap = std::map<int, NumericBound>;

[[nodiscard]] NumericBound numeric_bound(const contracts::Json &value);
[[nodiscard]] std::optional<TimeNormalization>
time_normalization(const contracts::Json &value);
[[nodiscard]] double normalize_value(const NumericBound &bound, double value);
[[nodiscard]] double denormalize_value(const NumericBound &bound, double value);
[[nodiscard]] double normalize_time(const TimeNormalization &contract,
                                    double value);
[[nodiscard]] double denormalize_time(const TimeNormalization &contract,
                                      double value);

[[nodiscard]] NormalizationContract build_normalization_contract(
    const AlgorithmStructureSpec &spec, const BoundMap &input_bounds = {},
    const BoundMap &parameter_bounds = {}, const BoundMap &state_bounds = {},
    const BoundMap &output_bounds = {},
    std::optional<TimeNormalization> time = std::nullopt);

[[nodiscard]] std::vector<double>
normalize_role(const NormalizationContract &contract, std::string_view role,
               const std::vector<double> &values);
[[nodiscard]] std::vector<double>
denormalize_role(const NormalizationContract &contract, std::string_view role,
                 const std::vector<double> &values);
[[nodiscard]] std::string
normalized_algorithm_signature(const CanonicalAlgorithmIR &structural_ir,
                               const NormalizationContract &contract);

[[nodiscard]] contracts::Json to_json(const NormalizationContract &contract);

} // namespace statewright::saa
