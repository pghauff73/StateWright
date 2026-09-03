#include "statewright/egcf/evidence.hpp"

#include "ledger_support.hpp"
#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/contracts/typed_id.hpp"
#include "statewright/core/event_store.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::egcf {
namespace {

using Json = contracts::Json;

[[noreturn]] void evidence_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument,
                      "EGCF evidence: " + std::move(message));
}

[[nodiscard]] std::vector<std::string>
stable_unique(std::vector<std::string> values) {
  std::set<std::string> seen;
  std::vector<std::string> result;
  for (auto &value : values) {
    if (seen.insert(value).second) {
      result.push_back(std::move(value));
    }
  }
  return result;
}

[[nodiscard]] std::optional<std::chrono::system_clock::time_point>
parse_utc(std::string_view value) {
  if (value.size() != 20U || value.back() != 'Z') {
    return std::nullopt;
  }
  std::tm parts{};
  std::istringstream input{std::string(value)};
  input >> std::get_time(&parts, "%Y-%m-%dT%H:%M:%SZ");
  if (!input || input.peek() != std::char_traits<char>::eof()) {
    return std::nullopt;
  }
  const auto time = ::timegm(&parts);
  if (time == static_cast<std::time_t>(-1)) {
    return std::nullopt;
  }
  return std::chrono::system_clock::from_time_t(time);
}

[[nodiscard]] bool fresh(const EvidenceArtifact &artifact,
                         int freshness_seconds) {
  if (freshness_seconds <= 0) {
    return true;
  }
  const auto created = parse_utc(artifact.created_at);
  if (!created) {
    return false;
  }
  return std::chrono::system_clock::now() - *created <=
         std::chrono::seconds(freshness_seconds);
}

[[nodiscard]] bool intersects(const std::vector<std::string> &left,
                              const std::vector<std::string> &right) {
  for (const auto &value : left) {
    if (std::ranges::find(right, value) != right.end()) {
      return true;
    }
  }
  return false;
}

} // namespace

std::string EvidenceRequirement::object_id() const {
  return contracts::typed_id("evidence-requirement", to_json(*this));
}

std::string EvidenceArtifact::object_id() const {
  return contracts::typed_id("egcf-evidence", to_json(*this));
}

std::string ConfidenceAssessment::object_id() const {
  return contracts::typed_id("confidence-assessment", to_json(*this));
}

Json to_json(const EvidenceRequirement &requirement) {
  return {{"category", requirement.category},
          {"freshness_seconds", requirement.freshness_seconds},
          {"independence_group", requirement.independence_group},
          {"mandatory", requirement.mandatory},
          {"name", requirement.name},
          {"oracle", requirement.oracle},
          {"subject_id", requirement.subject_id}};
}

Json to_json(const EvidenceArtifact &artifact) {
  return {{"algorithm_id", artifact.algorithm_id},
          {"category", artifact.category},
          {"claim_ids", artifact.claim_ids},
          {"command_id", artifact.command_id},
          {"content", artifact.content},
          {"created_at", artifact.created_at},
          {"environment", artifact.environment},
          {"independence_group", artifact.independence_group},
          {"limitations", artifact.limitations},
          {"method", artifact.method},
          {"oracle", artifact.oracle},
          {"path", artifact.path},
          {"producer", artifact.producer},
          {"requirement_ids", artifact.requirement_ids},
          {"sha256", artifact.sha256},
          {"simulated", artifact.simulated},
          {"source_snapshot_hash", artifact.source_snapshot_hash},
          {"subject_id", artifact.subject_id},
          {"success", artifact.success ? Json(*artifact.success) : Json(nullptr)},
          {"target", artifact.target}};
}

Json to_json(const ConfidenceAssessment &assessment) {
  return {{"blocking_gaps", assessment.blocking_gaps},
          {"conclusion", assessment.conclusion},
          {"conflicts", assessment.conflicts},
          {"created_at", assessment.created_at},
          {"dimensions", assessment.dimensions},
          {"evidence_ids", assessment.evidence_ids},
          {"known_unknowns", assessment.known_unknowns},
          {"policy", assessment.policy},
          {"subject_id", assessment.subject_id}};
}

