#include "statewright/egcf/nonlinear_store.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/core/file_io.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <memory>
#include <set>
#include <utility>

namespace statewright::egcf {
namespace {

using Json = contracts::Json;
using Database = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

[[noreturn]] void nonlinear_store_error(std::string message) {
  throw common::Error(common::ErrorCode::filesystem_failure,
                      std::move(message));
}

[[nodiscard]] std::string exact_sha(std::string value,
                                    std::string_view label) {
  if (value.size() != 64U ||
      !std::ranges::all_of(value, [](unsigned char character) {
        return std::isdigit(character) != 0 ||
               (character >= 'a' && character <= 'f');
      })) {
    nonlinear_store_error(std::string(label) +
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
    nonlinear_store_error("cannot format nonlinear store timestamp");
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
  Database database(raw, sqlite3_close);
  if (result != SQLITE_OK || !database) {
    const std::string message =
        raw == nullptr ? "unknown SQLite error" : sqlite3_errmsg(raw);
    nonlinear_store_error("cannot open nonlinear projection: " + message);
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
    nonlinear_store_error("nonlinear projection SQL failed: " + text);
  }
}

[[nodiscard]] Statement prepare(sqlite3 *database, std::string_view sql) {
  sqlite3_stmt *raw = nullptr;
  if (sqlite3_prepare_v2(database, std::string(sql).c_str(), -1, &raw,
                         nullptr) != SQLITE_OK) {
    nonlinear_store_error("cannot prepare nonlinear projection SQL: " +
                          std::string(sqlite3_errmsg(database)));
  }
  return Statement(raw, sqlite3_finalize);
}

void bind_text(sqlite3_stmt *statement, int index, std::string_view value) {
  if (sqlite3_bind_text(statement, index, value.data(),
                        static_cast<int>(value.size()), SQLITE_TRANSIENT) !=
      SQLITE_OK) {
    nonlinear_store_error("cannot bind nonlinear projection text");
  }
}

void bind_int(sqlite3_stmt *statement, int index, int value) {
  if (sqlite3_bind_int(statement, index, value) != SQLITE_OK) {
    nonlinear_store_error("cannot bind nonlinear projection integer");
  }
}

void step_done(sqlite3 *database, sqlite3_stmt *statement) {
  if (sqlite3_step(statement) != SQLITE_DONE) {
    nonlinear_store_error("cannot update nonlinear projection: " +
                          std::string(sqlite3_errmsg(database)));
  }
}

[[nodiscard]] std::string column_text(sqlite3_stmt *statement, int index) {
  const auto *value = sqlite3_column_text(statement, index);
  return value == nullptr
             ? std::string{}
             : std::string(reinterpret_cast<const char *>(value));
}

void create_tables(sqlite3 *database) {
  execute(database, R"SQL(
    CREATE TABLE IF NOT EXISTS nonlinear_local_forms (
      local_id TEXT PRIMARY KEY,
      parent_signature TEXT NOT NULL,
      local_behavior_signature TEXT NOT NULL UNIQUE,
      semantic_signature TEXT NOT NULL,
      source_jet_signature TEXT NOT NULL,
      coefficient_signature TEXT NOT NULL,
      scope_signature TEXT NOT NULL,
      jet_order INTEGER NOT NULL,
      generation INTEGER NOT NULL,
      payload_json TEXT NOT NULL,
      path TEXT NOT NULL,
      created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS nonlinear_local_parent_idx
      ON nonlinear_local_forms(parent_signature);
    CREATE INDEX IF NOT EXISTS nonlinear_local_source_idx
      ON nonlinear_local_forms(source_jet_signature);
    CREATE TABLE IF NOT EXISTS nonlinear_evidence (
      evidence_signature TEXT PRIMARY KEY,
      evidence_kind TEXT NOT NULL,
      exact INTEGER NOT NULL,
      canonical_local_eligible INTEGER NOT NULL,
      source_snapshot_hash TEXT NOT NULL,
      payload_json TEXT NOT NULL,
      path TEXT NOT NULL,
      created_at TEXT NOT NULL
    );
    CREATE TABLE IF NOT EXISTS nonlinear_local_evidence (
      local_id TEXT NOT NULL,
      evidence_signature TEXT NOT NULL,
      PRIMARY KEY(local_id, evidence_signature)
    );
    CREATE TABLE IF NOT EXISTS nonlinear_regional_assessments (
      assessment_signature TEXT PRIMARY KEY,
      parent_signature TEXT NOT NULL,
      status TEXT NOT NULL,
      observation_count INTEGER NOT NULL,
      regional_semantic_eligible INTEGER NOT NULL,
      generation INTEGER NOT NULL,
      payload_json TEXT NOT NULL,
      path TEXT NOT NULL,
      created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS nonlinear_regional_parent_idx
      ON nonlinear_regional_assessments(parent_signature);
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
  std::ranges::sort(result);
  return result;
}

[[nodiscard]] Json read_envelope(const std::filesystem::path &path) {
  return contracts::parse_json(core::read_text(path));
}

void immutable_write(const std::filesystem::path &path, const Json &envelope) {
  if (!std::filesystem::exists(path)) {
    core::atomic_write_text(path, envelope.dump(2) + "\n");
    return;
  }
  auto existing = read_envelope(path);
  auto expected = envelope;
  existing.erase("created_at");
  expected.erase("created_at");
  if (existing != expected) {
    nonlinear_store_error("immutable nonlinear object collision at " +
                          path.string());
  }
}

[[nodiscard]] std::filesystem::path digest_path(
    const std::filesystem::path &root, std::string signature,
    std::string_view label) {
  signature = exact_sha(std::move(signature), label);
  return root / signature.substr(0, 2) / (signature + ".json");
}

[[nodiscard]] std::string local_behavior_signature(
    const saa::CanonicalNonlinearRepresentativeForm &form) {
  return contracts::sha256_json(
      {{"parent_representative_behavior_signature",
        form.parent_representative_behavior_signature},
       {"representation_version", form.representation_version},
       {"schema_version", 1},
       {"semantic_signature", form.semantic_signature},
       {"transformed_jet_coefficient_signature",
        form.transformed_jet.coefficient_signature},
       {"transformed_jet_scope_signature",
        form.transformed_jet.scope_signature}});
}

} // namespace

NonlinearCanonicalStore::NonlinearCanonicalStore(EgcfStore &egcf_store)
    : egcf_store_(egcf_store),
      root_(egcf_store_.state_root() / "nonlinear-canonical"),
      local_root_(root_ / "local" / "sha256"),
      evidence_root_(root_ / "evidence" / "sha256"),
      regional_root_(root_ / "regional" / "sha256"),
      projection_path_(root_ / "projection.sqlite3") {
  std::filesystem::create_directories(local_root_);
  std::filesystem::create_directories(evidence_root_);
  std::filesystem::create_directories(regional_root_);
  ensure_projection();
}

const std::filesystem::path &NonlinearCanonicalStore::root() const noexcept {
  return root_;
}

void NonlinearCanonicalStore::ensure_projection() {
  auto database = open_database(projection_path_);
  create_tables(database.get());
}

std::filesystem::path
NonlinearCanonicalStore::local_path(std::string_view signature) const {
  return digest_path(local_root_, std::string(signature),
                     "local nonlinear signature");
}

std::filesystem::path
NonlinearCanonicalStore::evidence_path(std::string_view signature) const {
  return digest_path(evidence_root_, std::string(signature),
                     "nonlinear evidence signature");
}

std::filesystem::path
NonlinearCanonicalStore::regional_path(std::string_view signature) const {
  return digest_path(regional_root_, std::string(signature),
                     "regional nonlinear signature");
}

int NonlinearCanonicalStore::generation_for(
    std::string_view table, std::string_view identity_column,
    std::string_view identity) {
  const std::set<std::pair<std::string_view, std::string_view>> allowed = {
      {"nonlinear_local_forms", "local_behavior_signature"},
      {"nonlinear_regional_assessments", "assessment_signature"}};
  if (!allowed.contains({table, identity_column})) {
    nonlinear_store_error("unsupported nonlinear generation lookup");
  }
  auto database = open_database(projection_path_);
  auto statement = prepare(
      database.get(), "SELECT generation FROM " + std::string(table) +
                          " WHERE " + std::string(identity_column) + " = ?");
  bind_text(statement.get(), 1, identity);
  if (sqlite3_step(statement.get()) == SQLITE_ROW) {
    return sqlite3_column_int(statement.get(), 0);
  }
  return 0;
}

int NonlinearCanonicalStore::next_generation(std::string_view table) {
  if (table != "nonlinear_local_forms" &&
      table != "nonlinear_regional_assessments") {
    nonlinear_store_error("unsupported nonlinear generation table");
  }
  auto database = open_database(projection_path_);
  auto statement = prepare(database.get(),
                           "SELECT COALESCE(MAX(generation), 0) FROM " +
                               std::string(table));
  if (sqlite3_step(statement.get()) != SQLITE_ROW) {
    nonlinear_store_error("cannot read nonlinear store generation");
  }
  return sqlite3_column_int(statement.get(), 0) + 1;
}

void NonlinearCanonicalStore::validate_local_admission(
    const saa::CanonicalNonlinearRepresentativeForm &form,
    const saa::GovernedJetEvidence &evidence) const {
  if (!form.local_canonical_eligible) {
    nonlinear_store_error(
        "SAA-7.4 refuses non-qualified local nonlinear forms");
  }
  if (form.global_equivalence_eligible) {
    nonlinear_store_error(
        "SAA-7.4 local store refuses objects claiming global nonlinear equivalence");
  }
  if (form.store_status !=
      "ELIGIBLE_LOCAL_NONLINEAR_REPRESENTATIVE_FORM") {
    nonlinear_store_error(
        "SAA-7.4 form does not carry local nonlinear admission status");
  }
  if (local_behavior_signature(form) !=
      form.local_representative_behavior_signature) {
    nonlinear_store_error(
        "SAA-7.4 local nonlinear behavior signature failed revalidation");
  }
  if (!evidence.exact || !evidence.canonical_local_eligible ||
      !evidence.jet) {
    nonlinear_store_error(
        "SAA-7.4 exact canonical local admission requires exact qualified evidence");
  }
  if (evidence.parent_representative_behavior_signature !=
      form.parent_representative_behavior_signature) {
    nonlinear_store_error(
        "SAA-7.4 evidence belongs to a different SAA-6 parent");
  }
  if (evidence.jet->local_behavior_signature != form.source_jet_signature) {
    nonlinear_store_error(
        "SAA-7.4 evidence does not ground the source jet used by the local form");
  }
  if (evidence.evidence_kind != "EXACT_SYMBOLIC_POLYNOMIAL" &&
      evidence.evidence_kind != "EXACT_DERIVATIVE_TABLE") {
    nonlinear_store_error(
        "SAA-7.4 exact store admission does not accept this evidence kind");
  }
}

NonlinearLocalAdmission NonlinearCanonicalStore::admit_local(
    const saa::CanonicalNonlinearRepresentativeForm &form,
    const saa::GovernedJetEvidence &evidence) {
  validate_local_admission(form, evidence);
  const std::string local_signature =
      form.local_representative_behavior_signature;
  const std::string local_id =
      "local-nonlinear:sha256:" + local_signature;
  const auto local_object_path = local_path(local_signature);
  const auto evidence_object_path = evidence_path(evidence.evidence_signature);
  const bool existed = std::filesystem::exists(local_object_path);
  int generation = generation_for("nonlinear_local_forms",
                                  "local_behavior_signature", local_signature);
  if (generation == 0) {
    generation = existed
                     ? read_envelope(local_object_path)
                           .value("store_generation", 0)
                     : next_generation("nonlinear_local_forms");
  }
  const std::string now = utc_now();
  const Json evidence_payload = saa::to_json(evidence);
  immutable_write(
      evidence_object_path,
      {{"created_at", now},
       {"object_id",
        "nonlinear-evidence:sha256:" + evidence.evidence_signature},
       {"object_type", "nonlinear-evidence"},
       {"payload", evidence_payload},
       {"schema_version", nonlinear_store_schema_version},
       {"store_version", nonlinear_store_version}});
  const Json local_payload = saa::to_json(form);
  immutable_write(
      local_object_path,
      {{"created_at", now},
       {"object_id", local_id},
       {"object_type", "local-nonlinear-canonical-form"},
       {"payload", local_payload},
       {"schema_version", nonlinear_store_schema_version},
       {"store_generation", generation},
       {"store_version", nonlinear_store_version}});

  auto database = open_database(projection_path_);
  execute(database.get(), "BEGIN IMMEDIATE");
  try {
    auto evidence_statement = prepare(
        database.get(),
        "INSERT OR IGNORE INTO nonlinear_evidence("
        "evidence_signature,evidence_kind,exact,canonical_local_eligible,"
        "source_snapshot_hash,payload_json,path,created_at) VALUES(?,?,?,?,?,?,?,?)");
    bind_text(evidence_statement.get(), 1, evidence.evidence_signature);
    bind_text(evidence_statement.get(), 2, evidence.evidence_kind);
    bind_int(evidence_statement.get(), 3, evidence.exact ? 1 : 0);
    bind_int(evidence_statement.get(), 4,
             evidence.canonical_local_eligible ? 1 : 0);
    bind_text(evidence_statement.get(), 5, evidence.source_snapshot_hash);
    bind_text(evidence_statement.get(), 6,
              contracts::canonical_json(evidence_payload));
    bind_text(evidence_statement.get(), 7,
              std::filesystem::relative(evidence_object_path, root_)
                  .generic_string());
    bind_text(evidence_statement.get(), 8, now);
    step_done(database.get(), evidence_statement.get());

    auto local_statement = prepare(
        database.get(),
        "INSERT OR IGNORE INTO nonlinear_local_forms("
        "local_id,parent_signature,local_behavior_signature,semantic_signature,"
        "source_jet_signature,coefficient_signature,scope_signature,jet_order,"
        "generation,payload_json,path,created_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)");
    bind_text(local_statement.get(), 1, local_id);
    bind_text(local_statement.get(), 2,
              form.parent_representative_behavior_signature);
    bind_text(local_statement.get(), 3, local_signature);
    bind_text(local_statement.get(), 4, form.semantic_signature);
    bind_text(local_statement.get(), 5, form.source_jet_signature);
    bind_text(local_statement.get(), 6,
              form.transformed_jet.coefficient_signature);
    bind_text(local_statement.get(), 7,
              form.transformed_jet.scope_signature);
    bind_int(local_statement.get(), 8,
             static_cast<int>(form.transformed_jet.order));
    bind_int(local_statement.get(), 9, generation);
    bind_text(local_statement.get(), 10,
              contracts::canonical_json(local_payload));
    bind_text(local_statement.get(), 11,
              std::filesystem::relative(local_object_path, root_)
                  .generic_string());
    bind_text(local_statement.get(), 12, now);
    step_done(database.get(), local_statement.get());

    auto link_statement = prepare(
        database.get(),
        "INSERT OR IGNORE INTO nonlinear_local_evidence(local_id,evidence_signature) VALUES(?,?)");
    bind_text(link_statement.get(), 1, local_id);
    bind_text(link_statement.get(), 2, evidence.evidence_signature);
    step_done(database.get(), link_statement.get());
    execute(database.get(), "COMMIT");
  } catch (...) {
    execute(database.get(), "ROLLBACK");
    throw;
  }
  return {.status = existed ? "REUSED_LOCAL_NONLINEAR_FORM"
                            : "ADMITTED_NEW_LOCAL_NONLINEAR_FORM",
          .local_id = local_id,
          .local_behavior_signature = local_signature,
          .generation = generation,
          .evidence_signature = evidence.evidence_signature};
}

NonlinearRegionalAdmission
NonlinearCanonicalStore::admit_regional_stability(
    const saa::SemanticStabilityAssessment &assessment) {
  if (!assessment.regional_semantic_eligible) {
    nonlinear_store_error(
        "SAA-7.4 refuses unresolved or transition-bearing regional semantics");
  }
  if (assessment.status != "REGIONALLY_STABLE_SEMANTICS") {
    nonlinear_store_error(
        "SAA-7.4 only persists qualified regionally stable semantic claims");
  }
  const auto known = local_signatures(
      assessment.parent_representative_behavior_signature);
  const std::set<std::string> known_set(known.begin(), known.end());
  if (!std::ranges::all_of(assessment.local_behavior_signatures,
                           [&](const auto &signature) {
                             return known_set.contains(signature);
                           })) {
    nonlinear_store_error(
        "SAA-7.4 regional assessment references local forms not admitted to this store");
  }
  const auto path = regional_path(assessment.assessment_signature);
  const bool existed = std::filesystem::exists(path);
  int generation = generation_for("nonlinear_regional_assessments",
                                  "assessment_signature",
                                  assessment.assessment_signature);
  if (generation == 0) {
    generation = existed ? read_envelope(path).value("store_generation", 0)
                         : next_generation(
                               "nonlinear_regional_assessments");
  }
  const std::string now = utc_now();
  const Json payload = saa::to_json(assessment);
  immutable_write(
      path,
      {{"created_at", now},
       {"object_id",
        "regional-semantics:sha256:" + assessment.assessment_signature},
       {"object_type", "regional-nonlinear-semantics"},
       {"payload", payload},
       {"schema_version", nonlinear_store_schema_version},
       {"store_generation", generation},
       {"store_version", nonlinear_store_version}});
  auto database = open_database(projection_path_);
  auto statement = prepare(
      database.get(),
      "INSERT OR IGNORE INTO nonlinear_regional_assessments("
      "assessment_signature,parent_signature,status,observation_count,"
      "regional_semantic_eligible,generation,payload_json,path,created_at) "
      "VALUES(?,?,?,?,?,?,?,?,?)");
  bind_text(statement.get(), 1, assessment.assessment_signature);
  bind_text(statement.get(), 2,
            assessment.parent_representative_behavior_signature);
  bind_text(statement.get(), 3, assessment.status);
  bind_int(statement.get(), 4,
           static_cast<int>(assessment.observation_count));
  bind_int(statement.get(), 5,
           assessment.regional_semantic_eligible ? 1 : 0);
  bind_int(statement.get(), 6, generation);
  bind_text(statement.get(), 7, contracts::canonical_json(payload));
  bind_text(statement.get(), 8,
            std::filesystem::relative(path, root_).generic_string());
  bind_text(statement.get(), 9, now);
  step_done(database.get(), statement.get());
  return {.status = existed ? "REUSED_REGIONAL_SEMANTIC_ASSESSMENT"
                            : "ADMITTED_REGIONAL_SEMANTIC_ASSESSMENT",
          .assessment_signature = assessment.assessment_signature,
          .generation = generation};
}

std::vector<std::string> NonlinearCanonicalStore::local_signatures(
    std::optional<std::string> parent_signature) {
  auto database = open_database(projection_path_);
  const std::string sql = parent_signature
                              ? "SELECT local_behavior_signature FROM nonlinear_local_forms WHERE parent_signature=? ORDER BY local_behavior_signature"
                              : "SELECT local_behavior_signature FROM nonlinear_local_forms ORDER BY local_behavior_signature";
  auto statement = prepare(database.get(), sql);
  if (parent_signature) {
    bind_text(statement.get(), 1, *parent_signature);
  }
  std::vector<std::string> result;
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    result.push_back(column_text(statement.get(), 0));
  }
  return result;
}

std::vector<Json> NonlinearCanonicalStore::list_local(
    std::optional<std::string> parent_signature) {
  auto database = open_database(projection_path_);
  const std::string sql = parent_signature
                              ? "SELECT path FROM nonlinear_local_forms WHERE parent_signature=? ORDER BY local_behavior_signature"
                              : "SELECT path FROM nonlinear_local_forms ORDER BY local_behavior_signature";
  auto statement = prepare(database.get(), sql);
  if (parent_signature) {
    bind_text(statement.get(), 1, *parent_signature);
  }
  std::vector<Json> result;
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    result.push_back(read_envelope(root_ / column_text(statement.get(), 0)));
  }
  return result;
}

std::vector<std::string> NonlinearCanonicalStore::evidence_for_local(
    std::string_view local_behavior_signature) {
  const std::string local_id =
      "local-nonlinear:sha256:" +
      exact_sha(std::string(local_behavior_signature),
                "local nonlinear signature");
  auto database = open_database(projection_path_);
  auto statement = prepare(
      database.get(),
      "SELECT evidence_signature FROM nonlinear_local_evidence WHERE local_id=? ORDER BY evidence_signature");
  bind_text(statement.get(), 1, local_id);
  std::vector<std::string> result;
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    result.push_back(column_text(statement.get(), 0));
  }
  return result;
}

std::vector<Json> NonlinearCanonicalStore::list_regional(
    std::optional<std::string> parent_signature) {
  auto database = open_database(projection_path_);
  const std::string sql = parent_signature
                              ? "SELECT path FROM nonlinear_regional_assessments WHERE parent_signature=? ORDER BY assessment_signature"
                              : "SELECT path FROM nonlinear_regional_assessments ORDER BY assessment_signature";
  auto statement = prepare(database.get(), sql);
  if (parent_signature) {
    bind_text(statement.get(), 1, *parent_signature);
  }
  std::vector<Json> result;
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    result.push_back(read_envelope(root_ / column_text(statement.get(), 0)));
  }
  return result;
}

void NonlinearCanonicalStore::rebuild_projection() {
  std::error_code error;
  std::filesystem::remove(projection_path_, error);
  if (error) {
    nonlinear_store_error("cannot replace nonlinear projection: " +
                          error.message());
  }
  ensure_projection();
  auto database = open_database(projection_path_);
  execute(database.get(), "BEGIN IMMEDIATE");
  try {
    for (const auto &path : json_files(local_root_)) {
      const Json envelope = read_envelope(path);
      const Json &payload = envelope.at("payload");
      const Json &jet = payload.at("transformed_jet");
      auto statement = prepare(
          database.get(),
          "INSERT INTO nonlinear_local_forms("
          "local_id,parent_signature,local_behavior_signature,semantic_signature,"
          "source_jet_signature,coefficient_signature,scope_signature,jet_order,"
          "generation,payload_json,path,created_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)");
      bind_text(statement.get(), 1,
                envelope.at("object_id").get<std::string>());
      bind_text(statement.get(), 2,
                payload.at("parent_representative_behavior_signature")
                    .get<std::string>());
      bind_text(statement.get(), 3,
                payload.at("local_representative_behavior_signature")
                    .get<std::string>());
      bind_text(statement.get(), 4,
                payload.at("semantic_signature").get<std::string>());
      bind_text(statement.get(), 5,
                payload.at("source_jet_signature").get<std::string>());
      bind_text(statement.get(), 6,
                jet.at("coefficient_signature").get<std::string>());
      bind_text(statement.get(), 7,
                jet.at("scope_signature").get<std::string>());
      bind_int(statement.get(), 8, jet.at("order").get<int>());
      bind_int(statement.get(), 9,
               envelope.value("store_generation", 0));
      bind_text(statement.get(), 10, contracts::canonical_json(payload));
      bind_text(statement.get(), 11,
                std::filesystem::relative(path, root_).generic_string());
      bind_text(statement.get(), 12, envelope.value("created_at", ""));
      step_done(database.get(), statement.get());
    }
    for (const auto &path : json_files(evidence_root_)) {
      const Json envelope = read_envelope(path);
      const Json &payload = envelope.at("payload");
      auto statement = prepare(
          database.get(),
          "INSERT INTO nonlinear_evidence("
          "evidence_signature,evidence_kind,exact,canonical_local_eligible,"
          "source_snapshot_hash,payload_json,path,created_at) VALUES(?,?,?,?,?,?,?,?)");
      bind_text(statement.get(), 1,
                payload.at("evidence_signature").get<std::string>());
      bind_text(statement.get(), 2,
                payload.at("evidence_kind").get<std::string>());
      bind_int(statement.get(), 3, payload.at("exact").get<bool>() ? 1 : 0);
      bind_int(statement.get(), 4,
               payload.at("canonical_local_eligible").get<bool>() ? 1 : 0);
      bind_text(statement.get(), 5,
                payload.at("source_snapshot_hash").get<std::string>());
      bind_text(statement.get(), 6, contracts::canonical_json(payload));
      bind_text(statement.get(), 7,
                std::filesystem::relative(path, root_).generic_string());
      bind_text(statement.get(), 8, envelope.value("created_at", ""));
      step_done(database.get(), statement.get());
    }
    for (const auto &path : json_files(regional_root_)) {
      const Json envelope = read_envelope(path);
      const Json &payload = envelope.at("payload");
      auto statement = prepare(
          database.get(),
          "INSERT INTO nonlinear_regional_assessments("
          "assessment_signature,parent_signature,status,observation_count,"
          "regional_semantic_eligible,generation,payload_json,path,created_at) VALUES(?,?,?,?,?,?,?,?,?)");
      bind_text(statement.get(), 1,
                payload.at("assessment_signature").get<std::string>());
      bind_text(statement.get(), 2,
                payload.at("parent_representative_behavior_signature")
                    .get<std::string>());
      bind_text(statement.get(), 3,
                payload.at("status").get<std::string>());
      bind_int(statement.get(), 4,
               payload.at("observation_count").get<int>());
      bind_int(statement.get(), 5,
               payload.at("regional_semantic_eligible").get<bool>() ? 1 : 0);
      bind_int(statement.get(), 6,
               envelope.value("store_generation", 0));
      bind_text(statement.get(), 7, contracts::canonical_json(payload));
      bind_text(statement.get(), 8,
                std::filesystem::relative(path, root_).generic_string());
      bind_text(statement.get(), 9, envelope.value("created_at", ""));
      step_done(database.get(), statement.get());
    }

    auto evidence = prepare(
        database.get(),
        "SELECT evidence_signature,payload_json FROM nonlinear_evidence WHERE exact=1 AND canonical_local_eligible=1");
    while (sqlite3_step(evidence.get()) == SQLITE_ROW) {
      const std::string evidence_signature = column_text(evidence.get(), 0);
      const Json payload = contracts::parse_json(column_text(evidence.get(), 1));
      if (!payload.contains("jet") || payload.at("jet").is_null()) {
        continue;
      }
      const std::string source =
          payload.at("jet").at("local_behavior_signature").get<std::string>();
      auto locals = prepare(
          database.get(),
          "SELECT local_id FROM nonlinear_local_forms WHERE source_jet_signature=?");
      bind_text(locals.get(), 1, source);
      while (sqlite3_step(locals.get()) == SQLITE_ROW) {
        auto link = prepare(
            database.get(),
            "INSERT OR IGNORE INTO nonlinear_local_evidence(local_id,evidence_signature) VALUES(?,?)");
        bind_text(link.get(), 1, column_text(locals.get(), 0));
        bind_text(link.get(), 2, evidence_signature);
        step_done(database.get(), link.get());
      }
    }
    execute(database.get(), "COMMIT");
  } catch (...) {
    execute(database.get(), "ROLLBACK");
    throw;
  }
}

Json to_json(const NonlinearLocalAdmission &value) {
  return {{"evidence_signature", value.evidence_signature},
          {"generation", value.generation},
          {"local_behavior_signature", value.local_behavior_signature},
          {"local_id", value.local_id},
          {"status", value.status}};
}

Json to_json(const NonlinearRegionalAdmission &value) {
  return {{"assessment_signature", value.assessment_signature},
          {"generation", value.generation},
          {"status", value.status}};
}

} // namespace statewright::egcf
