#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/core/file_io.hpp"
#include "statewright/egcf/records.hpp"
#include "statewright/egcf/store.hpp"

#include <catch2/catch_test_macros.hpp>

#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

using statewright::contracts::Json;

std::filesystem::path temporary_directory() {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  auto path = std::filesystem::temp_directory_path() /
              ("statewright-egcf-store-" + std::to_string(suffix));
  std::filesystem::create_directories(path);
  return path;
}

statewright::egcf::EgcfRecord intent(std::string objective) {
  return {.object_type = "intent",
          .payload = {{"actor", "user"},
                      {"ambiguities", Json::array()},
                      {"assumptions", Json::array()},
                      {"created_at", "2026-08-21T00:00:00Z"},
                      {"objective", std::move(objective)},
                      {"provenance", Json::object()},
                      {"raw_request", "inspect"},
                      {"raw_request_hash", "abc"}}};
}

std::vector<std::byte> bytes(std::string_view text) {
  const auto *begin = reinterpret_cast<const std::byte *>(text.data());
  return {begin, begin + text.size()};
}

Json read_json_file(const std::filesystem::path &path) {
  return statewright::contracts::parse_json(statewright::core::read_text(path));
}

} // namespace

TEST_CASE("EGCF schema registry covers every durable record type") {
  const statewright::egcf::RecordSchemaRegistry schemas(STATEWRIGHT_RESOURCE_ROOT);
  REQUIRE(schemas.object_types().size() == 55U);
  REQUIRE(schemas.schema_for("intent").at("additionalProperties") == false);
  REQUIRE_THROWS_AS(schemas.schema_for("Intent"), statewright::common::Error);

  auto valid = intent("inspect");
  REQUIRE_NOTHROW(schemas.validate_record_payload(valid.object_type,
                                                   valid.payload));
  auto missing = valid.payload;
  missing.erase("actor");
  REQUIRE_THROWS_AS(schemas.validate_record_payload("intent", missing),
                    statewright::common::Error);
  auto unknown = valid.payload;
  unknown["executor"] = "forbidden";
  REQUIRE_THROWS_AS(schemas.validate_record_payload("intent", unknown),
                    statewright::common::Error);
  auto wrong_type = valid.payload;
  wrong_type["assumptions"] = "not-an-array";
  REQUIRE_THROWS_AS(schemas.validate_record_payload("intent", wrong_type),
                    statewright::common::Error);
}

TEST_CASE("all frozen EGCF record fixtures retain exact oracle IDs") {
  const auto fixture_path = std::filesystem::path(STATEWRIGHT_EGCF_RECORD_FIXTURES);
  const auto fixture = read_json_file(fixture_path);
  REQUIRE(fixture.at("oracle_commit") ==
          "957111e2d5d11dec719c7f993f51644e701fc256");
  REQUIRE(fixture.at("case_count") == 25);
  const std::string expected_checksum = statewright::core::read_text(
      fixture_path.string() + ".sha256");
  REQUIRE(expected_checksum.substr(0, 64U) ==
          statewright::contracts::sha256_file(fixture_path));

  const statewright::egcf::RecordSchemaRegistry schemas(STATEWRIGHT_RESOURCE_ROOT);
  for (const auto &item : fixture.at("cases")) {
    const statewright::egcf::EgcfRecord record{
        .object_type = item.at("object_type").get<std::string>(),
        .payload = item.at("payload")};
    INFO(record.object_type);
    REQUIRE_NOTHROW(
        schemas.validate_record_payload(record.object_type, record.payload));
    REQUIRE(record.object_id() == item.at("object_id").get<std::string>());

    auto unknown = record.payload;
    unknown["unknown_fixture_field"] = true;
    REQUIRE_THROWS_AS(
        schemas.validate_record_payload(record.object_type, unknown),
        statewright::common::Error);
    const auto required = schemas.schema_for(record.object_type).at("required");
    REQUIRE_FALSE(required.empty());
    auto missing = record.payload;
    missing.erase(required.front().get<std::string>());
    REQUIRE_THROWS_AS(
        schemas.validate_record_payload(record.object_type, missing),
        statewright::common::Error);
  }
}

