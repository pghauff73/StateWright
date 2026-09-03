#include "statewright/saa/normalization.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

const std::set<std::string> bound_kinds = {
    "APPROXIMATE_BOUND", "DOMAIN_BOUND", "ENGINEERING_BOUND", "EXACT_BOUND",
    "OBSERVED_BOUND"};
const std::set<std::string> exact_bound_kinds = {"DOMAIN_BOUND", "EXACT_BOUND"};
const std::map<std::string, std::string> numeric_classes = {
    {"float", "CONTINUOUS_SCALAR"}, {"int", "INTEGER_SCALAR"},
    {"integer", "INTEGER_SCALAR"},  {"number", "CONTINUOUS_SCALAR"},
    {"real", "CONTINUOUS_SCALAR"}, {"scalar", "CONTINUOUS_SCALAR"}};
const std::map<std::string, int> role_order = {
    {"INPUT", 0}, {"PARAMETER", 1}, {"STATE", 2}, {"OUTPUT", 3}};

[[noreturn]] void normalization_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument,
                      std::move(message));
}

[[nodiscard]] std::string trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n\f\v");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n\f\v");
  return value.substr(first, last - first + 1U);
}

[[nodiscard]] std::string uppercase(std::string value) {
  value = trim(std::move(value));
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::toupper(character));
                 });
  return value;
}

[[nodiscard]] std::string lowercase(std::string value) {
  value = trim(std::move(value));
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

[[nodiscard]] double finite(double value, std::string_view label) {
  if (!std::isfinite(value)) {
    normalization_error(std::string(label) + " must be finite");
  }
  return value == 0.0 ? 0.0 : value;
}

void normalize_provenance(ProvenanceItems &items) {
  for (auto &[key, value] : items) {
    key = trim(std::move(key));
    if (key.empty()) {
      normalization_error("normalization provenance keys must be non-empty");
    }
    value = trim(std::move(value));
  }
  std::sort(items.begin(), items.end());
}

[[nodiscard]] Json provenance_json(const ProvenanceItems &items) {
  Json result = Json::array();
  for (const auto &[key, value] : items) {
    result.push_back(Json::array({key, value}));
  }
  return result;
}

template <typename Spec>
[[nodiscard]] std::vector<NormalizationBinding>
build_bindings(std::string role, const std::vector<Spec> &specs,
               const BoundMap &bounds) {
  std::set<int> expected;
  for (const auto &item : specs) {
    expected.insert(item.position);
  }
  for (const auto &[position, ignored] : bounds) {
    static_cast<void>(ignored);
    if (!expected.contains(position)) {
      normalization_error("normalization contains unknown " + role +
                          " position: " + std::to_string(position));
    }
  }
  for (const int position : expected) {
    if (!bounds.contains(position)) {
      normalization_error("normalization is missing " + role + " position: " +
                          std::to_string(position));
    }
  }
  std::vector<NormalizationBinding> result;
  result.reserve(specs.size());
  std::vector<const Spec *> ordered;
  ordered.reserve(specs.size());
  for (const auto &item : specs) {
    ordered.push_back(&item);
  }
  std::sort(ordered.begin(), ordered.end(), [](const Spec *left, const Spec *right) {
    return left->position < right->position;
  });
  for (const auto *item : ordered) {
    result.emplace_back(role, item->position, item->data_type, item->shape,
                        bounds.at(item->position));
  }
  return result;
}

[[nodiscard]] std::string contract_strength(
    const std::vector<NormalizationBinding> &bindings,
    const std::optional<TimeNormalization> &time) {
  std::set<std::string> strengths;
  for (const auto &binding : bindings) {
    strengths.insert(binding.strength());
  }
  if (time) {
    strengths.insert(time->strength());
  }
  if (strengths.empty() || strengths == std::set<std::string>{"EXACT"}) {
    return "EXACT_NORMALIZATION";
  }
  if (strengths == std::set<std::string>{"APPROXIMATE"}) {
    return "APPROXIMATE_NORMALIZATION";
  }
  return "MIXED_NORMALIZATION";
}

[[nodiscard]] std::vector<const NormalizationBinding *>
role_bindings(const NormalizationContract &contract, std::string_view role) {
  const std::string normalized = uppercase(std::string(role));
  if (!role_order.contains(normalized)) {
    normalization_error("unsupported normalization role: " +
                        std::string(role));
  }
  std::vector<const NormalizationBinding *> result;
  for (const auto &binding : contract.bindings) {
    if (binding.role == normalized) {
      result.push_back(&binding);
    }
  }
  return result;
}

} // namespace

