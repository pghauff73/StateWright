#include "statewright/egcf/store.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/contracts/typed_id.hpp"
#include "statewright/core/event_store.hpp"
#include "statewright/core/file_io.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <sys/file.h>
#include <unistd.h>
#include <utility>

namespace statewright::egcf {
namespace {

using Json = contracts::Json;

[[noreturn]] void store_error(std::string message) {
  throw common::Error(common::ErrorCode::filesystem_failure,
                      std::move(message));
}

[[nodiscard]] bool lowercase_hex(std::string_view value) {
  return std::all_of(value.begin(), value.end(), [](unsigned char character) {
    return (character >= '0' && character <= '9') ||
           (character >= 'a' && character <= 'f');
  });
}

[[nodiscard]] contracts::TypedIdParts
strict_id_parts(std::string_view object_id) {
  const auto parts = contracts::parse_typed_id(object_id);
  if (contracts::normalize_object_type(parts.object_type) != parts.object_type ||
      parts.digest.size() != 64U || !lowercase_hex(parts.digest)) {
    throw common::Error(common::ErrorCode::invalid_typed_id,
                        "invalid canonical typed object ID");
  }
  return parts;
}

[[nodiscard]] std::string pretty_json(const Json &value) {
  return value.dump(2, ' ', false, Json::error_handler_t::strict) + "\n";
}

[[nodiscard]] std::string utc_now() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t value = std::chrono::system_clock::to_time_t(now);
  std::tm parts{};
  gmtime_r(&value, &parts);
  std::array<char, 32> buffer{};
  if (std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ",
                    &parts) == 0U) {
    store_error("cannot format EGCF timestamp");
  }
  return buffer.data();
}

class FileLock final {
public:
  explicit FileLock(const std::filesystem::path &path) {
    std::filesystem::create_directories(path.parent_path());
    descriptor_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (descriptor_ < 0) {
      store_error("cannot open EGCF workspace lock: " +
                  std::string(std::strerror(errno)));
    }
    if (::flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
      const std::string message = std::strerror(errno);
      ::close(descriptor_);
      descriptor_ = -1;
      store_error("EGCF workspace is already locked: " + message);
    }
  }

  ~FileLock() {
    if (descriptor_ >= 0) {
      static_cast<void>(::flock(descriptor_, LOCK_UN));
      static_cast<void>(::close(descriptor_));
    }
  }

  FileLock(const FileLock &) = delete;
  FileLock &operator=(const FileLock &) = delete;

private:
  int descriptor_ = -1;
};

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
    store_error("cannot open EGCF projection: " + message);
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
    store_error("EGCF projection SQL failed: " + text);
  }
}

[[nodiscard]] Statement prepare(sqlite3 *database, std::string_view sql) {
  sqlite3_stmt *raw = nullptr;
  if (sqlite3_prepare_v2(database, std::string(sql).c_str(), -1, &raw,
                         nullptr) != SQLITE_OK) {
    store_error("cannot prepare EGCF projection SQL: " +
                std::string(sqlite3_errmsg(database)));
  }
  return Statement(raw);
}

void bind_text(sqlite3_stmt *statement, int index, std::string_view value) {
  if (sqlite3_bind_text(statement, index, value.data(),
                        static_cast<int>(value.size()), SQLITE_TRANSIENT) !=
      SQLITE_OK) {
    store_error("cannot bind EGCF projection text");
  }
}

void bind_integer(sqlite3_stmt *statement, int index, std::size_t value) {
  if (sqlite3_bind_int64(statement, index,
                         static_cast<sqlite3_int64>(value)) != SQLITE_OK) {
    store_error("cannot bind EGCF projection integer");
  }
}

void step_done(sqlite3 *database, sqlite3_stmt *statement) {
  if (sqlite3_step(statement) != SQLITE_DONE) {
    store_error("cannot update EGCF projection: " +
                std::string(sqlite3_errmsg(database)));
  }
}

[[nodiscard]] std::string column_text(sqlite3_stmt *statement, int index) {
  const auto *text = sqlite3_column_text(statement, index);
  return text == nullptr ? std::string() :
                           std::string(reinterpret_cast<const char *>(text));
}

void initialize(sqlite3 *database) {
  execute(database, "PRAGMA foreign_keys=ON");
  execute(database, "PRAGMA journal_mode=DELETE");
  execute(database, "PRAGMA synchronous=FULL");
  execute(database,
          "CREATE TABLE IF NOT EXISTS metadata("
          "key TEXT PRIMARY KEY, value TEXT NOT NULL)");
  execute(database,
          "CREATE TABLE IF NOT EXISTS objects("
          "object_id TEXT PRIMARY KEY, object_type TEXT NOT NULL,"
          "digest TEXT NOT NULL UNIQUE, payload_json TEXT NOT NULL,"
          "path TEXT NOT NULL UNIQUE)");
  execute(database,
          "CREATE INDEX IF NOT EXISTS objects_type_idx "
          "ON objects(object_type, object_id)");
  execute(database,
          "CREATE TABLE IF NOT EXISTS supersedence("
          "old_id TEXT NOT NULL, new_id TEXT NOT NULL, reason TEXT NOT NULL,"
          "authority TEXT NOT NULL, created_at TEXT NOT NULL,"
          "record_id TEXT PRIMARY KEY)");
  execute(database,
          "CREATE INDEX IF NOT EXISTS supersedence_old_new_idx "
          "ON supersedence(old_id, new_id, record_id)");
  execute(database,
          "CREATE INDEX IF NOT EXISTS supersedence_new_idx "
          "ON supersedence(new_id, old_id)");
  execute(database,
          "CREATE TABLE IF NOT EXISTS events("
          "event_id TEXT PRIMARY KEY, timestamp TEXT NOT NULL,"
          "previous_hash TEXT NOT NULL, event_hash TEXT NOT NULL UNIQUE,"
          "event_type TEXT NOT NULL, payload_hash TEXT NOT NULL,"
          "payload_json TEXT NOT NULL)");
  execute(database,
          "CREATE INDEX IF NOT EXISTS events_type_idx "
          "ON events(event_type, timestamp, event_id)");
  execute(database,
          "CREATE VIRTUAL TABLE IF NOT EXISTS object_fts USING fts5("
          "object_id UNINDEXED, object_type, content, tokenize='unicode61')");
  execute(database,
          "CREATE TABLE IF NOT EXISTS internet_records("
          "object_id TEXT PRIMARY KEY, object_type TEXT NOT NULL,"
          "status TEXT NOT NULL, snapshot_id TEXT NOT NULL,"
          "candidate_ref TEXT NOT NULL, source_group TEXT NOT NULL,"
          "payload_json TEXT NOT NULL)");
  execute(database,
          "CREATE INDEX IF NOT EXISTS internet_records_type_status_idx "
          "ON internet_records(object_type, status, object_id)");
  execute(database,
          "CREATE INDEX IF NOT EXISTS internet_records_snapshot_idx "
          "ON internet_records(snapshot_id, object_id)");
  execute(database,
          "CREATE INDEX IF NOT EXISTS internet_records_candidate_idx "
          "ON internet_records(candidate_ref, object_id)");
  execute(database,
          "CREATE VIRTUAL TABLE IF NOT EXISTS internet_record_fts USING fts5("
          "object_id UNINDEXED, object_type, content, tokenize='unicode61')");
}

