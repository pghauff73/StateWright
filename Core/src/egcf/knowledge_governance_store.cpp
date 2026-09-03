#include "statewright/egcf/knowledge_governance_store.hpp"

#include "ledger_support.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <optional>
#include <ranges>
#include <set>
#include <string_view>
#include <utility>

namespace statewright::egcf {
namespace {

using Json = contracts::Json;
using namespace ledger_support;
inline constexpr std::string_view store_label =
    "knowledge governance store";

void create_tables(sqlite3 *database) {
  execute(database, R"SQL(
    CREATE TABLE IF NOT EXISTS saa_failure_patterns (
      pattern_ref TEXT PRIMARY KEY, pattern_signature TEXT NOT NULL UNIQUE,
      failure_class TEXT NOT NULL, component TEXT NOT NULL,
      mechanism TEXT NOT NULL, payload_json TEXT NOT NULL,
      path TEXT NOT NULL, created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS saa_failure_patterns_class_idx
      ON saa_failure_patterns(failure_class);
    CREATE INDEX IF NOT EXISTS saa_failure_patterns_component_idx
      ON saa_failure_patterns(component);
    CREATE TABLE IF NOT EXISTS saa_failure_occurrences (
      occurrence_ref TEXT PRIMARY KEY,
      observation_signature TEXT NOT NULL UNIQUE,
      pattern_signature TEXT NOT NULL, provenance_id TEXT NOT NULL,
      payload_json TEXT NOT NULL, path TEXT NOT NULL, created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS saa_failure_occurrence_pattern_idx
      ON saa_failure_occurrences(pattern_signature);
    CREATE TABLE IF NOT EXISTS saa_benchmark_gates (
      gate_ref TEXT PRIMARY KEY, assessment_signature TEXT NOT NULL UNIQUE,
      candidate_ref TEXT NOT NULL, status TEXT NOT NULL,
      promotion_eligible INTEGER NOT NULL, payload_json TEXT NOT NULL,
      path TEXT NOT NULL, created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS saa_benchmark_candidate_idx
      ON saa_benchmark_gates(candidate_ref);
    CREATE TABLE IF NOT EXISTS saa_integrity_snapshots (
      snapshot_ref TEXT PRIMARY KEY, snapshot_signature TEXT NOT NULL UNIQUE,
      generation INTEGER NOT NULL UNIQUE, payload_json TEXT NOT NULL,
      path TEXT NOT NULL, created_at TEXT NOT NULL
    );
    CREATE TABLE IF NOT EXISTS saa_integrity_trajectories (
      trajectory_ref TEXT PRIMARY KEY,
      trajectory_signature TEXT NOT NULL UNIQUE,
      latest_generation INTEGER NOT NULL, status TEXT NOT NULL,
      qualified INTEGER NOT NULL, payload_json TEXT NOT NULL,
      path TEXT NOT NULL, created_at TEXT NOT NULL
    );
    CREATE TABLE IF NOT EXISTS saa_improvement_opportunities (
      opportunity_ref TEXT PRIMARY KEY,
      opportunity_signature TEXT NOT NULL UNIQUE,
      opportunity_id TEXT NOT NULL, kind TEXT NOT NULL,
      priority_bp INTEGER NOT NULL, eligible INTEGER NOT NULL,
      payload_json TEXT NOT NULL, path TEXT NOT NULL, created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS saa_opportunity_kind_idx
      ON saa_improvement_opportunities(kind);
    CREATE TABLE IF NOT EXISTS saa_improvement_schedules (
      schedule_ref TEXT PRIMARY KEY, schedule_signature TEXT NOT NULL UNIQUE,
      status TEXT NOT NULL, payload_json TEXT NOT NULL,
      path TEXT NOT NULL, created_at TEXT NOT NULL
    );
    CREATE TABLE IF NOT EXISTS saa_knowledge_governance_metadata (
      key TEXT PRIMARY KEY, value TEXT NOT NULL
    );
  )SQL", store_label);
}

void insert_metadata(sqlite3 *database) {
  auto statement = prepare(
      database,
      "INSERT OR REPLACE INTO saa_knowledge_governance_metadata(key,value) "
      "VALUES(?,?)",
      store_label);
  for (const auto &[key, value] :
       std::vector<std::pair<std::string, std::string>>{
           {"schema_version",
            std::to_string(knowledge_governance_store_schema_version)},
           {"rebuilt_at", utc_now()}}) {
    reset_statement(statement.get());
    bind_text(statement.get(), 1, key, store_label);
    bind_text(statement.get(), 2, value, store_label);
    step_done(database, statement.get(), store_label);
  }
}

[[nodiscard]] saa::CanonicalFailurePattern
failure_pattern_from_json(const Json &payload) {
  return {.failure_class = payload.at("failure_class").get<std::string>(),
          .component = payload.at("component").get<std::string>(),
          .mechanism = payload.at("mechanism").get<std::string>(),
          .semantic_roles =
              payload.at("semantic_roles").get<std::vector<std::string>>(),
          .violated_invariants = payload.at("violated_invariants")
                                     .get<std::vector<std::string>>(),
          .boundary_signature =
              payload.at("boundary_signature").get<std::string>(),
          .context_signature =
              payload.at("context_signature").get<std::string>(),
          .pattern_signature =
              payload.at("pattern_signature").get<std::string>()};
}

} // namespace

KnowledgeGovernanceStore::KnowledgeGovernanceStore(EgcfStore &egcf_store)
    : egcf_store_(egcf_store), state_root_(egcf_store.state_root()),
      root_(state_root_ / "knowledge-governance"),
      pattern_root_(root_ / "failure-patterns" / "sha256"),
      occurrence_root_(root_ / "failure-occurrences" / "sha256"),
      benchmark_root_(root_ / "benchmark-gates" / "sha256"),
      snapshot_root_(root_ / "integrity-snapshots" / "sha256"),
      trajectory_root_(root_ / "integrity-trajectories" / "sha256"),
      opportunity_root_(root_ / "improvement-opportunities" / "sha256"),
      schedule_root_(root_ / "improvement-schedules" / "sha256"),
      projection_path_(egcf_store.projection_path()) {
  for (const auto &path : {pattern_root_, occurrence_root_, benchmark_root_,
                           snapshot_root_, trajectory_root_, opportunity_root_,
                           schedule_root_}) {
    std::filesystem::create_directories(path);
  }
  rebuild_projection();
}

const std::filesystem::path &KnowledgeGovernanceStore::root() const noexcept {
  return root_;
}

saa::ReasoningEvidenceResolver
KnowledgeGovernanceStore::evidence_resolver() const {
  return [this](std::string_view evidence_id)
             -> std::optional<saa::ReasoningGroundingEvidence> {
    try {
      const auto record = egcf_store_.get(evidence_id);
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

void KnowledgeGovernanceStore::ensure_evidence(
    const std::vector<std::string> &evidence_ids,
    std::string_view evidence_label) const {
  const auto resolver = evidence_resolver();
  for (const auto &evidence_id : evidence_ids) {
    const auto record = resolver(evidence_id);
    if (!record) {
      error(store_label, std::string(evidence_label) +
                             " evidence is not registered: " + evidence_id);
    }
    if (record->object_type != "egcf-evidence") {
      error(store_label, std::string(evidence_label) +
                             " evidence must reference EvidenceArtifact");
    }
    if (record->success != true || record->simulated) {
      error(store_label, std::string(evidence_label) +
                             " evidence must be successful and non-simulated");
    }
    if ((!record->producer.starts_with("deterministic-") &&
         !record->producer.starts_with("human-")) ||
        record->method == "reported") {
      error(store_label, std::string(evidence_label) +
                             " evidence must be deterministic/human grounded");
    }
  }
}

FailureRegistration KnowledgeGovernanceStore::register_failure_observation(
    const saa::FailureObservation &observation) {
  ensure_evidence(observation.evidence_ids, "SAA-12.1 failure");
  const auto pattern = saa::canonicalize_failure(observation);
  const Json pattern_payload = saa::to_json(pattern);
  Json occurrence_payload = saa::to_json(observation);
  occurrence_payload["pattern_signature"] = pattern.pattern_signature;
  const auto stored_pattern = write_immutable(
      pattern_root_, knowledge_governance_store_version, "failure-pattern",
      pattern.pattern_signature, pattern_payload, store_label);
  const auto stored_occurrence = write_immutable(
      occurrence_root_, knowledge_governance_store_version,
      "failure-occurrence", observation.observation_signature,
      occurrence_payload, store_label);
  auto database = open_database(projection_path_, store_label);
  create_tables(database.get());
  auto count_statement = prepare(
      database.get(),
      "SELECT COUNT(*) FROM saa_failure_occurrences WHERE "
      "pattern_signature=?",
      store_label);
  bind_text(count_statement.get(), 1, pattern.pattern_signature, store_label);
  if (sqlite3_step(count_statement.get()) != SQLITE_ROW) {
    error(store_label, "cannot count failure occurrences");
  }
  const bool repeated = sqlite3_column_int(count_statement.get(), 0) > 0;

  auto pattern_insert = prepare(
      database.get(),
      "INSERT OR IGNORE INTO saa_failure_patterns VALUES(?,?,?,?,?,?,?,?)",
      store_label);
  bind_text(pattern_insert.get(), 1, stored_pattern.object_ref, store_label);
  bind_text(pattern_insert.get(), 2, pattern.pattern_signature, store_label);
  bind_text(pattern_insert.get(), 3, pattern.failure_class, store_label);
  bind_text(pattern_insert.get(), 4, pattern.component, store_label);
  bind_text(pattern_insert.get(), 5, pattern.mechanism, store_label);
  bind_text(pattern_insert.get(), 6,
            contracts::canonical_json(pattern_payload), store_label);
  bind_text(pattern_insert.get(), 7,
            stored_pattern.path.lexically_relative(state_root_)
                .generic_string(),
            store_label);
  bind_text(pattern_insert.get(), 8, stored_pattern.created_at, store_label);
  step_done(database.get(), pattern_insert.get(), store_label);

  auto occurrence_insert = prepare(
      database.get(),
      "INSERT OR IGNORE INTO saa_failure_occurrences VALUES(?,?,?,?,?,?,?)",
      store_label);
  bind_text(occurrence_insert.get(), 1, stored_occurrence.object_ref,
            store_label);
  bind_text(occurrence_insert.get(), 2, observation.observation_signature,
            store_label);
  bind_text(occurrence_insert.get(), 3, pattern.pattern_signature,
            store_label);
  bind_text(occurrence_insert.get(), 4, observation.provenance_id,
            store_label);
  bind_text(occurrence_insert.get(), 5,
            contracts::canonical_json(occurrence_payload), store_label);
  bind_text(occurrence_insert.get(), 6,
            stored_occurrence.path.lexically_relative(state_root_)
                .generic_string(),
            store_label);
  bind_text(occurrence_insert.get(), 7, stored_occurrence.created_at,
            store_label);
  step_done(database.get(), occurrence_insert.get(), store_label);
  insert_metadata(database.get());
  return {.pattern_ref = stored_pattern.object_ref,
          .occurrence_ref = stored_occurrence.object_ref,
          .repeated = repeated};
}

std::vector<Json> KnowledgeGovernanceStore::failure_patterns() {
  return list_objects("saa_failure_patterns", "pattern_ref");
}

int KnowledgeGovernanceStore::failure_occurrence_count(
    std::string_view pattern_signature) {
  auto database = open_database(projection_path_, store_label);
  auto statement = prepare(
      database.get(),
      "SELECT COUNT(*) FROM saa_failure_occurrences WHERE "
      "pattern_signature=?",
      store_label);
  bind_text(statement.get(), 1, pattern_signature, store_label);
  if (sqlite3_step(statement.get()) != SQLITE_ROW) {
    error(store_label, "cannot count failure occurrences");
  }
  return sqlite3_column_int(statement.get(), 0);
}

std::optional<saa::FailureMatchAssessment>
KnowledgeGovernanceStore::assess_failure_retry(
    const saa::FailureObservation &observation) {
  const auto candidate = saa::canonicalize_failure(observation);
  auto database = open_database(projection_path_, store_label);
  auto statement = prepare(
      database.get(),
      "SELECT payload_json FROM saa_failure_patterns WHERE "
      "pattern_signature=?",
      store_label);
  bind_text(statement.get(), 1, candidate.pattern_signature, store_label);
  if (sqlite3_step(statement.get()) != SQLITE_ROW) {
    return std::nullopt;
  }
  const auto pattern =
      failure_pattern_from_json(Json::parse(column_text(statement.get(), 0)));
  return saa::compare_failure_to_pattern(
      observation, pattern,
      failure_occurrence_count(candidate.pattern_signature));
}

std::string KnowledgeGovernanceStore::register_benchmark_gate(
    const saa::OIECBenchGateAssessment &assessment) {
  const Json payload = saa::to_json(assessment);
  const auto stored = write_immutable(
      benchmark_root_, knowledge_governance_store_version, "oiec-bench-gate",
      assessment.assessment_signature, payload, store_label);
  auto database = open_database(projection_path_, store_label);
  create_tables(database.get());
  auto statement = prepare(
      database.get(),
      "INSERT OR IGNORE INTO saa_benchmark_gates VALUES(?,?,?,?,?,?,?,?)",
      store_label);
  bind_text(statement.get(), 1, stored.object_ref, store_label);
  bind_text(statement.get(), 2, assessment.assessment_signature,
            store_label);
  bind_text(statement.get(), 3, assessment.candidate_ref, store_label);
  bind_text(statement.get(), 4, assessment.status, store_label);
  bind_int(statement.get(), 5,
           assessment.canonical_promotion_eligible ? 1 : 0, store_label);
  bind_text(statement.get(), 6, contracts::canonical_json(payload),
            store_label);
  bind_text(statement.get(), 7,
            stored.path.lexically_relative(state_root_).generic_string(),
            store_label);
  bind_text(statement.get(), 8, stored.created_at, store_label);
  step_done(database.get(), statement.get(), store_label);
  return stored.object_ref;
}

std::string KnowledgeGovernanceStore::register_integrity_snapshot(
    const saa::KnowledgeIntegritySnapshot &snapshot) {
  const Json payload = saa::to_json(snapshot);
  const auto stored = write_immutable(
      snapshot_root_, knowledge_governance_store_version,
      "knowledge-integrity-snapshot", snapshot.snapshot_signature, payload,
      store_label);
  auto database = open_database(projection_path_, store_label);
  create_tables(database.get());
  auto existing = prepare(
      database.get(),
      "SELECT snapshot_signature FROM saa_integrity_snapshots WHERE "
      "generation=?",
      store_label);
  bind_int(existing.get(), 1, snapshot.generation, store_label);
  if (sqlite3_step(existing.get()) == SQLITE_ROW &&
      column_text(existing.get(), 0) != snapshot.snapshot_signature) {
    error(store_label,
          "SAA-12.3 one generation cannot have conflicting integrity "
          "snapshots");
  }
  auto statement = prepare(
      database.get(),
      "INSERT OR IGNORE INTO saa_integrity_snapshots VALUES(?,?,?,?,?,?)",
      store_label);
  bind_text(statement.get(), 1, stored.object_ref, store_label);
  bind_text(statement.get(), 2, snapshot.snapshot_signature, store_label);
  bind_int(statement.get(), 3, snapshot.generation, store_label);
  bind_text(statement.get(), 4, contracts::canonical_json(payload),
            store_label);
  bind_text(statement.get(), 5,
            stored.path.lexically_relative(state_root_).generic_string(),
            store_label);
  bind_text(statement.get(), 6, stored.created_at, store_label);
  step_done(database.get(), statement.get(), store_label);
  return stored.object_ref;
}

std::string KnowledgeGovernanceStore::register_integrity_trajectory(
    const saa::KnowledgeIntegrityTrajectory &trajectory) {
  auto database = open_database(projection_path_, store_label);
  create_tables(database.get());
  std::set<std::string> known;
  auto known_query = prepare(
      database.get(), "SELECT snapshot_signature FROM saa_integrity_snapshots",
      store_label);
  while (sqlite3_step(known_query.get()) == SQLITE_ROW) {
    known.insert(column_text(known_query.get(), 0));
  }
  if (!std::ranges::all_of(trajectory.snapshot_signatures,
                           [&](const auto &signature) {
                             return known.contains(signature);
                           })) {
    error(store_label,
          "SAA-12.3 trajectory references unregistered integrity snapshots");
  }
  const Json payload = saa::to_json(trajectory);
  const auto stored = write_immutable(
      trajectory_root_, knowledge_governance_store_version,
      "knowledge-integrity-trajectory", trajectory.trajectory_signature,
      payload, store_label);
  auto statement = prepare(
      database.get(),
      "INSERT OR IGNORE INTO saa_integrity_trajectories "
      "VALUES(?,?,?,?,?,?,?,?)",
      store_label);
  bind_text(statement.get(), 1, stored.object_ref, store_label);
  bind_text(statement.get(), 2, trajectory.trajectory_signature,
            store_label);
  bind_int(statement.get(), 3, trajectory.latest_generation, store_label);
  bind_text(statement.get(), 4, trajectory.status, store_label);
  bind_int(statement.get(), 5,
           trajectory.knowledge_integrity_qualified ? 1 : 0, store_label);
  bind_text(statement.get(), 6, contracts::canonical_json(payload),
            store_label);
  bind_text(statement.get(), 7,
            stored.path.lexically_relative(state_root_).generic_string(),
            store_label);
  bind_text(statement.get(), 8, stored.created_at, store_label);
  step_done(database.get(), statement.get(), store_label);
  return stored.object_ref;
}

std::string KnowledgeGovernanceStore::register_opportunity(
    const saa::ImprovementOpportunity &opportunity) {
  const Json payload = saa::to_json(opportunity);
  const auto stored = write_immutable(
      opportunity_root_, knowledge_governance_store_version,
      "improvement-opportunity", opportunity.opportunity_signature, payload,
      store_label);
  auto database = open_database(projection_path_, store_label);
  create_tables(database.get());
  auto statement = prepare(
      database.get(),
      "INSERT OR IGNORE INTO saa_improvement_opportunities "
      "VALUES(?,?,?,?,?,?,?,?,?)",
      store_label);
  bind_text(statement.get(), 1, stored.object_ref, store_label);
  bind_text(statement.get(), 2, opportunity.opportunity_signature,
            store_label);
  bind_text(statement.get(), 3, opportunity.opportunity_id, store_label);
  bind_text(statement.get(), 4, opportunity.kind, store_label);
  bind_int(statement.get(), 5, opportunity.priority_bp, store_label);
  bind_int(statement.get(), 6, opportunity.eligible() ? 1 : 0, store_label);
  bind_text(statement.get(), 7, contracts::canonical_json(payload),
            store_label);
  bind_text(statement.get(), 8,
            stored.path.lexically_relative(state_root_).generic_string(),
            store_label);
  bind_text(statement.get(), 9, stored.created_at, store_label);
  step_done(database.get(), statement.get(), store_label);
  return stored.object_ref;
}

std::string KnowledgeGovernanceStore::register_schedule(
    const saa::ImprovementSchedule &schedule) {
  auto database = open_database(projection_path_, store_label);
  create_tables(database.get());
  std::set<std::string> known;
  auto query = prepare(database.get(),
                       "SELECT opportunity_signature FROM "
                       "saa_improvement_opportunities",
                       store_label);
  while (sqlite3_step(query.get()) == SQLITE_ROW) {
    known.insert(column_text(query.get(), 0));
  }
  if (!std::ranges::all_of(schedule.selected, [&](const auto &entry) {
        return known.contains(entry.opportunity_signature);
      })) {
    error(store_label,
          "SAA-12.4 schedule selects an unregistered improvement opportunity");
  }
  const Json payload = saa::to_json(schedule);
  const auto stored = write_immutable(
      schedule_root_, knowledge_governance_store_version,
      "improvement-schedule", schedule.schedule_signature, payload,
      store_label);
  auto statement = prepare(
      database.get(),
      "INSERT OR IGNORE INTO saa_improvement_schedules VALUES(?,?,?,?,?,?)",
      store_label);
  bind_text(statement.get(), 1, stored.object_ref, store_label);
  bind_text(statement.get(), 2, schedule.schedule_signature, store_label);
  bind_text(statement.get(), 3, schedule.status, store_label);
  bind_text(statement.get(), 4, contracts::canonical_json(payload),
            store_label);
  bind_text(statement.get(), 5,
            stored.path.lexically_relative(state_root_).generic_string(),
            store_label);
  bind_text(statement.get(), 6, stored.created_at, store_label);
  step_done(database.get(), statement.get(), store_label);
  return stored.object_ref;
}

std::vector<Json> KnowledgeGovernanceStore::list_objects(
    std::string_view table, std::string_view ref_column) {
  static const std::set<std::string_view> allowed_tables = {
      "saa_failure_patterns",        "saa_failure_occurrences",
      "saa_benchmark_gates",        "saa_integrity_snapshots",
      "saa_integrity_trajectories", "saa_improvement_opportunities",
      "saa_improvement_schedules"};
  static const std::set<std::string_view> allowed_columns = {
      "pattern_ref",    "occurrence_ref", "gate_ref", "snapshot_ref",
      "trajectory_ref", "opportunity_ref", "schedule_ref"};
  if (!allowed_tables.contains(table) ||
      !allowed_columns.contains(ref_column)) {
    error(store_label, "unsupported projection listing");
  }
  auto database = open_database(projection_path_, store_label);
  const std::string sql = "SELECT " + std::string(ref_column) +
                          ",payload_json FROM " + std::string(table) +
                          " ORDER BY " + std::string(ref_column);
  auto statement = prepare(database.get(), sql, store_label);
  std::vector<Json> result;
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    result.push_back({{"object_ref", column_text(statement.get(), 0)},
                      {"payload", Json::parse(column_text(statement.get(), 1))}});
  }
  return result;
}

void KnowledgeGovernanceStore::rebuild_projection() {
  auto database = open_database(projection_path_, store_label);
  create_tables(database.get());
  execute(database.get(), "BEGIN IMMEDIATE", store_label);
  try {
    execute(database.get(), R"SQL(
      DELETE FROM saa_failure_patterns;
      DELETE FROM saa_failure_occurrences;
      DELETE FROM saa_benchmark_gates;
      DELETE FROM saa_integrity_snapshots;
      DELETE FROM saa_integrity_trajectories;
      DELETE FROM saa_improvement_opportunities;
      DELETE FROM saa_improvement_schedules;
      DELETE FROM saa_knowledge_governance_metadata;
    )SQL", store_label);

    const auto visit = [&](const std::filesystem::path &root,
                           std::string_view kind, std::string_view signature_key,
                           std::string_view sql, const auto &bind_payload) {
      auto statement = prepare(database.get(), sql, store_label);
      for (const auto &path : json_files(root)) {
        const Json envelope = read_envelope(path, store_label);
        const Json &payload = envelope.at("payload");
        const std::string object_ref =
            envelope.at("object_id").get<std::string>();
        if (contracts::parse_typed_id(object_ref).object_type != kind ||
            typed_ref(kind,
                      payload.at(signature_key).get_ref<const std::string &>()) !=
                object_ref) {
          error(store_label, "immutable identity mismatch at " + path.string());
        }
        reset_statement(statement.get());
        bind_payload(statement.get(), payload, object_ref,
                     path.lexically_relative(state_root_).generic_string(),
                     envelope.at("created_at").get_ref<const std::string &>());
        step_done(database.get(), statement.get(), store_label);
      }
    };

    visit(pattern_root_, "failure-pattern", "pattern_signature",
          "INSERT INTO saa_failure_patterns VALUES(?,?,?,?,?,?,?,?)",
          [&](sqlite3_stmt *statement, const Json &payload,
              const std::string &object_ref, const std::string &path,
              const std::string &created_at) {
            bind_text(statement, 1, object_ref, store_label);
            bind_text(statement, 2,
                      payload.at("pattern_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 3,
                      payload.at("failure_class")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 4,
                      payload.at("component").get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 5,
                      payload.at("mechanism").get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 6, contracts::canonical_json(payload),
                      store_label);
            bind_text(statement, 7, path, store_label);
            bind_text(statement, 8, created_at, store_label);
          });
    visit(occurrence_root_, "failure-occurrence", "observation_signature",
          "INSERT INTO saa_failure_occurrences VALUES(?,?,?,?,?,?,?)",
          [&](sqlite3_stmt *statement, const Json &payload,
              const std::string &object_ref, const std::string &path,
              const std::string &created_at) {
            bind_text(statement, 1, object_ref, store_label);
            bind_text(statement, 2,
                      payload.at("observation_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 3,
                      payload.at("pattern_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 4,
                      payload.at("provenance_id")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 5, contracts::canonical_json(payload),
                      store_label);
            bind_text(statement, 6, path, store_label);
            bind_text(statement, 7, created_at, store_label);
          });
    visit(benchmark_root_, "oiec-bench-gate", "assessment_signature",
          "INSERT INTO saa_benchmark_gates VALUES(?,?,?,?,?,?,?,?)",
          [&](sqlite3_stmt *statement, const Json &payload,
              const std::string &object_ref, const std::string &path,
              const std::string &created_at) {
            bind_text(statement, 1, object_ref, store_label);
            bind_text(statement, 2,
                      payload.at("assessment_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 3,
                      payload.at("candidate_ref")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 4,
                      payload.at("status").get_ref<const std::string &>(),
                      store_label);
            bind_int(statement, 5,
                     payload.at("canonical_promotion_eligible").get<bool>() ? 1
                                                                            : 0,
                     store_label);
            bind_text(statement, 6, contracts::canonical_json(payload),
                      store_label);
            bind_text(statement, 7, path, store_label);
            bind_text(statement, 8, created_at, store_label);
          });
    visit(snapshot_root_, "knowledge-integrity-snapshot", "snapshot_signature",
          "INSERT INTO saa_integrity_snapshots VALUES(?,?,?,?,?,?)",
          [&](sqlite3_stmt *statement, const Json &payload,
              const std::string &object_ref, const std::string &path,
              const std::string &created_at) {
            bind_text(statement, 1, object_ref, store_label);
            bind_text(statement, 2,
                      payload.at("snapshot_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_int(statement, 3, payload.at("generation").get<int>(),
                     store_label);
            bind_text(statement, 4, contracts::canonical_json(payload),
                      store_label);
            bind_text(statement, 5, path, store_label);
            bind_text(statement, 6, created_at, store_label);
          });
    visit(trajectory_root_, "knowledge-integrity-trajectory",
          "trajectory_signature",
          "INSERT INTO saa_integrity_trajectories VALUES(?,?,?,?,?,?,?,?)",
          [&](sqlite3_stmt *statement, const Json &payload,
              const std::string &object_ref, const std::string &path,
              const std::string &created_at) {
            bind_text(statement, 1, object_ref, store_label);
            bind_text(statement, 2,
                      payload.at("trajectory_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_int(statement, 3, payload.at("latest_generation").get<int>(),
                     store_label);
            bind_text(statement, 4,
                      payload.at("status").get_ref<const std::string &>(),
                      store_label);
            bind_int(statement, 5,
                     payload.at("knowledge_integrity_qualified").get<bool>()
                         ? 1
                         : 0,
                     store_label);
            bind_text(statement, 6, contracts::canonical_json(payload),
                      store_label);
            bind_text(statement, 7, path, store_label);
            bind_text(statement, 8, created_at, store_label);
          });
    visit(opportunity_root_, "improvement-opportunity",
          "opportunity_signature",
          "INSERT INTO saa_improvement_opportunities "
          "VALUES(?,?,?,?,?,?,?,?,?)",
          [&](sqlite3_stmt *statement, const Json &payload,
              const std::string &object_ref, const std::string &path,
              const std::string &created_at) {
            bind_text(statement, 1, object_ref, store_label);
            bind_text(statement, 2,
                      payload.at("opportunity_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 3,
                      payload.at("opportunity_id")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 4,
                      payload.at("kind").get_ref<const std::string &>(),
                      store_label);
            bind_int(statement, 5, payload.at("priority_bp").get<int>(),
                     store_label);
            bind_int(statement, 6, payload.at("eligible").get<bool>() ? 1 : 0,
                     store_label);
            bind_text(statement, 7, contracts::canonical_json(payload),
                      store_label);
            bind_text(statement, 8, path, store_label);
            bind_text(statement, 9, created_at, store_label);
          });
    visit(schedule_root_, "improvement-schedule", "schedule_signature",
          "INSERT INTO saa_improvement_schedules VALUES(?,?,?,?,?,?)",
          [&](sqlite3_stmt *statement, const Json &payload,
              const std::string &object_ref, const std::string &path,
              const std::string &created_at) {
            bind_text(statement, 1, object_ref, store_label);
            bind_text(statement, 2,
                      payload.at("schedule_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 3,
                      payload.at("status").get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 4, contracts::canonical_json(payload),
                      store_label);
            bind_text(statement, 5, path, store_label);
            bind_text(statement, 6, created_at, store_label);
          });
    insert_metadata(database.get());
    execute(database.get(), "COMMIT", store_label);
  } catch (...) {
    sqlite3_exec(database.get(), "ROLLBACK", nullptr, nullptr, nullptr);
    throw;
  }
}

} // namespace statewright::egcf