NumericBound::NumericBound(double minimum_value, double maximum_value,
                           std::string kind_value, std::string unit_value,
                           ProvenanceItems provenance_value)
    : minimum(finite(minimum_value, "normalization minimum")),
      maximum(finite(maximum_value, "normalization maximum")),
      kind(uppercase(std::move(kind_value))), unit(trim(std::move(unit_value))),
      provenance(std::move(provenance_value)) {
  if (!bound_kinds.contains(kind)) {
    normalization_error("unsupported normalization bound kind: " + kind);
  }
  if (maximum <= minimum) {
    normalization_error(
        "normalization maximum must be greater than minimum");
  }
  normalize_provenance(provenance);
}

double NumericBound::width() const noexcept { return maximum - minimum; }

std::string NumericBound::strength() const {
  return exact_bound_kinds.contains(kind) ? "EXACT" : "APPROXIMATE";
}

Json NumericBound::audit_payload() const {
  return {{"kind", kind},
          {"maximum", maximum},
          {"minimum", minimum},
          {"provenance", provenance_json(provenance)},
          {"unit", unit}};
}

Json NumericBound::canonical_payload() const {
  return {{"strength", strength()},
          {"target", Json::array({0.0, 1.0})},
          {"transform", "AFFINE_REVERSIBLE"}};
}

NormalizationBinding::NormalizationBinding(
    std::string role_value, int position_value, std::string data_type_value,
    std::vector<int> shape_value, NumericBound bound_value)
    : role(uppercase(std::move(role_value))), position(position_value),
      data_type(lowercase(std::move(data_type_value))),
      shape(std::move(shape_value)), bound(std::move(bound_value)) {
  if (!role_order.contains(role)) {
    normalization_error("unsupported normalization role: " + role);
  }
  if (position < 0) {
    normalization_error("normalization binding position cannot be negative");
  }
  if (!numeric_classes.contains(data_type)) {
    normalization_error("SAA-2 supports scalar numeric coordinates only, not " +
                        data_type);
  }
  if (!shape.empty()) {
    normalization_error(
        "SAA-2 scalar normalization does not yet support shaped/vector ports");
  }
}

std::string NormalizationBinding::canonical_data_type() const {
  return numeric_classes.at(data_type);
}

std::string NormalizationBinding::strength() const { return bound.strength(); }

Json NormalizationBinding::audit_payload() const {
  return {{"bound", bound.audit_payload()},
          {"canonical_data_type", canonical_data_type()},
          {"position", position},
          {"role", role},
          {"shape", shape},
          {"source_data_type", data_type}};
}

Json NormalizationBinding::canonical_payload() const {
  return {{"data_type", canonical_data_type()},
          {"normalization", bound.canonical_payload()},
          {"position", position},
          {"role", role},
          {"shape", shape}};
}

TimeNormalization::TimeNormalization(double characteristic_time_value,
                                     std::string kind_value,
                                     std::string unit_value,
                                     ProvenanceItems provenance_value)
    : characteristic_time(
          finite(characteristic_time_value,
                 "normalization characteristic_time")),
      kind(uppercase(std::move(kind_value))), unit(trim(std::move(unit_value))),
      provenance(std::move(provenance_value)) {
  if (characteristic_time <= 0.0) {
    normalization_error("normalization characteristic_time must be positive");
  }
  if (!bound_kinds.contains(kind)) {
    normalization_error("unsupported time normalization kind: " + kind);
  }
  normalize_provenance(provenance);
}

std::string TimeNormalization::strength() const {
  return exact_bound_kinds.contains(kind) ? "EXACT" : "APPROXIMATE";
}

Json TimeNormalization::audit_payload() const {
  return {{"characteristic_time", characteristic_time},
          {"kind", kind},
          {"provenance", provenance_json(provenance)},
          {"transform", "TAU_EQUALS_T_OVER_TC"},
          {"unit", unit}};
}

Json TimeNormalization::canonical_payload() const {
  return {{"dimensionless_time", true},
          {"strength", strength()},
          {"transform", "TAU_EQUALS_T_OVER_TC"}};
}

const NormalizationBinding &
NormalizationContract::binding(std::string_view role, int position) const {
  const std::string normalized = uppercase(std::string(role));
  if (!role_order.contains(normalized)) {
    normalization_error("unsupported normalization role: " +
                        std::string(role));
  }
  for (const auto &item : bindings) {
    if (item.role == normalized && item.position == position) {
      return item;
    }
  }
  normalization_error("normalization contract has no " + normalized +
                      " position " + std::to_string(position));
}