[[nodiscard]] std::string flatten_text(const Json &value) {
  std::ostringstream output;
  const auto append = [&](const auto &self, const Json &item) -> void {
    if (item.is_object()) {
      for (const auto &[key, child] : item.items()) {
        output << key << ' ';
        self(self, child);
      }
    } else if (item.is_array()) {
      for (const auto &child : item) {
        self(self, child);
      }
    } else if (item.is_string()) {
      output << item.get_ref<const std::string &>() << ' ';
    } else if (!item.is_null()) {
      output << contracts::canonical_json(item) << ' ';
    }
  };
  append(append, value);
  return output.str();
}

[[nodiscard]] std::string internet_field(
    const Json &payload, std::initializer_list<std::string_view> keys) {
  for (const auto key : keys) {
    const auto found = payload.find(std::string(key));
    if (found != payload.end() && found->is_string()) {
      return found->get<std::string>();
    }
  }
  const auto plan = payload.find("plan");
  if (plan != payload.end() && plan->is_object()) {
    for (const auto key : keys) {
      const auto found = plan->find(std::string(key));
      if (found != plan->end() && found->is_string()) {
        return found->get<std::string>();
      }
    }
  }
  return {};
}

void insert_internet_record(sqlite3_stmt *record_statement,
                            sqlite3_stmt *fts_statement,
                            const EgcfEnvelope &envelope) {
  if (!envelope.object_type.starts_with("internet-")) {
    return;
  }
  sqlite3_reset(record_statement);
  sqlite3_clear_bindings(record_statement);
  bind_text(record_statement, 1, envelope.object_id);
  bind_text(record_statement, 2, envelope.object_type);
  bind_text(record_statement, 3,
            internet_field(envelope.payload, {"status", "admission_status"}));
  bind_text(record_statement, 4,
            internet_field(envelope.payload, {"snapshot_id", "snapshot_ref"}));
  bind_text(record_statement, 5,
            internet_field(envelope.payload, {"candidate_ref", "candidate_id"}));
  bind_text(record_statement, 6,
            internet_field(envelope.payload, {"source_group"}));
  bind_text(record_statement, 7,
            contracts::canonical_json(envelope.payload));
  step_done(sqlite3_db_handle(record_statement), record_statement);

  sqlite3_reset(fts_statement);
  sqlite3_clear_bindings(fts_statement);
  bind_text(fts_statement, 1, envelope.object_id);
  bind_text(fts_statement, 2, envelope.object_type);
  bind_text(fts_statement, 3, flatten_text(envelope.payload));
  step_done(sqlite3_db_handle(fts_statement), fts_statement);
}

void sort_envelopes_by_id(std::vector<EgcfEnvelope> &envelopes) {
  std::sort(envelopes.begin(), envelopes.end(),
            [](const EgcfEnvelope &left, const EgcfEnvelope &right) {
              return left.object_id < right.object_id;
            });
}

[[nodiscard]] ProjectionCheckpoint authoritative_checkpoint(
    const std::vector<EgcfEnvelope> &envelopes,
    const std::vector<Json> &event_values, std::string_view event_head) {
  Json object_material = Json::array();
  for (const auto &envelope : envelopes) {
    object_material.push_back(to_json(envelope));
  }
  Json event_material = Json::array();
  for (const auto &event : event_values) {
    event_material.push_back(
        {{"event_hash", event.at("event_hash")},
         {"event_id", event.at("event_id")}});
  }
  const std::string digest = contracts::sha256_json(
      {{"event_head", event_head},
       {"events", event_material},
       {"objects", object_material},
       {"projection_schema_version", egcf_projection_schema_version}});
  return {.schema_version = egcf_projection_schema_version,
          .authoritative_digest = digest,
          .event_head = std::string(event_head),
          .object_count = envelopes.size(),
          .event_count = event_values.size()};
}

void remove_projection_files(const std::filesystem::path &path) {
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  std::filesystem::remove(path.string() + "-wal", ignored);
  std::filesystem::remove(path.string() + "-shm", ignored);
  std::filesystem::remove(path.string() + "-journal", ignored);
}

[[nodiscard]] StoredObject row_object(sqlite3_stmt *statement) {
  return {.object_id = column_text(statement, 0),
          .object_type = column_text(statement, 1),
          .digest = column_text(statement, 2),
          .payload = contracts::parse_json(column_text(statement, 3)),
          .relative_path = column_text(statement, 4)};
}

} // namespace

Json to_json(const StoredObject &object) {
  return {{"digest", object.digest},
          {"object_id", object.object_id},
          {"object_type", object.object_type},
          {"path", object.relative_path.generic_string()},
          {"payload", object.payload}};
}

Json to_json(const ArtifactBytes &artifact) {
  return {{"artifact_id", artifact.artifact_id},
          {"digest", artifact.digest},
          {"path", artifact.path.generic_string()},
          {"size", artifact.size}};
}