EvidenceRequirement evidence_requirement_from_json(const Json &value) {
  try {
    return {.subject_id = value.at("subject_id"),
            .name = value.at("name"),
            .category = value.at("category"),
            .oracle = value.at("oracle"),
            .freshness_seconds = value.at("freshness_seconds"),
            .independence_group = value.at("independence_group"),
            .mandatory = value.at("mandatory")};
  } catch (const std::exception &exception) {
    evidence_error(std::string("invalid evidence requirement: ") +
                   exception.what());
  }
}

EvidenceArtifact evidence_artifact_from_json(const Json &value) {
  try {
    EvidenceArtifact result = {
        .subject_id = value.at("subject_id"),
        .claim_ids = value.at("claim_ids"),
        .requirement_ids = value.at("requirement_ids"),
        .category = value.at("category"),
        .producer = value.at("producer"),
        .method = value.at("method"),
        .source_snapshot_hash = value.at("source_snapshot_hash"),
        .target = value.at("target"),
        .oracle = value.at("oracle"),
        .environment = value.at("environment"),
        .command_id = value.at("command_id"),
        .algorithm_id = value.at("algorithm_id"),
        .created_at = value.at("created_at"),
        .sha256 = value.at("sha256"),
        .success = std::nullopt,
        .limitations = value.at("limitations"),
        .independence_group = value.at("independence_group"),
        .simulated = value.at("simulated"),
        .path = value.at("path"),
        .content = value.at("content")};
    if (!value.at("success").is_null()) {
      result.success = value.at("success").get<bool>();
    }
    return result;
  } catch (const std::exception &exception) {
    evidence_error(std::string("invalid evidence artifact: ") +
                   exception.what());
  }
}

EvidenceManager::EvidenceManager(EgcfStore &store) : store_(store) {}

std::string
EvidenceManager::add_requirement(const EvidenceRequirement &requirement) {
  if (requirement.subject_id.empty() || requirement.name.empty() ||
      requirement.category.empty() || requirement.freshness_seconds < 0) {
    evidence_error("evidence requirement fields are invalid");
  }
  return store_.register_record(
      {.object_type = "evidence-requirement", .payload = to_json(requirement)},
      "egcf_evidence_requirement_registered");
}

std::string EvidenceManager::collect(EvidenceInput input) {
  if (input.subject_id.empty() || input.category.empty() ||
      input.producer.empty() || input.method.empty() ||
      input.source_snapshot_hash.empty()) {
    evidence_error("evidence requires subject, category, producer, method, and source snapshot");
  }
  const auto safe_content = core::redact(input.content);
  const auto safe_environment = core::redact(input.environment);
  const auto independence_group = input.independence_group.empty()
                                      ? "producer:" + input.producer
                                      : input.independence_group;
  EvidenceArtifact artifact = {
      .subject_id = std::move(input.subject_id),
      .claim_ids = stable_unique(std::move(input.claim_ids)),
      .requirement_ids = stable_unique(std::move(input.requirement_ids)),
      .category = std::move(input.category),
      .producer = std::move(input.producer),
      .method = std::move(input.method),
      .source_snapshot_hash = std::move(input.source_snapshot_hash),
      .target = std::move(input.target),
      .oracle = std::move(input.oracle),
      .environment = safe_environment,
      .command_id = std::move(input.command_id),
      .algorithm_id = std::move(input.algorithm_id),
      .created_at = ledger_support::utc_now(),
      .sha256 = contracts::sha256_json(safe_content),
      .success = input.success,
      .limitations = stable_unique(std::move(input.limitations)),
      .independence_group = independence_group,
      .simulated = input.simulated,
      .path = std::move(input.path),
      .content = safe_content};
  return store_.register_record(
      {.object_type = "egcf-evidence", .payload = to_json(artifact)},
      "egcf_evidence_collected");
}

std::vector<std::pair<std::string, EvidenceRequirement>>
EvidenceManager::requirements(std::string_view subject_id) {
  std::vector<std::pair<std::string, EvidenceRequirement>> result;
  for (const auto &stored : store_.list("evidence-requirement")) {
    auto requirement = evidence_requirement_from_json(stored.payload);
    if (requirement.subject_id == subject_id) {
      result.emplace_back(stored.object_id, std::move(requirement));
    }
  }
  return result;
}

