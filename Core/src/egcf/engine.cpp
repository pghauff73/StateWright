#include "statewright/egcf/engine.hpp"

#include "ledger_support.hpp"
#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/contracts/typed_id.hpp"
#include "statewright/egcf/lifecycle.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::egcf {
namespace {

using Json = contracts::Json;

[[noreturn]] void engine_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied,
                      "EGCF execution: " + std::move(message));
}

[[nodiscard]] core::AuthorityManifest
normalize_authority(core::AuthorityManifest authority,
                    const core::Workspace &workspace, bool recovery) {
  const auto supplied_hash = authority.authority_hash;
  const auto finalized = core::finalize_authority(std::move(authority));
  if (!supplied_hash.empty() && supplied_hash != finalized.authority_hash) {
    engine_error("authority hash does not match its canonical payload");
  }
  core::validate_authority(finalized, workspace, !recovery);
  return finalized;
}

[[nodiscard]] std::vector<std::string>
canonical_strings(std::vector<std::string> values) {
  std::ranges::sort(values);
  values.erase(std::unique(values.begin(), values.end()), values.end());
  return values;
}

[[nodiscard]] Json resolve_value(const Json &value,
                                 const std::map<std::string, Json> &outputs,
                                 const std::set<std::string> &known_nodes) {
  if (value.is_object() && value.contains("$from")) {
    const auto source = value.at("$from").get<std::string>();
    if (!known_nodes.contains(source)) {
      return value;
    }
    const auto output = outputs.find(source);
    if (output == outputs.end()) {
      if (value.contains("default")) {
        return value.at("default");
      }
      engine_error("referenced node has no output: " + source);
    }
    Json result = output->second;
    if (value.contains("path")) {
      for (const auto &segment : value.at("path")) {
        try {
          if (segment.is_string()) {
            result = result.at(segment.get<std::string>());
          } else if (segment.is_number_unsigned()) {
            result = result.at(segment.get<std::size_t>());
          } else if (segment.is_number_integer()) {
            const auto index = segment.get<long long>();
            if (index < 0) {
              engine_error("output reference path index cannot be negative");
            }
            result = result.at(static_cast<std::size_t>(index));
          } else {
            engine_error("output reference path segment is invalid");
          }
        } catch (const common::Error &) {
          throw;
        } catch (const std::exception &) {
          if (value.contains("default")) {
            return value.at("default");
          }
          engine_error("cannot resolve output reference: " + source);
        }
      }
    }
    return result;
  }
  if (value.is_object()) {
    Json result = Json::object();
    for (const auto &[key, child] : value.items()) {
      result[key] = resolve_value(child, outputs, known_nodes);
    }
    return result;
  }
  if (value.is_array()) {
    Json result = Json::array();
    for (const auto &child : value) {
      result.push_back(resolve_value(child, outputs, known_nodes));
    }
    return result;
  }
  return value;
}

[[nodiscard]] bool truthy(const Json &value) {
  if (value.is_null()) {
    return false;
  }
  if (value.is_boolean()) {
    return value.get<bool>();
  }
  if (value.is_number()) {
    return value.get<long double>() != 0.0L;
  }
  if (value.is_string()) {
    return !value.get_ref<const std::string &>().empty();
  }
  return !value.empty();
}

[[nodiscard]] bool condition_met(const Json &condition,
                                 const std::map<std::string, Json> &outputs,
                                 const std::set<std::string> &known_nodes) {
  if (condition.empty()) {
    return true;
  }
  if (!condition.is_object()) {
    engine_error("workflow condition must be an object");
  }
  const std::set<std::string> allowed = {"value", "equals", "not_equals",
                                         "in", "truthy"};
  for (const auto &[key, unused] : condition.items()) {
    static_cast<void>(unused);
    if (!allowed.contains(key)) {
      engine_error("workflow condition has unknown field: " + key);
    }
  }
  const auto observed = resolve_value(condition.value("value", Json(nullptr)),
                                      outputs, known_nodes);
  bool has_check = false;
  bool result = true;
  if (condition.contains("equals")) {
    has_check = true;
    result = result && observed == condition.at("equals");
  }
  if (condition.contains("not_equals")) {
    has_check = true;
    result = result && observed != condition.at("not_equals");
  }
  if (condition.contains("in")) {
    has_check = true;
    if (!condition.at("in").is_array()) {
      engine_error("workflow condition in field must be an array");
    }
    result = result &&
             std::ranges::find(condition.at("in"), observed) !=
                 condition.at("in").end();
  }
  if (condition.contains("truthy")) {
    has_check = true;
    if (!condition.at("truthy").is_boolean()) {
      engine_error("workflow condition truthy field must be boolean");
    }
    result = result &&
             truthy(observed) == condition.at("truthy").get<bool>();
  }
  if (!has_check) {
    engine_error("workflow condition requires a comparison");
  }
  return result;
}

[[nodiscard]] std::vector<core::Change>
transaction_changes(const Json &inputs, const core::Workspace &workspace,
                    const std::vector<std::string> &scope,
                    const std::vector<std::string> &forbidden) {
  if (!inputs.contains("changes") || !inputs.at("changes").is_array() ||
      inputs.at("changes").empty()) {
    engine_error("eon.execute requires a non-empty changes array");
  }
  std::vector<core::Change> result;
  for (const auto &value : inputs.at("changes")) {
    if (!value.is_object()) {
      engine_error("eon.execute changes must be objects");
    }
    const auto operation = value.value("type", value.value("operation", ""));
    if (!value.contains("path") || !value.at("path").is_string()) {
      engine_error("eon.execute change requires a path");
    }
    const auto path = workspace.require_scope(
        value.at("path").get<std::string>(), scope, forbidden);
    if (operation == "write") {
      if (!value.contains("content") || !value.at("content").is_string()) {
        engine_error("eon.execute write requires string content");
      }
      result.push_back({.type = core::ChangeType::write,
                        .path = path,
                        .content = value.at("content").get<std::string>(),
                        .old_text = {},
                        .new_text = {},
                        .count = 1});
    } else if (operation == "replace") {
      if (!value.contains("old") || !value.at("old").is_string() ||
          !value.contains("new") || !value.at("new").is_string()) {
        engine_error("eon.execute replace requires old and new strings");
      }
      const int count = value.value("count", 1);
      if (count < 1) {
        engine_error("eon.execute replace count must be positive");
      }
      result.push_back({.type = core::ChangeType::replace,
                        .path = path,
                        .content = {},
                        .old_text = value.at("old").get<std::string>(),
                        .new_text = value.at("new").get<std::string>(),
                        .count = count});
    } else {
      engine_error("unsupported eon.execute change operation: " + operation);
    }
  }
  return result;
}

[[nodiscard]] Json output_envelope(std::string_view command_id, bool read_only,
                                   Json result, std::string_view algorithm_id,
                                   std::string_view source_snapshot_hash) {
  return {{"ok", true},
          {"command_id", command_id},
          {"read_only", read_only},
          {"result", std::move(result)},
          {"provenance",
           {{"algorithm_id", algorithm_id},
            {"implementation", "statewright-cpp"},
            {"source_snapshot_hash", source_snapshot_hash}}}};
}

[[nodiscard]] Json repository_metrics(const core::Workspace &workspace) {
  std::map<std::string, std::size_t> extensions;
  std::uintmax_t byte_count = 0;
  const auto files = workspace.files();
  for (const auto &path : files) {
    std::error_code error;
    byte_count += std::filesystem::file_size(path, error);
    if (error) {
      engine_error("cannot measure repository file: " + error.message());
    }
    auto extension = path.extension().string();
    if (extension.empty()) {
      extension = "<none>";
    }
    ++extensions[extension];
  }
  return {{"bytes", byte_count},
          {"extensions", extensions},
          {"files", files.size()},
          {"source_snapshot_hash", workspace.snapshot_hash()}};
}

void factorial_rows(const std::vector<std::pair<std::string, Json>> &dimensions,
                    std::size_t index, Json &row, Json &rows) {
  if (index == dimensions.size()) {
    rows.push_back(row);
    return;
  }
  const auto &[name, values] = dimensions.at(index);
  for (const auto &value : values) {
    row[name] = value;
    factorial_rows(dimensions, index + 1U, row, rows);
  }
  row.erase(name);
}