Json to_json(const ProjectionCheckpoint &checkpoint) {
  return {{"authoritative_digest", checkpoint.authoritative_digest},
          {"event_count", checkpoint.event_count},
          {"event_head", checkpoint.event_head},
          {"object_count", checkpoint.object_count},
          {"schema_version", checkpoint.schema_version}};
}

ObjectStore::ObjectStore(std::filesystem::path root,
                         std::filesystem::path resource_root)
    : root_(std::move(root)), schemas_(std::move(resource_root)) {
  std::filesystem::create_directories(root_);
}

const std::filesystem::path &ObjectStore::root() const noexcept { return root_; }

const RecordSchemaRegistry &ObjectStore::schemas() const noexcept {
  return schemas_;
}

std::filesystem::path ObjectStore::path_for(std::string_view object_id) const {
  const auto parts = strict_id_parts(object_id);
  return root_ / parts.digest.substr(0, 2U) / (parts.digest + ".json");
}

bool ObjectStore::contains(std::string_view object_id) const {
  return std::filesystem::is_regular_file(path_for(object_id));
}

std::string ObjectStore::put(const EgcfRecord &record) {
  schemas_.validate_record_payload(record.object_type, record.payload);
  const EgcfEnvelope envelope = envelope_for(record);
  const auto path = path_for(envelope.object_id);
  if (std::filesystem::exists(path)) {
    const Json existing = contracts::parse_json(core::read_text(path));
    if (contracts::canonical_json(existing) !=
        contracts::canonical_json(to_json(envelope))) {
      store_error("immutable EGCF object collision: " + envelope.object_id);
    }
    return envelope.object_id;
  }
  core::atomic_write_text(path, pretty_json(to_json(envelope)),
                          std::filesystem::perms::owner_read |
                              std::filesystem::perms::owner_write);
  return envelope.object_id;
}

EgcfEnvelope ObjectStore::get_envelope(std::string_view object_id) const {
  const auto path = path_for(object_id);
  Json value;
  try {
    value = contracts::parse_json(core::read_text(path));
  } catch (const common::Error &error) {
    store_error("cannot read EGCF object " + std::string(object_id) + ": " +
                error.what());
  }
  EgcfEnvelope envelope = envelope_from_json(value);
  if (envelope.object_id != object_id) {
    store_error("EGCF object identity mismatch: " + std::string(object_id));
  }
  schemas_.validate_record_payload(envelope.object_type, envelope.payload);
  const auto parts = strict_id_parts(object_id);
  if (parts.object_type != envelope.object_type ||
      contracts::typed_id(envelope.object_type, envelope.payload) != object_id) {
    store_error("EGCF object hash mismatch: " + std::string(object_id));
  }
  return envelope;
}

EgcfRecord ObjectStore::get(std::string_view object_id) const {
  const auto envelope = get_envelope(object_id);
  return {.object_type = envelope.object_type, .payload = envelope.payload};
}

std::vector<EgcfEnvelope> ObjectStore::envelopes() const {
  std::vector<std::filesystem::path> paths;
  if (!std::filesystem::exists(root_)) {
    return {};
  }
  for (const auto &bucket : std::filesystem::directory_iterator(root_)) {
    if (!bucket.is_directory()) {
      store_error("invalid EGCF object-store entry: " +
                  bucket.path().string());
    }
    for (const auto &entry : std::filesystem::directory_iterator(bucket.path())) {
      if (!entry.is_regular_file() || entry.path().extension() != ".json") {
        store_error("invalid EGCF object-store entry: " +
                    entry.path().string());
      }
      paths.push_back(entry.path());
    }
  }
  std::sort(paths.begin(), paths.end());
  std::vector<EgcfEnvelope> result;
  result.reserve(paths.size());
  for (const auto &path : paths) {
    try {
      const Json value = contracts::parse_json(core::read_text(path));
      auto envelope = envelope_from_json(value);
      if (path != path_for(envelope.object_id)) {
        store_error("EGCF object stored at noncanonical path: " + path.string());
      }
      schemas_.validate_record_payload(envelope.object_type, envelope.payload);
      const auto parts = strict_id_parts(envelope.object_id);
      if (parts.object_type != envelope.object_type ||
          contracts::typed_id(envelope.object_type, envelope.payload) !=
              envelope.object_id) {
        store_error("EGCF object hash mismatch: " + envelope.object_id);
      }
      result.push_back(std::move(envelope));
    } catch (const common::Error &error) {
      store_error("invalid EGCF object-store entry " + path.string() + ": " +
                  error.what());
    }
  }
  return result;
}

ArtifactStore::ArtifactStore(std::filesystem::path root)
    : root_(std::move(root)) {
  std::filesystem::create_directories(root_);
}

const std::filesystem::path &ArtifactStore::root() const noexcept {
  return root_;
}

ArtifactBytes ArtifactStore::put(std::span<const std::byte> content) {
  const std::string digest = contracts::sha256_bytes(content);
  const std::string artifact_id = "artifact-bytes:sha256:" + digest;
  const auto path = root_ / digest.substr(0, 2U) / digest;
  if (std::filesystem::exists(path)) {
    if (core::read_bytes(path) !=
        std::vector<std::byte>(content.begin(), content.end())) {
      store_error("immutable EGCF artifact collision: " + artifact_id);
    }
    return {.artifact_id = artifact_id,
            .digest = digest,
            .size = content.size(),
            .path = path};
  }
  core::atomic_write_bytes(path, content,
                           std::filesystem::perms::owner_read |
                               std::filesystem::perms::owner_write);
  return {.artifact_id = artifact_id,
          .digest = digest,
          .size = content.size(),
          .path = path};
}

std::vector<std::byte> ArtifactStore::get(std::string_view artifact_id) const {
  static constexpr std::string_view prefix = "artifact-bytes:sha256:";
  if (!artifact_id.starts_with(prefix)) {
    throw common::Error(common::ErrorCode::invalid_typed_id,
                        "invalid EGCF artifact byte ID");
  }
  const std::string digest(artifact_id.substr(prefix.size()));
  if (digest.size() != 64U || !lowercase_hex(digest)) {
    throw common::Error(common::ErrorCode::invalid_typed_id,
                        "invalid EGCF artifact byte digest");
  }
  const auto bytes = core::read_bytes(root_ / digest.substr(0, 2U) / digest);
  if (contracts::sha256_bytes(bytes) != digest) {
    store_error("EGCF artifact byte hash mismatch: " + std::string(artifact_id));
  }
  return bytes;
}