NumericBound numeric_bound(const Json &value) {
  if (value.is_array()) {
    if (value.size() != 2U || !value.at(0).is_number() ||
        !value.at(1).is_number()) {
      normalization_error(
          "normalization bound sequence must contain [minimum, maximum]");
    }
    return NumericBound(value.at(0).get<double>(), value.at(1).get<double>());
  }
  if (!value.is_object()) {
    normalization_error(
        "normalization bound must be an object or [minimum, maximum]");
  }
  static const std::set<std::string> allowed = {
      "kind", "maximum", "minimum", "provenance", "unit"};
  for (const auto &[key, ignored] : value.items()) {
    static_cast<void>(ignored);
    if (!allowed.contains(key)) {
      normalization_error("unknown normalization bound field: " + key);
    }
  }
  if (!value.contains("minimum") || !value.contains("maximum")) {
    normalization_error("normalization bound requires minimum and maximum");
  }
  ProvenanceItems provenance;
  if (value.contains("provenance")) {
    if (!value.at("provenance").is_object()) {
      normalization_error("normalization provenance must be an object");
    }
    for (const auto &[key, item] : value.at("provenance").items()) {
      provenance.emplace_back(key, item.is_string() ? item.get<std::string>()
                                                    : item.dump());
    }
  }
  return NumericBound(value.at("minimum").get<double>(),
                      value.at("maximum").get<double>(),
                      value.value("kind", "EXACT_BOUND"),
                      value.value("unit", ""), std::move(provenance));
}

std::optional<TimeNormalization> time_normalization(const Json &value) {
  if (value.is_null()) {
    return std::nullopt;
  }
  if (value.is_number()) {
    return TimeNormalization(value.get<double>());
  }
  if (!value.is_object()) {
    normalization_error("time normalization must be an object or number");
  }
  static const std::set<std::string> allowed = {
      "characteristic_time", "kind", "provenance", "unit"};
  for (const auto &[key, ignored] : value.items()) {
    static_cast<void>(ignored);
    if (!allowed.contains(key)) {
      normalization_error("unknown time normalization field: " + key);
    }
  }
  if (!value.contains("characteristic_time")) {
    normalization_error("time normalization requires characteristic_time");
  }
  ProvenanceItems provenance;
  if (value.contains("provenance")) {
    if (!value.at("provenance").is_object()) {
      normalization_error("time normalization provenance must be an object");
    }
    for (const auto &[key, item] : value.at("provenance").items()) {
      provenance.emplace_back(key, item.is_string() ? item.get<std::string>()
                                                    : item.dump());
    }
  }
  return TimeNormalization(value.at("characteristic_time").get<double>(),
                           value.value("kind", "EXACT_BOUND"),
                           value.value("unit", ""), std::move(provenance));
}

double normalize_value(const NumericBound &bound, double value) {
  const double observed = finite(value, "normalization source value");
  if (observed < bound.minimum || observed > bound.maximum) {
    normalization_error("source value lies outside normalization bound");
  }
  if (observed == bound.minimum) {
    return 0.0;
  }
  if (observed == bound.maximum) {
    return 1.0;
  }
  return (observed - bound.minimum) / bound.width();
}

double denormalize_value(const NumericBound &bound, double value) {
  const double normalized = finite(value, "normalized value");
  if (normalized < 0.0 || normalized > 1.0) {
    normalization_error("normalized value must lie in [0, 1]");
  }
  if (normalized == 0.0) {
    return bound.minimum;
  }
  if (normalized == 1.0) {
    return bound.maximum;
  }
  return bound.minimum + normalized * bound.width();
}

double normalize_time(const TimeNormalization &contract, double value) {
  return finite(value, "time value") / contract.characteristic_time;
}

double denormalize_time(const TimeNormalization &contract, double value) {
  return finite(value, "dimensionless time value") *
         contract.characteristic_time;
}

