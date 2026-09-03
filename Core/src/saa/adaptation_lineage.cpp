#include "statewright/saa/adaptation_lineage.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <set>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[noreturn]] void lineage_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

[[nodiscard]] std::string trimmed(std::string value) {
  const auto first = value.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\n\r\f\v");
  return value.substr(first, last - first + 1U);
}

[[nodiscard]] std::string canonical_sha(std::string value,
                                        std::string_view label) {
  value = trimmed(std::move(value));
  std::ranges::transform(value, value.begin(), [](const char character) {
    return static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  });
  const bool valid =
      value.size() == 64U &&
      std::ranges::all_of(value, [](const char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
      });
  if (!valid) {
    lineage_error(std::string(label) +
                  " must be an exact SHA-256 digest");
  }
  return value;
}

} // namespace

std::string adapted_candidate_ref(std::string signature) {
  return "adapted-candidate:sha256:" +
         canonical_sha(std::move(signature), "adapted candidate signature");
}

AdaptationLineageEdge make_adaptation_lineage_edge(
    const AdaptedAlgorithmCandidate &candidate, const AdaptationStep &step,
    std::string source_explanation_signature) {
  if (candidate.changed_dimension != step.dimension) {
    lineage_error(
        "lineage candidate dimension differs from originating adaptation step");
  }
  if (candidate.component != step.component) {
    lineage_error(
        "lineage candidate component differs from originating adaptation step");
  }
  if (candidate.base_algorithm_id != step.base_algorithm_id) {
    lineage_error("lineage candidate base algorithm differs from originating "
                  "adaptation step");
  }
  if (!candidate.qualification_required ||
      candidate.canonical_reuse_eligible) {
    lineage_error(
        "SAA-11.1 lineage accepts only unqualified adaptation candidates");
  }
  source_explanation_signature = canonical_sha(
      std::move(source_explanation_signature), "source explanation signature");
  static_cast<void>(
      canonical_sha(step.step_signature, "adaptation step signature"));
  static_cast<void>(canonical_sha(candidate.candidate_signature,
                                  "adapted candidate signature"));
  std::string parent_signature = trimmed(candidate.parent_candidate_signature);
  std::ranges::transform(
      parent_signature, parent_signature.begin(), [](const char character) {
        return static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
      });
  std::string parent_ref;
  if (!parent_signature.empty()) {
    parent_signature = canonical_sha(std::move(parent_signature),
                                     "parent adapted candidate signature");
    if (parent_signature == candidate.candidate_signature) {
      lineage_error("adaptation lineage cannot self-parent");
    }
    parent_ref = adapted_candidate_ref(parent_signature);
  } else {
    parent_ref = trimmed(candidate.base_algorithm_id);
    if (parent_ref.empty()) {
      lineage_error("first-generation adaptation candidate requires a "
                    "canonical base algorithm reference");
    }
  }
  const std::string child_ref =
      adapted_candidate_ref(candidate.candidate_signature);
  const Json payload =
      {{"base_algorithm_id", candidate.base_algorithm_id},
       {"candidate_signature", candidate.candidate_signature},
       {"changed_dimension", candidate.changed_dimension},
       {"child_ref", child_ref},
       {"component", candidate.component},
       {"parent_candidate_signature", parent_signature},
       {"parent_ref", parent_ref},
       {"relation", "ADAPTED_FROM"},
       {"source_explanation_signature", source_explanation_signature},
       {"step_signature", step.step_signature},
       {"version", adaptation_lineage_version}};
  return {.schema_version = 1,
          .lineage_version = std::string(adaptation_lineage_version),
          .relation = "ADAPTED_FROM",
          .parent_ref = std::move(parent_ref),
          .child_ref = child_ref,
          .base_algorithm_id = candidate.base_algorithm_id,
          .component = candidate.component,
          .changed_dimension = candidate.changed_dimension,
          .step_signature = step.step_signature,
          .source_explanation_signature =
              std::move(source_explanation_signature),
          .candidate_signature = candidate.candidate_signature,
          .parent_candidate_signature = std::move(parent_signature),
          .edge_signature = contracts::sha256_json(payload)};
}

AdaptationPromotionRecord make_adaptation_promotion(
    std::string candidate_ref_value, std::string canonical_algorithm_ref,
    std::string qualification_signature, std::vector<std::string> evidence_ids) {
  candidate_ref_value = trimmed(std::move(candidate_ref_value));
  constexpr std::string_view candidate_prefix =
      "adapted-candidate:sha256:";
  if (!candidate_ref_value.starts_with(candidate_prefix)) {
    lineage_error(
        "SAA-11.1 promotion requires adapted-candidate reference");
  }
  static_cast<void>(canonical_sha(
      candidate_ref_value.substr(candidate_ref_value.rfind(':') + 1U),
      "promotion candidate signature"));
  canonical_algorithm_ref = trimmed(std::move(canonical_algorithm_ref));
  if (!canonical_algorithm_ref.starts_with("canonical-algorithm:sha256:") &&
      !canonical_algorithm_ref.starts_with("canonical-reasoning:sha256:")) {
    lineage_error("SAA-11.1 promotion target must be canonical mathematical "
                  "or reasoning algorithm");
  }
  qualification_signature = canonical_sha(
      std::move(qualification_signature), "promotion qualification signature");
  std::set<std::string> evidence;
  for (auto &evidence_id : evidence_ids) {
    evidence_id = trimmed(std::move(evidence_id));
    if (!evidence_id.empty()) {
      evidence.insert(std::move(evidence_id));
    }
  }
  if (evidence.empty()) {
    lineage_error(
        "SAA-11.1 promotion requires grounded qualification evidence");
  }
  std::vector<std::string> canonical_evidence(evidence.begin(), evidence.end());
  const Json payload =
      {{"candidate_ref", candidate_ref_value},
       {"canonical_algorithm_ref", canonical_algorithm_ref},
       {"evidence_ids", canonical_evidence},
       {"qualification_signature", qualification_signature},
       {"version", adaptation_lineage_version}};
  return {.schema_version = 1,
          .lineage_version = std::string(adaptation_lineage_version),
          .candidate_ref = std::move(candidate_ref_value),
          .canonical_algorithm_ref = std::move(canonical_algorithm_ref),
          .qualification_signature = std::move(qualification_signature),
          .evidence_ids = std::move(canonical_evidence),
          .promotion_signature = contracts::sha256_json(payload)};
}

Json to_json(const AdaptationLineageEdge &value) {
  return {{"base_algorithm_id", value.base_algorithm_id},
          {"candidate_signature", value.candidate_signature},
          {"changed_dimension", value.changed_dimension},
          {"child_ref", value.child_ref},
          {"component", value.component},
          {"edge_signature", value.edge_signature},
          {"lineage_version", value.lineage_version},
          {"parent_candidate_signature", value.parent_candidate_signature},
          {"parent_ref", value.parent_ref},
          {"relation", value.relation},
          {"schema_version", value.schema_version},
          {"source_explanation_signature",
           value.source_explanation_signature},
          {"step_signature", value.step_signature}};
}

Json to_json(const AdaptationPromotionRecord &value) {
  return {{"candidate_ref", value.candidate_ref},
          {"canonical_algorithm_ref", value.canonical_algorithm_ref},
          {"evidence_ids", value.evidence_ids},
          {"lineage_version", value.lineage_version},
          {"promotion_signature", value.promotion_signature},
          {"qualification_signature", value.qualification_signature},
          {"schema_version", value.schema_version}};
}

} // namespace statewright::saa