class EgcfStore::Impl final {
public:
  Impl(std::filesystem::path workspace, std::filesystem::path resources)
      : workspace_root(std::filesystem::weakly_canonical(workspace)),
        state_root(workspace_root / ".ourd-agent" / "egcf"),
        lock(state_root / "lock"),
        object_store(state_root / "objects" / "sha256", resources),
        artifact_store(state_root / "artifacts" / "sha256"),
        parent_event_head(load_parent_event_head()),
        event_store(state_root / "events.jsonl"),
        projection_path(state_root / "projection.sqlite3") {
    ensure_projection();
  }

  [[nodiscard]] std::string load_parent_event_head() const {
    const auto path = workspace_root / ".ourd-agent" / "events.jsonl";
    if (!std::filesystem::exists(path)) {
      return {};
    }
    return core::EventStore(path).head();
  }

  struct ProjectionFileStamp final {
    std::filesystem::file_time_type modified_at;
    std::uintmax_t size = 0;

    bool operator==(const ProjectionFileStamp &) const = default;
  };

  [[nodiscard]] std::optional<ProjectionFileStamp>
  projection_file_stamp() const {
    std::error_code error;
    const auto modified_at =
        std::filesystem::last_write_time(projection_path, error);
    if (error) {
      return std::nullopt;
    }
    const auto size = std::filesystem::file_size(projection_path, error);
    if (error) {
      return std::nullopt;
    }
    return ProjectionFileStamp{.modified_at = modified_at, .size = size};
  }

  void remember_projection_stamp() {
    validated_projection_stamp = projection_file_stamp();
  }

  void validate_and_remember_projection() {
    validate_projection();
    remember_projection_stamp();
  }

  void ensure_projection() {
    const auto current_stamp = projection_file_stamp();
    if (current_stamp && validated_projection_stamp &&
        *current_stamp == *validated_projection_stamp) {
      return;
    }
    if (!current_stamp) {
      replace_projection();
      return;
    }
    try {
      validate_and_remember_projection();
    } catch (const common::Error &) {
      replace_projection();
    }
  }

  void reload_authoritative_cache() {
    authoritative_envelopes = object_store.envelopes();
    sort_envelopes_by_id(authoritative_envelopes);
    authoritative_events = event_store.events();
    authoritative_event_head = event_store.validate_chain();
  }

  void cache_appended_authority(
      const std::vector<EgcfEnvelope> &envelopes,
      const std::vector<Json> &event_values) {
    authoritative_envelopes.insert(authoritative_envelopes.end(),
                                   envelopes.begin(), envelopes.end());
    sort_envelopes_by_id(authoritative_envelopes);
    authoritative_events.insert(authoritative_events.end(),
                                event_values.begin(), event_values.end());
    if (!event_values.empty()) {
      authoritative_event_head =
          event_values.back().at("event_hash").get<std::string>();
    }
  }

  [[nodiscard]] ProjectionCheckpoint checkpoint() const {
    return authoritative_checkpoint(authoritative_envelopes,
                                    authoritative_events,
                                    authoritative_event_head);
  }

