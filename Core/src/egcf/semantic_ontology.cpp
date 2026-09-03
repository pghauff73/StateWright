#include "statewright/egcf/semantic_ontology.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/typed_id.hpp"
#include "statewright/core/file_io.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <ctime>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <sstream>
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

[[noreturn]] void ontology_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument,
                      std::move(message));
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

[[nodiscard]] std::string utc_now() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t value = std::chrono::system_clock::to_time_t(now);
  std::tm parts{};
  gmtime_r(&value, &parts);
  std::array<char, 32> buffer{};
  if (std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ",
                    &parts) == 0U) {
    ontology_error("cannot format semantic ontology timestamp");
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
    ontology_error("cannot open semantic ontology projection: " + message);
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
    ontology_error("semantic ontology projection SQL failed: " + text);
  }
}

[[nodiscard]] Statement prepare(sqlite3 *database, std::string_view sql) {
  sqlite3_stmt *raw = nullptr;
  if (sqlite3_prepare_v2(database, std::string(sql).c_str(), -1, &raw,
                         nullptr) != SQLITE_OK) {
    ontology_error("cannot prepare semantic ontology SQL: " +
                   std::string(sqlite3_errmsg(database)));
  }
  return Statement(raw);
}

void bind_text(sqlite3_stmt *statement, int index, std::string_view value) {
  if (sqlite3_bind_text(statement, index, value.data(),
                        static_cast<int>(value.size()), SQLITE_TRANSIENT) !=
      SQLITE_OK) {
    ontology_error("cannot bind semantic ontology text");
  }
}

void bind_int(sqlite3_stmt *statement, int index, int value) {
  if (sqlite3_bind_int(statement, index, value) != SQLITE_OK) {
    ontology_error("cannot bind semantic ontology integer");
  }
}