[[nodiscard]] Json covering_design(const Json &parameters, int strength) {
  if (!parameters.is_object() || parameters.empty() || parameters.size() > 10U) {
    engine_error("experiment parameters require one to ten dimensions");
  }
  if (strength != 2) {
    engine_error("v1 covering arrays support pairwise strength=2");
  }
  std::vector<std::pair<std::string, Json>> dimensions;
  std::size_t combination_count = 1U;
  for (const auto &[name, values] : parameters.items()) {
    if (name.empty() || !values.is_array() || values.empty() ||
        values.size() > 20U) {
      engine_error("experiment dimensions require names and one to twenty values");
    }
    if (combination_count > 10'000U / values.size()) {
      engine_error("factorial design exceeds 10000 combinations");
    }
    combination_count *= values.size();
    dimensions.emplace_back(name, values);
  }
  Json rows = Json::array();
  Json row = Json::object();
  factorial_rows(dimensions, 0U, row, rows);
  std::set<std::string> required_pairs;
  for (const auto &candidate : rows) {
    for (std::size_t left = 0; left < dimensions.size(); ++left) {
      for (std::size_t right = left + 1U; right < dimensions.size(); ++right) {
        required_pairs.insert(contracts::canonical_json(
            {dimensions.at(left).first, candidate.at(dimensions.at(left).first),
             dimensions.at(right).first,
             candidate.at(dimensions.at(right).first)}));
      }
    }
  }
  Json selected = Json::array();
  std::vector<Json> remaining(rows.begin(), rows.end());
  while (!required_pairs.empty()) {
    std::size_t best_index = 0U;
    std::size_t best_coverage = 0U;
    for (std::size_t index = 0; index < remaining.size(); ++index) {
      std::size_t coverage = 0U;
      for (std::size_t left = 0; left < dimensions.size(); ++left) {
        for (std::size_t right = left + 1U; right < dimensions.size(); ++right) {
          const auto key = contracts::canonical_json(
              {dimensions.at(left).first,
               remaining.at(index).at(dimensions.at(left).first),
               dimensions.at(right).first,
               remaining.at(index).at(dimensions.at(right).first)});
          coverage += required_pairs.contains(key) ? 1U : 0U;
        }
      }
      if (coverage > best_coverage) {
        best_coverage = coverage;
        best_index = index;
      }
    }
    if (remaining.empty() || best_coverage == 0U) {
      engine_error("covering design could not satisfy remaining pairs");
    }
    const auto best = remaining.at(best_index);
    selected.push_back(best);
    remaining.erase(remaining.begin() +
                    static_cast<std::ptrdiff_t>(best_index));
    for (std::size_t left = 0; left < dimensions.size(); ++left) {
      for (std::size_t right = left + 1U; right < dimensions.size(); ++right) {
        required_pairs.erase(contracts::canonical_json(
            {dimensions.at(left).first, best.at(dimensions.at(left).first),
             dimensions.at(right).first, best.at(dimensions.at(right).first)}));
      }
    }
  }
  return selected;
}

[[nodiscard]] std::vector<std::string> failure_categories(
    std::string_view observed) {
  std::string normalized_text(observed);
  std::ranges::transform(normalized_text, normalized_text.begin(),
                         [](unsigned char value) {
                           return static_cast<char>(std::tolower(value));
                         });
  const std::vector<std::pair<std::string, std::vector<std::string>>> markers = {
      {"authority", {"permission", "authority", "forbidden", "denied"}},
      {"scope", {"scope", "path", "escape", "target"}},
      {"evidence", {"evidence", "oracle", "coverage", "confidence"}},
      {"algorithm", {"algorithm", "qualification", "selection"}},
      {"execution", {"return code", "exception", "timeout", "crash"}},
      {"rollback", {"rollback", "restore", "compensat"}},
      {"drift", {"stale", "drift", "snapshot", "hash mismatch"}}};
  std::vector<std::string> result;
  for (const auto &[category, values] : markers) {
    if (std::ranges::any_of(values, [&normalized_text](const auto &marker) {
          return normalized_text.find(marker) != std::string::npos;
        })) {
      result.push_back(category);
    }
  }
  return result.empty() ? std::vector<std::string>{"unknown"} : result;
}

[[nodiscard]] std::map<std::string, Json>
compiled_nodes_by_id(const CompiledWorkflow &compiled) {
  std::map<std::string, Json> result;
  for (const auto &node : compiled.nodes) {
    if (!node.is_object() || !node.contains("node_id") ||
        !node.at("node_id").is_string() ||
        !result.emplace(node.at("node_id").get<std::string>(), node).second) {
      engine_error("compiled workflow node set is invalid");
    }
  }
  return result;
}

} // namespace

EgcfEngine::EgcfEngine(std::filesystem::path root,
                       std::filesystem::path resource_root,
                       core::AuthorityManifest authority, std::string actor,
                       bool recovery)
    : resource_root_(std::filesystem::weakly_canonical(resource_root)),
      workspace_(std::move(root)),
      authority_(normalize_authority(std::move(authority), workspace_, recovery)),
      store_(workspace_.root(), resource_root_), commands_(resource_root_),
      algorithms_(commands_),
      compiler_(store_, workspace_, commands_, algorithms_, authority_),
      approvals_(store_, workspace_), evidence_(store_), ieps_(evidence_),
      invariants_(store_), decisions_(store_),
      reasoning_(store_, ieps_),
      assurance_(store_, evidence_, invariants_, decisions_),
      transaction_events_(store_.state_root() / "runtime-events.jsonl"),
      transactions_(workspace_, store_.state_root() / "runtime",
                    authority_.authority_hash, transaction_events_),
      actor_(std::move(actor)) {
  if (actor_.empty()) {
    engine_error("actor must be non-empty");
  }
  bootstrap_algorithms();
  grant_id_ = register_capability_grant();
}

const core::Workspace &EgcfEngine::workspace() const noexcept {
  return workspace_;
}

const core::AuthorityManifest &EgcfEngine::authority() const noexcept {
  return authority_;
}

EgcfStore &EgcfEngine::store() noexcept { return store_; }

const CommandRegistry &EgcfEngine::commands() const noexcept { return commands_; }

const AlgorithmRegistry &EgcfEngine::algorithms() const noexcept {
  return algorithms_;
}

const std::string &EgcfEngine::capability_grant_id() const noexcept {
  return grant_id_;
}

EvidenceManager &EgcfEngine::evidence() noexcept { return evidence_; }

Ieps &EgcfEngine::ieps() noexcept { return ieps_; }

InvariantManager &EgcfEngine::invariants() noexcept { return invariants_; }

DecisionManager &EgcfEngine::decisions() noexcept { return decisions_; }

OiecSrProposalService &EgcfEngine::reasoning() noexcept { return reasoning_; }

void EgcfEngine::bootstrap_algorithms() {
  const std::map<std::string, std::string> specialized = {
      {"eon.execute@1", "eon"},
      {"repo.metrics@1", "builtin"},
      {"simulate.migration@1", "simulation"},
      {"simulate.worktree@1", "simulation"},
      {"workflow.compile@1", "builtin"},
      {"workflow.monitor@1", "builtin"},
      {"workflow.pause@1", "builtin"},
      {"workflow.replay@1", "builtin"},
      {"workflow.resume@1", "builtin"},
      {"ieps.generate@1", "builtin"},
      {"ieps.qualify@1", "builtin"},
      {"algorithm.benchmark@1", "builtin"},
      {"algorithm.compare@1", "builtin"},
      {"algorithm.explain@1", "builtin"},
      {"algorithm.search@1", "builtin"},
      {"algorithm.select@1", "builtin"},
      {"invariant.discover@1", "builtin"},
      {"decision.conflicts@1", "builtin"},
      {"experiment.covering@1", "builtin"},
      {"cfel.classify@1", "builtin"},
      {"evidence.confidence@1", "builtin"},
      {"assurance.generate@1", "builtin"},
      {"hrt.interpret@1", "builtin"},
      {"hrt.assumptions@1", "builtin"},
      {"hrt.ambiguity@1", "builtin"},
      {"hrt.claims@1", "builtin"},
      {"hrt.explain@1", "builtin"},
      {"hrt.summary@1", "builtin"},
      {"debug.hypotheses@1", "builtin"}};
  std::vector<EgcfRecord> records;
  std::vector<std::string> expected_ids;
  for (const auto &command : commands_.definitions()) {
    const auto command_id = command.command_id();
    const auto level = command.capability_query.value("level", "C0");
    const auto specialized_handler = specialized.find(command_id);
    if (specialized_handler == specialized.end() &&
        level != "C0" && level != "C1" && level != "C2") {
      continue;
    }
    const std::string implementation_kind =
        specialized_handler == specialized.end() ? "builtin"
                                                 : specialized_handler->second;
    const auto capabilities = command.capability_query.value(
        "facets", std::vector<std::string>{});
    const auto capability_level =
        command.capability_query.value("level", std::string("C0"));
    AlgorithmDefinition algorithm = {
        .name = "builtin." + command.namespace_name + "." + command.name,
        .version = command.version,
        .implementation_kind = implementation_kind,
        .implementation_ref = "statewright-core:" + command_id,
        .implementation_digest = contracts::sha256_text(
            "statewright-core:" + command_id + ":" + implementation_kind),
        .command_ids = {command_id},
        .input_schema = command.input_schema,
        .output_schema = command.output_schema,
        .applicability = Json::object(),
        .capability_requirements = capabilities,
        .capability_level = capability_level,
        .risk_floor = command.risk_policy,
        .rollback_class = command.rollback_policy,
        .invariants = command.invariants,
        .evidence_requirements = command.evidence_requirements,
        .qualification_policy = {{"deterministic", true}},
        .owner = "egcf-core",
        .provenance = {{"implementation", "statewright-cpp"}},
        .status = "QUALIFIED",
        .known_failures = {}};
    const auto algorithm_object_id =
        algorithms_.register_core_algorithm(algorithm);
    const auto &registered_algorithm =
        algorithms_.resolve_exact(algorithm.algorithm_id());
    if (registered_algorithm.object_id() != algorithm_object_id) {
      engine_error("core algorithm identity mismatch");
    }
    records.push_back(as_record(registered_algorithm));
    expected_ids.push_back(algorithm_object_id);
    QualificationRecord qualification = {
        .algorithm_id = registered_algorithm.algorithm_id(),
        .algorithm_digest = registered_algorithm.implementation_digest,
        .context = Json::object(),
        .context_hash = {},
        .evidence_ids = {"egcf-evidence:sha256:" + std::string(64U, '0')},
        .tests = {{{"name", "core-contract"}, {"success", true}}},
        .benchmarks = Json::array(),
        .known_failures = {},
        .status = "QUALIFIED",
        .qualified_by = "deterministic-statewright-core",
        .created_at = "2026-09-02T00:00:00Z",
        .expires_at = "2099-01-01T00:00:00Z"};
    const auto qualification_id =
        algorithms_.register_qualification(qualification);
    const auto qualifications =
        algorithms_.qualifications(registered_algorithm);
    const auto registered_qualification = std::ranges::find_if(
        qualifications, [&qualification_id](const auto &candidate) {
          return candidate.object_id() == qualification_id;
        });
    if (registered_qualification == qualifications.end()) {
      engine_error("core qualification identity mismatch");
    }
    records.push_back(as_record(*registered_qualification));
    expected_ids.push_back(qualification_id);
  }
  const auto registered_ids =
      store_.register_records(records, "egcf_core_algorithm_bootstrapped");
  if (registered_ids != expected_ids) {
    engine_error("core algorithm bootstrap persistence mismatch");
  }
}

