#include "statewright/saa/representative_form.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;
using RationalChannelMatrix = std::vector<std::vector<RationalChannel>>;

[[noreturn]] void representative_form_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument,
                      std::move(message));
}

[[nodiscard]] Json rational_matrix_json(const RationalMatrix &matrix) {
  Json result = Json::array();
  for (const auto &row : matrix) {
    Json encoded = Json::array();
    for (const auto &value : row) {
      encoded.push_back(rational_json(value));
    }
    result.push_back(std::move(encoded));
  }
  return result;
}

[[nodiscard]] Json rational_channel_matrix_json(
    const RationalChannelMatrix &matrix) {
  Json result = Json::array();
  for (const auto &row : matrix) {
    Json encoded = Json::array();
    for (const auto &channel : row) {
      encoded.push_back(to_json(channel));
    }
    result.push_back(std::move(encoded));
  }
  return result;
}

[[nodiscard]] std::string canonical_text(std::string value) {
  std::istringstream input(value);
  std::ostringstream output;
  std::string word;
  bool first = true;
  while (input >> word) {
    if (!first) {
      output << ' ';
    }
    std::transform(word.begin(), word.end(), word.begin(),
                   [](unsigned char character) {
                     return static_cast<char>(std::tolower(character));
                   });
    output << word;
    first = false;
  }
  return output.str();
}

[[nodiscard]] std::vector<const NormalizationBinding *> bindings_for_role(
    const NormalizationContract &contract, std::string_view role) {
  std::vector<const NormalizationBinding *> result;
  for (const auto &binding : contract.bindings) {
    if (binding.role == role) {
      result.push_back(&binding);
    }
  }
  std::sort(result.begin(), result.end(), [](const auto *left,
                                             const auto *right) {
    return left->position < right->position;
  });
  return result;
}

[[nodiscard]] const RepresentativeInputCandidate &validate_candidate(
    const RepresentativeInputSearch &search) {
  if (!search.representative_found() || !search.best_candidate) {
    representative_form_error(
        "SAA-6 requires an admissible representative form from SAA-5");
  }
  const auto &candidate = *search.best_candidate;
  if (candidate.status != "REPRESENTATIVE_FORM_CANDIDATE") {
    representative_form_error(
        "SAA-6 best candidate is not a representative-form candidate");
  }
  if (!candidate.exact_decoupled) {
    representative_form_error(
        "SAA-6 representative candidate must be exactly decoupled");
  }
  if (!candidate.independent || !candidate.minimal) {
    representative_form_error(
        "SAA-6 representative candidate must be independent and minimal");
  }
  if (!candidate.admissibility.admissible) {
    representative_form_error(
        "SAA-6 representative candidate must pass SAA-5.2 admissibility");
  }
  return candidate;
}

void validate_source_normalization(
    const NormalizationContract &normalization,
    const RepresentativeInputCandidate &candidate,
    const CanonicalMIMOCoupling &mimo) {
  if (normalization.normalization_strength != "EXACT_NORMALIZATION") {
    representative_form_error(
        "SAA-6 canonical representative admission requires exact source normalization");
  }
  const auto inputs = bindings_for_role(normalization, "INPUT");
  const auto outputs = bindings_for_role(normalization, "OUTPUT");
  if (inputs.size() != candidate.source_input_count) {
    representative_form_error(
        "SAA-6 source normalization input count does not match representative candidate");
  }
  if (outputs.size() != mimo.output_count) {
    representative_form_error(
        "SAA-6 source normalization output count does not match MIMO representation");
  }
  if (!normalization.time) {
    representative_form_error(
        "SAA-6 requires the canonical characteristic-time contract");
  }
  const auto not_exact = [](const NormalizationBinding *binding) {
    return binding->strength() != "EXACT";
  };
  if (std::any_of(inputs.begin(), inputs.end(), not_exact) ||
      std::any_of(outputs.begin(), outputs.end(), not_exact)) {
    representative_form_error(
        "SAA-6 source input/output bounds must be exact/domain bounds");
  }
}

struct SemanticMaps final {
  std::map<std::size_t, const SemanticRepresentationIssue *> issues;
  std::map<std::string, const SemanticCandidateMeaning *> candidates;
  std::map<std::string, const SemanticResolution *> resolutions;
};