std::vector<EvidenceArtifact>
EvidenceManager::artifacts(std::string_view subject_id) {
  std::vector<EvidenceArtifact> result;
  for (const auto &stored : store_.list("egcf-evidence")) {
    auto artifact = evidence_artifact_from_json(stored.payload);
    if (artifact.subject_id == subject_id) {
      result.push_back(std::move(artifact));
    }
  }
  return result;
}

Json EvidenceManager::coverage(std::string_view subject_id) {
  const auto requirement_records = requirements(subject_id);
  const auto artifact_records = artifacts(subject_id);
  Json matrix = Json::array();
  std::vector<std::string> missing;
  std::set<std::string> used;
  std::size_t covered_count = 0;
  for (const auto &[requirement_id, requirement] : requirement_records) {
    std::vector<std::string> matches;
    for (const auto &artifact : artifact_records) {
      const auto artifact_id = artifact.object_id();
      if (!used.contains(artifact_id) &&
          std::ranges::find(artifact.requirement_ids, requirement_id) !=
              artifact.requirement_ids.end() &&
          artifact.category == requirement.category &&
          fresh(artifact, requirement.freshness_seconds) &&
          (requirement.oracle.empty() || artifact.oracle == requirement.oracle) &&
          (requirement.independence_group.empty() ||
           artifact.independence_group == requirement.independence_group) &&
          (!artifact.simulated ||
           requirement.category.starts_with("simulation")) &&
          (!artifact.success || *artifact.success)) {
        matches.push_back(artifact_id);
        used.insert(artifact_id);
        break;
      }
    }
    const bool covered = !matches.empty();
    if (covered) {
      ++covered_count;
    } else if (requirement.mandatory) {
      missing.push_back(requirement_id);
    }
    matrix.push_back({{"covered", covered},
                      {"evidence_ids", matches},
                      {"mandatory", requirement.mandatory},
                      {"name", requirement.name},
                      {"requirement_id", requirement_id}});
  }
  const double coverage_value =
      requirement_records.empty()
          ? 1.0
          : static_cast<double>(covered_count) /
                static_cast<double>(requirement_records.size());
  return {{"artifact_reuse_forbidden", true},
          {"coverage", coverage_value},
          {"missing_mandatory", missing},
          {"requirements", matrix},
          {"subject_id", subject_id}};
}

Json EvidenceManager::uniqueness(std::string_view subject_id) {
  std::map<std::string, std::vector<std::string>> by_hash;
  std::map<std::string, std::vector<std::string>> by_group;
  for (const auto &artifact : artifacts(subject_id)) {
    by_hash[artifact.sha256].push_back(artifact.object_id());
    by_group[artifact.independence_group].push_back(artifact.object_id());
  }
  Json duplicates = Json::object();
  Json dependent = Json::object();
  for (const auto &[key, ids] : by_hash) {
    if (ids.size() > 1U) {
      duplicates[key] = ids;
    }
  }
  for (const auto &[key, ids] : by_group) {
    if (ids.size() > 1U) {
      dependent[key] = ids;
    }
  }
  return {{"dependent_groups", dependent},
          {"duplicate_content", duplicates},
          {"independent", dependent.empty()},
          {"subject_id", subject_id},
          {"unique", duplicates.empty() && dependent.empty()}};
}

Json EvidenceManager::conflicts(std::string_view subject_id) {
  const auto values = artifacts(subject_id);
  Json result = Json::array();
  for (std::size_t left_index = 0; left_index < values.size(); ++left_index) {
    for (std::size_t right_index = left_index + 1U;
         right_index < values.size(); ++right_index) {
      const auto &left = values.at(left_index);
      const auto &right = values.at(right_index);
      const bool opposite = left.success && right.success &&
                            *left.success != *right.success;
      if (opposite && left.simulated == right.simulated &&
          (intersects(left.claim_ids, right.claim_ids) ||
           intersects(left.requirement_ids, right.requirement_ids))) {
        result.push_back(
            {{"left", left.object_id()},
             {"reason",
              "opposite evidence outcomes for the same claim or requirement"},
             {"right", right.object_id()}});
      }
    }
  }
  return result;
}

