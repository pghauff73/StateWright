#include "statewright/egcf/reasoning_service.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/reasoning/certification.hpp"
#include "statewright/reasoning/hypotheses.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace statewright::egcf {
namespace {

using Json = contracts::Json;

[[noreturn]] void reasoning_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument,
                      "OIEC-SR proposal service: " + std::move(message));
}

[[nodiscard]] std::string trim(std::string value) {
  const auto first = std::ranges::find_if_not(value, [](unsigned char character) {
    return std::isspace(character) != 0;
  });
  const auto last = std::find_if_not(value.rbegin(), value.rend(),
                                     [](unsigned char character) {
                                       return std::isspace(character) != 0;
                                     })
                        .base();
  return first >= last ? std::string{} : std::string(first, last);
}

[[nodiscard]] std::vector<std::string> unique_strings(
    std::vector<std::string> values) {
  std::set<std::string> unique;
  for (auto &value : values) {
    value = trim(std::move(value));
    if (!value.empty()) {
      unique.insert(std::move(value));
    }
  }
  return {unique.begin(), unique.end()};
}

[[nodiscard]] std::vector<std::string> sentences(std::string_view text) {
  std::vector<std::string> result;
  std::string current;
  for (const char character : text) {
    current.push_back(character);
    if (character == '.' || character == '!' || character == '?') {
      const auto sentence = trim(std::move(current));
      if (!sentence.empty()) {
        result.push_back(sentence);
      }
      current.clear();
    }
  }
  const auto remainder = trim(std::move(current));
  if (!remainder.empty()) {
    result.push_back(remainder);
  }
  return result;
}

[[nodiscard]] std::string goal_text(const Json &request,
                                    std::string_view fallback) {
  const auto found = request.find("goal");
  if (found == request.end() || found->is_null()) {
    return std::string(fallback);
  }
  if (found->is_string()) {
    return found->get<std::string>();
  }
  if (found->is_array()) {
    std::string result;
    for (const auto &item : *found) {
      if (!item.is_string()) {
        reasoning_error("goal array must contain strings");
      }
      if (!result.empty()) {
        result += "; ";
      }
      result += item.get<std::string>();
    }
    return result;
  }
  reasoning_error("goal must be a string or string array");
}

[[nodiscard]] std::vector<std::string> json_strings(const Json &request,
                                                    std::string_view key) {
  const auto found = request.find(std::string(key));
  if (found == request.end() || found->is_null()) {
    return {};
  }
  if (!found->is_array()) {
    reasoning_error(std::string(key) + " must be an array");
  }
  return unique_strings(found->get<std::vector<std::string>>());
}

} // namespace

OiecSrProposalService::OiecSrProposalService(EgcfStore &store, Ieps &ieps)
    : store_(store), ieps_(ieps) {}

Json OiecSrProposalService::propose(const Json &request,
                                    std::string source_snapshot_hash,
                                    std::vector<std::string> scope) {
  if (!request.is_object()) {
    reasoning_error("request must be an object");
  }
  const std::string statement = trim(request.value(
      "statement", request.value("text", request.value("objective", ""))));
  auto hypothesis_statements = json_strings(request, "hypotheses");
  if (hypothesis_statements.empty()) {
    hypothesis_statements = sentences(statement);
  }
  if (hypothesis_statements.empty()) {
    reasoning_error("a statement or explicit hypothesis is required");
  }
  const int maximum = request.value("max_hypotheses", 16);
  if (maximum < 1 || maximum > 64 ||
      hypothesis_statements.size() > static_cast<std::size_t>(maximum)) {
    reasoning_error("hypothesis count exceeds the bounded request");
  }
  const bool mutually_exclusive = request.value("mutually_exclusive", false);
  const auto evidence_ids = json_strings(request, "evidence");
  const auto assumptions = json_strings(request, "assumptions");
  const auto predictions = json_strings(request, "predictions");
  const auto requested_falsifiers = json_strings(request, "falsifiers");
  scope = unique_strings(std::move(scope));
  if (scope.empty()) {
    scope = {"**"};
  }
  std::vector<reasoning::HypothesisProposal> proposals;
  proposals.reserve(hypothesis_statements.size());
  const int default_prior = mutually_exclusive
                                ? reasoning::score_scale /
                                      static_cast<int>(hypothesis_statements.size())
                                : reasoning::score_scale / 2;
  for (std::size_t index = 0; index < hypothesis_statements.size(); ++index) {
    const auto &proposition = hypothesis_statements.at(index);
    const std::string falsifier =
        index < requested_falsifiers.size()
            ? requested_falsifiers.at(index)
            : "independent evidence contradicts: " + proposition;
    proposals.push_back(
        {.hypothesis_id = {},
         .proposition = proposition,
         .prior_bp = default_prior,
         .posterior_bp = default_prior,
         .supporting_evidence = evidence_ids,
         .conflicting_evidence = {},
         .assumptions = assumptions,
         .predictions = predictions,
         .falsifiers = {falsifier},
         .status = "ACTIVE"});
  }
  const auto boundary_signature = contracts::sha256_json(scope);
  const auto dimension_signature = contracts::sha256_json(
      request.value("dimensions", Json::array()));
  const auto problem = reasoning::create_reasoning_problem(
      statement.empty() ? hypothesis_statements.front() : statement,
      goal_text(request, statement), std::move(source_snapshot_hash),
      boundary_signature, dimension_signature, evidence_ids,
      request.value("uncertainty_bp", reasoning::score_scale / 2),
      request.value("difficulty_bp", reasoning::score_scale / 2),
      mutually_exclusive);
  const auto state = reasoning::build_hypothesis_set(
      proposals, problem.problem_id, maximum, mutually_exclusive);
  std::vector<std::string> claim_ids;
  std::vector<std::string> requirement_ids;
  for (const auto &hypothesis : state.hypotheses) {
    const Json claim = {
        {"confidence_policy",
         {{"advisory", true},
          {"posterior_bp", hypothesis.posterior_bp},
          {"provider_authority", false}}},
        {"falsifier", hypothesis.falsifiers.front()},
        {"scope", scope},
        {"statement", hypothesis.proposition},
        {"status", "PROPOSED"},
        {"subject_id", problem.problem_id}};
    claim_ids.push_back(store_.register_record(
        {.object_type = "claim", .payload = claim},
        "oiec_sr_claim_proposed"));
    requirement_ids.push_back(ieps_.oracle(
        problem.problem_id, "falsify " + hypothesis.hypothesis_id,
        "counterexample", "independent deterministic or human oracle", true,
        0, "oiec-sr:" + hypothesis.hypothesis_id));
  }
  return {{"authoritative", false},
          {"claim_ids", claim_ids},
          {"evidence_requirement_ids", requirement_ids},
          {"hypothesis_set", reasoning::to_json(state)},
          {"next_operation", "RETRIEVE_EVIDENCE"},
          {"problem", reasoning::to_json(problem)},
          {"provider_authority", false},
          {"status", "ADVISORY_PROPOSAL"}};
}

} // namespace statewright::egcf
