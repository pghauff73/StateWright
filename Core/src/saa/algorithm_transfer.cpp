#include "statewright/saa/algorithm_transfer.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <set>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

[[noreturn]] void transfer_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

[[nodiscard]] std::string canonical_text(std::string value) {
  std::string result;
  bool pending_space = false;
  for (const char character : value) {
    if (std::isspace(static_cast<unsigned char>(character)) != 0) {
      pending_space = !result.empty();
      continue;
    }
    if (pending_space) {
      result.push_back(' ');
      pending_space = false;
    }
    result.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }
  return result;
}

[[nodiscard]] std::vector<std::string>
canonical_texts(std::vector<std::string> values) {
  std::set<std::string> result;
  for (auto &value : values) {
    value = canonical_text(std::move(value));
    if (!value.empty()) {
      result.insert(std::move(value));
    }
  }
  return {result.begin(), result.end()};
}

[[nodiscard]] std::string canonical_sha(std::string value,
                                        std::string_view label) {
  value = canonical_text(std::move(value));
  const bool valid =
      value.size() == 64U &&
      std::ranges::all_of(value, [](const char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
      });
  if (!valid) {
    transfer_error(std::string(label) + " must be SHA-256");
  }
  return value;
}

[[nodiscard]] bool concept_equivalent(const SemanticConcept &left,
                                      const SemanticConcept &right,
                                      SemanticMeaningEquivalence *ontology) {
  if (left.concept_signature == right.concept_signature) {
    return true;
  }
  if (ontology == nullptr) {
    return false;
  }
  std::vector<std::string> left_terms = {left.canonical_name, left.meaning};
  left_terms.insert(left_terms.end(), left.aliases.begin(), left.aliases.end());
  std::vector<std::string> right_terms = {right.canonical_name, right.meaning};
  right_terms.insert(right_terms.end(), right.aliases.begin(),
                     right.aliases.end());
  for (const auto &left_term : left_terms) {
    for (const auto &right_term : right_terms) {
      try {
        if (ontology->meanings_equivalent(left_term, right_term)) {
          return true;
        }
      } catch (...) {
      }
    }
  }
  return false;
}

[[nodiscard]] bool subset(const std::vector<std::string> &required,
                          const std::vector<std::string> &available) {
  return std::ranges::all_of(required, [&](const auto &value) {
    return std::ranges::binary_search(available, value);
  });
}

} // namespace

AlgorithmDomainContract
canonical_algorithm_domain_contract(AlgorithmDomainContract contract) {
  std::ranges::sort(contract.input_concepts, {},
                    &SemanticConcept::concept_signature);
  if (std::ranges::any_of(contract.input_concepts,
                          [](const auto &semantic_concept) {
                            return !semantic_concept.canonical_eligible;
                          })) {
    transfer_error(
        "SAA-10.2 requires canonically resolved transfer concepts");
  }
  contract.domain = canonical_text(std::move(contract.domain));
  contract.invariants = canonical_texts(std::move(contract.invariants));
  for (auto &signature : contract.boundary_signatures) {
    signature =
        canonical_sha(std::move(signature), "SAA-10.2 boundary signature");
  }
  std::ranges::sort(contract.boundary_signatures);
  contract.dynamics_signature = canonical_sha(
      std::move(contract.dynamics_signature), "SAA-10.2 dynamics signature");
  contract.evidence_requirements =
      canonical_texts(std::move(contract.evidence_requirements));
  for (auto &signature : contract.qualification_evidence_signatures) {
    signature = canonical_sha(std::move(signature),
                              "SAA-10.2 qualification evidence signature");
  }
  std::ranges::sort(contract.qualification_evidence_signatures);
  if (!contract.evidence_scope_signature.empty()) {
    contract.evidence_scope_signature = canonical_sha(
        std::move(contract.evidence_scope_signature),
        "SAA-10.2 evidence scope signature");
  }
  return contract;
}

