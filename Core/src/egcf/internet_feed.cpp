#include "statewright/egcf/internet_feed.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/contracts/typed_id.hpp"
#include "statewright/egcf/internet_experiment.hpp"
#include "statewright/egcf/exact_affine_expression.hpp"
#include "statewright/egcf/grounded_experiment.hpp"
#include "statewright/egcf/internet_polynomial_translation.hpp"
#include "statewright/egcf/internet_polynomial_candidate.hpp"

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
  contracts::Json units = {{"status", "SOURCE_UNSPECIFIED"}};
  std::vector<std::string> unresolved;
};

Translation
translate_algorithm(const sources::InternetSourceFragment &fragment) {
  Translation result;
  const bool diagnostic_v2 = fragment.metadata.contains("classification_version");
  if ((fragment.metadata.value("document_extraction_truncated", false) &&
       fragment.metadata.value("section_completeness", std::string{}) != "COMPLETE") ||
      fragment.metadata.value("section_completeness", std::string{}) ==
          "INCOMPLETE") {
    result.name = "incomplete-source-context";
    result.unresolved = {"SOURCE_CONTEXT_INCOMPLETE"};
    return result;
  }
  if (fragment.metadata.value("mathematical_context_review_required", false)) {
    result.name = "mathematical-source-context";
    result.unresolved = {"MATHEMATICAL_CONTEXT_REVIEW_REQUIRED",
                         "DOMAIN_BRANCH_AND_ERROR_BOUNDS_NOT_QUALIFIED"};
    result.provenance = {{"status", "UNSUPPORTED_MATHEMATICAL_SOURCE"},
                         {"source_fragment_id", fragment.object_id()}};
    return result;
  }
  if (fragment.metadata.value("context_preservation", std::string{}) ==
      "whole-section-decoded-text") {
    result.name = "preserved-source-section";
    result.unresolved = {"SOURCE_SECTION_SYNTAX_NOT_SUPPORTED"};
    return result;
  }
  if (fragment.metadata.value("candidate_evidence", std::string{}) ==
      "CSS_ABSOLUTE_LENGTH_SECTION") {
    result.name = "CSS inches to CSS pixels";
    // Reviewed W3C CSS Values 3, 22 March 2024, section 5.2. The exact
    // section includes both the ratio and the physical/device-pixel caveats.
    // A changed source requires a new reviewed adapter, not heuristic parsing.
    if (fragment.metadata.value("section_completeness", std::string{}) !=
            "COMPLETE" ||
        contracts::sha256_text(fragment.text) !=
            "cd24adf3f40876a83f6b179f3417317d0124e1dd475fe2abde7880b2785201ec") {
      result.unresolved = {"SOURCE_SECTION_REVISION_NOT_SUPPORTED"};
      return result;
    }
    const auto input_unit = fragment.metadata.value("css_input_unit", std::string("in"));
    static const std::map<std::string, std::pair<std::string, std::string>> conversions = {
        {"in", {"96", "inches"}}, {"cm", {"4800/127", "centimeters"}},
        {"mm", {"480/127", "millimeters"}}, {"Q", {"120/127", "quarter-millimeters"}},
        {"pc", {"16", "picas"}}, {"pt", {"4/3", "points"}}};
    const auto conversion = conversions.find(input_unit);
    if (conversion == conversions.end()) {
      result.unresolved = {"CSS_INPUT_UNIT_NOT_SUPPORTED"};
      return result;
    }
    const auto &factor = conversion->second.first;
    const auto input_name = "css_length_" + input_unit;
    result.name = "CSS " + conversion->second.second + " to CSS pixels";
    result.inputs = {input_name};
    result.outputs = {"css_length_px"};
    result.saa_ir = {
        {"name", input_unit == "in" ? "css-inches-to-css-pixels" : "css-" + input_unit + "-to-css-pixels"},
        {"entry_nodes", {"scale"}},
        {"inputs", {{{"name", input_name}, {"position", 0}}}},
        {"nodes", {{{"id", "scale"}, {"primitive", "MULTIPLY"},
                    {"operands", {{{"constant", factor}}, {{"input", 0}}}}},
                   {{"id", "offset"}, {"primitive", "ADD"},
                    {"operands", {{{"node", "scale"}}, {{"constant", "0"}}}}}}},
        {"outputs", {{{"name", "css_length_px"}, {"position", 0},
                      {"source", {{"node", "offset"}}}}}}};
    result.invariants = {input_unit == "in" ? "CSS pixel length equals 96 times CSS inch length" :
                         "CSS pixel length equals " + factor + " times CSS " + input_unit + " length",
                         "terminates in two steps"};
    result.termination = {{"bounded_steps", 2}, {"terminates", true}};
    result.units = {{"status", "SOURCE_DECLARED"}, {"input", "CSS " + input_unit},
                    {"output", "CSS px"}};
    result.provenance = {
        {"translator_version", input_unit == "in" ? "css-absolute-length-in-px-v1" : "css-absolute-length-to-px-v2"},
        {"source_fragment_id", fragment.object_id()},
        {"snapshot_id", fragment.snapshot_id}, {"selector", fragment.selector},
        {"section_sha256", contracts::sha256_text(fragment.text)},
        {"reference_url", "https://www.w3.org/TR/2024/CRD-css-values-3-20240322/#absolute-lengths"},
        {"slope", factor}, {"bias", "0"},
        {"scope", "Exact numeric CSS unit conversion only; not device pixels, physical screen measurement, or property range validation"}};
    return result;
  }
  if (const auto polynomial = translate_internet_polynomial_fragment(fragment)) {
    result.name = polynomial->name;
    result.inputs = polynomial->inputs;
    result.outputs = polynomial->outputs;
    result.saa_ir = polynomial->saa_ir;
    result.invariants = polynomial->invariants;
    result.termination = polynomial->termination;
    result.provenance = polynomial->provenance;
    result.units = polynomial->units;
    return result;
  }
  std::string field_text = fragment.text;
  bool projected_fields = false;
  if (fragment.metadata.value("scalar_expression_syntax", std::string{}) ==
          "exact-affine-expression-v1" && field_text.find(';') == std::string::npos) {
    // Accept one title followed by explicit labeled lines. Every line remains
    // in the parse: conditions and unrecognized statements are not discarded.
    const auto newline = field_text.find('\n');
    if (newline != std::string::npos) {
      for (auto &c : field_text) if (c == '\n') c = ';';
      projected_fields = true;
    }
  }
  const auto first_separator = field_text.find(';');
  result.name = trim(field_text.substr(0U, first_separator));
  struct Field {
    std::string value;
    std::size_t start;
    std::size_t end;
  };
  std::map<std::string, Field> fields;
  bool valid_fields = first_separator != std::string::npos;
  std::size_t offset =
      valid_fields ? first_separator + 1U : field_text.size();
  while (offset < field_text.size()) {
    const auto separator = field_text.find(';', offset);
    const std::size_t end =
        separator == std::string::npos ? field_text.size() : separator;
    const std::string field = field_text.substr(offset, end - offset);
    const auto colon = field.find(':');
    if (!trim(field).empty()) {
      if (colon == std::string::npos) {
        valid_fields = false;
        break;
      }
      std::string label = lower(trim(field.substr(0, colon)));
      if (fragment.metadata.value("scalar_expression_syntax", std::string{}) ==
          "exact-affine-expression-v1") {
        if (label == "input") label = "inputs";
        if (label == "output") label = "outputs";
      }
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
    bool abbreviated_affine = false;
    bool parsed_affine = false;
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
    if (!supported && procedure.value.size() <= 256U) {
      // Accept only complete scalar scale/offset expressions. Do not infer
      // missing operands from prose or evaluate downloaded expressions.
      static const std::regex scale(
          R"(^return\s+([+-]?[0-9]{1,64}(?:/[0-9]{1,64})?)\s*\*\s*([A-Za-z_][A-Za-z0-9_]{0,63})$)");
      static const std::regex offset_expression(
          R"(^return\s+([A-Za-z_][A-Za-z0-9_]{0,63})\s*([+-])\s*([0-9]{1,64}(?:/[0-9]{1,64})?)$)");
      std::smatch match;
      try {
        if (std::regex_match(procedure.value, match, scale) &&
            match[2] == input) {
          slope = mpq_class(match[1].str(), 10);
          bias = 0;
          abbreviated_affine = true;
        } else if (std::regex_match(procedure.value, match,
                                    offset_expression) &&
                   match[1] == input) {
          slope = 1;
          bias = mpq_class(match[3].str(), 10);
          if (match[2] == "-")
            bias = -bias;
          abbreviated_affine = true;
        }
        if (abbreviated_affine && slope.get_den() != 0 &&
            bias.get_den() != 0) {
          slope.canonicalize();
          bias.canonicalize();
          supported = slope != 0;
        }
      } catch (const std::exception &) {
        supported = false;
      }
    }
    // New extraction versions may use exact decimals, parentheses and
    // constant division. Keep old-fragment translation replay unchanged.
    if (!supported && fragment.metadata.value("scalar_expression_syntax", std::string{}) ==
                          "exact-affine-expression-v1" &&
        procedure.value.starts_with("return ")) {
      if (const auto expression = parse_exact_affine_expression(
              std::string_view(procedure.value).substr(7), input)) {
        slope = expression->slope;
        bias = expression->bias;
        supported = true;
        parsed_affine = true;
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
      result.provenance = {{"translator_version",
                            (parsed_affine || projected_fields) ? "exact-scalar-procedure-v4" :
                            abbreviated_affine ? "exact-scalar-procedure-v3"
                                               : "exact-scalar-procedure-v2"},
                           {"source_fragment_id", fragment.object_id()},
                           {"snapshot_id", fragment.snapshot_id},
                           {"selector", fragment.selector},
                           {"procedure_start", procedure.start},
                           {"procedure_end", procedure.end},
                           {"procedure", procedure.value},
                           {"slope", slope.get_str()},
                           {"bias", bias.get_str()}};
      if (projected_fields)
        result.provenance["field_text_projection"] = "newline-to-semicolon-byte-preserving-v1";
    }
  }
  if (result.name.empty()) {
    result.unresolved.push_back("MISSING_ALGORITHM_NAME");
  }
  if (result.inputs.empty()) {
    result.unresolved.push_back(diagnostic_v2
        ? "SOURCE_INPUT_DECLARATION_NOT_PARSED" : "MISSING_SEMANTIC_INPUTS");
  }
  if (result.outputs.empty()) {
    result.unresolved.push_back(diagnostic_v2
        ? "SOURCE_OUTPUT_DECLARATION_NOT_PARSED" : "MISSING_SEMANTIC_OUTPUTS");
  }
  if (result.saa_ir.empty()) {
    if (diagnostic_v2 && !fields.contains("procedure"))
      result.unresolved.push_back("SOURCE_PROCEDURE_NOT_PARSED");
    result.unresolved.push_back(diagnostic_v2
        ? "UNSUPPORTED_PROCEDURE_SYNTAX_OR_FAMILY"
        : "UNSUPPORTED_SOURCE_TO_SAA_IR_TRANSLATION");
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
  if (candidate.applicability.value("translation", contracts::Json::object())
          .value("translator_version", std::string{}) ==
      "fungrim-chebyshev-quadratic-candidate-v1") {
    verify_fungrim_quadratic_candidate_translation(candidate, fragment);
    return;
  }
  const auto translation = translate_algorithm(fragment);
  if (candidate.source_fragment_id != fragment.object_id() ||
      candidate.snapshot_id != fragment.snapshot_id ||
      !translation.unresolved.empty() ||
      candidate.proposed_saa_ir != translation.saa_ir ||
      candidate.semantic_inputs != translation.inputs ||
      candidate.semantic_outputs != translation.outputs ||
      candidate.claimed_invariants != translation.invariants ||
      candidate.termination_properties != translation.termination ||
      ((translation.provenance.value("translator_version", std::string{}) ==
           "css-absolute-length-in-px-v1" ||
        translation.provenance.value("translator_version", std::string{}) ==
           "css-absolute-length-to-px-v2" ||
        translation.provenance.value("translator_version", std::string{}) ==
           "exact-polynomial-source-v1") && candidate.units != translation.units) ||
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
    const sources::InternetExtractionResult &extraction_value,
    std::string source_label, bool strict,
    std::size_t maximum_fragments_per_step) {
  auto extraction = extraction_value;
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
  InternetFeedResult result;
  result.total_fragments = extraction.fragments.size();
  std::vector<StoredObject> progress_records;
  if (maximum_fragments_per_step != 0) {
    for (const auto type : {"brain-feed-batch", "internet-retrieval-receipt",
                            "internet-algorithm-candidate"}) {
      auto records = store_.list(type);
      progress_records.insert(progress_records.end(), records.begin(), records.end());
    }
    for (const auto &fragment : extraction.fragments)
      progress_records.push_back({.object_id = fragment.object_id(),
          .object_type = "internet-source-fragment", .digest = {},
          .payload = sources::to_json(fragment), .relative_path = {}});
    std::vector<std::string> completed;
    if (const auto outputs = internet_feed_completion_outputs(
            extraction.receipt, progress_records, &completed)) {
      result.completion_output_ids = *outputs;
      auto material = to_json(result);
      material.erase("result_signature");
      result.result_signature = contracts::sha256_json(material);
      return result;
    }
    const std::set<std::string> done(completed.begin(), completed.end());
    std::erase_if(extraction.fragments, [&](const auto &fragment) {
      return done.contains(fragment.object_id());
    });
    if (extraction.fragments.size() > maximum_fragments_per_step)
      extraction.fragments.resize(maximum_fragments_per_step);
  }
  result.processed_fragments = extraction.fragments.size();

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
  // Reuse the original durable dispositions. Refeeding an already staged item
  // marks it duplicate and would change novelty after a crash/restart.
  bool batch_found = false;
  for (const auto &batch : brain_feed_.batches()) {
    if (batch.source_signature != snapshot_parts.digest ||
        batch.dispositions.size() < items.size()) {
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
                         std::move(items), strict, true);
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

  // Failure records are not changed by candidate/retrieval registration below.
  // Load and normalize this snapshot lazily once per batch, not per fragment.
  std::vector<std::pair<std::string, std::string>> failure_documents;
  bool failure_documents_loaded = false;
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
      const auto terms = lexical_terms(fragment.text);
      CanonicalAlgorithmQuery query;
      query.semantic_meanings = translation.inputs;
      query.lexical_terms = terms;
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
        // Feed and qualification must agree on catalogue presence, including
        // the full polynomial representation rather than only linear forms.
        InternetAlgorithmCandidate lookup_candidate;
        lookup_candidate.proposed_saa_ir = translation.saa_ir;
        lookup_candidate.semantic_inputs = translation.inputs;
        lookup_candidate.semantic_outputs = translation.outputs;
        lookup_candidate.units = translation.units;
        const auto exact = exact_capability_search(store_, lookup_candidate);
        retrieval.exact_match_ids = candidate_ids(exact.at("candidates"));
        retrieval.canonical_search["exact_structural_search"] = exact;
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
      if (!terms.empty() && !failure_documents_loaded) {
        for (const auto &failure : store_.list("failure")) {
          failure_documents.emplace_back(
              failure.object_id,
              lower(contracts::canonical_json(failure.payload)));
        }
        failure_documents_loaded = true;
      }
      for (const auto &[failure_id, payload] : failure_documents) {
        if (std::ranges::any_of(terms, [&](const auto &term) {
              return payload.find(term) != std::string::npos;
            })) {
          retrieval.failure_match_ids.push_back(failure_id);
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
    candidate.units = translation.units;
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
  if (maximum_fragments_per_step != 0) {
    progress_records.push_back({.object_id = result.brain_feed_batch.object_id(),
        .object_type = "brain-feed-batch", .digest = {},
        .payload = to_json(result.brain_feed_batch), .relative_path = {}});
    for (const auto &receipt : result.retrieval_receipts)
      progress_records.push_back({.object_id = receipt.object_id(),
          .object_type = "internet-retrieval-receipt", .digest = {},
          .payload = to_json(receipt), .relative_path = {}});
    for (const auto &candidate : result.candidates)
      progress_records.push_back({.object_id = candidate.object_id(),
          .object_type = "internet-algorithm-candidate", .digest = {},
          .payload = to_json(candidate), .relative_path = {}});
    const auto outputs = internet_feed_completion_outputs(extraction_value.receipt, progress_records);
    result.complete = outputs.has_value();
    if (outputs) result.completion_output_ids = *outputs;
  }
  auto material = to_json(result);
  material.erase("result_signature");
  result.result_signature = contracts::sha256_json(material);
  return result;
}

std::optional<std::vector<std::string>> internet_feed_completion_outputs(
    const sources::InternetExtractionReceipt &extraction,
    const std::vector<StoredObject> &records,
    std::vector<std::string> *completed_fragments) {
  if (completed_fragments) completed_fragments->clear();
  if (extraction.fragment_ids.empty()) {
    return std::vector<std::string>{};
  }
  const auto snapshot = contracts::parse_typed_id(extraction.snapshot_id);
  // The director asks about many extractions against the same store snapshot.
  // Do not copy every payload or re-parse unrelated candidates for each one.
  std::map<std::string, const StoredObject *> by_id;
  std::vector<std::pair<std::string, BrainFeedBatchReceipt>> batches;
  std::vector<InternetAlgorithmCandidate> candidates;
  for (const auto &record : records) {
    by_id.emplace(record.object_id, &record);
    if (record.object_type == "brain-feed-batch" &&
        record.payload.value("source_signature", std::string{}) == snapshot.digest) {
      const auto batch = brain_feed_batch_from_json(record.payload);
      if (batch.source_signature == snapshot.digest) {
        batches.emplace_back(batch.object_id(), batch);
      }
    } else if (record.object_type == "internet-algorithm-candidate" &&
               record.payload.value("snapshot_id", std::string{}) == extraction.snapshot_id) {
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
  std::vector<std::string> outputs;
  bool complete = true;
  for (const auto &fragment_id : extraction.fragment_ids) {
    bool fragment_complete = false;
    for (const auto &[batch_id, batch] : batches) {
      const auto stored = by_id.find(fragment_id);
      if (stored == by_id.end() ||
          stored->second->object_type != "internet-source-fragment") {
        break;
      }
      const auto fragment =
          sources::internet_source_fragment_from_json(stored->second->payload);
      const auto source_item =
          "internet-source-" +
          contracts::parse_typed_id(fragment_id).digest.substr(0U, 16U);
      if (!std::ranges::any_of(batch.dispositions, [&](const auto &entry) {
            return entry.item_id == source_item &&
                   entry.kind == "SOURCE_DOCUMENT";
          })) {
        continue;
      }
      if (fragment.fragment_kind != "ALGORITHM_DESCRIPTION") {
        outputs.push_back(batch_id);
        fragment_complete = true;
        break;
      }
      bool candidate_found = false;
      for (const auto &candidate : candidates) {
        if (candidate.source_fragment_id != fragment_id) {
          continue;
        }
        const auto receipt = by_id.find(candidate.retrieval_receipt_id);
        if (receipt == by_id.end() ||
            receipt->second->object_type != "internet-retrieval-receipt") {
          continue;
        }
        const auto retrieval = internet_knowledge_search_receipt_from_json(
            receipt->second->payload);
        if (retrieval.source_fragment_id == fragment_id &&
            retrieval.snapshot_id == extraction.snapshot_id &&
            retrieval.brain_feed_batch_id == batch_id &&
            retrieval.search_complete) {
          outputs.push_back(receipt->first);
          outputs.push_back(candidate.object_id());
          candidate_found = true;
          break;
        }
      }
      if (candidate_found) {
        outputs.push_back(batch_id);
        fragment_complete = true;
        break;
      }
    }
    if (fragment_complete && completed_fragments) completed_fragments->push_back(fragment_id);
    complete = complete && fragment_complete;
  }
  if (complete) {
    std::ranges::sort(outputs);
    outputs.erase(std::unique(outputs.begin(), outputs.end()), outputs.end());
    return outputs;
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
  return {{"complete", value.complete},
          {"processed_fragments", value.processed_fragments},
          {"total_fragments", value.total_fragments},
          {"completion_output_ids", value.completion_output_ids},
          {"brain_feed_batch", value.brain_feed_batch.batch_signature.empty()
              ? contracts::Json(nullptr) : to_json(value.brain_feed_batch)},
          {"candidates", std::move(candidates)},
          {"result_signature", value.result_signature},
          {"retrieval_receipts", std::move(retrieval)}};
}

} // namespace statewright::egcf