std::string EgcfEngine::register_capability_grant() {
  auto capabilities = authority_.read_capabilities;
  capabilities.insert(capabilities.end(), authority_.command_capabilities.begin(),
                      authority_.command_capabilities.end());
  capabilities.insert(capabilities.end(), authority_.semantic_capabilities.begin(),
                      authority_.semantic_capabilities.end());
  capabilities = canonical_strings(std::move(capabilities));
  const Json payload = {
      {"approval_modes", {"automatic", "policy", "human"}},
      {"authority_hash", authority_.authority_hash},
      {"budget", Json::object()},
      {"capabilities", capabilities},
      {"capability_ceiling", authority_.semantic_capability_ceiling},
      {"expires_at", authority_.expires_at},
      {"issuer", authority_.operator_name},
      {"resources", {{"forbidden_paths", authority_.forbidden_paths}}},
      {"scope", authority_.allowed_paths},
      {"subject", actor_},
      {"use_count", 0},
      {"use_limit", 20}};
  return store_.register_record(
      {.object_type = "capability-grant", .payload = payload},
      "egcf_capability_granted");
}

CompiledWorkflow EgcfEngine::compile(const WorkflowDefinition &workflow,
                                     const CommandContext &context) {
  return compiler_.compile(workflow, context);
}

ExecutionPlan
EgcfEngine::create_execution_plan(const CompiledWorkflow &compiled,
                                  bool prepare_mutations) {
  const auto stored = store_.get(compiled.object_id());
  if (stored.object_type != "compiled-workflow" ||
      stored.payload != to_json(compiled)) {
    engine_error("execution planning requires the canonical compiled object");
  }
  if (compiled.source_snapshot_hash != workspace_.snapshot_hash()) {
    engine_error("compiled workflow source snapshot is stale");
  }
  auto rollback_graph = compiled.rollback_graph;
  std::vector<std::string> action_ids;
  std::vector<std::string> algorithm_digests;
  std::vector<std::string> evidence_ids = compiled.command_context.evidence;
  int mutation_count = 0;
  const auto nodes = compiled_nodes_by_id(compiled);
  for (const auto &node_id : compiled.execution_order) {
    const auto &node = nodes.at(node_id);
    const auto selection_id = node.at("selection_id").get<std::string>();
    const auto selection = store_.get(selection_id);
    if (selection.object_type != "selection-decision") {
      engine_error("compiled node selection is not canonical");
    }
    evidence_ids.push_back(selection_id);
    const auto selected_evidence = selection.payload.value(
        "evidence_ids", std::vector<std::string>{});
    evidence_ids.insert(evidence_ids.end(), selected_evidence.begin(),
                        selected_evidence.end());
    algorithm_digests.push_back(
        node.at("algorithm_digest").get<std::string>());
    const auto &algorithm = algorithms_.resolve_exact(
        node.at("algorithm_id").get<std::string>());
    if (algorithm.object_id() !=
            node.at("algorithm_definition_id").get<std::string>() ||
        algorithm.implementation_digest !=
            node.at("algorithm_digest").get<std::string>()) {
      engine_error("compiled algorithm identity drifted before planning");
    }
    if (algorithm.implementation_kind == "eon" && prepare_mutations) {
      ++mutation_count;
      if (mutation_count > 1) {
        engine_error(
            "the admitted C3 vertical slice supports one transaction per plan");
      }
      const auto changes = transaction_changes(
          node.at("inputs"), workspace_,
          node.at("scope").get<std::vector<std::string>>(),
          authority_.forbidden_paths);
      const auto transaction = transactions_.prepare_changes(changes);
      rollback_graph[node_id]["prepared"] = {
          {"candidate_hash", transaction.candidate_hash},
          {"diff", transaction.diff},
          {"source_snapshot_hash", transaction.source_snapshot_hash},
          {"status", transaction.status},
          {"targets", transaction.targets},
          {"transaction_id", transaction.transaction_id}};
      action_ids.push_back(transaction.transaction_id);
    }
  }
  ExecutionPlan plan = {
      .compiled_workflow_id = compiled.object_id(),
      .graph_hash = compiled.graph_hash,
      .source_snapshot_hash = compiled.source_snapshot_hash,
      .node_order = compiled.execution_order,
      .eon_action_ids = std::move(action_ids),
      .algorithm_digests = std::move(algorithm_digests),
      .capability_grant_id = grant_id_,
      .evidence_ids = canonical_strings(std::move(evidence_ids)),
      .budget = compiled.budget,
      .rollback_graph = std::move(rollback_graph),
      .approval_policy = compiled.approval_policy,
      .expires_at = authority_.expires_at,
      .created_at = ledger_support::utc_now()};
  const auto plan_id = store_.register_record(
      {.object_type = "execution-plan", .payload = to_json(plan)},
      "egcf_execution_plan_created");
  if (plan_id != plan.object_id()) {
    engine_error("execution plan identity mismatch");
  }
  return plan;
}

std::string EgcfEngine::authorize(std::string_view plan_id,
                                  std::string approver,
                                  std::string authority_statement,
                                  Json constraints, std::string expires_at,
                                  int use_limit) {
  return approvals_.authorize(plan_id, std::move(approver),
                              std::move(authority_statement),
                              std::move(constraints), std::move(expires_at),
                              true, use_limit);
}

ExecutionPlan EgcfEngine::load_plan(std::string_view plan_id) const {
  const auto stored = store_.get(plan_id);
  if (stored.object_type != "execution-plan") {
    engine_error("not an execution plan: " + std::string(plan_id));
  }
  const auto plan = execution_plan_from_json(stored.payload);
  if (plan.object_id() != plan_id) {
    engine_error("execution plan identity mismatch");
  }
  return plan;
}

CompiledWorkflow EgcfEngine::load_compiled(const ExecutionPlan &plan) const {
  const auto stored = store_.get(plan.compiled_workflow_id);
  if (stored.object_type != "compiled-workflow") {
    engine_error("execution plan references an invalid compiled workflow");
  }
  const auto compiled = compiled_workflow_from_json(stored.payload);
  if (compiled.object_id() != plan.compiled_workflow_id) {
    engine_error("compiled workflow identity mismatch");
  }
  return compiled;
}

