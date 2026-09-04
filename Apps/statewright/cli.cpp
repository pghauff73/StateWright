#include "cli.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/build_identity.hpp"
#include "statewright/contracts/canonical_json.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/contracts/typed_id.hpp"
#include "statewright/core/authority.hpp"
#include "statewright/core/file_io.hpp"
#include "statewright/core/operational_hypotheses.hpp"
#include "statewright/core/workspace.hpp"
#include "statewright/egcf/brain_feed.hpp"
#include "statewright/egcf/autonomous_promotion.hpp"
#include "statewright/egcf/engine.hpp"
#include "statewright/egcf/internet_experiment.hpp"
#include "statewright/egcf/internet_feed.hpp"
#include "statewright/egcf/internet_improvement_orchestrator.hpp"
#include "statewright/egcf/internet_improvement_store.hpp"
#include "statewright/egcf/internet_probation.hpp"
#include "statewright/egcf/internet_reasoning.hpp"
#include "statewright/egcf/internet_source_coordinator.hpp"
#include "statewright/egcf/knowledge_governance_store.hpp"
#include "statewright/egcf/registry.hpp"
#include "statewright/egcf/workflow.hpp"
#include "statewright/reasoning/benchmark.hpp"
#include "statewright/reasoning/hypotheses.hpp"
#include "statewright/reasoning/qualification.hpp"
#include "statewright/saa/algorithm_ir.hpp"
#include "statewright/saa/knowledge_integrity.hpp"
#include "statewright/saa/oiec_bench_gate.hpp"
#include "statewright/saa/search.hpp"
#include "statewright/sources/extraction.hpp"
#include "statewright/sources/http_provider.hpp"
#include "statewright/sources/scheduler.hpp"
#include "statewright/sources/snapshot.hpp"
#include "statewright/sources/watchlist.hpp"

#include <algorithm>
#include <array>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <gmpxx.h>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::app {
namespace {

using Json = contracts::Json;

[[nodiscard]] std::string timestamp_after(std::string_view timestamp,
                                          int seconds) {
  if (timestamp.size() != 20U) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "timestamp must use canonical UTC form");
  }
  std::tm parts{};
  const std::string value(timestamp);
  if (strptime(value.c_str(), "%Y-%m-%dT%H:%M:%SZ", &parts) == nullptr) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "timestamp must use canonical UTC form");
  }
  const std::time_t shifted = timegm(&parts) + seconds;
  std::tm shifted_parts{};
  if (gmtime_r(&shifted, &shifted_parts) == nullptr) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "timestamp is out of range");
  }
  std::array<char, 21> buffer{};
  if (strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ",
               &shifted_parts) != 20U) {
    throw common::Error(common::ErrorCode::internal_failure,
                        "timestamp formatting failed");
  }
  return buffer.data();
}

[[nodiscard]] std::time_t timestamp_value(std::string_view timestamp) {
  if (timestamp.size() != 20U) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "timestamp must use canonical UTC form");
  }
  std::tm parts{};
  const std::string value(timestamp);
  if (strptime(value.c_str(), "%Y-%m-%dT%H:%M:%SZ", &parts) == nullptr) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "timestamp must use canonical UTC form");
  }
  const std::time_t result = timegm(&parts);
  if (result == static_cast<std::time_t>(-1)) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "timestamp is out of range");
  }
  return result;
}

void usage(std::ostream &output) {
  output
      << "Usage:\n"
      << "  statewright version [--json]\n"
      << "  statewright <operation> <request-json-or-@file>\n"
      << "\nOperations:\n"
      << "  inspect reason hypothesis algorithm retrieve explain command compile\n"
      << "  simulate approve execute verify rollback replay ledger-verify\n"
      << "  projection-rebuild benchmark qualification brain-feed repository-feed\n"
      << "  internet-watch internet-watchlist internet-poll internet-fetch internet-source\n"
      << "  internet-extract internet-candidate internet-improvement\n"
      << "  internet-promotion-policy internet-probation internet-integrity\n"
      << "\nLegacy operations:\n"
      << "  canonicalize hash hash-json typed-id operational-hypothesis\n"
      << "  reasoning-hypothesis-set saa-canonicalize saa-search\n"
      << "  egcf-command-describe\n";
}

[[nodiscard]] std::vector<std::string> strings(const Json &value,
                                                std::string_view key) {
  const auto iterator = value.find(std::string(key));
  if (iterator == value.end() || iterator->is_null()) {
    return {};
  }
  if (!iterator->is_array()) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        std::string(key) + " must be an array");
  }
  return iterator->get<std::vector<std::string>>();
}

[[nodiscard]] core::HypothesisProposal operational_proposal(
    const Json &value) {
  return {.proposition = value.value("proposition", ""),
          .model_prior_bp = value.value("model_prior_bp", 5'000),
          .assumptions = strings(value, "assumptions"),
          .predictions = strings(value, "predictions"),
          .falsifiers = strings(value, "falsifiers")};
}

[[nodiscard]] reasoning::HypothesisProposal reasoning_proposal(
    const Json &value) {
  std::optional<int> posterior;
  if (value.contains("posterior_bp") && !value.at("posterior_bp").is_null()) {
    posterior = value.at("posterior_bp").get<int>();
  }
  return {.hypothesis_id = value.value("hypothesis_id", ""),
          .proposition = value.value("proposition", ""),
          .prior_bp = value.value("prior_bp", 0),
          .posterior_bp = posterior,
          .supporting_evidence = strings(value, "supporting_evidence"),
          .conflicting_evidence = strings(value, "conflicting_evidence"),
          .assumptions = strings(value, "assumptions"),
          .predictions = strings(value, "predictions"),
          .falsifiers = strings(value, "falsifiers"),
          .status = value.value("status", "ACTIVE")};
}

[[nodiscard]] saa::AlgorithmSearchQuery search_query(const Json &value) {
  saa::AlgorithmSearchQuery query;
  if (value.contains("structural_hash") &&
      !value.at("structural_hash").is_null()) {
    query.structural_hash = value.at("structural_hash").get<std::string>();
  }
  if (value.contains("domain") && !value.at("domain").is_null()) {
    query.domain = value.at("domain").get<std::string>();
  }
  query.required_primitives = strings(value, "required_primitives");
  query.semantic_terms = strings(value, "semantic_terms");
  query.required_invariants = strings(value, "required_invariants");
  query.require_qualified = value.value("require_qualified", true);
  query.limit = value.value("limit", 10U);
  return query;
}

[[nodiscard]] Json execute_reasoning_set(const Json &request) {
  std::vector<reasoning::HypothesisProposal> proposals;
  for (const auto &proposal : request.at("proposals")) {
    proposals.push_back(reasoning_proposal(proposal));
  }
  return reasoning::to_json(reasoning::build_hypothesis_set(
      proposals, request.at("problem_id").get<std::string>(),
      request.value("max_hypotheses", 16),
      request.value("mutually_exclusive", false),
      strings(request, "update_ids")));
}

[[nodiscard]] Json execute_saa_search(const Json &algorithms,
                                      const Json &query_payload) {
  if (!algorithms.is_array()) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "algorithms must be an array");
  }
  saa::AlgorithmSearchIndex index;
  for (const auto &item : algorithms) {
    static_cast<void>(index.register_algorithm(saa::make_searchable_algorithm(
        saa::canonicalize_mapping(item.at("structure")),
        item.at("domain").get<std::string>(), strings(item, "semantic_terms"),
        strings(item, "invariants"), strings(item, "evidence_ids"),
        item.value("qualification_status", "CANDIDATE"),
        item.value("source_snapshot_hash", ""))));
  }
  return saa::to_json(index.search(search_query(query_payload)));
}

[[nodiscard]] Json request_json(std::string_view argument) {
  if (argument.starts_with('@')) {
    return contracts::parse_json(
        core::read_text(std::filesystem::path(argument.substr(1U))));
  }
  return contracts::parse_json(argument);
}

[[nodiscard]] std::filesystem::path request_root(const Json &request) {
  return std::filesystem::weakly_canonical(
      request.value("workspace", std::string(".")));
}

[[nodiscard]] bool contains_path(const std::filesystem::path &root,
                                 const std::filesystem::path &path) {
  const auto mismatch = std::mismatch(root.begin(), root.end(), path.begin(),
                                      path.end());
  return mismatch.first == root.end();
}

[[nodiscard]] bool is_resource_root(const std::filesystem::path &root) {
  return std::filesystem::is_regular_file(root / "manifest.sha256");
}