ConfidenceAssessment EvidenceManager::confidence(std::string_view subject_id,
                                                 std::string policy) {
  const auto values = artifacts(subject_id);
  const auto coverage_value = coverage(subject_id);
  const auto conflict_values = conflicts(subject_id);
  const auto uniqueness_value = uniqueness(subject_id);
  std::set<std::string> groups;
  std::set<std::string> unknowns;
  double oracle_strength = 0.0;
  double source_authority = 0.0;
  double reproducibility = 0.0;
  double freshness = 0.0;
  bool counterexamples_present = false;
  for (const auto &artifact : values) {
    oracle_strength += artifact.oracle.empty() ? 0.0 : 1.0;
    source_authority +=
        artifact.producer.starts_with("deterministic") ||
                artifact.producer.starts_with("human")
            ? 1.0
            : 0.5;
    reproducibility +=
        !artifact.environment.empty() && !artifact.source_snapshot_hash.empty()
            ? 1.0
            : 0.0;
    freshness += artifact.created_at.empty() ? 0.0 : 1.0;
    counterexamples_present =
        counterexamples_present || artifact.category == "counterexample";
    groups.insert(artifact.independence_group);
    unknowns.insert(artifact.limitations.begin(), artifact.limitations.end());
  }
  const double denominator = values.empty() ? 1.0
                                             : static_cast<double>(values.size());
  const double conflict_resistance =
      conflict_values.empty()
          ? 1.0
          : std::max(0.0, 1.0 -
                              static_cast<double>(conflict_values.size()) /
                                  denominator);
  const Json dimensions = {
      {"conflict_resistance", conflict_resistance},
      {"counterexample_coverage", counterexamples_present ? 1.0 : 0.0},
      {"coverage", coverage_value.at("coverage")},
      {"freshness", values.empty() ? 0.0 : freshness / denominator},
      {"independence",
       values.empty() ? 0.0
                      : static_cast<double>(groups.size()) / denominator},
      {"oracle_strength",
       values.empty() ? 0.0 : oracle_strength / denominator},
      {"relevance", 1.0},
      {"reproducibility",
       values.empty() ? 0.0 : reproducibility / denominator},
      {"source_authority",
       values.empty() ? 0.0 : source_authority / denominator}};
  auto blocking =
      coverage_value.at("missing_mandatory").get<std::vector<std::string>>();
  if (!uniqueness_value.at("duplicate_content").empty()) {
    blocking.push_back("duplicate evidence content");
  }
  if (!uniqueness_value.at("dependent_groups").empty()) {
    blocking.push_back("evidence independence groups are reused");
  }
  std::vector<std::string> conflict_reasons;
  for (const auto &conflict : conflict_values) {
    conflict_reasons.push_back(conflict.at("reason").get<std::string>());
  }
  double total = 0.0;
  for (const auto &[name, value] : dimensions.items()) {
    static_cast<void>(name);
    total += value.get<double>();
  }
  const double average = total / static_cast<double>(dimensions.size());
  const std::string conclusion =
      !blocking.empty() || !conflict_reasons.empty()
          ? "BLOCKED"
          : average >= 0.85 ? "HIGH" : average >= 0.6 ? "MEDIUM" : "LOW";
  std::vector<std::string> evidence_ids;
  for (const auto &artifact : values) {
    evidence_ids.push_back(artifact.object_id());
  }
  ConfidenceAssessment assessment = {
      .subject_id = std::string(subject_id),
      .policy = std::move(policy),
      .dimensions = dimensions,
      .blocking_gaps = std::move(blocking),
      .conflicts = std::move(conflict_reasons),
      .known_unknowns = {unknowns.begin(), unknowns.end()},
      .conclusion = conclusion,
      .evidence_ids = std::move(evidence_ids),
      .created_at = ledger_support::utc_now()};
  const auto assessment_id = store_.register_record(
      {.object_type = "confidence-assessment", .payload = to_json(assessment)},
      "egcf_confidence_assessed");
  if (assessment_id != assessment.object_id()) {
    evidence_error("confidence assessment identity mismatch");
  }
  return assessment;
}

Json EvidenceManager::graph(std::string_view subject_id) {
  Json nodes = Json::array();
  Json edges = Json::array();
  for (const auto &[requirement_id, requirement] : requirements(subject_id)) {
    nodes.push_back({{"id", requirement_id},
                     {"label", requirement.name},
                     {"type", "requirement"}});
  }
  for (const auto &artifact : artifacts(subject_id)) {
    nodes.push_back({{"id", artifact.object_id()},
                     {"label", artifact.category},
                     {"type", "evidence"}});
    for (const auto &requirement_id : artifact.requirement_ids) {
      edges.push_back({{"from", artifact.object_id()},
                       {"relation", "supports"},
                       {"to", requirement_id}});
    }
  }
  return {{"edges", edges}, {"nodes", nodes}};
}