void EgcfEngine::validate_plan(const ExecutionPlan &plan,
                               const CompiledWorkflow &compiled,
                               bool require_current_source) const {
  if (plan.graph_hash != compiled.graph_hash ||
      plan.source_snapshot_hash != compiled.source_snapshot_hash ||
      plan.node_order != compiled.execution_order ||
      plan.capability_grant_id != grant_id_) {
    engine_error("execution plan no longer matches its compiled workflow");
  }
  if (require_current_source &&
      plan.source_snapshot_hash != workspace_.snapshot_hash()) {
    engine_error("execution plan source snapshot is stale");
  }
  if (!plan.expires_at.empty() && plan.expires_at <= ledger_support::utc_now()) {
    engine_error("execution plan has expired");
  }
  const auto nodes = compiled_nodes_by_id(compiled);
  std::vector<std::string> observed_digests;
  for (const auto &node_id : plan.node_order) {
    const auto &node = nodes.at(node_id);
    const auto &algorithm = algorithms_.resolve_exact(
        node.at("algorithm_id").get<std::string>());
    if (algorithm.status != "QUALIFIED" ||
        algorithm.implementation_digest !=
            node.at("algorithm_digest").get<std::string>()) {
      engine_error("algorithm is stale or not executable");
    }
    observed_digests.push_back(algorithm.implementation_digest);
  }
  if (observed_digests != plan.algorithm_digests) {
    engine_error("execution plan algorithm digest sequence is invalid");
  }
}

std::vector<std::string>
EgcfEngine::register_evidence(const ExecutionPlan &plan, const Json &node,
                              const Json &verification, bool simulated) {
  const auto node_id = node.at("node_id").get<std::string>();
  const auto independence_group = "executor:" + node_id;
  const auto category = simulated ? "simulation" : "test";
  std::vector<std::pair<std::string, std::string>> requirements;
  for (const auto &name :
       node.at("evidence_requirements").get<std::vector<std::string>>()) {
    requirements.emplace_back(
        name, evidence_.add_requirement(
                  {.subject_id = plan.object_id(),
                   .name = name,
                   .category = category,
                   .oracle = "statewright.egcf.verify",
                   .freshness_seconds = 0,
                   .independence_group = independence_group,
                   .mandatory = true}));
  }
  if (requirements.empty()) {
    requirements.emplace_back(std::string{}, std::string{});
  }
  std::vector<std::string> evidence_ids;
  evidence_ids.reserve(requirements.size());
  for (const auto &[name, requirement_id] : requirements) {
    evidence_ids.push_back(evidence_.collect(
        {.subject_id = plan.object_id(),
         .content = {{"node_id", node.at("node_id")},
                     {"requirement_name", name},
                     {"verification", verification}},
         .category = category,
         .producer = "deterministic-statewright-egcf",
         .method = "deterministic executor verification",
         .source_snapshot_hash = plan.source_snapshot_hash,
         .target = node_id,
         .oracle = "statewright.egcf.verify",
         .environment = {{"engine", "statewright-cpp"}},
         .command_id = node.at("command_id").get<std::string>(),
         .algorithm_id = node.at("algorithm_id").get<std::string>(),
         .requirement_ids = requirement_id.empty()
                                ? std::vector<std::string>{}
                                : std::vector<std::string>{requirement_id},
         .success = true,
         .limitations = simulated
                            ? std::vector<std::string>{"simulation evidence"}
                            : std::vector<std::string>{},
         .independence_group = independence_group,
         .simulated = simulated}));
  }
  return evidence_ids;
}

