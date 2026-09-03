#include "statewright/egcf/reasoning_store.hpp"

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
#include <memory>
#include <utility>

namespace statewright::egcf {
namespace {

using Json = contracts::Json;

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

[[noreturn]] void reasoning_store_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument,
                      std::move(message));
}

[[nodiscard]] std::string normalized_text(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n\f\v");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n\f\v");
  value = value.substr(first, last - first + 1U);
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
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
    reasoning_store_error(std::string(label) +
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
    reasoning_store_error("cannot format canonical reasoning timestamp");
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
    reasoning_store_error("cannot open canonical reasoning projection: " +
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
    reasoning_store_error("canonical reasoning projection SQL failed: " +
                          text);
  }
}

[[nodiscard]] Statement prepare(sqlite3 *database, std::string_view sql) {
  sqlite3_stmt *raw = nullptr;
  if (sqlite3_prepare_v2(database, std::string(sql).c_str(), -1, &raw,
                         nullptr) != SQLITE_OK) {
    reasoning_store_error("cannot prepare canonical reasoning SQL: " +
                          std::string(sqlite3_errmsg(database)));
  }
  return Statement(raw);
}

void bind_text(sqlite3_stmt *statement, int index, std::string_view value) {
  if (sqlite3_bind_text(statement, index, value.data(),
                        static_cast<int>(value.size()), SQLITE_TRANSIENT) !=
      SQLITE_OK) {
    reasoning_store_error("cannot bind canonical reasoning text");
  }
}

void bind_int(sqlite3_stmt *statement, int index, int value) {
  if (sqlite3_bind_int(statement, index, value) != SQLITE_OK) {
    reasoning_store_error("cannot bind canonical reasoning integer");
  }
}