[[nodiscard]] std::filesystem::path default_resource_root() {
  if (const char *override_root = std::getenv("STATEWRIGHT_RESOURCE_ROOT");
      override_root != nullptr && *override_root != '\0') {
    return std::filesystem::weakly_canonical(override_root);
  }

  std::error_code error;
  const auto executable = std::filesystem::canonical("/proc/self/exe", error);
  if (!error) {
    const std::filesystem::path install_subdir(
        STATEWRIGHT_INSTALL_RESOURCE_SUBDIR);
    const auto installed = std::filesystem::weakly_canonical(
        install_subdir.is_absolute()
            ? install_subdir
            : executable.parent_path().parent_path() / install_subdir);
    if (is_resource_root(installed)) {
      return installed;
    }

    const auto build_root = std::filesystem::weakly_canonical(
        std::filesystem::path(STATEWRIGHT_BUILD_BINARY_ROOT));
    if (contains_path(build_root, executable)) {
      const auto source_resources = std::filesystem::weakly_canonical(
          std::filesystem::path(STATEWRIGHT_BUILD_RESOURCE_ROOT));
      if (is_resource_root(source_resources)) {
        return source_resources;
      }
    }
  }

  throw common::Error(
      common::ErrorCode::filesystem_failure,
      "cannot locate StateWright resources; install the resource bundle or set "
      "STATEWRIGHT_RESOURCE_ROOT");
}

[[nodiscard]] std::filesystem::path resource_root(const Json &request) {
  if (request.contains("resource_root")) {
    return std::filesystem::weakly_canonical(
        request.at("resource_root").get<std::string>());
  }
  return default_resource_root();
}

[[nodiscard]] core::AuthorityManifest default_authority(
    const core::Workspace &workspace,
    const std::filesystem::path &resources) {
  egcf::CommandRegistry registry(resources);
  std::set<std::string> capabilities;
  for (const auto &definition : registry.definitions()) {
    const auto level = definition.capability_query.value("level", "C0");
    if (level == "C0" || level == "C1" || level == "C2") {
      const auto facets = definition.capability_query.value(
          "facets", std::vector<std::string>{});
      capabilities.insert(facets.begin(), facets.end());
    }
  }
  core::AuthorityManifest authority;
  authority.task_id = "statewright-cli-read-only";
  authority.goal = "Bounded local StateWright inspection and analysis";
  authority.source_snapshot_hash = workspace.snapshot_hash();
  authority.allowed_paths = {"**"};
  authority.forbidden_paths = {".ourd-agent/**"};
  authority.read_capabilities = {"filesystem.read", "workspace.list",
                                 "workspace.read", "workspace.search"};
  authority.semantic_capability_ceiling = "C2";
  authority.semantic_capabilities = {capabilities.begin(), capabilities.end()};
  authority.operator_name = "statewright-cli";
  authority.read_only = true;
  return core::finalize_authority(std::move(authority));
}

[[nodiscard]] core::AuthorityManifest request_authority(
    const Json &request, const core::Workspace &workspace,
    const std::filesystem::path &resources) {
  const auto found = request.find("authority");
  if (found == request.end() || found->is_null()) {
    return default_authority(workspace, resources);
  }
  Json value;
  if (found->is_object()) {
    value = *found;
  } else if (found->is_string()) {
    value = contracts::parse_json(
        core::read_text(std::filesystem::path(found->get<std::string>())));
  } else {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "authority must be an object or JSON file path");
  }
  return core::authority_from_json(value);
}

[[nodiscard]] egcf::CommandContext command_context(const Json &request) {
  const auto found = request.find("context");
  if (found == request.end()) {
    return {};
  }
  return egcf::command_context_from_json(*found);
}

class EngineSession final {
public:
  EngineSession(const Json &request, bool recovery = false)
      : root_(request_root(request)), resources_(resource_root(request)),
        workspace_(root_),
        authority_(request_authority(request, workspace_, resources_)),
        engine_(root_, resources_, authority_,
                request.value("actor", std::string("user")), recovery) {}

  [[nodiscard]] egcf::EgcfEngine &engine() noexcept { return engine_; }
  [[nodiscard]] const core::Workspace &workspace() const noexcept {
    return workspace_;
  }

private:
  std::filesystem::path root_;
  std::filesystem::path resources_;
  core::Workspace workspace_;
  core::AuthorityManifest authority_;
  egcf::EgcfEngine engine_;
};

[[nodiscard]] Json stored_objects(const std::vector<egcf::StoredObject> &items) {
  Json result = Json::array();
  for (const auto &item : items) {
    result.push_back(egcf::to_json(item));
  }
  return result;
}

[[nodiscard]] Json record_json(const egcf::EgcfRecord &record) {
  return {{"object_id", record.object_id()},
          {"object_type", record.object_type},
          {"payload", record.payload}};
}

[[nodiscard]] sources::InternetExtractionResult extraction_from_store(
    egcf::EgcfStore &store, std::string_view receipt_id) {
  const auto stored = store.get(receipt_id);
  if (stored.object_type != "internet-extraction-receipt") {
    throw common::Error(
        common::ErrorCode::invalid_argument,
        "extraction_receipt_id does not reference an internet extraction");
  }
  sources::InternetExtractionResult result;
  result.receipt =
      sources::internet_extraction_receipt_from_json(stored.payload);
  for (const auto &fragment_id : result.receipt.fragment_ids) {
    const auto fragment = store.get(fragment_id);
    if (fragment.object_type != "internet-source-fragment") {
      throw common::Error(common::ErrorCode::json_contract,
                          "extraction fragment reference has wrong type");
    }
    result.fragments.push_back(
        sources::internet_source_fragment_from_json(fragment.payload));
  }
  return result;
}

[[nodiscard]] mpq_class exact_rational(const Json &value,
                                       std::string_view label) {
  try {
    if (value.is_number_integer()) {
      return mpq_class(value.get<long>());
    }
    if (value.is_number_unsigned()) {
      return mpq_class(value.get<unsigned long>());
    }
    if (value.is_string()) {
      mpq_class result(value.get<std::string>());
      result.canonicalize();
      return result;
    }
  } catch (const std::exception &) {
  }
  throw common::Error(common::ErrorCode::invalid_argument,
                      std::string(label) +
                          " must be an integer or exact rational string");
}

[[nodiscard]] std::vector<mpq_class> exact_rationals(
    const Json &value, std::string_view label) {
  if (!value.is_array()) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        std::string(label) + " must be an array");
  }
  std::vector<mpq_class> result;
  result.reserve(value.size());
  for (const auto &item : value) {
    result.push_back(exact_rational(item, label));
  }
  return result;
}

[[nodiscard]] std::vector<std::pair<std::string, int>> benchmark_scores(
    const Json &request) {
  const auto &value = request.at("benchmark_track_scores");
  if (!value.is_object()) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "benchmark_track_scores must be an object");
  }
  std::vector<std::pair<std::string, int>> result;
  for (const auto &[name, score] : value.items()) {
    result.emplace_back(name, score.get<int>());
  }
  return result;
}

[[nodiscard]] saa::OIECBenchGatePolicy benchmark_policy(const Json &request) {
  saa::OIECBenchGatePolicy result;
  if (!request.contains("benchmark_policy")) {
    return result;
  }
  const auto &value = request.at("benchmark_policy");
  if (value.contains("minimum_track_scores")) {
    for (const auto &[name, score] :
         value.at("minimum_track_scores").items()) {
      result.minimum_track_scores.emplace_back(name, score.get<int>());
    }
  }
  result.minimum_independence_groups = value.value(
      "minimum_independence_groups", result.minimum_independence_groups);
  return result;
}

[[nodiscard]] saa::KnowledgeIntegritySnapshot integrity_snapshot(
    const Json &value) {
  return saa::make_integrity_snapshot(
      value.at("generation").get<int>(),
      value.at("canonical_knowledge_count").get<int>(),
      value.value("semantic_contradictions", 0),
      value.value("semantic_drift_events", 0),
      value.value("false_canonical_admissions", 0),
      value.value("corrected_error_opportunities", 0),
      value.value("corrected_error_recurrences", 0),
      value.value("retrieval_queries", 0),
      value.value("retrieval_correct_selections", 0),
      value.value("equivalent_failure_opportunities", 0),
      value.value("equivalent_failure_retries", 0));
}

[[nodiscard]] saa::KnowledgeIntegrityPolicy integrity_policy(
    const Json &request) {
  saa::KnowledgeIntegrityPolicy result;
  if (!request.contains("integrity_policy")) {
    return result;
  }
  const auto &value = request.at("integrity_policy");
  result.max_contradiction_rate_bp = value.value(
      "max_contradiction_rate_bp", result.max_contradiction_rate_bp);
  result.max_semantic_drift_rate_bp = value.value(
      "max_semantic_drift_rate_bp", result.max_semantic_drift_rate_bp);
  result.max_false_admission_rate_bp = value.value(
      "max_false_admission_rate_bp", result.max_false_admission_rate_bp);
  result.max_corrected_error_recurrence_rate_bp = value.value(
      "max_corrected_error_recurrence_rate_bp",
      result.max_corrected_error_recurrence_rate_bp);
  result.min_retrieval_precision_bp = value.value(
      "min_retrieval_precision_bp", result.min_retrieval_precision_bp);
  result.min_equivalent_failure_avoidance_bp = value.value(
      "min_equivalent_failure_avoidance_bp",
      result.min_equivalent_failure_avoidance_bp);
  return result;
}

