#include "statewright/saa/projection.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <memory>
#include <set>
#include <sstream>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[noreturn]] void projection_error(std::string message) {
  throw common::Error(common::ErrorCode::filesystem_failure,
                      std::move(message));
}

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
    projection_error("cannot open SAA projection database: " + message);
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
    projection_error("SAA projection SQL failed: " + text);
  }
}

[[nodiscard]] Statement prepare(sqlite3 *database, std::string_view sql) {
  sqlite3_stmt *raw = nullptr;
  if (sqlite3_prepare_v2(database, std::string(sql).c_str(), -1, &raw,
                         nullptr) != SQLITE_OK) {
    projection_error("cannot prepare SAA projection SQL: " +
                     std::string(sqlite3_errmsg(database)));
  }
  return Statement(raw);
}

void bind_text(sqlite3_stmt *statement, int index, std::string_view value) {
  if (sqlite3_bind_text(statement, index, value.data(),
                        static_cast<int>(value.size()), SQLITE_TRANSIENT) !=
      SQLITE_OK) {
    projection_error("cannot bind SAA projection text value");
  }
}

void step_done(sqlite3 *database, sqlite3_stmt *statement) {
  if (sqlite3_step(statement) != SQLITE_DONE) {
    projection_error("cannot update SAA projection: " +
                     std::string(sqlite3_errmsg(database)));
  }
}

[[nodiscard]] std::string join(const std::vector<std::string> &values) {
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
primitives(const CanonicalAlgorithmIR &ir) {
  std::set<std::string> values;
  for (const auto &node : ir.canonical_payload.at("nodes")) {
    values.insert(node.at("primitive").get<std::string>());
  }
  return {values.begin(), values.end()};
}

[[nodiscard]] std::string
authoritative_digest(const AlgorithmSearchIndex &index) {
  Json material = Json::array();
  for (const auto &[identity, algorithm] : index.algorithms()) {
    material.push_back(
        {{"canonical_algorithm_id", identity}, {"signature", algorithm.signature}});
  }
  return contracts::sha256_json(
      {{"algorithms", material},
       {"schema_version", algorithm_projection_schema_version}});
}

void initialize(sqlite3 *database) {
  execute(database, "PRAGMA foreign_keys=ON");
  execute(database, "PRAGMA journal_mode=WAL");
  execute(database, "PRAGMA synchronous=FULL");
  execute(database,
          "CREATE TABLE IF NOT EXISTS projection_metadata("
          "key TEXT PRIMARY KEY, value TEXT NOT NULL)");
  execute(database,
          "CREATE TABLE IF NOT EXISTS algorithms("
          "canonical_algorithm_id TEXT PRIMARY KEY,"
          "signature TEXT NOT NULL,"
          "structural_hash TEXT NOT NULL,"
          "semantic_signature TEXT NOT NULL,"
          "domain TEXT NOT NULL,"
          "qualification_status TEXT NOT NULL,"
          "payload_json TEXT NOT NULL)");
  execute(database,
          "CREATE INDEX IF NOT EXISTS idx_algorithms_structural_hash "
          "ON algorithms(structural_hash)");
  execute(database,
          "CREATE INDEX IF NOT EXISTS idx_algorithms_semantic_signature "
          "ON algorithms(semantic_signature)");
  execute(database,
          "CREATE VIRTUAL TABLE IF NOT EXISTS algorithm_fts USING fts5("
          "canonical_algorithm_id UNINDEXED, domain, semantic_terms, invariants,"
          "primitives, tokenize='unicode61')");
}

} // namespace

AlgorithmSearchProjection::AlgorithmSearchProjection(
    std::filesystem::path database_path)
    : database_path_(std::move(database_path)) {
  if (database_path_.empty()) {
    projection_error("SAA projection database path must be non-empty");
  }
}

const std::filesystem::path &
AlgorithmSearchProjection::database_path() const noexcept {
  return database_path_;
}

