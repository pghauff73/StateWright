#include "statewright/saa/semantic_revision.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace statewright::saa {
namespace {

using Json = contracts::Json;

const std::vector<std::string> revision_subsystems = {
    "EON", "OURD", "IURM", "CFEL", "BD_DL", "HYPOTHESIS_STATE",
    "ALGORITHM_STORE"};

[[noreturn]] void revision_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument,
                      std::move(message));
}

[[nodiscard]] std::string trimmed(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n\f\v");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n\f\v");
  return value.substr(first, last - first + 1U);
}

[[nodiscard]] std::string collapsed(std::string value) {
  std::istringstream input(value);
  std::ostringstream output;
  std::string word;
  bool first = true;
  while (input >> word) {
    if (!first) {
      output << ' ';
    }
    output << word;
    first = false;
  }
  return output.str();
}

[[nodiscard]] std::string normalized_text(std::string value) {
  value = collapsed(std::move(value));
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

[[nodiscard]] std::string uppercase(std::string value) {
  value = trimmed(std::move(value));
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::toupper(character));
                 });
  return value;
}

[[nodiscard]] Json exponent_json(const PhysicalDimensionVector &value) {
  return std::vector<int>(value.exponents.begin(), value.exponents.end());
}

[[nodiscard]] std::vector<std::string>
normalized_ids(std::vector<std::string> values) {
  std::set<std::string> normalized;
  for (auto &value : values) {
    value = trimmed(std::move(value));
    if (!value.empty()) {
      normalized.insert(std::move(value));
    }
  }
  return {normalized.begin(), normalized.end()};
}

[[nodiscard]] std::vector<std::string>
normalized_texts(std::vector<std::string> values) {
  std::set<std::string> normalized;
  for (auto &value : values) {
    value = normalized_text(std::move(value));
    if (!value.empty()) {
      normalized.insert(std::move(value));
    }
  }
  return {normalized.begin(), normalized.end()};
}

void require_grounded_evidence(const SemanticEvidenceResolver &resolver,
                               std::string_view evidence_id) {
  std::optional<SemanticGroundingEvidence> evidence;
  try {
    evidence = resolver(evidence_id);
  } catch (const std::exception &) {
    evidence = std::nullopt;
  }
  if (!evidence) {
    revision_error("semantic requalification evidence is not registered: " +
                   std::string(evidence_id));
  }
  if (evidence->object_type != "egcf-evidence") {
    revision_error(
        "semantic requalification evidence ID does not reference EvidenceArtifact");
  }
  if (evidence->success != true || evidence->simulated) {
    revision_error(
        "semantic requalification evidence must be successful and non-simulated");
  }
  if (!evidence->producer.starts_with("deterministic-") &&
      !evidence->producer.starts_with("human-")) {
    revision_error(
        "semantic requalification evidence requires deterministic or human producer");
  }
  if (evidence->method == "reported" || evidence->method == "model-claimed" ||
      evidence->method == "model-generated-claim") {
    revision_error(
        "reported/model-claimed evidence cannot requalify semantics");
  }
}

} // namespace

SemanticRevisionFalsifierResult::SemanticRevisionFalsifierResult(
    std::string falsifier_value, std::string outcome_value,
    std::string evidence_id_value)
    : falsifier(normalized_text(std::move(falsifier_value))),
      outcome(uppercase(std::move(outcome_value))),
      evidence_id(trimmed(std::move(evidence_id_value))) {}