[[nodiscard]] SemanticMaps semantic_maps(
    const std::vector<SemanticRepresentationIssue> &issues,
    const std::vector<SemanticCandidateMeaning> &candidates,
    const std::vector<SemanticResolution> &resolutions,
    std::size_t representative_inputs) {
  SemanticMaps result;
  for (const auto &issue : issues) {
    if (issue.coordinate_kind != "REPRESENTATIVE_INPUT") {
      representative_form_error(
          "SAA-6 semantic issues must describe the representative coordinates, not the source representation");
    }
    if (issue.coordinate_index < 0 ||
        !result.issues
             .emplace(static_cast<std::size_t>(issue.coordinate_index), &issue)
             .second) {
      representative_form_error(
          "duplicate SAA-6 semantic issue for representative coordinate");
    }
  }
  if (result.issues.size() != representative_inputs) {
    representative_form_error(
        "SAA-6 requires exactly one semantic issue per representative input");
  }
  for (std::size_t index = 0; index < representative_inputs; ++index) {
    if (!result.issues.contains(index)) {
      representative_form_error(
          "SAA-6 requires exactly one semantic issue per representative input");
    }
  }
  for (const auto &candidate : candidates) {
    if (!result.candidates.emplace(candidate.issue_id, &candidate).second) {
      representative_form_error(
          "duplicate semantic candidate for one SAA-6 issue");
    }
  }
  for (const auto &resolution : resolutions) {
    if (!result.resolutions.emplace(resolution.issue_id, &resolution).second) {
      representative_form_error(
          "duplicate semantic resolution for one SAA-6 issue");
    }
  }
  if (!canonical_semantic_admission(true, issues, resolutions)) {
    representative_form_error(
        "SAA-6 canonical representative form requires every representative semantic issue to be resolved");
  }
  for (const auto &issue : issues) {
    const auto candidate = result.candidates.find(issue.issue_id);
    const auto resolution = result.resolutions.find(issue.issue_id);
    if (candidate == result.candidates.end() ||
        resolution == result.resolutions.end()) {
      representative_form_error(
          "SAA-6 requires candidate meaning and resolution for every semantic issue");
    }
    if (resolution->second->candidate_id != candidate->second->candidate_id) {
      representative_form_error(
          "SAA-6 semantic resolution references a different candidate meaning");
    }
    if (resolution->second->status != "SEMANTICALLY_RESOLVED" ||
        !resolution->second->canonical_semantic_eligible) {
      representative_form_error(
          "SAA-6 semantic resolution is not canonical-admission eligible");
    }
    if (resolution->second->semantic_fit_bp != 10000) {
      representative_form_error(
          "SAA-6 requires complete semantic output-footprint fit");
    }
  }
  return result;
}

[[nodiscard]] std::vector<std::size_t>
canonical_input_order(const RepresentativeInputCandidate &candidate) {
  const std::size_t count = candidate.representative_input_count;
  if (count == 0U) {
    return {};
  }
  if (!candidate.preferred_input_to_output_pairing ||
      candidate.preferred_input_to_output_pairing->size() != count) {
    representative_form_error(
        "SAA-6 representative form requires one-to-one input/output pairing");
  }
  std::set<std::size_t> unique(
      candidate.preferred_input_to_output_pairing->begin(),
      candidate.preferred_input_to_output_pairing->end());
  if (unique.size() != count) {
    representative_form_error(
        "SAA-6 representative input/output pairing must be injective");
  }
  std::vector<std::size_t> order(count);
  std::iota(order.begin(), order.end(), 0U);
  std::sort(order.begin(), order.end(), [&](std::size_t left,
                                            std::size_t right) {
    return std::pair((*candidate.preferred_input_to_output_pairing)[left], left) <
           std::pair((*candidate.preferred_input_to_output_pairing)[right], right);
  });
  return order;
}

[[nodiscard]] std::pair<mpq_class, mpq_class>
representative_raw_bound(const RationalPolynomial &coefficients) {
  mpq_class minimum = 0;
  mpq_class maximum = 0;
  for (const auto &value : coefficients) {
    if (value < 0) {
      minimum += value;
    } else {
      maximum += value;
    }
  }
  return {minimum, maximum};
}