  void populate_projection(sqlite3 *database,
                           const std::vector<EgcfEnvelope> &envelopes,
                           const std::vector<Json> &event_values,
                           const ProjectionCheckpoint &expected) {
    initialize(database);
    execute(database, "BEGIN IMMEDIATE");
    try {
      execute(database, R"SQL(
        DELETE FROM internet_record_fts;
        DELETE FROM internet_records;
        DELETE FROM object_fts;
        DELETE FROM supersedence;
        DELETE FROM objects;
        DELETE FROM events;
        DELETE FROM metadata;
      )SQL");
      auto object_insert = prepare(
          database,
          "INSERT INTO objects(object_id, object_type, digest, payload_json, "
          "path) VALUES (?, ?, ?, ?, ?)");
      auto fts_insert = prepare(
          database,
          "INSERT INTO object_fts(object_id, object_type, content) "
          "VALUES (?, ?, ?)");
      auto supersedence_insert = prepare(
          database,
          "INSERT INTO supersedence(old_id, new_id, reason, "
          "authority, created_at, record_id) VALUES (?, ?, ?, ?, ?, ?)");
      auto internet_insert = prepare(
          database,
          "INSERT INTO internet_records(object_id, object_type, status, "
          "snapshot_id, candidate_ref, source_group, payload_json) "
          "VALUES (?, ?, ?, ?, ?, ?, ?)");
      auto internet_fts_insert = prepare(
          database,
          "INSERT INTO internet_record_fts(object_id, object_type, content) "
          "VALUES (?, ?, ?)");
      for (const auto &envelope : envelopes) {
        const auto parts = strict_id_parts(envelope.object_id);
        const auto path = object_store.path_for(envelope.object_id);
        sqlite3_reset(object_insert.get());
        sqlite3_clear_bindings(object_insert.get());
        bind_text(object_insert.get(), 1, envelope.object_id);
        bind_text(object_insert.get(), 2, envelope.object_type);
        bind_text(object_insert.get(), 3, parts.digest);
        bind_text(object_insert.get(), 4,
                  contracts::canonical_json(envelope.payload));
        bind_text(object_insert.get(), 5,
                  path.lexically_relative(state_root).generic_string());
        step_done(database, object_insert.get());

        sqlite3_reset(fts_insert.get());
        sqlite3_clear_bindings(fts_insert.get());
        bind_text(fts_insert.get(), 1, envelope.object_id);
        bind_text(fts_insert.get(), 2, envelope.object_type);
        bind_text(fts_insert.get(), 3, flatten_text(envelope.payload));
        step_done(database, fts_insert.get());

        insert_internet_record(internet_insert.get(), internet_fts_insert.get(),
                               envelope);

        if (envelope.object_type == "supersedence") {
          sqlite3_reset(supersedence_insert.get());
          sqlite3_clear_bindings(supersedence_insert.get());
          bind_text(supersedence_insert.get(), 1,
                    envelope.payload.at("old_id")
                        .get_ref<const std::string &>());
          bind_text(supersedence_insert.get(), 2,
                    envelope.payload.at("new_id")
                        .get_ref<const std::string &>());
          bind_text(supersedence_insert.get(), 3,
                    envelope.payload.at("reason")
                        .get_ref<const std::string &>());
          bind_text(supersedence_insert.get(), 4,
                    envelope.payload.at("authority")
                        .get_ref<const std::string &>());
          bind_text(supersedence_insert.get(), 5,
                    envelope.payload.at("created_at")
                        .get_ref<const std::string &>());
          bind_text(supersedence_insert.get(), 6, envelope.object_id);
          step_done(database, supersedence_insert.get());
        }
      }

      auto event_insert = prepare(
          database,
          "INSERT INTO events(event_id, timestamp, previous_hash, event_hash, "
          "event_type, payload_hash, payload_json) VALUES (?, ?, ?, ?, ?, ?, "
          "?)");
      for (const auto &event : event_values) {
        sqlite3_reset(event_insert.get());
        sqlite3_clear_bindings(event_insert.get());
        bind_text(event_insert.get(), 1,
                  event.at("event_id").get_ref<const std::string &>());
        bind_text(event_insert.get(), 2,
                  event.at("timestamp").get_ref<const std::string &>());
        bind_text(event_insert.get(), 3,
                  event.at("previous_hash").get_ref<const std::string &>());
        bind_text(event_insert.get(), 4,
                  event.at("event_hash").get_ref<const std::string &>());
        bind_text(event_insert.get(), 5,
                  event.at("event_type").get_ref<const std::string &>());
        bind_text(event_insert.get(), 6,
                  event.at("payload_hash").get_ref<const std::string &>());
        bind_text(event_insert.get(), 7,
                  contracts::canonical_json(event.at("payload")));
        step_done(database, event_insert.get());
      }

      auto metadata_insert = prepare(
          database, "INSERT INTO metadata(key, value) VALUES (?, ?)");
      const std::array<std::pair<std::string, std::string>, 6> metadata = {{
          {"schema_version", std::to_string(expected.schema_version)},
          {"authoritative_digest", expected.authoritative_digest},
          {"event_head", expected.event_head},
          {"object_count", std::to_string(expected.object_count)},
          {"event_count", std::to_string(expected.event_count)},
          {"rebuilt_at", utc_now()},
      }};
      for (const auto &[key, value] : metadata) {
        sqlite3_reset(metadata_insert.get());
        sqlite3_clear_bindings(metadata_insert.get());
        bind_text(metadata_insert.get(), 1, key);
        bind_text(metadata_insert.get(), 2, value);
        step_done(database, metadata_insert.get());
      }
      execute(database, "COMMIT");
    } catch (...) {
      sqlite3_exec(database, "ROLLBACK", nullptr, nullptr, nullptr);
      throw;
    }
  }

  void append_projection(const std::vector<EgcfEnvelope> &envelopes,
                         const std::vector<Json> &event_values) {
    const ProjectionCheckpoint expected = checkpoint();
    auto database = open_database(projection_path);
    initialize(database.get());
    execute(database.get(), "BEGIN IMMEDIATE");
    try {
      auto object_insert = prepare(
          database.get(),
          "INSERT INTO objects(object_id, object_type, digest, payload_json, "
          "path) VALUES (?, ?, ?, ?, ?)");
      auto fts_insert = prepare(
          database.get(),
          "INSERT INTO object_fts(object_id, object_type, content) "
          "VALUES (?, ?, ?)");
      auto supersedence_insert = prepare(
          database.get(),
          "INSERT INTO supersedence(old_id, new_id, reason, "
          "authority, created_at, record_id) VALUES (?, ?, ?, ?, ?, ?)");
      auto internet_insert = prepare(
          database.get(),
          "INSERT INTO internet_records(object_id, object_type, status, "
          "snapshot_id, candidate_ref, source_group, payload_json) "
          "VALUES (?, ?, ?, ?, ?, ?, ?)");
      auto internet_fts_insert = prepare(
          database.get(),
          "INSERT INTO internet_record_fts(object_id, object_type, content) "
          "VALUES (?, ?, ?)");
      for (const auto &envelope : envelopes) {
        const auto parts = strict_id_parts(envelope.object_id);
        const auto path = object_store.path_for(envelope.object_id);
        sqlite3_reset(object_insert.get());
        sqlite3_clear_bindings(object_insert.get());
        bind_text(object_insert.get(), 1, envelope.object_id);
        bind_text(object_insert.get(), 2, envelope.object_type);
        bind_text(object_insert.get(), 3, parts.digest);
        bind_text(object_insert.get(), 4,
                  contracts::canonical_json(envelope.payload));
        bind_text(object_insert.get(), 5,
                  path.lexically_relative(state_root).generic_string());
        step_done(database.get(), object_insert.get());

        sqlite3_reset(fts_insert.get());
        sqlite3_clear_bindings(fts_insert.get());
        bind_text(fts_insert.get(), 1, envelope.object_id);
        bind_text(fts_insert.get(), 2, envelope.object_type);
        bind_text(fts_insert.get(), 3, flatten_text(envelope.payload));
        step_done(database.get(), fts_insert.get());

        insert_internet_record(internet_insert.get(), internet_fts_insert.get(),
                               envelope);

        if (envelope.object_type == "supersedence") {
          sqlite3_reset(supersedence_insert.get());
          sqlite3_clear_bindings(supersedence_insert.get());
          bind_text(supersedence_insert.get(), 1,
                    envelope.payload.at("old_id")
                        .get_ref<const std::string &>());
          bind_text(supersedence_insert.get(), 2,
                    envelope.payload.at("new_id")
                        .get_ref<const std::string &>());
          bind_text(supersedence_insert.get(), 3,
                    envelope.payload.at("reason")
                        .get_ref<const std::string &>());
          bind_text(supersedence_insert.get(), 4,
                    envelope.payload.at("authority")
                        .get_ref<const std::string &>());
          bind_text(supersedence_insert.get(), 5,
                    envelope.payload.at("created_at")
                        .get_ref<const std::string &>());
          bind_text(supersedence_insert.get(), 6, envelope.object_id);
          step_done(database.get(), supersedence_insert.get());
        }
      }

      auto event_insert = prepare(
          database.get(),
          "INSERT INTO events(event_id, timestamp, previous_hash, event_hash, "
          "event_type, payload_hash, payload_json) VALUES (?, ?, ?, ?, ?, ?, "
          "?)");
      for (const auto &event : event_values) {
        sqlite3_reset(event_insert.get());
        sqlite3_clear_bindings(event_insert.get());
        bind_text(event_insert.get(), 1,
                  event.at("event_id").get_ref<const std::string &>());
        bind_text(event_insert.get(), 2,
                  event.at("timestamp").get_ref<const std::string &>());
        bind_text(event_insert.get(), 3,
                  event.at("previous_hash").get_ref<const std::string &>());
        bind_text(event_insert.get(), 4,
                  event.at("event_hash").get_ref<const std::string &>());
        bind_text(event_insert.get(), 5,
                  event.at("event_type").get_ref<const std::string &>());
        bind_text(event_insert.get(), 6,
                  event.at("payload_hash").get_ref<const std::string &>());
        bind_text(event_insert.get(), 7,
                  contracts::canonical_json(event.at("payload")));
        step_done(database.get(), event_insert.get());
      }

      auto metadata_upsert = prepare(
          database.get(),
          "INSERT OR REPLACE INTO metadata(key, value) VALUES (?, ?)");
      const std::array<std::pair<std::string, std::string>, 6> metadata = {{
          {"schema_version", std::to_string(expected.schema_version)},
          {"authoritative_digest", expected.authoritative_digest},
          {"event_head", expected.event_head},
          {"object_count", std::to_string(expected.object_count)},
          {"event_count", std::to_string(expected.event_count)},
          {"rebuilt_at", utc_now()},
      }};
      for (const auto &[key, value] : metadata) {
        sqlite3_reset(metadata_upsert.get());
        sqlite3_clear_bindings(metadata_upsert.get());
        bind_text(metadata_upsert.get(), 1, key);
        bind_text(metadata_upsert.get(), 2, value);
        step_done(database.get(), metadata_upsert.get());
      }
      execute(database.get(), "COMMIT");
    } catch (...) {
      sqlite3_exec(database.get(), "ROLLBACK", nullptr, nullptr, nullptr);
      throw;
    }
    database.reset();
    remember_projection_stamp();
  }