SemanticContradiction detect_semantic_contradiction(
    const SemanticConcept &concept_value, std::string observed_statement,
    std::vector<std::string> evidence_ids, std::string observed_meaning,
    std::string observed_quantity_kind,
    std::optional<PhysicalDimensionVector> observed_dimension,
    std::optional<PhysicalUnit> observed_unit, int severity_bp) {
  observed_statement = collapsed(std::move(observed_statement));
  if (observed_statement.empty()) {
    revision_error("semantic contradiction requires observed statement");
  }
  evidence_ids = normalized_ids(std::move(evidence_ids));
  if (evidence_ids.empty()) {
    revision_error("semantic contradiction requires evidence references");
  }
  severity_bp = std::clamp(severity_bp, 0, 10000);
  if (!observed_dimension && observed_unit) {
    observed_dimension = observed_unit->dimension;
  }
  observed_meaning = normalized_text(std::move(observed_meaning));
  observed_quantity_kind =
      normalized_text(std::move(observed_quantity_kind));

  std::vector<std::string> kinds;
  if (!observed_meaning.empty() &&
      observed_meaning != concept_value.meaning) {
    kinds.emplace_back("MEANING_CONTRADICTION");
  }
  if (!observed_quantity_kind.empty() &&
      observed_quantity_kind != concept_value.quantity_kind) {
    kinds.emplace_back("QUANTITY_KIND_CONTRADICTION");
  }
  if (observed_dimension && concept_value.physical_dimension &&
      observed_dimension != concept_value.physical_dimension) {
    kinds.emplace_back("DIMENSION_CONTRADICTION");
  }
  if (observed_unit && concept_value.canonical_unit &&
      observed_unit->dimension != concept_value.canonical_unit->dimension) {
    kinds.emplace_back("UNIT_DIMENSION_CONTRADICTION");
  }
  if (kinds.empty()) {
    kinds.emplace_back("EVIDENCE_CONTRADICTS_DECLARED_SEMANTICS");
  }
  std::ostringstream kind_stream;
  for (std::size_t index = 0; index < kinds.size(); ++index) {
    if (index != 0U) {
      kind_stream << '+';
    }
    kind_stream << kinds[index];
  }
  const std::string kind = kind_stream.str();
  const Json payload = {
      {"concept_signature", concept_value.concept_signature},
      {"contradiction_kind", kind},
      {"evidence_ids", evidence_ids},
      {"observed_dimension",
       observed_dimension ? exponent_json(*observed_dimension) : Json(nullptr)},
      {"observed_meaning", observed_meaning},
      {"observed_quantity_kind", observed_quantity_kind},
      {"observed_statement", observed_statement},
      {"observed_unit_signature",
       observed_unit ? Json(observed_unit->signature()) : Json(nullptr)},
      {"severity_bp", severity_bp},
      {"version", semantic_revision_version}};
  const std::string signature = contracts::sha256_json(payload);
  return {.contradiction_id =
              "semantic-contradiction:" + signature.substr(0, 24),
          .concept_signature = concept_value.concept_signature,
          .contradiction_kind = kind,
          .observed_statement = std::move(observed_statement),
          .observed_meaning = std::move(observed_meaning),
          .observed_quantity_kind = std::move(observed_quantity_kind),
          .observed_dimension = std::move(observed_dimension),
          .observed_unit_symbol =
              observed_unit ? observed_unit->canonical_symbol() : "",
          .evidence_ids = std::move(evidence_ids),
          .severity_bp = severity_bp,
          .status = "SEMANTIC_CONTRADICTION_OPEN",
          .contradiction_signature = signature};
}

