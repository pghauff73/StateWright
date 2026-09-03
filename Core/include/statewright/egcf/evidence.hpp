#pragma once

#include "statewright/contracts/canonical_json.hpp"
#include "statewright/egcf/store.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace statewright::egcf {

struct EvidenceRequirement final {
  std::string subject_id;
  std::string name;
  std::string category;
  std::string oracle;
  int freshness_seconds = 0;
  std::string independence_group;
  bool mandatory = true;

  [[nodiscard]] std::string object_id() const;
};

struct EvidenceArtifact final {
  std::string subject_id;
  std::vector<std::string> claim_ids;
  std::vector<std::string> requirement_ids;
  std::string category;
  std::string producer;
  std::string method;
  std::string source_snapshot_hash;
  std::string target;
  std::string oracle;
  contracts::Json environment = contracts::Json::object();
  std::string command_id;
  std::string algorithm_id;
  std::string created_at;
  std::string sha256;
  std::optional<bool> success;
  std::vector<std::string> limitations;
  std::string independence_group;
  bool simulated = false;
  std::string path;
  contracts::Json content;

  [[nodiscard]] std::string object_id() const;
};

struct ConfidenceAssessment final {
  std::string subject_id;
  std::string policy;
  contracts::Json dimensions = contracts::Json::object();
  std::vector<std::string> blocking_gaps;
  std::vector<std::string> conflicts;
  std::vector<std::string> known_unknowns;
  std::string conclusion;
  std::vector<std::string> evidence_ids;
  std::string created_at;

  [[nodiscard]] std::string object_id() const;
};

struct EvidenceInput final {
  std::string subject_id{};
  contracts::Json content = nullptr;
  std::string category{};
  std::string producer{};
  std::string method{};
  std::string source_snapshot_hash{};
  std::string target{};
  std::string oracle{};
  contracts::Json environment = contracts::Json::object();
  std::string command_id{};
  std::string algorithm_id{};
  std::vector<std::string> claim_ids{};
  std::vector<std::string> requirement_ids{};
  std::optional<bool> success{};
  std::vector<std::string> limitations{};
  std::string independence_group{};
  bool simulated = false;
  std::string path{};
};

[[nodiscard]] contracts::Json to_json(const EvidenceRequirement &requirement);
[[nodiscard]] contracts::Json to_json(const EvidenceArtifact &artifact);
[[nodiscard]] contracts::Json to_json(const ConfidenceAssessment &assessment);
[[nodiscard]] EvidenceRequirement
evidence_requirement_from_json(const contracts::Json &value);
[[nodiscard]] EvidenceArtifact
evidence_artifact_from_json(const contracts::Json &value);

class EvidenceManager final {
public:
  explicit EvidenceManager(EgcfStore &store);

  [[nodiscard]] std::string add_requirement(
      const EvidenceRequirement &requirement);
  [[nodiscard]] std::string collect(EvidenceInput input);
  [[nodiscard]] std::vector<std::pair<std::string, EvidenceRequirement>>
  requirements(std::string_view subject_id);
  [[nodiscard]] std::vector<EvidenceArtifact>
  artifacts(std::string_view subject_id);
  [[nodiscard]] contracts::Json coverage(std::string_view subject_id);
  [[nodiscard]] contracts::Json uniqueness(std::string_view subject_id);
  [[nodiscard]] contracts::Json conflicts(std::string_view subject_id);
  [[nodiscard]] ConfidenceAssessment
  confidence(std::string_view subject_id,
             std::string policy = "egcf-default-v1");
  [[nodiscard]] contracts::Json graph(std::string_view subject_id);

private:
  EgcfStore &store_;
};

class Ieps final {
public:
  explicit Ieps(EvidenceManager &evidence);

  [[nodiscard]] std::string oracle(std::string subject_id, std::string name,
                                   std::string category, std::string oracle,
                                   bool mandatory = true,
                                   int freshness_seconds = 0,
                                   std::string independence_group = {});
  [[nodiscard]] contracts::Json qualify(std::string_view subject_id);
  [[nodiscard]] contracts::Json gate(std::string_view subject_id);
  [[nodiscard]] contracts::Json coverage(std::string_view subject_id);
  [[nodiscard]] contracts::Json uniqueness(std::string_view subject_id);
  [[nodiscard]] static contracts::Json
  counterexamples(const contracts::Json &candidates,
                  const contracts::Json &predicate_results);
  [[nodiscard]] static contracts::Json mutation(const contracts::Json &items);
  [[nodiscard]] static contracts::Json shrink(const contracts::Json &sequence,
                                              const contracts::Json &required);

private:
  EvidenceManager &evidence_;
};

} // namespace statewright::egcf
