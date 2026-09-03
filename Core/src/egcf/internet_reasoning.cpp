#include "statewright/egcf/internet_reasoning.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/contracts/typed_id.hpp"
#include "statewright/core/file_io.hpp"

#include <algorithm>
#include <exception>
#include <set>
#include <utility>

namespace statewright::egcf {
namespace {

[[noreturn]] void reasoning_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

std::vector<std::string> json_strings(const contracts::Json &value,
                                      std::string_view key) {
  const auto found = value.find(std::string(key));
  if (found == value.end()) {
    return {};
  }
  if (!found->is_array()) {
    reasoning_error("provider field must be an array: " + std::string(key));
  }
  std::set<std::string> unique;
  for (const auto &entry : *found) {
    if (!entry.is_string() || entry.get_ref<const std::string &>().empty()) {
      reasoning_error("provider array must contain non-empty strings: " +
                      std::string(key));
    }
    unique.insert(entry.get<std::string>());
  }
  return {unique.begin(), unique.end()};
}

contracts::Json fallback_interpretation(
    const InternetAlgorithmCandidate &candidate) {
  return {{"falsifiers",
           {"A counterexample produces output different from the translated IR.",
            "A required precondition is absent from the source."}},
          {"hypotheses",
           {"The source describes the translated candidate IR exactly.",
            "The source omits conditions that invalidate the translated IR."}},
          {"missing_evidence",
           {"independent reproduction", "counterexample search"}},
          {"unresolved_assumptions", candidate.unresolved_assumptions}};
}

std::string bounded_context(
    const std::vector<sources::InternetSourceFragment> &fragments) {
  if (fragments.empty() || fragments.size() > 16U) {
    reasoning_error("internet reasoning requires one to sixteen fragments");
  }
  std::string context;
  for (const auto &fragment : fragments) {
    if (fragment.text.size() > 1024U) {
      reasoning_error("internet reasoning fragment exceeds context bound");
    }
    if (!context.empty()) {
      context += "\n---\n";
    }
    context += fragment.fragment_kind + ": " + fragment.text;
  }
  if (context.size() > 16U * 1024U) {
    reasoning_error("internet reasoning context exceeds total bound");
  }
  return context;
}

std::string grammar_identity(EgcfStore &store) {
  const auto path = store.objects().schemas().resource_root() /
                    "grammars/providers/oiec_reasoning_response.gbnf";
  return "sha256:" + contracts::sha256_bytes(core::read_bytes(path));
}

} // namespace

InternetReasoningCoordinator::InternetReasoningCoordinator(EgcfStore &store)
    : store_(store), internet_(store), evidence_(store), ieps_(evidence_),
      proposals_(store, ieps_) {}

InternetReasoningResult InternetReasoningCoordinator::analyze(
    const InternetAlgorithmCandidate &candidate_value,
    const std::vector<sources::InternetSourceFragment> &fragments,
    providers::ReasoningProvider *provider, std::string provider_identity,
    std::string model_identity) {
  const auto candidate =
      canonical_internet_algorithm_candidate(candidate_value);
  const std::string candidate_id =
      internet_.register_algorithm_candidate(candidate);
  std::vector<std::string> fragment_ids;
  std::vector<std::string> snapshot_ids;
  for (const auto &fragment_value : fragments) {
    const auto fragment = sources::canonical_source_fragment(fragment_value);
    if (fragment.snapshot_id != candidate.snapshot_id) {
      reasoning_error("reasoning fragment does not belong to candidate snapshot");
    }
    static_cast<void>(internet_.register_source_fragment(fragment));
    fragment_ids.push_back(fragment.object_id());
    snapshot_ids.push_back(fragment.snapshot_id);
  }
  const contracts::Json request = {
      {"candidate_id", candidate_id},
      {"context", bounded_context(fragments)},
      {"objective",
       "Generate competing interpretations, contradictions, counterexamples, "
       "falsifiers, missing evidence, and unresolved assumptions."},
      {"output_contract",
       {{"falsifiers", "string[]"},
        {"hypotheses", "string[]"},
        {"missing_evidence", "string[]"},
        {"unresolved_assumptions", "string[]"}}},
      {"provider_authority", false},
      {"version", internet_reasoning_coordinator_version}};
  contracts::Json interpretation = fallback_interpretation(candidate);
  contracts::Json provider_envelope = {
      {"interpretation", interpretation}, {"status", "NOT_INVOKED"}};
  bool provider_available = false;
  std::string status = "DETERMINISTIC_FALLBACK";
  if (provider != nullptr) {
    try {
      providers::BoundedReasoningProvider bounded(*provider, 1, 4096, 0);
      provider_envelope = bounded.create_response(request);
      interpretation = providers::parse_reasoning_json_object(
          providers::response_text(provider_envelope));
      static_cast<void>(json_strings(interpretation, "hypotheses"));
      static_cast<void>(json_strings(interpretation, "falsifiers"));
      static_cast<void>(json_strings(interpretation, "missing_evidence"));
      static_cast<void>(json_strings(interpretation, "unresolved_assumptions"));
      provider_available = true;
      status = "PROVIDER_ADVISORY";
    } catch (const std::exception &error) {
      interpretation = fallback_interpretation(candidate);
      provider_envelope = {{"error", error.what()},
                           {"interpretation", interpretation},
                           {"status", "FAILED"}};
      status = "PROVIDER_FAILED_FALLBACK";
    }
  }
  const auto hypotheses = json_strings(interpretation, "hypotheses");
  const auto falsifiers = json_strings(interpretation, "falsifiers");
  const auto unresolved =
      json_strings(interpretation, "unresolved_assumptions");
  const auto proposal = proposals_.propose(
      {{"assumptions", unresolved},
       {"falsifiers", falsifiers},
       {"goal", "validate the internet algorithm candidate"},
       {"hypotheses", hypotheses},
       {"max_hypotheses", 8},
       {"text", request.at("context")}},
      contracts::parse_typed_id(candidate.snapshot_id).digest,
      {"internet-source-fragments"});

  InternetReasoningAnalysis analysis;
  analysis.candidate_id = candidate_id;
  analysis.snapshot_ids = std::move(snapshot_ids);
  analysis.source_fragment_ids = std::move(fragment_ids);
  analysis.request = request;
  analysis.request_signature = contracts::sha256_json(request);
  analysis.provider_identity = std::move(provider_identity);
  analysis.model_identity = std::move(model_identity);
  analysis.grammar_identity = grammar_identity(store_);
  analysis.parser_version = "statewright-internet-reasoning-parser-v1";
  analysis.provider_available = provider_available;
  analysis.provider_output_signature = contracts::sha256_json(provider_envelope);
  analysis.hypothesis_set = proposal.at("hypothesis_set");
  analysis.proposal_ids =
      proposal.at("claim_ids").get<std::vector<std::string>>();
  analysis.falsifier_ids =
      proposal.at("evidence_requirement_ids").get<std::vector<std::string>>();
  analysis.missing_evidence_ids = analysis.falsifier_ids;
  analysis.unresolved_assumptions = unresolved;
  analysis.authoritative = false;
  analysis.status = status;
  analysis = canonical_internet_reasoning_analysis(std::move(analysis));
  const std::string analysis_id = internet_.register_reasoning_analysis(analysis);

  InternetAlgorithmCandidate updated = candidate;
  updated.oiec_sr_proposal_ids = analysis.proposal_ids;
  updated.oiec_sr_falsifier_ids = analysis.falsifier_ids;
  updated.unresolved_assumptions.insert(updated.unresolved_assumptions.end(),
                                        unresolved.begin(), unresolved.end());
  updated.candidate_signature.clear();
  updated = canonical_internet_algorithm_candidate(std::move(updated));
  const std::string updated_id = internet_.supersede_algorithm_candidate(
      candidate_id, updated, "OIEC-SR advisory analysis attached");
  InternetReasoningResult result{
      .analysis = std::move(analysis),
      .updated_candidate = std::move(updated),
      .analysis_id = analysis_id,
      .updated_candidate_id = updated_id,
      .result_signature = {}};
  auto material = to_json(result);
  material.erase("result_signature");
  result.result_signature = contracts::sha256_json(material);
  return result;
}

contracts::Json to_json(const InternetReasoningResult &value) {
  return {{"analysis", to_json(value.analysis)},
          {"analysis_id", value.analysis_id},
          {"result_signature", value.result_signature},
          {"updated_candidate", to_json(value.updated_candidate)},
          {"updated_candidate_id", value.updated_candidate_id}};
}

} // namespace statewright::egcf
