#include "statewright/common/error.hpp"
#include "statewright/saa/projection.hpp"

#include <catch2/catch_test_macros.hpp>

#include <sqlite3.h>

#include <chrono>
#include <filesystem>

namespace {

statewright::contracts::Json add_algorithm() {
  return {{"nodes",
           {{{"id", "sum"},
             {"operands", {{{"input", 0}}, {{"input", 1}}}},
             {"primitive", "ADD"}}}},
          {"inputs", {{{"position", 0}}, {{"position", 1}}}},
          {"outputs",
           {{{"position", 0}, {"source", {{"node", "sum"}}}}}}};
}

std::filesystem::path temporary_directory() {
  const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
  auto path = std::filesystem::temp_directory_path() /
              ("statewright-saa-projection-" + std::to_string(suffix));
  std::filesystem::create_directories(path);
  return path;
}

} // namespace

TEST_CASE("SAA SQLite projection rebuilds from authoritative algorithms") {
  const auto root = temporary_directory();
  const auto database = root / "projection.sqlite3";
  statewright::saa::AlgorithmSearchIndex index;
  const auto algorithm = statewright::saa::make_searchable_algorithm(
      statewright::saa::canonicalize_mapping(add_algorithm()), "control",
      {"sum", "aggregation"}, {"bounded"}, {"e1"}, "QUALIFIED");
  const auto identity = index.register_algorithm(algorithm);
  const statewright::saa::AlgorithmSearchProjection projection(database);
  const auto digest = projection.rebuild(index);
  REQUIRE(digest.size() == 64U);
  REQUIRE_NOTHROW(projection.verify(index));
  REQUIRE(projection.search_text("sum") ==
          std::vector<std::string>{identity});

  std::filesystem::remove(database);
  REQUIRE(projection.rebuild(index) == digest);
  REQUIRE_NOTHROW(projection.verify(index));
  std::filesystem::remove_all(root);
}

TEST_CASE("SAA SQLite projection rejects stale or corrupt rows") {
  const auto root = temporary_directory();
  const auto database = root / "projection.sqlite3";
  statewright::saa::AlgorithmSearchIndex index;
  const auto identity = index.register_algorithm(
      statewright::saa::make_searchable_algorithm(
          statewright::saa::canonicalize_mapping(add_algorithm()), "control",
          {"sum"}, {}, {}, "QUALIFIED"));
  static_cast<void>(identity);
  const statewright::saa::AlgorithmSearchProjection projection(database);
  const auto digest = projection.rebuild(index);
  REQUIRE(digest.size() == 64U);

  sqlite3 *raw = nullptr;
  REQUIRE(sqlite3_open(database.c_str(), &raw) == SQLITE_OK);
  REQUIRE(sqlite3_exec(raw, "UPDATE algorithms SET signature='tampered'", nullptr,
                       nullptr, nullptr) == SQLITE_OK);
  REQUIRE(sqlite3_close(raw) == SQLITE_OK);
  REQUIRE_THROWS_AS(projection.verify(index), statewright::common::Error);
  std::filesystem::remove_all(root);
}
