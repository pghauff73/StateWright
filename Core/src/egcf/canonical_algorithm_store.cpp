#include "statewright/egcf/canonical_algorithm_store.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/contracts/typed_id.hpp"
#include "statewright/core/file_io.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <ctime>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <utility>

namespace statewright::egcf {
namespace {

using Json = contracts::Json;

const std::set<std::string> relation_types = {
    "EQUIVALENT_TO",       "NEAR_VARIANT_OF", "GENERALIZES",
    "SPECIALIZES",         "DERIVED_FROM",    "COMPOSED_FROM",
    "APPROXIMATES",        "BOUNDS",          "DECOUPLES",
    "REQUIRES",            "LOWER_COST_THAN", "STRONGER_EVIDENCE_THAN"};

struct DatabaseCloser final {
  void operator()(sqlite3 *database) const noexcept {
    static_cast<void>(sqlite3_close(database));
  }
};

struct StatementFinalizer final {
  void operator()(sqlite3_stmt *statement) const noexcept {
    static_cast<void>(sqlite3_finalize(statement));
  }
};

using Database = std::unique_ptr<sqlite3, DatabaseCloser>;
using Statement = std::unique_ptr<sqlite3_stmt, StatementFinalizer>;

[[noreturn]] void canonical_store_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument,
                      std::move(message));
}

[[nodiscard]] std::string trimmed(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n\f\v");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n\f\v");
  return value.substr(first, last - first + 1U);
}

[[nodiscard]] std::string normalized_text(std::string value) {
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

[[nodiscard]] std::string uppercase(std::string value) {
  value = trimmed(std::move(value));
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::toupper(character));
                 });
  return value;
}

[[nodiscard]] std::string exact_sha(std::string value,
                                    std::string_view label) {
  value = normalized_text(std::move(value));
  if (value.size() != 64U ||
      !std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return std::isdigit(character) != 0 ||
               (character >= 'a' && character <= 'f');
      })) {
    canonical_store_error(std::string(label) +
                          " must be an exact SHA-256 digest");
  }
  return value;
}

[[nodiscard]] std::string utc_now() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t value = std::chrono::system_clock::to_time_t(now);
  std::tm parts{};
  gmtime_r(&value, &parts);
  std::array<char, 32> buffer{};
  if (std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ",
                    &parts) == 0U) {
    canonical_store_error("cannot format canonical algorithm timestamp");
  }
  return buffer.data();
}