  void replace_projection() {
    reload_authoritative_cache();
    const ProjectionCheckpoint expected = checkpoint();
    const auto temporary = projection_path.string() + ".rebuild." +
                           std::to_string(static_cast<long long>(::getpid()));
    remove_projection_files(temporary);
    try {
      auto database = open_database(temporary);
      populate_projection(database.get(), authoritative_envelopes,
                          authoritative_events, expected);
      database.reset();
      remove_projection_files(projection_path);
      std::filesystem::rename(temporary, projection_path);
    } catch (...) {
      remove_projection_files(temporary);
      throw;
    }
    validate_and_remember_projection();
  }

  void rebuild_projection() {
    reload_authoritative_cache();
    const ProjectionCheckpoint expected = checkpoint();
    auto database = open_database(projection_path);
    populate_projection(database.get(), authoritative_envelopes,
                        authoritative_events, expected);
    database.reset();
    validate_and_remember_projection();
  }

  void validate_projection() {
    reload_authoritative_cache();
    const ProjectionCheckpoint expected = checkpoint();
    auto database = open_database(projection_path);
    initialize(database.get());
    auto integrity = prepare(database.get(), "PRAGMA integrity_check");
    if (sqlite3_step(integrity.get()) != SQLITE_ROW ||
        column_text(integrity.get(), 0) != "ok") {
      store_error("EGCF projection SQLite integrity check failed");
    }
    const auto metadata_value = [&](std::string_view key) {
      auto statement =
          prepare(database.get(), "SELECT value FROM metadata WHERE key=?");
      bind_text(statement.get(), 1, key);
      if (sqlite3_step(statement.get()) != SQLITE_ROW) {
        store_error("EGCF projection lacks metadata: " + std::string(key));
      }
      return column_text(statement.get(), 0);
    };
    if (metadata_value("schema_version") !=
            std::to_string(expected.schema_version) ||
        metadata_value("authoritative_digest") !=
            expected.authoritative_digest ||
        metadata_value("event_head") != expected.event_head ||
        metadata_value("object_count") !=
            std::to_string(expected.object_count) ||
        metadata_value("event_count") != std::to_string(expected.event_count)) {
      store_error("EGCF projection checkpoint mismatch");
    }
    const auto require_count = [&](std::string_view table, std::size_t count) {
      auto statement = prepare(database.get(),
                               "SELECT COUNT(*) FROM " + std::string(table));
      if (sqlite3_step(statement.get()) != SQLITE_ROW ||
          sqlite3_column_int64(statement.get(), 0) !=
              static_cast<sqlite3_int64>(count)) {
        store_error("EGCF projection row count mismatch: " +
                    std::string(table));
      }
    };
    require_count("objects", expected.object_count);
    require_count("object_fts", expected.object_count);
    require_count("events", expected.event_count);
    const auto internet_count = static_cast<std::size_t>(std::ranges::count_if(
        authoritative_envelopes, [](const auto &envelope) {
          return envelope.object_type.starts_with("internet-");
        }));
    require_count("internet_records", internet_count);
    require_count("internet_record_fts", internet_count);

    auto object_lookup = prepare(
        database.get(),
        "SELECT object_type, digest, payload_json, path FROM objects "
        "WHERE object_id=?");
    std::size_t supersedence_count = 0;
    auto internet_lookup = prepare(
        database.get(),
        "SELECT object_type, status, snapshot_id, candidate_ref, source_group, "
        "payload_json FROM internet_records WHERE object_id=?");
    for (const auto &envelope : authoritative_envelopes) {
      sqlite3_reset(object_lookup.get());
      sqlite3_clear_bindings(object_lookup.get());
      bind_text(object_lookup.get(), 1, envelope.object_id);
      if (sqlite3_step(object_lookup.get()) != SQLITE_ROW) {
        store_error("EGCF projection is missing object: " + envelope.object_id);
      }
      const auto parts = strict_id_parts(envelope.object_id);
      const auto relative = object_store.path_for(envelope.object_id)
                                .lexically_relative(state_root)
                                .generic_string();
      if (column_text(object_lookup.get(), 0) != envelope.object_type ||
          column_text(object_lookup.get(), 1) != parts.digest ||
          column_text(object_lookup.get(), 2) !=
              contracts::canonical_json(envelope.payload) ||
          column_text(object_lookup.get(), 3) != relative) {
        store_error("EGCF projection object mismatch: " + envelope.object_id);
      }
      if (envelope.object_type == "supersedence") {
        ++supersedence_count;
      }
      if (envelope.object_type.starts_with("internet-")) {
        sqlite3_reset(internet_lookup.get());
        sqlite3_clear_bindings(internet_lookup.get());
        bind_text(internet_lookup.get(), 1, envelope.object_id);
        if (sqlite3_step(internet_lookup.get()) != SQLITE_ROW ||
            column_text(internet_lookup.get(), 0) != envelope.object_type ||
            column_text(internet_lookup.get(), 1) !=
                internet_field(envelope.payload, {"status", "admission_status"}) ||
            column_text(internet_lookup.get(), 2) !=
                internet_field(envelope.payload, {"snapshot_id", "snapshot_ref"}) ||
            column_text(internet_lookup.get(), 3) !=
                internet_field(envelope.payload, {"candidate_ref", "candidate_id"}) ||
            column_text(internet_lookup.get(), 4) !=
                internet_field(envelope.payload, {"source_group"}) ||
            column_text(internet_lookup.get(), 5) !=
                contracts::canonical_json(envelope.payload)) {
          store_error("EGCF internet projection mismatch: " +
                      envelope.object_id);
        }
      }
    }
    require_count("supersedence", supersedence_count);

    auto event_lookup = prepare(
        database.get(),
        "SELECT event_hash, payload_hash, payload_json FROM events "
        "WHERE event_id=?");
    for (const auto &event : authoritative_events) {
      sqlite3_reset(event_lookup.get());
      sqlite3_clear_bindings(event_lookup.get());
      bind_text(event_lookup.get(), 1,
                event.at("event_id").get_ref<const std::string &>());
      if (sqlite3_step(event_lookup.get()) != SQLITE_ROW ||
          column_text(event_lookup.get(), 0) !=
              event.at("event_hash").get<std::string>() ||
          column_text(event_lookup.get(), 1) !=
              event.at("payload_hash").get<std::string>() ||
          column_text(event_lookup.get(), 2) !=
              contracts::canonical_json(event.at("payload"))) {
        store_error("EGCF projection event mismatch");
      }
    }
  }