AlgorithmTransferAssessment assess_algorithm_transfer(
    std::string source_algorithm_id, AlgorithmDomainContract source,
    AlgorithmDomainContract target, SemanticMeaningEquivalence *ontology) {
  const auto src =
      canonical_algorithm_domain_contract(std::move(source));
  const auto dst =
      canonical_algorithm_domain_contract(std::move(target));
  const bool semantic_match =
      src.input_concepts.size() == dst.input_concepts.size() &&
      std::ranges::all_of(src.input_concepts, [&](const auto &left) {
        return std::ranges::any_of(dst.input_concepts, [&](const auto &right) {
          return concept_equivalent(left, right, ontology);
        });
      });
  const bool boundary_match =
      src.boundary_signatures == dst.boundary_signatures;
  const bool invariant_match = subset(src.invariants, dst.invariants);
  const bool dynamics_match =
      src.dynamics_signature == dst.dynamics_signature;
  const bool evidence_requirements_match =
      subset(src.evidence_requirements, dst.evidence_requirements);
  const bool evidence_signatures_match =
      !src.qualification_evidence_signatures.empty() &&
      subset(src.qualification_evidence_signatures,
             dst.qualification_evidence_signatures);
  const bool evidence_scope_match =
      !src.evidence_scope_signature.empty() &&
      src.evidence_scope_signature == dst.evidence_scope_signature;
  const bool evidence_match = evidence_requirements_match &&
                              evidence_signatures_match &&
                              evidence_scope_match;

  std::vector<std::string> blockers;
  std::vector<std::string> adaptation;
  if (!semantic_match) {
    blockers.push_back("semantic contracts are not exactly equivalent");
  }
  if (!boundary_match) {
    adaptation.push_back("BOUNDARY_CONTRACT");
  }
  if (!invariant_match) {
    adaptation.push_back("INVARIANT_CONTRACT");
  }
  if (!dynamics_match) {
    adaptation.push_back("DYNAMICS_CONTRACT");
  }
  if (!evidence_match) {
    adaptation.push_back("EVIDENCE_CONTRACT");
  }
  const bool exact = semantic_match && boundary_match && invariant_match &&
                     dynamics_match && evidence_match;
  std::string status;
  if (exact) {
    status = "EXACT_TRANSFER_CONTRACT_MATCH";
  } else if (!blockers.empty()) {
    status = "TRANSFER_BLOCKED_SEMANTIC_MISMATCH";
  } else {
    status = "TRANSFER_REQUIRES_DOMAIN_REQUALIFICATION";
  }
  const Json payload =
      {{"adaptation_gaps", adaptation},
       {"blocking_gaps", blockers},
       {"boundary_match", boundary_match},
       {"dynamics_match", dynamics_match},
       {"evidence_requirements_match", evidence_requirements_match},
       {"evidence_scope_match", evidence_scope_match},
       {"evidence_signatures_match", evidence_signatures_match},
       {"invariant_match", invariant_match},
       {"semantic_match", semantic_match},
       {"source", to_json(src)},
       {"source_algorithm_id", source_algorithm_id},
       {"status", status},
       {"target", to_json(dst)},
       {"version", algorithm_transfer_version}};
  return {.schema_version = 1,
          .transfer_version = std::string(algorithm_transfer_version),
          .source_algorithm_id = std::move(source_algorithm_id),
          .source_domain = src.domain,
          .target_domain = dst.domain,
          .semantic_contract_match = semantic_match,
          .boundary_contract_match = boundary_match,
          .invariant_contract_match = invariant_match,
          .dynamics_contract_match = dynamics_match,
          .evidence_contract_match = evidence_match,
          .status = std::move(status),
          .transfer_without_requalification = exact,
          .adaptation_required = !adaptation.empty(),
          .blocking_gaps = std::move(blockers),
          .adaptation_gaps = std::move(adaptation),
          .assessment_signature = contracts::sha256_json(payload)};
}

Json to_json(const AlgorithmDomainContract &value) {
  std::vector<std::string> concept_signatures;
  for (const auto &semantic_concept : value.input_concepts) {
    concept_signatures.push_back(semantic_concept.concept_signature);
  }
  return {{"boundary_signatures", value.boundary_signatures},
          {"domain", value.domain},
          {"dynamics_signature", value.dynamics_signature},
          {"evidence_requirements", value.evidence_requirements},
          {"evidence_scope_signature", value.evidence_scope_signature},
          {"input_concept_signatures", concept_signatures},
          {"invariants", value.invariants},
          {"qualification_evidence_signatures",
           value.qualification_evidence_signatures}};
}

Json to_json(const AlgorithmTransferAssessment &value) {
  return {{"adaptation_gaps", value.adaptation_gaps},
          {"adaptation_required", value.adaptation_required},
          {"assessment_signature", value.assessment_signature},
          {"blocking_gaps", value.blocking_gaps},
          {"boundary_contract_match", value.boundary_contract_match},
          {"dynamics_contract_match", value.dynamics_contract_match},
          {"evidence_contract_match", value.evidence_contract_match},
          {"invariant_contract_match", value.invariant_contract_match},
          {"schema_version", value.schema_version},
          {"semantic_contract_match", value.semantic_contract_match},
          {"source_algorithm_id", value.source_algorithm_id},
          {"source_domain", value.source_domain},
          {"status", value.status},
          {"target_domain", value.target_domain},
          {"transfer_version", value.transfer_version},
          {"transfer_without_requalification",
           value.transfer_without_requalification}};
}

} // namespace statewright::saa