[[nodiscard]] egcf::InternetExperimentRequest experiment_request(
    const Json &request) {
  egcf::InternetExperimentRequest result;
  result.baseline_ref = request.at("baseline_ref").get<std::string>();
  result.baseline_saa_ir = request.at("baseline_saa_ir");
  result.dataset_snapshot_ids = strings(request, "dataset_snapshot_ids");
  for (const auto &value : request.at("trial_groups")) {
    egcf::InternetScalarTrialGroup group;
    group.independence_group =
        value.at("independence_group").get<std::string>();
    group.baseline_context_signature =
        value.value("baseline_context_signature", std::string{});
    group.candidate_context_signature =
        value.value("candidate_context_signature", std::string{});
    group.deterministic_seed = value.at("deterministic_seed").get<int>();
    group.inputs = exact_rationals(value.at("inputs"), "trial inputs");
    group.expected_outputs = exact_rationals(
        value.at("expected_outputs"), "trial expected_outputs");
    result.trial_groups.push_back(std::move(group));
  }
  result.context_signature = request.value("context_signature", std::string{});
  if (result.context_signature.empty()) {
    result.context_signature = egcf::internet_experiment_context_signature(
        result.dataset_snapshot_ids, result.trial_groups);
  }
  for (auto &group : result.trial_groups) {
    if (group.baseline_context_signature.empty()) {
      group.baseline_context_signature = result.context_signature;
    }
    if (group.candidate_context_signature.empty()) {
      group.candidate_context_signature = result.context_signature;
    }
  }
  result.minimum_material_effect = exact_rational(
      request.value("minimum_material_effect", Json(0)),
      "minimum_material_effect");
  result.minimum_output = exact_rational(
      request.value("minimum_output", Json(-1000000)), "minimum_output");
  result.maximum_output = exact_rational(
      request.value("maximum_output", Json(1000000)), "maximum_output");
  result.minimum_trials_per_group = request.value(
      "minimum_trials_per_group", result.minimum_trials_per_group);
  result.minimum_experiments =
      request.value("minimum_experiments", result.minimum_experiments);
  result.minimum_independence_groups = request.value(
      "minimum_independence_groups", result.minimum_independence_groups);
  result.maximum_total_trials =
      request.value("maximum_total_trials", result.maximum_total_trials);
  result.benchmark_track_scores = benchmark_scores(request);
  result.benchmark_policy = benchmark_policy(request);
  for (const auto &value : request.at("integrity_snapshots")) {
    result.integrity_snapshots.push_back(integrity_snapshot(value));
  }
  result.integrity_policy = integrity_policy(request);
  result.independent_review = request.value("independent_review", true);
  result.recorded_at = request.at("recorded_at").get<std::string>();
  return result;
}

[[nodiscard]] egcf::InternetAlgorithmCandidate candidate_from_store(
    egcf::EgcfStore &store, std::string_view candidate_id) {
  const auto record = store.get(candidate_id);
  if (record.object_type != "internet-algorithm-candidate") {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "candidate_id does not reference an internet candidate");
  }
  return egcf::internet_algorithm_candidate_from_json(record.payload);
}

[[nodiscard]] saa::ProbationRegressionSignals regression_signals(
    const Json &request) {
  const auto signals = request.value("regression_signals", Json::object());
  return {.semantic_contradiction =
              signals.value("semantic_contradiction", false),
          .falsifier_succeeded = signals.value("falsifier_succeeded", false),
          .corrected_error_recurrence =
              signals.value("corrected_error_recurrence", false),
          .equivalent_failure_retry_regression =
              signals.value("equivalent_failure_retry_regression", false),
          .independence_passed = signals.value("independence_passed", true),
          .evidence_fresh = signals.value("evidence_fresh", true),
          .projection_integrity_passed =
              signals.value("projection_integrity_passed", true)};
}

[[nodiscard]] egcf::InternetProbationObservationRequest
probation_observation_request(const Json &request) {
  return {.query_signature =
              request.at("query_signature").get<std::string>(),
          .context_signature =
              request.at("context_signature").get<std::string>(),
          .observed_at = request.at("observed_at").get<std::string>(),
          .window_index = request.at("window_index").get<int>(),
          .candidate_correct = request.at("candidate_correct").get<bool>(),
          .baseline_correct = request.at("baseline_correct").get<bool>(),
          .invariant_passed = request.at("invariant_passed").get<bool>(),
          .benchmark_passed = request.at("benchmark_passed").get<bool>(),
          .integrity_passed = request.at("integrity_passed").get<bool>(),
          .source_valid = request.at("source_valid").get<bool>(),
          .reproduction_passed =
              request.at("reproduction_passed").get<bool>(),
          .evidence_ids = strings(request, "evidence_ids"),
          .regression_signals = regression_signals(request)};
}

[[nodiscard]] egcf::InternetImprovementRunRequest
internet_improvement_run_request(const Json &request) {
  egcf::InternetImprovementRunRequest result;
  result.current_timestamp = request.value(
      "current_timestamp",
      request.value("recorded_at",
                    request.value("observed_at",
                                  std::string("1970-01-01T00:00:00Z"))));
  result.cycle_key = request.value("cycle_key", result.current_timestamp);
  result.worker_id = request.value("worker_id", std::string{});
  result.action_lease_expires_at = request.value(
      "action_lease_expires_at",
      request.value("lease_expires_at", std::string{}));
  if (result.action_lease_expires_at.empty()) {
    result.action_lease_expires_at =
        timestamp_after(result.current_timestamp, 60);
  }
  result.fetch_lease_expires_at = request.value(
      "fetch_lease_expires_at", result.action_lease_expires_at);
  result.prior_snapshot_id =
      request.value("prior_snapshot_id", std::string{});
  result.source_label =
      request.value("source_label", result.source_label);
  result.strict_feed = request.value("strict", result.strict_feed);
  result.policy = egcf::internet_director_policy_from_json(
      request.value("policy", Json::object()));
  if (request.contains("promotion_policy_id")) {
    result.policy.promotion_policy_id =
        request.at("promotion_policy_id").get<std::string>();
  }
  if (request.contains("query_signature")) {
    result.policy.probation_query_signature =
        request.at("query_signature").get<std::string>();
  }
  if (request.contains("candidate_id")) {
    result.policy.candidate_scope_id =
        request.at("candidate_id").get<std::string>();
  }
  if (result.policy.action_deadline.empty()) {
    result.policy.action_deadline = request.value(
        "action_deadline", result.current_timestamp);
  }
  result.policy =
      egcf::canonical_internet_director_policy(std::move(result.policy));
  return result;
}

[[nodiscard]] egcf::InternetExperimentProtocol
internet_experiment_protocol(const Json &request) {
  const auto &value = request.contains("protocol") ? request.at("protocol")
                                                    : request;
  egcf::InternetExperimentProtocol protocol;
  protocol.protocol_version =
      value.value("protocol_version", std::string("v1"));
  protocol.applicable_candidate_statuses = value.value(
      "applicable_candidate_statuses",
      std::vector<std::string>{"VALIDATION_READY"});
  protocol.applicable_primitives =
      value.value("applicable_primitives", std::vector<std::string>{});
  protocol.applicable_domains =
      value.value("applicable_domains", std::vector<std::string>{});
  protocol.baseline_ref = value.at("baseline_ref").get<std::string>();
  protocol.baseline_saa_ir = value.at("baseline_saa_ir");
  protocol.dataset_snapshot_ids =
      value.at("dataset_snapshot_ids").get<std::vector<std::string>>();
  protocol.trial_groups = value.at("trial_groups");
  protocol.minimum_material_effect = value.value(
      "minimum_material_effect", protocol.minimum_material_effect);
  protocol.minimum_output =
      value.value("minimum_output", protocol.minimum_output);
  protocol.maximum_output =
      value.value("maximum_output", protocol.maximum_output);
  protocol.minimum_trials_per_group = value.value(
      "minimum_trials_per_group", protocol.minimum_trials_per_group);
  protocol.minimum_experiments =
      value.value("minimum_experiments", protocol.minimum_experiments);
  protocol.minimum_independence_groups = value.value(
      "minimum_independence_groups", protocol.minimum_independence_groups);
  protocol.maximum_total_trials =
      value.value("maximum_total_trials", protocol.maximum_total_trials);
  protocol.benchmark_track_scores =
      value.value("benchmark_track_scores", Json::object());
  protocol.benchmark_policy =
      value.value("benchmark_policy", Json::object());
  protocol.integrity_snapshots =
      value.value("integrity_snapshots", Json::array());
  protocol.integrity_policy =
      value.value("integrity_policy", Json::object());
  protocol.independent_review = value.value("independent_review", true);
  protocol.valid_from = value.at("valid_from").get<std::string>();
  protocol.valid_until = value.value("valid_until", std::string{});
  protocol.supersedes_protocol_id =
      value.value("supersedes_protocol_id", std::string{});
  protocol.source_provenance =
      value.value("source_provenance", Json::object());
  return egcf::canonical_internet_experiment_protocol(std::move(protocol));
}