[[nodiscard]] RepresentativeCoordinateBoundary boundary_for_input(
    const RepresentativeInputCandidate &candidate,
    std::size_t candidate_input_index,
    const NormalizationContract &normalization,
    const SemanticResolution &resolution) {
  const auto &coefficients =
      candidate.source_to_representative_projection[candidate_input_index];
  const auto [minimum, maximum] = representative_raw_bound(coefficients);
  if (maximum <= minimum) {
    representative_form_error(
        "SAA-6 nonzero representative coordinate has zero reachable width");
  }
  const Json material =
      {{"bound_policy", representative_bound_policy},
       {"candidate_input_index", candidate_input_index},
       {"raw_maximum", rational_json(maximum)},
       {"raw_minimum", rational_json(minimum)},
       {"representative_version", canonical_representative_version},
       {"schema_version", 1},
       {"semantic_resolution_signature", resolution.resolution_signature},
       {"source_normalization_signature",
        normalization.canonical_signature},
       {"target", Json::array({Json::array({0, 1}), Json::array({1, 1})})}};
  return {candidate_input_index,
          minimum,
          maximum,
          maximum - minimum,
          0,
          1,
          std::string(representative_bound_policy),
          normalization.canonical_signature,
          resolution.resolution_signature,
          contracts::sha256_json(material)};
}

[[nodiscard]] RationalChannelMatrix renormalized_channels(
    const RepresentativeInputCandidate &candidate,
    const std::vector<std::size_t> &input_order,
    const std::map<std::size_t, RepresentativeCoordinateBoundary> &boundaries) {
  RationalChannelMatrix result;
  for (const auto &row : candidate.representative_channels) {
    std::vector<RationalChannel> normalized_row;
    for (const std::size_t original_index : input_order) {
      normalized_row.push_back(scale_rational_channel(
          row[original_index], boundaries.at(original_index).raw_width));
    }
    result.push_back(std::move(normalized_row));
  }
  return result;
}

} // namespace

RepresentativeCoordinateBoundary::RepresentativeCoordinateBoundary(
    std::size_t candidate_input_index_value, mpq_class raw_minimum_value,
    mpq_class raw_maximum_value, mpq_class raw_width_value,
    mpq_class normalized_minimum_value, mpq_class normalized_maximum_value,
    std::string bound_policy_value,
    std::string source_normalization_signature_value,
    std::string semantic_resolution_signature_value,
    std::string boundary_signature_value)
    : candidate_input_index(candidate_input_index_value),
      raw_minimum(std::move(raw_minimum_value)),
      raw_maximum(std::move(raw_maximum_value)),
      raw_width(std::move(raw_width_value)),
      normalized_minimum(std::move(normalized_minimum_value)),
      normalized_maximum(std::move(normalized_maximum_value)),
      bound_policy(std::move(bound_policy_value)),
      source_normalization_signature(
          std::move(source_normalization_signature_value)),
      semantic_resolution_signature(
          std::move(semantic_resolution_signature_value)),
      boundary_signature(std::move(boundary_signature_value)) {
  if (raw_maximum <= raw_minimum) {
    representative_form_error(
        "SAA-6 representative boundary must have positive width");
  }
  if (raw_width != raw_maximum - raw_minimum) {
    representative_form_error(
        "SAA-6 representative boundary width is inconsistent");
  }
  if (normalized_minimum != 0 || normalized_maximum != 1) {
    representative_form_error(
        "SAA-6 representative target range must be exactly [0,1]");
  }
}

mpq_class RepresentativeCoordinateBoundary::normalize(
    const mpq_class &value) const {
  if (value < raw_minimum || value > raw_maximum) {
    representative_form_error(
        "representative value lies outside its SAA-6 boundary");
  }
  return (value - raw_minimum) / raw_width;
}

mpq_class RepresentativeCoordinateBoundary::denormalize(
    const mpq_class &value) const {
  if (value < 0 || value > 1) {
    representative_form_error(
        "SAA-6 normalized representative value must lie in [0,1]");
  }
  return raw_minimum + value * raw_width;
}

Json to_json(const RepresentativeCoordinateBoundary &value) {
  return {{"bound_policy", value.bound_policy},
          {"boundary_signature", value.boundary_signature},
          {"candidate_input_index", value.candidate_input_index},
          {"normalized_maximum", rational_json(value.normalized_maximum)},
          {"normalized_minimum", rational_json(value.normalized_minimum)},
          {"raw_maximum", rational_json(value.raw_maximum)},
          {"raw_minimum", rational_json(value.raw_minimum)},
          {"raw_width", rational_json(value.raw_width)},
          {"semantic_resolution_signature",
           value.semantic_resolution_signature},
          {"source_normalization_signature",
           value.source_normalization_signature}};
}

