#pragma once

#include "statewright/egcf/store.hpp"
#include "statewright/saa/semantic_alignment.hpp"
#include "statewright/saa/semantic_revision.hpp"
#include "statewright/saa/unified_retrieval.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::egcf {

inline constexpr std::string_view semantic_ontology_version =
    "saa-semantic-ontology-v1";
inline constexpr int semantic_ontology_schema_version = 1;

class SemanticOntologyStore final : public saa::SemanticMeaningEquivalence {
public:
  explicit SemanticOntologyStore(EgcfStore &egcf_store);

  [[nodiscard]] const std::filesystem::path &root() const noexcept;
  [[nodiscard]] saa::SemanticEvidenceResolver evidence_resolver() const;

  [[nodiscard]] std::string
  admit_concept(const saa::SemanticConcept &semantic_concept);
  [[nodiscard]] saa::SemanticConcept
  load_concept(std::string_view concept_id) const;
  [[nodiscard]] std::vector<contracts::Json> concepts();
  [[nodiscard]] std::vector<std::string>
  resolve_text(std::string_view text);

  [[nodiscard]] std::string
  admit_alignment(const saa::SemanticAlignmentAssessment &assessment);
  [[nodiscard]] std::vector<contracts::Json> alignments();

  [[nodiscard]] std::string
  admit_revision(const saa::SemanticRequalification &requalification);
  [[nodiscard]] std::vector<contracts::Json> revisions();

  [[nodiscard]] std::vector<std::string>
  equivalent_concept_ids(std::string_view concept_id);
  [[nodiscard]] bool meanings_equivalent(std::string_view left,
                                         std::string_view right) override;

  void rebuild_projection();

private:
  void ensure_projection();
  void verify_concept_evidence(
      const saa::SemanticConcept &semantic_concept) const;
  [[nodiscard]] std::filesystem::path path_for(
      const std::filesystem::path &root, std::string_view object_id,
      std::string_view expected_kind) const;

  EgcfStore &egcf_store_;
  std::filesystem::path state_root_;
  std::filesystem::path root_;
  std::filesystem::path concept_root_;
  std::filesystem::path alignment_root_;
  std::filesystem::path revision_root_;
  std::filesystem::path projection_path_;
};

} // namespace statewright::egcf