[[nodiscard]] egcf::InternetSourceAssessmentInput
internet_source_assessment_input(const Json &request) {
  const auto &value = request.contains("input") ? request.at("input") : request;
  egcf::InternetSourceAssessmentInput input;
  input.snapshot_id = value.at("snapshot_id").get<std::string>();
  input.fetch_receipt_id =
      value.at("fetch_receipt_id").get<std::string>();
  input.source_policy_id =
      value.at("source_policy_id").get<std::string>();
  input.robots_allowed = value.at("robots_allowed").get<bool>();
  input.license_classification =
      value.at("license_classification").get<std::string>();
  input.evidence_ids =
      value.at("evidence_ids").get<std::vector<std::string>>();
  input.producer_identity =
      value.at("producer_identity").get<std::string>();
  input.provenance = value.value("provenance", Json::object());
  return egcf::canonical_internet_source_assessment_input(std::move(input));
}

[[nodiscard]] egcf::InternetProbationObservationInput
internet_probation_observation_input(const Json &request) {
  const auto &value = request.contains("input") ? request.at("input") : request;
  egcf::InternetProbationObservationInput input;
  input.candidate_id = value.at("candidate_id").get<std::string>();
  input.admission_id = value.at("admission_id").get<std::string>();
  input.query_signature = value.at("query_signature").get<std::string>();
  input.context_signature =
      value.at("context_signature").get<std::string>();
  input.observed_at = value.at("observed_at").get<std::string>();
  input.window_index = value.at("window_index").get<int>();
  input.candidate_correct = value.at("candidate_correct").get<bool>();
  input.baseline_correct = value.at("baseline_correct").get<bool>();
  input.invariant_passed = value.at("invariant_passed").get<bool>();
  input.benchmark_passed = value.at("benchmark_passed").get<bool>();
  input.integrity_passed = value.at("integrity_passed").get<bool>();
  input.source_valid = value.at("source_valid").get<bool>();
  input.reproduction_passed =
      value.at("reproduction_passed").get<bool>();
  input.evidence_ids =
      value.at("evidence_ids").get<std::vector<std::string>>();
  input.regression_signals =
      value.value("regression_signals", Json::object());
  input.producer_identity =
      value.at("producer_identity").get<std::string>();
  input.provenance = value.value("provenance", Json::object());
  return egcf::canonical_internet_probation_observation_input(
      std::move(input));
}

[[nodiscard]] Json execute_internet_watch(const Json &request) {
  egcf::EgcfStore store(request_root(request), resource_root(request));
  egcf::InternetImprovementStore internet(store);
  const auto action = request.value("action", std::string("list"));
  if (action == "list") {
    return {{"active_watch_ids", internet.active_watch_ids()},
            {"watches", stored_objects(internet.list("internet-watch"))}};
  }
  if (action == "get") {
    return record_json(store.get(request.at("watch_id").get<std::string>()));
  }
  sources::InternetWatch watch;
  if (action == "enable" || action == "disable" || action == "supersede") {
    const std::string old_id = request.at("watch_id").get<std::string>();
    watch = sources::internet_watch_from_json(store.get(old_id).payload);
    watch.supersedes_watch_id = old_id;
    watch.schedule_generation += 1;
    if (action == "enable") {
      watch.enabled = true;
    } else if (action == "disable") {
      watch.enabled = false;
    }
  } else if (action != "register") {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "unsupported internet-watch action");
  }
  if (request.contains("source_policy")) {
    watch.source_policy_id = internet.register_source_policy(
        sources::source_policy_from_json(request.at("source_policy")));
  } else if (request.contains("source_policy_id")) {
    watch.source_policy_id =
        request.at("source_policy_id").get<std::string>();
  }
  if (request.contains("canonical_url")) {
    watch.canonical_url = request.at("canonical_url").get<std::string>();
  }
  if (request.contains("source_group")) {
    watch.source_group = request.at("source_group").get<std::string>();
  }
  if (request.contains("accepted_mime_types")) {
    watch.accepted_mime_types = strings(request, "accepted_mime_types");
  } else if (watch.accepted_mime_types.empty() &&
             !watch.source_policy_id.empty()) {
    watch.accepted_mime_types = sources::source_policy_from_json(
                                    store.get(watch.source_policy_id).payload)
                                    .accepted_mime_types;
  }
  watch.enabled = request.value("enabled", watch.enabled);
  watch.polling_interval_seconds = request.value(
      "polling_interval_seconds", watch.polling_interval_seconds);
  watch.deterministic_jitter_seconds = request.value(
      "deterministic_jitter_seconds", watch.deterministic_jitter_seconds);
  watch.maximum_redirects =
      request.value("maximum_redirects", watch.maximum_redirects);
  watch.maximum_response_bytes = request.value(
      "maximum_response_bytes", watch.maximum_response_bytes);
  watch.maximum_decompressed_bytes = request.value(
      "maximum_decompressed_bytes", watch.maximum_decompressed_bytes);
  watch.request_timeout_seconds = request.value(
      "request_timeout_seconds", watch.request_timeout_seconds);
  watch.schedule_generation =
      request.value("schedule_generation", watch.schedule_generation);
  watch.watch_signature.clear();
  watch = sources::canonical_watch(std::move(watch));
  const std::string watch_id = internet.register_watch(watch);
  return {{"watch", sources::to_json(watch)}, {"watch_id", watch_id}};
}

[[nodiscard]] Json json_file(const std::filesystem::path &path) {
  return contracts::parse_json(core::read_text(path));
}

[[nodiscard]] Json watchlist_manifest(const Json &request) {
  if (request.contains("manifest")) {
    if (!request.at("manifest").is_object()) {
      throw common::Error(common::ErrorCode::invalid_argument,
                          "watchlist manifest must be an object");
    }
    return request.at("manifest");
  }
  if (!request.contains("manifest_path")) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "watchlist action requires manifest or manifest_path");
  }
  return json_file(request.at("manifest_path").get<std::string>());
}

[[nodiscard]] Json watchlist_source_registry(
    const Json &request, const std::filesystem::path &resources) {
  if (request.contains("source_registry")) {
    return request.at("source_registry");
  }
  const auto path = request.contains("source_registry_path")
                        ? std::filesystem::path(
                              request.at("source_registry_path")
                                  .get<std::string>())
                        : resources /
                              "watchlists/internet/source-groups-v1.json";
  return json_file(path);
}

void validate_watchlist_schema(const Json &manifest,
                               const std::filesystem::path &resources) {
  egcf::RecordSchemaRegistry schemas(resources);
  auto schema = json_file(
      resources / "schemas/watchlists/saa-internet-watchlist-v1.schema.json");
  schema["properties"]["watches"]["items"] = schema["$defs"]["watch"];
  schema.erase("$defs");
  schemas.validate_json_value(schema, manifest, "$watchlist");
}

[[nodiscard]] std::filesystem::path watchlist_resource_path(
    const std::filesystem::path &resources, std::string reference) {
  std::filesystem::path relative(std::move(reference));
  if (!relative.empty() && *relative.begin() == "resources") {
    relative = relative.lexically_relative("resources");
  }
  const auto result = std::filesystem::weakly_canonical(resources / relative);
  if (!contains_path(resources, result)) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "watchlist resource reference escapes resource root");
  }
  return result;
}

void write_watchlist_output(const Json &request, const Json &value) {
  if (request.contains("output_path")) {
    core::atomic_write_text(
        request.at("output_path").get<std::string>(),
        contracts::canonical_json(value) + "\n");
  }
}

void validate_preflight_report(const Json &report, const Json &manifest,
                               const Json &request) {
  if (!report.is_object() || report.value("schema_version", 0) != 1 ||
      report.value("manifest_sha256", std::string{}) !=
          contracts::sha256_json(manifest) ||
      !report.contains("results") || !report.at("results").is_array()) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "preflight report does not match the watchlist");
  }
  Json signed_material = report;
  const std::string signature =
      signed_material.value("report_signature", std::string{});
  signed_material.erase("report_signature");
  signed_material.erase("report_id");
  if (signature.empty() || contracts::sha256_json(signed_material) != signature) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "preflight report signature is invalid");
  }
  const std::time_t checked =
      timestamp_value(report.at("checked_at").get<std::string>());
  const std::time_t current = timestamp_value(
      request.at("current_timestamp").get<std::string>());
  const auto age = static_cast<long long>(current - checked);
  if (age < 0 || age > request.value("maximum_preflight_age_seconds", 86'400)) {
    throw common::Error(common::ErrorCode::policy_denied,
                        "preflight report is stale or from the future");
  }
}