Ieps::Ieps(EvidenceManager &evidence) : evidence_(evidence) {}

std::string Ieps::oracle(std::string subject_id, std::string name,
                         std::string category, std::string oracle_value,
                         bool mandatory, int freshness_seconds,
                         std::string independence_group) {
  return evidence_.add_requirement(
      {.subject_id = std::move(subject_id),
       .name = name,
       .category = std::move(category),
       .oracle = std::move(oracle_value),
       .freshness_seconds = freshness_seconds,
       .independence_group = independence_group.empty()
                                 ? std::move(name)
                                 : std::move(independence_group),
       .mandatory = mandatory});
}

Json Ieps::qualify(std::string_view subject_id) {
  const auto coverage_value = evidence_.coverage(subject_id);
  const auto confidence_value = evidence_.confidence(subject_id);
  const auto conflict_values = evidence_.conflicts(subject_id);
  const bool qualified =
      coverage_value.at("missing_mandatory").empty() &&
      conflict_values.empty() && confidence_value.conclusion != "BLOCKED";
  return {{"confidence", to_json(confidence_value)},
          {"confidence_id", confidence_value.object_id()},
          {"conflicts", conflict_values},
          {"coverage", coverage_value},
          {"evaluated_at", ledger_support::utc_now()},
          {"qualified", qualified},
          {"subject_id", subject_id}};
}

Json Ieps::gate(std::string_view subject_id) {
  const auto qualification = qualify(subject_id);
  const bool qualified = qualification.at("qualified").get<bool>();
  return {{"qualification", qualification},
          {"reason", qualified
                         ? "all mandatory evidence gates passed"
                         : "mandatory evidence, conflict, or confidence gate failed"},
          {"subject_id", subject_id},
          {"verdict", qualified ? "APPROVE" : "REFUSE"}};
}

Json Ieps::coverage(std::string_view subject_id) {
  return evidence_.coverage(subject_id);
}

Json Ieps::uniqueness(std::string_view subject_id) {
  return evidence_.uniqueness(subject_id);
}

Json Ieps::counterexamples(const Json &candidates,
                           const Json &predicate_results) {
  if (!candidates.is_array() || !predicate_results.is_array()) {
    evidence_error("counterexamples requires two arrays");
  }
  const auto count = std::min(candidates.size(), predicate_results.size());
  Json failures = Json::array();
  for (std::size_t index = 0; index < count; ++index) {
    if (!predicate_results.at(index).get<bool>()) {
      failures.push_back(candidates.at(index));
    }
  }
  return {{"counterexamples", failures},
          {"found", !failures.empty()},
          {"tested", count}};
}

Json Ieps::mutation(const Json &items) {
  if (!items.is_array()) {
    evidence_error("mutation requires an array");
  }
  Json survivors = Json::array();
  std::size_t detected = 0;
  for (const auto &item : items) {
    if (!item.is_object()) {
      evidence_error("mutation entries must be objects");
    }
    if (item.value("detected", false)) {
      ++detected;
    } else {
      survivors.push_back(item);
    }
  }
  return {{"detected", detected},
          {"mutation_score",
           items.empty() ? 1.0
                         : static_cast<double>(detected) /
                               static_cast<double>(items.size())},
          {"survivors", survivors},
          {"total", items.size()}};
}

Json Ieps::shrink(const Json &sequence, const Json &required) {
  if (!sequence.is_array() || !required.is_array()) {
    evidence_error("shrink requires two arrays");
  }
  Json minimized = Json::array();
  for (const auto &item : sequence) {
    if (std::ranges::find(required, item) != required.end()) {
      minimized.push_back(item);
    }
  }
  Json missing = Json::array();
  for (const auto &item : required) {
    if (std::ranges::find(minimized, item) == minimized.end()) {
      missing.push_back(item);
    }
  }
  return {{"minimized", minimized},
          {"missing_required", missing},
          {"original_size", sequence.size()},
          {"preserved", missing.empty()}};
}

} // namespace statewright::egcf