Json EgcfEngine::execute_plan(std::string_view plan_id,
                              std::string_view approval_id,
                              bool pause_at_checkpoint, bool resume) {
  const auto plan = load_plan(plan_id);
  const auto compiled = load_compiled(plan);
  validate_plan(plan, compiled, true);
  if ((plan.approval_policy == "human" ||
       plan.approval_policy == "quorum")) {
    if (approval_id.empty()) {
      engine_error("execution plan requires exact human approval");
    }
    const auto approval = approvals_.validate(plan, approval_id);
    if (approval.constraints.value("simulated_only", false) &&
        !compiled.command_context.simulate) {
      engine_error("approval permits simulated execution only");
    }
  }

  Lifecycle lifecycle("COMPILED");
  static_cast<void>(lifecycle.transition("AUTHORIZED"));
  static_cast<void>(lifecycle.transition("EXECUTING"));
  const auto nodes = compiled_nodes_by_id(compiled);
  std::set<std::string> known_nodes;
  for (const auto &[node_id, unused] : nodes) {
    static_cast<void>(unused);
    known_nodes.insert(node_id);
  }
  std::map<std::string, Json> outputs;
  std::vector<std::string> execution_ids;
  std::vector<std::string> evidence_ids;
  std::vector<std::string> applied_transactions;
  std::set<std::string> completed_node_ids;
  bool paused_checkpoint_found = false;
  bool workflow_completed = false;
  for (const auto &stored : store_.list("execution")) {
    const auto record = execution_from_json(stored.payload);
    if (record.plan_id != plan.object_id()) {
      continue;
    }
    if (record.node_id == "__workflow__" &&
        (record.status == "COMPLETED" || record.status == "SIMULATED")) {
      workflow_completed = true;
    }
    if (record.status == "PAUSED" &&
        record.node_id.starts_with("__checkpoint__:")) {
      paused_checkpoint_found = true;
    }
    if (resume && nodes.contains(record.node_id) &&
        (record.status == "COMPLETED" || record.status == "SIMULATED" ||
         record.status == "SKIPPED")) {
      completed_node_ids.insert(record.node_id);
      outputs[record.node_id] = record.output;
      execution_ids.push_back(stored.object_id);
      evidence_ids.insert(evidence_ids.end(), record.evidence_ids.begin(),
                          record.evidence_ids.end());
    }
  }
  if (workflow_completed) {
    engine_error("execution plan already has a terminal workflow execution");
  }
  if (resume && !paused_checkpoint_found) {
    engine_error("resume requires a persisted paused checkpoint");
  }
  if (!resume && paused_checkpoint_found) {
    engine_error("paused execution requires explicit resume");
  }
  const auto started = std::chrono::steady_clock::now();
  const auto workflow_started_at = ledger_support::utc_now();
  std::size_t action_index = 0;
  try {
    for (const auto &node_id : plan.node_order) {
      ++action_index;
      if (completed_node_ids.contains(node_id)) {
        continue;
      }
      if (plan.budget.actions &&
          action_index > static_cast<std::size_t>(*plan.budget.actions)) {
        engine_error("execution action budget exceeded");
      }
      if (plan.budget.wall_seconds &&
          std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                        started)
                  .count() > *plan.budget.wall_seconds) {
        engine_error("execution wall-time budget exceeded");
      }
      const auto &node = nodes.at(node_id);
      for (const auto &dependency :
           node.at("depends_on").get<std::vector<std::string>>()) {
        if (!outputs.contains(dependency)) {
          engine_error("node dependency has not completed: " + dependency);
        }
      }
      const auto inputs = resolve_value(node.at("inputs"), outputs, known_nodes);
      const auto &definition = commands_.resolve_exact(
          node.at("command_id").get<std::string>());
      store_.objects().schemas().validate_json_value(definition.input_schema,
                                                     inputs, "$input");
      const auto &algorithm = algorithms_.resolve_exact(
          node.at("algorithm_id").get<std::string>());
      const auto started_at = ledger_support::utc_now();
      Json output;
      bool simulated = false;
      bool skipped = false;
      std::optional<core::TransactionRecord> transaction;
      if (!condition_met(node.at("when"), outputs, known_nodes)) {
        output = output_envelope(node.at("command_id").get<std::string>(), true,
                                 {{"condition", node.at("when")},
                                 {"skipped", true}},
                                 algorithm.algorithm_id(),
                                 plan.source_snapshot_hash);
        skipped = true;
      } else if (algorithm.implementation_kind == "eon") {
        if (compiled.command_context.simulate) {
          const auto result = simulation_.worktree(workspace_.root(),
                                                   inputs.at("changes"));
          output = output_envelope(definition.command_id(), true, result,
                                   algorithm.algorithm_id(),
                                   plan.source_snapshot_hash);
          simulated = true;
        } else {
          const auto prepared = plan.rollback_graph.at(node_id).at("prepared");
          transaction = transactions_.load(
              prepared.at("transaction_id").get<std::string>());
          if (transaction->status != "PREPARED" ||
              transaction->candidate_hash !=
                  prepared.at("candidate_hash").get<std::string>() ||
              transaction->source_snapshot_hash != plan.source_snapshot_hash) {
            engine_error("prepared transaction does not match approved plan");
          }
          transactions_.apply(*transaction);
          applied_transactions.push_back(transaction->transaction_id);
          transactions_.verify_applied(*transaction);
          output = output_envelope(
              definition.command_id(), false,
              {{"applied_snapshot_hash", transaction->applied_snapshot_hash},
               {"candidate_hash", transaction->candidate_hash},
               {"status", "VERIFIED"},
               {"targets", transaction->targets},
               {"transaction_id", transaction->transaction_id}},
              algorithm.algorithm_id(), plan.source_snapshot_hash);
        }
      } else if (algorithm.implementation_kind == "simulation") {
        Json result;
        if (definition.command_id() == "simulate.migration@1") {
          result = simulation_.migration(inputs.at("before"),
                                         inputs.at("operations"));
        } else if (definition.command_id() == "simulate.worktree@1") {
          result = simulation_.worktree(
              workspace_.root(), inputs.value("changes", Json::array()));
        } else {
          engine_error("unsupported simulation algorithm");
        }
        output = output_envelope(definition.command_id(), true, result,
                                 algorithm.algorithm_id(),
                                 plan.source_snapshot_hash);
        simulated = true;
      } else if (definition.command_id() == "repo.metrics@1") {
        output = output_envelope(definition.command_id(), true,
                                 repository_metrics(workspace_),
                                 algorithm.algorithm_id(),
                                 plan.source_snapshot_hash);
      } else if (definition.command_id() == "workflow.monitor@1") {
        const auto target_plan_id = inputs.value("plan_id", std::string{});
        Json executions = Json::array();
        for (const auto &record : store_.list("execution")) {
          const auto execution = execution_from_json(record.payload);
          if (execution.plan_id == target_plan_id) {
            auto value = to_json(execution);
            value["id"] = record.object_id;
            executions.push_back(std::move(value));
          }
        }
        output = output_envelope(
            definition.command_id(), true,
            {{"executions", std::move(executions)},
             {"plan_id", target_plan_id}},
            algorithm.algorithm_id(), plan.source_snapshot_hash);
      } else if (definition.command_id() == "workflow.pause@1") {
        const auto target_plan_id = inputs.value("plan_id", std::string{});
        static_cast<void>(load_plan(target_plan_id));
        const auto timestamp = ledger_support::utc_now();
        const ExecutionRecord control = {
            .plan_id = target_plan_id,
            .node_id = "__control__",
            .algorithm_id = "workflow.control@1",
            .executor = "egcf-engine",
            .inputs_hash = contracts::sha256_json(inputs),
            .output = {{"checkpoint", inputs.value("checkpoint", false)}},
            .status = "PAUSE_REQUESTED",
            .usage = {{"actions", 0}},
            .evidence_ids = {},
            .started_at = timestamp,
            .completed_at = timestamp,
            .simulated = false};
        const auto control_id = store_.register_record(
            {.object_type = "execution", .payload = to_json(control)},
            "egcf_workflow_pause_requested");
        output = output_envelope(
            definition.command_id(), true,
            {{"control_id", control_id},
             {"plan_id", target_plan_id},
             {"status", "PAUSE_REQUESTED"}},
            algorithm.algorithm_id(), plan.source_snapshot_hash);
      } else if (definition.command_id() == "workflow.resume@1") {
        const auto target_plan_id = inputs.value("plan_id", std::string{});
        static_cast<void>(load_plan(target_plan_id));
        std::string paused_execution_id;
        for (const auto &record : store_.list("execution")) {
          const auto execution = execution_from_json(record.payload);
          if (execution.plan_id == target_plan_id &&
              execution.status == "PAUSED") {
            paused_execution_id = record.object_id;
          }
        }
        if (paused_execution_id.empty()) {
          engine_error("workflow has no persisted paused checkpoint");
        }
        const auto timestamp = ledger_support::utc_now();
        const ExecutionRecord control = {
            .plan_id = target_plan_id,
            .node_id = "__control__",
            .algorithm_id = "workflow.control@1",
            .executor = "egcf-engine",
            .inputs_hash = contracts::sha256_json(inputs),
            .output = {{"paused_execution_id", paused_execution_id}},
            .status = "RESUME_REQUESTED",
            .usage = {{"actions", 0}},
            .evidence_ids = {},
            .started_at = timestamp,
            .completed_at = timestamp,
            .simulated = false};
        const auto control_id = store_.register_record(
            {.object_type = "execution", .payload = to_json(control)},
            "egcf_workflow_resume_requested");
        output = output_envelope(
            definition.command_id(), true,
            {{"control_id", control_id},
             {"plan_id", target_plan_id},
             {"revalidation_required", true},
             {"status", "RESUME_REQUESTED"}},
            algorithm.algorithm_id(), plan.source_snapshot_hash);
      } else if (definition.command_id() == "workflow.replay@1") {
        const auto target = load_plan(inputs.at("plan_id").get<std::string>());
        output = output_envelope(
            definition.command_id(), true,
            {{"current_snapshot", workspace_.snapshot_hash()},
             {"historical_plan_id", target.object_id()},
             {"historical_snapshot", target.source_snapshot_hash},
             {"reauthorization_required", true},
             {"same_snapshot",
              target.source_snapshot_hash == workspace_.snapshot_hash()}},
            algorithm.algorithm_id(), plan.source_snapshot_hash);
      } else if (definition.command_id() == "workflow.compile@1") {
        Json definition_value;
        if (inputs.contains("workflow_definition_id")) {
          const auto workflow_record = store_.get(
              inputs.at("workflow_definition_id").get<std::string>());
          if (workflow_record.object_type != "workflow-definition") {
            engine_error("workflow_definition_id is not a workflow");
          }
          definition_value = workflow_record.payload;
        } else if (inputs.contains("definition")) {
          definition_value = inputs.at("definition").is_string()
                                 ? contracts::parse_json(
                                       inputs.at("definition").get<std::string>())
                                 : inputs.at("definition");
        } else {
          definition_value = {
              {"description", inputs.value("description", std::string{})},
              {"name", inputs.value("name", std::string("workflow"))},
              {"nodes", inputs.value("nodes", Json::array())},
              {"outputs", inputs.value("outputs", Json::object())},
              {"parameters", inputs.value("parameters", Json::object())},
              {"version", inputs.value("version", 1)}};
        }
        const auto nested = compiler_.compile(
            workflow_definition_from_json(definition_value),
            compiled.command_context);
        output = output_envelope(
            definition.command_id(), true,
            {{"compiled_workflow_id", nested.object_id()},
             {"graph_hash", nested.graph_hash}},
            algorithm.algorithm_id(), plan.source_snapshot_hash);
      } else if (definition.command_id() == "ieps.generate@1") {
        std::vector<std::string> requirement_ids;
        for (const auto &requirement : inputs.at("requirements")) {
          requirement_ids.push_back(ieps_.oracle(
              inputs.at("subject_id").get<std::string>(),
              requirement.at("name").get<std::string>(),
              requirement.at("category").get<std::string>(),
              requirement.value("oracle", std::string{}),
              requirement.value("mandatory", true),
              requirement.value("freshness_seconds", 0),
              requirement.value("independence_group",
                                requirement.at("name").get<std::string>())));
        }
        output = output_envelope(
            definition.command_id(), true,
            {{"requirement_ids", requirement_ids},
             {"status", "CANDIDATE_EVIDENCE_PLAN"}},
            algorithm.algorithm_id(), plan.source_snapshot_hash);
      } else if (definition.command_id() == "ieps.qualify@1") {
        output = output_envelope(
            definition.command_id(), true,
            ieps_.qualify(inputs.at("subject_id").get<std::string>()),
            algorithm.algorithm_id(), plan.source_snapshot_hash);
      } else if (definition.command_id() == "algorithm.search@1") {
        const auto &requested = commands_.resolve_for_discovery(
            inputs.at("command_id").get<std::string>());
        Json candidates = Json::array();
        for (const auto &candidate :
             algorithms_.search(requested.command_id())) {
          auto value = to_json(candidate);
          value["algorithm_id"] = candidate.algorithm_id();
          value["object_id"] = candidate.object_id();
          candidates.push_back(std::move(value));
        }
        output = output_envelope(
            definition.command_id(), true,
            {{"algorithms", std::move(candidates)},
             {"command_id", requested.command_id()}},
            algorithm.algorithm_id(), plan.source_snapshot_hash);
      } else if (definition.command_id() == "algorithm.compare@1") {
        Json comparisons = Json::array();
        for (const auto &algorithm_id :
             inputs.value("algorithm_ids", std::vector<std::string>{})) {
          const auto &candidate = algorithms_.resolve_exact(algorithm_id);
          comparisons.push_back(
              {{"algorithm_id", candidate.algorithm_id()},
               {"capability_level", candidate.capability_level},
               {"known_failures", candidate.known_failures},
               {"qualification_count",
                algorithms_.qualifications(candidate).size()},
               {"risk_floor", candidate.risk_floor},
               {"rollback_class", candidate.rollback_class},
               {"status", candidate.status}});
        }
        output = output_envelope(definition.command_id(), true,
                                 {{"algorithms", std::move(comparisons)}},
                                 algorithm.algorithm_id(),
                                 plan.source_snapshot_hash);
      } else if (definition.command_id() == "algorithm.explain@1") {
        const auto selection = store_.get(
            inputs.at("selection_id").get<std::string>());
        if (selection.object_type != "selection-decision") {
          engine_error("algorithm explanation requires a selection decision");
        }
        output = output_envelope(
            definition.command_id(), true,
            {{"selection_id", selection.object_id()},
             {"selection", selection.payload}},
            algorithm.algorithm_id(), plan.source_snapshot_hash);
      } else if (definition.command_id() == "algorithm.benchmark@1") {
        const auto samples =
            inputs.value("samples", std::vector<double>{});
        Json minimum = nullptr;
        Json maximum = nullptr;
        Json mean = nullptr;
        if (!samples.empty()) {
          const auto [minimum_iterator, maximum_iterator] =
              std::ranges::minmax_element(samples);
          double total = 0.0;
          for (const auto sample : samples) {
            total += sample;
          }
          minimum = *minimum_iterator;
          maximum = *maximum_iterator;
          mean = total / static_cast<double>(samples.size());
        }
        output = output_envelope(
            definition.command_id(), true,
            {{"algorithm_id", inputs.value("algorithm_id", std::string{})},
             {"maximum", maximum},
             {"mean", mean},
             {"minimum", minimum},
             {"sample_count", samples.size()},
             {"unit", inputs.value("unit", std::string("seconds"))}},
            algorithm.algorithm_id(), plan.source_snapshot_hash);
      } else if (definition.command_id() == "algorithm.select@1") {
        const auto &requested = commands_.resolve_for_discovery(
            inputs.at("command_id").get<std::string>());
        auto allowed = authority_.read_capabilities;
        allowed.insert(allowed.end(), authority_.command_capabilities.begin(),
                       authority_.command_capabilities.end());
        allowed.insert(allowed.end(), authority_.semantic_capabilities.begin(),
                       authority_.semantic_capabilities.end());
        allowed = canonical_strings(std::move(allowed));
        SelectionEngine selector(algorithms_);
        const auto selection = selector.select(
            requested.command_id(),
            inputs.value("context", Json::object()),
            authority_.semantic_capability_ceiling, allowed,
            inputs.value("invariants", std::vector<std::string>{}),
            ledger_support::utc_now());
        const auto selection_id = store_.register_record(
            {.object_type = "selection-decision",
             .payload = to_json(selection)},
            "egcf_algorithm_selected");
        auto result = to_json(selection);
        result["selection_id"] = selection_id;
        output = output_envelope(definition.command_id(), true, result,
                                 algorithm.algorithm_id(),
                                 plan.source_snapshot_hash);
      } else if (definition.command_id() == "invariant.discover@1") {
        const auto identifiers = invariants_.discover(
            inputs.at("statements").get<std::vector<std::string>>(),
            inputs.value("scope", std::vector<std::string>{"**"}),
            inputs.value("source", std::string("model proposal")));
        output = output_envelope(
            definition.command_id(), true,
            {{"candidate_invariant_ids", identifiers}, {"registered", false}},
            algorithm.algorithm_id(), plan.source_snapshot_hash);
      } else if (definition.command_id() == "decision.conflicts@1") {
        output = output_envelope(
            definition.command_id(), true,
            {{"conflicts", decisions_.conflicts()}}, algorithm.algorithm_id(),
            plan.source_snapshot_hash);
      } else if (definition.command_id() == "experiment.covering@1") {
        const int strength = inputs.value("strength", 2);
        const auto design = covering_design(inputs.at("parameters"), strength);
        output = output_envelope(
            definition.command_id(), true,
            {{"design", design}, {"runs", design.size()}, {"strength", 2}},
            algorithm.algorithm_id(), plan.source_snapshot_hash);
      } else if (definition.command_id() == "cfel.classify@1") {
        const auto observed = inputs.at("observed").get<std::string>();
        const auto categories = failure_categories(observed);
        const FailureRecord failure = {
            .subject_id = inputs.value("subject_id", std::string{}),
            .expected = inputs.value("expected", std::string{}),
            .observed = observed,
            .active_dimension =
                inputs.value("active_dimension", categories.front()),
            .frozen_dimensions = inputs.value(
                "frozen_dimensions", std::vector<std::string>{}),
            .evidence_ids =
                inputs.value("evidence_ids", std::vector<std::string>{}),
            .retry_count = inputs.value("retry_count", 0),
            .status = "CLASSIFIED",
            .created_at = ledger_support::utc_now()};
        const auto failure_id = store_.register_record(
            {.object_type = "failure", .payload = to_json(failure)},
            "egcf_failure_classified");
        output = output_envelope(
            definition.command_id(), true,
            {{"categories", categories},
             {"failure_id", failure_id},
             {"proposal_only", false}},
            algorithm.algorithm_id(), plan.source_snapshot_hash);
      } else if (definition.command_id() == "evidence.confidence@1") {
        const auto assessment = evidence_.confidence(
            inputs.at("subject_id").get<std::string>(),
            inputs.value("policy", std::string("egcf-default-v1")));
        auto result = to_json(assessment);
        result["confidence_id"] = assessment.object_id();
        output = output_envelope(definition.command_id(), true, result,
                                 algorithm.algorithm_id(),
                                 plan.source_snapshot_hash);
      } else if (definition.command_id() == "assurance.generate@1") {
        Json approval_facts = {{"satisfied", false},
                               {"source", "no immutable approval supplied"}};
        if (inputs.contains("approval_id") &&
            !inputs.at("approval_id").get<std::string>().empty()) {
          const auto approval_record =
              store_.get(inputs.at("approval_id").get<std::string>());
          if (approval_record.object_type != "approval") {
            engine_error("assurance approval is not canonical");
          }
          const auto approval = approval_from_json(approval_record.payload);
          if (!approval.human) {
            engine_error("assurance approval is not human");
          }
          approval_facts = Json::object();
          approval_facts["approval_id"] = approval_record.object_id();
          approval_facts["approver"] = approval.approver;
          approval_facts["plan_hash"] = approval.plan_hash;
          approval_facts["plan_id"] = approval.plan_id;
          approval_facts["satisfied"] = true;
        } else if (inputs.contains("approval_facts")) {
          approval_facts["reported_claim"] = inputs.at("approval_facts");
        }
        const auto assurance = assurance_.generate(
            inputs.at("subject_id").get<std::string>(),
            inputs.value("top_claim", std::string(
                                          "The subject satisfies its engineering requirements")),
            inputs.value("capability_facts", Json::object()), approval_facts,
            inputs.value("rollback_argument", Json::object()),
            inputs.value("uncertainties", std::vector<std::string>{}));
        auto result = to_json(assurance);
        result["assurance_case_id"] = assurance.object_id();
        output = output_envelope(definition.command_id(), true, result,
                                 algorithm.algorithm_id(),
                                 plan.source_snapshot_hash);
      } else if (definition.namespace_name == "hrt" ||
                 definition.command_id() == "debug.hypotheses@1") {
        const auto text = inputs.value("text", inputs.value("request", ""));
        Json result;
        if (definition.command_id() == "hrt.interpret@1") {
          std::vector<std::string> ambiguities;
          std::string lowered = text;
          std::ranges::transform(lowered, lowered.begin(),
                                 [](unsigned char character) {
                                   return static_cast<char>(
                                       std::tolower(character));
                                 });
          for (const auto marker : {"maybe", "either", " or ", "unsure", "?"}) {
            if (lowered.find(marker) != std::string::npos) {
              ambiguities.emplace_back(marker);
            }
          }
          result = {{"ambiguities", ambiguities},
                    {"assumptions",
                     inputs.value("assumptions", std::vector<std::string>{})},
                    {"objective", text},
                    {"statements", text.empty() ? Json::array() : Json({text})}};
        } else if (definition.command_id() == "hrt.summary@1") {
          result = {{"summary", text}};
        } else if (definition.command_id() == "hrt.assumptions@1") {
          result = {{"assumptions",
                     inputs.value("assumptions", std::vector<std::string>{})},
                    {"implicit_candidates", Json::array()}};
        } else if (definition.command_id() == "hrt.ambiguity@1") {
          const bool ambiguous = text.find('?') != std::string::npos ||
                                 text.find(" or ") != std::string::npos;
          result = {{"ambiguous", ambiguous},
                    {"clarification_required", ambiguous},
                    {"markers", ambiguous ? Json({"? or alternative"})
                                           : Json::array()}};
        } else if (definition.command_id() == "hrt.claims@1" ||
                   definition.command_id() == "debug.hypotheses@1") {
          result = reasoning_.propose(inputs, plan.source_snapshot_hash,
                                      compiled.command_context.scope);
        } else {
          result = {{"ambiguities", Json::array()},
                    {"assumptions",
                     inputs.value("assumptions", std::vector<std::string>{})},
                    {"explanation", text}};
        }
        output = output_envelope(definition.command_id(), true, result,
                                 algorithm.algorithm_id(),
                                 plan.source_snapshot_hash);
      } else if (algorithm.implementation_kind == "builtin" &&
                 (algorithm.capability_level == "C0" ||
                  algorithm.capability_level == "C1" ||
                  algorithm.capability_level == "C2")) {
        const Json result = {
            {"assumptions",
             inputs.value("assumptions", std::vector<std::string>{})},
            {"inputs", core::redact(inputs)},
            {"limitations",
             {"v1 generic semantic adapter",
              "no external or workspace mutation executor invoked"}},
            {"operation",
             definition.namespace_name + "." + definition.name},
            {"status", "READ_ONLY_RESULT"},
            {"strict", compiled.command_context.strict}};
        output = output_envelope(definition.command_id(), true, result,
                                 algorithm.algorithm_id(),
                                 plan.source_snapshot_hash);
        simulated = compiled.command_context.simulate;
      } else {
        engine_error("unsupported core algorithm dispatch: " +
                     definition.command_id());
      }

      store_.objects().schemas().validate_json_value(definition.output_schema,
                                                     output, "$output");
      store_.objects().schemas().validate_json_value(algorithm.output_schema,
                                                     output,
                                                     "$algorithm_output");
      const Json verification = {
          {"output_hash", contracts::sha256_json(output)},
          {"source_snapshot_hash",
           transaction ? transaction->applied_snapshot_hash
                       : workspace_.snapshot_hash()},
          {"verified", true}};
      const auto node_evidence_ids =
          register_evidence(plan, node, verification, simulated);
      evidence_ids.insert(evidence_ids.end(), node_evidence_ids.begin(),
                          node_evidence_ids.end());
      if (transaction) {
        transactions_.finalize(*transaction, node_evidence_ids);
      }
      const ExecutionRecord record = {
          .plan_id = plan.object_id(),
          .node_id = node_id,
          .algorithm_id = algorithm.algorithm_id(),
          .executor = algorithm.implementation_kind,
          .inputs_hash = contracts::sha256_json(inputs),
          .output = output,
          .status = skipped ? "SKIPPED"
                            : simulated ? "SIMULATED" : "COMPLETED",
          .usage = {{"actions", skipped ? 0 : 1}},
          .evidence_ids = node_evidence_ids,
          .started_at = started_at,
          .completed_at = ledger_support::utc_now(),
          .simulated = simulated};
      const auto execution_id = store_.register_record(
          {.object_type = "execution", .payload = to_json(record)},
          skipped ? "egcf_node_skipped"
                  : simulated ? "egcf_node_simulated" : "egcf_node_executed");
      execution_ids.push_back(execution_id);
      outputs[node_id] = output;
      if (node.at("checkpoint").get<bool>() && pause_at_checkpoint) {
        const ExecutionRecord pause_record = {
            .plan_id = plan.object_id(),
            .node_id = "__checkpoint__:" + node_id,
            .algorithm_id = "workflow.control@1",
            .executor = "egcf-engine",
            .inputs_hash = contracts::sha256_json(node_id),
            .output = {{"checkpoint_node_id", node_id}},
            .status = "PAUSED",
            .usage = {{"actions", 0}},
            .evidence_ids = node_evidence_ids,
            .started_at = ledger_support::utc_now(),
            .completed_at = ledger_support::utc_now(),
            .simulated = false};
        const auto checkpoint_id = store_.register_record(
            {.object_type = "execution", .payload = to_json(pause_record)},
            "egcf_workflow_paused");
        return {{"checkpoint_id", checkpoint_id},
                {"checkpoint_node_id", node_id},
                {"execution_ids", execution_ids},
                {"lifecycle", lifecycle.history()},
                {"lifecycle_stages", lifecycle.projection()},
                {"ok", true},
                {"plan_id", plan.object_id()},
                {"status", "PAUSED"}};
      }
    }
  } catch (const std::exception &exception) {
    std::vector<std::string> failures;
    const auto post_state = workspace_.snapshot_hash();
    for (auto iterator = applied_transactions.rbegin();
         iterator != applied_transactions.rend(); ++iterator) {
      try {
        auto transaction = transactions_.load(*iterator);
        if (transaction.status == "APPLIED" ||
            transaction.status == "VERIFIED") {
          transactions_.rollback(transaction);
        }
      } catch (const std::exception &rollback_exception) {
        failures.push_back(rollback_exception.what());
      }
    }
    const RollbackRecord rollback = {
        .plan_id = plan.object_id(),
        .execution_ids = canonical_strings(execution_ids),
        .rollback_class = applied_transactions.empty() ? "none" : "exact",
        .pre_state = {{"source_snapshot_hash", plan.source_snapshot_hash}},
        .post_state = {{"source_snapshot_hash", post_state}},
        .restored_state = {{"source_snapshot_hash", workspace_.snapshot_hash()}},
        .failures = failures,
        .status = applied_transactions.empty()
                      ? "NOT_REQUIRED"
                      : failures.empty() ? "ROLLED_BACK"
                                         : "PARTIALLY_COMPENSATED",
        .created_at = ledger_support::utc_now()};
    const auto rollback_id = store_.register_record(
        {.object_type = "rollback", .payload = to_json(rollback)},
        "egcf_rollback_recorded");
    int retry_count = 0;
    for (const auto &stored : store_.list("failure")) {
      const auto failure = failure_from_json(stored.payload);
      if (failure.subject_id == plan.object_id() &&
          failure.active_dimension == "execution") {
        retry_count = std::max(retry_count, failure.retry_count + 1);
      }
    }
    const std::string observed = exception.what();
    const auto categories = failure_categories(observed);
    const FailureRecord failure = {
        .subject_id = plan.object_id(),
        .expected = "all authorized plan nodes execute and verify",
        .observed = observed,
        .active_dimension = categories.front(),
        .frozen_dimensions = {"algorithm digests", "authority", "plan hash"},
        .evidence_ids = canonical_strings(evidence_ids),
        .retry_count = retry_count,
        .status = "FAILED",
        .created_at = ledger_support::utc_now()};
    const auto failure_id = store_.register_record(
        {.object_type = "failure", .payload = to_json(failure)},
        "egcf_execution_failed");
    static_cast<void>(lifecycle.transition("FAILED"));
    engine_error("plan failed; failure_id=" + failure_id +
                 "; rollback_id=" + rollback_id + ": " + observed);
  }

  static_cast<void>(lifecycle.transition("VERIFYING"));
  static_cast<void>(lifecycle.transition("COMPLETED"));
  const bool fully_simulated = !execution_ids.empty() &&
                               std::ranges::all_of(
                                   execution_ids, [this](const auto &id) {
                                     return execution_from_json(
                                                store_.get(id).payload)
                                         .simulated;
                                   });
  const ExecutionRecord workflow_record = {
      .plan_id = plan.object_id(),
      .node_id = "__workflow__",
      .algorithm_id = "workflow.lifecycle@1",
      .executor = "egcf-engine",
      .inputs_hash = contracts::sha256_json(plan.node_order),
      .output = {{"node_execution_ids", execution_ids}},
      .status = fully_simulated ? "SIMULATED" : "COMPLETED",
      .usage = {{"actions", execution_ids.size()}},
      .evidence_ids = canonical_strings(evidence_ids),
      .started_at = workflow_started_at,
      .completed_at = ledger_support::utc_now(),
      .simulated = fully_simulated};
  const auto workflow_execution_id = store_.register_record(
      {.object_type = "execution", .payload = to_json(workflow_record)},
      "egcf_workflow_completed");
  return {{"approval_id", approval_id},
          {"execution_ids", execution_ids},
          {"ok", true},
          {"outputs", [&outputs, &plan] {
             Json result = Json::array();
             for (const auto &node_id : plan.node_order) {
               result.push_back(outputs.at(node_id));
             }
             return result;
           }()},
          {"plan_id", plan.object_id()},
          {"status", fully_simulated ? "SIMULATED" : "COMPLETED"},
          {"lifecycle", lifecycle.history()},
          {"lifecycle_stages", lifecycle.projection()},
          {"workflow_execution_id", workflow_execution_id}};
}