  std::filesystem::path workspace_root;
  std::filesystem::path state_root;
  FileLock lock;
  ObjectStore object_store;
  ArtifactStore artifact_store;
  std::string parent_event_head;
  core::EventStore event_store;
  std::filesystem::path projection_path;
  std::vector<EgcfEnvelope> authoritative_envelopes;
  std::vector<Json> authoritative_events;
  std::string authoritative_event_head;
  std::optional<ProjectionFileStamp> validated_projection_stamp;
};

EgcfStore::EgcfStore(std::filesystem::path workspace_root,
                     std::filesystem::path resource_root)
    : impl_(std::make_unique<Impl>(std::move(workspace_root),
                                  std::move(resource_root))) {}

EgcfStore::~EgcfStore() = default;

const std::filesystem::path &EgcfStore::workspace_root() const noexcept {
  return impl_->workspace_root;
}

const std::filesystem::path &EgcfStore::state_root() const noexcept {
  return impl_->state_root;
}

const std::filesystem::path &EgcfStore::projection_path() const noexcept {
  return impl_->projection_path;
}

const std::string &EgcfStore::parent_event_head() const noexcept {
  return impl_->parent_event_head;
}

const ObjectStore &EgcfStore::objects() const noexcept {
  return impl_->object_store;
}

const ArtifactStore &EgcfStore::artifacts() const noexcept {
  return impl_->artifact_store;
}

std::string EgcfStore::register_record(const EgcfRecord &record,
                                       std::string_view event_type) {
  return register_records({record}, event_type).front();
}

std::vector<std::string>
EgcfStore::register_records(const std::vector<EgcfRecord> &records,
                            std::string_view event_type) {
  if (event_type.empty()) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "EGCF registration event type must be non-empty");
  }
  std::vector<std::string> result;
  result.reserve(records.size());
  std::vector<EgcfEnvelope> new_envelopes;
  std::vector<Json> new_events;
  for (const auto &record : records) {
    const std::string predicted = record.object_id();
    const bool existed = impl_->object_store.contains(predicted);
    const std::string object_id = impl_->object_store.put(record);
    result.push_back(object_id);
    if (!existed) {
      new_envelopes.push_back(envelope_for(record));
      new_events.push_back(impl_->event_store.append(
          event_type, {{"object_id", object_id},
                       {"object_type", record.object_type},
                       {"parent_event_head", impl_->parent_event_head}}));
    }
  }
  if (!new_envelopes.empty()) {
    impl_->cache_appended_authority(new_envelopes, new_events);
    try {
      impl_->append_projection(new_envelopes, new_events);
    } catch (const common::Error &) {
      impl_->replace_projection();
    }
  }
  return result;
}

std::vector<std::string>
EgcfStore::register_resources(const EgcfResourceBundle &bundle) {
  if (bundle.receipt.manifest_hash !=
      verify_resource_manifest(bundle.receipt.resource_root).manifest_hash) {
    throw common::Error(common::ErrorCode::json_contract,
                        "EGCF resource bundle manifest changed before admission");
  }
  std::vector<EgcfRecord> records;
  records.reserve(bundle.command_definitions.size() +
                  bundle.algorithm_definitions.size() +
                  bundle.workflow_definitions.size());
  records.insert(records.end(), bundle.command_definitions.begin(),
                 bundle.command_definitions.end());
  records.insert(records.end(), bundle.algorithm_definitions.begin(),
                 bundle.algorithm_definitions.end());
  records.insert(records.end(), bundle.workflow_definitions.begin(),
                 bundle.workflow_definitions.end());
  return register_records(records, "egcf_resource_registered");
}

