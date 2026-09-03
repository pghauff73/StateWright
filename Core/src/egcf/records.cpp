#include "statewright/egcf/records.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/typed_id.hpp"
#include "statewright/core/file_io.hpp"

#include <algorithm>
#include <cmath>
#include <regex>
#include <set>
#include <string>
#include <utility>

namespace statewright::egcf {
namespace {

using Json = contracts::Json;

[[noreturn]] void schema_error(std::string message) {
  throw common::Error(common::ErrorCode::json_contract, std::move(message));
}

[[nodiscard]] Json read_json(const std::filesystem::path &path) {
  try {
    return contracts::parse_json(core::read_text(path));
  } catch (const common::Error &error) {
    schema_error("cannot load EGCF schema resource " + path.string() + ": " +
                 error.what());
  }
}

[[nodiscard]] bool matches_type(std::string_view expected, const Json &value) {
  if (expected == "object") {
    return value.is_object();
  }
  if (expected == "array") {
    return value.is_array();
  }
  if (expected == "string") {
    return value.is_string();
  }
  if (expected == "integer") {
    return value.is_number_integer() || value.is_number_unsigned();
  }
  if (expected == "number") {
    return value.is_number() && !value.is_boolean();
  }
  if (expected == "boolean") {
    return value.is_boolean();
  }
  return expected == "null" && value.is_null();
}

[[nodiscard]] std::string type_label(const Json &expected) {
  if (expected.is_string()) {
    return expected.get<std::string>();
  }
  return "one of " + contracts::canonical_json(expected);
}

[[nodiscard]] std::vector<std::string> string_array(const Json &value,
                                                     std::string_view label) {
  if (!value.is_array()) {
    schema_error(std::string(label) + " must be an array");
  }
  std::vector<std::string> result;
  result.reserve(value.size());
  for (const auto &item : value) {
    if (!item.is_string()) {
      schema_error(std::string(label) + " must contain strings");
    }
    result.push_back(item.get<std::string>());
  }
  return result;
}

[[nodiscard]] Json normalize_workflow(const Json &source) {
  Json payload = source;
  payload.erase("schema_version");
  for (auto &node : payload.at("nodes")) {
    if (!node.contains("depends_on")) {
      node["depends_on"] = Json::array();
    }
    if (!node.contains("when")) {
      node["when"] = Json::object();
    }
    if (!node.contains("retry_limit")) {
      node["retry_limit"] = 0;
    }
    if (!node.contains("checkpoint")) {
      node["checkpoint"] = false;
    }
  }
  return payload;
}

} // namespace

std::string EgcfRecord::object_id() const {
  return contracts::typed_id(object_type, payload);
}

Json to_json(const EgcfRecord &record) { return record.payload; }

Json to_json(const EgcfEnvelope &envelope) {
  return {{"object_id", envelope.object_id},
          {"object_type", envelope.object_type},
          {"payload", envelope.payload},
          {"schema_version", envelope.schema_version}};
}

EgcfEnvelope envelope_for(const EgcfRecord &record) {
  return {.schema_version = egcf_object_schema_version,
          .object_type = record.object_type,
          .object_id = record.object_id(),
          .payload = record.payload};
}

EgcfEnvelope envelope_from_json(const Json &value) {
  if (!value.is_object()) {
    schema_error("EGCF object envelope must be an object");
  }
  static const std::set<std::string> expected = {
      "object_id", "object_type", "payload", "schema_version"};
  std::vector<std::string> unknown;
  for (const auto &[key, ignored] : value.items()) {
    static_cast<void>(ignored);
    if (!expected.contains(key)) {
      unknown.push_back(key);
    }
  }
  if (!unknown.empty()) {
    schema_error("EGCF object envelope has unknown fields");
  }
  for (const auto &key : expected) {
    if (!value.contains(key)) {
      schema_error("EGCF object envelope is missing field: " + key);
    }
  }
  if (!value.at("schema_version").is_number_integer() ||
      value.at("schema_version").get<int>() != egcf_object_schema_version ||
      !value.at("object_type").is_string() ||
      !value.at("object_id").is_string() || !value.at("payload").is_object()) {
    schema_error("EGCF object envelope has invalid field types");
  }
  return {.schema_version = egcf_object_schema_version,
          .object_type = value.at("object_type").get<std::string>(),
          .object_id = value.at("object_id").get<std::string>(),
          .payload = value.at("payload")};
}

EgcfRecord as_record(const CommandDefinition &definition) {
  return {.object_type = "command-definition", .payload = to_json(definition)};
}

EgcfRecord as_record(const AlgorithmDefinition &definition) {
  return {.object_type = "algorithm-definition", .payload = to_json(definition)};
}

EgcfRecord as_record(const QualificationRecord &record) {
  return {.object_type = "qualification", .payload = to_json(record)};
}

EgcfRecord as_record(const SelectionDecision &decision) {
  return {.object_type = "selection-decision", .payload = to_json(decision)};
}

RecordSchemaRegistry::RecordSchemaRegistry(std::filesystem::path resource_root)
    : resource_root_(std::filesystem::weakly_canonical(resource_root)) {
  static_cast<void>(verify_resource_manifest(resource_root_));
  object_schema_ =
      read_json(resource_root_ / "schemas/egcf-v1/objects.schema.json");
  if (!object_schema_.is_object() || !object_schema_.contains("$defs") ||
      !object_schema_.at("$defs").is_object()) {
    schema_error("EGCF object schema lacks $defs");
  }
  if (object_schema_.at("$defs").size() != 25U) {
    schema_error("frozen EGCF object schema must define exactly 25 durable types");
  }

  const std::array extension_paths = {
      resource_root_ / "schemas/statewright-v1/objects-extension.schema.json",
      resource_root_ /
          "schemas/statewright-v1/internet-improvement-extension.schema.json"};
  for (const auto &extension_path : extension_paths) {
    const Json extension = read_json(extension_path);
    if (!extension.is_object() || !extension.contains("$defs") ||
        !extension.at("$defs").is_object() ||
        extension.at("$defs").empty()) {
      schema_error("StateWright object extension lacks durable types: " +
                   extension_path.string());
    }
    for (const auto &[name, schema] : extension.at("$defs").items()) {
      if (object_schema_.at("$defs").contains(name)) {
        schema_error("StateWright object extension redefines durable type: " +
                     name);
      }
      object_schema_["$defs"][name] = schema;
    }
  }
  const auto types = object_types();
  if (types.size() != 47U) {
    schema_error("EGCF object schema must define exactly 43 durable types");
  }
}

const std::filesystem::path &RecordSchemaRegistry::resource_root() const noexcept {
  return resource_root_;
}

std::vector<std::string> RecordSchemaRegistry::object_types() const {
  std::vector<std::string> result;
  if (object_schema_.contains("$defs") && object_schema_.at("$defs").is_object()) {
    result.reserve(object_schema_.at("$defs").size());
    for (const auto &[name, ignored] : object_schema_.at("$defs").items()) {
      static_cast<void>(ignored);
      result.push_back(name);
    }
  }
  return result;
}

const Json &RecordSchemaRegistry::schema_for(std::string_view object_type) const {
  const std::string normalized = contracts::normalize_object_type(object_type);
  if (normalized != object_type) {
    schema_error("EGCF object type must use canonical spelling: " +
                 std::string(object_type));
  }
  const auto &definitions = object_schema_.at("$defs");
  const auto iterator = definitions.find(normalized);
  if (iterator == definitions.end()) {
    schema_error("unknown EGCF object type: " + normalized);
  }
  return *iterator;
}

const Json &
RecordSchemaRegistry::resolve_reference(std::string_view reference) const {
  static constexpr std::string_view prefix = "#/$defs/";
  if (!reference.starts_with(prefix)) {
    schema_error("unsupported EGCF schema reference: " +
                 std::string(reference));
  }
  return schema_for(reference.substr(prefix.size()));
}

void RecordSchemaRegistry::validate_record_payload(
    std::string_view object_type, const Json &payload) const {
  if (!payload.is_object()) {
    schema_error(std::string(object_type) + " payload must be an object");
  }
  validate_json_value(schema_for(object_type), payload,
                      "$record." + std::string(object_type));
  if (object_type == "command-definition") {
    static const std::set<std::string> forbidden = {
        "callback", "callable", "executor", "function", "handler",
        "subprocess"};
    for (const auto &field : forbidden) {
      if (payload.contains(field)) {
        schema_error("command definitions cannot reference executors: " +
                     field);
      }
    }
  }
}

void RecordSchemaRegistry::validate_json_value(const Json &schema,
                                               const Json &value,
                                               std::string_view path) const {
  if (!schema.is_object()) {
    schema_error(std::string(path) + " schema must be an object");
  }
  if (value.is_object() && value.contains("$from")) {
    static const std::set<std::string> reference_fields = {"$from", "default",
                                                           "path"};
    for (const auto &[key, ignored] : value.items()) {
      static_cast<void>(ignored);
      if (!reference_fields.contains(key)) {
        schema_error(std::string(path) +
                     " reference has unknown field: " + key);
      }
    }
    if (!value.at("$from").is_string()) {
      schema_error(std::string(path) + " reference $from must be string");
    }
    if (value.contains("path")) {
      if (!value.at("path").is_array()) {
        schema_error(std::string(path) +
                     " reference path must be an array");
      }
      for (const auto &item : value.at("path")) {
        if (!item.is_string() && !item.is_number_integer() &&
            !item.is_number_unsigned()) {
          schema_error(std::string(path) +
                       " reference path must contain strings or integers");
        }
      }
    }
    return;
  }

  if (schema.contains("$ref")) {
    if (!schema.at("$ref").is_string()) {
      schema_error(std::string(path) + " schema reference must be a string");
    }
    validate_json_value(
        resolve_reference(schema.at("$ref").get<std::string>()), value, path);
    return;
  }

  if (schema.contains("allOf")) {
    for (const auto &subschema : schema.at("allOf")) {
      validate_json_value(subschema, value, path);
    }
  }
  const auto validate_alternatives = [&](std::string_view keyword,
                                         bool require_exactly_one) {
    if (!schema.contains(keyword)) {
      return;
    }
    const auto &alternatives = schema.at(keyword);
    if (!alternatives.is_array()) {
      schema_error(std::string(path) + " " + std::string(keyword) +
                   " must be an array");
    }
    std::size_t matches = 0;
    for (const auto &alternative : alternatives) {
      try {
        validate_json_value(alternative, value, path);
        ++matches;
      } catch (const common::Error &) {
      }
    }
    if (matches == 0U || (require_exactly_one && matches != 1U)) {
      schema_error(std::string(path) + " does not satisfy " +
                   std::string(keyword));
    }
  };
  validate_alternatives("anyOf", false);
  validate_alternatives("oneOf", true);

  if (schema.contains("type")) {
    const auto &expected = schema.at("type");
    bool matches = false;
    if (expected.is_string()) {
      matches = matches_type(expected.get<std::string>(), value);
    } else if (expected.is_array()) {
      for (const auto &item : expected) {
        if (item.is_string() && matches_type(item.get<std::string>(), value)) {
          matches = true;
          break;
        }
      }
    } else {
      schema_error(std::string(path) + " schema type is invalid");
    }
    if (!matches) {
      schema_error(std::string(path) + " must be " + type_label(expected));
    }
  }
  if (schema.contains("enum") &&
      std::find(schema.at("enum").begin(), schema.at("enum").end(), value) ==
          schema.at("enum").end()) {
    schema_error(std::string(path) + " is not an allowed enum value");
  }
  if (schema.contains("const") && value != schema.at("const")) {
    schema_error(std::string(path) + " does not equal the required constant");
  }
  if (value.is_string()) {
    const auto length = value.get_ref<const std::string &>().size();
    if (schema.contains("minLength") &&
        length < schema.at("minLength").get<std::size_t>()) {
      schema_error(std::string(path) + " is shorter than minLength");
    }
    if (schema.contains("maxLength") &&
        length > schema.at("maxLength").get<std::size_t>()) {
      schema_error(std::string(path) + " is longer than maxLength");
    }
    if (schema.contains("pattern")) {
      const std::regex pattern(schema.at("pattern").get<std::string>());
      if (!std::regex_match(value.get_ref<const std::string &>(), pattern)) {
        schema_error(std::string(path) + " does not match the required pattern");
      }
    }
  }
  if (value.is_number()) {
    const long double number = value.get<long double>();
    if (schema.contains("minimum") &&
        number < schema.at("minimum").get<long double>()) {
      schema_error(std::string(path) + " is below minimum");
    }
    if (schema.contains("maximum") &&
        number > schema.at("maximum").get<long double>()) {
      schema_error(std::string(path) + " is above maximum");
    }
  }
  if (value.is_object()) {
    const Json empty_properties = Json::object();
    const Json &properties = schema.contains("properties")
                                 ? schema.at("properties")
                                 : empty_properties;
    if (!properties.is_object()) {
      schema_error(std::string(path) + " schema properties must be an object");
    }
    if (schema.contains("required")) {
      for (const auto &name : string_array(schema.at("required"), "required")) {
        if (!value.contains(name)) {
          schema_error(std::string(path) +
                       " is missing required field: " + name);
        }
      }
    }
    for (const auto &[key, item] : value.items()) {
      const auto property = properties.find(key);
      if (property != properties.end() && !property->empty()) {
        validate_json_value(*property, item,
                            std::string(path) + "." + key);
        continue;
      }
      if (!schema.contains("additionalProperties")) {
        continue;
      }
      const auto &additional = schema.at("additionalProperties");
      if (additional.is_boolean() && !additional.get<bool>()) {
        schema_error(std::string(path) + " has unknown field: " + key);
      }
      if (additional.is_object()) {
        validate_json_value(additional, item,
                            std::string(path) + "." + key);
      }
    }
  }
  if (value.is_array()) {
    if (schema.contains("minItems") &&
        value.size() < schema.at("minItems").get<std::size_t>()) {
      schema_error(std::string(path) + " has fewer than minItems");
    }
    if (schema.contains("maxItems") &&
        value.size() > schema.at("maxItems").get<std::size_t>()) {
      schema_error(std::string(path) + " has more than maxItems");
    }
    if (schema.value("uniqueItems", false)) {
      std::set<std::string> values;
      for (const auto &item : value) {
        if (!values.insert(contracts::canonical_json(item)).second) {
          schema_error(std::string(path) + " must contain unique items");
        }
      }
    }
    if (schema.contains("items") && schema.at("items").is_object()) {
      for (std::size_t index = 0; index < value.size(); ++index) {
        validate_json_value(schema.at("items"), value.at(index),
                            std::string(path) + "[" +
                                std::to_string(index) + "]");
      }
    }
  }
}

EgcfResourceBundle load_resource_bundle(
    const std::filesystem::path &resource_root) {
  EgcfResourceBundle bundle;
  bundle.receipt = verify_resource_manifest(resource_root);
  const RecordSchemaRegistry schemas(bundle.receipt.resource_root);

  const CommandRegistry commands(bundle.receipt.resource_root);
  bundle.command_definitions.reserve(commands.definitions().size());
  for (const auto &definition : commands.definitions()) {
    auto record = as_record(definition);
    schemas.validate_record_payload(record.object_type, record.payload);
    bundle.command_definitions.push_back(std::move(record));
  }

  bundle.algorithm_catalog =
      read_json(bundle.receipt.resource_root / "algorithms/v1/catalog.json");
  if (!bundle.algorithm_catalog.is_object() ||
      bundle.algorithm_catalog.value("schema_version", 0) != 1 ||
      bundle.algorithm_catalog.value("floating_versions_allowed", true) ||
      bundle.algorithm_catalog.value("direct_command_callbacks_allowed", true)) {
    schema_error("EGCF algorithm catalog policy is invalid");
  }
  if (bundle.algorithm_catalog.contains("algorithms")) {
    if (!bundle.algorithm_catalog.at("algorithms").is_array()) {
      schema_error("EGCF algorithm catalog algorithms must be an array");
    }
    for (const auto &payload : bundle.algorithm_catalog.at("algorithms")) {
      schemas.validate_record_payload("algorithm-definition", payload);
      bundle.algorithm_definitions.push_back(
          {.object_type = "algorithm-definition", .payload = payload});
    }
  }

  const Json workflow_schema = read_json(
      bundle.receipt.resource_root / "schemas/egcf-v1/workflow.schema.json");
  const auto workflow_root = bundle.receipt.resource_root / "workflows/v1";
  if (std::filesystem::exists(workflow_root)) {
    for (const auto &entry : std::filesystem::directory_iterator(workflow_root)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".json") {
        continue;
      }
      const Json source = read_json(entry.path());
      schemas.validate_json_value(workflow_schema, source, "$workflow");
      Json payload = normalize_workflow(source);
      schemas.validate_record_payload("workflow-definition", payload);
      bundle.workflow_definitions.push_back(
          {.object_type = "workflow-definition", .payload = std::move(payload)});
    }
  }
  std::sort(bundle.workflow_definitions.begin(),
            bundle.workflow_definitions.end(),
            [](const EgcfRecord &left, const EgcfRecord &right) {
              return left.object_id() < right.object_id();
            });
  return bundle;
}

} // namespace statewright::egcf