Json EgcfEngine::verify_plan(std::string_view plan_id) {
  const auto plan = load_plan(plan_id);
  const auto compiled = load_compiled(plan);
  validate_plan(plan, compiled, false);
  const auto nodes = compiled_nodes_by_id(compiled);
  std::map<std::string, ExecutionRecord> latest;
  std::vector<std::string> workflow_execution_ids;
  for (const auto &stored : store_.list("execution")) {
    const auto record = execution_from_json(stored.payload);
    if (record.plan_id != plan.object_id()) {
      continue;
    }
    if (record.node_id == "__workflow__") {
      if (record.status == "COMPLETED" || record.status == "SIMULATED") {
        workflow_execution_ids.push_back(stored.object_id);
      }
      continue;
    }
    if (!nodes.contains(record.node_id)) {
      continue;
    }
    const auto found = latest.find(record.node_id);
    if (found == latest.end() || found->second.completed_at < record.completed_at) {
      latest[record.node_id] = record;
    }
  }
  Json checks = Json::array();
  std::vector<std::string> evidence_ids;
  bool ok = !workflow_execution_ids.empty();
  checks.push_back({{"check", "workflow_terminal_record"},
                    {"ok", !workflow_execution_ids.empty()}});
  for (const auto &node_id : plan.node_order) {
    const auto found = latest.find(node_id);
    const bool completed =
        found != latest.end() &&
        (found->second.status == "COMPLETED" ||
         found->second.status == "SIMULATED" ||
         found->second.status == "SKIPPED");
    ok = ok && completed;
    Json check = {{"check", "node_execution"},
                  {"node_id", node_id},
                  {"ok", completed}};
    if (completed) {
      const auto &definition = commands_.resolve_exact(
          nodes.at(node_id).at("command_id").get<std::string>());
      store_.objects().schemas().validate_json_value(
          definition.output_schema, found->second.output, "$verification");
      for (const auto &evidence_id : found->second.evidence_ids) {
        const auto evidence_record = store_.get(evidence_id);
        if (evidence_record.object_type != "egcf-evidence") {
          engine_error("execution references non-evidence object");
        }
        evidence_ids.push_back(evidence_id);
      }
      check["execution_status"] = found->second.status;
      check["evidence_count"] = found->second.evidence_ids.size();
      check["ok"] = check.at("ok").get<bool>() &&
                    !found->second.evidence_ids.empty();
      ok = ok && check.at("ok").get<bool>();
    }
    checks.push_back(std::move(check));
  }
  const Json verification = {
      {"checks", checks},
      {"current_snapshot_hash", workspace_.snapshot_hash()},
      {"evidence_ids", canonical_strings(evidence_ids)},
      {"graph_hash", plan.graph_hash},
      {"ok", ok},
      {"plan_id", plan.object_id()},
      {"source_snapshot_hash", plan.source_snapshot_hash},
      {"status", ok ? "VERIFIED" : "FAILED"},
      {"workflow_execution_ids", canonical_strings(workflow_execution_ids)}};
  const auto evidence_id = evidence_.collect(
      {.subject_id = plan.object_id(),
       .content = verification,
       .category = "test",
       .producer = "deterministic-statewright-egcf",
       .method = "post-action plan verification",
       .source_snapshot_hash = plan.source_snapshot_hash,
       .target = plan.object_id(),
       .oracle = "statewright.egcf.verify_plan",
       .environment = {{"engine", "statewright-cpp"}},
       .command_id = "workflow.execute@1",
       .algorithm_id = "workflow.lifecycle@1",
       .requirement_ids = {},
       .success = ok,
       .limitations = {},
       .independence_group = "plan-verification:" + plan.object_id(),
       .simulated = false});
  auto result = verification;
  result["verification_evidence_id"] = evidence_id;
  return result;
}