TEST_CASE("EGCF workflow references and resource imports are strict") {
  const statewright::egcf::RecordSchemaRegistry schemas(STATEWRIGHT_RESOURCE_ROOT);
  const Json reference = {{"$from", "interpret"},
                          {"path", Json::array({"result", 0})},
                          {"default", nullptr}};
  REQUIRE_NOTHROW(schemas.validate_json_value(
      {{"type", "string"}}, reference, "$input.text"));
  auto invalid_reference = reference;
  invalid_reference["executor"] = "hidden";
  REQUIRE_THROWS_AS(schemas.validate_json_value(
                        {{"type", "string"}}, invalid_reference, "$input.text"),
                    statewright::common::Error);

  const auto bundle =
      statewright::egcf::load_resource_bundle(STATEWRIGHT_RESOURCE_ROOT);
  REQUIRE(bundle.receipt.verified_files == 42U);
  REQUIRE(bundle.command_definitions.size() == 188U);
  REQUIRE(bundle.algorithm_definitions.empty());
  REQUIRE(bundle.workflow_definitions.size() == 1U);
  REQUIRE(bundle.workflow_definitions.front().payload.at("nodes").front().at(
              "depends_on") == Json::array());
  REQUIRE(bundle.command_definitions.at(0).object_id().starts_with(
      "command-definition:sha256:"));
}

TEST_CASE("EGCF resource admission persists one exact canonical catalog") {
  const auto root = temporary_directory();
  statewright::egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  const auto bundle =
      statewright::egcf::load_resource_bundle(STATEWRIGHT_RESOURCE_ROOT);
  const auto first_ids = store.register_resources(bundle);
  REQUIRE(first_ids.size() == 189U);
  REQUIRE(store.events().size() == 189U);
  REQUIRE(store.list("command-definition").size() == 188U);
  REQUIRE(store.list("workflow-definition").size() == 1U);
  REQUIRE(store.register_resources(bundle) == first_ids);
  REQUIRE(store.events().size() == 189U);
  REQUIRE(store.active_ids("command-definition").size() == 188U);
  std::filesystem::remove_all(root);
}

TEST_CASE("EGCF immutable object envelopes retain exact oracle identity") {
  const auto root = temporary_directory();
  statewright::egcf::ObjectStore store(root / "objects", STATEWRIGHT_RESOURCE_ROOT);
  const auto record = intent("inspect");
  const std::string expected =
      "intent:sha256:6ce04327e56440f3d5827242d7153c552c507a2d0b02eacc5b7221937ce6934f";
  REQUIRE(record.object_id() == expected);
  REQUIRE(store.put(record) == expected);
  REQUIRE(store.put(record) == expected);
  REQUIRE(store.get(expected).payload == record.payload);

  const auto path = store.path_for(expected);
  auto envelope = statewright::contracts::parse_json(
      statewright::core::read_text(path));
  envelope["payload"]["objective"] = "tampered";
  statewright::core::atomic_write_text(path,
                                       statewright::contracts::canonical_json(
                                           envelope));
  REQUIRE_THROWS_AS(store.get(expected), statewright::common::Error);
  std::filesystem::remove_all(root);
}