void step_done(sqlite3 *database, sqlite3_stmt *statement) {
  if (sqlite3_step(statement) != SQLITE_DONE) {
    ontology_error("cannot update semantic ontology projection: " +
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
    CREATE TABLE IF NOT EXISTS semantic_ontology_concepts (
      concept_id TEXT PRIMARY KEY,
      concept_signature TEXT NOT NULL UNIQUE,
      canonical_name TEXT NOT NULL,
      meaning TEXT NOT NULL,
      domain TEXT NOT NULL,
      quantity_kind TEXT NOT NULL,
      dimension_signature TEXT NOT NULL,
      aliases_json TEXT NOT NULL,
      payload_json TEXT NOT NULL,
      path TEXT NOT NULL,
      created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS semantic_ontology_name_idx
      ON semantic_ontology_concepts(canonical_name);
    CREATE INDEX IF NOT EXISTS semantic_ontology_meaning_idx
      ON semantic_ontology_concepts(meaning);
    CREATE INDEX IF NOT EXISTS semantic_ontology_quantity_idx
      ON semantic_ontology_concepts(quantity_kind);
    CREATE TABLE IF NOT EXISTS semantic_ontology_alignments (
      alignment_id TEXT PRIMARY KEY,
      left_concept_id TEXT NOT NULL,
      right_concept_id TEXT NOT NULL,
      relation TEXT NOT NULL,
      status TEXT NOT NULL,
      exact_substitution_eligible INTEGER NOT NULL,
      payload_json TEXT NOT NULL,
      path TEXT NOT NULL,
      created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS semantic_ontology_alignment_left_idx
      ON semantic_ontology_alignments(left_concept_id);
    CREATE INDEX IF NOT EXISTS semantic_ontology_alignment_right_idx
      ON semantic_ontology_alignments(right_concept_id);
    CREATE TABLE IF NOT EXISTS semantic_ontology_revisions (
      revision_id TEXT PRIMARY KEY,
      source_concept_id TEXT NOT NULL,
      replacement_concept_id TEXT NOT NULL,
      payload_json TEXT NOT NULL,
      path TEXT NOT NULL,
      created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS semantic_ontology_revision_source_idx
      ON semantic_ontology_revisions(source_concept_id);
    CREATE TABLE IF NOT EXISTS semantic_ontology_metadata (
      key TEXT PRIMARY KEY,
      value TEXT NOT NULL
    );
  )SQL");
}

[[nodiscard]] std::string concept_id(std::string_view signature) {
  return "semantic-concept:sha256:" + std::string(signature);
}

[[nodiscard]] std::string alignment_id(std::string_view signature) {
  return "semantic-alignment:sha256:" + std::string(signature);
}

[[nodiscard]] std::string revision_id(std::string_view signature) {
  return "semantic-requalification:sha256:" + std::string(signature);
}

[[nodiscard]] std::array<int, saa::physical_dimension_count>
parse_exponents(const Json &value) {
  if (!value.is_array() || value.size() != saa::physical_dimension_count) {
    ontology_error("invalid semantic ontology dimension payload");
  }
  std::array<int, saa::physical_dimension_count> result{};
  for (std::size_t index = 0; index < result.size(); ++index) {
    result[index] = value.at(index).get<int>();
  }
  return result;
}

[[nodiscard]] mpq_class parse_fraction(const Json &value) {
  if (!value.is_array() || value.size() != 2U) {
    ontology_error("invalid semantic ontology rational payload");
  }
  mpq_class result(value.at(0).get<long>(), value.at(1).get<long>());
  result.canonicalize();
  return result;
}

[[nodiscard]] saa::SemanticConcept concept_from_payload(const Json &payload) {
  std::optional<saa::PhysicalDimensionVector> dimension;
  if (!payload.at("physical_dimension").is_null()) {
    dimension = saa::PhysicalDimensionVector(parse_exponents(
        payload.at("physical_dimension").at("exponents")));
  }
  std::optional<saa::PhysicalUnit> unit;
  if (!payload.at("canonical_unit").is_null()) {
    const auto &unit_payload = payload.at("canonical_unit");
    const saa::PhysicalDimensionVector unit_dimension(parse_exponents(
        unit_payload.at("dimension").at("exponents")));
    unit.emplace(unit_payload.at("symbol").get<std::string>(),
                 unit_payload.at("name").get<std::string>(), unit_dimension,
                 parse_fraction(unit_payload.at("scale_to_si")),
                 parse_fraction(unit_payload.at("offset_to_si")));
    if (unit->signature() != unit_payload.at("signature").get<std::string>()) {
      ontology_error("semantic ontology unit signature mismatch");
    }
  }
  auto result = saa::make_semantic_concept(
      payload.at("canonical_name").get<std::string>(),
      payload.at("meaning").get<std::string>(),
      payload.at("domain").get<std::string>(),
      payload.at("quantity_kind").get<std::string>(),
      payload.at("aliases").get<std::vector<std::string>>(), dimension, unit,
      payload.at("evidence_ids").get<std::vector<std::string>>(),
      payload.at("semantic_status").get<std::string>());
  if (result.concept_signature !=
          payload.at("concept_signature").get<std::string>() ||
      result.canonical_eligible !=
          payload.at("canonical_eligible").get<bool>()) {
    ontology_error("semantic ontology concept payload signature mismatch");
  }
  return result;
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
    if (existing.at("object_id") != envelope.at("object_id") ||
        existing.at("schema_version") != envelope.at("schema_version") ||
        existing.at("ontology_version") != envelope.at("ontology_version") ||
        contracts::canonical_json(existing.at("payload")) !=
            contracts::canonical_json(envelope.at("payload"))) {
      ontology_error("immutable semantic-ontology collision at " +
                     path.string());
    }
    return;
  }
  core::atomic_write_text(path, envelope.dump(2) + "\n");
}

[[nodiscard]] Json read_envelope(const std::filesystem::path &path) {
  return contracts::parse_json(core::read_text(path));
}

} // namespace

SemanticOntologyStore::SemanticOntologyStore(EgcfStore &egcf_store)
    : egcf_store_(egcf_store), state_root_(egcf_store.state_root()),
      root_(state_root_ / "semantic-ontology"),
      concept_root_(root_ / "concepts" / "sha256"),
      alignment_root_(root_ / "alignments" / "sha256"),
      revision_root_(root_ / "revisions" / "sha256"),
      projection_path_(egcf_store.projection_path()) {
  std::filesystem::create_directories(concept_root_);
  std::filesystem::create_directories(alignment_root_);
  std::filesystem::create_directories(revision_root_);
  ensure_projection();
}

const std::filesystem::path &SemanticOntologyStore::root() const noexcept {
  return root_;
}

saa::SemanticEvidenceResolver SemanticOntologyStore::evidence_resolver() const {
  return [this](std::string_view evidence_id_value)
             -> std::optional<saa::SemanticGroundingEvidence> {
    try {
      const auto record = egcf_store_.get(evidence_id_value);
      saa::SemanticGroundingEvidence result;
      result.object_type = record.object_type;
      if (record.payload.contains("success") &&
          !record.payload.at("success").is_null()) {
        result.success = record.payload.at("success").get<bool>();
      }
      result.simulated = record.payload.value("simulated", false);
      result.producer = record.payload.value("producer", "");
      result.method = record.payload.value("method", "");
      return result;
    } catch (const std::exception &) {
      return std::nullopt;
    }
  };
}

void SemanticOntologyStore::verify_concept_evidence(
    const saa::SemanticConcept &semantic_concept) const {
  if (semantic_concept.evidence_ids.empty()) {
    ontology_error("semantic ontology concept requires grounded evidence");
  }
  const auto resolver = evidence_resolver();
  for (const auto &evidence_id_value : semantic_concept.evidence_ids) {
    const auto evidence = resolver(evidence_id_value);
    if (!evidence) {
      ontology_error("semantic concept evidence is not registered: " +
                     evidence_id_value);
    }
    if (evidence->object_type != "egcf-evidence") {
      ontology_error(
          "semantic concept evidence ID does not reference EvidenceArtifact");
    }
    if (evidence->success != true || evidence->simulated) {
      ontology_error(
          "semantic concept evidence must be successful and non-simulated");
    }
    if (!evidence->producer.starts_with("deterministic-") &&
        !evidence->producer.starts_with("human-")) {
      ontology_error(
          "semantic concept evidence requires deterministic or human grounding");
    }
    if (evidence->method == "reported" ||
        evidence->method == "model-claimed" ||
        evidence->method == "model-generated-claim") {
      ontology_error(
          "reported/model-claimed evidence cannot ground canonical semantic concepts");
    }
  }
}

std::filesystem::path SemanticOntologyStore::path_for(
    const std::filesystem::path &object_root, std::string_view object_id_value,
    std::string_view expected_kind) const {
  const auto parts = contracts::parse_typed_id(object_id_value);
  if (parts.object_type != expected_kind) {
    ontology_error("semantic ontology expected " + std::string(expected_kind) +
                   " ID");
  }
  return object_root / parts.digest.substr(0, 2) /
         (parts.digest + ".json");
}

void SemanticOntologyStore::ensure_projection() {
  auto database = open_database(projection_path_);
  create_tables(database.get());
  auto marker = prepare(
      database.get(),
      "SELECT value FROM semantic_ontology_metadata WHERE key='schema_version'");
  const bool current = sqlite3_step(marker.get()) == SQLITE_ROW &&
                       column_text(marker.get(), 0) ==
                           std::to_string(semantic_ontology_schema_version);
  marker.reset();
  database.reset();
  if (!current) {
    rebuild_projection();
  }
}

std::string SemanticOntologyStore::admit_concept(
    const saa::SemanticConcept &semantic_concept) {
  if (!semantic_concept.canonical_eligible ||
      semantic_concept.semantic_status != "SEMANTICALLY_RESOLVED") {
    ontology_error(
        "semantic ontology admits only evidence-grounded resolved concepts");
  }
  verify_concept_evidence(semantic_concept);
  const std::string id = concept_id(semantic_concept.concept_signature);
  const Json payload = saa::to_json(semantic_concept);
  const auto path = path_for(concept_root_, id, "semantic-concept");
  const Json envelope = {{"created_at", utc_now()},
                         {"object_id", id},
                         {"ontology_version", semantic_ontology_version},
                         {"payload", payload},
                         {"schema_version", 1}};
  immutable_write(path, envelope);
  const Json stored = read_envelope(path);
  ensure_projection();
  auto database = open_database(projection_path_);
  auto statement = prepare(
      database.get(),
      "INSERT OR IGNORE INTO semantic_ontology_concepts(concept_id,concept_signature,canonical_name,meaning,domain,quantity_kind,dimension_signature,aliases_json,payload_json,path,created_at) VALUES(?,?,?,?,?,?,?,?,?,?,?)");
  bind_text(statement.get(), 1, id);
  bind_text(statement.get(), 2, semantic_concept.concept_signature);
  bind_text(statement.get(), 3, semantic_concept.canonical_name);
  bind_text(statement.get(), 4, semantic_concept.meaning);
  bind_text(statement.get(), 5, semantic_concept.domain);
  bind_text(statement.get(), 6, semantic_concept.quantity_kind);
  bind_text(statement.get(), 7,
            semantic_concept.physical_dimension
                ? semantic_concept.physical_dimension->signature()
                : "");
  bind_text(statement.get(), 8,
            contracts::canonical_json(semantic_concept.aliases));
  bind_text(statement.get(), 9, contracts::canonical_json(payload));
  bind_text(statement.get(), 10,
            path.lexically_relative(state_root_).generic_string());
  bind_text(statement.get(), 11,
            stored.at("created_at").get_ref<const std::string &>());
  step_done(database.get(), statement.get());
  execute(database.get(),
          "INSERT OR REPLACE INTO semantic_ontology_metadata(key,value) "
          "VALUES('schema_version','1')");
  return id;
}

saa::SemanticConcept
SemanticOntologyStore::load_concept(std::string_view concept_id_value) const {
  const auto path =
      path_for(concept_root_, concept_id_value, "semantic-concept");
  Json envelope;
  try {
    envelope = read_envelope(path);
  } catch (const std::exception &error) {
    ontology_error("cannot read semantic concept " +
                   std::string(concept_id_value) + ": " + error.what());
  }
  if (envelope.at("object_id") != concept_id_value) {
    ontology_error("semantic concept object identity mismatch");
  }
  auto result = concept_from_payload(envelope.at("payload"));
  if (concept_id(result.concept_signature) != concept_id_value) {
    ontology_error("semantic concept signature does not match object ID");
  }
  return result;
}

std::vector<Json> SemanticOntologyStore::concepts() {
  ensure_projection();
  auto database = open_database(projection_path_);
  auto statement = prepare(
      database.get(),
      "SELECT concept_id,payload_json,created_at FROM semantic_ontology_concepts ORDER BY concept_id");
  std::vector<Json> result;
  while (true) {
    const int status = sqlite3_step(statement.get());
    if (status == SQLITE_DONE) {
      break;
    }
    if (status != SQLITE_ROW) {
      ontology_error("cannot read semantic ontology concepts");
    }
    result.push_back({{"concept_id", column_text(statement.get(), 0)},
                      {"payload",
                       contracts::parse_json(column_text(statement.get(), 1))},
                      {"created_at", column_text(statement.get(), 2)}});
  }
  return result;
}

std::vector<std::string>
SemanticOntologyStore::resolve_text(std::string_view text) {
  const std::string target = normalized_text(std::string(text));
  if (target.empty()) {
    return {};
  }
  std::vector<std::string> matches;
  for (const auto &item : concepts()) {
    const auto &payload = item.at("payload");
    std::set<std::string> names = {
        normalized_text(payload.at("canonical_name").get<std::string>()),
        normalized_text(payload.at("meaning").get<std::string>())};
    for (const auto &alias : payload.at("aliases")) {
      names.insert(normalized_text(alias.get<std::string>()));
    }
    if (names.contains(target)) {
      matches.push_back(item.at("concept_id").get<std::string>());
    }
  }
  std::sort(matches.begin(), matches.end());
  return matches;
}

std::string SemanticOntologyStore::admit_alignment(
    const saa::SemanticAlignmentAssessment &assessment) {
  if (!assessment.canonical_alignment_eligible) {
    ontology_error(
        "unresolved semantic alignment cannot enter canonical ontology");
  }
  const std::string left_id = concept_id(assessment.left_concept_signature);
  const std::string right_id = concept_id(assessment.right_concept_signature);
  static_cast<void>(load_concept(left_id));
  static_cast<void>(load_concept(right_id));
  const std::string id = alignment_id(assessment.alignment_signature);
  const Json payload = saa::to_json(assessment);
  const auto path = path_for(alignment_root_, id, "semantic-alignment");
  const Json envelope = {{"created_at", utc_now()},
                         {"object_id", id},
                         {"ontology_version", semantic_ontology_version},
                         {"payload", payload},
                         {"schema_version", 1}};
  immutable_write(path, envelope);
  const Json stored = read_envelope(path);
  ensure_projection();
  auto database = open_database(projection_path_);
  auto statement = prepare(
      database.get(),
      "INSERT OR IGNORE INTO semantic_ontology_alignments(alignment_id,left_concept_id,right_concept_id,relation,status,exact_substitution_eligible,payload_json,path,created_at) VALUES(?,?,?,?,?,?,?,?,?)");
  bind_text(statement.get(), 1, id);
  bind_text(statement.get(), 2, left_id);
  bind_text(statement.get(), 3, right_id);
  bind_text(statement.get(), 4, assessment.relation);
  bind_text(statement.get(), 5, assessment.status);
  bind_int(statement.get(), 6,
           assessment.exact_substitution_eligible ? 1 : 0);
  bind_text(statement.get(), 7, contracts::canonical_json(payload));
  bind_text(statement.get(), 8,
            path.lexically_relative(state_root_).generic_string());
  bind_text(statement.get(), 9,
            stored.at("created_at").get_ref<const std::string &>());
  step_done(database.get(), statement.get());
  return id;
}

std::vector<Json> SemanticOntologyStore::alignments() {
  ensure_projection();
  auto database = open_database(projection_path_);
  auto statement = prepare(
      database.get(),
      "SELECT alignment_id,left_concept_id,right_concept_id,relation,status,exact_substitution_eligible,payload_json FROM semantic_ontology_alignments ORDER BY alignment_id");
  std::vector<Json> result;
  while (true) {
    const int status = sqlite3_step(statement.get());
    if (status == SQLITE_DONE) {
      break;
    }
    if (status != SQLITE_ROW) {
      ontology_error("cannot read semantic ontology alignments");
    }
    result.push_back(
        {{"alignment_id", column_text(statement.get(), 0)},
         {"left_concept_id", column_text(statement.get(), 1)},
         {"right_concept_id", column_text(statement.get(), 2)},
         {"relation", column_text(statement.get(), 3)},
         {"status", column_text(statement.get(), 4)},
         {"exact_substitution_eligible",
          sqlite3_column_int(statement.get(), 5) != 0},
         {"payload", contracts::parse_json(column_text(statement.get(), 6))}});
  }
  return result;
}

std::string SemanticOntologyStore::admit_revision(
    const saa::SemanticRequalification &requalification) {
  if (!requalification.canonical_replacement_eligible ||
      !requalification.replacement_concept) {
    ontology_error(
        "blocked semantic requalification cannot enter canonical ontology");
  }
  const std::string source_id =
      concept_id(requalification.source_concept_signature);
  static_cast<void>(load_concept(source_id));
  const std::string replacement_id =
      admit_concept(*requalification.replacement_concept);
  const std::string id =
      revision_id(requalification.requalification_signature);
  Json payload = saa::to_json(requalification);
  payload["source_concept_id"] = source_id;
  payload["replacement_concept_id"] = replacement_id;
  const auto path =
      path_for(revision_root_, id, "semantic-requalification");
  const Json envelope = {{"created_at", utc_now()},
                         {"object_id", id},
                         {"ontology_version", semantic_ontology_version},
                         {"payload", payload},
                         {"schema_version", 1}};
  immutable_write(path, envelope);
  const Json stored = read_envelope(path);
  ensure_projection();
  auto database = open_database(projection_path_);
  auto statement = prepare(
      database.get(),
      "INSERT OR IGNORE INTO semantic_ontology_revisions(revision_id,source_concept_id,replacement_concept_id,payload_json,path,created_at) VALUES(?,?,?,?,?,?)");
  bind_text(statement.get(), 1, id);
  bind_text(statement.get(), 2, source_id);
  bind_text(statement.get(), 3, replacement_id);
  bind_text(statement.get(), 4, contracts::canonical_json(payload));
  bind_text(statement.get(), 5,
            path.lexically_relative(state_root_).generic_string());
  bind_text(statement.get(), 6,
            stored.at("created_at").get_ref<const std::string &>());
  step_done(database.get(), statement.get());
  return id;
}

std::vector<Json> SemanticOntologyStore::revisions() {
  ensure_projection();
  auto database = open_database(projection_path_);
  auto statement = prepare(
      database.get(),
      "SELECT revision_id,source_concept_id,replacement_concept_id,payload_json FROM semantic_ontology_revisions ORDER BY revision_id");
  std::vector<Json> result;
  while (true) {
    const int status = sqlite3_step(statement.get());
    if (status == SQLITE_DONE) {
      break;
    }
    if (status != SQLITE_ROW) {
      ontology_error("cannot read semantic ontology revisions");
    }
    result.push_back(
        {{"revision_id", column_text(statement.get(), 0)},
         {"source_concept_id", column_text(statement.get(), 1)},
         {"replacement_concept_id", column_text(statement.get(), 2)},
         {"payload", contracts::parse_json(column_text(statement.get(), 3))}});
  }
  return result;
}

std::vector<std::string> SemanticOntologyStore::equivalent_concept_ids(
    std::string_view concept_id_value) {
  static_cast<void>(load_concept(concept_id_value));
  std::map<std::string, std::set<std::string>> adjacency;
  for (const auto &item : alignments()) {
    if (!item.at("exact_substitution_eligible").get<bool>()) {
      continue;
    }
    const auto left = item.at("left_concept_id").get<std::string>();
    const auto right = item.at("right_concept_id").get<std::string>();
    adjacency[left].insert(right);
    adjacency[right].insert(left);
  }
  std::set<std::string> seen = {std::string(concept_id_value)};
  std::deque<std::string> queue = {std::string(concept_id_value)};
  while (!queue.empty()) {
    const std::string current = queue.front();
    queue.pop_front();
    for (const auto &neighbor : adjacency[current]) {
      if (seen.insert(neighbor).second) {
        queue.push_back(neighbor);
      }
    }
  }
  return {seen.begin(), seen.end()};
}

bool SemanticOntologyStore::meanings_equivalent(std::string_view left,
                                                std::string_view right) {
  if (normalized_text(std::string(left)) ==
      normalized_text(std::string(right))) {
    return true;
  }
  const auto left_ids = resolve_text(left);
  const auto resolved_right_ids = resolve_text(right);
  const std::set<std::string> right_ids(resolved_right_ids.begin(),
                                        resolved_right_ids.end());
  for (const auto &left_id : left_ids) {
    const auto equivalents = equivalent_concept_ids(left_id);
    if (std::any_of(equivalents.begin(), equivalents.end(),
                    [&](const auto &candidate) {
                      return right_ids.contains(candidate);
                    })) {
      return true;
    }
  }
  return false;
}

void SemanticOntologyStore::rebuild_projection() {
  auto database = open_database(projection_path_);
  create_tables(database.get());
  execute(database.get(), "BEGIN IMMEDIATE");
  try {
    execute(database.get(), "DELETE FROM semantic_ontology_concepts");
    execute(database.get(), "DELETE FROM semantic_ontology_alignments");
    execute(database.get(), "DELETE FROM semantic_ontology_revisions");
    execute(database.get(), "DELETE FROM semantic_ontology_metadata");

    auto concept_insert = prepare(
        database.get(),
        "INSERT INTO semantic_ontology_concepts VALUES(?,?,?,?,?,?,?,?,?,?,?)");
    for (const auto &path : json_files(concept_root_)) {
      const Json envelope = read_envelope(path);
      const auto semantic_concept =
          concept_from_payload(envelope.at("payload"));
      const std::string id = envelope.at("object_id").get<std::string>();
      if (concept_id(semantic_concept.concept_signature) != id) {
        ontology_error("invalid semantic concept entry: " + path.string());
      }
      sqlite3_reset(concept_insert.get());
      sqlite3_clear_bindings(concept_insert.get());
      bind_text(concept_insert.get(), 1, id);
      bind_text(concept_insert.get(), 2, semantic_concept.concept_signature);
      bind_text(concept_insert.get(), 3, semantic_concept.canonical_name);
      bind_text(concept_insert.get(), 4, semantic_concept.meaning);
      bind_text(concept_insert.get(), 5, semantic_concept.domain);
      bind_text(concept_insert.get(), 6, semantic_concept.quantity_kind);
      bind_text(concept_insert.get(), 7,
                semantic_concept.physical_dimension
                    ? semantic_concept.physical_dimension->signature()
                    : "");
      bind_text(concept_insert.get(), 8,
                contracts::canonical_json(semantic_concept.aliases));
      bind_text(concept_insert.get(), 9,
                contracts::canonical_json(envelope.at("payload")));
      bind_text(concept_insert.get(), 10,
                path.lexically_relative(state_root_).generic_string());
      bind_text(concept_insert.get(), 11,
                envelope.at("created_at").get_ref<const std::string &>());
      step_done(database.get(), concept_insert.get());
    }

    auto alignment_insert = prepare(
        database.get(),
        "INSERT INTO semantic_ontology_alignments VALUES(?,?,?,?,?,?,?,?,?)");
    for (const auto &path : json_files(alignment_root_)) {
      const Json envelope = read_envelope(path);
      const Json &payload = envelope.at("payload");
      const std::string id = envelope.at("object_id").get<std::string>();
      if (alignment_id(payload.at("alignment_signature").get<std::string>()) !=
          id) {
        ontology_error("invalid semantic alignment entry: " + path.string());
      }
      sqlite3_reset(alignment_insert.get());
      sqlite3_clear_bindings(alignment_insert.get());
      bind_text(alignment_insert.get(), 1, id);
      bind_text(alignment_insert.get(), 2,
                concept_id(payload.at("left_concept_signature")
                               .get<std::string>()));
      bind_text(alignment_insert.get(), 3,
                concept_id(payload.at("right_concept_signature")
                               .get<std::string>()));
      bind_text(alignment_insert.get(), 4,
                payload.at("relation").get_ref<const std::string &>());
      bind_text(alignment_insert.get(), 5,
                payload.at("status").get_ref<const std::string &>());
      bind_int(alignment_insert.get(), 6,
               payload.at("exact_substitution_eligible").get<bool>() ? 1 : 0);
      bind_text(alignment_insert.get(), 7,
                contracts::canonical_json(payload));
      bind_text(alignment_insert.get(), 8,
                path.lexically_relative(state_root_).generic_string());
      bind_text(alignment_insert.get(), 9,
                envelope.at("created_at").get_ref<const std::string &>());
      step_done(database.get(), alignment_insert.get());
    }

    auto revision_insert = prepare(
        database.get(),
        "INSERT INTO semantic_ontology_revisions VALUES(?,?,?,?,?,?)");
    for (const auto &path : json_files(revision_root_)) {
      const Json envelope = read_envelope(path);
      const Json &payload = envelope.at("payload");
      const std::string id = envelope.at("object_id").get<std::string>();
      if (revision_id(payload.at("requalification_signature")
                          .get<std::string>()) != id) {
        ontology_error("invalid semantic revision entry: " + path.string());
      }
      sqlite3_reset(revision_insert.get());
      sqlite3_clear_bindings(revision_insert.get());
      bind_text(revision_insert.get(), 1, id);
      bind_text(revision_insert.get(), 2,
                payload.at("source_concept_id")
                    .get_ref<const std::string &>());
      bind_text(revision_insert.get(), 3,
                payload.at("replacement_concept_id")
                    .get_ref<const std::string &>());
      bind_text(revision_insert.get(), 4, contracts::canonical_json(payload));
      bind_text(revision_insert.get(), 5,
                path.lexically_relative(state_root_).generic_string());
      bind_text(revision_insert.get(), 6,
                envelope.at("created_at").get_ref<const std::string &>());
      step_done(database.get(), revision_insert.get());
    }
    auto metadata = prepare(
        database.get(),
        "INSERT INTO semantic_ontology_metadata(key,value) VALUES(?,?)");
    for (const auto &[key, value] :
         std::vector<std::pair<std::string, std::string>>{
             {"schema_version",
              std::to_string(semantic_ontology_schema_version)},
             {"rebuilt_at", utc_now()}}) {
      sqlite3_reset(metadata.get());
      sqlite3_clear_bindings(metadata.get());
      bind_text(metadata.get(), 1, key);
      bind_text(metadata.get(), 2, value);
      step_done(database.get(), metadata.get());
    }
    execute(database.get(), "COMMIT");
  } catch (...) {
    sqlite3_exec(database.get(), "ROLLBACK", nullptr, nullptr, nullptr);
    throw;
  }
}

} // namespace statewright::egcf
