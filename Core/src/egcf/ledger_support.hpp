#pragma once

#include "statewright/common/error.hpp"
#include "statewright/contracts/canonical_json.hpp"
#include "statewright/contracts/typed_id.hpp"
#include "statewright/core/file_io.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::egcf::ledger_support {

using Json = contracts::Json;
using Database = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

[[noreturn]] inline void error(std::string_view label, std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument,
                      std::string(label) + ": " + std::move(message));
}

[[nodiscard]] inline std::string utc_now() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t value = std::chrono::system_clock::to_time_t(now);
  std::tm parts{};
  gmtime_r(&value, &parts);
  std::array<char, 32> buffer{};
  if (std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ",
                    &parts) == 0U) {
    error("ledger", "cannot format timestamp");
  }
  return buffer.data();
}

[[nodiscard]] inline Database open_database(
    const std::filesystem::path &path, std::string_view label) {
  std::filesystem::create_directories(path.parent_path());
  sqlite3 *raw = nullptr;
  const int result = sqlite3_open_v2(
      path.c_str(), &raw,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
      nullptr);
  Database database(raw, &sqlite3_close);
  if (result != SQLITE_OK || !database) {
    const std::string message =
        raw == nullptr ? "unknown SQLite error" : sqlite3_errmsg(raw);
    error(label, "cannot open projection: " + message);
  }
  sqlite3_busy_timeout(database.get(), 5'000);
  return database;
}

inline void execute(sqlite3 *database, std::string_view sql,
                    std::string_view label) {
  char *message = nullptr;
  const int result = sqlite3_exec(database, std::string(sql).c_str(), nullptr,
                                  nullptr, &message);
  if (result != SQLITE_OK) {
    const std::string text =
        message == nullptr ? sqlite3_errmsg(database) : std::string(message);
    sqlite3_free(message);
    error(label, "projection SQL failed: " + text);
  }
}

[[nodiscard]] inline Statement prepare(sqlite3 *database,
                                       std::string_view sql,
                                       std::string_view label) {
  sqlite3_stmt *raw = nullptr;
  if (sqlite3_prepare_v2(database, std::string(sql).c_str(), -1, &raw,
                         nullptr) != SQLITE_OK) {
    error(label, "cannot prepare SQL: " +
                     std::string(sqlite3_errmsg(database)));
  }
  return Statement(raw, &sqlite3_finalize);
}

inline void bind_text(sqlite3_stmt *statement, int index,
                      std::string_view value, std::string_view label) {
  if (sqlite3_bind_text(statement, index, value.data(),
                        static_cast<int>(value.size()), SQLITE_TRANSIENT) !=
      SQLITE_OK) {
    error(label, "cannot bind projection text");
  }
}

inline void bind_int(sqlite3_stmt *statement, int index, int value,
                     std::string_view label) {
  if (sqlite3_bind_int(statement, index, value) != SQLITE_OK) {
    error(label, "cannot bind projection integer");
  }
}

inline void step_done(sqlite3 *database, sqlite3_stmt *statement,
                      std::string_view label) {
  if (sqlite3_step(statement) != SQLITE_DONE) {
    error(label, "cannot update projection: " +
                     std::string(sqlite3_errmsg(database)));
  }
}

[[nodiscard]] inline std::string column_text(sqlite3_stmt *statement,
                                             int index) {
  const auto *text = sqlite3_column_text(statement, index);
  return text == nullptr ? std::string()
                         : std::string(reinterpret_cast<const char *>(text));
}

[[nodiscard]] inline std::string typed_ref(std::string_view kind,
                                           std::string_view signature) {
  return std::string(kind) + ":sha256:" + std::string(signature);
}

[[nodiscard]] inline std::filesystem::path path_for(
    const std::filesystem::path &root, std::string_view object_ref,
    std::string_view kind, std::string_view label) {
  const auto parts = contracts::parse_typed_id(object_ref);
  if (parts.object_type != kind) {
    error(label, "expected " + std::string(kind) + " reference");
  }
  return root / parts.digest.substr(0, 2U) / (parts.digest + ".json");
}

struct StoredEnvelope final {
  std::string object_ref;
  std::filesystem::path path;
  std::string created_at;
};

[[nodiscard]] inline Json read_envelope(const std::filesystem::path &path,
                                        std::string_view label) {
  try {
    return Json::parse(core::read_text(path));
  } catch (const std::exception &exception) {
    error(label, "cannot read immutable object " + path.string() + ": " +
                     exception.what());
  }
}

[[nodiscard]] inline StoredEnvelope write_immutable(
    const std::filesystem::path &root, std::string_view store_version,
    std::string_view kind, std::string_view signature, const Json &payload,
    std::string_view label) {
  const std::string object_ref = typed_ref(kind, signature);
  const std::filesystem::path path =
      path_for(root, object_ref, kind, label);
  if (std::filesystem::exists(path)) {
    const Json existing = read_envelope(path, label);
    if (existing.value("object_id", "") != object_ref ||
        !existing.contains("payload") ||
        contracts::canonical_json(existing.at("payload")) !=
            contracts::canonical_json(payload)) {
      error(label, "immutable collision at " + path.string());
    }
    return {.object_ref = object_ref,
            .path = path,
            .created_at = existing.value("created_at", "")};
  }
  const std::string created_at = utc_now();
  const Json envelope = {{"created_at", created_at},
                         {"object_id", object_ref},
                         {"payload", payload},
                         {"schema_version", 1},
                         {"store_version", store_version}};
  core::atomic_write_text(path, envelope.dump(2) + "\n");
  return {.object_ref = object_ref,
          .path = path,
          .created_at = created_at};
}

[[nodiscard]] inline std::vector<std::filesystem::path>
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

inline void reset_statement(sqlite3_stmt *statement) {
  sqlite3_reset(statement);
  sqlite3_clear_bindings(statement);
}

} // namespace statewright::egcf::ledger_support