Json EgcfEngine::rollback_plan(std::string_view plan_id) {
  const auto plan = load_plan(plan_id);
  const auto compiled = load_compiled(plan);
  validate_plan(plan, compiled, false);
  const auto post_state = workspace_.snapshot_hash();
  std::vector<std::string> transaction_ids;
  std::vector<std::string> failures;
  for (auto iterator = plan.node_order.rbegin(); iterator != plan.node_order.rend();
       ++iterator) {
    const auto node = plan.rollback_graph.find(*iterator);
    if (node == plan.rollback_graph.end() ||
        !node->contains("prepared")) {
      continue;
    }
    const auto transaction_id =
        node->at("prepared").at("transaction_id").get<std::string>();
    try {
      auto transaction = transactions_.load(transaction_id);
      if (transaction.status == "APPLIED" || transaction.status == "VERIFIED") {
        transactions_.rollback(transaction);
        transaction_ids.push_back(transaction_id);
      } else if (transaction.status != "ROLLED_BACK") {
        engine_error("transaction is not rollback eligible: " + transaction_id);
      }
    } catch (const std::exception &exception) {
      failures.push_back(exception.what());
    }
  }
  std::vector<std::string> execution_ids;
  for (const auto &stored : store_.list("execution")) {
    const auto execution = execution_from_json(stored.payload);
    if (execution.plan_id == plan.object_id()) {
      execution_ids.push_back(stored.object_id);
    }
  }
  const auto restored = workspace_.snapshot_hash();
  if (restored != plan.source_snapshot_hash) {
    failures.push_back("workspace snapshot was not restored exactly");
  }
  const RollbackRecord record = {
      .plan_id = plan.object_id(),
      .execution_ids = canonical_strings(std::move(execution_ids)),
      .rollback_class = "exact",
      .pre_state = {{"source_snapshot_hash", plan.source_snapshot_hash}},
      .post_state = {{"source_snapshot_hash", post_state}},
      .restored_state = {{"source_snapshot_hash", restored}},
      .failures = failures,
      .status = failures.empty() ? "ROLLED_BACK" : "PARTIALLY_COMPENSATED",
      .created_at = ledger_support::utc_now()};
  const auto rollback_id = store_.register_record(
      {.object_type = "rollback", .payload = to_json(record)},
      "egcf_rollback_recorded");
  return {{"failures", failures},
          {"ok", failures.empty()},
          {"plan_id", plan.object_id()},
          {"restored_snapshot_hash", restored},
          {"rollback_id", rollback_id},
          {"status", record.status},
          {"transaction_ids", transaction_ids}};
}