SemanticRevisionProposal propose_semantic_revision(
    const SemanticConcept &concept_value,
    const std::vector<SemanticContradiction> &contradictions,
    std::string meaning, std::optional<std::string> quantity_kind,
    std::optional<std::string> name, std::optional<std::string> domain,
    std::optional<std::vector<std::string>> aliases,
    std::optional<PhysicalDimensionVector> physical_dimension,
    std::optional<PhysicalUnit> canonical_unit,
    std::vector<std::string> assumptions,
    std::vector<std::string> falsifiers) {
  if (contradictions.empty()) {
    revision_error(
        "SAA-9.2 semantic revision requires at least one contradiction");
  }
  if (std::any_of(contradictions.begin(), contradictions.end(),
                  [&](const auto &item) {
                    return item.concept_signature !=
                           concept_value.concept_signature;
                  })) {
    revision_error(
        "semantic revision contradictions target different concepts");
  }
  std::optional<PhysicalDimensionVector> dimension = physical_dimension;
  if (!dimension) {
    dimension = canonical_unit ? std::optional(canonical_unit->dimension)
                               : concept_value.physical_dimension;
  }
  if (canonical_unit && dimension != canonical_unit->dimension) {
    revision_error("proposed semantic unit contradicts proposed dimension");
  }
  std::string proposed_name =
      normalized_text(name.value_or(concept_value.canonical_name));
  std::string proposed_meaning = normalized_text(std::move(meaning));
  std::string proposed_domain =
      normalized_text(domain.value_or(concept_value.domain));
  std::string proposed_quantity =
      normalized_text(quantity_kind.value_or(concept_value.quantity_kind));
  auto proposed_aliases = normalized_texts(
      aliases.value_or(concept_value.aliases));
  if (proposed_meaning.empty()) {
    revision_error("semantic revision requires proposed meaning");
  }
  std::set<std::string> contradiction_set;
  for (const auto &contradiction : contradictions) {
    contradiction_set.insert(contradiction.contradiction_signature);
  }
  std::vector<std::string> contradiction_signatures(
      contradiction_set.begin(), contradiction_set.end());
  assumptions = normalized_texts(std::move(assumptions));
  falsifiers = normalized_texts(std::move(falsifiers));
  const std::optional<PhysicalUnit> retained_unit =
      canonical_unit ? canonical_unit : concept_value.canonical_unit;
  const Json payload = {
      {"aliases", proposed_aliases},
      {"assumptions", assumptions},
      {"contradictions", contradiction_signatures},
      {"dimension", dimension ? exponent_json(*dimension) : Json(nullptr)},
      {"domain", proposed_domain},
      {"falsifiers", falsifiers},
      {"meaning", proposed_meaning},
      {"name", proposed_name},
      {"quantity_kind", proposed_quantity},
      {"source_concept_signature", concept_value.concept_signature},
      {"unit_signature",
       retained_unit ? Json(retained_unit->signature()) : Json(nullptr)},
      {"version", semantic_revision_version}};
  const std::string signature = contracts::sha256_json(payload);
  return {.proposal_id = "semantic-revision:" + signature.substr(0, 24),
          .source_concept_signature = concept_value.concept_signature,
          .contradiction_signatures = std::move(contradiction_signatures),
          .proposed_name = std::move(proposed_name),
          .proposed_meaning = std::move(proposed_meaning),
          .proposed_domain = std::move(proposed_domain),
          .proposed_quantity_kind = std::move(proposed_quantity),
          .proposed_aliases = std::move(proposed_aliases),
          .proposed_dimension = std::move(dimension),
          .proposed_unit_symbol =
              retained_unit ? retained_unit->canonical_symbol() : "",
          .assumptions = std::move(assumptions),
          .falsifiers = std::move(falsifiers),
          .epistemic_status = "MODEL_PROPOSED_SEMANTIC_REVISION",
          .proposal_signature = signature};
}