TEST_CASE("EGCF ledger rebuilds projection and preserves event chain") {
  const auto root = temporary_directory();
  statewright::egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  const auto first = intent("inspect");
  const auto first_id = store.register_record(first);
  REQUIRE(store.register_record(first) == first_id);
  REQUIRE(store.events().size() == 1U);

  const auto artifact_content = bytes("evidence");
  const auto artifact_id = store.register_artifact(
      artifact_content, "text/plain", {first_id}, {{"producer", "unit-test"}},
      "2026-08-21T00:00:01Z");
  REQUIRE(store.get(artifact_id).payload.at("size") == 8);
  REQUIRE(store.events().size() == 2U);
  REQUIRE(store.event_head().size() == 64U);
  const auto checkpoint = store.projection_checkpoint();
  REQUIRE(checkpoint.object_count == 2U);
  REQUIRE(checkpoint.event_count == 2U);
  REQUIRE_NOTHROW(store.validate_projection());

  std::filesystem::remove(store.projection_path());
  store.rebuild_projection();
  REQUIRE(store.projection_checkpoint().authoritative_digest ==
          checkpoint.authoritative_digest);
  REQUIRE_NOTHROW(store.validate_projection());
  REQUIRE(store.list().size() == 2U);

  std::ofstream corrupt(store.projection_path(), std::ios::binary | std::ios::trunc);
  REQUIRE(corrupt.is_open());
  corrupt << "not-sqlite";
  corrupt.close();
  REQUIRE(store.list().size() == 2U);
  REQUIRE_NOTHROW(store.validate_projection());
  std::filesystem::remove_all(root);
}

TEST_CASE("EGCF stale projections rebuild and FTS remains derived") {
  const auto root = temporary_directory();
  statewright::egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  const auto inspect_id = store.register_record(intent("inspect parser"));
  const auto repair_id = store.register_record(intent("repair renderer"));
  REQUIRE(store.search_text("parser").front().object_id == inspect_id);
  REQUIRE(store.search_text("renderer", "intent").front().object_id ==
          repair_id);

  sqlite3 *database = nullptr;
  REQUIRE(sqlite3_open(store.projection_path().c_str(), &database) == SQLITE_OK);
  REQUIRE(sqlite3_exec(database,
                       "UPDATE objects SET payload_json='{}' WHERE "
                       "object_id IS NOT NULL",
                       nullptr, nullptr, nullptr) == SQLITE_OK);
  REQUIRE(sqlite3_close(database) == SQLITE_OK);
  REQUIRE(store.list("intent").size() == 2U);
  REQUIRE_NOTHROW(store.validate_projection());
  std::filesystem::remove_all(root);
}

TEST_CASE("EGCF supersedence never mutates prior records") {
  const auto root = temporary_directory();
  statewright::egcf::EgcfStore store(root, STATEWRIGHT_RESOURCE_ROOT);
  const auto old_id = store.register_record(intent("inspect"));
  const auto new_id = store.register_record(intent("inspect safely"));
  const auto old_path = store.objects().path_for(old_id);
  const auto old_hash = statewright::contracts::sha256_file(old_path);
  const auto supersedence_id = store.supersede(
      old_id, new_id, "clarified objective", "human:owner",
      "2026-08-21T00:00:02Z");
  REQUIRE(supersedence_id.starts_with("supersedence:sha256:"));
  const auto second_supersedence_id = store.supersede(
      old_id, new_id, "qualification evidence refreshed", "human:owner",
      "2026-08-21T00:00:03Z");
  REQUIRE(second_supersedence_id != supersedence_id);
  REQUIRE(store.list("supersedence").size() == 2U);
  REQUIRE(statewright::contracts::sha256_file(old_path) == old_hash);
  REQUIRE(store.active_ids("intent") == std::vector<std::string>{new_id});
  REQUIRE(store.get(old_id).payload.at("objective") == "inspect");
  REQUIRE_NOTHROW(store.validate_projection());
  std::filesystem::remove_all(root);
}

TEST_CASE("EGCF workspace lock rejects concurrent writers") {
  const auto root = temporary_directory();
  statewright::egcf::EgcfStore first(root, STATEWRIGHT_RESOURCE_ROOT);
  REQUIRE_THROWS_AS(
      statewright::egcf::EgcfStore(root, STATEWRIGHT_RESOURCE_ROOT),
      statewright::common::Error);
  std::filesystem::remove_all(root);
}