std::string EgcfStore::register_artifact(
    std::span<const std::byte> content, std::string media_type,
    std::vector<std::string> source_ids, Json provenance,
    std::optional<std::string> created_at) {
  if (media_type.empty() || !provenance.is_object()) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "EGCF artifact metadata is invalid");
  }
  const ArtifactBytes bytes = impl_->artifact_store.put(content);
  std::vector<std::string> unique_sources;
  std::set<std::string> seen;
  for (auto &source : source_ids) {
    static_cast<void>(strict_id_parts(source));
    if (seen.insert(source).second) {
      unique_sources.push_back(std::move(source));
    }
  }
  const auto relative = bytes.path.lexically_relative(impl_->state_root);
  return register_record(
      {.object_type = "artifact",
       .payload = {{"created_at", created_at.value_or(utc_now())},
                   {"media_type", std::move(media_type)},
                   {"path", relative.generic_string()},
                   {"provenance", std::move(provenance)},
                   {"sha256", bytes.digest},
                   {"size", bytes.size},
                   {"source_ids", std::move(unique_sources)}}},
      "egcf_artifact_registered");
}

EgcfRecord EgcfStore::get(std::string_view object_id) const {
  return impl_->object_store.get(object_id);
}

std::vector<StoredObject>
EgcfStore::list(std::optional<std::string> object_type) {
  if (object_type) {
    static_cast<void>(impl_->object_store.schemas().schema_for(*object_type));
  }
  impl_->ensure_projection();
  auto database = open_database(impl_->projection_path);
  std::string sql =
      "SELECT object_id, object_type, digest, payload_json, path FROM objects";
  if (object_type) {
    sql += " WHERE object_type=?";
  }
  sql += " ORDER BY object_type, object_id";
  auto statement = prepare(database.get(), sql);
  if (object_type) {
    bind_text(statement.get(), 1, *object_type);
  }
  std::vector<StoredObject> result;
  while (true) {
    const int status = sqlite3_step(statement.get());
    if (status == SQLITE_DONE) {
      break;
    }
    if (status != SQLITE_ROW) {
      store_error("cannot list EGCF projection objects: " +
                  std::string(sqlite3_errmsg(database.get())));
    }
    result.push_back(row_object(statement.get()));
  }
  return result;
}

std::vector<StoredObject>
EgcfStore::search_text(std::string_view query,
                       std::optional<std::string> object_type,
                       std::size_t limit) {
  if (query.empty() || limit < 1U || limit > 100U) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "EGCF search query and limit must be bounded");
  }
  if (object_type) {
    static_cast<void>(impl_->object_store.schemas().schema_for(*object_type));
  }
  impl_->ensure_projection();
  auto database = open_database(impl_->projection_path);
  std::string sql =
      "SELECT o.object_id, o.object_type, o.digest, o.payload_json, o.path "
      "FROM object_fts JOIN objects o ON o.object_id=object_fts.object_id "
      "WHERE object_fts MATCH ?";
  if (object_type) {
    sql += " AND o.object_type=?";
  }
  sql += " ORDER BY bm25(object_fts), o.object_id LIMIT ?";
  auto statement = prepare(database.get(), sql);
  bind_text(statement.get(), 1, query);
  int next = 2;
  if (object_type) {
    bind_text(statement.get(), next++, *object_type);
  }
  bind_integer(statement.get(), next, limit);
  std::vector<StoredObject> result;
  while (true) {
    const int status = sqlite3_step(statement.get());
    if (status == SQLITE_DONE) {
      break;
    }
    if (status != SQLITE_ROW) {
      store_error("EGCF FTS query failed: " +
                  std::string(sqlite3_errmsg(database.get())));
    }
    result.push_back(row_object(statement.get()));
  }
  return result;
}

std::string EgcfStore::supersede(std::string old_id, std::string new_id,
                                 std::string reason, std::string authority,
                                 std::optional<std::string> created_at) {
  static_cast<void>(get(old_id));
  static_cast<void>(get(new_id));
  if (old_id == new_id || reason.empty() || authority.empty()) {
    throw common::Error(common::ErrorCode::invalid_argument,
                        "EGCF supersedence requires distinct objects and "
                        "non-empty reason and authority");
  }
  return register_record(
      {.object_type = "supersedence",
       .payload = {{"authority", std::move(authority)},
                   {"created_at", created_at.value_or(utc_now())},
                   {"new_id", std::move(new_id)},
                   {"old_id", std::move(old_id)},
                   {"reason", std::move(reason)}}},
      "egcf_object_superseded");
}

std::vector<std::string> EgcfStore::active_ids(std::string_view object_type) {
  static_cast<void>(impl_->object_store.schemas().schema_for(object_type));
  impl_->ensure_projection();
  auto database = open_database(impl_->projection_path);
  auto statement = prepare(
      database.get(),
      "SELECT object_id FROM objects WHERE object_type=? AND object_id NOT IN "
      "(SELECT old_id FROM supersedence) ORDER BY object_id");
  bind_text(statement.get(), 1, object_type);
  std::vector<std::string> result;
  while (true) {
    const int status = sqlite3_step(statement.get());
    if (status == SQLITE_DONE) {
      break;
    }
    if (status != SQLITE_ROW) {
      store_error("cannot query active EGCF IDs");
    }
    result.push_back(column_text(statement.get(), 0));
  }
  return result;
}

std::vector<Json> EgcfStore::events() const { return impl_->event_store.events(); }

std::string EgcfStore::event_head() const {
  return impl_->event_store.validate_chain();
}

ProjectionCheckpoint EgcfStore::projection_checkpoint() const {
  return impl_->checkpoint();
}

void EgcfStore::validate_projection() const {
  impl_->validate_and_remember_projection();
}

void EgcfStore::rebuild_projection() { impl_->rebuild_projection(); }

} // namespace statewright::egcf