[[nodiscard]] const Json *preflight_result(const Json &report,
                                           const Json &entry) {
  if (!report.is_object()) {
    return nullptr;
  }
  const std::string name = entry.at("name").get<std::string>();
  const std::string hash = contracts::sha256_json(entry);
  const Json *result = nullptr;
  for (const auto &candidate : report.at("results")) {
    if (candidate.value("entry_name", std::string{}) != name) {
      continue;
    }
    if (result != nullptr) {
      throw common::Error(common::ErrorCode::invalid_argument,
                          "preflight report contains duplicate entry results");
    }
    if (candidate.value("entry_sha256", std::string{}) != hash) {
      throw common::Error(common::ErrorCode::invalid_argument,
                          "preflight entry hash does not match manifest");
    }
    if (candidate.value("eligible", false)) {
      const bool robots_required =
          entry.at("robots").at("required").get<bool>();
      const std::string content_type =
          candidate.value("content_type", std::string{});
      const auto accepted_mime_types =
          entry.at("accepted_mime_types").get<std::vector<std::string>>();
      const int http_status = candidate.value("http_status", 0);
      if (candidate.value("status", std::string{}) !=
              "PREFLIGHT_ELIGIBLE" ||
          !candidate.contains("blocking_reasons") ||
          !candidate.at("blocking_reasons").is_array() ||
          !candidate.at("blocking_reasons").empty() ||
          candidate.value("canonical_url", std::string{}) !=
              entry.at("canonical_url").get<std::string>() ||
          candidate.value("final_url", std::string{}) !=
              entry.at("canonical_url").get<std::string>() ||
          http_status < 200 || http_status >= 300 || http_status == 204 ||
          !candidate.value("tls_verified", false) ||
          candidate.value("provider_identity", std::string{}).empty() ||
          !candidate.contains("resolved_addresses") ||
          !candidate.at("resolved_addresses").is_array() ||
          candidate.at("resolved_addresses").empty() ||
          std::find(accepted_mime_types.begin(), accepted_mime_types.end(),
                    content_type) == accepted_mime_types.end() ||
          (robots_required &&
           (!candidate.value("robots_policy_evaluated", false) ||
            !candidate.value("robots_allowed", false)))) {
        throw common::Error(common::ErrorCode::policy_denied,
                            "eligible preflight result lacks required evidence");
      }
    }
    result = &candidate;
  }
  return result;
}

[[nodiscard]] Json execute_internet_watchlist(const Json &request) {
  const auto resources = resource_root(request);
  const auto registry = watchlist_source_registry(request, resources);
  const std::string action = request.value("action", std::string("validate"));
  if (action == "create") {
    const auto manifest = sources::create_watchlist_manifest(request, registry);
    validate_watchlist_schema(manifest, resources);
    write_watchlist_output(request, manifest);
    return {{"manifest", manifest},
            {"manifest_sha256", contracts::sha256_json(manifest)},
            {"watch_count", manifest.at("watches").size()}};
  }

  const auto manifest = watchlist_manifest(request);
  validate_watchlist_schema(manifest, resources);
  sources::validate_watchlist_manifest(manifest, registry);
  if (action == "validate") {
    return {{"manifest_sha256", contracts::sha256_json(manifest)},
            {"valid", true},
            {"watch_count", manifest.at("watches").size()},
            {"watchlist_version", manifest.at("watchlist_version")}};
  }

  const auto policy_path = watchlist_resource_path(
      resources, manifest.at("source_policy_ref").get<std::string>());
  const auto base_policy = sources::source_policy_from_json(json_file(policy_path));
  if (action == "preflight") {
    sources::CurlHttpFetchProvider provider;
    const auto report = sources::preflight_watchlist_manifest(
        manifest, registry, base_policy, provider,
        request.at("checked_at").get<std::string>());
    write_watchlist_output(request, report);
    return report;
  }
  if (action != "register" && action != "resume") {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "unsupported internet-watchlist action");
  }

  Json report;
  if (request.contains("preflight_report")) {
    report = request.at("preflight_report");
  } else if (request.contains("preflight_report_path")) {
    report = json_file(
        request.at("preflight_report_path").get<std::string>());
  }
  if (!report.is_null()) {
    validate_preflight_report(report, manifest, request);
  }

  const bool dry_run = request.value("dry_run", false);
  std::unique_ptr<egcf::EgcfStore> store;
  std::unique_ptr<egcf::InternetImprovementStore> internet;
  if (!dry_run) {
    store = std::make_unique<egcf::EgcfStore>(request_root(request), resources);
    internet = std::make_unique<egcf::InternetImprovementStore>(*store);
  }
  const bool eligible_only = request.value("eligible_only", true);
  const bool enable_eligible = request.value("enable_eligible", false);
  const auto selected_groups = strings(request, "source_groups");
  const std::string report_hash =
      report.is_null() ? std::string{} : contracts::sha256_json(report);
  Json registrations = Json::array();
  Json skipped = Json::array();
  for (const auto &entry : manifest.at("watches")) {
    const std::string entry_group =
        entry.at("source_group").get<std::string>();
    if (!selected_groups.empty() &&
        std::find(selected_groups.begin(), selected_groups.end(), entry_group) ==
            selected_groups.end()) {
      skipped.push_back({{"entry_name", entry.at("name")},
                         {"reason", "source-group-filtered"}});
      continue;
    }
    const Json *result = preflight_result(report, entry);
    const bool eligible = result != nullptr && result->value("eligible", false);
    if (!eligible && eligible_only) {
      skipped.push_back(
          {{"entry_name", entry.at("name")},
           {"reason", result == nullptr ? "missing-preflight-result"
                                        : result->value(
                                              "status",
                                              std::string("QUARANTINED"))}});
      continue;
    }

    const auto policy = sources::watchlist_source_policy(entry, base_policy);
    const std::string policy_id =
        dry_run ? policy.object_id() : internet->register_source_policy(policy);
    const auto watch = sources::watchlist_watch(
        entry, policy_id, eligible, enable_eligible);
    const std::string watch_id =
        dry_run ? watch.object_id() : internet->register_watch(watch);
    const std::string status =
        !eligible ? "QUARANTINED"
                  : watch.enabled ? "REGISTERED_ENABLED"
                                  : "REGISTERED_DISABLED";
    const auto registration = sources::make_watchlist_registration(
        manifest, entry, watch_id, policy_id, report_hash, status);
    const egcf::EgcfRecord registration_record = {
        .object_type = "internet-watch-registration",
        .payload = registration};
    const std::string registration_id =
        dry_run
            ? registration_record.object_id()
            : store->register_record(
                  registration_record,
                  "internet_watch_registration_registered");
    registrations.push_back(
        {{"eligibility_status", status},
         {"entry_name", entry.at("name")},
         {"registration_id", registration_id},
         {"source_policy_id", policy_id},
         {"watch_id", watch_id}});
  }
  return {{"dry_run", dry_run},
          {"manifest_sha256", contracts::sha256_json(manifest)},
          {"registrations", std::move(registrations)},
          {"skipped", std::move(skipped)}};
}

[[nodiscard]] Json execute_internet_poll(const Json &request) {
  egcf::EgcfStore store(request_root(request), resource_root(request));
  egcf::InternetImprovementStore internet(store);
  const auto action = request.value("action", std::string("list"));
  if (action == "list") {
    return {{"jobs", stored_objects(internet.list("internet-fetch-job"))},
            {"leases", stored_objects(internet.list("internet-fetch-lease"))}};
  }
  if (action == "lease") {
    const std::string job_id = request.at("job_id").get<std::string>();
    const auto lease = sources::acquire_fetch_lease(
        job_id, request.at("worker_id").get<std::string>(),
        request.at("acquired_at").get<std::string>(),
        request.at("expires_at").get<std::string>(),
        request.value("predecessor_lease_id", std::string{}));
    return {{"lease", sources::to_json(lease)},
            {"lease_id", internet.register_fetch_lease(lease)}};
  }
  std::vector<sources::InternetFetchJob> jobs;
  for (const auto &item : internet.list("internet-fetch-job")) {
    jobs.push_back(sources::internet_fetch_job_from_json(item.payload));
  }
  std::vector<sources::InternetFetchLease> leases;
  for (const auto &item : internet.list("internet-fetch-lease")) {
    leases.push_back(sources::internet_fetch_lease_from_json(item.payload));
  }
  if (action == "schedule") {
    std::vector<sources::InternetWatch> watches;
    for (const auto &watch_id : internet.active_watch_ids()) {
      watches.push_back(
          sources::internet_watch_from_json(store.get(watch_id).payload));
    }
    const auto created = sources::schedule_fetch_interval(
        watches, jobs, request.at("scheduled_interval").get<std::string>(),
        request.at("earliest_start").get<std::string>(),
        request.at("deadline").get<std::string>(),
        request.value("retry_ceiling", 3));
    Json ids = Json::array();
    Json records = Json::array();
    for (const auto &job : created) {
      ids.push_back(internet.register_fetch_job(job));
      records.push_back(sources::to_json(job));
    }
    return {{"jobs", records}, {"job_ids", ids}};
  }
  if (action == "select") {
    sources::InternetSchedulerLimits limits;
    limits.global_concurrency =
        request.value("global_concurrency", limits.global_concurrency);
    limits.per_source_group_concurrency = request.value(
        "per_source_group_concurrency", limits.per_source_group_concurrency);
    limits.global_response_byte_budget = request.value(
        "global_response_byte_budget", limits.global_response_byte_budget);
    limits.global_cpu_unit_budget = request.value(
        "global_cpu_unit_budget", limits.global_cpu_unit_budget);
    limits.maximum_clock_jump_seconds = request.value(
        "maximum_clock_jump_seconds", limits.maximum_clock_jump_seconds);
    return sources::to_json(sources::select_due_fetch_jobs(
        std::move(jobs), leases,
        request.at("current_timestamp").get<std::string>(), limits));
  }
  throw common::Error(common::ErrorCode::invalid_argument,
                      "unsupported internet-poll action");
}

