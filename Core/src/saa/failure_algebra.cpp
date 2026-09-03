#include "statewright/saa/failure_algebra.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <ranges>
#include <set>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

inline constexpr std::array<std::string_view, 10U> failure_classes = {
    "INVARIANT_VIOLATION", "EXPERIMENT_REGRESSION", "EXPERIMENT_TRADEOFF",
    "SEMANTIC_MISMATCH", "RETRIEVAL_MISMATCH", "CYCLE_STOP",
    "ADAPTATION_REJECTED", "QUALIFICATION_FAILURE", "EVIDENCE_FAILURE",
    "OTHER_BOUNDED_FAILURE"};

[[noreturn]] void failure_error(std::string message) {
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

[[nodiscard]] std::string canonical_text(std::string value) {
  std::string result;
  bool pending_space = false;
  for (const char character : value) {
    if (std::isspace(static_cast<unsigned char>(character)) != 0) {
      pending_space = !result.empty();
      continue;
    }
    if (pending_space) {
      result.push_back(' ');
      pending_space = false;
    }
    result.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }
  return result;
}

[[nodiscard]] std::string uppercase(std::string value) {
  value = trimmed(std::move(value));
  std::ranges::transform(value, value.begin(), [](const char character) {
    return static_cast<char>(
        std::toupper(static_cast<unsigned char>(character)));
  });
  return value;
}

[[nodiscard]] std::vector<std::string>
canonical_texts(std::vector<std::string> values) {
  std::set<std::string> result;
  for (auto &value : values) {
    value = canonical_text(std::move(value));
    if (!value.empty()) {
      result.insert(std::move(value));
    }
  }
  return {result.begin(), result.end()};
}

[[nodiscard]] std::vector<std::string>
canonical_ids(std::vector<std::string> values) {
  std::set<std::string> result;
  for (auto &value : values) {
    value = trimmed(std::move(value));
    if (!value.empty()) {
      result.insert(std::move(value));
    }
  }
  return {result.begin(), result.end()};
}

[[nodiscard]] std::string canonical_sha(std::string value,
                                        std::string_view label,
                                        bool allow_empty = true) {
  value = trimmed(std::move(value));
  std::ranges::transform(value, value.begin(), [](const char character) {
    return static_cast<char>(
        std::tolower(static_cast<unsigned char>(character)));
  });
  if (value.empty() && allow_empty) {
    return value;
  }
  if (value.size() != 64U ||
      !std::ranges::all_of(value, [](const char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
      })) {
    failure_error(std::string(label) +
                  " must be an exact SHA-256 signature");
  }
  return value;
}

} // namespace

FailureObservation make_failure_observation(
    std::string source_kind, std::string component,
    std::string failure_class, std::string mechanism,
    std::vector<std::string> semantic_roles,
    std::vector<std::string> violated_invariants,
    std::string boundary_signature, std::string context_signature,
    std::vector<std::string> evidence_ids, std::string provenance_id) {
  source_kind = canonical_text(std::move(source_kind));
  component = canonical_text(std::move(component));
  failure_class = uppercase(std::move(failure_class));
  mechanism = canonical_text(std::move(mechanism));
  if (source_kind.empty() || component.empty() || mechanism.empty()) {
    failure_error(
        "SAA-12.1 failure source, component and mechanism are required");
  }
  if (std::ranges::find(failure_classes, failure_class) ==
      failure_classes.end()) {
    failure_error("unsupported SAA-12.1 failure class: " + failure_class);
  }
  boundary_signature = canonical_sha(
      std::move(boundary_signature), "failure boundary signature");
  context_signature =
      canonical_sha(std::move(context_signature), "failure context signature");
  evidence_ids = canonical_ids(std::move(evidence_ids));
  if (evidence_ids.empty()) {
    failure_error(
        "SAA-12.1 failure observation requires evidence references");
  }
  provenance_id = trimmed(std::move(provenance_id));
  if (provenance_id.empty()) {
    failure_error("SAA-12.1 failure observation requires provenance_id");
  }
  semantic_roles = canonical_texts(std::move(semantic_roles));
  violated_invariants = canonical_texts(std::move(violated_invariants));
  const Json material =
      {{"boundary_signature", boundary_signature},
       {"component", component},
       {"context_signature", context_signature},
       {"evidence_ids", evidence_ids},
       {"failure_class", failure_class},
       {"mechanism", mechanism},
       {"provenance_id", provenance_id},
       {"semantic_roles", semantic_roles},
       {"source_kind", source_kind},
       {"version", failure_algebra_version},
       {"violated_invariants", violated_invariants}};
  return {.source_kind = std::move(source_kind),
          .component = std::move(component),
          .failure_class = std::move(failure_class),
          .mechanism = std::move(mechanism),
          .semantic_roles = std::move(semantic_roles),
          .violated_invariants = std::move(violated_invariants),
          .boundary_signature = std::move(boundary_signature),
          .context_signature = std::move(context_signature),
          .evidence_ids = std::move(evidence_ids),
          .provenance_id = std::move(provenance_id),
          .observation_signature = contracts::sha256_json(material)};
}

