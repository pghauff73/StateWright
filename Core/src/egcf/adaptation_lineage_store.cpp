#include "statewright/egcf/adaptation_lineage_store.hpp"

#include "ledger_support.hpp"

#include <sqlite3.h>

#include <algorithm>
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
inline constexpr std::string_view store_label = "adaptation lineage store";

void create_tables(sqlite3 *database) {
  execute(database, R"SQL(
    CREATE TABLE IF NOT EXISTS adaptation_candidates (
      candidate_ref TEXT PRIMARY KEY, candidate_signature TEXT NOT NULL UNIQUE,
      base_algorithm_id TEXT NOT NULL, component TEXT NOT NULL,
      changed_dimension TEXT NOT NULL,
      parent_candidate_signature TEXT NOT NULL, payload_json TEXT NOT NULL,
      path TEXT NOT NULL, created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS adaptation_candidates_base_idx
      ON adaptation_candidates(base_algorithm_id);
    CREATE INDEX IF NOT EXISTS adaptation_candidates_dimension_idx
      ON adaptation_candidates(changed_dimension);
    CREATE TABLE IF NOT EXISTS adaptation_lineage_edges (
      edge_ref TEXT PRIMARY KEY, edge_signature TEXT NOT NULL UNIQUE,
      parent_ref TEXT NOT NULL, child_ref TEXT NOT NULL UNIQUE,
      relation TEXT NOT NULL, changed_dimension TEXT NOT NULL,
      payload_json TEXT NOT NULL, path TEXT NOT NULL, created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS adaptation_edges_parent_idx
      ON adaptation_lineage_edges(parent_ref);
    CREATE INDEX IF NOT EXISTS adaptation_edges_child_idx
      ON adaptation_lineage_edges(child_ref);
    CREATE TABLE IF NOT EXISTS adaptation_promotions (
      promotion_ref TEXT PRIMARY KEY, promotion_signature TEXT NOT NULL UNIQUE,
      candidate_ref TEXT NOT NULL, canonical_algorithm_ref TEXT NOT NULL,
      qualification_signature TEXT NOT NULL, evidence_json TEXT NOT NULL,
      payload_json TEXT NOT NULL, path TEXT NOT NULL, created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS adaptation_promotions_candidate_idx
      ON adaptation_promotions(candidate_ref);
    CREATE TABLE IF NOT EXISTS adaptation_experiments (
      experiment_ref TEXT PRIMARY KEY, design_signature TEXT NOT NULL UNIQUE,
      baseline_ref TEXT NOT NULL, candidate_ref TEXT NOT NULL,
      context_signature TEXT NOT NULL, payload_json TEXT NOT NULL,
      path TEXT NOT NULL, created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS adaptation_experiments_candidate_idx
      ON adaptation_experiments(candidate_ref);
    CREATE TABLE IF NOT EXISTS adaptation_experiment_results (
      result_ref TEXT PRIMARY KEY, result_signature TEXT NOT NULL UNIQUE,
      design_signature TEXT NOT NULL, status TEXT NOT NULL,
      candidate_improvement_qualified INTEGER NOT NULL,
      payload_json TEXT NOT NULL, path TEXT NOT NULL, created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS adaptation_results_design_idx
      ON adaptation_experiment_results(design_signature);
    CREATE TABLE IF NOT EXISTS adaptation_store_metadata (
      key TEXT PRIMARY KEY, value TEXT NOT NULL
    );
  )SQL", store_label);
}

void metadata(sqlite3 *database) {
  auto statement = prepare(
      database,
      "INSERT OR REPLACE INTO adaptation_store_metadata(key,value) "
      "VALUES(?,?)",
      store_label);
  for (const auto &[key, value] :
       std::vector<std::pair<std::string, std::string>>{
           {"schema_version",
            std::to_string(adaptation_lineage_store_schema_version)},
           {"rebuilt_at", utc_now()}}) {
    reset_statement(statement.get());
    bind_text(statement.get(), 1, key, store_label);
    bind_text(statement.get(), 2, value, store_label);
    step_done(database, statement.get(), store_label);
  }
}

[[nodiscard]] std::vector<Json> query_payloads(
    const std::filesystem::path &projection_path, std::string_view sql,
    std::string_view reference_key,
    std::optional<std::string_view> parameter = std::nullopt) {
  auto database = open_database(projection_path, store_label);
  auto statement = prepare(database.get(), sql, store_label);
  if (parameter) {
    bind_text(statement.get(), 1, *parameter, store_label);
  }
  std::vector<Json> result;
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    result.push_back({{std::string(reference_key),
                       column_text(statement.get(), 0)},
                      {"payload", Json::parse(column_text(statement.get(), 1))}});
  }
  return result;
}

} // namespace

AdaptationLineageStore::AdaptationLineageStore(EgcfStore &egcf_store)
    : egcf_store_(egcf_store), state_root_(egcf_store.state_root()),
      root_(state_root_ / "adaptation-lineage"),
      candidate_root_(root_ / "candidates" / "sha256"),
      edge_root_(root_ / "edges" / "sha256"),
      promotion_root_(root_ / "promotions" / "sha256"),
      experiment_root_(root_ / "experiments" / "sha256"),
      result_root_(root_ / "experiment-results" / "sha256"),
      projection_path_(egcf_store.projection_path()) {
  for (const auto &path : {candidate_root_, edge_root_, promotion_root_,
                           experiment_root_, result_root_}) {
    std::filesystem::create_directories(path);
  }
  rebuild_projection();
}

const std::filesystem::path &AdaptationLineageStore::root() const noexcept {
  return root_;
}

saa::ReasoningEvidenceResolver AdaptationLineageStore::evidence_resolver() const {
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

void AdaptationLineageStore::verify_evidence(
    const std::vector<std::string> &evidence_ids,
    std::string_view purpose) const {
  const auto resolver = evidence_resolver();
  for (const auto &evidence_id : evidence_ids) {
    const auto record = resolver(evidence_id);
    if (!record) {
      error(store_label, std::string(purpose) +
                             " evidence is not registered: " + evidence_id);
    }
    if (record->object_type != "egcf-evidence" || record->success != true ||
        record->simulated ||
        ((!record->producer.starts_with("deterministic-") &&
          !record->producer.starts_with("human-")) ||
         record->method == "reported")) {
      error(store_label,
            std::string(purpose) +
                " evidence must be successful deterministic/human "
                "EvidenceArtifact");
    }
  }
}

bool AdaptationLineageStore::candidate_exists(
    std::string_view candidate_ref) const {
  auto database = open_database(projection_path_, store_label);
  auto statement = prepare(
      database.get(),
      "SELECT 1 FROM adaptation_candidates WHERE candidate_ref=?",
      store_label);
  bind_text(statement.get(), 1, candidate_ref, store_label);
  return sqlite3_step(statement.get()) == SQLITE_ROW;
}

std::pair<std::string, std::string> AdaptationLineageStore::register_candidate(
    const saa::AdaptedAlgorithmCandidate &candidate,
    const saa::AdaptationStep &step,
    std::string source_explanation_signature) {
  const auto edge = saa::make_adaptation_lineage_edge(
      candidate, step, std::move(source_explanation_signature));
  if (!edge.parent_candidate_signature.empty() &&
      !candidate_exists(edge.parent_ref)) {
    error(store_label,
          "SAA-11.1 parent adapted candidate is not registered");
  }
  const auto parent_ancestors = edge.parent_candidate_signature.empty()
                                    ? std::vector<std::string>{}
                                    : ancestors(edge.parent_ref);
  if (edge.parent_ref == edge.child_ref ||
      std::ranges::find(parent_ancestors, edge.child_ref) !=
          parent_ancestors.end()) {
    error(store_label, "SAA-11.1 lineage cycle detected");
  }
  const Json candidate_payload = saa::to_json(candidate);
  const Json edge_payload = saa::to_json(edge);
  const auto stored_candidate = write_immutable(
      candidate_root_, adaptation_lineage_store_version, "adapted-candidate",
      candidate.candidate_signature, candidate_payload, store_label);
  const auto stored_edge = write_immutable(
      edge_root_, adaptation_lineage_store_version, "adaptation-edge",
      edge.edge_signature, edge_payload, store_label);
  auto database = open_database(projection_path_, store_label);
  create_tables(database.get());
  auto candidate_insert = prepare(
      database.get(),
      "INSERT OR IGNORE INTO adaptation_candidates "
      "VALUES(?,?,?,?,?,?,?,?,?)",
      store_label);
  bind_text(candidate_insert.get(), 1, stored_candidate.object_ref,
            store_label);
  bind_text(candidate_insert.get(), 2, candidate.candidate_signature,
            store_label);
  bind_text(candidate_insert.get(), 3, candidate.base_algorithm_id,
            store_label);
  bind_text(candidate_insert.get(), 4, candidate.component, store_label);
  bind_text(candidate_insert.get(), 5, candidate.changed_dimension,
            store_label);
  bind_text(candidate_insert.get(), 6, candidate.parent_candidate_signature,
            store_label);
  bind_text(candidate_insert.get(), 7,
            contracts::canonical_json(candidate_payload), store_label);
  bind_text(candidate_insert.get(), 8,
            stored_candidate.path.lexically_relative(state_root_)
                .generic_string(),
            store_label);
  bind_text(candidate_insert.get(), 9, stored_candidate.created_at,
            store_label);
  step_done(database.get(), candidate_insert.get(), store_label);
  auto edge_insert = prepare(
      database.get(),
      "INSERT OR IGNORE INTO adaptation_lineage_edges "
      "VALUES(?,?,?,?,?,?,?,?,?)",
      store_label);
  bind_text(edge_insert.get(), 1, stored_edge.object_ref, store_label);
  bind_text(edge_insert.get(), 2, edge.edge_signature, store_label);
  bind_text(edge_insert.get(), 3, edge.parent_ref, store_label);
  bind_text(edge_insert.get(), 4, edge.child_ref, store_label);
  bind_text(edge_insert.get(), 5, edge.relation, store_label);
  bind_text(edge_insert.get(), 6, edge.changed_dimension, store_label);
  bind_text(edge_insert.get(), 7, contracts::canonical_json(edge_payload),
            store_label);
  bind_text(edge_insert.get(), 8,
            stored_edge.path.lexically_relative(state_root_).generic_string(),
            store_label);
  bind_text(edge_insert.get(), 9, stored_edge.created_at, store_label);
  step_done(database.get(), edge_insert.get(), store_label);
  metadata(database.get());
  return {stored_candidate.object_ref, stored_edge.object_ref};
}

Json AdaptationLineageStore::get_candidate(
    std::string_view candidate_ref) const {
  const auto path = path_for(candidate_root_, candidate_ref,
                             "adapted-candidate", store_label);
  const auto envelope = read_envelope(path, store_label);
  if (envelope.value("object_id", "") != candidate_ref) {
    error(store_label, "adaptation candidate identity mismatch");
  }
  return envelope;
}

std::vector<Json> AdaptationLineageStore::candidates() {
  return query_payloads(
      projection_path_,
      "SELECT candidate_ref,payload_json FROM adaptation_candidates ORDER BY "
      "candidate_ref",
      "candidate_ref");
}

std::vector<Json> AdaptationLineageStore::lineage_edges() const {
  auto database = open_database(projection_path_, store_label);
  auto statement = prepare(
      database.get(),
      "SELECT edge_ref,parent_ref,child_ref,changed_dimension,payload_json "
      "FROM adaptation_lineage_edges ORDER BY edge_ref",
      store_label);
  std::vector<Json> result;
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    result.push_back({{"changed_dimension", column_text(statement.get(), 3)},
                      {"child_ref", column_text(statement.get(), 2)},
                      {"edge_ref", column_text(statement.get(), 0)},
                      {"parent_ref", column_text(statement.get(), 1)},
                      {"payload", Json::parse(column_text(statement.get(), 4))}});
  }
  return result;
}

std::optional<std::string> AdaptationLineageStore::parent(
    std::string_view candidate_ref) const {
  auto database = open_database(projection_path_, store_label);
  auto statement = prepare(
      database.get(),
      "SELECT parent_ref FROM adaptation_lineage_edges WHERE child_ref=?",
      store_label);
  bind_text(statement.get(), 1, candidate_ref, store_label);
  if (sqlite3_step(statement.get()) != SQLITE_ROW) {
    return std::nullopt;
  }
  return column_text(statement.get(), 0);
}

std::vector<std::string> AdaptationLineageStore::children(
    std::string_view reference) const {
  auto database = open_database(projection_path_, store_label);
  auto statement = prepare(
      database.get(),
      "SELECT child_ref FROM adaptation_lineage_edges WHERE parent_ref=? "
      "ORDER BY child_ref",
      store_label);
  bind_text(statement.get(), 1, reference, store_label);
  std::vector<std::string> result;
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    result.push_back(column_text(statement.get(), 0));
  }
  return result;
}

std::vector<std::string> AdaptationLineageStore::ancestors(
    std::string_view candidate_ref) const {
  std::string current(candidate_ref);
  std::vector<std::string> result;
  std::set<std::string> seen{current};
  for (std::size_t depth = 0; depth < saa::max_lineage_depth; ++depth) {
    const auto parent_ref = parent(current);
    if (!parent_ref) {
      return result;
    }
    if (!seen.insert(*parent_ref).second) {
      error(store_label,
            "SAA-11.1 stored adaptation lineage contains a cycle");
    }
    result.push_back(*parent_ref);
    if (!parent_ref->starts_with("adapted-candidate:sha256:")) {
      return result;
    }
    current = *parent_ref;
  }
  error(store_label, "SAA-11.1 lineage exceeds bounded depth");
}

bool AdaptationLineageStore::descends_from(
    std::string_view candidate_ref, std::string_view ancestor_ref) const {
  const auto chain = ancestors(candidate_ref);
  return std::ranges::find(chain, ancestor_ref) != chain.end();
}

std::string AdaptationLineageStore::register_promotion(
    const saa::AdaptationPromotionRecord &promotion) {
  static_cast<void>(get_candidate(promotion.candidate_ref));
  verify_evidence(promotion.evidence_ids, "promotion");
  const Json payload = saa::to_json(promotion);
  const auto stored = write_immutable(
      promotion_root_, adaptation_lineage_store_version,
      "adaptation-promotion", promotion.promotion_signature, payload,
      store_label);
  auto database = open_database(projection_path_, store_label);
  create_tables(database.get());
  auto statement = prepare(
      database.get(),
      "INSERT OR IGNORE INTO adaptation_promotions "
      "VALUES(?,?,?,?,?,?,?,?,?)",
      store_label);
  bind_text(statement.get(), 1, stored.object_ref, store_label);
  bind_text(statement.get(), 2, promotion.promotion_signature, store_label);
  bind_text(statement.get(), 3, promotion.candidate_ref, store_label);
  bind_text(statement.get(), 4, promotion.canonical_algorithm_ref,
            store_label);
  bind_text(statement.get(), 5, promotion.qualification_signature,
            store_label);
  bind_text(statement.get(), 6,
            contracts::canonical_json(promotion.evidence_ids), store_label);
  bind_text(statement.get(), 7, contracts::canonical_json(payload),
            store_label);
  bind_text(statement.get(), 8,
            stored.path.lexically_relative(state_root_).generic_string(),
            store_label);
  bind_text(statement.get(), 9, stored.created_at, store_label);
  step_done(database.get(), statement.get(), store_label);
  return stored.object_ref;
}

std::vector<Json> AdaptationLineageStore::promotions(
    std::optional<std::string> candidate_ref) const {
  if (candidate_ref) {
    return query_payloads(
        projection_path_,
        "SELECT promotion_ref,payload_json FROM adaptation_promotions WHERE "
        "candidate_ref=? ORDER BY promotion_ref",
        "promotion_ref",
        *candidate_ref);
  }
  return query_payloads(
      projection_path_,
      "SELECT promotion_ref,payload_json FROM adaptation_promotions ORDER BY "
      "promotion_ref",
      "promotion_ref");
}

std::string AdaptationLineageStore::register_experiment_design(
    const saa::AlgorithmABExperimentDesign &design) {
  static_cast<void>(get_candidate(design.candidate_ref));
  if (!descends_from(design.candidate_ref, design.baseline_ref)) {
    error(store_label,
          "SAA-11.2 candidate must descend from the experiment baseline");
  }
  const Json payload = saa::to_json(design);
  const auto stored = write_immutable(
      experiment_root_, adaptation_lineage_store_version,
      "adaptation-experiment", design.design_signature, payload, store_label);
  auto database = open_database(projection_path_, store_label);
  create_tables(database.get());
  auto statement = prepare(
      database.get(),
      "INSERT OR IGNORE INTO adaptation_experiments "
      "VALUES(?,?,?,?,?,?,?,?)",
      store_label);
  bind_text(statement.get(), 1, stored.object_ref, store_label);
  bind_text(statement.get(), 2, design.design_signature, store_label);
  bind_text(statement.get(), 3, design.baseline_ref, store_label);
  bind_text(statement.get(), 4, design.candidate_ref, store_label);
  bind_text(statement.get(), 5, design.context_signature, store_label);
  bind_text(statement.get(), 6, contracts::canonical_json(payload),
            store_label);
  bind_text(statement.get(), 7,
            stored.path.lexically_relative(state_root_).generic_string(),
            store_label);
  bind_text(statement.get(), 8, stored.created_at, store_label);
  step_done(database.get(), statement.get(), store_label);
  return stored.object_ref;
}

std::string AdaptationLineageStore::register_experiment_result(
    const saa::AlgorithmABExperimentResult &result) {
  auto database = open_database(projection_path_, store_label);
  create_tables(database.get());
  auto design_query = prepare(
      database.get(),
      "SELECT 1 FROM adaptation_experiments WHERE design_signature=?",
      store_label);
  bind_text(design_query.get(), 1, result.design_signature, store_label);
  if (sqlite3_step(design_query.get()) != SQLITE_ROW) {
    error(store_label,
          "SAA-11.2 experiment result references an unregistered design");
  }
  const Json payload = saa::to_json(result);
  const auto stored = write_immutable(
      result_root_, adaptation_lineage_store_version,
      "adaptation-experiment-result", result.result_signature, payload,
      store_label);
  auto statement = prepare(
      database.get(),
      "INSERT OR IGNORE INTO adaptation_experiment_results "
      "VALUES(?,?,?,?,?,?,?,?)",
      store_label);
  bind_text(statement.get(), 1, stored.object_ref, store_label);
  bind_text(statement.get(), 2, result.result_signature, store_label);
  bind_text(statement.get(), 3, result.design_signature, store_label);
  bind_text(statement.get(), 4, result.status, store_label);
  bind_int(statement.get(), 5,
           result.candidate_improvement_qualified ? 1 : 0, store_label);
  bind_text(statement.get(), 6, contracts::canonical_json(payload),
            store_label);
  bind_text(statement.get(), 7,
            stored.path.lexically_relative(state_root_).generic_string(),
            store_label);
  bind_text(statement.get(), 8, stored.created_at, store_label);
  step_done(database.get(), statement.get(), store_label);
  return stored.object_ref;
}

std::vector<Json> AdaptationLineageStore::experiments() const {
  return query_payloads(
      projection_path_,
      "SELECT experiment_ref,payload_json FROM adaptation_experiments ORDER "
      "BY experiment_ref",
      "experiment_ref");
}

std::vector<Json> AdaptationLineageStore::experiment_results(
    std::optional<std::string> design_signature) const {
  if (design_signature) {
    return query_payloads(
        projection_path_,
        "SELECT result_ref,payload_json FROM adaptation_experiment_results "
        "WHERE design_signature=? ORDER BY result_ref",
        "result_ref",
        *design_signature);
  }
  return query_payloads(
      projection_path_,
      "SELECT result_ref,payload_json FROM adaptation_experiment_results "
      "ORDER BY result_ref",
      "result_ref");
}

void AdaptationLineageStore::rebuild_projection() {
  auto database = open_database(projection_path_, store_label);
  create_tables(database.get());
  execute(database.get(), "BEGIN IMMEDIATE", store_label);
  try {
    execute(database.get(), R"SQL(
      DELETE FROM adaptation_candidates;
      DELETE FROM adaptation_lineage_edges;
      DELETE FROM adaptation_promotions;
      DELETE FROM adaptation_experiments;
      DELETE FROM adaptation_experiment_results;
      DELETE FROM adaptation_store_metadata;
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
    visit(candidate_root_, "adapted-candidate", "candidate_signature",
          "INSERT INTO adaptation_candidates VALUES(?,?,?,?,?,?,?,?,?)",
          [&](sqlite3_stmt *statement, const Json &payload,
              const std::string &object_ref, const std::string &path,
              const std::string &created_at) {
            bind_text(statement, 1, object_ref, store_label);
            bind_text(statement, 2,
                      payload.at("candidate_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 3,
                      payload.at("base_algorithm_id")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 4,
                      payload.at("component").get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 5,
                      payload.at("changed_dimension")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 6,
                      payload.at("parent_candidate_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 7, contracts::canonical_json(payload),
                      store_label);
            bind_text(statement, 8, path, store_label);
            bind_text(statement, 9, created_at, store_label);
          });
    visit(edge_root_, "adaptation-edge", "edge_signature",
          "INSERT INTO adaptation_lineage_edges VALUES(?,?,?,?,?,?,?,?,?)",
          [&](sqlite3_stmt *statement, const Json &payload,
              const std::string &object_ref, const std::string &path,
              const std::string &created_at) {
            bind_text(statement, 1, object_ref, store_label);
            bind_text(statement, 2,
                      payload.at("edge_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 3,
                      payload.at("parent_ref").get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 4,
                      payload.at("child_ref").get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 5,
                      payload.at("relation").get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 6,
                      payload.at("changed_dimension")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 7, contracts::canonical_json(payload),
                      store_label);
            bind_text(statement, 8, path, store_label);
            bind_text(statement, 9, created_at, store_label);
          });
    visit(promotion_root_, "adaptation-promotion", "promotion_signature",
          "INSERT INTO adaptation_promotions VALUES(?,?,?,?,?,?,?,?,?)",
          [&](sqlite3_stmt *statement, const Json &payload,
              const std::string &object_ref, const std::string &path,
              const std::string &created_at) {
            bind_text(statement, 1, object_ref, store_label);
            bind_text(statement, 2,
                      payload.at("promotion_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 3,
                      payload.at("candidate_ref").get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 4,
                      payload.at("canonical_algorithm_ref")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 5,
                      payload.at("qualification_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 6,
                      contracts::canonical_json(payload.at("evidence_ids")),
                      store_label);
            bind_text(statement, 7, contracts::canonical_json(payload),
                      store_label);
            bind_text(statement, 8, path, store_label);
            bind_text(statement, 9, created_at, store_label);
          });
    visit(experiment_root_, "adaptation-experiment", "design_signature",
          "INSERT INTO adaptation_experiments VALUES(?,?,?,?,?,?,?,?)",
          [&](sqlite3_stmt *statement, const Json &payload,
              const std::string &object_ref, const std::string &path,
              const std::string &created_at) {
            bind_text(statement, 1, object_ref, store_label);
            bind_text(statement, 2,
                      payload.at("design_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 3,
                      payload.at("baseline_ref").get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 4,
                      payload.at("candidate_ref").get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 5,
                      payload.at("context_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 6, contracts::canonical_json(payload),
                      store_label);
            bind_text(statement, 7, path, store_label);
            bind_text(statement, 8, created_at, store_label);
          });
    visit(result_root_, "adaptation-experiment-result", "result_signature",
          "INSERT INTO adaptation_experiment_results VALUES(?,?,?,?,?,?,?,?)",
          [&](sqlite3_stmt *statement, const Json &payload,
              const std::string &object_ref, const std::string &path,
              const std::string &created_at) {
            bind_text(statement, 1, object_ref, store_label);
            bind_text(statement, 2,
                      payload.at("result_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 3,
                      payload.at("design_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 4,
                      payload.at("status").get_ref<const std::string &>(),
                      store_label);
            bind_int(statement, 5,
                     payload.at("candidate_improvement_qualified").get<bool>()
                         ? 1
                         : 0,
                     store_label);
            bind_text(statement, 6, contracts::canonical_json(payload),
                      store_label);
            bind_text(statement, 7, path, store_label);
            bind_text(statement, 8, created_at, store_label);
          });
    metadata(database.get());
    execute(database.get(), "COMMIT", store_label);
  } catch (...) {
    sqlite3_exec(database.get(), "ROLLBACK", nullptr, nullptr, nullptr);
    throw;
  }
}

} // namespace statewright::egcf