[[nodiscard]] Json execute_internet_fetch(const Json &request) {
  egcf::EgcfStore store(request_root(request), resource_root(request));
  egcf::InternetImprovementStore internet(store);
  const auto action = request.value("action", std::string("list"));
  if (action == "list") {
    return {{"receipts",
             stored_objects(internet.list("internet-fetch-receipt"))}};
  }
  if (action != "execute") {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "unsupported internet-fetch action");
  }
  sources::CurlHttpFetchProvider provider;
  egcf::InternetSourceCoordinator coordinator(store);
  return egcf::to_json(coordinator.execute_fetch(
      request.at("job_id").get<std::string>(),
      request.at("lease_id").get<std::string>(),
      request.at("current_timestamp").get<std::string>(), provider,
      request.value("snapshot_id", std::string{})));
}

[[nodiscard]] Json execute_internet_source(const Json &request) {
  egcf::EgcfStore store(request_root(request), resource_root(request));
  egcf::InternetImprovementStore internet(store);
  const auto action = request.value("action", std::string("list"));
  if (action == "get") {
    return record_json(store.get(request.at("object_id").get<std::string>()));
  }
  if (action == "assess") {
    egcf::InternetSourceCoordinator coordinator(store);
    return egcf::to_json(coordinator.assess(
        request.at("snapshot_id").get<std::string>(),
        request.at("fetch_receipt_id").get<std::string>(),
        request.at("source_policy_id").get<std::string>(),
        request.at("robots_allowed").get<bool>(),
        request.value("license_classification", std::string("UNKNOWN"))));
  }
  static const std::map<std::string, std::string> kinds = {
      {"assessments", "internet-policy-assessment"},
      {"extractions", "internet-extraction-receipt"},
      {"fragments", "internet-source-fragment"},
      {"policies", "internet-source-policy"},
      {"receipts", "internet-fetch-receipt"},
      {"snapshots", "internet-source-snapshot"}};
  const auto kind = request.value("kind", std::string("snapshots"));
  const auto found = kinds.find(kind);
  if (action != "list" || found == kinds.end()) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "unsupported internet-source action or kind");
  }
  return {{kind, stored_objects(internet.list(found->second))}};
}

[[nodiscard]] Json execute_internet_extract(const Json &request) {
  egcf::EgcfStore store(request_root(request), resource_root(request));
  egcf::InternetImprovementStore internet(store);
  const auto action = request.value("action", std::string("execute"));
  if (action == "list") {
    return {{"extractions",
             stored_objects(internet.list("internet-extraction-receipt"))}};
  }
  if (action != "execute") {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "unsupported internet-extract action");
  }
  sources::InternetExtractionLimits limits;
  limits.maximum_input_bytes =
      request.value("maximum_input_bytes", limits.maximum_input_bytes);
  limits.maximum_fragments =
      request.value("maximum_fragments", limits.maximum_fragments);
  limits.maximum_fragment_bytes =
      request.value("maximum_fragment_bytes", limits.maximum_fragment_bytes);
  limits.maximum_nesting_depth =
      request.value("maximum_nesting_depth", limits.maximum_nesting_depth);
  egcf::InternetSourceCoordinator coordinator(store);
  return egcf::to_json(coordinator.extract(
      request.at("snapshot_id").get<std::string>(), limits));
}

[[nodiscard]] Json execute_internet_candidate(const Json &request) {
  egcf::EgcfStore store(request_root(request), resource_root(request));
  egcf::InternetImprovementStore internet(store);
  const auto action = request.value("action", std::string("list"));
  if (action == "get") {
    return record_json(store.get(request.at("candidate_id").get<std::string>()));
  }
  if (action == "explain") {
    const auto candidate = candidate_from_store(
        store, request.at("candidate_id").get<std::string>());
    const auto retrieval = store.get(candidate.retrieval_receipt_id);
    if (retrieval.object_type != "internet-retrieval-receipt") {
      throw common::Error(common::ErrorCode::json_contract,
                          "candidate retrieval receipt has wrong type");
    }
    return {{"candidate", egcf::to_json(candidate)},
            {"retrieval_explanation", record_json(retrieval)}};
  }
  if (action == "migrate") {
    const std::string migrated_id = internet.migrate_algorithm_candidate(
        request.at("candidate_id").get<std::string>());
    return {{"candidate_id", migrated_id},
            {"candidate", record_json(store.get(migrated_id))}};
  }
  if (action != "list") {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "unsupported internet-candidate action");
  }
  return {{"candidates",
           stored_objects(internet.list("internet-algorithm-candidate"))}};
}

[[nodiscard]] Json execute_internet_promotion_policy(const Json &request) {
  egcf::EgcfStore store(request_root(request), resource_root(request));
  egcf::InternetImprovementStore internet(store);
  const auto action = request.value("action", std::string("list"));
  if (action == "list") {
    return {{"assessments",
             stored_objects(internet.list("internet-promotion-assessment"))},
            {"policies",
             stored_objects(internet.list("internet-promotion-policy"))}};
  }
  if (action == "get") {
    return record_json(store.get(request.at("object_id").get<std::string>()));
  }
  if (action == "register" || action == "register-default") {
    Json policy_json;
    if (action == "register-default") {
      policy_json = contracts::parse_json(core::read_text(
          resource_root(request) /
          "policies/internet/default-promotion-policy-v1.json"));
    } else {
      policy_json = request.at("policy");
    }
    const auto policy =
        saa::autonomous_promotion_policy_from_json(policy_json);
    return {{"policy", saa::to_json(policy)},
            {"policy_id", internet.register_promotion_policy(policy)}};
  }
  if (action == "assess") {
    egcf::AutonomousPromotionController controller(store);
    return egcf::to_json(controller.assess(
        candidate_from_store(
            store, request.at("candidate_id").get<std::string>()),
        request.at("policy_id").get<std::string>()));
  }
  throw common::Error(common::ErrorCode::invalid_argument,
                      "unsupported internet-promotion-policy action");
}

[[nodiscard]] Json execute_internet_probation(const Json &request) {
  egcf::EgcfStore store(request_root(request), resource_root(request));
  egcf::InternetImprovementStore internet(store);
  egcf::InternetProbationController probation(store);
  const auto action = request.value("action", std::string("list"));
  if (action == "list") {
    return {{"admissions",
             stored_objects(internet.list("internet-probation-admission"))},
            {"demotions",
             stored_objects(internet.list("internet-demotion-decision"))},
            {"observations",
             stored_objects(internet.list("internet-probation-observation"))},
            {"promotions",
             stored_objects(internet.list("internet-promotion-decision"))}};
  }
  const auto candidate = candidate_from_store(
      store, request.at("candidate_id").get<std::string>());
  if (action == "admit") {
    return egcf::to_json(probation.admit(
        candidate,
        request.value("previous_preferred_canonical_ref", std::string{})));
  }
  if (action == "select") {
    return egcf::to_json(probation.select(
        candidate, request.at("query_signature").get<std::string>()));
  }
  if (action == "observe") {
    return egcf::to_json(
        probation.observe(candidate, probation_observation_request(request)));
  }
  throw common::Error(common::ErrorCode::invalid_argument,
                      "unsupported internet-probation action");
}

[[nodiscard]] Json execute_internet_integrity(const Json &request) {
  egcf::EgcfStore store(request_root(request), resource_root(request));
  egcf::InternetImprovementStore internet(store);
  egcf::CanonicalAlgorithmStore canonical(store);
  egcf::KnowledgeGovernanceStore governance(store);
  const auto action = request.value("action", std::string("verify"));
  if (action == "rebuild") {
    internet.rebuild_projection();
    canonical.rebuild_projection();
    governance.rebuild_projection();
  } else if (action != "verify") {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "unsupported internet-integrity action");
  }
  internet.verify_integrity();
  return {{"action", action},
          {"candidate_count",
           internet.list("internet-algorithm-candidate").size()},
          {"event_head", store.event_head()},
          {"ok", true},
          {"probation_observation_count",
           internet.list("internet-probation-observation").size()},
          {"snapshot_count",
           internet.list("internet-source-snapshot").size()}};
}