void step_done(sqlite3 *database, sqlite3_stmt *statement) {
  if (sqlite3_step(statement) != SQLITE_DONE) {
    reasoning_store_error("cannot update canonical reasoning projection: " +
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
    CREATE TABLE IF NOT EXISTS canonical_reasoning_algorithms (
      reasoning_id TEXT PRIMARY KEY,
      canonical_reasoning_signature TEXT NOT NULL UNIQUE,
      topology_signature TEXT NOT NULL,
      semantic_signature TEXT NOT NULL,
      canonicalization_strength TEXT NOT NULL,
      input_json TEXT NOT NULL,
      output_json TEXT NOT NULL,
      applicability_json TEXT NOT NULL,
      max_steps INTEGER NOT NULL,
      store_generation INTEGER NOT NULL,
      payload_json TEXT NOT NULL,
      path TEXT NOT NULL,
      created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS canonical_reasoning_topology_idx
      ON canonical_reasoning_algorithms(topology_signature);
    CREATE INDEX IF NOT EXISTS canonical_reasoning_semantic_idx
      ON canonical_reasoning_algorithms(semantic_signature);
    CREATE TABLE IF NOT EXISTS canonical_reasoning_qualifications (
      qualification_id TEXT PRIMARY KEY,
      reasoning_id TEXT NOT NULL,
      outcome_signature TEXT NOT NULL,
      qualification_signature TEXT NOT NULL,
      evidence_json TEXT NOT NULL,
      payload_json TEXT NOT NULL,
      path TEXT NOT NULL,
      created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS canonical_reasoning_qual_reasoning_idx
      ON canonical_reasoning_qualifications(reasoning_id);
    CREATE TABLE IF NOT EXISTS canonical_reasoning_metadata (
      key TEXT PRIMARY KEY,
      value TEXT NOT NULL
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

void immutable_write(const std::filesystem::path &path, const Json &envelope) {
  if (std::filesystem::exists(path)) {
    const Json existing = contracts::parse_json(core::read_text(path));
    for (const std::string_view field : {"schema_version", "store_version",
                                         "object_id", "payload"}) {
      if (existing.at(field) != envelope.at(field)) {
        reasoning_store_error("immutable reasoning-store collision at " +
                              path.string());
      }
    }
    if (envelope.contains("store_generation") &&
        existing.at("store_generation") != envelope.at("store_generation")) {
      reasoning_store_error("immutable reasoning-store collision at " +
                            path.string());
    }
    return;
  }
  core::atomic_write_text(path, envelope.dump(2) + "\n");
}

[[nodiscard]] Json read_envelope(const std::filesystem::path &path) {
  return contracts::parse_json(core::read_text(path));
}

[[nodiscard]] std::string reasoning_id(std::string signature) {
  return "canonical-reasoning:sha256:" +
         exact_sha(std::move(signature), "canonical reasoning signature");
}

[[nodiscard]] std::string qualification_id(std::string signature) {
  return "reasoning-qualification:sha256:" + exact_sha(
             std::move(signature), "reasoning qualification signature");
}

[[nodiscard]] saa::CanonicalReasoningAlgorithm
algorithm_from_payload(const Json &payload) {
  return {.schema_version = payload.at("schema_version").get<int>(),
          .reasoning_version =
              payload.at("reasoning_version").get<std::string>(),
          .input_semantics =
              payload.at("input_semantics").get<std::vector<std::string>>(),
          .output_semantics =
              payload.at("output_semantics").get<std::vector<std::string>>(),
          .canonical_nodes =
              payload.at("canonical_nodes").get<std::vector<Json>>(),
          .canonical_edges =
              payload.at("canonical_edges").get<std::vector<Json>>(),
          .invariants =
              payload.at("invariants").get<std::vector<std::string>>(),
          .termination = payload.at("termination"),
          .applicability =
              payload.at("applicability").get<std::vector<std::string>>(),
          .topology_signature =
              payload.at("topology_signature").get<std::string>(),
          .semantic_signature =
              payload.at("semantic_signature").get<std::string>(),
          .canonical_reasoning_signature =
              payload.at("canonical_reasoning_signature").get<std::string>(),
          .canonicalization_strength =
              payload.at("canonicalization_strength").get<std::string>(),
          .canonical_permutations_evaluated =
              payload.at("canonical_permutations_evaluated")
                  .get<std::size_t>(),
          .public_artifact_only =
              payload.at("public_artifact_only").get<bool>(),
          .warnings = payload.value("warnings", std::vector<std::string>{})};
}

} // namespace

CanonicalReasoningStore::CanonicalReasoningStore(EgcfStore &egcf_store)
    : egcf_store_(egcf_store), state_root_(egcf_store.state_root()),
      root_(state_root_ / "canonical-reasoning"),
      algorithm_root_(root_ / "objects" / "sha256"),
      qualification_root_(root_ / "qualifications" / "sha256"),
      projection_path_(egcf_store.projection_path()) {
  std::filesystem::create_directories(algorithm_root_);
  std::filesystem::create_directories(qualification_root_);
  ensure_projection();
}

const std::filesystem::path &CanonicalReasoningStore::root() const noexcept {
  return root_;
}

saa::ReasoningEvidenceResolver
CanonicalReasoningStore::evidence_resolver() const {
  return [this](std::string_view evidence_id_value)
             -> std::optional<saa::ReasoningGroundingEvidence> {
    try {
      const auto record = egcf_store_.get(evidence_id_value);
      saa::ReasoningGroundingEvidence result;
      result.object_type = record.object_type;
      if (record.payload.contains("success") &&
          !record.payload.at("success").is_null()) {
        result.success = record.payload.at("success").get<bool>();
      }
      result.simulated = record.payload.value("simulated", false);
      result.producer = record.payload.value("producer", "");
      result.method = record.payload.value("method", "");
      result.requirement_ids = record.payload.value(
          "requirement_ids", std::vector<std::string>{});
      result.independence_group =
          record.payload.value("independence_group", "");
      return result;
    } catch (const std::exception &) {
      return std::nullopt;
    }
  };
}

std::filesystem::path CanonicalReasoningStore::algorithm_path(
    std::string_view reasoning_id_value) const {
  const auto parts = contracts::parse_typed_id(reasoning_id_value);
  if (parts.object_type != "canonical-reasoning") {
    reasoning_store_error("reasoning store ID has wrong type");
  }
  return algorithm_root_ / parts.digest.substr(0, 2) /
         (parts.digest + ".json");
}

std::filesystem::path CanonicalReasoningStore::qualification_path(
    std::string_view qualification_id_value) const {
  const auto parts = contracts::parse_typed_id(qualification_id_value);
  if (parts.object_type != "reasoning-qualification") {
    reasoning_store_error("reasoning qualification ID has wrong type");
  }
  return qualification_root_ / parts.digest.substr(0, 2) /
         (parts.digest + ".json");
}

void CanonicalReasoningStore::verify_algorithm(
    const saa::CanonicalReasoningAlgorithm &algorithm) const {
  if (algorithm.reasoning_version != saa::reasoning_algebra_version) {
    reasoning_store_error("unsupported reasoning algebra version");
  }
  if (algorithm.canonicalization_strength !=
      "EXACT_BOUNDED_GRAPH_CANONICALIZATION") {
    reasoning_store_error(
        "canonical reasoning store admits only exact bounded graph canonicalization");
  }
  if (!algorithm.public_artifact_only) {
    reasoning_store_error(
        "canonical reasoning store accepts public reasoning artifacts only");
  }
  static_cast<void>(
      exact_sha(algorithm.topology_signature, "topology signature"));
  static_cast<void>(
      exact_sha(algorithm.semantic_signature, "semantic signature"));
  static_cast<void>(exact_sha(algorithm.canonical_reasoning_signature,
                              "canonical reasoning signature"));
  const std::string expected = contracts::sha256_json(
      {{"canonicalization_strength", algorithm.canonicalization_strength},
       {"semantic_signature", algorithm.semantic_signature},
       {"topology_signature", algorithm.topology_signature},
       {"version", saa::reasoning_algebra_version}});
  if (expected != algorithm.canonical_reasoning_signature) {
    reasoning_store_error("canonical reasoning signature mismatch");
  }
  if (!algorithm.termination.is_object() ||
      algorithm.termination.value("max_steps", 0) < 1) {
    reasoning_store_error(
        "canonical reasoning algorithm has no bounded termination");
  }
}

void CanonicalReasoningStore::verify_qualification(
    const saa::CanonicalReasoningAlgorithm &algorithm,
    const saa::ReasoningOutcomeQualification &qualification) const {
  if (qualification.canonical_reasoning_signature !=
      algorithm.canonical_reasoning_signature) {
    reasoning_store_error(
        "reasoning qualification belongs to a different algorithm");
  }
  if (qualification.status != "QUALIFIED_REASONING_OUTCOME") {
    reasoning_store_error(
        "reasoning store admission requires QUALIFIED_REASONING_OUTCOME");
  }
  if (!qualification.canonical_reuse_eligible) {
    reasoning_store_error(
        "reasoning outcome is not canonical-reuse eligible");
  }
  if (qualification.evidence_requirement_coverage_bp != 10000) {
    reasoning_store_error(
        "reasoning qualification has incomplete evidence coverage");
  }
  if (qualification.grounded_evidence_ids.empty() ||
      qualification.independence_groups.empty()) {
    reasoning_store_error(
        "reasoning qualification lacks grounded independent evidence");
  }
  static_cast<void>(exact_sha(qualification.qualification_signature,
                              "reasoning qualification signature"));
}

void CanonicalReasoningStore::ensure_projection() {
  auto database = open_database(projection_path_);
  create_tables(database.get());
  auto marker = prepare(
      database.get(),
      "SELECT value FROM canonical_reasoning_metadata WHERE key='schema_version'");
  const bool current = sqlite3_step(marker.get()) == SQLITE_ROW &&
                       column_text(marker.get(), 0) ==
                           std::to_string(
                               canonical_reasoning_store_schema_version);
  marker.reset();
  database.reset();
  if (!current) {
    rebuild_projection();
  }
}

int CanonicalReasoningStore::current_generation() {
  ensure_projection();
  auto database = open_database(projection_path_);
  auto statement = prepare(
      database.get(),
      "SELECT COALESCE(MAX(store_generation),0) FROM canonical_reasoning_algorithms");
  if (sqlite3_step(statement.get()) != SQLITE_ROW) {
    reasoning_store_error("cannot read canonical reasoning generation");
  }
  return sqlite3_column_int(statement.get(), 0);
}

ReasoningStoreLookup CanonicalReasoningStore::lookup(
    const saa::CanonicalReasoningAlgorithm &algorithm) {
  verify_algorithm(algorithm);
  ensure_projection();
  auto database = open_database(projection_path_);
  const auto query_ids = [&](std::string_view sql,
                             std::string_view first,
                             std::optional<std::string_view> second) {
    std::vector<std::string> result;
    auto statement = prepare(database.get(), sql);
    bind_text(statement.get(), 1, first);
    if (second) {
      bind_text(statement.get(), 2, *second);
    }
    while (sqlite3_step(statement.get()) == SQLITE_ROW) {
      result.push_back(column_text(statement.get(), 0));
    }
    return result;
  };
  auto exact = query_ids(
      "SELECT reasoning_id FROM canonical_reasoning_algorithms WHERE canonical_reasoning_signature=? ORDER BY reasoning_id",
      algorithm.canonical_reasoning_signature, std::nullopt);
  auto topology = query_ids(
      "SELECT reasoning_id FROM canonical_reasoning_algorithms WHERE topology_signature=? AND canonical_reasoning_signature!=? ORDER BY reasoning_id",
      algorithm.topology_signature, algorithm.canonical_reasoning_signature);
  auto semantic = query_ids(
      "SELECT reasoning_id FROM canonical_reasoning_algorithms WHERE semantic_signature=? AND canonical_reasoning_signature!=? ORDER BY reasoning_id",
      algorithm.semantic_signature, algorithm.canonical_reasoning_signature);
  std::string status;
  if (!exact.empty()) {
    status = "REASONING_EQUIVALENT_ALREADY_STORED";
  } else if (!topology.empty() && !semantic.empty()) {
    status = "MULTIPLE_REASONING_NEIGHBOR_MATCHES";
  } else if (!topology.empty()) {
    status = "REASONING_TOPOLOGY_MATCH_SEMANTIC_DIFFERENCE";
  } else if (!semantic.empty()) {
    status = "REASONING_SEMANTIC_MATCH_TOPOLOGY_DIFFERENCE";
  } else {
    status = "UNIQUE_CANONICAL_REASONING_CANDIDATE";
  }
  return {std::move(status), std::move(exact), std::move(topology),
          std::move(semantic)};
}

std::string CanonicalReasoningStore::persist_qualification(
    std::string_view reasoning_id_value,
    const saa::ReasoningOutcomeQualification &qualification) {
  const std::string id = qualification_id(qualification.qualification_signature);
  Json payload = saa::to_json(qualification);
  payload["reasoning_id"] = reasoning_id_value;
  const Json envelope = {{"created_at", utc_now()},
                         {"object_id", id},
                         {"payload", payload},
                         {"schema_version", 1},
                         {"store_version", canonical_reasoning_store_version}};
  const auto path = qualification_path(id);
  immutable_write(path, envelope);
  const Json stored = read_envelope(path);
  auto database = open_database(projection_path_);
  create_tables(database.get());
  auto statement = prepare(
      database.get(),
      "INSERT OR IGNORE INTO canonical_reasoning_qualifications VALUES(?,?,?,?,?,?,?,?)");
  bind_text(statement.get(), 1, id);
  bind_text(statement.get(), 2, reasoning_id_value);
  bind_text(statement.get(), 3, qualification.outcome_signature);
  bind_text(statement.get(), 4, qualification.qualification_signature);
  bind_text(statement.get(), 5,
            contracts::canonical_json(qualification.grounded_evidence_ids));
  bind_text(statement.get(), 6, contracts::canonical_json(payload));
  bind_text(statement.get(), 7,
            path.lexically_relative(state_root_).generic_string());
  bind_text(statement.get(), 8,
            stored.at("created_at").get_ref<const std::string &>());
  step_done(database.get(), statement.get());
  return id;
}

ReasoningStoreAdmission CanonicalReasoningStore::admit(
    const saa::CanonicalReasoningAlgorithm &algorithm,
    const saa::ReasoningOutcomeQualification &qualification) {
  verify_algorithm(algorithm);
  verify_qualification(algorithm, qualification);
  auto existing = lookup(algorithm);
  if (!existing.exact_ids.empty()) {
    const std::string id = existing.exact_ids.front();
    const std::string qualification_value =
        persist_qualification(id, qualification);
    return {"REUSED_EXISTING_CANONICAL_REASONING", id,
            qualification_value, generation_for(id), std::move(existing)};
  }

  const int generation = current_generation() + 1;
  const std::string id = reasoning_id(algorithm.canonical_reasoning_signature);
  const Json payload = saa::to_json(algorithm);
  const Json envelope = {{"created_at", utc_now()},
                         {"object_id", id},
                         {"payload", payload},
                         {"schema_version", 1},
                         {"store_generation", generation},
                         {"store_version", canonical_reasoning_store_version}};
  const auto path = algorithm_path(id);
  immutable_write(path, envelope);
  const Json stored = read_envelope(path);
  auto database = open_database(projection_path_);
  create_tables(database.get());
  auto statement = prepare(
      database.get(),
      "INSERT INTO canonical_reasoning_algorithms VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)");
  bind_text(statement.get(), 1, id);
  bind_text(statement.get(), 2, algorithm.canonical_reasoning_signature);
  bind_text(statement.get(), 3, algorithm.topology_signature);
  bind_text(statement.get(), 4, algorithm.semantic_signature);
  bind_text(statement.get(), 5, algorithm.canonicalization_strength);
  bind_text(statement.get(), 6,
            contracts::canonical_json(algorithm.input_semantics));
  bind_text(statement.get(), 7,
            contracts::canonical_json(algorithm.output_semantics));
  bind_text(statement.get(), 8,
            contracts::canonical_json(algorithm.applicability));
  bind_int(statement.get(), 9, algorithm.termination.at("max_steps").get<int>());
  bind_int(statement.get(), 10, generation);
  bind_text(statement.get(), 11, contracts::canonical_json(payload));
  bind_text(statement.get(), 12,
            path.lexically_relative(state_root_).generic_string());
  bind_text(statement.get(), 13,
            stored.at("created_at").get_ref<const std::string &>());
  step_done(database.get(), statement.get());
  auto marker = prepare(
      database.get(),
      "INSERT OR REPLACE INTO canonical_reasoning_metadata(key,value) VALUES('schema_version',?)");
  bind_text(marker.get(), 1,
            std::to_string(canonical_reasoning_store_schema_version));
  step_done(database.get(), marker.get());
  marker.reset();
  statement.reset();
  database.reset();
  const std::string qualification_value = persist_qualification(id, qualification);
  return {"ADMITTED_NEW_CANONICAL_REASONING", id, qualification_value,
          generation, std::move(existing)};
}

int CanonicalReasoningStore::generation_for(std::string_view reasoning_id_value) {
  ensure_projection();
  auto database = open_database(projection_path_);
  auto statement = prepare(
      database.get(),
      "SELECT store_generation FROM canonical_reasoning_algorithms WHERE reasoning_id=?");
  bind_text(statement.get(), 1, reasoning_id_value);
  if (sqlite3_step(statement.get()) != SQLITE_ROW) {
    reasoning_store_error("unknown canonical reasoning algorithm: " +
                          std::string(reasoning_id_value));
  }
  return sqlite3_column_int(statement.get(), 0);
}

Json CanonicalReasoningStore::get(std::string_view reasoning_id_value) const {
  const auto path = algorithm_path(reasoning_id_value);
  Json envelope;
  try {
    envelope = read_envelope(path);
  } catch (const std::exception &error) {
    reasoning_store_error("cannot read canonical reasoning algorithm " +
                          std::string(reasoning_id_value) + ": " +
                          error.what());
  }
  if (envelope.at("object_id") != reasoning_id_value) {
    reasoning_store_error("canonical reasoning object identity mismatch");
  }
  const Json &payload = envelope.at("payload");
  if (!payload.is_object()) {
    reasoning_store_error("canonical reasoning payload is invalid");
  }
  const auto parts = contracts::parse_typed_id(reasoning_id_value);
  if (payload.at("canonical_reasoning_signature") != parts.digest) {
    reasoning_store_error(
        "canonical reasoning signature does not match object ID");
  }
  return envelope;
}

saa::CanonicalReasoningAlgorithm CanonicalReasoningStore::load_algorithm(
    std::string_view reasoning_id_value) const {
  auto algorithm = algorithm_from_payload(get(reasoning_id_value).at("payload"));
  verify_algorithm(algorithm);
  return algorithm;
}

std::vector<std::string>
CanonicalReasoningStore::list_reasoning_ids() const {
  auto database = open_database(projection_path_);
  create_tables(database.get());
  auto statement = prepare(
      database.get(),
      "SELECT reasoning_id FROM canonical_reasoning_algorithms ORDER BY store_generation,reasoning_id");
  std::vector<std::string> result;
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    result.push_back(column_text(statement.get(), 0));
  }
  return result;
}

saa::CanonicalReasoningAlgorithm
CanonicalReasoningStore::load_reasoning_algorithm(
    std::string_view reasoning_id_value) const {
  return load_algorithm(reasoning_id_value);
}

std::vector<Json> CanonicalReasoningStore::list() {
  ensure_projection();
  auto database = open_database(projection_path_);
  auto statement = prepare(
      database.get(),
      "SELECT reasoning_id,store_generation,payload_json,created_at FROM canonical_reasoning_algorithms ORDER BY store_generation,reasoning_id");
  std::vector<Json> result;
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    result.push_back({{"reasoning_id", column_text(statement.get(), 0)},
                      {"store_generation", sqlite3_column_int(statement.get(), 1)},
                      {"payload", contracts::parse_json(column_text(statement.get(), 2))},
                      {"created_at", column_text(statement.get(), 3)}});
  }
  return result;
}

std::vector<Json> CanonicalReasoningStore::qualifications(
    std::optional<std::string> reasoning_id_value) {
  ensure_projection();
  auto database = open_database(projection_path_);
  auto statement = reasoning_id_value
                       ? prepare(database.get(),
                                 "SELECT payload_json FROM canonical_reasoning_qualifications WHERE reasoning_id=? ORDER BY qualification_id")
                       : prepare(database.get(),
                                 "SELECT payload_json FROM canonical_reasoning_qualifications ORDER BY qualification_id");
  if (reasoning_id_value) {
    bind_text(statement.get(), 1, *reasoning_id_value);
  }
  std::vector<Json> result;
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    result.push_back(contracts::parse_json(column_text(statement.get(), 0)));
  }
  return result;
}

void CanonicalReasoningStore::rebuild_projection() {
  auto database = open_database(projection_path_);
  create_tables(database.get());
  execute(database.get(), "BEGIN IMMEDIATE");
  try {
    execute(database.get(), "DELETE FROM canonical_reasoning_algorithms");
    execute(database.get(), "DELETE FROM canonical_reasoning_qualifications");
    execute(database.get(), "DELETE FROM canonical_reasoning_metadata");

    auto algorithm_insert = prepare(
        database.get(),
        "INSERT INTO canonical_reasoning_algorithms VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (const auto &path : json_files(algorithm_root_)) {
      const Json envelope = read_envelope(path);
      const Json &payload = envelope.at("payload");
      const std::string id = envelope.at("object_id").get<std::string>();
      const auto parts = contracts::parse_typed_id(id);
      if (parts.object_type != "canonical-reasoning" ||
          payload.at("canonical_reasoning_signature") != parts.digest) {
        reasoning_store_error("invalid canonical reasoning entry: " +
                              path.string());
      }
      auto algorithm = algorithm_from_payload(payload);
      verify_algorithm(algorithm);
      sqlite3_reset(algorithm_insert.get());
      sqlite3_clear_bindings(algorithm_insert.get());
      bind_text(algorithm_insert.get(), 1, id);
      bind_text(algorithm_insert.get(), 2,
                algorithm.canonical_reasoning_signature);
      bind_text(algorithm_insert.get(), 3, algorithm.topology_signature);
      bind_text(algorithm_insert.get(), 4, algorithm.semantic_signature);
      bind_text(algorithm_insert.get(), 5,
                algorithm.canonicalization_strength);
      bind_text(algorithm_insert.get(), 6,
                contracts::canonical_json(algorithm.input_semantics));
      bind_text(algorithm_insert.get(), 7,
                contracts::canonical_json(algorithm.output_semantics));
      bind_text(algorithm_insert.get(), 8,
                contracts::canonical_json(algorithm.applicability));
      bind_int(algorithm_insert.get(), 9,
               algorithm.termination.at("max_steps").get<int>());
      bind_int(algorithm_insert.get(), 10,
               envelope.at("store_generation").get<int>());
      bind_text(algorithm_insert.get(), 11,
                contracts::canonical_json(payload));
      bind_text(algorithm_insert.get(), 12,
                path.lexically_relative(state_root_).generic_string());
      bind_text(algorithm_insert.get(), 13,
                envelope.at("created_at").get_ref<const std::string &>());
      step_done(database.get(), algorithm_insert.get());
    }

    auto qualification_insert = prepare(
        database.get(),
        "INSERT INTO canonical_reasoning_qualifications VALUES(?,?,?,?,?,?,?,?)");
    for (const auto &path : json_files(qualification_root_)) {
      const Json envelope = read_envelope(path);
      const Json &payload = envelope.at("payload");
      const std::string id = envelope.at("object_id").get<std::string>();
      const auto parts = contracts::parse_typed_id(id);
      if (parts.object_type != "reasoning-qualification" ||
          payload.at("qualification_signature") != parts.digest) {
        reasoning_store_error("invalid reasoning qualification entry: " +
                              path.string());
      }
      sqlite3_reset(qualification_insert.get());
      sqlite3_clear_bindings(qualification_insert.get());
      bind_text(qualification_insert.get(), 1, id);
      bind_text(qualification_insert.get(), 2,
                payload.at("reasoning_id").get_ref<const std::string &>());
      bind_text(qualification_insert.get(), 3,
                payload.at("outcome_signature").get_ref<const std::string &>());
      bind_text(
          qualification_insert.get(), 4,
          payload.at("qualification_signature").get_ref<const std::string &>());
      bind_text(qualification_insert.get(), 5,
                contracts::canonical_json(
                    payload.at("grounded_evidence_ids")));
      bind_text(qualification_insert.get(), 6,
                contracts::canonical_json(payload));
      bind_text(qualification_insert.get(), 7,
                path.lexically_relative(state_root_).generic_string());
      bind_text(qualification_insert.get(), 8,
                envelope.at("created_at").get_ref<const std::string &>());
      step_done(database.get(), qualification_insert.get());
    }

    auto metadata = prepare(
        database.get(),
        "INSERT INTO canonical_reasoning_metadata(key,value) VALUES(?,?)");
    for (const auto &[key, value] :
         std::array<std::pair<std::string, std::string>, 2>{
             std::pair{"schema_version",
                       std::to_string(
                           canonical_reasoning_store_schema_version)},
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

Json to_json(const ReasoningStoreLookup &value) {
  return {{"status", value.status},
          {"exact_ids", value.exact_ids},
          {"topology_match_ids", value.topology_match_ids},
          {"semantic_match_ids", value.semantic_match_ids},
          {"unique", value.unique()}};
}

Json to_json(const ReasoningStoreAdmission &value) {
  return {{"status", value.status},
          {"reasoning_id", value.reasoning_id},
          {"qualification_id", value.qualification_id},
          {"store_generation", value.store_generation},
          {"lookup", to_json(value.lookup)},
          {"admitted_new", value.admitted_new()}};
}

} // namespace statewright::egcf
