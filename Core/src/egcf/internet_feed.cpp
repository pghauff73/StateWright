#include "statewright/egcf/internet_feed.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/contracts/typed_id.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace statewright::egcf {
namespace {

[[noreturn]] void feed_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

std::string lower(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

std::string trim(std::string value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front())) != 0) {
    value.erase(value.begin());
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back())) != 0) {
    value.pop_back();
  }
  return value;
}

std::vector<std::string> parse_list(std::string_view text,
                                    std::string_view label) {
  const std::string normalized = lower(std::string(text));
  const std::string marker = lower(std::string(label)) + ":";
  const auto start = normalized.find(marker);
  if (start == std::string::npos) {
    return {};
  }
  const std::size_t value_start = start + marker.size();
  const auto end = text.find_first_of(";\n", value_start);
  std::string value(text.substr(value_start, end - value_start));
  std::vector<std::string> result;
  std::size_t offset = 0U;
  while (offset <= value.size()) {
    const auto separator = value.find(',', offset);
    std::string item = trim(value.substr(offset, separator - offset));
    if (!item.empty()) {
      result.push_back(std::move(item));
    }
    if (separator == std::string::npos) {
      break;
    }
    offset = separator + 1U;
  }
  std::ranges::sort(result);
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

std::vector<std::string> lexical_terms(std::string_view text) {
  std::set<std::string> unique;
  std::string token;
  for (const char raw_character : std::string(text)) {
    const auto character = static_cast<unsigned char>(raw_character);
    if (std::isalnum(character) != 0 || character == '_') {
      token.push_back(static_cast<char>(std::tolower(character)));
    } else if (token.size() >= 4U) {
      unique.insert(std::move(token));
      token.clear();
    } else {
      token.clear();
    }
  }
  if (token.size() >= 4U) {
    unique.insert(std::move(token));
  }
  std::vector<std::string> result;
  for (const auto &value : unique) {
    if (value != "algorithm" && value != "procedure" && value != "input" &&
        value != "output") {
      result.push_back(value);
    }
    if (result.size() == 8U) {
      break;
    }
  }
  return result;
}

contracts::Json identity_ir(std::string input, std::string output) {
  return {{"entry_nodes", {"identity"}},
          {"inputs", {{{"name", input}, {"position", 0}}}},
          {"name", "internet-identity-candidate"},
          {"nodes",
           {{{"id", "identity"},
             {"operands", {{{"input", 0}}}},
             {"primitive", "IDENTITY"}}}},
          {"outputs",
           {{{"name", output},
             {"position", 0},
             {"source", {{"node", "identity"}}}}}}};
}

struct Translation final {
  std::string name;
  std::vector<std::string> inputs;
  std::vector<std::string> outputs;
  contracts::Json saa_ir = contracts::Json::object();
  std::vector<std::string> invariants;
  contracts::Json termination = contracts::Json::object();
  std::vector<std::string> unresolved;
};

Translation translate_algorithm(const sources::InternetSourceFragment &fragment) {
  Translation result;
  result.name = trim(fragment.text.substr(0U, fragment.text.find('\n')));
  if (result.name.size() > 120U) {
    result.name.resize(120U);
  }
  result.inputs = parse_list(fragment.text, "inputs");
  result.outputs = parse_list(fragment.text, "outputs");
  const std::string normalized = lower(fragment.text);
  const bool identity = normalized.find("identity") != std::string::npos ||
                        normalized.find("return the input") != std::string::npos ||
                        normalized.find("returns the input") != std::string::npos;
  if (identity) {
    if (result.inputs.empty()) {
      result.inputs = {"x"};
    }
    if (result.outputs.empty()) {
      result.outputs = {"y"};
    }
    if (result.inputs.size() == 1U && result.outputs.size() == 1U) {
      result.saa_ir = identity_ir(result.inputs.front(), result.outputs.front());
      result.invariants = {"output equals input", "terminates in one step"};
      result.termination = {{"bounded_steps", 1}, {"terminates", true}};
    }
  }
  if (result.name.empty()) {
    result.unresolved.push_back("MISSING_ALGORITHM_NAME");
  }
  if (result.inputs.empty()) {
    result.unresolved.push_back("MISSING_SEMANTIC_INPUTS");
  }
  if (result.outputs.empty()) {
    result.unresolved.push_back("MISSING_SEMANTIC_OUTPUTS");
  }
  if (result.saa_ir.empty()) {
    result.unresolved.push_back("UNSUPPORTED_SOURCE_TO_SAA_IR_TRANSLATION");
  }
  return result;
}

std::vector<std::string> candidate_ids(const contracts::Json &candidates) {
  std::vector<std::string> result;
  for (const auto &candidate : candidates) {
    result.push_back(candidate.at("canonical_id").get<std::string>());
  }
  std::ranges::sort(result);
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

} // namespace

InternetFeedCoordinator::InternetFeedCoordinator(EgcfStore &store)
    : store_(store), internet_(store), brain_feed_(store),
      canonical_algorithms_(store) {}

InternetFeedResult InternetFeedCoordinator::process(
    const sources::InternetPolicyAssessment &assessment_value,
    const sources::InternetExtractionResult &extraction,
    std::string source_label, bool strict) {
  const auto assessment =
      sources::canonical_policy_assessment(assessment_value);
  if (!assessment.admissible() ||
      assessment.snapshot_id != extraction.receipt.snapshot_id) {
    feed_error("internet feed requires an admissible matching snapshot");
  }
  static_cast<void>(internet_.register_policy_assessment(assessment));
  static_cast<void>(internet_.register_extraction(extraction));
  if (extraction.fragments.size() > maximum_brain_feed_items / 2U) {
    feed_error("internet extraction exceeds bounded brain-feed expansion");
  }

  std::vector<BrainFeedItem> items;
  std::map<std::string, std::string> algorithm_item_by_fragment;
  std::map<std::string, Translation> translations;
  for (const auto &fragment : extraction.fragments) {
    const auto parts = contracts::parse_typed_id(fragment.object_id());
    const std::string suffix = parts.digest.substr(0U, 16U);
    const std::string source_item_id = "internet-source-" + suffix;
    items.push_back(make_brain_feed_item(
        source_item_id, "SOURCE_DOCUMENT",
        {{"content", fragment.text},
         {"fragment_id", fragment.object_id()},
         {"fragment_kind", fragment.fragment_kind},
         {"selector", fragment.selector},
         {"snapshot_id", fragment.snapshot_id}},
        {}, {}, fragment.selector));
    if (fragment.fragment_kind != "ALGORITHM_DESCRIPTION") {
      continue;
    }
    Translation translation = translate_algorithm(fragment);
    const std::string algorithm_item_id = "internet-algorithm-" + suffix;
    items.push_back(make_brain_feed_item(
        algorithm_item_id, "ALGORITHM_CANDIDATE",
        {{"inputs", translation.inputs},
         {"name", translation.name},
         {"outputs", translation.outputs},
         {"procedure", fragment.text},
         {"proposed_saa_ir", translation.saa_ir},
         {"snapshot_id", fragment.snapshot_id}},
        {source_item_id}, {source_item_id}, fragment.selector));
    algorithm_item_by_fragment.emplace(fragment.object_id(), algorithm_item_id);
    translations.emplace(fragment.object_id(), std::move(translation));
  }

  const auto snapshot_parts =
      contracts::parse_typed_id(extraction.receipt.snapshot_id);
  InternetFeedResult result;
  result.brain_feed_batch = brain_feed_.feed(
      "internet-" + snapshot_parts.digest.substr(0U, 16U),
      snapshot_parts.digest, std::move(source_label), std::move(items), strict);
  std::map<std::string, BrainFeedDisposition> dispositions;
  for (const auto &disposition : result.brain_feed_batch.dispositions) {
    dispositions.emplace(disposition.item_id, disposition);
  }

  for (const auto &fragment : extraction.fragments) {
    const auto algorithm_item = algorithm_item_by_fragment.find(fragment.object_id());
    if (algorithm_item == algorithm_item_by_fragment.end()) {
      continue;
    }
    const auto disposition = dispositions.find(algorithm_item->second);
    if (disposition == dispositions.end()) {
      feed_error("brain feed omitted internet algorithm disposition");
    }
    const auto &translation = translations.at(fragment.object_id());
    CanonicalAlgorithmQuery query;
    query.semantic_meanings = translation.inputs;
    query.lexical_terms = lexical_terms(fragment.text);
    query.input_count = static_cast<int>(translation.inputs.size());
    query.output_count = static_cast<int>(translation.outputs.size());
    query.limit = 20U;
    const auto search = canonical_algorithms_.search(std::move(query));

    InternetKnowledgeSearchReceipt retrieval;
    retrieval.snapshot_id = fragment.snapshot_id;
    retrieval.source_fragment_id = fragment.object_id();
    retrieval.brain_feed_batch_id = result.brain_feed_batch.object_id();
    retrieval.canonical_search = to_json(search);
    retrieval.related_match_ids = candidate_ids(search.candidates);
    for (const auto &entry : search.excluded) {
      retrieval.exclusions.push_back(
          entry.at("canonical_id").get<std::string>() + ":" +
          contracts::canonical_json(entry.at("reasons")));
    }
    retrieval.exclusions.push_back(
        "EXACT_ID_SEARCH:NO_SOURCE_CANONICAL_ID_CLAIM");
    retrieval.exclusions.push_back(
        "MATHEMATICAL_EQUIVALENCE:REQUIRES_QUALIFIED_REPRESENTATIVE_FORM");
    retrieval.exclusions.push_back(
        "REASONING_EQUIVALENCE:NOT_APPLICABLE_TO_MATHEMATICAL_CANDIDATE");
    retrieval.exclusions.push_back(
        "TRANSFER_ADAPTATION:REQUIRES_QUALIFIED_BASELINE");
    const auto terms = lexical_terms(fragment.text);
    for (const auto &failure : store_.list("failure")) {
      const std::string payload = lower(contracts::canonical_json(failure.payload));
      if (std::ranges::any_of(terms, [&](const auto &term) {
            return payload.find(term) != std::string::npos;
          })) {
        retrieval.failure_match_ids.push_back(failure.object_id);
      }
    }
    retrieval.search_complete = true;
    if (disposition->second.duplicate()) {
      retrieval.novelty_status = "DUPLICATE";
    } else if (search.selected_canonical_id) {
      retrieval.novelty_status = "RELATED_EXISTING";
    } else if (!translation.unresolved.empty()) {
      retrieval.novelty_status = "QUARANTINED";
    } else {
      retrieval.novelty_status = "NOVEL_CANDIDATE";
    }
    retrieval = canonical_knowledge_search_receipt(std::move(retrieval));
    const std::string retrieval_id =
        internet_.register_retrieval_receipt(retrieval);

    InternetAlgorithmCandidate candidate;
    candidate.source_fragment_id = fragment.object_id();
    candidate.snapshot_id = fragment.snapshot_id;
    candidate.source_policy_assessment_id = assessment.object_id();
    candidate.proposed_saa_ir = translation.saa_ir;
    candidate.semantic_inputs = translation.inputs;
    candidate.semantic_outputs = translation.outputs;
    candidate.units = {{"status", "SOURCE_UNSPECIFIED"}};
    candidate.applicability = {{"source_group", "internet"}};
    candidate.claimed_invariants = translation.invariants;
    candidate.termination_properties = translation.termination;
    candidate.retrieval_receipt_id = retrieval_id;
    candidate.exact_match_ids = retrieval.exact_match_ids;
    candidate.equivalent_match_ids = retrieval.equivalent_match_ids;
    candidate.related_match_ids = retrieval.related_match_ids;
    candidate.transfer_match_ids = retrieval.transfer_match_ids;
    candidate.failure_match_ids = retrieval.failure_match_ids;
    candidate.unresolved_assumptions = translation.unresolved;
    if (retrieval.novelty_status == "NOVEL_CANDIDATE") {
      candidate.status = "VALIDATION_READY";
    } else {
      candidate.status = retrieval.novelty_status;
    }
    candidate = canonical_internet_algorithm_candidate(std::move(candidate));
    static_cast<void>(internet_.register_algorithm_candidate(candidate));
    result.retrieval_receipts.push_back(std::move(retrieval));
    result.candidates.push_back(std::move(candidate));
  }
  auto material = to_json(result);
  material.erase("result_signature");
  result.result_signature = contracts::sha256_json(material);
  return result;
}

contracts::Json to_json(const InternetFeedResult &value) {
  contracts::Json retrieval = contracts::Json::array();
  for (const auto &receipt : value.retrieval_receipts) {
    retrieval.push_back(to_json(receipt));
  }
  contracts::Json candidates = contracts::Json::array();
  for (const auto &candidate : value.candidates) {
    candidates.push_back(to_json(candidate));
  }
  return {{"brain_feed_batch", to_json(value.brain_feed_batch)},
          {"candidates", std::move(candidates)},
          {"result_signature", value.result_signature},
          {"retrieval_receipts", std::move(retrieval)}};
}

} // namespace statewright::egcf