Json to_json(const CanonicalRepresentativeInput &value) {
  return {{"boundary", to_json(value.boundary)},
          {"candidate_input_index", value.candidate_input_index},
          {"canonical_meaning", value.canonical_meaning},
          {"canonical_position", value.canonical_position},
          {"excluded_output_indices", value.excluded_output_indices},
          {"expected_output_indices", value.expected_output_indices},
          {"meaning", value.meaning},
          {"paired_output_index", value.paired_output_index},
          {"semantic_candidate_id", value.semantic_candidate_id},
          {"semantic_candidate_signature",
           value.semantic_candidate_signature},
          {"semantic_evidence_ids", value.semantic_evidence_ids},
          {"semantic_issue_id", value.semantic_issue_id},
          {"semantic_resolution_signature",
           value.semantic_resolution_signature},
          {"source_coefficients", polynomial_json(value.source_coefficients)},
          {"source_input_indices", value.source_input_indices}};
}

Json to_json(const CanonicalRepresentativeAlgorithmForm &value) {
  Json inputs = Json::array();
  for (const auto &input : value.inputs) {
    inputs.push_back(to_json(input));
  }
  return {
      {"audit_hash", value.audit_hash},
      {"canonical_admission_eligible", value.canonical_admission_eligible},
      {"canonical_algorithm_signature", value.canonical_algorithm_signature},
      {"canonical_input_permutation", value.canonical_input_permutation},
      {"domain", value.domain},
      {"inputs", inputs},
      {"mathematical_representative_signature",
       value.mathematical_representative_signature},
      {"normalized_channels",
       rational_channel_matrix_json(value.normalized_channels)},
      {"normalized_sample_interval",
       value.normalized_sample_interval
           ? rational_json(*value.normalized_sample_interval)
           : Json(nullptr)},
      {"output_count", value.output_count},
      {"representative_behavior_signature",
       value.representative_behavior_signature},
      {"representative_candidate_signature",
       value.representative_candidate_signature},
      {"representative_input_count", value.representative_input_count},
      {"representative_search_audit_hash",
       value.representative_search_audit_hash},
      {"representative_version", value.representative_version_value},
      {"schema_version", value.schema_version},
      {"semantic_representative_signature",
       value.semantic_representative_signature},
      {"source_mimo_signature", value.source_mimo_signature},
      {"source_normalization_signature",
       value.source_normalization_signature},
      {"source_structural_hash", value.source_structural_hash},
      {"source_structural_strength", value.source_structural_strength},
      {"store_status", value.store_status},
      {"structural_binding_policy", value.structural_binding_policy_value},
      {"variable", value.variable},
      {"warnings", value.warnings}};
}