[[nodiscard]] Json execute_internet_improvement(const Json &request) {
  const std::string action =
      request.value("action", std::string("status"));
  if (action == "feed") {
    egcf::EgcfStore store(request_root(request), resource_root(request));
    const auto assessment_record = store.get(
        request.at("policy_assessment_id").get<std::string>());
    if (assessment_record.object_type != "internet-policy-assessment") {
      throw common::Error(common::ErrorCode::invalid_argument,
                          "policy_assessment_id has wrong type");
    }
    egcf::InternetFeedCoordinator coordinator(store);
    return egcf::to_json(coordinator.process(
        sources::internet_policy_assessment_from_json(
            assessment_record.payload),
        extraction_from_store(
            store, request.at("extraction_receipt_id").get<std::string>()),
        request.value("source_label", std::string("internet-source")),
        request.value("strict", true)));
  }
  if (action == "reason") {
    egcf::EgcfStore store(request_root(request), resource_root(request));
    const auto candidate = candidate_from_store(
        store, request.at("candidate_id").get<std::string>());
    std::vector<std::string> fragment_ids;
    if (request.contains("fragment_ids")) {
      fragment_ids = strings(request, "fragment_ids");
    } else {
      fragment_ids = {candidate.source_fragment_id};
    }
    std::vector<sources::InternetSourceFragment> fragments;
    for (const auto &fragment_id : fragment_ids) {
      const auto fragment = store.get(fragment_id);
      if (fragment.object_type != "internet-source-fragment") {
        throw common::Error(common::ErrorCode::invalid_argument,
                            "fragment_id has wrong type");
      }
      fragments.push_back(
          sources::internet_source_fragment_from_json(fragment.payload));
    }
    egcf::InternetReasoningCoordinator coordinator(store);
    return egcf::to_json(coordinator.analyze(candidate, fragments));
  }
  if (action == "experiment-qualify") {
    egcf::EgcfStore store(request_root(request), resource_root(request));
    egcf::InternetExperimentCoordinator coordinator(store);
    return egcf::to_json(coordinator.qualify(
        candidate_from_store(
            store, request.at("candidate_id").get<std::string>()),
        experiment_request(request)));
  }
  if (action == "policy-assess") {
    Json forwarded = request;
    forwarded["action"] = "assess";
    return execute_internet_promotion_policy(forwarded);
  }
  if (action == "probation-admit" || action == "probation-select" ||
      action == "probation-observe") {
    Json forwarded = request;
    forwarded["action"] = action.substr(std::string("probation-").size());
    return execute_internet_probation(forwarded);
  }
  egcf::EgcfStore store(request_root(request), resource_root(request));
  egcf::InternetImprovementStore internet(store);
  if (action == "protocol-register") {
    const auto protocol = internet_experiment_protocol(request);
    return {{"protocol", egcf::to_json(protocol)},
            {"protocol_id", internet.register_experiment_protocol(protocol)}};
  }
  if (action == "source-assessment-input-register") {
    const auto input = internet_source_assessment_input(request);
    return {{"input", egcf::to_json(input)},
            {"input_id", internet.register_source_assessment_input(input)}};
  }
  if (action == "probation-observation-input-register") {
    const auto input = internet_probation_observation_input(request);
    return {{"input", egcf::to_json(input)},
            {"input_id",
             internet.register_probation_observation_input(input)}};
  }
  if (action == "plan") {
    egcf::InternetImprovementOrchestrator orchestrator(store);
    const auto plan = orchestrator.plan(internet_improvement_run_request(request));
    Json result = {{"plan", egcf::to_json(plan)}};
    if (request.value("register", false)) {
      result["plan_id"] = internet.register_improvement_plan(plan);
    }
    return result;
  }
  if (action == "run-once" || action == "advance" || action == "resume") {
    if (action == "advance" && request.contains("baseline_ref") &&
        request.contains("trial_groups") &&
        request.contains("valid_from")) {
      static_cast<void>(internet.register_experiment_protocol(
          internet_experiment_protocol(request)));
    }
    sources::CurlHttpFetchProvider fetch_provider;
    egcf::InternetImprovementOrchestrator orchestrator(
        store, &fetch_provider);
    auto run_request = internet_improvement_run_request(request);
    if (action == "advance") {
      run_request.policy.maximum_actions = 1;
      run_request.policy.enable_acquisition = false;
      run_request.policy.candidate_scope_id =
          request.at("candidate_id").get<std::string>();
      run_request.policy = egcf::canonical_internet_director_policy(
          std::move(run_request.policy));
    }
    return egcf::to_json(
        action == "resume"
            ? orchestrator.resume(
                  request.at("run_id").get<std::string>(), run_request)
            : orchestrator.run_once(run_request));
  }
  if (action == "run-status") {
    egcf::InternetImprovementOrchestrator orchestrator(store);
    return orchestrator.run_status(
        request.value("run_id", std::string{}),
        request.value("worker_id", std::string{}),
        request.value("nonterminal_only", false));
  }
  if (action == "explain-action") {
    egcf::InternetImprovementOrchestrator orchestrator(store);
    return orchestrator.explain_action(
        request.at("action_key").get<std::string>());
  }
  if (action != "status") {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "unsupported internet-improvement action");
  }
  return {{"action_leases",
           stored_objects(
               internet.list("internet-improvement-action-lease"))},
          {"action_receipts",
           stored_objects(
               internet.list("internet-improvement-action-receipt"))},
          {"candidates",
           stored_objects(internet.list("internet-algorithm-candidate"))},
          {"experiment_protocols",
           stored_objects(internet.list("internet-experiment-protocol"))},
          {"experiment_qualifications",
           stored_objects(
               internet.list("internet-experiment-qualification"))},
          {"plans",
           stored_objects(internet.list("internet-improvement-plan"))},
          {"promotion_assessments",
           stored_objects(internet.list("internet-promotion-assessment"))},
          {"probation_admissions",
           stored_objects(internet.list("internet-probation-admission"))},
          {"runs", stored_objects(internet.list("internet-improvement-run"))}};
}

