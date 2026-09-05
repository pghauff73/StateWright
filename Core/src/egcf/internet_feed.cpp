#include "statewright/egcf/internet_feed.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/contracts/typed_id.hpp"
#include "statewright/egcf/internet_experiment.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <regex>
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
  const auto whitespace = [](unsigned char c) { return std::isspace(c) != 0; };
  const auto first = std::find_if_not(value.begin(), value.end(), whitespace);
  if (first == value.end())
    return {};
  const auto last =
      std::find_if_not(value.rbegin(), value.rend(), whitespace).base();
  return {first, last};
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
  contracts::Json provenance = contracts::Json::object();
  std::vector<std::string> unresolved;
};

Translation
translate_algorithm(const sources::InternetSourceFragment &fragment) {
  Translation result;
  if (fragment.metadata.value("mathematical_context_review_required", false)) {
    result.name = "mathematical-source-context";
    result.unresolved = {"MATHEMATICAL_CONTEXT_REVIEW_REQUIRED",
                         "DOMAIN_BRANCH_AND_ERROR_BOUNDS_NOT_QUALIFIED"};
    result.provenance = {{"status", "UNSUPPORTED_MATHEMATICAL_SOURCE"},
                         {"source_fragment_id", fragment.object_id()}};
    return result;
  }
  const auto first_separator = fragment.text.find(';');
  result.name = trim(fragment.text.substr(0U, first_separator));
  struct Field {
    std::string value;
    std::size_t start;
    std::size_t end;
  };
  std::map<std::string, Field> fields;
  bool valid_fields = first_separator != std::string::npos;
  std::size_t offset =
      valid_fields ? first_separator + 1U : fragment.text.size();
  while (offset < fragment.text.size()) {
    const auto separator = fragment.text.find(';', offset);
    const std::size_t end =
        separator == std::string::npos ? fragment.text.size() : separator;
    const std::string field = fragment.text.substr(offset, end - offset);
    const auto colon = field.find(':');
    if (!trim(field).empty()) {
      if (colon == std::string::npos) {
        valid_fields = false;
        break;
      }
      const std::string label = lower(trim(field.substr(0, colon)));
      if (label != "inputs" && label != "outputs" && label != "procedure" &&
          label != "source code example") {
        valid_fields = false;
        break;
      }
      if (!fields
               .emplace(label, Field{trim(field.substr(colon + 1U)),
                                     offset + colon + 1U, end})
               .second) {
        valid_fields = false;
        break;
      }
    }
    offset = end + 1U;
  }
  static const std::regex identifier("[A-Za-z_][A-Za-z0-9_]{0,63}");
  for (const auto &label : {"inputs", "outputs"}) {
    if (const auto found = fields.find(label);
        found != fields.end() &&
        std::regex_match(found->second.value, identifier)) {
      (std::string_view(label) == "inputs" ? result.inputs : result.outputs)
          .push_back(found->second.value);
    }
  }
  if (valid_fields && result.inputs.size() == 1U &&
      result.outputs.size() == 1U && fields.contains("procedure")) {
    const auto &procedure = fields.at("procedure");
    const auto &input = result.inputs.front();
    const auto &output = result.outputs.front();
    mpq_class slope{1}, bias{0};
    bool supported = procedure.value == "return the input" ||
                     procedure.value == "return " + input;
    if (!supported && procedure.value.size() <= 256U) {
      static const std::regex affine(
          R"(^return\s+([+-]?[0-9]{1,64}(?:/[0-9]{1,64})?)\s*\*\s*([A-Za-z_][A-Za-z0-9_]{0,63})\s*([+-])\s*([0-9]{1,64}(?:/[0-9]{1,64})?)$)");
      std::smatch match;
      if (std::regex_match(procedure.value, match, affine) &&
          match[2] == input) {
        try {
          slope = mpq_class(match[1].str());
          bias = mpq_class(match[4].str());
          // GMP canonicalization is undefined for a zero denominator.
          supported = slope.get_den() != 0 && bias.get_den() != 0;
          if (supported) {
            slope.canonicalize();
            bias.canonicalize();
            if (match[3] == "-") {
              bias = -bias;
            }
            supported = slope != 0;
          }
        } catch (const std::exception &) {
          supported = false;
        }
      }
    }
    if (supported) {
      if (slope == 1 && bias == 0) {
        result.saa_ir = identity_ir(input, output);
        result.invariants = {"output equals input", "terminates in one step"};
        result.termination = {{"bounded_steps", 1}, {"terminates", true}};
      } else {
        result.saa_ir = {
            {"name", "internet-affine-candidate"},
            {"entry_nodes", {"scale"}},
            {"inputs", {{{"name", input}, {"position", 0}}}},
            {"nodes",
             {{{"id", "scale"},
               {"primitive", "MULTIPLY"},
               {"operands", {{{"constant", slope.get_str()}}, {{"input", 0}}}}},
              {{"id", "offset"},
               {"primitive", "ADD"},
               {"operands",
                {{{"node", "scale"}}, {{"constant", bias.get_str()}}}}}}},
            {"outputs",
             {{{"name", output},
               {"position", 0},
               {"source", {{"node", "offset"}}}}}}};
        result.invariants = {"output equals " + slope.get_str() +
                                 " * input + " + bias.get_str(),
                             "terminates in two steps"};
        result.termination = {{"bounded_steps", 2}, {"terminates", true}};
      }
      result.provenance = {{"translator_version", "exact-scalar-procedure-v2"},
                           {"source_fragment_id", fragment.object_id()},
                           {"snapshot_id", fragment.snapshot_id},
                           {"selector", fragment.selector},
                           {"procedure_start", procedure.start},
                           {"procedure_end", procedure.end},
                           {"procedure", procedure.value},
                           {"slope", slope.get_str()},
                           {"bias", bias.get_str()}};
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

void verify_internet_candidate_translation(
    const InternetAlgorithmCandidate &candidate,
    const sources::InternetSourceFragment &fragment) {
  const auto translation = translate_algorithm(fragment);
  if (candidate.source_fragment_id != fragment.object_id() ||
      candidate.snapshot_id != fragment.snapshot_id ||
      !translation.unresolved.empty() ||
      candidate.proposed_saa_ir != translation.saa_ir ||
      candidate.semantic_inputs != translation.inputs ||
      candidate.semantic_outputs != translation.outputs ||
      candidate.claimed_invariants != translation.invariants ||
      candidate.termination_properties != translation.termination ||
      candidate.applicability.value("translation", contracts::Json::object()) !=
          translation.provenance) {
    feed_error(
        "candidate does not faithfully match its explicit source procedure");
  }
}

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
    items.push_back(
        make_brain_feed_item(source_item_id, "SOURCE_DOCUMENT",
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
  // Reuse the original durable dispositions. Refeeding an already staged item
  // marks it duplicate and would change novelty after a crash/restart.
  bool batch_found = false;
  for (const auto &batch : brain_feed_.batches()) {
    if (batch.source_signature != snapshot_parts.digest ||
        batch.dispositions.size() != items.size()) {
      continue;
    }
    const bool matches = std::ranges::all_of(items, [&](const auto &item) {
      return std::ranges::any_of(batch.dispositions, [&](const auto &entry) {
        return entry.item_id == item.item_id &&
               entry.item_signature == item.item_signature;
      });
    });
    if (matches) {
      result.brain_feed_batch = batch;
      batch_found = true;
      break;
    }
  }
  if (!batch_found) {
    result.brain_feed_batch =
        brain_feed_.feed("internet-" + snapshot_parts.digest.substr(0U, 16U),
                         snapshot_parts.digest, std::move(source_label),
                         std::move(items), strict);
  }
  std::map<std::string, BrainFeedDisposition> dispositions;
  for (const auto &disposition : result.brain_feed_batch.dispositions) {
    dispositions.emplace(disposition.item_id, disposition);
  }
  std::map<std::string, InternetKnowledgeSearchReceipt> prior_retrievals;
  for (const auto &record : internet_.list("internet-retrieval-receipt")) {
    auto retrieval =
        internet_knowledge_search_receipt_from_json(record.payload);
    if (retrieval.brain_feed_batch_id == result.brain_feed_batch.object_id() &&
        retrieval.snapshot_id == extraction.receipt.snapshot_id) {
      prior_retrievals.emplace(retrieval.source_fragment_id,
                               std::move(retrieval));
    }
  }
  std::map<std::string, InternetAlgorithmCandidate> prior_candidates;
  for (const auto &record : internet_.list("internet-algorithm-candidate")) {
    auto candidate = internet_algorithm_candidate_from_json(record.payload);
    if (candidate.snapshot_id == extraction.receipt.snapshot_id &&
        candidate.reasoning_analysis_ids.empty() &&
        candidate.experiment_qualification_ids.empty() &&
        candidate.promotion_assessment_ids.empty()) {
      prior_candidates.emplace(candidate.retrieval_receipt_id,
                               std::move(candidate));
    }
  }

  for (const auto &fragment : extraction.fragments) {
    const auto algorithm_item =
        algorithm_item_by_fragment.find(fragment.object_id());
    if (algorithm_item == algorithm_item_by_fragment.end()) {
      continue;
    }
    const auto disposition = dispositions.find(algorithm_item->second);
    if (disposition == dispositions.end()) {
      feed_error("brain feed omitted internet algorithm disposition");
    }
    const auto &translation = translations.at(fragment.object_id());
    const auto prior_retrieval = prior_retrievals.find(fragment.object_id());
    InternetKnowledgeSearchReceipt retrieval;
    if (prior_retrieval != prior_retrievals.end()) {
      retrieval = prior_retrieval->second;
    } else {
      CanonicalAlgorithmQuery query;
      query.semantic_meanings = translation.inputs;
      query.lexical_terms = lexical_terms(fragment.text);
      query.input_count = static_cast<int>(translation.inputs.size());
      query.output_count = static_cast<int>(translation.outputs.size());
      query.limit = 20U;
      const auto search = canonical_algorithms_.search(std::move(query));

      retrieval.snapshot_id = fragment.snapshot_id;
      retrieval.source_fragment_id = fragment.object_id();
      retrieval.brain_feed_batch_id = result.brain_feed_batch.object_id();
      retrieval.canonical_search = to_json(search);
      retrieval.source_policy_assessment_id = assessment.object_id();
      retrieval.related_match_ids = candidate_ids(search.candidates);
      if (translation.unresolved.empty()) {
        CanonicalAlgorithmQuery exact_query;
        exact_query.source_structural_hash =
            saa::canonicalize_mapping(translation.saa_ir).structural_hash;
        exact_query.semantic_meanings = {internet_exact_scalar_meaning(
            internet_exact_scalar_program(translation.saa_ir),
            translation.inputs.front(), translation.outputs.front())};
        exact_query.input_count = 1;
        exact_query.output_count = 1;
        exact_query.limit = 20U;
        const auto exact = canonical_algorithms_.search(std::move(exact_query));
        retrieval.exact_match_ids = candidate_ids(exact.candidates);
        retrieval.canonical_search["exact_structural_search"] = to_json(exact);
      }
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
        const std::string payload =
            lower(contracts::canonical_json(failure.payload));
        if (std::ranges::any_of(terms, [&](const auto &term) {
              return payload.find(term) != std::string::npos;
            })) {
          retrieval.failure_match_ids.push_back(failure.object_id);
        }
      }
      retrieval.search_complete = true;
      if (disposition->second.duplicate() ||
          !retrieval.exact_match_ids.empty()) {
        retrieval.novelty_status = "DUPLICATE";
      } else if (!translation.unresolved.empty()) {
        retrieval.novelty_status = "QUARANTINED";
      } else {
        retrieval.novelty_status = "NOVEL_CANDIDATE";
      }
      retrieval = canonical_knowledge_search_receipt(std::move(retrieval));
    }
    const std::string retrieval_id =
        internet_.register_retrieval_receipt(retrieval);

    if (const auto prior_candidate = prior_candidates.find(retrieval_id);
        prior_candidate != prior_candidates.end()) {
      result.retrieval_receipts.push_back(std::move(retrieval));
      result.candidates.push_back(prior_candidate->second);
      continue;
    }

    InternetAlgorithmCandidate candidate;
    candidate.source_fragment_id = fragment.object_id();
    candidate.snapshot_id = fragment.snapshot_id;
    candidate.source_policy_assessment_id =
        retrieval.source_policy_assessment_id.empty()
            ? assessment.object_id()
            : retrieval.source_policy_assessment_id;
    candidate.proposed_saa_ir = translation.saa_ir;
    candidate.semantic_inputs = translation.inputs;
    candidate.semantic_outputs = translation.outputs;
    candidate.units = {{"status", "SOURCE_UNSPECIFIED"}};
    candidate.applicability = {
        {"source_group",
         store_.get(fragment.snapshot_id).payload.at("source_group")},
        {"translation", translation.provenance}};
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

std::optional<std::vector<std::string>> internet_feed_completion_outputs(
    const sources::InternetExtractionReceipt &extraction,
    const std::vector<StoredObject> &records) {
  if (extraction.fragment_ids.empty()) {
    return std::vector<std::string>{};
  }
  const auto snapshot = contracts::parse_typed_id(extraction.snapshot_id);
  std::map<std::string, StoredObject> by_id;
  std::vector<BrainFeedBatchReceipt> batches;
  std::vector<InternetAlgorithmCandidate> candidates;
  for (const auto &record : records) {
    by_id.emplace(record.object_id, record);
    if (record.object_type == "brain-feed-batch") {
      const auto batch = brain_feed_batch_from_json(record.payload);
      if (batch.source_signature == snapshot.digest) {
        batches.push_back(batch);
      }
    } else if (record.object_type == "internet-algorithm-candidate") {
      const auto candidate =
          internet_algorithm_candidate_from_json(record.payload);
      if (candidate.snapshot_id == extraction.snapshot_id &&
          candidate.reasoning_analysis_ids.empty() &&
          candidate.experiment_qualification_ids.empty() &&
          candidate.promotion_assessment_ids.empty()) {
        candidates.push_back(candidate);
      }
    }
  }
  for (const auto &batch : batches) {
    std::vector<std::string> outputs = {batch.object_id()};
    bool complete = true;
    for (const auto &fragment_id : extraction.fragment_ids) {
      const auto stored = by_id.find(fragment_id);
      if (stored == by_id.end() ||
          stored->second.object_type != "internet-source-fragment") {
        complete = false;
        break;
      }
      const auto fragment =
          sources::internet_source_fragment_from_json(stored->second.payload);
      const auto source_item =
          "internet-source-" +
          contracts::parse_typed_id(fragment_id).digest.substr(0U, 16U);
      if (!std::ranges::any_of(batch.dispositions, [&](const auto &entry) {
            return entry.item_id == source_item &&
                   entry.kind == "SOURCE_DOCUMENT";
          })) {
        complete = false;
        break;
      }
      if (fragment.fragment_kind != "ALGORITHM_DESCRIPTION") {
        continue;
      }
      bool candidate_found = false;
      for (const auto &candidate : candidates) {
        if (candidate.source_fragment_id != fragment_id) {
          continue;
        }
        const auto receipt = by_id.find(candidate.retrieval_receipt_id);
        if (receipt == by_id.end() ||
            receipt->second.object_type != "internet-retrieval-receipt") {
          continue;
        }
        const auto retrieval = internet_knowledge_search_receipt_from_json(
            receipt->second.payload);
        if (retrieval.source_fragment_id == fragment_id &&
            retrieval.snapshot_id == extraction.snapshot_id &&
            retrieval.brain_feed_batch_id == batch.object_id() &&
            retrieval.search_complete) {
          outputs.push_back(receipt->first);
          outputs.push_back(candidate.object_id());
          candidate_found = true;
          break;
        }
      }
      if (!candidate_found) {
        complete = false;
        break;
      }
    }
    if (complete) {
      std::ranges::sort(outputs);
      return outputs;
    }
  }
  return std::nullopt;
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
