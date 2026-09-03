#pragma once

#include "statewright/saa/unified_retrieval.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace statewright::saa {

inline constexpr std::string_view algorithm_transfer_version =
    "saa-algorithm-transfer-v1";

struct AlgorithmDomainContract final {
  std::string domain;
  std::vector<SemanticConcept> input_concepts;
  std::vector<std::string> invariants;
  std::vector<std::string> boundary_signatures;
  std::string dynamics_signature;
  std::vector<std::string> evidence_requirements;
  std::vector<std::string> qualification_evidence_signatures;
  std::string evidence_scope_signature;
};

struct AlgorithmTransferAssessment final {
  int schema_version = 1;
  std::string transfer_version = std::string(algorithm_transfer_version);
  std::string source_algorithm_id;
  std::string source_domain;
  std::string target_domain;
  bool semantic_contract_match = false;
  bool boundary_contract_match = false;
  bool invariant_contract_match = false;
  bool dynamics_contract_match = false;
  bool evidence_contract_match = false;
  std::string status;
  bool transfer_without_requalification = false;
  bool adaptation_required = false;
  std::vector<std::string> blocking_gaps;
  std::vector<std::string> adaptation_gaps;
  std::string assessment_signature;
};

[[nodiscard]] AlgorithmDomainContract
canonical_algorithm_domain_contract(AlgorithmDomainContract contract);
[[nodiscard]] AlgorithmTransferAssessment assess_algorithm_transfer(
    std::string source_algorithm_id, AlgorithmDomainContract source,
    AlgorithmDomainContract target,
    SemanticMeaningEquivalence *ontology = nullptr);

[[nodiscard]] contracts::Json to_json(const AlgorithmDomainContract &value);
[[nodiscard]] contracts::Json
to_json(const AlgorithmTransferAssessment &value);

} // namespace statewright::saa
