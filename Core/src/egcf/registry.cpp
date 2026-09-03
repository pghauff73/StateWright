#include "statewright/egcf/registry.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/contracts/typed_id.hpp"

#include <algorithm>
#include <fstream>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace statewright::egcf {
namespace {

using Json = contracts::Json;

[[noreturn]] void egcf_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied, std::move(message));
}

[[nodiscard]] Json read_json(const std::filesystem::path &path) {
  std::ifstream input(path);
  if (!input.is_open()) {
    egcf_error("cannot open EGCF resource: " + path.string());
  }
  try {
    return Json::parse(input);
  } catch (const Json::exception &error) {
    egcf_error("invalid EGCF JSON resource " + path.string() + ": " +
               error.what());
  }
}

[[nodiscard]] std::vector<std::string> strings(const Json &value,
                                               std::string_view label) {
  if (!value.is_array()) {
    egcf_error(std::string(label) + " must be an array");
  }
  std::vector<std::string> result;
  for (const auto &item : value) {
    if (!item.is_string()) {
      egcf_error(std::string(label) + " entries must be strings");
    }
    result.push_back(item.get<std::string>());
  }
  return result;
}

void validate_definition(const CommandDefinition &definition) {
  if (definition.namespace_name.empty() || definition.name.empty() ||
      definition.version < 1) {
    egcf_error("EGCF command definition identity is invalid");
  }
  if (!definition.input_schema.is_object() ||
      !definition.output_schema.is_object() ||
      !definition.capability_query.is_object() ||
      !definition.algorithm_query.is_object()) {
    egcf_error("EGCF command definition schemas and queries must be objects");
  }
  if (!definition.capability_query.contains("level") ||
      !definition.capability_query.contains("facets")) {
    egcf_error("EGCF command definition lacks capability requirements");
  }
  if (definition.risk_policy.empty() || definition.rollback_policy.empty() ||
      definition.approval_policy.empty()) {
    egcf_error("EGCF command definition policy fields must be explicit");
  }
}

[[nodiscard]] bool exact_sha256(std::string_view value) {
  return value.size() == 64U &&
         std::ranges::all_of(value, [](unsigned char character) {
           return std::isdigit(character) != 0 ||
                  (character >= 'a' && character <= 'f') ||
                  (character >= 'A' && character <= 'F');
         });
}

[[nodiscard]] std::vector<std::string>
canonical_strings(std::vector<std::string> values) {
  std::ranges::sort(values);
  values.erase(std::unique(values.begin(), values.end()), values.end());
  return values;
}

void validate_algorithm(const AlgorithmDefinition &definition,
                        bool trusted_core) {
  if (definition.name.empty() || definition.version < 1 ||
      definition.implementation_ref.empty() ||
      !exact_sha256(definition.implementation_digest) ||
      definition.command_ids.empty() || definition.capability_level.empty() ||
      definition.risk_floor.empty() || definition.rollback_class.empty() ||
      definition.owner.empty()) {
    egcf_error("EGCF algorithm definition is incomplete");
  }
  const bool public_kind = definition.implementation_kind == "builtin" ||
                           definition.implementation_kind == "reference";
  const bool core_kind = definition.implementation_kind == "eon" ||
                         definition.implementation_kind == "engine-control" ||
                         definition.implementation_kind == "simulation";
  if ((!trusted_core && !public_kind) ||
      (trusted_core && (!core_kind && !public_kind))) {
    egcf_error(
        "non-core algorithm proposals cannot select privileged executor kinds");
  }
  if (trusted_core && definition.owner != "egcf-core") {
    egcf_error("privileged executor bootstrap requires egcf-core ownership");
  }
  if (definition.implementation_kind == "reference" &&
      definition.status != "PROPOSED" && definition.status != "CANDIDATE") {
    egcf_error("external algorithm references cannot self-qualify");
  }
  const std::string lowered = [&] {
    std::string value = definition.implementation_ref;
    std::ranges::transform(value, value.begin(), [](unsigned char character) {
      return static_cast<char>(std::tolower(character));
    });
    return value;
  }();
  for (const std::string_view marker : {"shell", "subprocess", "callback",
                                        "callable", "exec(", "eval("}) {
    if (lowered.find(marker) != std::string::npos) {
      egcf_error(
          "algorithm implementation reference contains a forbidden executor "
          "marker");
    }
  }
}