NormalizationContract build_normalization_contract(
    const AlgorithmStructureSpec &spec, const BoundMap &input_bounds,
    const BoundMap &parameter_bounds, const BoundMap &state_bounds,
    const BoundMap &output_bounds, std::optional<TimeNormalization> time) {
  validate_structure(spec);
  std::vector<NormalizationBinding> bindings;
  const auto append = [&](std::vector<NormalizationBinding> values) {
    bindings.insert(bindings.end(),
                    std::make_move_iterator(values.begin()),
                    std::make_move_iterator(values.end()));
  };
  append(build_bindings("INPUT", spec.inputs, input_bounds));
  append(build_bindings("PARAMETER", spec.parameters, parameter_bounds));
  append(build_bindings("STATE", spec.states, state_bounds));
  append(build_bindings("OUTPUT", spec.outputs, output_bounds));
  std::sort(bindings.begin(), bindings.end(),
            [](const NormalizationBinding &left,
               const NormalizationBinding &right) {
              return std::pair(role_order.at(left.role), left.position) <
                     std::pair(role_order.at(right.role), right.position);
            });
  const std::string strength = contract_strength(bindings, time);
  std::vector<std::string> warnings;
  if (strength != "EXACT_NORMALIZATION") {
    warnings.push_back(
        "normalization uses one or more approximate/observed engineering "
        "bounds; canonical equality is weaker than exact bounded "
        "normalization");
  }
  Json audit_bindings = Json::array();
  Json canonical_bindings = Json::array();
  for (const auto &binding : bindings) {
    audit_bindings.push_back(binding.audit_payload());
    canonical_bindings.push_back(binding.canonical_payload());
  }
  const Json audit =
      {{"bindings", audit_bindings},
       {"normalization_strength", strength},
       {"normalizer_version", normalizer_version},
       {"schema_version", 1},
       {"time", time ? time->audit_payload() : Json(nullptr)}};
  const Json canonical =
      {{"bindings", canonical_bindings},
       {"normalization_strength", strength},
       {"normalizer_version", normalizer_version},
       {"schema_version", 1},
       {"time", time ? time->canonical_payload() : Json(nullptr)}};
  return {.schema_version = 1,
          .normalizer_version_value = std::string(normalizer_version),
          .bindings = std::move(bindings),
          .time = std::move(time),
          .normalization_strength = strength,
          .contract_hash = contracts::sha256_json(audit),
          .canonical_signature = contracts::sha256_json(canonical),
          .warnings = std::move(warnings)};
}

std::vector<double> normalize_role(const NormalizationContract &contract,
                                   std::string_view role,
                                   const std::vector<double> &values) {
  const auto bindings = role_bindings(contract, role);
  if (values.size() != bindings.size()) {
    normalization_error("normalization value count does not match bindings");
  }
  std::vector<double> result;
  result.reserve(values.size());
  for (std::size_t index = 0; index < values.size(); ++index) {
    result.push_back(normalize_value(bindings[index]->bound, values[index]));
  }
  return result;
}

std::vector<double> denormalize_role(const NormalizationContract &contract,
                                     std::string_view role,
                                     const std::vector<double> &values) {
  const auto bindings = role_bindings(contract, role);
  if (values.size() != bindings.size()) {
    normalization_error("denormalization value count does not match bindings");
  }
  std::vector<double> result;
  result.reserve(values.size());
  for (std::size_t index = 0; index < values.size(); ++index) {
    result.push_back(denormalize_value(bindings[index]->bound, values[index]));
  }
  return result;
}

std::string normalized_algorithm_signature(
    const CanonicalAlgorithmIR &structural_ir,
    const NormalizationContract &contract) {
  return contracts::sha256_json(
      {{"claim_scope", "STRUCTURE_PLUS_INTERFACE_COORDINATES_ONLY"},
       {"normalization",
        {{"normalizer_version", contract.normalizer_version_value},
         {"signature", contract.canonical_signature},
         {"strength", contract.normalization_strength}}},
       {"signature_version", "saa-structural-interface-normalized-v1"},
       {"structural",
        {{"canonicalizer_version", structural_ir.canonicalizer_version_value},
         {"hash", structural_ir.structural_hash},
         {"strength", structural_ir.canonicalization_strength}}}});
}

Json to_json(const NormalizationContract &contract) {
  Json bindings = Json::array();
  for (const auto &binding : contract.bindings) {
    bindings.push_back(binding.audit_payload());
  }
  return {{"bindings", bindings},
          {"canonical_signature", contract.canonical_signature},
          {"contract_hash", contract.contract_hash},
          {"normalization_strength", contract.normalization_strength},
          {"normalizer_version", contract.normalizer_version_value},
          {"schema_version", contract.schema_version},
          {"time", contract.time ? contract.time->audit_payload() : Json(nullptr)},
          {"warnings", contract.warnings}};
}

} // namespace statewright::saa
