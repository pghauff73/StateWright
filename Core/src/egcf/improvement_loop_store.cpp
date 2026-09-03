#include "statewright/egcf/improvement_loop_store.hpp"

#include "ledger_support.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <string_view>
#include <utility>

namespace statewright::egcf {
namespace {

using Json = contracts::Json;
using namespace ledger_support;
inline constexpr std::string_view store_label = "improvement loop store";

void create_tables(sqlite3 *database) {
  execute(database, R"SQL(
    CREATE TABLE IF NOT EXISTS improvement_evolution_plans (
      plan_ref TEXT PRIMARY KEY, plan_signature TEXT NOT NULL UNIQUE,
      root_algorithm_ref TEXT NOT NULL, final_candidate_ref TEXT NOT NULL,
      step_count INTEGER NOT NULL, payload_json TEXT NOT NULL,
      path TEXT NOT NULL, created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS improvement_plan_candidate_idx
      ON improvement_evolution_plans(final_candidate_ref);
    CREATE TABLE IF NOT EXISTS improvement_step_qualifications (
      qualification_ref TEXT PRIMARY KEY,
      qualification_signature TEXT NOT NULL UNIQUE,
      plan_signature TEXT NOT NULL, candidate_ref TEXT NOT NULL,
      status TEXT NOT NULL, step_qualified INTEGER NOT NULL,
      payload_json TEXT NOT NULL, path TEXT NOT NULL, created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS improvement_qualification_plan_idx
      ON improvement_step_qualifications(plan_signature);
    CREATE TABLE IF NOT EXISTS improvement_evolution_assessments (
      assessment_ref TEXT PRIMARY KEY,
      assessment_signature TEXT NOT NULL UNIQUE,
      plan_signature TEXT NOT NULL, final_candidate_ref TEXT NOT NULL,
      status TEXT NOT NULL, evolution_qualified INTEGER NOT NULL,
      payload_json TEXT NOT NULL, path TEXT NOT NULL, created_at TEXT NOT NULL
    );
    CREATE TABLE IF NOT EXISTS improvement_experiment_aggregates (
      aggregate_ref TEXT PRIMARY KEY, aggregate_signature TEXT NOT NULL UNIQUE,
      design_signature TEXT NOT NULL, experiment_count INTEGER NOT NULL,
      status TEXT NOT NULL, sustained_improvement_qualified INTEGER NOT NULL,
      payload_json TEXT NOT NULL, path TEXT NOT NULL, created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS improvement_aggregate_design_idx
      ON improvement_experiment_aggregates(design_signature);
    CREATE TABLE IF NOT EXISTS improvement_loop_decisions (
      decision_ref TEXT PRIMARY KEY, decision_signature TEXT NOT NULL UNIQUE,
      phase TEXT NOT NULL, status TEXT NOT NULL, terminal INTEGER NOT NULL,
      candidate_ref TEXT NOT NULL, payload_json TEXT NOT NULL,
      path TEXT NOT NULL, created_at TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS improvement_decision_status_idx
      ON improvement_loop_decisions(status);
    CREATE TABLE IF NOT EXISTS improvement_store_metadata (
      key TEXT PRIMARY KEY, value TEXT NOT NULL
    );
  )SQL", store_label);
}

void insert_metadata(sqlite3 *database) {
  auto statement = prepare(
      database,
      "INSERT OR REPLACE INTO improvement_store_metadata(key,value) "
      "VALUES(?,?)",
      store_label);
  for (const auto &[key, value] :
       std::vector<std::pair<std::string, std::string>>{
           {"schema_version",
            std::to_string(improvement_loop_store_schema_version)},
           {"rebuilt_at", utc_now()}}) {
    reset_statement(statement.get());
    bind_text(statement.get(), 1, key, store_label);
    bind_text(statement.get(), 2, value, store_label);
    step_done(database, statement.get(), store_label);
  }
}

[[nodiscard]] std::vector<Json> query_payloads(
    const std::filesystem::path &projection_path, std::string_view sql,
    std::string_view reference_key) {
  auto database = open_database(projection_path, store_label);
  create_tables(database.get());
  auto statement = prepare(database.get(), sql, store_label);
  std::vector<Json> result;
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    result.push_back({{std::string(reference_key),
                       column_text(statement.get(), 0)},
                      {"payload", Json::parse(column_text(statement.get(), 1))}});
  }
  return result;
}

[[nodiscard]] bool signature_exists(sqlite3 *database, std::string_view sql,
                                    std::string_view signature) {
  auto statement = prepare(database, sql, store_label);
  bind_text(statement.get(), 1, signature, store_label);
  return sqlite3_step(statement.get()) == SQLITE_ROW;
}

} // namespace

ImprovementLoopStore::ImprovementLoopStore(
    EgcfStore &egcf_store, AdaptationLineageStore &adaptation_store)
    : egcf_store_(egcf_store), adaptation_store_(adaptation_store),
      state_root_(egcf_store.state_root()), root_(state_root_ / "improvement-loop"),
      plan_root_(root_ / "evolution-plans" / "sha256"),
      qualification_root_(root_ / "evolution-step-qualifications" / "sha256"),
      assessment_root_(root_ / "evolution-assessments" / "sha256"),
      aggregate_root_(root_ / "experiment-aggregates" / "sha256"),
      decision_root_(root_ / "loop-decisions" / "sha256"),
      projection_path_(egcf_store.projection_path()) {
  for (const auto &path : {plan_root_, qualification_root_, assessment_root_,
                           aggregate_root_, decision_root_}) {
    std::filesystem::create_directories(path);
  }
  rebuild_projection();
}

const std::filesystem::path &ImprovementLoopStore::root() const noexcept {
  return root_;
}

saa::ReasoningEvidenceResolver ImprovementLoopStore::evidence_resolver() const {
  return adaptation_store_.evidence_resolver();
}

void ImprovementLoopStore::verify_evidence(
    const std::vector<std::string> &evidence_ids) const {
  const auto resolver = evidence_resolver();
  for (const auto &evidence_id : evidence_ids) {
    const auto record = resolver(evidence_id);
    if (!record) {
      error(store_label,
            "improvement evidence is not registered: " + evidence_id);
    }
    if (record->object_type != "egcf-evidence") {
      error(store_label,
            "improvement evidence must reference EvidenceArtifact");
    }
    if (record->success != true || record->simulated) {
      error(store_label,
            "improvement evidence must be successful and non-simulated");
    }
    if ((!record->producer.starts_with("deterministic-") &&
         !record->producer.starts_with("human-")) ||
        record->method == "reported") {
      error(store_label,
            "improvement evidence must be deterministic/human grounded");
    }
  }
}

Json ImprovementLoopStore::plan_payload(std::string_view plan_signature) const {
  auto database = open_database(projection_path_, store_label);
  create_tables(database.get());
  auto statement = prepare(
      database.get(),
      "SELECT payload_json FROM improvement_evolution_plans WHERE "
      "plan_signature=?",
      store_label);
  bind_text(statement.get(), 1, plan_signature, store_label);
  if (sqlite3_step(statement.get()) != SQLITE_ROW) {
    error(store_label, "evolution plan is not registered");
  }
  return Json::parse(column_text(statement.get(), 0));
}

std::string ImprovementLoopStore::register_evolution_plan(
    const saa::MultiStepEvolutionPlan &plan) {
  static_cast<void>(adaptation_store_.get_candidate(plan.final_candidate_ref));
  const Json payload = saa::to_json(plan);
  const auto stored = write_immutable(
      plan_root_, improvement_loop_store_version, "evolution-plan",
      plan.plan_signature, payload, store_label);
  auto database = open_database(projection_path_, store_label);
  create_tables(database.get());
  auto statement = prepare(
      database.get(),
      "INSERT OR IGNORE INTO improvement_evolution_plans "
      "VALUES(?,?,?,?,?,?,?,?)",
      store_label);
  bind_text(statement.get(), 1, stored.object_ref, store_label);
  bind_text(statement.get(), 2, plan.plan_signature, store_label);
  bind_text(statement.get(), 3, plan.root_algorithm_ref, store_label);
  bind_text(statement.get(), 4, plan.final_candidate_ref, store_label);
  bind_int(statement.get(), 5, static_cast<int>(plan.steps.size()), store_label);
  bind_text(statement.get(), 6, contracts::canonical_json(payload), store_label);
  bind_text(statement.get(), 7,
            stored.path.lexically_relative(state_root_).generic_string(),
            store_label);
  bind_text(statement.get(), 8, stored.created_at, store_label);
  step_done(database.get(), statement.get(), store_label);
  insert_metadata(database.get());
  return stored.object_ref;
}

std::string ImprovementLoopStore::register_step_qualification(
    const saa::EvolutionStepQualification &qualification) {
  const Json plan = plan_payload(qualification.plan_signature);
  std::set<std::string> candidate_refs;
  for (const auto &step : plan.at("steps")) {
    candidate_refs.insert(step.at("candidate_ref").get<std::string>());
  }
  if (!candidate_refs.contains(qualification.candidate_ref)) {
    error(store_label,
          "step qualification candidate is not part of registered plan");
  }
  verify_evidence(qualification.grounded_evidence_ids);
  const Json payload = saa::to_json(qualification);
  const auto stored = write_immutable(
      qualification_root_, improvement_loop_store_version,
      "evolution-step-qualification", qualification.qualification_signature,
      payload, store_label);
  auto database = open_database(projection_path_, store_label);
  create_tables(database.get());
  auto statement = prepare(
      database.get(),
      "INSERT OR IGNORE INTO improvement_step_qualifications "
      "VALUES(?,?,?,?,?,?,?,?,?)",
      store_label);
  bind_text(statement.get(), 1, stored.object_ref, store_label);
  bind_text(statement.get(), 2, qualification.qualification_signature,
            store_label);
  bind_text(statement.get(), 3, qualification.plan_signature, store_label);
  bind_text(statement.get(), 4, qualification.candidate_ref, store_label);
  bind_text(statement.get(), 5, qualification.status, store_label);
  bind_int(statement.get(), 6, qualification.step_qualified ? 1 : 0,
           store_label);
  bind_text(statement.get(), 7, contracts::canonical_json(payload), store_label);
  bind_text(statement.get(), 8,
            stored.path.lexically_relative(state_root_).generic_string(),
            store_label);
  bind_text(statement.get(), 9, stored.created_at, store_label);
  step_done(database.get(), statement.get(), store_label);
  return stored.object_ref;
}

std::string ImprovementLoopStore::register_evolution_assessment(
    const saa::MultiStepEvolutionAssessment &assessment) {
  const Json plan = plan_payload(assessment.plan_signature);
  if (assessment.final_candidate_ref !=
      plan.at("final_candidate_ref").get_ref<const std::string &>()) {
    error(store_label,
          "evolution assessment final candidate differs from registered plan");
  }
  if (assessment.evolution_qualified) {
    auto database = open_database(projection_path_, store_label);
    create_tables(database.get());
    auto statement = prepare(
        database.get(),
        "SELECT qualification_signature,candidate_ref,step_qualified FROM "
        "improvement_step_qualifications WHERE plan_signature=?",
        store_label);
    bind_text(statement.get(), 1, assessment.plan_signature, store_label);
    std::map<std::string, std::pair<std::string, bool>> stored;
    while (sqlite3_step(statement.get()) == SQLITE_ROW) {
      stored.emplace(column_text(statement.get(), 0),
                     std::pair{column_text(statement.get(), 1),
                               sqlite3_column_int(statement.get(), 2) != 0});
    }
    const std::set<std::string> assessment_signatures(
        assessment.qualification_signatures.begin(),
        assessment.qualification_signatures.end());
    std::set<std::string> stored_signatures;
    std::set<std::string> stored_candidates;
    bool all_qualified = true;
    for (const auto &[signature, candidate] : stored) {
      stored_signatures.insert(signature);
      stored_candidates.insert(candidate.first);
      all_qualified = all_qualified && candidate.second;
    }
    if (assessment_signatures != stored_signatures) {
      error(store_label,
            "qualified evolution assessment is not backed by the exact stored "
            "step qualifications");
    }
    if (!all_qualified) {
      error(store_label,
            "qualified evolution assessment references an unqualified "
            "intermediate step");
    }
    std::set<std::string> expected_candidates;
    for (const auto &step : plan.at("steps")) {
      expected_candidates.insert(step.at("candidate_ref").get<std::string>());
    }
    if (stored_candidates != expected_candidates) {
      error(store_label,
            "qualified evolution assessment does not cover every plan "
            "candidate");
    }
  }
  const Json payload = saa::to_json(assessment);
  const auto stored = write_immutable(
      assessment_root_, improvement_loop_store_version,
      "evolution-assessment", assessment.assessment_signature, payload,
      store_label);
  auto database = open_database(projection_path_, store_label);
  create_tables(database.get());
  auto statement = prepare(
      database.get(),
      "INSERT OR IGNORE INTO improvement_evolution_assessments "
      "VALUES(?,?,?,?,?,?,?,?,?)",
      store_label);
  bind_text(statement.get(), 1, stored.object_ref, store_label);
  bind_text(statement.get(), 2, assessment.assessment_signature, store_label);
  bind_text(statement.get(), 3, assessment.plan_signature, store_label);
  bind_text(statement.get(), 4, assessment.final_candidate_ref, store_label);
  bind_text(statement.get(), 5, assessment.status, store_label);
  bind_int(statement.get(), 6, assessment.evolution_qualified ? 1 : 0,
           store_label);
  bind_text(statement.get(), 7, contracts::canonical_json(payload), store_label);
  bind_text(statement.get(), 8,
            stored.path.lexically_relative(state_root_).generic_string(),
            store_label);
  bind_text(statement.get(), 9, stored.created_at, store_label);
  step_done(database.get(), statement.get(), store_label);
  return stored.object_ref;
}

std::string ImprovementLoopStore::register_experiment_aggregate(
    const saa::RepeatedExperimentAggregate &aggregate) {
  const auto results =
      adaptation_store_.experiment_results(aggregate.design_signature);
  std::map<std::string, Json> registered;
  for (const auto &item : results) {
    const Json &payload = item.at("payload");
    registered.emplace(payload.at("result_signature").get<std::string>(),
                       payload);
  }
  const std::set<std::string> expected(aggregate.result_signatures.begin(),
                                       aggregate.result_signatures.end());
  std::set<std::string> actual;
  for (const auto &[signature, payload] : registered) {
    static_cast<void>(payload);
    actual.insert(signature);
  }
  if (expected != actual) {
    error(store_label,
          "experiment aggregate must bind exactly the registered repeated "
          "results for its design");
  }
  if (aggregate.sustained_improvement_qualified &&
      !std::ranges::all_of(registered, [](const auto &entry) {
        return entry.second.value("candidate_improvement_qualified", false);
      })) {
    error(store_label,
          "sustained improvement cannot be registered over an unqualified "
          "constituent result");
  }
  const Json payload = saa::to_json(aggregate);
  const auto stored = write_immutable(
      aggregate_root_, improvement_loop_store_version,
      "experiment-aggregate", aggregate.aggregate_signature, payload,
      store_label);
  auto database = open_database(projection_path_, store_label);
  create_tables(database.get());
  auto statement = prepare(
      database.get(),
      "INSERT OR IGNORE INTO improvement_experiment_aggregates "
      "VALUES(?,?,?,?,?,?,?,?,?)",
      store_label);
  bind_text(statement.get(), 1, stored.object_ref, store_label);
  bind_text(statement.get(), 2, aggregate.aggregate_signature, store_label);
  bind_text(statement.get(), 3, aggregate.design_signature, store_label);
  bind_int(statement.get(), 4, aggregate.experiment_count, store_label);
  bind_text(statement.get(), 5, aggregate.status, store_label);
  bind_int(statement.get(), 6,
           aggregate.sustained_improvement_qualified ? 1 : 0, store_label);
  bind_text(statement.get(), 7, contracts::canonical_json(payload), store_label);
  bind_text(statement.get(), 8,
            stored.path.lexically_relative(state_root_).generic_string(),
            store_label);
  bind_text(statement.get(), 9, stored.created_at, store_label);
  step_done(database.get(), statement.get(), store_label);
  return stored.object_ref;
}

std::string ImprovementLoopStore::register_loop_decision(
    const saa::IntelligenceImprovementDecision &decision) {
  auto database = open_database(projection_path_, store_label);
  create_tables(database.get());
  if (!decision.evolution_assessment_signature.empty() &&
      !signature_exists(
          database.get(),
          "SELECT 1 FROM improvement_evolution_assessments WHERE "
          "assessment_signature=?",
          decision.evolution_assessment_signature)) {
    error(store_label,
          "loop decision references an unregistered evolution assessment");
  }
  if (!decision.experiment_aggregate_signature.empty() &&
      !signature_exists(
          database.get(),
          "SELECT 1 FROM improvement_experiment_aggregates WHERE "
          "aggregate_signature=?",
          decision.experiment_aggregate_signature)) {
    error(store_label,
          "loop decision references an unregistered experiment aggregate");
  }
  if (!decision.promotion_ref.empty()) {
    const auto promotions = adaptation_store_.promotions();
    if (std::ranges::none_of(promotions, [&](const Json &promotion) {
          return promotion.value("promotion_ref", "") == decision.promotion_ref;
        })) {
      error(store_label,
            "loop decision references an unregistered adaptation promotion");
    }
  }
  if (decision.status == "CLOSED_LOOP_IMPROVEMENT_VERIFIED" &&
      (decision.promotion_ref.empty() ||
       decision.post_promotion_receipt_signature.empty())) {
    error(store_label,
          "closed-loop verification requires promotion and post-promotion "
          "retrieval");
  }
  const Json payload = saa::to_json(decision);
  const auto stored = write_immutable(
      decision_root_, improvement_loop_store_version,
      "improvement-loop-decision", decision.decision_signature, payload,
      store_label);
  auto statement = prepare(
      database.get(),
      "INSERT OR IGNORE INTO improvement_loop_decisions "
      "VALUES(?,?,?,?,?,?,?,?,?)",
      store_label);
  bind_text(statement.get(), 1, stored.object_ref, store_label);
  bind_text(statement.get(), 2, decision.decision_signature, store_label);
  bind_text(statement.get(), 3, decision.phase, store_label);
  bind_text(statement.get(), 4, decision.status, store_label);
  bind_int(statement.get(), 5, decision.terminal ? 1 : 0, store_label);
  bind_text(statement.get(), 6, decision.candidate_ref.value_or(""),
            store_label);
  bind_text(statement.get(), 7, contracts::canonical_json(payload), store_label);
  bind_text(statement.get(), 8,
            stored.path.lexically_relative(state_root_).generic_string(),
            store_label);
  bind_text(statement.get(), 9, stored.created_at, store_label);
  step_done(database.get(), statement.get(), store_label);
  return stored.object_ref;
}

std::vector<Json> ImprovementLoopStore::decisions() const {
  return query_payloads(
      projection_path_,
      "SELECT decision_ref,payload_json FROM improvement_loop_decisions "
      "ORDER BY created_at,decision_ref",
      "decision_ref");
}

std::vector<Json> ImprovementLoopStore::aggregates() const {
  return query_payloads(
      projection_path_,
      "SELECT aggregate_ref,payload_json FROM "
      "improvement_experiment_aggregates ORDER BY aggregate_ref",
      "aggregate_ref");
}

void ImprovementLoopStore::rebuild_projection() {
  auto database = open_database(projection_path_, store_label);
  create_tables(database.get());
  execute(database.get(), "BEGIN IMMEDIATE", store_label);
  try {
    execute(database.get(), R"SQL(
      DELETE FROM improvement_evolution_plans;
      DELETE FROM improvement_step_qualifications;
      DELETE FROM improvement_evolution_assessments;
      DELETE FROM improvement_experiment_aggregates;
      DELETE FROM improvement_loop_decisions;
      DELETE FROM improvement_store_metadata;
    )SQL", store_label);
    const auto visit = [&](const std::filesystem::path &root,
                           std::string_view kind,
                           std::string_view signature_key,
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
    visit(plan_root_, "evolution-plan", "plan_signature",
          "INSERT INTO improvement_evolution_plans VALUES(?,?,?,?,?,?,?,?)",
          [&](sqlite3_stmt *statement, const Json &payload,
              const std::string &object_ref, const std::string &path,
              const std::string &created_at) {
            bind_text(statement, 1, object_ref, store_label);
            bind_text(statement, 2,
                      payload.at("plan_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 3,
                      payload.at("root_algorithm_ref")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 4,
                      payload.at("final_candidate_ref")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_int(statement, 5,
                     static_cast<int>(payload.at("steps").size()), store_label);
            bind_text(statement, 6, contracts::canonical_json(payload),
                      store_label);
            bind_text(statement, 7, path, store_label);
            bind_text(statement, 8, created_at, store_label);
          });
    visit(qualification_root_, "evolution-step-qualification",
          "qualification_signature",
          "INSERT INTO improvement_step_qualifications "
          "VALUES(?,?,?,?,?,?,?,?,?)",
          [&](sqlite3_stmt *statement, const Json &payload,
              const std::string &object_ref, const std::string &path,
              const std::string &created_at) {
            bind_text(statement, 1, object_ref, store_label);
            bind_text(statement, 2,
                      payload.at("qualification_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 3,
                      payload.at("plan_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 4,
                      payload.at("candidate_ref")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 5,
                      payload.at("status").get_ref<const std::string &>(),
                      store_label);
            bind_int(statement, 6,
                     payload.at("step_qualified").get<bool>() ? 1 : 0,
                     store_label);
            bind_text(statement, 7, contracts::canonical_json(payload),
                      store_label);
            bind_text(statement, 8, path, store_label);
            bind_text(statement, 9, created_at, store_label);
          });
    visit(assessment_root_, "evolution-assessment", "assessment_signature",
          "INSERT INTO improvement_evolution_assessments "
          "VALUES(?,?,?,?,?,?,?,?,?)",
          [&](sqlite3_stmt *statement, const Json &payload,
              const std::string &object_ref, const std::string &path,
              const std::string &created_at) {
            bind_text(statement, 1, object_ref, store_label);
            bind_text(statement, 2,
                      payload.at("assessment_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 3,
                      payload.at("plan_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 4,
                      payload.at("final_candidate_ref")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 5,
                      payload.at("status").get_ref<const std::string &>(),
                      store_label);
            bind_int(statement, 6,
                     payload.at("evolution_qualified").get<bool>() ? 1 : 0,
                     store_label);
            bind_text(statement, 7, contracts::canonical_json(payload),
                      store_label);
            bind_text(statement, 8, path, store_label);
            bind_text(statement, 9, created_at, store_label);
          });
    visit(aggregate_root_, "experiment-aggregate", "aggregate_signature",
          "INSERT INTO improvement_experiment_aggregates "
          "VALUES(?,?,?,?,?,?,?,?,?)",
          [&](sqlite3_stmt *statement, const Json &payload,
              const std::string &object_ref, const std::string &path,
              const std::string &created_at) {
            bind_text(statement, 1, object_ref, store_label);
            bind_text(statement, 2,
                      payload.at("aggregate_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 3,
                      payload.at("design_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_int(statement, 4,
                     payload.at("experiment_count").get<int>(), store_label);
            bind_text(statement, 5,
                      payload.at("status").get_ref<const std::string &>(),
                      store_label);
            bind_int(statement, 6,
                     payload.at("sustained_improvement_qualified").get<bool>()
                         ? 1
                         : 0,
                     store_label);
            bind_text(statement, 7, contracts::canonical_json(payload),
                      store_label);
            bind_text(statement, 8, path, store_label);
            bind_text(statement, 9, created_at, store_label);
          });
    visit(decision_root_, "improvement-loop-decision", "decision_signature",
          "INSERT INTO improvement_loop_decisions VALUES(?,?,?,?,?,?,?,?,?)",
          [&](sqlite3_stmt *statement, const Json &payload,
              const std::string &object_ref, const std::string &path,
              const std::string &created_at) {
            bind_text(statement, 1, object_ref, store_label);
            bind_text(statement, 2,
                      payload.at("decision_signature")
                          .get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 3,
                      payload.at("phase").get_ref<const std::string &>(),
                      store_label);
            bind_text(statement, 4,
                      payload.at("status").get_ref<const std::string &>(),
                      store_label);
            bind_int(statement, 5, payload.at("terminal").get<bool>() ? 1 : 0,
                     store_label);
            bind_text(statement, 6,
                      payload.value("candidate_ref", std::string()),
                      store_label);
            bind_text(statement, 7, contracts::canonical_json(payload),
                      store_label);
            bind_text(statement, 8, path, store_label);
            bind_text(statement, 9, created_at, store_label);
          });
    insert_metadata(database.get());
    execute(database.get(), "COMMIT", store_label);
  } catch (...) {
    sqlite3_exec(database.get(), "ROLLBACK", nullptr, nullptr, nullptr);
    throw;
  }
}

} // namespace statewright::egcf
