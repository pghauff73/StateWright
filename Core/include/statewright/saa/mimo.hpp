#pragma once

#include "statewright/saa/algorithm_ir.hpp"
#include "statewright/saa/dynamics.hpp"
#include "statewright/saa/normalization.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view mimo_version = "saa-mimo-coupling-v1";
inline constexpr std::size_t max_mimo_inputs = 6U;
inline constexpr std::size_t max_mimo_outputs = 6U;
inline constexpr std::size_t max_port_permutations = 4096U;

using TransferFunctionMatrix =
    std::vector<std::vector<LinearTransferFunction>>;
using CanonicalDynamicsMatrix =
    std::vector<std::vector<CanonicalLinearDynamics>>;
using OptionalRationalMatrix =
    std::vector<std::vector<std::optional<mpq_class>>>;
using BooleanMatrix = std::vector<std::vector<bool>>;

struct MIMOTransferMatrix final {
  std::string domain;
  TransferFunctionMatrix channels;
  contracts::Json metadata = contracts::Json::object();

  MIMOTransferMatrix(std::string domain_value,
                     TransferFunctionMatrix channels_value,
                     contracts::Json metadata_value =
                         contracts::Json::object());
};

struct RationalChannel final {
  RationalPolynomial numerator;
  RationalPolynomial denominator;

  [[nodiscard]] bool zero() const noexcept;
};

struct ResidualCouplingSample final {
  double coordinate = 0.0;
  double ratio = 0.0;
};

struct StaticDecouplingResult final {
  RationalMatrix decoupler;
  std::vector<std::vector<RationalChannel>> decoupled_channels;
  std::string canonical_signature;
  std::vector<ResidualCouplingSample> residual_coupling_samples;
};

struct CanonicalMIMOCoupling final {
  int schema_version = 1;
  std::string mimo_version_value = std::string(mimo_version);
  std::string domain;
  std::string variable;
  std::size_t output_count = 0;
  std::size_t input_count = 0;
  CanonicalDynamicsMatrix channels;
  std::string dynamic_strength;
  std::string coupling_strength;
  std::optional<mpq_class> normalized_sample_interval;
  std::string ordered_signature;
  std::optional<std::string> permutation_invariant_signature;
  std::string permutation_strength;
  std::optional<std::vector<std::size_t>> canonical_output_permutation;
  std::optional<std::vector<std::size_t>> canonical_input_permutation;
  BooleanMatrix nonzero_pattern;
  bool permutation_decoupled = false;
  std::optional<std::vector<std::size_t>> exact_diagonal_input_permutation;
  OptionalRationalMatrix steady_gain;
  std::optional<RationalMatrix> relative_gain_array;
  std::optional<std::vector<std::size_t>> preferred_rga_pairing;
  std::optional<mpq_class> rga_off_pairing_mass;
  std::optional<StaticDecouplingResult> static_decoupling;
  std::string audit_hash;
  std::vector<std::string> warnings;
};

[[nodiscard]] contracts::Json to_json(const RationalChannel &value);
[[nodiscard]] contracts::Json to_json(const StaticDecouplingResult &value);
[[nodiscard]] contracts::Json to_json(const CanonicalMIMOCoupling &value);

[[nodiscard]] std::optional<RationalMatrix>
invert_rational_matrix(const RationalMatrix &matrix);
[[nodiscard]] RationalChannel scale_rational_channel(
    const RationalChannel &channel, const mpq_class &scalar);
[[nodiscard]] RationalChannel add_rational_channels(
    const RationalChannel &first, const RationalChannel &second);

[[nodiscard]] CanonicalMIMOCoupling canonicalize_mimo_transfer_matrix(
    const MIMOTransferMatrix &transfer_matrix,
    const NormalizationContract &normalization,
    std::size_t permutation_budget = max_port_permutations);

[[nodiscard]] std::string mimo_algorithm_signature(
    const CanonicalAlgorithmIR &structural_ir,
    const NormalizationContract &normalization,
    const CanonicalMIMOCoupling &mimo, bool ignore_port_order = false);

} // namespace statewright::saa