[[nodiscard]] Json execute_operation(std::string_view operation,
                                     const Json &request) {
  if (!request.is_object()) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "CLI request must be a JSON object");
  }
  if (operation == "internet-watch") {
    return execute_internet_watch(request);
  }
  if (operation == "internet-watchlist") {
    return execute_internet_watchlist(request);
  }
  if (operation == "internet-poll") {
    return execute_internet_poll(request);
  }
  if (operation == "internet-fetch") {
    return execute_internet_fetch(request);
  }
  if (operation == "internet-source") {
    return execute_internet_source(request);
  }
  if (operation == "internet-extract") {
    return execute_internet_extract(request);
  }
  if (operation == "internet-candidate") {
    return execute_internet_candidate(request);
  }
  if (operation == "internet-improvement") {
    return execute_internet_improvement(request);
  }
  if (operation == "internet-promotion-policy") {
    return execute_internet_promotion_policy(request);
  }
  if (operation == "internet-probation") {
    return execute_internet_probation(request);
  }
  if (operation == "internet-integrity") {
    return execute_internet_integrity(request);
  }
  if (operation == "inspect") {
    const core::Workspace workspace(request_root(request));
    Json files = Json::array();
    for (const auto &path : workspace.files(
             request.value("path", std::string(".")))) {
      files.push_back(workspace.relative(path));
    }
    return {{"files", files},
            {"source_snapshot_hash", workspace.snapshot_hash()},
            {"workspace", workspace.root().generic_string()}};
  }
  if (operation == "reason") {
    EngineSession session(request);
    const auto input = request.value("input", request);
    return session.engine().reasoning().propose(
        input, session.workspace().snapshot_hash(),
        command_context(request).scope);
  }
  if (operation == "hypothesis") {
    return execute_reasoning_set(request.at("input"));
  }
  if (operation == "algorithm") {
    const auto action = request.value("action", std::string("list"));
    if (action == "canonicalize") {
      return saa::to_json(saa::canonicalize_mapping(
          request.at("structure"), request.value("max_permutations", 10'000U)));
    }
    EngineSession session(request);
    if (action == "describe") {
      return egcf::to_json(session.engine().algorithms().resolve_exact(
          request.at("algorithm_id").get<std::string>()));
    }
    if (action == "select") {
      return session.engine().invoke(
          "algorithm.select@1",
          {{"command_id", request.at("command_id")},
           {"context", request.value("selection_context", Json::object())},
           {"invariants",
            request.value("invariants", std::vector<std::string>{})}},
          command_context(request));
    }
    Json result = Json::array();
    for (const auto &algorithm : session.engine().algorithms().algorithms()) {
      result.push_back(egcf::to_json(algorithm));
    }
    return {{"algorithms", result}};
  }
  if (operation == "retrieve") {
    EngineSession session(request);
    return {{"objects",
             stored_objects(session.engine().store().search_text(
                 request.at("query").get<std::string>(),
                 request.contains("object_type")
                     ? std::optional<std::string>(
                           request.at("object_type").get<std::string>())
                     : std::nullopt,
                 request.value("limit", 20U)))}};
  }
  if (operation == "explain") {
    EngineSession session(request);
    if (request.contains("object_id")) {
      const auto record = session.engine().store().get(
          request.at("object_id").get<std::string>());
      return {{"object_id", record.object_id()},
              {"object_type", record.object_type},
              {"payload", record.payload}};
    }
    return session.engine().commands().describe(
        request.at("command_id").get<std::string>());
  }
  if (operation == "command") {
    EngineSession session(request);
    return session.engine().invoke(
        request.at("command_id").get<std::string>(),
        request.value("inputs", Json::object()), command_context(request));
  }
  if (operation == "compile") {
    EngineSession session(request);
    const auto compiled = session.engine().compile(
        egcf::workflow_definition_from_json(request.at("workflow")),
        command_context(request));
    Json result = {{"compiled", egcf::to_json(compiled)},
                   {"compiled_workflow_id", compiled.object_id()}};
    if (request.value("create_plan", true)) {
      const auto context = command_context(request);
      const auto plan = session.engine().create_execution_plan(
          compiled, request.value(
                        "prepare_mutations",
                        compiled.capability_level == "C3" && !context.dry_run &&
                            !context.simulate));
      result["execution_plan"] = egcf::to_json(plan);
      result["execution_plan_id"] = plan.object_id();
    }
    return result;
  }
  if (operation == "simulate") {
    EngineSession session(request);
    auto context = command_context(request);
    context.simulate = true;
    if (request.contains("workflow")) {
      const auto compiled = session.engine().compile(
          egcf::workflow_definition_from_json(request.at("workflow")), context);
      const auto plan = session.engine().create_execution_plan(compiled, false);
      return session.engine().execute_plan(plan.object_id());
    }
    return session.engine().invoke(
        request.value("command_id", std::string("simulate.worktree@1")),
        request.value("inputs", Json::object()), context);
  }
  if (operation == "approve") {
    EngineSession session(request);
    return {{"approval_id",
             session.engine().authorize(
                 request.at("plan_id").get<std::string>(),
                 request.at("approver").get<std::string>(),
                 request.at("authority_statement").get<std::string>(),
                 request.value("constraints", Json::object()),
                 request.value("expires_at", std::string{}),
                 request.value("use_limit", 1))}};
  }
  if (operation == "execute") {
    EngineSession session(request, request.value("recovery", false));
    return session.engine().execute_plan(
        request.at("plan_id").get<std::string>(),
        request.value("approval_id", std::string{}),
        request.value("pause_at_checkpoint", false),
        request.value("resume", false));
  }
  if (operation == "verify") {
    EngineSession session(request, true);
    return session.engine().verify_plan(
        request.at("plan_id").get<std::string>());
  }
  if (operation == "rollback") {
    EngineSession session(request, true);
    return session.engine().rollback_plan(
        request.at("plan_id").get<std::string>());
  }
  if (operation == "replay") {
    EngineSession session(request, true);
    return session.engine().replay(request.at("plan_id").get<std::string>(),
                                   command_context(request));
  }
  if (operation == "ledger-verify") {
    egcf::EgcfStore store(request_root(request), resource_root(request));
    store.validate_projection();
    return {{"event_count", store.events().size()},
            {"event_head", store.event_head()},
            {"ok", true},
            {"projection", egcf::to_json(store.projection_checkpoint())}};
  }
  if (operation == "projection-rebuild") {
    egcf::EgcfStore store(request_root(request), resource_root(request));
    store.rebuild_projection();
    return {{"ok", true},
            {"projection", egcf::to_json(store.projection_checkpoint())},
            {"rebuilt", true}};
  }
  if (operation == "benchmark") {
    const auto path = std::filesystem::path(
        request.at("run_path").get<std::string>());
    if (request.contains("checksum_path")) {
      static_cast<void>(reasoning::verify_benchmark_checksum(
          path, request.at("checksum_path").get<std::string>()));
    }
    const auto run = reasoning::load_benchmark_run(path);
    reasoning::require_benchmark_run_integrity(run);
    return reasoning::to_json(run);
  }
  if (operation == "qualification") {
    std::vector<reasoning::BenchmarkRun> runs;
    for (const auto &path : request.at("run_paths")) {
      runs.push_back(reasoning::load_benchmark_run(
          path.get<std::string>()));
    }
    return reasoning::to_json(reasoning::qualify_reasoning_runs(runs));
  }
  if (operation == "brain-feed") {
    egcf::EgcfStore store(request_root(request), resource_root(request));
    egcf::BrainFeedProcessor processor(store);
    std::vector<egcf::BrainFeedItem> items;
    for (const auto &item : request.at("items")) {
      items.push_back(egcf::make_brain_feed_item(
          item.at("id"), item.at("kind"), item.at("payload"),
          item.value("depends_on", std::vector<std::string>{}),
          item.value("evidence_from", std::vector<std::string>{}),
          item.value("source_path", std::string{})));
    }
    return egcf::to_json(processor.feed(
        request.at("batch_id"), request.at("source_signature"),
        request.value("source_label", std::string{}), std::move(items),
        request.value("strict", false)));
  }
  if (operation == "repository-feed") {
    egcf::EgcfStore store(request_root(request), resource_root(request));
    egcf::BrainFeedProcessor processor(store);
    const auto source = request.value("source", request_root(request).string());
    if (request.value("scan_only", false)) {
      return egcf::to_json(egcf::scan_repository(source));
    }
    return egcf::to_json(egcf::feed_repository(
        processor, source, {}, request.value("strict", false)));
  }
  throw common::Error(common::ErrorCode::invalid_argument,
                      "unknown StateWright operation: " +
                          std::string(operation));
}

[[nodiscard]] Json response(std::string_view operation, Json result) {
  return {{"build", contracts::build_identity()},
          {"ok", true},
          {"operation", operation},
          {"protocol", "statewright.cli.v1"},
          {"result", std::move(result)}};
}

[[nodiscard]] Json error_response(std::string_view operation,
                                  std::string code, std::string message) {
  return {{"build", contracts::build_identity()},
          {"error", {{"code", std::move(code)},
                     {"message", std::move(message)}}},
          {"ok", false},
          {"operation", operation},
          {"protocol", "statewright.cli.v1"}};
}

} // namespace

int run_cli(int argc, char **argv) {
  try {
    if (argc < 2) {
      usage(std::cerr);
      return 2;
    }
    const std::string_view command(argv[1]);
    if (command == "version") {
      const auto identity = contracts::build_identity();
      if (argc == 3 && std::string_view(argv[2]) == "--json") {
        std::cout << contracts::canonical_json(identity) << '\n';
      } else if (argc == 2) {
        std::cout << "StateWright "
                  << identity.at("statewright_version").get<std::string>()
                  << '\n';
      } else {
        usage(std::cerr);
        return 2;
      }
      return 0;
    }
    if (command == "canonicalize" && argc == 3) {
      std::cout << contracts::canonicalize_json_text(argv[2]) << '\n';
      return 0;
    }
    if (command == "hash" && argc == 3) {
      std::cout << contracts::sha256_text(argv[2]) << '\n';
      return 0;
    }
    if (command == "hash-json" && argc == 3) {
      std::cout << contracts::sha256_json(contracts::parse_json(argv[2]))
                << '\n';
      return 0;
    }
    if (command == "typed-id" && argc == 4) {
      std::cout << contracts::typed_id(argv[2], contracts::parse_json(argv[3]))
                << '\n';
      return 0;
    }
    if (command == "operational-hypothesis" && argc == 3) {
      std::cout << contracts::canonical_json(core::to_json(
                       core::make_operational_hypothesis(
                           operational_proposal(contracts::parse_json(argv[2])))))
                << '\n';
      return 0;
    }
    if (command == "reasoning-hypothesis-set" && argc == 3) {
      std::cout << contracts::canonical_json(
                       execute_reasoning_set(contracts::parse_json(argv[2])))
                << '\n';
      return 0;
    }
    if (command == "saa-canonicalize" && (argc == 3 || argc == 4)) {
      const auto maximum =
          argc == 4 ? static_cast<std::uint64_t>(std::stoull(argv[3]))
                    : 10'000U;
      std::cout << contracts::canonical_json(saa::to_json(
                       saa::canonicalize_mapping(contracts::parse_json(argv[2]),
                                                 maximum)))
                << '\n';
      return 0;
    }
    if (command == "saa-search" && argc == 4) {
      std::cout << contracts::canonical_json(execute_saa_search(
                       contracts::parse_json(argv[2]),
                       contracts::parse_json(argv[3])))
                << '\n';
      return 0;
    }
    if (command == "egcf-command-describe" && (argc == 3 || argc == 4)) {
      const std::filesystem::path resources =
          argc == 4 ? std::filesystem::path(argv[3])
                    : default_resource_root();
      const egcf::CommandRegistry registry(resources);
      std::cout << contracts::canonical_json(registry.describe(argv[2])) << '\n';
      return 0;
    }
    if (argc != 3) {
      usage(std::cerr);
      return 2;
    }
    try {
      std::cout << contracts::canonical_json(
                       response(command, execute_operation(
                                             command, request_json(argv[2]))))
                << '\n';
      return 0;
    } catch (const common::Error &error) {
      std::cout << contracts::canonical_json(error_response(
                       command, std::string(common::error_code_name(error.code())),
                       error.what()))
                << '\n';
      return 1;
    } catch (const std::exception &error) {
      std::cout << contracts::canonical_json(error_response(
                       command, "internal_failure", error.what()))
                << '\n';
      return 1;
    }
  } catch (const common::Error &error) {
    std::cerr << common::error_code_name(error.code()) << ": " << error.what()
              << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << "internal_failure: " << error.what() << '\n';
    return 1;
  }
}

} // namespace statewright::app