Json EgcfEngine::invoke(std::string_view command_id, Json inputs,
                        const CommandContext &context) {
  if (!inputs.is_object()) {
    engine_error("command inputs must be an object");
  }
  const auto &definition = commands_.resolve_for_discovery(command_id);
  WorkflowDefinition workflow = {
      .name = "invoke-" + definition.namespace_name + "-" + definition.name,
      .version = definition.version,
      .parameters = Json::object(),
      .nodes = {{.node_id = "invoke",
                 .command_id = definition.command_id(),
                 .inputs = std::move(inputs),
                 .depends_on = {},
                 .when = Json::object(),
                 .retry_limit = 0,
                 .checkpoint = false}},
      .outputs = Json::object(),
      .description = "Canonical single-command invocation"};
  const auto compiled = compile(workflow, context);
  const auto plan = create_execution_plan(
      compiled, compiled.capability_level == "C3" && !context.simulate &&
                    !context.dry_run);
  Json projection = {{"compiled_workflow_id", compiled.object_id()},
                     {"execution_plan_id", plan.object_id()},
                     {"graph_hash", compiled.graph_hash},
                     {"ok", true},
                     {"source_snapshot_hash", compiled.source_snapshot_hash},
                     {"status", "COMPILED"}};
  if (context.dry_run) {
    return projection;
  }
  if (compiled.capability_level == "C2" && !context.simulate &&
      definition.namespace_name != "simulate") {
    engine_error("C2 commands require simulation authorization");
  }
  if (compiled.approval_policy == "human" ||
      compiled.approval_policy == "quorum") {
    projection["approval_required"] = compiled.approval_policy;
    projection["status"] = "AWAITING_APPROVAL";
    return projection;
  }
  const auto execution = execute_plan(plan.object_id());
  for (const auto &[key, value] : execution.items()) {
    projection[key] = value;
  }
  return projection;
}

Json EgcfEngine::replay(std::string_view plan_id,
                        const CommandContext &context) {
  const auto historical = load_plan(plan_id);
  const auto compiled = load_compiled(historical);
  std::vector<WorkflowNode> nodes;
  for (const auto &node : compiled.nodes) {
    nodes.push_back({.node_id = node.at("node_id"),
                     .command_id = node.at("command_id"),
                     .inputs = node.at("inputs"),
                     .depends_on = node.at("depends_on"),
                     .when = node.at("when"),
                     .retry_limit = node.at("retry_limit"),
                     .checkpoint = node.at("checkpoint")});
  }
  const auto separator = compiled.workflow_id.rfind('@');
  if (separator == std::string::npos) {
    engine_error("historical workflow identity is invalid");
  }
  const WorkflowDefinition workflow = {
      .name = compiled.workflow_id.substr(0, separator),
      .version = std::stoi(compiled.workflow_id.substr(separator + 1U)),
      .parameters = Json::object(),
      .nodes = std::move(nodes),
      .outputs = Json::object(),
      .description = "Recompiled historical EGCF plan"};
  auto replay_context = context;
  replay_context.replay = std::string(plan_id);
  const auto replayed = compile(workflow, replay_context);
  const auto plan = create_execution_plan(
      replayed,
      replayed.capability_level == "C3" && !replay_context.simulate &&
          !replay_context.dry_run);
  return {{"current_snapshot", workspace_.snapshot_hash()},
          {"historical_graph_hash", historical.graph_hash},
          {"historical_plan_id", historical.object_id()},
          {"historical_snapshot", historical.source_snapshot_hash},
          {"reauthorization_required",
           replayed.capability_level == "C3" ||
               replayed.capability_level == "C4" ||
               replayed.capability_level == "C5"},
          {"replayed_graph_hash", replayed.graph_hash},
          {"replayed_plan_id", plan.object_id()},
          {"same_graph", historical.graph_hash == replayed.graph_hash},
          {"same_snapshot",
           historical.source_snapshot_hash == workspace_.snapshot_hash()}};
}

} // namespace statewright::egcf