CanonicalFailurePattern
canonicalize_failure(const FailureObservation &observation) {
  const Json material =
      {{"boundary_signature", observation.boundary_signature},
       {"component", observation.component},
       {"context_signature", observation.context_signature},
       {"failure_class", observation.failure_class},
       {"mechanism", observation.mechanism},
       {"semantic_roles", observation.semantic_roles},
       {"version", failure_algebra_version},
       {"violated_invariants", observation.violated_invariants}};
  return {.failure_class = observation.failure_class,
          .component = observation.component,
          .mechanism = observation.mechanism,
          .semantic_roles = observation.semantic_roles,
          .violated_invariants = observation.violated_invariants,
          .boundary_signature = observation.boundary_signature,
          .context_signature = observation.context_signature,
          .pattern_signature = contracts::sha256_json(material)};
}

FailureMatchAssessment compare_failure_to_pattern(
    const FailureObservation &observation,
    const CanonicalFailurePattern &pattern, int prior_occurrence_count) {
  const auto candidate = canonicalize_failure(observation);
  std::vector<std::string> differences;
  if (candidate.failure_class != pattern.failure_class) {
    differences.push_back("FAILURE_CLASS");
  }
  if (candidate.component != pattern.component) {
    differences.push_back("COMPONENT");
  }
  if (candidate.mechanism != pattern.mechanism) {
    differences.push_back("MECHANISM");
  }
  if (candidate.semantic_roles != pattern.semantic_roles) {
    differences.push_back("SEMANTIC_ROLES");
  }
  if (candidate.violated_invariants != pattern.violated_invariants) {
    differences.push_back("VIOLATED_INVARIANTS");
  }
  if (candidate.boundary_signature != pattern.boundary_signature) {
    differences.push_back("BOUNDARY_SIGNATURE");
  }
  if (candidate.context_signature != pattern.context_signature) {
    differences.push_back("CONTEXT_SIGNATURE");
  }
  const bool exact = differences.empty() &&
                     candidate.pattern_signature == pattern.pattern_signature;
  std::string status;
  if (exact) {
    status = "EXACT_CANONICAL_FAILURE_MATCH";
  } else if (candidate.failure_class == pattern.failure_class &&
             candidate.mechanism == pattern.mechanism) {
    status = "SAME_FAILURE_MECHANISM_DIFFERENT_SCOPE";
  } else {
    status = "DISTINCT_FAILURE_PATTERN";
  }
  const bool retry_blocked = exact && prior_occurrence_count > 0;
  const Json payload =
      {{"differences", differences},
       {"observation_signature", observation.observation_signature},
       {"pattern_signature", pattern.pattern_signature},
       {"prior_occurrence_count", prior_occurrence_count},
       {"retry_blocked", retry_blocked},
       {"status", status},
       {"version", failure_algebra_version}};
  return {.status = std::move(status),
          .exact_match = exact,
          .retry_blocked = retry_blocked,
          .matched_pattern_signature =
              exact ? pattern.pattern_signature : std::string{},
          .differences = std::move(differences),
          .assessment_signature = contracts::sha256_json(payload)};
}

Json to_json(const FailureObservation &value) {
  return {{"boundary_signature", value.boundary_signature},
          {"component", value.component},
          {"context_signature", value.context_signature},
          {"evidence_ids", value.evidence_ids},
          {"failure_class", value.failure_class},
          {"mechanism", value.mechanism},
          {"observation_signature", value.observation_signature},
          {"provenance_id", value.provenance_id},
          {"semantic_roles", value.semantic_roles},
          {"source_kind", value.source_kind},
          {"violated_invariants", value.violated_invariants}};
}

Json to_json(const CanonicalFailurePattern &value) {
  return {{"boundary_signature", value.boundary_signature},
          {"component", value.component},
          {"context_signature", value.context_signature},
          {"failure_class", value.failure_class},
          {"mechanism", value.mechanism},
          {"pattern_signature", value.pattern_signature},
          {"semantic_roles", value.semantic_roles},
          {"violated_invariants", value.violated_invariants}};
}

Json to_json(const FailureMatchAssessment &value) {
  return {{"assessment_signature", value.assessment_signature},
          {"differences", value.differences},
          {"exact_match", value.exact_match},
          {"matched_pattern_signature", value.matched_pattern_signature},
          {"retry_blocked", value.retry_blocked},
          {"status", value.status}};
}

} // namespace statewright::saa