SemanticRequalification requalify_semantic_revision(
    const SemanticEvidenceResolver &evidence_resolver,
    const SemanticConcept &source, const SemanticRevisionProposal &proposal,
    std::vector<std::string> evidence_ids,
    std::vector<SemanticRevisionFalsifierResult> falsifier_results,
    bool independent_review) {
  if (proposal.source_concept_signature != source.concept_signature) {
    revision_error(
        "semantic revision proposal targets a different source concept");
  }
  evidence_ids = normalized_ids(std::move(evidence_ids));
  std::vector<std::string> blockers;
  if (evidence_ids.empty()) {
    blockers.emplace_back("no grounded semantic requalification evidence");
  } else {
    for (const auto &evidence_id : evidence_ids) {
      try {
        require_grounded_evidence(evidence_resolver, evidence_id);
      } catch (const common::Error &error) {
        blockers.emplace_back(error.what());
      }
    }
  }

  std::map<std::string, SemanticRevisionFalsifierResult> by_falsifier;
  for (auto &result : falsifier_results) {
    result.falsifier = normalized_text(std::move(result.falsifier));
    result.outcome = uppercase(std::move(result.outcome));
    result.evidence_id = trimmed(std::move(result.evidence_id));
    by_falsifier.insert_or_assign(result.falsifier, std::move(result));
  }
  std::vector<SemanticRevisionFalsifierResult> normalized_results;
  for (const auto &falsifier : proposal.falsifiers) {
    const auto found = by_falsifier.find(falsifier);
    if (found == by_falsifier.end()) {
      blockers.push_back("missing falsifier result: " + falsifier);
      continue;
    }
    const auto &result = found->second;
    if (result.outcome != "SURVIVED") {
      blockers.push_back(
          "semantic revision falsifier did not survive: " + falsifier);
    }
    if (!result.evidence_id.empty()) {
      try {
        require_grounded_evidence(evidence_resolver, result.evidence_id);
      } catch (const common::Error &error) {
        blockers.emplace_back(error.what());
      }
    }
    normalized_results.push_back(result);
  }
  if (!independent_review) {
    blockers.emplace_back("independent semantic review missing");
  }

  std::optional<PhysicalUnit> unit;
  if (!proposal.proposed_unit_symbol.empty()) {
    unit = physical_unit(proposal.proposed_unit_symbol);
  }
  std::optional<SemanticConcept> replacement;
  if (blockers.empty()) {
    replacement = make_semantic_concept(
        proposal.proposed_name, proposal.proposed_meaning,
        proposal.proposed_domain, proposal.proposed_quantity_kind,
        proposal.proposed_aliases, proposal.proposed_dimension, unit,
        evidence_ids, "SEMANTICALLY_RESOLVED");
  }
  const std::string status = replacement ? "SEMANTIC_REQUALIFIED"
                                         : "SEMANTIC_REQUALIFICATION_BLOCKED";
  const bool replacement_eligible = replacement.has_value();
  Json result_payload = Json::array();
  for (const auto &result : normalized_results) {
    result_payload.push_back(to_json(result));
  }
  const Json payload = {
      {"blocking_reasons", blockers},
      {"contradictions", proposal.contradiction_signatures},
      {"evidence_ids", evidence_ids},
      {"falsifiers", result_payload},
      {"independent_review", independent_review},
      {"proposal", proposal.proposal_signature},
      {"replacement",
       replacement ? Json(replacement->concept_signature) : Json(nullptr)},
      {"source", source.concept_signature},
      {"status", status},
      {"version", semantic_revision_version}};
  return {.source_concept_signature = source.concept_signature,
          .replacement_concept = std::move(replacement),
          .proposal_signature = proposal.proposal_signature,
          .contradiction_signatures = proposal.contradiction_signatures,
          .evidence_ids = std::move(evidence_ids),
          .falsifier_results = std::move(normalized_results),
          .independent_review = independent_review,
          .status = status,
          .canonical_replacement_eligible = replacement_eligible,
          .requalification_signature = contracts::sha256_json(payload),
          .blocking_reasons = std::move(blockers)};
}