CanonicalRepresentativeAlgorithmForm canonicalize_representative_algorithm(
    const CanonicalAlgorithmIR &structural_ir,
    const NormalizationContract &source_normalization,
    const CanonicalMIMOCoupling &mimo,
    const RepresentativeInputSearch &representative_search,
    const std::vector<SemanticRepresentationIssue> &semantic_issues,
    const std::vector<SemanticCandidateMeaning> &semantic_candidates,
    const std::vector<SemanticResolution> &semantic_resolutions) {
  const auto &candidate = validate_candidate(representative_search);
  validate_source_normalization(source_normalization, candidate, mimo);
  if (candidate.source_input_count != mimo.input_count) {
    representative_form_error(
        "SAA-6 representative candidate source dimension mismatches MIMO input count");
  }
  if (candidate.representative_channels.size() != mimo.output_count) {
    representative_form_error(
        "SAA-6 representative candidate output dimension mismatches MIMO output count");
  }
  if (std::any_of(candidate.representative_channels.begin(),
                  candidate.representative_channels.end(),
                  [&](const auto &row) {
                    return row.size() != candidate.representative_input_count;
                  })) {
    representative_form_error(
        "SAA-6 representative channel matrix dimensions are inconsistent");
  }
  const auto maps = semantic_maps(
      semantic_issues, semantic_candidates, semantic_resolutions,
      candidate.representative_input_count);
  const auto input_order = canonical_input_order(candidate);
  const std::vector<std::size_t> pairing =
      candidate.preferred_input_to_output_pairing.value_or(
          std::vector<std::size_t>{});
  std::map<std::size_t, RepresentativeCoordinateBoundary> boundaries;
  for (std::size_t input = 0; input < candidate.representative_input_count;
       ++input) {
    const auto &issue = *maps.issues.at(input);
    boundaries.emplace(input,
                       boundary_for_input(candidate, input,
                                          source_normalization,
                                          *maps.resolutions.at(issue.issue_id)));
  }
  auto normalized_channels =
      renormalized_channels(candidate, input_order, boundaries);

  std::vector<CanonicalRepresentativeInput> canonical_inputs;
  Json semantic_identity_rows = Json::array();
  for (std::size_t canonical_position = 0;
       canonical_position < input_order.size(); ++canonical_position) {
    const std::size_t original_index = input_order[canonical_position];
    const auto &issue = *maps.issues.at(original_index);
    const auto &semantic_candidate = *maps.candidates.at(issue.issue_id);
    const auto &resolution = *maps.resolutions.at(issue.issue_id);
    const auto &coefficients =
        candidate.source_to_representative_projection[original_index];
    std::vector<std::size_t> source_indices;
    RationalPolynomial source_coefficients;
    for (std::size_t source = 0; source < coefficients.size(); ++source) {
      if (coefficients[source] != 0) {
        source_indices.push_back(source);
        source_coefficients.push_back(coefficients[source]);
      }
    }
    const std::string canonical_meaning =
        canonical_text(semantic_candidate.meaning);
    canonical_inputs.push_back(
        {.canonical_position = canonical_position,
         .candidate_input_index = original_index,
         .paired_output_index = pairing[original_index],
         .meaning = semantic_candidate.meaning,
         .canonical_meaning = canonical_meaning,
         .expected_output_indices =
             semantic_candidate.expected_output_indices,
         .excluded_output_indices =
             semantic_candidate.excluded_output_indices,
         .source_input_indices = std::move(source_indices),
         .source_coefficients = std::move(source_coefficients),
         .semantic_issue_id = issue.issue_id,
         .semantic_candidate_id = semantic_candidate.candidate_id,
         .semantic_candidate_signature = semantic_candidate.signature,
         .semantic_resolution_signature = resolution.resolution_signature,
         .semantic_evidence_ids = resolution.evidence_ids,
         .boundary = boundaries.at(original_index)});
    semantic_identity_rows.push_back(
        {{"canonical_position", canonical_position},
         {"excluded_output_indices",
          semantic_candidate.excluded_output_indices},
         {"expected_output_indices",
          semantic_candidate.expected_output_indices},
         {"meaning", canonical_meaning},
         {"paired_output_index", pairing[original_index]}});
  }

  const Json mathematical_payload =
      {{"claim_scope",
        "EXACT_MINIMAL_DECOUPLED_RENORMALIZED_REPRESENTATIVE_DYNAMICS"},
       {"domain", mimo.domain},
       {"input_order_policy", "ORDER_BY_UNIQUE_PAIRED_OUTPUT"},
       {"normalized_channels",
        rational_channel_matrix_json(normalized_channels)},
       {"normalized_sample_interval",
        mimo.normalized_sample_interval
            ? rational_json(*mimo.normalized_sample_interval)
            : Json(nullptr)},
       {"output_count", mimo.output_count},
       {"representative_input_count", candidate.representative_input_count},
       {"representative_version", canonical_representative_version},
       {"schema_version", 1},
       {"target_input_domain", Json::array({0, 1})},
       {"variable", mimo.variable}};
  const std::string mathematical_signature =
      contracts::sha256_json(mathematical_payload);
  const Json semantic_payload =
      {{"claim_scope", "RESOLVED_REPRESENTATIVE_INPUT_SEMANTICS"},
       {"inputs", semantic_identity_rows},
       {"representative_version", canonical_representative_version},
       {"schema_version", 1}};
  const std::string semantic_signature =
      contracts::sha256_json(semantic_payload);
  const Json behavior_payload =
      {{"claim_scope", "CANONICAL_REPRESENTATIVE_BEHAVIOR_AND_SEMANTICS"},
       {"mathematical_representative_signature", mathematical_signature},
       {"representative_version", canonical_representative_version},
       {"schema_version", 1},
       {"semantic_representative_signature", semantic_signature}};
  const std::string behavior_signature =
      contracts::sha256_json(behavior_payload);
  const Json algorithm_payload =
      {{"claim_scope",
        "CANONICAL_REPRESENTATIVE_ALGORITHM_WITH_CONSERVATIVE_SOURCE_STRUCTURE"},
       {"representative_behavior_signature", behavior_signature},
       {"representative_version", canonical_representative_version},
       {"schema_version", 1},
       {"source_structural_hash", structural_ir.structural_hash},
       {"source_structural_strength",
        structural_ir.canonicalization_strength},
       {"structural_binding_policy", structural_binding_policy}};
  const std::string algorithm_signature =
      contracts::sha256_json(algorithm_payload);

  Json boundary_signatures = Json::array();
  Json issue_signatures = Json::array();
  Json candidate_signatures = Json::array();
  Json resolution_signatures = Json::array();
  for (const std::size_t index : input_order) {
    const auto &issue = *maps.issues.at(index);
    boundary_signatures.push_back(boundaries.at(index).boundary_signature);
    issue_signatures.push_back(issue.signature);
    candidate_signatures.push_back(maps.candidates.at(issue.issue_id)->signature);
    resolution_signatures.push_back(
        maps.resolutions.at(issue.issue_id)->resolution_signature);
  }
  const Json audit_payload =
      {{"boundary_signatures", boundary_signatures},
       {"canonical_algorithm_signature", algorithm_signature},
       {"canonical_input_permutation", input_order},
       {"representative_behavior_signature", behavior_signature},
       {"representative_candidate_signature", candidate.canonical_signature},
       {"representative_search_audit_hash", representative_search.audit_hash},
       {"representative_version", canonical_representative_version},
       {"schema_version", 1},
       {"semantic_candidate_signatures", candidate_signatures},
       {"semantic_issue_signatures", issue_signatures},
       {"semantic_resolution_signatures", resolution_signatures},
       {"source_mimo_signature", mimo.ordered_signature},
       {"source_normalization_contract_hash",
        source_normalization.contract_hash},
       {"source_normalization_signature",
        source_normalization.canonical_signature},
       {"source_structural_hash", structural_ir.structural_hash},
       {"source_to_representative_projection",
        rational_matrix_json(candidate.source_to_representative_projection)}};

  return {
      .schema_version = 1,
      .representative_version_value =
          std::string(canonical_representative_version),
      .domain = mimo.domain,
      .variable = mimo.variable,
      .output_count = mimo.output_count,
      .representative_input_count = candidate.representative_input_count,
      .normalized_sample_interval = mimo.normalized_sample_interval,
      .source_mimo_signature = mimo.ordered_signature,
      .source_normalization_signature =
          source_normalization.canonical_signature,
      .source_structural_hash = structural_ir.structural_hash,
      .source_structural_strength = structural_ir.canonicalization_strength,
      .representative_search_audit_hash = representative_search.audit_hash,
      .representative_candidate_signature = candidate.canonical_signature,
      .canonical_input_permutation = input_order,
      .inputs = std::move(canonical_inputs),
      .normalized_channels = std::move(normalized_channels),
      .mathematical_representative_signature = mathematical_signature,
      .semantic_representative_signature = semantic_signature,
      .representative_behavior_signature = behavior_signature,
      .canonical_algorithm_signature = algorithm_signature,
      .structural_binding_policy_value = std::string(structural_binding_policy),
      .canonical_admission_eligible = true,
      .store_status = "ELIGIBLE_CANONICAL_REPRESENTATIVE_FORM",
      .audit_hash = contracts::sha256_json(audit_payload),
      .warnings = {
          "SAA-6 canonical algorithm identity conservatively binds the source SAA-1 structural hash; a future representative structural rewrite may merge additional equivalent implementations."}};
}

mpq_class normalize_representative_value(
    const RepresentativeCoordinateBoundary &boundary,
    const mpq_class &value) {
  return boundary.normalize(value);
}

mpq_class denormalize_representative_value(
    const RepresentativeCoordinateBoundary &boundary,
    const mpq_class &value) {
  return boundary.denormalize(value);
}

} // namespace statewright::saa