void validate_qualification(QualificationRecord &record) {
  if (record.algorithm_id.empty() || !exact_sha256(record.algorithm_digest) ||
      !record.context.is_object() || !record.tests.is_array() ||
      !record.benchmarks.is_array() || record.status.empty() ||
      record.qualified_by.empty() || record.created_at.empty()) {
    egcf_error("EGCF qualification record is incomplete");
  }
  const std::string expected_context_hash =
      contracts::sha256_json(record.context);
  if (!record.context_hash.empty() &&
      record.context_hash != expected_context_hash) {
    egcf_error("EGCF qualification context hash mismatch");
  }
  record.context_hash = expected_context_hash;
  record.evidence_ids = canonical_strings(std::move(record.evidence_ids));
  record.known_failures = canonical_strings(std::move(record.known_failures));
}

[[nodiscard]] int capability_rank(std::string_view level) {
  static const std::map<std::string, int> ranks = {
      {"C0", 0}, {"C1", 1}, {"C2", 2},
      {"C3", 3}, {"C4", 4}, {"C5", 5}};
  const auto iterator = ranks.find(std::string(level));
  if (iterator == ranks.end()) {
    egcf_error("unknown EGCF capability level: " + std::string(level));
  }
  return iterator->second;
}

[[nodiscard]] bool context_matches(const Json &required_context,
                                   const Json &observed_context) {
  for (const auto &[key, expected] : required_context.items()) {
    const auto iterator = observed_context.find(key);
    if (iterator == observed_context.end() || *iterator != expected) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool expired(std::string_view expires_at,
                           std::string_view decision_time) {
  if (expires_at.empty()) {
    return false;
  }
  if (decision_time.empty()) {
    egcf_error("EGCF selection requires an explicit decision timestamp");
  }
  return expires_at <= decision_time;
}

[[nodiscard]] std::pair<std::string, int>
version_key(std::string_view identifier) {
  const auto separator = identifier.rfind('@');
  if (separator == std::string_view::npos) {
    return {std::string(identifier), 0};
  }
  const std::string version_text(identifier.substr(separator + 1U));
  try {
    std::size_t consumed = 0;
    const int version = std::stoi(version_text, &consumed);
    if (consumed != version_text.size()) {
      return {std::string(identifier), 0};
    }
    return {std::string(identifier.substr(0, separator)), version};
  } catch (const std::exception &) {
    return {std::string(identifier), 0};
  }
}

} // namespace

std::string CommandDefinition::command_id() const {
  return namespace_name + "." + name + "@" + std::to_string(version);
}

std::string CommandDefinition::object_id() const {
  return contracts::typed_id("command-definition", to_json(*this));
}

Json to_json(const CommandDefinition &definition) {
  return {{"algorithm_query", definition.algorithm_query},
          {"aliases", definition.aliases},
          {"approval_policy", definition.approval_policy},
          {"budget_policy", definition.budget_policy},
          {"capability_query", definition.capability_query},
          {"description", definition.description},
          {"evidence_requirements", definition.evidence_requirements},
          {"input_schema", definition.input_schema},
          {"intent_kinds", definition.intent_kinds},
          {"invariants", definition.invariants},
          {"lifecycle_policy", definition.lifecycle_policy},
          {"name", definition.name},
          {"namespace", definition.namespace_name},
          {"output_schema", definition.output_schema},
          {"postconditions", definition.postconditions},
          {"preconditions", definition.preconditions},
          {"risk_policy", definition.risk_policy},
          {"rollback_policy", definition.rollback_policy},
          {"version", definition.version}};
}

Json to_json(const ResourceReceipt &receipt) {
  return {{"manifest_hash", receipt.manifest_hash},
          {"resource_root", receipt.resource_root.generic_string()},
          {"verified_files", receipt.verified_files},
          {"verified_paths", receipt.verified_paths}};
}

std::string AlgorithmDefinition::algorithm_id() const {
  return name + "@" + std::to_string(version);
}

std::string AlgorithmDefinition::object_id() const {
  return contracts::typed_id("algorithm-definition", to_json(*this));
}

std::string QualificationRecord::object_id() const {
  return contracts::typed_id("qualification", to_json(*this));
}

std::string SelectionDecision::object_id() const {
  return contracts::typed_id("selection-decision", to_json(*this));
}

Json to_json(const AlgorithmDefinition &definition) {
  return {{"applicability", definition.applicability},
          {"capability_level", definition.capability_level},
          {"capability_requirements", definition.capability_requirements},
          {"command_ids", definition.command_ids},
          {"evidence_requirements", definition.evidence_requirements},
          {"implementation_digest", definition.implementation_digest},
          {"implementation_kind", definition.implementation_kind},
          {"implementation_ref", definition.implementation_ref},
          {"input_schema", definition.input_schema},
          {"invariants", definition.invariants},
          {"known_failures", definition.known_failures},
          {"name", definition.name},
          {"output_schema", definition.output_schema},
          {"owner", definition.owner},
          {"provenance", definition.provenance},
          {"qualification_policy", definition.qualification_policy},
          {"risk_floor", definition.risk_floor},
          {"rollback_class", definition.rollback_class},
          {"status", definition.status},
          {"version", definition.version}};
}

Json to_json(const QualificationRecord &record) {
  return {{"algorithm_digest", record.algorithm_digest},
          {"algorithm_id", record.algorithm_id},
          {"benchmarks", record.benchmarks},
          {"context", record.context},
          {"context_hash", record.context_hash},
          {"created_at", record.created_at},
          {"evidence_ids", record.evidence_ids},
          {"expires_at", record.expires_at},
          {"known_failures", record.known_failures},
          {"qualified_by", record.qualified_by},
          {"status", record.status},
          {"tests", record.tests}};
}

Json to_json(const SelectionDecision &decision) {
  return {{"candidates", decision.candidates},
          {"command_id", decision.command_id},
          {"context_hash", decision.context_hash},
          {"created_at", decision.created_at},
          {"evidence_ids", decision.evidence_ids},
          {"excluded", decision.excluded},
          {"ranking", decision.ranking},
          {"score_components", decision.score_components},
          {"selected_algorithm_digest", decision.selected_algorithm_digest},
          {"selected_algorithm_id", decision.selected_algorithm_id},
          {"tie_break", decision.tie_break}};
}

ResourceReceipt verify_resource_manifest(
    const std::filesystem::path &resource_root) {
  const auto root = std::filesystem::weakly_canonical(resource_root);
  const auto manifest = root / "manifest.sha256";
  std::ifstream input(manifest);
  if (!input.is_open()) {
    egcf_error("cannot open EGCF resource manifest: " + manifest.string());
  }
  ResourceReceipt receipt;
  receipt.resource_root = root;
  receipt.manifest_hash = contracts::sha256_file(manifest);
  std::string line;
  std::set<std::string> seen;
  while (std::getline(input, line)) {
    if (line.empty()) {
      continue;
    }
    const auto separator = line.find("  ");
    if (separator == std::string::npos) {
      egcf_error("malformed EGCF resource manifest line");
    }
    const std::string expected = line.substr(0, separator);
    std::string relative = line.substr(separator + 2U);
    if (relative.starts_with("./")) {
      relative.erase(0, 2U);
    }
    if (expected.size() != 64U || relative.empty() ||
        !seen.insert(relative).second) {
      egcf_error("invalid EGCF resource manifest entry: " + relative);
    }
    const auto path = root / relative;
    if (!std::filesystem::is_regular_file(path) ||
        contracts::sha256_file(path) != expected) {
      egcf_error("EGCF resource hash mismatch: " + relative);
    }
    receipt.verified_paths.push_back(relative);
  }
  if (!input.eof()) {
    egcf_error("cannot read EGCF resource manifest");
  }
  receipt.verified_files = receipt.verified_paths.size();
  if (receipt.verified_files == 0U) {
    egcf_error("EGCF resource manifest is empty");
  }
  return receipt;
}

CommandRegistry::CommandRegistry(std::filesystem::path resource_root)
    : resource_receipt_(verify_resource_manifest(resource_root)) {
  const auto catalog =
      read_json(resource_receipt_.resource_root / "commands/v1/catalog.json");
  const auto contracts_resource =
      read_json(resource_receipt_.resource_root / "commands/v1/contracts.json");
  if (catalog.value("schema_version", 0) != 1 ||
      contracts_resource.value("schema_version", 0) != 1 ||
      !catalog.contains("namespaces") ||
      !contracts_resource.contains("commands") ||
      !contracts_resource.contains("input_field_schemas") ||
      !contracts_resource.contains("output_envelope_schema")) {
    egcf_error("unsupported or incomplete EGCF command resources");
  }
  const auto &command_contracts = contracts_resource.at("commands");
  const auto &input_fields = contracts_resource.at("input_field_schemas");
  const auto &output_schema = contracts_resource.at("output_envelope_schema");
  if (!catalog.at("namespaces").is_object() || !command_contracts.is_object() ||
      !input_fields.is_object() || !output_schema.is_object()) {
    egcf_error("EGCF command resource shape is invalid");
  }

  for (const auto &[namespace_name, verbs] : catalog.at("namespaces").items()) {
    for (const auto &verb_value : verbs) {
      const std::string verb = verb_value.get<std::string>();
      const std::string base = namespace_name + "." + verb;
      const std::string command_id = base + "@1";
      const auto iterator = command_contracts.find(command_id);
      if (iterator == command_contracts.end() || !iterator->is_object()) {
        egcf_error("missing EGCF command contract: " + command_id);
      }
      const auto &contract = *iterator;
      const auto &settings = contract.at("settings");
      const std::string level = settings.at("level").get<std::string>();
      CommandDefinition definition{
          .namespace_name = namespace_name,
          .name = verb,
          .version = 1,
          .intent_kinds = {base, namespace_name},
          .input_schema = {{"additionalProperties", false},
                           {"properties", input_fields},
                           {"required", contract.at("required_inputs")},
                           {"type", "object"}},
          .output_schema = output_schema,
          .preconditions = strings(contract.at("preconditions"),
                                   "EGCF command preconditions"),
          .postconditions = strings(contract.at("postconditions"),
                                    "EGCF command postconditions"),
          .invariants =
              strings(contract.at("invariants"), "EGCF command invariants"),
          .evidence_requirements = strings(
              contract.at("evidence_requirements"),
              "EGCF command evidence requirements"),
          .capability_query = {{"facets", settings.at("facets")},
                               {"level", level}},
          .algorithm_query = {{"command_id", command_id}},
          .risk_policy = settings.at("risk").get<std::string>(),
          .rollback_policy = settings.at("rollback").get<std::string>(),
          .budget_policy = {{"actions", 1}, {"retries", 0}},
          .approval_policy = settings.at("approval").get<std::string>(),
          .lifecycle_policy =
              {{"compressible", level == "C0" || level == "C1"}},
          .description = "EGCF semantic command " + base,
          .aliases = strings(contract.at("aliases"), "EGCF command aliases")};
      validate_definition(definition);
      const std::size_t index = definitions_.size();
      if (!exact_index_.emplace(command_id, index).second) {
        egcf_error("duplicate EGCF command ID: " + command_id);
      }
      for (const auto &alias : definition.aliases) {
        if (!aliases_.emplace(alias, command_id).second) {
          egcf_error("duplicate EGCF command alias: " + alias);
        }
      }
      definitions_.push_back(std::move(definition));
    }
  }
  if (definitions_.size() != command_contracts.size()) {
    egcf_error("EGCF command catalog and contracts disagree");
  }
}

const ResourceReceipt &CommandRegistry::resource_receipt() const noexcept {
  return resource_receipt_;
}

const std::vector<CommandDefinition> &
CommandRegistry::definitions() const noexcept {
  return definitions_;
}

const CommandDefinition &
CommandRegistry::resolve_exact(std::string_view identifier) const {
  std::string resolved(identifier);
  const auto alias = aliases_.find(resolved);
  if (alias != aliases_.end()) {
    resolved = alias->second;
  }
  const auto [base, version] = version_key(resolved);
  static_cast<void>(base);
  if (version < 1) {
    egcf_error("EGCF execution requires an exact command version: " + resolved);
  }
  const auto iterator = exact_index_.find(resolved);
  if (iterator == exact_index_.end()) {
    egcf_error("unknown EGCF command version: " + resolved);
  }
  return definitions_.at(iterator->second);
}

const CommandDefinition &
CommandRegistry::resolve_for_discovery(std::string_view identifier) const {
  std::string resolved(identifier);
  const auto alias = aliases_.find(resolved);
  if (alias != aliases_.end()) {
    resolved = alias->second;
  }
  const auto exact = exact_index_.find(resolved);
  if (exact != exact_index_.end()) {
    return definitions_.at(exact->second);
  }
  const auto [base, version] = version_key(resolved);
  if (version > 0) {
    egcf_error("unknown EGCF command version: " + resolved);
  }
  const CommandDefinition *selected = nullptr;
  for (const auto &definition : definitions_) {
    if (definition.namespace_name + "." + definition.name == base &&
        (selected == nullptr || definition.version > selected->version)) {
      selected = &definition;
    }
  }
  if (selected == nullptr) {
    egcf_error("unknown EGCF command or alias: " + resolved);
  }
  return *selected;
}

Json CommandRegistry::describe(std::string_view identifier) const {
  const auto &definition = resolve_for_discovery(identifier);
  auto result = to_json(definition);
  result["command_id"] = definition.command_id();
  result["object_id"] = definition.object_id();
  return result;
}

AlgorithmRegistry::AlgorithmRegistry(const CommandRegistry &commands)
    : commands_(commands) {}

std::string
AlgorithmRegistry::register_algorithm(AlgorithmDefinition definition) {
  return register_algorithm_impl(std::move(definition), false);
}

std::string
AlgorithmRegistry::register_core_algorithm(AlgorithmDefinition definition) {
  return register_algorithm_impl(std::move(definition), true);
}

std::string AlgorithmRegistry::register_algorithm_impl(
    AlgorithmDefinition definition, bool trusted_core) {
  validate_algorithm(definition, trusted_core);
  definition.command_ids = canonical_strings(std::move(definition.command_ids));
  definition.capability_requirements =
      canonical_strings(std::move(definition.capability_requirements));
  definition.invariants = canonical_strings(std::move(definition.invariants));
  definition.evidence_requirements =
      canonical_strings(std::move(definition.evidence_requirements));
  definition.known_failures =
      canonical_strings(std::move(definition.known_failures));
  for (const auto &command_id : definition.command_ids) {
    static_cast<void>(commands_.resolve_exact(command_id));
  }
  const std::string object_id = definition.object_id();
  const auto existing = object_index_.find(object_id);
  if (existing != object_index_.end()) {
    return object_id;
  }
  object_index_[object_id] = algorithms_.size();
  algorithms_.push_back(std::move(definition));
  return object_id;
}

std::string AlgorithmRegistry::register_qualification(
    QualificationRecord qualification) {
  validate_qualification(qualification);
  const auto &algorithm = resolve_exact(qualification.algorithm_id);
  if (algorithm.implementation_digest != qualification.algorithm_digest) {
    egcf_error("EGCF qualification references a stale algorithm digest");
  }
  const std::string object_id = qualification.object_id();
  if (qualification_index_.contains(object_id)) {
    return object_id;
  }
  qualification_index_[object_id] = qualifications_.size();
  qualifications_.push_back(std::move(qualification));
  return object_id;
}

const std::vector<AlgorithmDefinition> &
AlgorithmRegistry::algorithms() const noexcept {
  return algorithms_;
}

std::vector<AlgorithmDefinition>
AlgorithmRegistry::search(std::string_view exact_command_id) const {
  static_cast<void>(commands_.resolve_exact(exact_command_id));
  std::vector<AlgorithmDefinition> result;
  for (const auto &algorithm : algorithms_) {
    if (std::ranges::find(algorithm.command_ids, exact_command_id) !=
        algorithm.command_ids.end()) {
      result.push_back(algorithm);
    }
  }
  std::ranges::sort(result, [](const auto &left, const auto &right) {
    return std::tie(left.name, left.version, left.implementation_digest) <
           std::tie(right.name, right.version, right.implementation_digest);
  });
  return result;
}

const AlgorithmDefinition &
AlgorithmRegistry::resolve_exact(std::string_view algorithm_id) const {
  const auto [base, version] = version_key(algorithm_id);
  static_cast<void>(base);
  if (version < 1) {
    egcf_error("EGCF execution requires an exact algorithm version: " +
               std::string(algorithm_id));
  }
  const AlgorithmDefinition *selected = nullptr;
  for (const auto &algorithm : algorithms_) {
    if (algorithm.algorithm_id() == algorithm_id) {
      selected = &algorithm;
    }
  }
  if (selected == nullptr) {
    egcf_error("unknown EGCF algorithm version: " +
               std::string(algorithm_id));
  }
  return *selected;
}

std::vector<QualificationRecord>
AlgorithmRegistry::qualifications(const AlgorithmDefinition &algorithm) const {
  std::vector<QualificationRecord> result;
  for (const auto &record : qualifications_) {
    if (record.algorithm_id == algorithm.algorithm_id() &&
        record.algorithm_digest == algorithm.implementation_digest) {
      result.push_back(record);
    }
  }
  return result;
}

SelectionEngine::SelectionEngine(const AlgorithmRegistry &algorithms)
    : algorithms_(algorithms) {}

SelectionDecision SelectionEngine::select(
    std::string exact_command_id, Json context, std::string capability_ceiling,
    std::vector<std::string> allowed_capabilities,
    std::vector<std::string> invariant_names, std::string decision_time) const {
  if (!context.is_object()) {
    egcf_error("EGCF selection context must be an object");
  }
  const int ceiling = capability_rank(capability_ceiling);
  const std::set<std::string> allowed(allowed_capabilities.begin(),
                                     allowed_capabilities.end());
  const std::set<std::string> active_invariants(invariant_names.begin(),
                                               invariant_names.end());
  struct Candidate final {
    Json payload;
    int qualification_strength = 0;
    int invariant_compatibility = 0;
    int expected_correctness = 0;
    int rollback_quality = 0;
    int evidence_freshness = 0;
    int performance_fit = 0;
    int resource_cost = 0;
    int known_failure_count = 0;
    std::string algorithm_id;
    std::string digest;
  };
  const std::map<std::string, int> rollback_scores = {
      {"exact", 3},       {"compensating", 2}, {"best_effort", 1},
      {"none", 0},        {"irreversible", -1}};
  std::vector<Candidate> candidates;
  Json excluded = Json::array();
  for (const auto &algorithm : algorithms_.search(exact_command_id)) {
    std::vector<std::string> reasons;
    if (algorithm.status == "RETIRED" || algorithm.status == "DEPRECATED" ||
        algorithm.status == "PROPOSED") {
      reasons.push_back("status=" + algorithm.status);
    }
    if (capability_rank(algorithm.capability_level) > ceiling) {
      reasons.push_back("capability ceiling exceeded");
    }
    std::vector<std::string> missing_capabilities;
    for (const auto &capability : algorithm.capability_requirements) {
      if (!allowed.contains(capability)) {
        missing_capabilities.push_back(capability);
      }
    }
    if (!missing_capabilities.empty()) {
      reasons.push_back("missing capabilities: " +
                        contracts::canonical_json(missing_capabilities));
    }
    std::vector<std::string> context_mismatch;
    for (const auto &[key, expected] : algorithm.applicability.items()) {
      const auto iterator = context.find(key);
      if (iterator == context.end() || *iterator != expected) {
        context_mismatch.push_back(key);
      }
    }
    if (!context_mismatch.empty()) {
      reasons.push_back("context mismatch: " +
                        contracts::canonical_json(context_mismatch));
    }
    std::vector<std::string> missing_invariants;
    for (const auto &invariant : algorithm.invariants) {
      if (!active_invariants.contains(invariant)) {
        missing_invariants.push_back(invariant);
      }
    }
    if (!active_invariants.empty() && !missing_invariants.empty()) {
      reasons.push_back("invariant mismatch: " +
                        contracts::canonical_json(missing_invariants));
    }
    std::vector<QualificationRecord> qualifications;
    for (const auto &record : algorithms_.qualifications(algorithm)) {
      if (record.status == "QUALIFIED" &&
          !expired(record.expires_at, decision_time) &&
          context_matches(record.context, context)) {
        qualifications.push_back(record);
      }
    }
    if (qualifications.empty()) {
      reasons.push_back("no current qualification");
    }
    int passed_tests = 0;
    int benchmark_strength = 0;
    std::vector<std::string> qualification_ids;
    for (const auto &qualification : qualifications) {
      qualification_ids.push_back(qualification.object_id());
      benchmark_strength += static_cast<int>(qualification.benchmarks.size());
      for (const auto &test : qualification.tests) {
        if (test.is_object() && test.value("success", false)) {
          ++passed_tests;
        }
      }
    }
    const int rollback_quality =
        rollback_scores.contains(algorithm.rollback_class)
            ? rollback_scores.at(algorithm.rollback_class)
            : -2;
    const int resource_cost =
        algorithm.applicability.value("resource_cost", 0);
    const Json score_components = {
        {"deterministic_performance_fit", benchmark_strength},
        {"evidence_freshness", qualifications.empty() ? 0 : 1},
        {"expected_correctness", passed_tests},
        {"invariant_compatibility", missing_invariants.empty() ? 1 : 0},
        {"known_failure_count",
         static_cast<int>(algorithm.known_failures.size())},
        {"qualification_strength",
         static_cast<int>(qualification_ids.size())},
        {"resource_cost", resource_cost},
        {"rollback_quality", rollback_quality}};
    Json item = {{"algorithm_digest", algorithm.implementation_digest},
                 {"algorithm_id", algorithm.algorithm_id()},
                 {"known_failures", algorithm.known_failures},
                 {"qualification_ids", qualification_ids},
                 {"rollback_class", algorithm.rollback_class},
                 {"score_components", score_components},
                 {"status", algorithm.status}};
    if (!reasons.empty()) {
      item["reasons"] = reasons;
      excluded.push_back(std::move(item));
    } else {
      candidates.push_back(
          {.payload = std::move(item),
           .qualification_strength =
               static_cast<int>(qualification_ids.size()),
           .invariant_compatibility = missing_invariants.empty() ? 1 : 0,
           .expected_correctness = passed_tests,
           .rollback_quality = rollback_quality,
           .evidence_freshness = qualifications.empty() ? 0 : 1,
           .performance_fit = benchmark_strength,
           .resource_cost = resource_cost,
           .known_failure_count =
               static_cast<int>(algorithm.known_failures.size()),
           .algorithm_id = algorithm.algorithm_id(),
           .digest = algorithm.implementation_digest});
    }
  }
  if (candidates.empty()) {
    egcf_error("no qualified algorithm for " + exact_command_id + ": " +
               contracts::canonical_json(excluded));
  }
  std::ranges::sort(candidates, [](const Candidate &left,
                                   const Candidate &right) {
    return std::tuple(-left.qualification_strength,
                      -left.invariant_compatibility,
                      -left.expected_correctness, -left.rollback_quality,
                      -left.evidence_freshness, -left.performance_fit,
                      left.resource_cost, left.known_failure_count,
                      left.algorithm_id, left.digest) <
           std::tuple(-right.qualification_strength,
                      -right.invariant_compatibility,
                      -right.expected_correctness, -right.rollback_quality,
                      -right.evidence_freshness, -right.performance_fit,
                      right.resource_cost, right.known_failure_count,
                      right.algorithm_id, right.digest);
  });
  Json candidate_payloads = Json::array();
  Json vectors = Json::object();
  for (const auto &candidate : candidates) {
    candidate_payloads.push_back(candidate.payload);
    vectors[candidate.algorithm_id] = candidate.payload.at("score_components");
  }
  const auto &selected = candidates.front();
  return {.command_id = std::move(exact_command_id),
          .context_hash = contracts::sha256_json(context),
          .candidates = std::move(candidate_payloads),
          .excluded = std::move(excluded),
          .selected_algorithm_id = selected.algorithm_id,
          .selected_algorithm_digest = selected.digest,
          .ranking = {"qualification strength",
                      "invariant compatibility",
                      "expected correctness",
                      "rollback quality",
                      "evidence freshness",
                      "deterministic performance fit",
                      "resource cost",
                      "stable algorithm ID"},
          .tie_break = "algorithm_id then implementation_digest",
          .evidence_ids = selected.payload.at("qualification_ids")
                              .get<std::vector<std::string>>(),
          .created_at = std::move(decision_time),
          .score_components =
              {{"candidate_vectors", vectors},
               {"selected", selected.payload.at("score_components")}}};
}

} // namespace statewright::egcf