std::vector<SemanticRevisionDirective> propagate_semantic_contradiction(
    const SemanticContradiction &contradiction) {
  static const std::map<std::string, std::string> actions = {
      {"EON", "SURFACE_SEMANTIC_CONTRADICTION"},
      {"OURD", "CREATE_SEMANTIC_REVISION_OBJECTIVE"},
      {"IURM", "BLOCK_CONTRADICTED_MEANING_AS_INDEPENDENT_DIMENSION"},
      {"CFEL", "REGISTER_SEMANTIC_COLLISION"},
      {"BD_DL", "REASSESS_MEANING_DOMAIN_AND_BOUNDS"},
      {"HYPOTHESIS_STATE", "STORE_REVISED_MEANING_AS_UNVERIFIED_HYPOTHESIS"},
      {"ALGORITHM_STORE",
       "SUSPEND_DEPENDENT_CANONICAL_REUSE_PENDING_REQUALIFICATION"}};
  std::vector<SemanticRevisionDirective> directives;
  for (const auto &subsystem : revision_subsystems) {
    directives.push_back(
        {.subsystem = subsystem,
         .action = actions.at(subsystem),
         .blocking = subsystem == "IURM" || subsystem == "ALGORITHM_STORE",
         .contradiction_signature = contradiction.contradiction_signature,
         .rationale =
             "Contradicted semantics cannot remain canonical merely because prior knowledge used them. Revision requires evidence-backed requalification."});
  }
  return directives;
}

Json to_json(const SemanticContradiction &value) {
  return {{"concept_signature", value.concept_signature},
          {"contradiction_id", value.contradiction_id},
          {"contradiction_kind", value.contradiction_kind},
          {"contradiction_signature", value.contradiction_signature},
          {"evidence_ids", value.evidence_ids},
          {"observed_dimension",
           value.observed_dimension ? to_json(*value.observed_dimension)
                                    : Json(nullptr)},
          {"observed_meaning", value.observed_meaning},
          {"observed_quantity_kind", value.observed_quantity_kind},
          {"observed_statement", value.observed_statement},
          {"observed_unit_symbol", value.observed_unit_symbol},
          {"severity_bp", value.severity_bp},
          {"status", value.status}};
}

Json to_json(const SemanticRevisionProposal &value) {
  return {{"assumptions", value.assumptions},
          {"contradiction_signatures", value.contradiction_signatures},
          {"epistemic_status", value.epistemic_status},
          {"falsifiers", value.falsifiers},
          {"proposal_id", value.proposal_id},
          {"proposal_signature", value.proposal_signature},
          {"proposed_aliases", value.proposed_aliases},
          {"proposed_dimension",
           value.proposed_dimension ? to_json(*value.proposed_dimension)
                                    : Json(nullptr)},
          {"proposed_domain", value.proposed_domain},
          {"proposed_meaning", value.proposed_meaning},
          {"proposed_name", value.proposed_name},
          {"proposed_quantity_kind", value.proposed_quantity_kind},
          {"proposed_unit_symbol", value.proposed_unit_symbol},
          {"source_concept_signature", value.source_concept_signature}};
}

Json to_json(const SemanticRevisionFalsifierResult &value) {
  return {{"evidence_id", trimmed(value.evidence_id)},
          {"falsifier", normalized_text(value.falsifier)},
          {"outcome", uppercase(value.outcome)}};
}

Json to_json(const SemanticRequalification &value) {
  Json falsifiers = Json::array();
  for (const auto &result : value.falsifier_results) {
    falsifiers.push_back(to_json(result));
  }
  return {{"blocking_reasons", value.blocking_reasons},
          {"canonical_replacement_eligible",
           value.canonical_replacement_eligible},
          {"contradiction_signatures", value.contradiction_signatures},
          {"evidence_ids", value.evidence_ids},
          {"falsifier_results", falsifiers},
          {"independent_review", value.independent_review},
          {"proposal_signature", value.proposal_signature},
          {"replacement_concept",
           value.replacement_concept ? to_json(*value.replacement_concept)
                                     : Json(nullptr)},
          {"requalification_signature", value.requalification_signature},
          {"source_concept_signature", value.source_concept_signature},
          {"status", value.status}};
}

Json to_json(const SemanticRevisionDirective &value) {
  return {{"action", value.action},
          {"blocking", value.blocking},
          {"contradiction_signature", value.contradiction_signature},
          {"rationale", value.rationale},
          {"subsystem", value.subsystem}};
}

} // namespace statewright::saa
