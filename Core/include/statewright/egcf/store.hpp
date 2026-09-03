#pragma once

#include "statewright/contracts/canonical_json.hpp"
#include "statewright/egcf/records.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::egcf {

inline constexpr int egcf_projection_schema_version = 3;

struct StoredObject final {
  std::string object_id;
  std::string object_type;
  std::string digest;
  contracts::Json payload = contracts::Json::object();
  std::filesystem::path relative_path;
};

struct ArtifactBytes final {
  std::string artifact_id;
  std::string digest;
  std::size_t size = 0;
  std::filesystem::path path;
};

struct ProjectionCheckpoint final {
  int schema_version = egcf_projection_schema_version;
  std::string authoritative_digest;
  std::string event_head;
  std::size_t object_count = 0;
  std::size_t event_count = 0;
};

[[nodiscard]] contracts::Json to_json(const StoredObject &object);
[[nodiscard]] contracts::Json to_json(const ArtifactBytes &artifact);
[[nodiscard]] contracts::Json to_json(const ProjectionCheckpoint &checkpoint);

class ObjectStore final {
public:
  ObjectStore(std::filesystem::path root,
              std::filesystem::path resource_root);

  [[nodiscard]] const std::filesystem::path &root() const noexcept;
  [[nodiscard]] const RecordSchemaRegistry &schemas() const noexcept;
  [[nodiscard]] std::filesystem::path path_for(
      std::string_view object_id) const;
  [[nodiscard]] bool contains(std::string_view object_id) const;
  [[nodiscard]] std::string put(const EgcfRecord &record);
  [[nodiscard]] EgcfEnvelope get_envelope(std::string_view object_id) const;
  [[nodiscard]] EgcfRecord get(std::string_view object_id) const;
  [[nodiscard]] std::vector<EgcfEnvelope> envelopes() const;

private:
  std::filesystem::path root_;
  RecordSchemaRegistry schemas_;
};

class ArtifactStore final {
public:
  explicit ArtifactStore(std::filesystem::path root);

  [[nodiscard]] const std::filesystem::path &root() const noexcept;
  [[nodiscard]] ArtifactBytes put(std::span<const std::byte> content);
  [[nodiscard]] std::vector<std::byte> get(
      std::string_view artifact_id) const;

private:
  std::filesystem::path root_;
};

class EgcfStore final {
public:
  EgcfStore(std::filesystem::path workspace_root,
            std::filesystem::path resource_root);
  ~EgcfStore();

  EgcfStore(const EgcfStore &) = delete;
  EgcfStore &operator=(const EgcfStore &) = delete;
  EgcfStore(EgcfStore &&) = delete;
  EgcfStore &operator=(EgcfStore &&) = delete;

  [[nodiscard]] const std::filesystem::path &workspace_root() const noexcept;
  [[nodiscard]] const std::filesystem::path &state_root() const noexcept;
  [[nodiscard]] const std::filesystem::path &projection_path() const noexcept;
  [[nodiscard]] const std::string &parent_event_head() const noexcept;
  [[nodiscard]] const ObjectStore &objects() const noexcept;
  [[nodiscard]] const ArtifactStore &artifacts() const noexcept;

  [[nodiscard]] std::string register_record(
      const EgcfRecord &record,
      std::string_view event_type = "egcf_object_registered");
  [[nodiscard]] std::vector<std::string> register_records(
      const std::vector<EgcfRecord> &records,
      std::string_view event_type = "egcf_object_registered");
  [[nodiscard]] std::vector<std::string>
  register_resources(const EgcfResourceBundle &bundle);
  [[nodiscard]] std::string register_artifact(
      std::span<const std::byte> content, std::string media_type,
      std::vector<std::string> source_ids = {},
      contracts::Json provenance = contracts::Json::object(),
      std::optional<std::string> created_at = std::nullopt);
  [[nodiscard]] EgcfRecord get(std::string_view object_id) const;
  [[nodiscard]] std::vector<StoredObject>
  list(std::optional<std::string> object_type = std::nullopt);
  [[nodiscard]] std::vector<StoredObject>
  search_text(std::string_view query,
              std::optional<std::string> object_type = std::nullopt,
              std::size_t limit = 20U);
  [[nodiscard]] std::string supersede(
      std::string old_id, std::string new_id, std::string reason,
      std::string authority,
      std::optional<std::string> created_at = std::nullopt);
  [[nodiscard]] std::vector<std::string>
  active_ids(std::string_view object_type);

  [[nodiscard]] std::vector<contracts::Json> events() const;
  [[nodiscard]] std::string event_head() const;
  [[nodiscard]] ProjectionCheckpoint projection_checkpoint() const;
  void validate_projection() const;
  void rebuild_projection();

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace statewright::egcf
