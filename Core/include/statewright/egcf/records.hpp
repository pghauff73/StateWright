#pragma once

#include "statewright/contracts/canonical_json.hpp"
#include "statewright/egcf/registry.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::egcf {

inline constexpr int egcf_object_schema_version = 1;

struct EgcfRecord final {
  std::string object_type;
  contracts::Json payload = contracts::Json::object();

  [[nodiscard]] std::string object_id() const;
};

struct EgcfEnvelope final {
  int schema_version = egcf_object_schema_version;
  std::string object_type;
  std::string object_id;
  contracts::Json payload = contracts::Json::object();
};

[[nodiscard]] contracts::Json to_json(const EgcfRecord &record);
[[nodiscard]] contracts::Json to_json(const EgcfEnvelope &envelope);
[[nodiscard]] EgcfEnvelope envelope_for(const EgcfRecord &record);
[[nodiscard]] EgcfEnvelope envelope_from_json(const contracts::Json &value);

[[nodiscard]] EgcfRecord as_record(const CommandDefinition &definition);
[[nodiscard]] EgcfRecord as_record(const AlgorithmDefinition &definition);
[[nodiscard]] EgcfRecord as_record(const QualificationRecord &record);
[[nodiscard]] EgcfRecord as_record(const SelectionDecision &decision);

class RecordSchemaRegistry final {
public:
  explicit RecordSchemaRegistry(std::filesystem::path resource_root);

  [[nodiscard]] const std::filesystem::path &resource_root() const noexcept;
  [[nodiscard]] std::vector<std::string> object_types() const;
  [[nodiscard]] const contracts::Json &schema_for(
      std::string_view object_type) const;

  void validate_record_payload(std::string_view object_type,
                               const contracts::Json &payload) const;
  void validate_json_value(const contracts::Json &schema,
                           const contracts::Json &value,
                           std::string_view path = "$input") const;

private:
  [[nodiscard]] const contracts::Json &resolve_reference(
      std::string_view reference) const;

  std::filesystem::path resource_root_;
  contracts::Json object_schema_ = contracts::Json::object();
};

struct EgcfResourceBundle final {
  ResourceReceipt receipt;
  contracts::Json algorithm_catalog = contracts::Json::object();
  std::vector<EgcfRecord> command_definitions;
  std::vector<EgcfRecord> algorithm_definitions;
  std::vector<EgcfRecord> workflow_definitions;
};

[[nodiscard]] EgcfResourceBundle
load_resource_bundle(const std::filesystem::path &resource_root);

} // namespace statewright::egcf
