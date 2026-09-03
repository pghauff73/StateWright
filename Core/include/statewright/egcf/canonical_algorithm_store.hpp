#pragma once

#include "statewright/egcf/store.hpp"
#include "statewright/saa/representative_form.hpp"
#include "statewright/saa/unified_retrieval.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::egcf {

inline constexpr std::string_view canonical_algorithm_store_version =
    "saa-canonical-algorithm-store-v1";
inline constexpr int canonical_algorithm_store_schema_version = 1;

struct CanonicalLookupResult final {
  std::string status;
  std::vector<std::string> exact_equivalent_ids;
  std::vector<std::string> mathematical_match_ids;
  std::vector<std::string> semantic_match_ids;
  std::vector<std::string> source_bound_match_ids;

  [[nodiscard]] bool unique() const noexcept {
    return exact_equivalent_ids.empty();
  }
};

struct CanonicalAdmissionResult final {
  std::string status;
  std::string canonical_id;
  std::string source_id;
  int store_generation = 0;
  CanonicalLookupResult lookup;
  std::vector<std::string> relation_ids;

  [[nodiscard]] bool admitted_new() const noexcept {
    return status == "ADMITTED_NEW_CANONICAL";
  }
};

struct AlgorithmRelationRecord final {
  std::string relation_id;
  std::string relation_type;
  std::string source_ref;
  std::string source_kind;
  std::string target_ref;
  std::string target_kind;
  std::string basis;
  std::string basis_signature;
  std::vector<std::string> evidence_ids;
  int store_generation = 0;
  std::string created_at;
};

struct CanonicalAlgorithmQuery final {
  std::optional<std::string> representative_behavior_signature;
  std::optional<std::string> mathematical_signature;
  std::optional<std::string> semantic_signature;
  std::optional<std::string> source_structural_hash;
  std::optional<std::string> domain;
  std::optional<int> output_count;
  std::optional<int> input_count;
  std::vector<std::string> semantic_meanings;
  std::vector<std::string> lexical_terms;
  std::size_t limit = 10U;
};

struct CanonicalAlgorithmSearchResult final {
  int schema_version = 1;
  std::string search_version = "saa-canonical-store-search-v1";
  std::string query_signature;
  contracts::Json candidates = contracts::Json::array();
  contracts::Json excluded = contracts::Json::array();
  std::optional<std::string> selected_canonical_id;
  std::string status;
  std::string result_signature;
};

class CanonicalAlgorithmStore final : public saa::MathematicalAlgorithmCatalog {
public:
  explicit CanonicalAlgorithmStore(EgcfStore &egcf_store);

  [[nodiscard]] const std::filesystem::path &root() const noexcept;
  [[nodiscard]] CanonicalLookupResult
  lookup(const saa::CanonicalRepresentativeAlgorithmForm &form);
  [[nodiscard]] CanonicalAdmissionResult admit(
      const saa::CanonicalRepresentativeAlgorithmForm &form,
      const std::vector<saa::SemanticRepresentationIssue> &semantic_issues = {},
      const std::vector<saa::SemanticCandidateMeaning> &semantic_candidates = {},
      const std::vector<saa::SemanticResolution> &semantic_resolutions = {});
  [[nodiscard]] int current_generation();
  [[nodiscard]] contracts::Json get(std::string_view canonical_id);
  [[nodiscard]] std::vector<contracts::Json> list();
  [[nodiscard]] std::vector<contracts::Json>
  list_mathematical_algorithms() override;
  [[nodiscard]] std::vector<contracts::Json>
  sources(std::optional<std::string> canonical_id = std::nullopt);
  [[nodiscard]] std::string add_relation(
      std::string source_id, std::string target_id, std::string relation_type,
      std::string basis, std::vector<std::string> evidence_ids);
  [[nodiscard]] std::vector<AlgorithmRelationRecord> relations(
      std::optional<std::string> reference = std::nullopt,
      std::optional<std::string> relation_type = std::nullopt);
  [[nodiscard]] std::vector<contracts::Json>
  neighbors(std::string_view canonical_id);
  [[nodiscard]] CanonicalAlgorithmSearchResult
  search(CanonicalAlgorithmQuery query);
  [[nodiscard]] std::vector<std::string>
  search_text(std::string_view query, std::size_t limit = 10U);

  void rebuild_projection();

private:
  void ensure_projection();
  void verify_form(const saa::CanonicalRepresentativeAlgorithmForm &form) const;
  [[nodiscard]] std::string verify_semantic_proof(
      const saa::CanonicalRepresentativeAlgorithmForm &form,
      const std::vector<saa::SemanticRepresentationIssue> &issues,
      const std::vector<saa::SemanticCandidateMeaning> &candidates,
      const std::vector<saa::SemanticResolution> &resolutions) const;
  [[nodiscard]] contracts::Json canonical_payload(
      const saa::CanonicalRepresentativeAlgorithmForm &form) const;
  [[nodiscard]] std::filesystem::path
  algorithm_path(std::string_view canonical_id) const;
  [[nodiscard]] std::filesystem::path typed_path(
      const std::filesystem::path &object_root, std::string_view object_id,
      std::string_view expected_kind) const;
  [[nodiscard]] int generation_for(std::string_view canonical_id);
  [[nodiscard]] std::string persist_source(
      const saa::CanonicalRepresentativeAlgorithmForm &form,
      std::string_view canonical_id, std::string_view proof_signature,
      int generation, std::string_view created_at);
  void persist_canonical(const saa::CanonicalRepresentativeAlgorithmForm &form,
                         std::string_view canonical_id,
                         std::string_view source_id, int generation,
                         std::string_view created_at);
  [[nodiscard]] AlgorithmRelationRecord make_relation(
      std::string relation_type, std::string source_ref,
      std::string source_kind, std::string target_ref,
      std::string target_kind, std::string basis,
      std::string basis_signature, std::vector<std::string> evidence_ids,
      int generation) const;
  [[nodiscard]] std::string
  persist_relation(const AlgorithmRelationRecord &relation);
  void require_grounded_evidence(std::string_view evidence_id) const;

  EgcfStore &egcf_store_;
  std::filesystem::path state_root_;
  std::filesystem::path root_;
  std::filesystem::path algorithm_root_;
  std::filesystem::path source_root_;
  std::filesystem::path relation_root_;
  std::filesystem::path projection_path_;
};

[[nodiscard]] contracts::Json to_json(const CanonicalLookupResult &value);
[[nodiscard]] contracts::Json to_json(const CanonicalAdmissionResult &value);
[[nodiscard]] contracts::Json to_json(const AlgorithmRelationRecord &value);
[[nodiscard]] contracts::Json to_json(const CanonicalAlgorithmQuery &value);
[[nodiscard]] contracts::Json
to_json(const CanonicalAlgorithmSearchResult &value);

} // namespace statewright::egcf