[[nodiscard]] Database open_database(const std::filesystem::path &path) {
  std::filesystem::create_directories(path.parent_path());
  sqlite3 *raw = nullptr;
  const int result = sqlite3_open_v2(path.c_str(), &raw,
                                     SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                         SQLITE_OPEN_FULLMUTEX,
                                     nullptr);
  Database database(raw);
  if (result != SQLITE_OK || !database) {
    const std::string message =
        raw == nullptr ? "unknown SQLite error" : sqlite3_errmsg(raw);
    canonical_store_error("cannot open canonical algorithm projection: " +
                          message);
  }
  sqlite3_busy_timeout(database.get(), 5'000);
  return database;
}

void execute(sqlite3 *database, std::string_view sql) {
  char *message = nullptr;
  const int result = sqlite3_exec(database, std::string(sql).c_str(), nullptr,
                                  nullptr, &message);
  if (result != SQLITE_OK) {
    const std::string text =
        message == nullptr ? sqlite3_errmsg(database) : std::string(message);
    sqlite3_free(message);
    canonical_store_error("canonical algorithm projection SQL failed: " +
                          text);
  }
}

[[nodiscard]] Statement prepare(sqlite3 *database, std::string_view sql) {
  sqlite3_stmt *raw = nullptr;
  if (sqlite3_prepare_v2(database, std::string(sql).c_str(), -1, &raw,
                         nullptr) != SQLITE_OK) {
    canonical_store_error("cannot prepare canonical algorithm SQL: " +
                          std::string(sqlite3_errmsg(database)));
  }
  return Statement(raw);
}

void bind_text(sqlite3_stmt *statement, int index, std::string_view value) {
  if (sqlite3_bind_text(statement, index, value.data(),
                        static_cast<int>(value.size()), SQLITE_TRANSIENT) !=
      SQLITE_OK) {
    canonical_store_error("cannot bind canonical algorithm text");
  }
}

void bind_int(sqlite3_stmt *statement, int index, int value) {
  if (sqlite3_bind_int(statement, index, value) != SQLITE_OK) {
    canonical_store_error("cannot bind canonical algorithm integer");
  }
}

void step_done(sqlite3 *database, sqlite3_stmt *statement) {
  if (sqlite3_step(statement) != SQLITE_DONE) {
    canonical_store_error("cannot update canonical algorithm projection: " +
                          std::string(sqlite3_errmsg(database)));
  }
}

[[nodiscard]] std::string column_text(sqlite3_stmt *statement, int index) {
  const auto *text = sqlite3_column_text(statement, index);
  return text == nullptr ? std::string()
                         : std::string(reinterpret_cast<const char *>(text));
}

void create_tables(sqlite3 *database) {
  execute(database, R"SQL(
    CREATE TABLE IF NOT EXISTS canonical_algorithms (
      canonical_id TEXT PRIMARY KEY,
      representative_behavior_signature TEXT NOT NULL UNIQUE,
      mathematical_signature TEXT NOT NULL,
      semantic_signature TEXT NOT NULL,
      canonical_algorithm_signature TEXT NOT NULL,
      representative_version TEXT NOT NULL,
      domain TEXT NOT NULL,
      output_count INTEGER NOT NULL,
      input_count INTEGER NOT NULL,
      store_generation INTEGER NOT NULL,
      payload_json TEXT NOT NULL,
      path TEXT NOT NULL,
      created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS canonical_algorithms_math_idx
      ON canonical_algorithms(mathematical_signature);
    CREATE INDEX IF NOT EXISTS canonical_algorithms_semantic_idx
      ON canonical_algorithms(semantic_signature);
    CREATE INDEX IF NOT EXISTS canonical_algorithms_outer_idx
      ON canonical_algorithms(canonical_algorithm_signature);
    CREATE INDEX IF NOT EXISTS canonical_algorithms_shape_idx
      ON canonical_algorithms(domain,output_count,input_count);
    CREATE TABLE IF NOT EXISTS canonical_algorithm_sources (
      source_id TEXT PRIMARY KEY,
      canonical_id TEXT NOT NULL,
      canonical_algorithm_signature TEXT NOT NULL,
      source_structural_hash TEXT NOT NULL,
      source_mimo_signature TEXT NOT NULL,
      source_normalization_signature TEXT NOT NULL,
      representative_candidate_signature TEXT NOT NULL,
      representative_search_audit_hash TEXT NOT NULL,
      form_audit_hash TEXT NOT NULL,
      proof_signature TEXT NOT NULL,
      store_generation INTEGER NOT NULL,
      payload_json TEXT NOT NULL,
      path TEXT NOT NULL,
      created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS canonical_sources_canonical_idx
      ON canonical_algorithm_sources(canonical_id);
    CREATE INDEX IF NOT EXISTS canonical_sources_outer_idx
      ON canonical_algorithm_sources(canonical_algorithm_signature);
    CREATE INDEX IF NOT EXISTS canonical_sources_structure_idx
      ON canonical_algorithm_sources(source_structural_hash);
    CREATE TABLE IF NOT EXISTS canonical_algorithm_relations (
      relation_id TEXT PRIMARY KEY,
      relation_type TEXT NOT NULL,
      source_ref TEXT NOT NULL,
      source_kind TEXT NOT NULL,
      target_ref TEXT NOT NULL,
      target_kind TEXT NOT NULL,
      basis TEXT NOT NULL,
      basis_signature TEXT NOT NULL,
      evidence_json TEXT NOT NULL,
      store_generation INTEGER NOT NULL,
      payload_json TEXT NOT NULL,
      path TEXT NOT NULL,
      created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS canonical_relations_type_idx
      ON canonical_algorithm_relations(relation_type);
    CREATE INDEX IF NOT EXISTS canonical_relations_source_idx
      ON canonical_algorithm_relations(source_ref);
    CREATE INDEX IF NOT EXISTS canonical_relations_target_idx
      ON canonical_algorithm_relations(target_ref);
    CREATE TABLE IF NOT EXISTS canonical_store_metadata (
      key TEXT PRIMARY KEY,
      value TEXT NOT NULL
    );
    CREATE VIRTUAL TABLE IF NOT EXISTS canonical_algorithm_fts USING fts5(
      canonical_id UNINDEXED,
      domain,
      meanings
    );
  )SQL");
}

[[nodiscard]] std::vector<std::filesystem::path>
json_files(const std::filesystem::path &root) {
  std::vector<std::filesystem::path> result;
  if (!std::filesystem::exists(root)) {
    return result;
  }
  for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json") {
      result.push_back(entry.path());
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

[[nodiscard]] Json read_envelope(const std::filesystem::path &path) {
  return contracts::parse_json(core::read_text(path));
}

void immutable_write(const std::filesystem::path &path, const Json &envelope,
                     const std::vector<std::string> &volatile_payload = {}) {
  if (!std::filesystem::exists(path)) {
    core::atomic_write_text(path, envelope.dump(2) + "\n");
    return;
  }
  Json existing = read_envelope(path);
  Json expected = envelope;
  for (const auto &field : volatile_payload) {
    if (existing.contains("payload") && existing.at("payload").is_object()) {
      existing["payload"].erase(field);
    }
    if (expected.contains("payload") && expected.at("payload").is_object()) {
      expected["payload"].erase(field);
    }
  }
  existing.erase("created_at");
  expected.erase("created_at");
  if (existing != expected) {
    canonical_store_error("immutable canonical-store collision at " +
                          path.string());
  }
}

[[nodiscard]] Json channel_matrix_json(
    const std::vector<std::vector<saa::RationalChannel>> &matrix) {
  Json result = Json::array();
  for (const auto &row : matrix) {
    Json encoded = Json::array();
    for (const auto &channel : row) {
      encoded.push_back(saa::to_json(channel));
    }
    result.push_back(std::move(encoded));
  }
  return result;
}

[[nodiscard]] std::vector<std::string>
unique_ids(std::vector<std::string> values) {
  std::vector<std::string> result;
  std::set<std::string> seen;
  for (auto &value : values) {
    value = trimmed(std::move(value));
    if (!value.empty() && seen.insert(value).second) {
      result.push_back(std::move(value));
    }
  }
  return result;
}

[[nodiscard]] std::vector<std::string>
normalized_texts(std::vector<std::string> values) {
  std::set<std::string> result;
  for (auto &value : values) {
    value = normalized_text(std::move(value));
    if (!value.empty()) {
      result.insert(std::move(value));
    }
  }
  return {result.begin(), result.end()};
}

[[nodiscard]] std::string newline_joined(
    const std::vector<std::string> &values) {
  std::ostringstream output;
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index != 0U) {
      output << '\n';
    }
    output << values[index];
  }
  return output.str();
}

[[nodiscard]] std::vector<std::string>
payload_meanings(const Json &payload) {
  std::vector<std::string> result;
  if (!payload.contains("inputs") || !payload.at("inputs").is_array()) {
    return result;
  }
  for (const auto &input : payload.at("inputs")) {
    if (input.is_object() && input.contains("canonical_meaning") &&
        input.at("canonical_meaning").is_string()) {
      const std::string meaning =
          normalized_text(input.at("canonical_meaning").get<std::string>());
      if (!meaning.empty()) {
        result.push_back(meaning);
      }
    }
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

[[nodiscard]] std::vector<std::string>
missing_values(const std::vector<std::string> &required,
               const std::set<std::string> &available) {
  std::vector<std::string> result;
  for (const auto &value : required) {
    if (!available.contains(value)) {
      result.push_back(value);
    }
  }
  return result;
}

} // namespace

CanonicalAlgorithmStore::CanonicalAlgorithmStore(EgcfStore &egcf_store)
    : egcf_store_(egcf_store), state_root_(egcf_store.state_root()),
      root_(state_root_ / "canonical-algorithms"),
      algorithm_root_(root_ / "objects" / "sha256"),
      source_root_(root_ / "sources" / "sha256"),
      relation_root_(root_ / "relations" / "sha256"),
      projection_path_(egcf_store.projection_path()) {
  std::filesystem::create_directories(algorithm_root_);
  std::filesystem::create_directories(source_root_);
  std::filesystem::create_directories(relation_root_);
  ensure_projection();
}

const std::filesystem::path &CanonicalAlgorithmStore::root() const noexcept {
  return root_;
}

std::filesystem::path CanonicalAlgorithmStore::typed_path(
    const std::filesystem::path &object_root, std::string_view object_id,
    std::string_view expected_kind) const {
  const auto parts = contracts::parse_typed_id(object_id);
  if (parts.object_type != expected_kind) {
    canonical_store_error("expected " + std::string(expected_kind) +
                          " object ID");
  }
  return object_root / parts.digest.substr(0, 2) /
         (parts.digest + ".json");
}

std::filesystem::path CanonicalAlgorithmStore::algorithm_path(
    std::string_view canonical_id) const {
  return typed_path(algorithm_root_, canonical_id, "canonical-algorithm");
}

void CanonicalAlgorithmStore::ensure_projection() {
  auto database = open_database(projection_path_);
  create_tables(database.get());
  auto marker = prepare(
      database.get(),
      "SELECT value FROM canonical_store_metadata WHERE key='schema_version'");
  const bool current = sqlite3_step(marker.get()) == SQLITE_ROW &&
                       column_text(marker.get(), 0) ==
                           std::to_string(
                               canonical_algorithm_store_schema_version);
  marker.reset();
  database.reset();
  if (!current) {
    rebuild_projection();
  }
}

Json CanonicalAlgorithmStore::canonical_payload(
    const saa::CanonicalRepresentativeAlgorithmForm &form) const {
  Json inputs = Json::array();
  for (const auto &input : form.inputs) {
    inputs.push_back(
        {{"canonical_meaning", input.canonical_meaning},
         {"canonical_position", input.canonical_position},
         {"excluded_output_indices", input.excluded_output_indices},
         {"expected_output_indices", input.expected_output_indices},
         {"normalized_domain", Json::array({Json::array({0, 1}),
                                             Json::array({1, 1})})},
         {"paired_output_index", input.paired_output_index}});
  }
  return {
      {"domain", form.domain},
      {"inputs", inputs},
      {"mathematical_representative_signature",
       form.mathematical_representative_signature},
      {"normalized_channels", channel_matrix_json(form.normalized_channels)},
      {"normalized_sample_interval",
       form.normalized_sample_interval
           ? saa::rational_json(*form.normalized_sample_interval)
           : Json(nullptr)},
      {"output_count", form.output_count},
      {"qualification",
       "EXACT_MATHEMATICS_AND_EVIDENCE_GROUNDED_RESOLVED_SEMANTICS"},
      {"representative_behavior_signature",
       form.representative_behavior_signature},
      {"representative_input_count", form.representative_input_count},
      {"representative_version", form.representative_version_value},
      {"schema_version", 1},
      {"semantic_representative_signature",
       form.semantic_representative_signature},
      {"store_version", canonical_algorithm_store_version},
      {"variable", form.variable}};
}

void CanonicalAlgorithmStore::verify_form(
    const saa::CanonicalRepresentativeAlgorithmForm &form) const {
  if (form.representative_version_value !=
      saa::canonical_representative_version) {
    canonical_store_error("unsupported SAA-6 representative version");
  }
  if (!form.canonical_admission_eligible) {
    canonical_store_error("SAA-6 form is not canonical-admission eligible");
  }
  if (form.store_status != "ELIGIBLE_CANONICAL_REPRESENTATIVE_FORM") {
    canonical_store_error("SAA-6 form has not reached store-eligible status");
  }
  if (form.structural_binding_policy_value !=
      saa::structural_binding_policy) {
    canonical_store_error("unsupported SAA-6 structural binding policy");
  }
  for (const auto &[label, signature] :
       std::array<std::pair<std::string_view, std::string>, 5>{
           std::pair{"mathematical representative signature",
                     form.mathematical_representative_signature},
           std::pair{"semantic representative signature",
                     form.semantic_representative_signature},
           std::pair{"representative behavior signature",
                     form.representative_behavior_signature},
           std::pair{"canonical algorithm signature",
                     form.canonical_algorithm_signature},
           std::pair{"audit hash", form.audit_hash}}) {
    static_cast<void>(exact_sha(signature, label));
  }
  if (form.inputs.size() != form.representative_input_count) {
    canonical_store_error("SAA-6 canonical input positions must be contiguous");
  }
  std::set<std::size_t> paired;
  Json semantic_inputs = Json::array();
  Json boundary_signatures = Json::array();
  Json issue_signatures = Json::array();
  Json candidate_signatures = Json::array();
  Json resolution_signatures = Json::array();
  for (std::size_t index = 0; index < form.inputs.size(); ++index) {
    const auto &input = form.inputs[index];
    if (input.canonical_position != index) {
      canonical_store_error(
          "SAA-6 canonical input positions must be contiguous");
    }
    if (!paired.insert(input.paired_output_index).second) {
      canonical_store_error("SAA-6 paired outputs must be unique");
    }
    if (input.canonical_meaning != normalized_text(input.meaning) ||
        input.canonical_meaning.empty()) {
      canonical_store_error(
          "SAA-6 canonical meaning is inconsistent with resolved meaning");
    }
    const auto &boundary = input.boundary;
    if (boundary.bound_policy != saa::representative_bound_policy) {
      canonical_store_error("unsupported representative boundary policy");
    }
    if (boundary.raw_width !=
        boundary.raw_maximum - boundary.raw_minimum) {
      canonical_store_error("representative boundary width mismatch");
    }
    if (boundary.raw_width <= 0 || boundary.normalized_minimum != 0 ||
        boundary.normalized_maximum != 1) {
      canonical_store_error(
          "representative boundary is not a positive exact [0,1] normalization");
    }
    const Json boundary_material = {
        {"bound_policy", saa::representative_bound_policy},
        {"candidate_input_index", boundary.candidate_input_index},
        {"raw_maximum", saa::rational_json(boundary.raw_maximum)},
        {"raw_minimum", saa::rational_json(boundary.raw_minimum)},
        {"representative_version", saa::canonical_representative_version},
        {"schema_version", 1},
        {"semantic_resolution_signature",
         boundary.semantic_resolution_signature},
        {"source_normalization_signature",
         boundary.source_normalization_signature},
        {"target", Json::array({Json::array({0, 1}),
                                 Json::array({1, 1})})}};
    if (contracts::sha256_json(boundary_material) !=
        boundary.boundary_signature) {
      canonical_store_error("representative boundary signature mismatch");
    }
    semantic_inputs.push_back(
        {{"canonical_position", input.canonical_position},
         {"excluded_output_indices", input.excluded_output_indices},
         {"expected_output_indices", input.expected_output_indices},
         {"meaning", input.canonical_meaning},
         {"paired_output_index", input.paired_output_index}});
    boundary_signatures.push_back(boundary.boundary_signature);
    candidate_signatures.push_back(input.semantic_candidate_signature);
    resolution_signatures.push_back(input.semantic_resolution_signature);
  }
  const Json mathematical_payload = {
      {"claim_scope",
       "EXACT_MINIMAL_DECOUPLED_RENORMALIZED_REPRESENTATIVE_DYNAMICS"},
      {"domain", form.domain},
      {"input_order_policy", "ORDER_BY_UNIQUE_PAIRED_OUTPUT"},
      {"normalized_channels", channel_matrix_json(form.normalized_channels)},
      {"normalized_sample_interval",
       form.normalized_sample_interval
           ? saa::rational_json(*form.normalized_sample_interval)
           : Json(nullptr)},
      {"output_count", form.output_count},
      {"representative_input_count", form.representative_input_count},
      {"representative_version", saa::canonical_representative_version},
      {"schema_version", 1},
      {"target_input_domain", Json::array({0, 1})},
      {"variable", form.variable}};
  const std::string mathematical_signature =
      contracts::sha256_json(mathematical_payload);
  if (mathematical_signature != form.mathematical_representative_signature) {
    canonical_store_error("SAA-6 mathematical representative signature mismatch");
  }
  const std::string semantic_signature = contracts::sha256_json(
      {{"claim_scope", "RESOLVED_REPRESENTATIVE_INPUT_SEMANTICS"},
       {"inputs", semantic_inputs},
       {"representative_version", saa::canonical_representative_version},
       {"schema_version", 1}});
  if (semantic_signature != form.semantic_representative_signature) {
    canonical_store_error("SAA-6 semantic representative signature mismatch");
  }
  const std::string behavior_signature = contracts::sha256_json(
      {{"claim_scope", "CANONICAL_REPRESENTATIVE_BEHAVIOR_AND_SEMANTICS"},
       {"mathematical_representative_signature", mathematical_signature},
       {"representative_version", saa::canonical_representative_version},
       {"schema_version", 1},
       {"semantic_representative_signature", semantic_signature}});
  if (behavior_signature != form.representative_behavior_signature) {
    canonical_store_error("SAA-6 representative behavior signature mismatch");
  }
  const std::string algorithm_signature = contracts::sha256_json(
      {{"claim_scope",
        "CANONICAL_REPRESENTATIVE_ALGORITHM_WITH_CONSERVATIVE_SOURCE_STRUCTURE"},
       {"representative_behavior_signature", behavior_signature},
       {"representative_version", saa::canonical_representative_version},
       {"schema_version", 1},
       {"source_structural_hash", form.source_structural_hash},
       {"source_structural_strength", form.source_structural_strength},
       {"structural_binding_policy", saa::structural_binding_policy}});
  if (algorithm_signature != form.canonical_algorithm_signature) {
    canonical_store_error("SAA-6 canonical algorithm signature mismatch");
  }
}

void CanonicalAlgorithmStore::require_grounded_evidence(
    std::string_view evidence_id) const {
  EgcfRecord record;
  try {
    record = egcf_store_.get(evidence_id);
  } catch (const std::exception &) {
    canonical_store_error("canonical semantic evidence is not registered: " +
                          std::string(evidence_id));
  }
  if (record.object_type != "egcf-evidence") {
    canonical_store_error(
        "canonical semantic evidence ID does not reference EvidenceArtifact");
  }
  const auto success = record.payload.contains("success") &&
                               !record.payload.at("success").is_null()
                           ? std::optional<bool>(
                                 record.payload.at("success").get<bool>())
                           : std::nullopt;
  if (success != true || record.payload.value("simulated", false)) {
    canonical_store_error(
        "canonical semantic evidence must be successful and non-simulated");
  }
  const std::string producer = record.payload.value("producer", "");
  if (!producer.starts_with("deterministic-") &&
      !producer.starts_with("human-")) {
    canonical_store_error(
        "canonical semantic evidence must come from deterministic or human grounding");
  }
  if (record.payload.value("method", "") == "reported") {
    canonical_store_error(
        "reported-only evidence cannot ground canonical semantics");
  }
}

std::string CanonicalAlgorithmStore::verify_semantic_proof(
    const saa::CanonicalRepresentativeAlgorithmForm &form,
    const std::vector<saa::SemanticRepresentationIssue> &issues,
    const std::vector<saa::SemanticCandidateMeaning> &candidates,
    const std::vector<saa::SemanticResolution> &resolutions) const {
  if (form.representative_input_count == 0U) {
    if (!issues.empty() || !candidates.empty() || !resolutions.empty()) {
      canonical_store_error(
          "zero-input canonical form must not carry representative semantic issues");
    }
    return contracts::sha256_json(
        {{"proof", "ZERO_INPUT_VACUOUS_SEMANTICS"},
         {"schema_version", 1}});
  }
  std::map<int, const saa::SemanticRepresentationIssue *> issue_by_index;
  for (const auto &issue : issues) {
    if (issue.coordinate_kind != "REPRESENTATIVE_INPUT") {
      canonical_store_error(
          "canonical semantic proof must describe representative inputs");
    }
    if (!issue_by_index.emplace(issue.coordinate_index, &issue).second) {
      canonical_store_error("duplicate representative semantic issue");
    }
  }
  if (issue_by_index.size() != form.representative_input_count) {
    canonical_store_error(
        "canonical admission requires one semantic issue per representative input");
  }
  for (std::size_t index = 0; index < form.representative_input_count; ++index) {
    if (!issue_by_index.contains(static_cast<int>(index))) {
      canonical_store_error(
          "canonical admission requires one semantic issue per representative input");
    }
  }
  std::map<std::string, const saa::SemanticCandidateMeaning *>
      candidate_by_issue;
  for (const auto &candidate : candidates) {
    if (!candidate_by_issue.emplace(candidate.issue_id, &candidate).second) {
      canonical_store_error("duplicate semantic candidate for one issue");
    }
  }
  std::map<std::string, const saa::SemanticResolution *> resolution_by_issue;
  for (const auto &resolution : resolutions) {
    if (!resolution_by_issue.emplace(resolution.issue_id, &resolution).second) {
      canonical_store_error("duplicate semantic resolution for one issue");
    }
  }
  Json proof_rows = Json::array();
  for (const auto &form_input : form.inputs) {
    const auto issue_iterator =
        issue_by_index.find(static_cast<int>(form_input.candidate_input_index));
    if (issue_iterator == issue_by_index.end()) {
      canonical_store_error(
          "canonical admission is missing semantic candidate or resolution");
    }
    const auto &issue = *issue_iterator->second;
    const auto candidate_iterator = candidate_by_issue.find(issue.issue_id);
    const auto resolution_iterator = resolution_by_issue.find(issue.issue_id);
    if (candidate_iterator == candidate_by_issue.end() ||
        resolution_iterator == resolution_by_issue.end()) {
      canonical_store_error(
          "canonical admission is missing semantic candidate or resolution");
    }
    const auto &candidate = *candidate_iterator->second;
    const auto &resolution = *resolution_iterator->second;
    if (candidate.candidate_id != form_input.semantic_candidate_id ||
        candidate.signature != form_input.semantic_candidate_signature) {
      canonical_store_error(
          "stored semantic candidate differs from SAA-6 form");
    }
    if (resolution.candidate_id != candidate.candidate_id ||
        resolution.resolution_signature !=
            form_input.semantic_resolution_signature) {
      canonical_store_error(
          "semantic resolution differs from SAA-6 form");
    }
    if (resolution.status != "SEMANTICALLY_RESOLVED" ||
        !resolution.canonical_semantic_eligible ||
        !resolution.independent_review || resolution.semantic_fit_bp != 10000) {
      canonical_store_error(
          "canonical semantic proof requires complete independently reviewed resolution");
    }
    if (normalized_text(candidate.meaning) != form_input.canonical_meaning ||
        candidate.expected_output_indices !=
            form_input.expected_output_indices ||
        candidate.excluded_output_indices !=
            form_input.excluded_output_indices) {
      canonical_store_error(
          "semantic candidate contract differs from SAA-6 form");
    }
    auto resolution_evidence = resolution.evidence_ids;
    auto form_evidence = form_input.semantic_evidence_ids;
    std::sort(resolution_evidence.begin(), resolution_evidence.end());
    std::sort(form_evidence.begin(), form_evidence.end());
    if (resolution_evidence != form_evidence) {
      canonical_store_error("semantic evidence IDs differ from SAA-6 form");
    }
    if (resolution_evidence.empty()) {
      canonical_store_error("canonical semantics require grounded evidence");
    }
    std::map<std::string, const saa::SemanticFalsifierResult *> by_falsifier;
    for (const auto &result : resolution.falsifier_results) {
      by_falsifier[result.falsifier] = &result;
    }
    for (const auto &falsifier : candidate.falsifiers) {
      const auto found = by_falsifier.find(falsifier);
      if (found == by_falsifier.end() || found->second->outcome != "SURVIVED") {
        canonical_store_error(
            "all declared semantic falsifiers must have survived");
      }
      if (found->second->evidence_id) {
        require_grounded_evidence(*found->second->evidence_id);
      }
    }
    for (const auto &evidence_id : resolution_evidence) {
      require_grounded_evidence(evidence_id);
    }
    Json falsifiers = Json::array();
    for (const auto &result : resolution.falsifier_results) {
      falsifiers.push_back(saa::to_json(result));
    }
    proof_rows.push_back(
        {{"candidate_signature", candidate.signature},
         {"coordinate", form_input.canonical_position},
         {"evidence_ids", resolution_evidence},
         {"falsifiers", falsifiers},
         {"independent_review", true},
         {"issue_signature", issue.signature},
         {"resolution_signature", resolution.resolution_signature}});
  }
  return contracts::sha256_json(
      {{"representative_behavior_signature",
        form.representative_behavior_signature},
       {"schema_version", 1},
       {"semantic_proof", proof_rows},
       {"store_version", canonical_algorithm_store_version}});
}

CanonicalLookupResult CanonicalAlgorithmStore::lookup(
    const saa::CanonicalRepresentativeAlgorithmForm &form) {
  ensure_projection();
  verify_form(form);
  auto database = open_database(projection_path_);
  const auto query = [&](std::string_view sql, std::string_view first,
                         std::optional<std::string_view> second) {
    auto statement = prepare(database.get(), sql);
    bind_text(statement.get(), 1, first);
    if (second) {
      bind_text(statement.get(), 2, *second);
    }
    std::vector<std::string> result;
    while (sqlite3_step(statement.get()) == SQLITE_ROW) {
      result.push_back(column_text(statement.get(), 0));
    }
    return result;
  };
  auto exact = query(
      "SELECT canonical_id FROM canonical_algorithms WHERE representative_behavior_signature=? ORDER BY canonical_id",
      form.representative_behavior_signature, std::nullopt);
  auto mathematical = query(
      "SELECT canonical_id FROM canonical_algorithms WHERE mathematical_signature=? AND representative_behavior_signature!=? ORDER BY canonical_id",
      form.mathematical_representative_signature,
      form.representative_behavior_signature);
  auto semantic = query(
      "SELECT canonical_id FROM canonical_algorithms WHERE semantic_signature=? AND representative_behavior_signature!=? ORDER BY canonical_id",
      form.semantic_representative_signature,
      form.representative_behavior_signature);
  auto source_bound = query(
      "SELECT canonical_id FROM canonical_algorithms WHERE canonical_algorithm_signature=? ORDER BY canonical_id",
      form.canonical_algorithm_signature, std::nullopt);
  std::string status;
  if (!exact.empty()) {
    status = "REPRESENTATIVE_EQUIVALENT_ALREADY_STORED";
  } else if (!mathematical.empty() && !semantic.empty()) {
    status = "MULTIPLE_CANONICAL_NEIGHBOR_MATCHES";
  } else if (!mathematical.empty()) {
    status = "MATHEMATICAL_MATCH_SEMANTIC_DIFFERENCE";
  } else if (!semantic.empty()) {
    status = "SEMANTIC_MATCH_MATHEMATICAL_DIFFERENCE";
  } else {
    status = "UNIQUE_CANONICAL_CANDIDATE";
  }
  return {std::move(status), std::move(exact), std::move(mathematical),
          std::move(semantic), std::move(source_bound)};
}

int CanonicalAlgorithmStore::current_generation() {
  ensure_projection();
  auto database = open_database(projection_path_);
  auto statement = prepare(
      database.get(),
      "SELECT COALESCE(MAX(store_generation),0) FROM canonical_algorithms");
  if (sqlite3_step(statement.get()) != SQLITE_ROW) {
    canonical_store_error("cannot read canonical algorithm generation");
  }
  return sqlite3_column_int(statement.get(), 0);
}

std::string CanonicalAlgorithmStore::persist_source(
    const saa::CanonicalRepresentativeAlgorithmForm &form,
    std::string_view canonical_id, std::string_view proof_signature,
    int generation, std::string_view created_at) {
  const Json identity =
      {{"canonical_algorithm_signature", form.canonical_algorithm_signature},
       {"canonical_id", canonical_id},
       {"form_audit_hash", form.audit_hash},
       {"proof_signature", proof_signature},
       {"representative_candidate_signature",
        form.representative_candidate_signature},
       {"representative_search_audit_hash",
        form.representative_search_audit_hash},
       {"source_mimo_signature", form.source_mimo_signature},
       {"source_normalization_signature",
        form.source_normalization_signature},
       {"source_structural_hash", form.source_structural_hash}};
  const std::string source_id =
      contracts::typed_id("algorithm-source", identity);
  const Json payload =
      {{"canonical_algorithm_signature", form.canonical_algorithm_signature},
       {"canonical_id", canonical_id},
       {"created_at", created_at},
       {"form", saa::to_json(form)},
       {"form_audit_hash", form.audit_hash},
       {"proof_signature", proof_signature},
       {"representative_candidate_signature",
        form.representative_candidate_signature},
       {"representative_search_audit_hash",
        form.representative_search_audit_hash},
       {"schema_version", 1},
       {"source_mimo_signature", form.source_mimo_signature},
       {"source_normalization_signature",
        form.source_normalization_signature},
       {"source_structural_hash", form.source_structural_hash},
       {"store_generation", generation},
       {"store_version", canonical_algorithm_store_version}};
  const Json envelope = {{"object_id", source_id},
                         {"object_type", "algorithm-source"},
                         {"payload", payload},
                         {"schema_version", 1}};
  const auto path = typed_path(source_root_, source_id, "algorithm-source");
  immutable_write(path, envelope, {"created_at"});
  const Json stored = read_envelope(path);
  const Json &stored_payload = stored.at("payload");
  auto database = open_database(projection_path_);
  auto statement = prepare(
      database.get(),
      "INSERT OR IGNORE INTO canonical_algorithm_sources VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
  bind_text(statement.get(), 1, source_id);
  bind_text(statement.get(), 2, canonical_id);
  bind_text(statement.get(), 3, form.canonical_algorithm_signature);
  bind_text(statement.get(), 4, form.source_structural_hash);
  bind_text(statement.get(), 5, form.source_mimo_signature);
  bind_text(statement.get(), 6, form.source_normalization_signature);
  bind_text(statement.get(), 7, form.representative_candidate_signature);
  bind_text(statement.get(), 8, form.representative_search_audit_hash);
  bind_text(statement.get(), 9, form.audit_hash);
  bind_text(statement.get(), 10, proof_signature);
  bind_int(statement.get(), 11, stored_payload.at("store_generation").get<int>());
  bind_text(statement.get(), 12,
            contracts::canonical_json(stored_payload));
  bind_text(statement.get(), 13,
            path.lexically_relative(state_root_).generic_string());
  bind_text(statement.get(), 14,
            stored_payload.at("created_at").get_ref<const std::string &>());
  step_done(database.get(), statement.get());
  return source_id;
}

void CanonicalAlgorithmStore::persist_canonical(
    const saa::CanonicalRepresentativeAlgorithmForm &form,
    std::string_view canonical_id, std::string_view source_id, int generation,
    std::string_view created_at) {
  const Json payload = canonical_payload(form);
  const Json envelope =
      {{"anchor_source_id", source_id},
       {"created_at", created_at},
       {"object_id", canonical_id},
       {"object_type", "canonical-algorithm"},
       {"payload", payload},
       {"schema_version", 1},
       {"store_generation", generation},
       {"store_version", canonical_algorithm_store_version},
       {"canonical_algorithm_signature", form.canonical_algorithm_signature}};
  const auto path = algorithm_path(canonical_id);
  immutable_write(path, envelope);
  const Json stored = read_envelope(path);
  auto database = open_database(projection_path_);
  auto statement = prepare(
      database.get(),
      "INSERT OR IGNORE INTO canonical_algorithms VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)");
  bind_text(statement.get(), 1, canonical_id);
  bind_text(statement.get(), 2, form.representative_behavior_signature);
  bind_text(statement.get(), 3, form.mathematical_representative_signature);
  bind_text(statement.get(), 4, form.semantic_representative_signature);
  bind_text(statement.get(), 5, form.canonical_algorithm_signature);
  bind_text(statement.get(), 6, form.representative_version_value);
  bind_text(statement.get(), 7, form.domain);
  bind_int(statement.get(), 8, static_cast<int>(form.output_count));
  bind_int(statement.get(), 9,
           static_cast<int>(form.representative_input_count));
  bind_int(statement.get(), 10, generation);
  bind_text(statement.get(), 11, contracts::canonical_json(payload));
  bind_text(statement.get(), 12,
            path.lexically_relative(state_root_).generic_string());
  bind_text(statement.get(), 13,
            stored.at("created_at").get_ref<const std::string &>());
  step_done(database.get(), statement.get());
  auto fts_delete = prepare(
      database.get(),
      "DELETE FROM canonical_algorithm_fts WHERE canonical_id=?");
  bind_text(fts_delete.get(), 1, canonical_id);
  step_done(database.get(), fts_delete.get());
  auto fts_insert = prepare(
      database.get(),
      "INSERT INTO canonical_algorithm_fts(canonical_id,domain,meanings) VALUES(?,?,?)");
  bind_text(fts_insert.get(), 1, canonical_id);
  bind_text(fts_insert.get(), 2, form.domain);
  bind_text(fts_insert.get(), 3,
            newline_joined(payload_meanings(payload)));
  step_done(database.get(), fts_insert.get());
}

AlgorithmRelationRecord CanonicalAlgorithmStore::make_relation(
    std::string relation_type, std::string source_ref,
    std::string source_kind, std::string target_ref,
    std::string target_kind, std::string basis,
    std::string basis_signature, std::vector<std::string> evidence_ids,
    int generation) const {
  relation_type = uppercase(std::move(relation_type));
  if (!relation_types.contains(relation_type)) {
    canonical_store_error("unsupported canonical algorithm relation: " +
                          relation_type);
  }
  basis = trimmed(std::move(basis));
  if (basis.empty()) {
    canonical_store_error("algorithm relation requires a non-empty basis");
  }
  basis_signature = exact_sha(std::move(basis_signature),
                              "relation basis signature");
  evidence_ids = unique_ids(std::move(evidence_ids));
  if (relation_type == "NEAR_VARIANT_OF" && target_ref < source_ref) {
    std::swap(source_ref, target_ref);
    std::swap(source_kind, target_kind);
  }
  auto sorted_evidence = evidence_ids;
  std::sort(sorted_evidence.begin(), sorted_evidence.end());
  const Json identity = {{"basis", basis},
                         {"basis_signature", basis_signature},
                         {"evidence_ids", sorted_evidence},
                         {"relation_type", relation_type},
                         {"source_kind", source_kind},
                         {"source_ref", source_ref},
                         {"target_kind", target_kind},
                         {"target_ref", target_ref}};
  return {.relation_id = contracts::typed_id("algorithm-relation", identity),
          .relation_type = std::move(relation_type),
          .source_ref = std::move(source_ref),
          .source_kind = std::move(source_kind),
          .target_ref = std::move(target_ref),
          .target_kind = std::move(target_kind),
          .basis = std::move(basis),
          .basis_signature = std::move(basis_signature),
          .evidence_ids = std::move(sorted_evidence),
          .store_generation = generation,
          .created_at = utc_now()};
}

std::string CanonicalAlgorithmStore::persist_relation(
    const AlgorithmRelationRecord &relation) {
  const Json payload = to_json(relation);
  const Json envelope = {{"object_id", relation.relation_id},
                         {"object_type", "algorithm-relation"},
                         {"payload", payload},
                         {"schema_version", 1}};
  const auto path = typed_path(relation_root_, relation.relation_id,
                               "algorithm-relation");
  immutable_write(path, envelope, {"created_at"});
  const Json stored = read_envelope(path);
  const Json &stored_payload = stored.at("payload");
  auto database = open_database(projection_path_);
  auto statement = prepare(
      database.get(),
      "INSERT OR IGNORE INTO canonical_algorithm_relations VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)");
  bind_text(statement.get(), 1, relation.relation_id);
  bind_text(statement.get(), 2, relation.relation_type);
  bind_text(statement.get(), 3, relation.source_ref);
  bind_text(statement.get(), 4, relation.source_kind);
  bind_text(statement.get(), 5, relation.target_ref);
  bind_text(statement.get(), 6, relation.target_kind);
  bind_text(statement.get(), 7, relation.basis);
  bind_text(statement.get(), 8, relation.basis_signature);
  bind_text(statement.get(), 9,
            contracts::canonical_json(relation.evidence_ids));
  bind_int(statement.get(), 10,
           stored_payload.at("store_generation").get<int>());
  bind_text(statement.get(), 11,
            contracts::canonical_json(stored_payload));
  bind_text(statement.get(), 12,
            path.lexically_relative(state_root_).generic_string());
  bind_text(statement.get(), 13,
            stored_payload.at("created_at").get_ref<const std::string &>());
  step_done(database.get(), statement.get());
  return relation.relation_id;
}

CanonicalAdmissionResult CanonicalAlgorithmStore::admit(
    const saa::CanonicalRepresentativeAlgorithmForm &form,
    const std::vector<saa::SemanticRepresentationIssue> &semantic_issues,
    const std::vector<saa::SemanticCandidateMeaning> &semantic_candidates,
    const std::vector<saa::SemanticResolution> &semantic_resolutions) {
  ensure_projection();
  verify_form(form);
  const std::string proof_signature = verify_semantic_proof(
      form, semantic_issues, semantic_candidates, semantic_resolutions);
  auto found = lookup(form);
  const std::string created_at = utc_now();
  if (!found.exact_equivalent_ids.empty()) {
    const std::string canonical_id = found.exact_equivalent_ids.front();
    const int generation = generation_for(canonical_id);
    const std::string source_id = persist_source(
        form, canonical_id, proof_signature, generation, created_at);
    const auto relation = make_relation(
        "EQUIVALENT_TO", source_id, "SOURCE_REPRESENTATION", canonical_id,
        "CANONICAL_ALGORITHM", "EXACT_REPRESENTATIVE_BEHAVIOR_SIGNATURE",
        form.representative_behavior_signature, {}, generation);
    const std::string relation_id = persist_relation(relation);
    return {"REUSED_EQUIVALENT_CANONICAL", canonical_id, source_id,
            generation, std::move(found), {relation_id}};
  }
  const int generation = current_generation() + 1;
  const std::string canonical_id =
      "canonical-algorithm:sha256:" + form.representative_behavior_signature;
  const Json source_identity =
      {{"canonical_algorithm_signature", form.canonical_algorithm_signature},
       {"canonical_id", canonical_id},
       {"form_audit_hash", form.audit_hash},
       {"proof_signature", proof_signature},
       {"representative_candidate_signature",
        form.representative_candidate_signature},
       {"representative_search_audit_hash",
        form.representative_search_audit_hash},
       {"source_mimo_signature", form.source_mimo_signature},
       {"source_normalization_signature",
        form.source_normalization_signature},
       {"source_structural_hash", form.source_structural_hash}};
  const std::string source_id =
      contracts::typed_id("algorithm-source", source_identity);
  persist_canonical(form, canonical_id, source_id, generation, created_at);
  const std::string persisted_source_id = persist_source(
      form, canonical_id, proof_signature, generation, created_at);
  if (persisted_source_id != source_id) {
    canonical_store_error("canonical anchor source identity drifted");
  }
  std::vector<std::string> relation_ids;
  relation_ids.push_back(persist_relation(make_relation(
      "DERIVED_FROM", canonical_id, "CANONICAL_ALGORITHM", source_id,
      "SOURCE_REPRESENTATION", "EXACT_SAA6_DERIVATION", form.audit_hash, {},
      generation)));
  for (const auto &neighbor : found.mathematical_match_ids) {
    relation_ids.push_back(persist_relation(make_relation(
        "NEAR_VARIANT_OF", canonical_id, "CANONICAL_ALGORITHM", neighbor,
        "CANONICAL_ALGORITHM",
        "EXACT_MATHEMATICAL_SIGNATURE_MATCH_SEMANTIC_DIFFERENCE",
        form.mathematical_representative_signature, {}, generation)));
  }
  for (const auto &neighbor : found.semantic_match_ids) {
    relation_ids.push_back(persist_relation(make_relation(
        "NEAR_VARIANT_OF", canonical_id, "CANONICAL_ALGORITHM", neighbor,
        "CANONICAL_ALGORITHM",
        "EXACT_SEMANTIC_SIGNATURE_MATCH_MATHEMATICAL_DIFFERENCE",
        form.semantic_representative_signature, {}, generation)));
  }
  auto database = open_database(projection_path_);
  auto marker = prepare(
      database.get(),
      "INSERT OR REPLACE INTO canonical_store_metadata(key,value) VALUES('schema_version',?)");
  bind_text(marker.get(), 1,
            std::to_string(canonical_algorithm_store_schema_version));
  step_done(database.get(), marker.get());
  return {"ADMITTED_NEW_CANONICAL", canonical_id, source_id, generation,
          std::move(found), std::move(relation_ids)};
}

int CanonicalAlgorithmStore::generation_for(std::string_view canonical_id) {
  ensure_projection();
  auto database = open_database(projection_path_);
  auto statement = prepare(
      database.get(),
      "SELECT store_generation FROM canonical_algorithms WHERE canonical_id=?");
  bind_text(statement.get(), 1, canonical_id);
  if (sqlite3_step(statement.get()) != SQLITE_ROW) {
    canonical_store_error("unknown canonical algorithm: " +
                          std::string(canonical_id));
  }
  return sqlite3_column_int(statement.get(), 0);
}

Json CanonicalAlgorithmStore::get(std::string_view canonical_id) {
  ensure_projection();
  Json envelope;
  try {
    envelope = read_envelope(algorithm_path(canonical_id));
  } catch (const std::exception &error) {
    canonical_store_error("cannot read canonical algorithm " +
                          std::string(canonical_id) + ": " + error.what());
  }
  if (envelope.at("object_id") != canonical_id) {
    canonical_store_error("canonical algorithm object identity mismatch");
  }
  const auto parts = contracts::parse_typed_id(canonical_id);
  if (!envelope.at("payload").is_object() ||
      envelope.at("payload").at("representative_behavior_signature") !=
          parts.digest) {
    canonical_store_error(
        "canonical algorithm behavior signature does not match object ID");
  }
  return envelope;
}

std::vector<Json> CanonicalAlgorithmStore::list() {
  ensure_projection();
  auto database = open_database(projection_path_);
  auto statement = prepare(
      database.get(),
      "SELECT canonical_id,representative_behavior_signature,mathematical_signature,semantic_signature,canonical_algorithm_signature,representative_version,domain,output_count,input_count,store_generation,payload_json,path,created_at FROM canonical_algorithms ORDER BY store_generation,canonical_id");
  std::vector<Json> result;
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    result.push_back(
        {{"canonical_algorithm_signature", column_text(statement.get(), 4)},
         {"canonical_id", column_text(statement.get(), 0)},
         {"created_at", column_text(statement.get(), 12)},
         {"domain", column_text(statement.get(), 6)},
         {"input_count", sqlite3_column_int(statement.get(), 8)},
         {"mathematical_signature", column_text(statement.get(), 2)},
         {"output_count", sqlite3_column_int(statement.get(), 7)},
         {"path", column_text(statement.get(), 11)},
         {"payload", contracts::parse_json(column_text(statement.get(), 10))},
         {"representative_behavior_signature",
          column_text(statement.get(), 1)},
         {"representative_version", column_text(statement.get(), 5)},
         {"semantic_signature", column_text(statement.get(), 3)},
         {"store_generation", sqlite3_column_int(statement.get(), 9)}});
  }
  return result;
}

std::vector<Json> CanonicalAlgorithmStore::list_mathematical_algorithms() {
  return list();
}

std::vector<Json> CanonicalAlgorithmStore::sources(
    std::optional<std::string> canonical_id) {
  ensure_projection();
  auto database = open_database(projection_path_);
  auto statement = canonical_id
                       ? prepare(database.get(),
                                 "SELECT payload_json FROM canonical_algorithm_sources WHERE canonical_id=? ORDER BY source_id")
                       : prepare(database.get(),
                                 "SELECT payload_json FROM canonical_algorithm_sources ORDER BY source_id");
  if (canonical_id) {
    bind_text(statement.get(), 1, *canonical_id);
  }
  std::vector<Json> result;
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    result.push_back(contracts::parse_json(column_text(statement.get(), 0)));
  }
  return result;
}

std::string CanonicalAlgorithmStore::add_relation(
    std::string source_id, std::string target_id, std::string relation_type,
    std::string basis, std::vector<std::string> evidence_ids) {
  ensure_projection();
  relation_type = uppercase(std::move(relation_type));
  if (relation_type == "EQUIVALENT_TO" ||
      relation_type == "NEAR_VARIANT_OF" ||
      relation_type == "DERIVED_FROM") {
    canonical_store_error(
        relation_type +
        " is derived by SAA canonicalization and cannot be manually asserted");
  }
  static_cast<void>(get(source_id));
  static_cast<void>(get(target_id));
  evidence_ids = unique_ids(std::move(evidence_ids));
  for (const auto &evidence_id : evidence_ids) {
    require_grounded_evidence(evidence_id);
  }
  if (evidence_ids.empty()) {
    canonical_store_error(
        "non-derived algorithm relations require grounded evidence");
  }
  auto sorted_evidence = evidence_ids;
  std::sort(sorted_evidence.begin(), sorted_evidence.end());
  const std::string basis_text = trimmed(basis);
  const std::string basis_signature = contracts::sha256_json(
      {{"basis", basis_text},
       {"evidence_ids", sorted_evidence},
       {"relation_type", relation_type},
       {"schema_version", 1},
       {"source_id", source_id},
       {"store_version", canonical_algorithm_store_version},
       {"target_id", target_id}});
  return persist_relation(make_relation(
      relation_type, std::move(source_id), "CANONICAL_ALGORITHM",
      std::move(target_id), "CANONICAL_ALGORITHM", std::move(basis),
      basis_signature, std::move(evidence_ids), current_generation()));
}

std::vector<AlgorithmRelationRecord> CanonicalAlgorithmStore::relations(
    std::optional<std::string> reference,
    std::optional<std::string> relation_type) {
  ensure_projection();
  std::string sql = "SELECT payload_json FROM canonical_algorithm_relations";
  std::vector<std::string> parameters;
  if (reference) {
    sql += " WHERE (source_ref=? OR target_ref=?)";
    parameters.push_back(*reference);
    parameters.push_back(*reference);
  }
  if (relation_type) {
    *relation_type = uppercase(std::move(*relation_type));
    if (!relation_types.contains(*relation_type)) {
      canonical_store_error("unsupported canonical algorithm relation: " +
                            *relation_type);
    }
    sql += reference ? " AND relation_type=?" : " WHERE relation_type=?";
    parameters.push_back(*relation_type);
  }
  sql += " ORDER BY relation_type,relation_id";
  auto database = open_database(projection_path_);
  auto statement = prepare(database.get(), sql);
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    bind_text(statement.get(), static_cast<int>(index + 1U), parameters[index]);
  }
  std::vector<AlgorithmRelationRecord> result;
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    const Json payload = contracts::parse_json(column_text(statement.get(), 0));
    result.push_back(
        {.relation_id = payload.at("relation_id").get<std::string>(),
         .relation_type = payload.at("relation_type").get<std::string>(),
         .source_ref = payload.at("source_ref").get<std::string>(),
         .source_kind = payload.at("source_kind").get<std::string>(),
         .target_ref = payload.at("target_ref").get<std::string>(),
         .target_kind = payload.at("target_kind").get<std::string>(),
         .basis = payload.at("basis").get<std::string>(),
         .basis_signature =
             payload.at("basis_signature").get<std::string>(),
         .evidence_ids =
             payload.at("evidence_ids").get<std::vector<std::string>>(),
         .store_generation = payload.at("store_generation").get<int>(),
         .created_at = payload.at("created_at").get<std::string>()});
  }
  return result;
}

std::vector<Json>
CanonicalAlgorithmStore::neighbors(std::string_view canonical_id) {
  static_cast<void>(get(canonical_id));
  std::vector<Json> result;
  for (const auto &relation :
       relations(std::string(canonical_id), std::nullopt)) {
    const std::string &other = relation.source_ref == canonical_id
                                   ? relation.target_ref
                                   : relation.source_ref;
    result.push_back({{"basis", relation.basis},
                      {"evidence_ids", relation.evidence_ids},
                      {"neighbor_ref", other},
                      {"relation_id", relation.relation_id},
                      {"relation_type", relation.relation_type}});
  }
  return result;
}

std::vector<std::string> CanonicalAlgorithmStore::search_text(
    std::string_view query, std::size_t limit) {
  ensure_projection();
  if (query.empty() || limit < 1U || limit > 64U) {
    canonical_store_error(
        "canonical algorithm FTS query and limit must be bounded and non-empty");
  }
  auto database = open_database(projection_path_);
  auto statement = prepare(
      database.get(),
      "SELECT canonical_id FROM canonical_algorithm_fts WHERE canonical_algorithm_fts MATCH ? ORDER BY bm25(canonical_algorithm_fts),canonical_id LIMIT ?");
  bind_text(statement.get(), 1, query);
  bind_int(statement.get(), 2, static_cast<int>(limit));
  std::vector<std::string> result;
  while (true) {
    const int status = sqlite3_step(statement.get());
    if (status == SQLITE_DONE) {
      break;
    }
    if (status != SQLITE_ROW) {
      canonical_store_error("canonical algorithm FTS query failed: " +
                            std::string(sqlite3_errmsg(database.get())));
    }
    result.push_back(column_text(statement.get(), 0));
  }
  return result;
}

CanonicalAlgorithmSearchResult
CanonicalAlgorithmStore::search(CanonicalAlgorithmQuery query) {
  ensure_projection();
  if (query.limit < 1U || query.limit > 64U) {
    canonical_store_error("canonical algorithm search limit outside range");
  }
  const auto normalize_signature = [](std::optional<std::string> &value,
                                      std::string_view label) {
    if (value) {
      *value = exact_sha(std::move(*value), label);
    }
  };
  normalize_signature(query.representative_behavior_signature,
                      "representative behavior signature");
  normalize_signature(query.mathematical_signature,
                      "mathematical signature");
  normalize_signature(query.semantic_signature, "semantic signature");
  normalize_signature(query.source_structural_hash,
                      "source structural hash");
  if (query.domain) {
    *query.domain = normalized_text(std::move(*query.domain));
    if (query.domain->empty()) {
      query.domain.reset();
    }
  }
  if ((query.output_count && *query.output_count < 0) ||
      (query.input_count && *query.input_count < 0)) {
    canonical_store_error("canonical algorithm shape cannot be negative");
  }
  query.semantic_meanings =
      normalized_texts(std::move(query.semantic_meanings));
  query.lexical_terms = normalized_texts(std::move(query.lexical_terms));
  const std::string query_signature = contracts::sha256_json(
      {{"query", to_json(query)},
       {"version", "saa-canonical-store-search-v1"}});
  std::set<std::string> automatically_demoted;
  for (const auto &decision :
       egcf_store_.list("internet-demotion-decision")) {
    if (decision.payload.value("status", "") == "CANONICAL_DEMOTED") {
      automatically_demoted.insert(
          decision.payload.at("demoted_canonical_algorithm_ref")
              .get<std::string>());
    }
  }

  struct Ranked final {
    int score = 0;
    std::string canonical_id;
    Json receipt;
  };
  std::vector<Ranked> eligible;
  Json excluded = Json::array();
  for (const auto &item : list()) {
    const std::string canonical_id = item.at("canonical_id").get<std::string>();
    const Json &payload = item.at("payload");
    const auto meanings = payload_meanings(payload);
    const std::set<std::string> meaning_set(meanings.begin(), meanings.end());
    std::set<std::string> structural_hashes;
    for (const auto &source : sources(canonical_id)) {
      structural_hashes.insert(
          source.at("source_structural_hash").get<std::string>());
    }
    std::vector<std::string> reasons;
    int score = 0;
    if (automatically_demoted.contains(canonical_id)) {
      reasons.push_back("automatically_demoted_internet_candidate");
    }
    if (query.representative_behavior_signature) {
      if (item.at("representative_behavior_signature") !=
          *query.representative_behavior_signature) {
        reasons.push_back("representative_behavior_signature_mismatch");
      } else {
        score += 4'000;
      }
    }
    if (query.mathematical_signature) {
      if (item.at("mathematical_signature") !=
          *query.mathematical_signature) {
        reasons.push_back("mathematical_signature_mismatch");
      } else {
        score += 2'500;
      }
    }
    if (query.semantic_signature) {
      if (item.at("semantic_signature") != *query.semantic_signature) {
        reasons.push_back("semantic_signature_mismatch");
      } else {
        score += 1'500;
      }
    }
    if (query.source_structural_hash) {
      if (!structural_hashes.contains(*query.source_structural_hash)) {
        reasons.push_back("source_structural_hash_mismatch");
      } else {
        score += 1'500;
      }
    }
    if (query.domain) {
      if (normalized_text(payload.value("domain", "")) != *query.domain) {
        reasons.push_back("domain_mismatch");
      } else {
        score += 750;
      }
    }
    if (query.output_count) {
      if (payload.value("output_count", -1) != *query.output_count) {
        reasons.push_back("output_count_mismatch");
      } else {
        score += 500;
      }
    }
    if (query.input_count) {
      if (payload.value("representative_input_count", -1) !=
          *query.input_count) {
        reasons.push_back("input_count_mismatch");
      } else {
        score += 500;
      }
    }
    const auto missing_meanings =
        missing_values(query.semantic_meanings, meaning_set);
    if (!missing_meanings.empty()) {
      reasons.push_back("missing_semantic_meanings");
    } else if (!query.semantic_meanings.empty()) {
      score += 1'500;
    }
    std::vector<std::string> matched_lexical;
    for (const auto &term : query.lexical_terms) {
      const bool matched =
          normalized_text(payload.value("domain", "")).find(term) !=
              std::string::npos ||
          std::any_of(meanings.begin(), meanings.end(),
                      [&](const auto &meaning) {
                        return meaning.find(term) != std::string::npos;
                      });
      if (matched) {
        matched_lexical.push_back(term);
      }
    }
    if (!query.lexical_terms.empty() && matched_lexical.empty()) {
      reasons.push_back("no_lexical_match");
    } else if (!query.lexical_terms.empty()) {
      score += static_cast<int>(
          (1'000U * matched_lexical.size()) / query.lexical_terms.size());
    }
    if (score == 0 && reasons.empty()) {
      score = 1'000;
    }
    score = std::min(10'000, score);
    Json receipt =
        {{"canonical_id", canonical_id},
         {"domain", payload.value("domain", "")},
         {"matched_lexical_terms", matched_lexical},
         {"mathematical_signature",
          item.at("mathematical_signature")},
         {"meanings", meanings},
         {"missing_semantic_meanings", missing_meanings},
         {"representative_behavior_signature",
          item.at("representative_behavior_signature")},
         {"score_bp", score},
         {"semantic_signature", item.at("semantic_signature")},
         {"source_structural_hashes",
          std::vector<std::string>(structural_hashes.begin(),
                                   structural_hashes.end())}};
    if (reasons.empty()) {
      eligible.push_back({score, canonical_id, std::move(receipt)});
    } else {
      excluded.push_back(
          {{"canonical_id", canonical_id},
           {"reasons", reasons},
           {"receipt", std::move(receipt)}});
    }
  }
  std::sort(eligible.begin(), eligible.end(),
            [](const auto &left, const auto &right) {
              return std::make_pair(-left.score, left.canonical_id) <
                     std::make_pair(-right.score, right.canonical_id);
            });
  Json candidates = Json::array();
  for (std::size_t index = 0; index < eligible.size(); ++index) {
    if (index < query.limit) {
      candidates.push_back(eligible[index].receipt);
    } else {
      excluded.push_back(
          {{"canonical_id", eligible[index].canonical_id},
           {"reasons", Json::array({"result_limit_tie_break"})},
           {"receipt", eligible[index].receipt}});
    }
  }
  std::sort(excluded.begin(), excluded.end(), [](const Json &left,
                                                 const Json &right) {
    return left.at("canonical_id").get<std::string>() <
           right.at("canonical_id").get<std::string>();
  });
  std::optional<std::string> selected;
  if (!candidates.empty()) {
    selected = candidates.front().at("canonical_id").get<std::string>();
  }
  const std::string status = selected ? "CANONICAL_ALGORITHM_SELECTED"
                                      : "NO_CANONICAL_ALGORITHM_MATCH";
  const Json result_payload =
      {{"candidate_ids", [&] {
          Json ids = Json::array();
          for (const auto &candidate : candidates) {
            ids.push_back(candidate.at("canonical_id"));
          }
          return ids;
        }()},
       {"excluded", excluded},
       {"query_signature", query_signature},
       {"selected_canonical_id", selected ? Json(*selected) : Json(nullptr)},
       {"status", status},
       {"version", "saa-canonical-store-search-v1"}};
  return {.schema_version = 1,
          .search_version = "saa-canonical-store-search-v1",
          .query_signature = query_signature,
          .candidates = std::move(candidates),
          .excluded = std::move(excluded),
          .selected_canonical_id = std::move(selected),
          .status = status,
          .result_signature = contracts::sha256_json(result_payload)};
}

void CanonicalAlgorithmStore::rebuild_projection() {
  auto database = open_database(projection_path_);
  create_tables(database.get());
  execute(database.get(), "BEGIN IMMEDIATE");
  try {
    execute(database.get(), "DELETE FROM canonical_algorithms");
    execute(database.get(), "DELETE FROM canonical_algorithm_sources");
    execute(database.get(), "DELETE FROM canonical_algorithm_relations");
    execute(database.get(), "DELETE FROM canonical_store_metadata");
    execute(database.get(), "DELETE FROM canonical_algorithm_fts");
    auto algorithm_insert = prepare(
        database.get(),
        "INSERT INTO canonical_algorithms VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (const auto &path : json_files(algorithm_root_)) {
      const Json envelope = read_envelope(path);
      const Json &payload = envelope.at("payload");
      const std::string id = envelope.at("object_id").get<std::string>();
      const auto parts = contracts::parse_typed_id(id);
      if (parts.object_type != "canonical-algorithm" ||
          payload.at("representative_behavior_signature") != parts.digest) {
        canonical_store_error("invalid canonical algorithm entry: " +
                              path.string());
      }
      sqlite3_reset(algorithm_insert.get());
      sqlite3_clear_bindings(algorithm_insert.get());
      bind_text(algorithm_insert.get(), 1, id);
      bind_text(algorithm_insert.get(), 2, parts.digest);
      bind_text(algorithm_insert.get(), 3,
                payload.at("mathematical_representative_signature")
                    .get_ref<const std::string &>());
      bind_text(algorithm_insert.get(), 4,
                payload.at("semantic_representative_signature")
                    .get_ref<const std::string &>());
      bind_text(algorithm_insert.get(), 5,
                envelope.value("canonical_algorithm_signature", ""));
      bind_text(algorithm_insert.get(), 6,
                payload.at("representative_version")
                    .get_ref<const std::string &>());
      bind_text(algorithm_insert.get(), 7,
                payload.at("domain").get_ref<const std::string &>());
      bind_int(algorithm_insert.get(), 8,
               payload.at("output_count").get<int>());
      bind_int(algorithm_insert.get(), 9,
               payload.at("representative_input_count").get<int>());
      bind_int(algorithm_insert.get(), 10,
               envelope.at("store_generation").get<int>());
      bind_text(algorithm_insert.get(), 11,
                contracts::canonical_json(payload));
      bind_text(algorithm_insert.get(), 12,
                path.lexically_relative(state_root_).generic_string());
      bind_text(algorithm_insert.get(), 13,
                envelope.at("created_at").get_ref<const std::string &>());
      step_done(database.get(), algorithm_insert.get());
      auto fts_insert = prepare(
          database.get(),
          "INSERT INTO canonical_algorithm_fts(canonical_id,domain,meanings) VALUES(?,?,?)");
      bind_text(fts_insert.get(), 1, id);
      bind_text(fts_insert.get(), 2,
                payload.at("domain").get_ref<const std::string &>());
      bind_text(fts_insert.get(), 3,
                newline_joined(payload_meanings(payload)));
      step_done(database.get(), fts_insert.get());
    }
    auto source_insert = prepare(
        database.get(),
        "INSERT INTO canonical_algorithm_sources VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (const auto &path : json_files(source_root_)) {
      const Json envelope = read_envelope(path);
      const Json &payload = envelope.at("payload");
      sqlite3_reset(source_insert.get());
      sqlite3_clear_bindings(source_insert.get());
      bind_text(source_insert.get(), 1,
                envelope.at("object_id").get_ref<const std::string &>());
      bind_text(source_insert.get(), 2,
                payload.at("canonical_id").get_ref<const std::string &>());
      bind_text(source_insert.get(), 3,
                payload.at("canonical_algorithm_signature")
                    .get_ref<const std::string &>());
      bind_text(source_insert.get(), 4,
                payload.at("source_structural_hash")
                    .get_ref<const std::string &>());
      bind_text(source_insert.get(), 5,
                payload.at("source_mimo_signature")
                    .get_ref<const std::string &>());
      bind_text(source_insert.get(), 6,
                payload.at("source_normalization_signature")
                    .get_ref<const std::string &>());
      bind_text(source_insert.get(), 7,
                payload.at("representative_candidate_signature")
                    .get_ref<const std::string &>());
      bind_text(source_insert.get(), 8,
                payload.at("representative_search_audit_hash")
                    .get_ref<const std::string &>());
      bind_text(source_insert.get(), 9,
                payload.at("form_audit_hash").get_ref<const std::string &>());
      bind_text(source_insert.get(), 10,
                payload.at("proof_signature").get_ref<const std::string &>());
      bind_int(source_insert.get(), 11,
               payload.at("store_generation").get<int>());
      bind_text(source_insert.get(), 12,
                contracts::canonical_json(payload));
      bind_text(source_insert.get(), 13,
                path.lexically_relative(state_root_).generic_string());
      bind_text(source_insert.get(), 14,
                payload.at("created_at").get_ref<const std::string &>());
      step_done(database.get(), source_insert.get());
    }
    auto relation_insert = prepare(
        database.get(),
        "INSERT INTO canonical_algorithm_relations VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (const auto &path : json_files(relation_root_)) {
      const Json envelope = read_envelope(path);
      const Json &payload = envelope.at("payload");
      sqlite3_reset(relation_insert.get());
      sqlite3_clear_bindings(relation_insert.get());
      for (const auto &[index, field] :
           std::array<std::pair<int, std::string_view>, 8>{
               std::pair{1, "relation_id"}, std::pair{2, "relation_type"},
               std::pair{3, "source_ref"}, std::pair{4, "source_kind"},
               std::pair{5, "target_ref"}, std::pair{6, "target_kind"},
               std::pair{7, "basis"}, std::pair{8, "basis_signature"}}) {
        bind_text(relation_insert.get(), index,
                  payload.at(field).get_ref<const std::string &>());
      }
      bind_text(relation_insert.get(), 9,
                contracts::canonical_json(payload.at("evidence_ids")));
      bind_int(relation_insert.get(), 10,
               payload.at("store_generation").get<int>());
      bind_text(relation_insert.get(), 11,
                contracts::canonical_json(payload));
      bind_text(relation_insert.get(), 12,
                path.lexically_relative(state_root_).generic_string());
      bind_text(relation_insert.get(), 13,
                payload.at("created_at").get_ref<const std::string &>());
      step_done(database.get(), relation_insert.get());
    }
    auto metadata = prepare(
        database.get(),
        "INSERT INTO canonical_store_metadata(key,value) VALUES(?,?)");
    for (const auto &[key, value] :
         std::array<std::pair<std::string, std::string>, 2>{
             std::pair{"schema_version",
                       std::to_string(
                           canonical_algorithm_store_schema_version)},
             std::pair{"rebuilt_at", utc_now()}}) {
      sqlite3_reset(metadata.get());
      sqlite3_clear_bindings(metadata.get());
      bind_text(metadata.get(), 1, key);
      bind_text(metadata.get(), 2, value);
      step_done(database.get(), metadata.get());
    }
    execute(database.get(), "COMMIT");
  } catch (...) {
    try {
      execute(database.get(), "ROLLBACK");
    } catch (...) {
    }
    throw;
  }
}

Json to_json(const CanonicalLookupResult &value) {
  return {{"status", value.status},
          {"exact_equivalent_ids", value.exact_equivalent_ids},
          {"mathematical_match_ids", value.mathematical_match_ids},
          {"semantic_match_ids", value.semantic_match_ids},
          {"source_bound_match_ids", value.source_bound_match_ids},
          {"unique", value.unique()}};
}

Json to_json(const CanonicalAdmissionResult &value) {
  return {{"status", value.status},
          {"canonical_id", value.canonical_id},
          {"source_id", value.source_id},
          {"store_generation", value.store_generation},
          {"lookup", to_json(value.lookup)},
          {"relation_ids", value.relation_ids},
          {"admitted_new", value.admitted_new()}};
}

Json to_json(const AlgorithmRelationRecord &value) {
  return {{"basis", value.basis},
          {"basis_signature", value.basis_signature},
          {"created_at", value.created_at},
          {"evidence_ids", value.evidence_ids},
          {"relation_id", value.relation_id},
          {"relation_type", value.relation_type},
          {"source_kind", value.source_kind},
          {"source_ref", value.source_ref},
          {"store_generation", value.store_generation},
          {"target_kind", value.target_kind},
          {"target_ref", value.target_ref}};
}

Json to_json(const CanonicalAlgorithmQuery &value) {
  return {{"domain", value.domain ? Json(*value.domain) : Json(nullptr)},
          {"input_count",
           value.input_count ? Json(*value.input_count) : Json(nullptr)},
          {"lexical_terms", value.lexical_terms},
          {"limit", value.limit},
          {"mathematical_signature",
           value.mathematical_signature ? Json(*value.mathematical_signature)
                                        : Json(nullptr)},
          {"output_count",
           value.output_count ? Json(*value.output_count) : Json(nullptr)},
          {"representative_behavior_signature",
           value.representative_behavior_signature
               ? Json(*value.representative_behavior_signature)
               : Json(nullptr)},
          {"semantic_meanings", value.semantic_meanings},
          {"semantic_signature",
           value.semantic_signature ? Json(*value.semantic_signature)
                                    : Json(nullptr)},
          {"source_structural_hash",
           value.source_structural_hash ? Json(*value.source_structural_hash)
                                        : Json(nullptr)}};
}

Json to_json(const CanonicalAlgorithmSearchResult &value) {
  return {{"candidates", value.candidates},
          {"excluded", value.excluded},
          {"query_signature", value.query_signature},
          {"result_signature", value.result_signature},
          {"schema_version", value.schema_version},
          {"search_version", value.search_version},
          {"selected_canonical_id",
           value.selected_canonical_id ? Json(*value.selected_canonical_id)
                                       : Json(nullptr)},
          {"status", value.status}};
}

} // namespace statewright::egcf