std::string AlgorithmSearchProjection::rebuild(
    const AlgorithmSearchIndex &authoritative_index) const {
  auto database = open_database(database_path_);
  initialize(database.get());
  execute(database.get(), "BEGIN IMMEDIATE");
  try {
    execute(database.get(), "DELETE FROM algorithm_fts");
    execute(database.get(), "DELETE FROM algorithms");
    execute(database.get(), "DELETE FROM projection_metadata");
    auto algorithm_insert = prepare(
        database.get(),
        "INSERT INTO algorithms(canonical_algorithm_id, signature, "
        "structural_hash, semantic_signature, domain, qualification_status, "
        "payload_json) VALUES (?, ?, ?, ?, ?, ?, ?)");
    auto fts_insert = prepare(
        database.get(),
        "INSERT INTO algorithm_fts(canonical_algorithm_id, domain, "
        "semantic_terms, invariants, primitives) VALUES (?, ?, ?, ?, ?)");
    for (const auto &[identity, algorithm] : authoritative_index.algorithms()) {
      sqlite3_reset(algorithm_insert.get());
      sqlite3_clear_bindings(algorithm_insert.get());
      bind_text(algorithm_insert.get(), 1, identity);
      bind_text(algorithm_insert.get(), 2, algorithm.signature);
      bind_text(algorithm_insert.get(), 3,
                algorithm.structural_ir.structural_hash);
      bind_text(algorithm_insert.get(), 4, algorithm.semantic_signature);
      bind_text(algorithm_insert.get(), 5, algorithm.domain);
      bind_text(algorithm_insert.get(), 6, algorithm.qualification_status);
      const std::string payload = contracts::canonical_json(to_json(algorithm));
      bind_text(algorithm_insert.get(), 7, payload);
      step_done(database.get(), algorithm_insert.get());

      sqlite3_reset(fts_insert.get());
      sqlite3_clear_bindings(fts_insert.get());
      bind_text(fts_insert.get(), 1, identity);
      bind_text(fts_insert.get(), 2, algorithm.domain);
      const std::string semantic_terms = join(algorithm.semantic_terms);
      const std::string invariants = join(algorithm.invariants);
      const std::string primitive_text = join(primitives(algorithm.structural_ir));
      bind_text(fts_insert.get(), 3, semantic_terms);
      bind_text(fts_insert.get(), 4, invariants);
      bind_text(fts_insert.get(), 5, primitive_text);
      step_done(database.get(), fts_insert.get());
    }
    const std::string digest = authoritative_digest(authoritative_index);
    auto metadata_insert = prepare(
        database.get(),
        "INSERT INTO projection_metadata(key, value) VALUES (?, ?)");
    bind_text(metadata_insert.get(), 1, "schema_version");
    bind_text(metadata_insert.get(), 2,
              std::to_string(algorithm_projection_schema_version));
    step_done(database.get(), metadata_insert.get());
    sqlite3_reset(metadata_insert.get());
    sqlite3_clear_bindings(metadata_insert.get());
    bind_text(metadata_insert.get(), 1, "authoritative_digest");
    bind_text(metadata_insert.get(), 2, digest);
    step_done(database.get(), metadata_insert.get());
    execute(database.get(), "COMMIT");
    return digest;
  } catch (...) {
    sqlite3_exec(database.get(), "ROLLBACK", nullptr, nullptr, nullptr);
    throw;
  }
}

void AlgorithmSearchProjection::verify(
    const AlgorithmSearchIndex &authoritative_index) const {
  auto database = open_database(database_path_);
  initialize(database.get());
  auto integrity = prepare(database.get(), "PRAGMA integrity_check");
  if (sqlite3_step(integrity.get()) != SQLITE_ROW ||
      std::string(reinterpret_cast<const char *>(
          sqlite3_column_text(integrity.get(), 0))) != "ok") {
    projection_error("SAA projection SQLite integrity check failed");
  }
  auto metadata = prepare(
      database.get(),
      "SELECT value FROM projection_metadata WHERE key='authoritative_digest'");
  if (sqlite3_step(metadata.get()) != SQLITE_ROW) {
    projection_error("SAA projection lacks authoritative digest");
  }
  const std::string stored(reinterpret_cast<const char *>(
      sqlite3_column_text(metadata.get(), 0)));
  if (stored != authoritative_digest(authoritative_index)) {
    projection_error("SAA projection authoritative digest mismatch");
  }
  auto count = prepare(database.get(), "SELECT COUNT(*) FROM algorithms");
  if (sqlite3_step(count.get()) != SQLITE_ROW ||
      sqlite3_column_int64(count.get(), 0) !=
          static_cast<sqlite3_int64>(authoritative_index.algorithms().size())) {
    projection_error("SAA projection row count mismatch");
  }
  auto lookup = prepare(
      database.get(),
      "SELECT signature FROM algorithms WHERE canonical_algorithm_id=?");
  for (const auto &[identity, algorithm] : authoritative_index.algorithms()) {
    sqlite3_reset(lookup.get());
    sqlite3_clear_bindings(lookup.get());
    bind_text(lookup.get(), 1, identity);
    if (sqlite3_step(lookup.get()) != SQLITE_ROW) {
      projection_error("SAA projection is missing algorithm: " + identity);
    }
    const std::string signature(reinterpret_cast<const char *>(
        sqlite3_column_text(lookup.get(), 0)));
    if (signature != algorithm.signature) {
      projection_error("SAA projection algorithm signature mismatch: " +
                       identity);
    }
  }
}

std::vector<std::string>
AlgorithmSearchProjection::search_text(std::string_view query,
                                       std::size_t limit) const {
  if (query.empty() || limit < 1U || limit > 64U) {
    projection_error("SAA FTS query and limit must be bounded and non-empty");
  }
  auto database = open_database(database_path_);
  initialize(database.get());
  auto statement = prepare(
      database.get(),
      "SELECT canonical_algorithm_id FROM algorithm_fts "
      "WHERE algorithm_fts MATCH ? "
      "ORDER BY bm25(algorithm_fts), canonical_algorithm_id LIMIT ?");
  bind_text(statement.get(), 1, query);
  if (sqlite3_bind_int64(statement.get(), 2,
                         static_cast<sqlite3_int64>(limit)) != SQLITE_OK) {
    projection_error("cannot bind SAA FTS limit");
  }
  std::vector<std::string> result;
  while (true) {
    const int status = sqlite3_step(statement.get());
    if (status == SQLITE_DONE) {
      break;
    }
    if (status != SQLITE_ROW) {
      projection_error("SAA FTS query failed: " +
                       std::string(sqlite3_errmsg(database.get())));
    }
    result.emplace_back(reinterpret_cast<const char *>(
        sqlite3_column_text(statement.get(), 0)));
  }
  return result;
}

} // namespace statewright::saa
