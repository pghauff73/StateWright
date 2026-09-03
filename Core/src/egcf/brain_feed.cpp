#include "statewright/egcf/brain_feed.hpp"

#include "ledger_support.hpp"
#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/contracts/typed_id.hpp"
#include "statewright/core/file_io.hpp"
#include "statewright/egcf/execution.hpp"
#include "statewright/saa/semantic_units.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <optional>
#include <ranges>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <utility>

namespace statewright::egcf {
namespace {

using Json = contracts::Json;

const std::set<std::string> item_kinds = {
    "ALGORITHM_CANDIDATE", "CLAIM",          "DATASET",
    "EVIDENCE",            "EXPERIMENT_CANDIDATE", "FAILURE",
    "INVARIANT_CANDIDATE", "MEASUREMENT",    "REASONING_CANDIDATE",
    "SEMANTIC_CONCEPT",    "SOURCE_DOCUMENT"};

[[noreturn]] void feed_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument,
                      "brain feed: " + std::move(message));
}

[[nodiscard]] std::string normalized_text(std::string value) {
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

[[nodiscard]] std::string uppercase(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char character) {
    return static_cast<char>(std::toupper(character));
  });
  return value;
}

[[nodiscard]] std::vector<std::string>
canonical_strings(std::vector<std::string> values) {
  std::set<std::string> result;
  for (auto &value : values) {
    value = normalized_text(std::move(value));
    if (!value.empty()) {
      result.insert(std::move(value));
    }
  }
  return {result.begin(), result.end()};
}

[[nodiscard]] bool exact_sha(std::string_view value) {
  return value.size() == 64U &&
         std::ranges::all_of(value, [](unsigned char character) {
           return std::isdigit(character) != 0 ||
                  (character >= 'a' && character <= 'f');
         });
}

[[nodiscard]] const BrainFeedDisposition *find_resolved(
    const std::vector<BrainFeedDisposition> &resolved,
    std::string_view item_id) {
  const auto found = std::ranges::find_if(
      resolved, [item_id](const auto &item) { return item.item_id == item_id; });
  return found == resolved.end() ? nullptr : &*found;
}

[[nodiscard]] std::vector<std::string> resolved_evidence_ids(
    const BrainFeedItem &item,
    const std::vector<BrainFeedDisposition> &resolved) {
  auto result = item.payload.value("evidence_ids", std::vector<std::string>{});
  for (const auto &source : item.evidence_from) {
    const auto *disposition = find_resolved(resolved, source);
    if (disposition == nullptr) {
      continue;
    }
    for (const auto &target : disposition->target_refs) {
      if (target.starts_with("egcf-evidence:sha256:")) {
        result.push_back(target);
      }
    }
  }
  return canonical_strings(std::move(result));
}

[[nodiscard]] std::vector<std::string>
grounding_failures(EgcfStore &store,
                   const std::vector<std::string> &evidence_ids) {
  std::vector<std::string> reasons;
  if (evidence_ids.empty()) {
    reasons.push_back("GROUNDED_EVIDENCE_REQUIRED");
    return reasons;
  }
  for (const auto &evidence_id : evidence_ids) {
    try {
      const auto stored = store.get(evidence_id);
      if (stored.object_type != "egcf-evidence") {
        reasons.push_back("NOT_EVIDENCE:" + evidence_id);
        continue;
      }
      const auto evidence = evidence_artifact_from_json(stored.payload);
      if (!evidence.success.value_or(false)) {
        reasons.push_back("EVIDENCE_NOT_SUCCESSFUL:" + evidence_id);
      }
      if (evidence.simulated) {
        reasons.push_back("SIMULATED_EVIDENCE:" + evidence_id);
      }
      if (!evidence.producer.starts_with("deterministic-") &&
          !evidence.producer.starts_with("human-")) {
        reasons.push_back("EVIDENCE_PRODUCER_NOT_GROUNDED:" + evidence_id);
      }
      if (evidence.method == "reported") {
        reasons.push_back("REPORTED_ONLY_EVIDENCE:" + evidence_id);
      }
    } catch (const std::exception &) {
      reasons.push_back("MISSING_EVIDENCE:" + evidence_id);
    }
  }
  return canonical_strings(std::move(reasons));
}

[[nodiscard]] std::optional<saa::PhysicalDimensionVector>
dimension_from_json(const Json &value) {
  if (value.is_null()) {
    return std::nullopt;
  }
  if (!value.is_array() || value.size() != saa::physical_dimension_count) {
    feed_error("semantic physical_dimension must contain seven integers");
  }
  std::array<int, saa::physical_dimension_count> exponents{};
  for (std::size_t index = 0; index < exponents.size(); ++index) {
    if (!value.at(index).is_number_integer()) {
      feed_error("semantic physical_dimension must contain integers");
    }
    exponents.at(index) = value.at(index).get<int>();
  }
  return saa::PhysicalDimensionVector(exponents);
}

[[nodiscard]] std::string extension_language(const std::filesystem::path &path) {
  const std::string extension = path.extension().string();
  static const std::map<std::string, std::string> languages = {
      {".c", "C"},       {".cc", "C++"},      {".cpp", "C++"},
      {".cxx", "C++"},  {".h", "C"},         {".hh", "C++"},
      {".hpp", "C++"},  {".hxx", "C++"},     {".java", "Java"},
      {".js", "JavaScript"}, {".jsx", "JavaScript"},
      {".md", "Document"}, {".py", "Python"}, {".rs", "Rust"},
      {".sh", "Shell"}, {".ts", "TypeScript"}, {".tsx", "TypeScript"},
      {".txt", "Document"}};
  const auto found = languages.find(extension);
  return found == languages.end() ? "Text" : found->second;
}

[[nodiscard]] bool ignored_path(const std::filesystem::path &relative) {
  static const std::set<std::string> ignored = {
      ".git", ".hg", ".svn", ".ourd-agent", ".venv", "__pycache__",
      "build", "dist", "node_modules", "out", "target", "vendor"};
  return std::ranges::any_of(relative, [](const auto &component) {
    static const std::set<std::string> values = {
        ".git", ".hg", ".svn", ".ourd-agent", ".venv", "__pycache__",
        "build", "dist", "node_modules", "out", "target", "vendor"};
    return values.contains(component.string());
  });
}

[[nodiscard]] bool likely_text(const std::vector<std::byte> &bytes) {
  return std::ranges::none_of(bytes, [](std::byte value) {
    return value == std::byte{0};
  });
}

[[nodiscard]] std::vector<std::pair<std::string, std::size_t>>
symbols(std::string_view text, std::string_view language,
        std::size_t maximum) {
  std::vector<std::pair<std::string, std::size_t>> result;
  const std::regex pattern(
      language == "Python"
          ? R"(^\s*(?:async\s+)?def\s+([A-Za-z_]\w*)\s*\()"
          : language == "JavaScript" || language == "TypeScript"
                ? R"((?:function\s+|(?:const|let|var)\s+)([A-Za-z_$][\w$]*))"
                : R"(^\s*(?:[A-Za-z_]\w*[\s:*&<>]+)+([A-Za-z_]\w*)\s*\([^;{}]*\)\s*(?:const\s*)?(?:noexcept\s*)?\{)");
  std::istringstream input{std::string(text)};
  std::string line;
  std::size_t line_number = 0U;
  while (result.size() < maximum && std::getline(input, line)) {
    ++line_number;
    std::smatch match;
    if (std::regex_search(line, match, pattern) && match.size() > 1U) {
      result.emplace_back(match.str(1), line_number);
    }
  }
  return result;
}

} // namespace

std::string BrainFeedItem::object_id() const {
  return contracts::typed_id("brain-feed-item", to_json(*this));
}

bool BrainFeedDisposition::quarantined() const noexcept {
  return status == "QUARANTINED";
}

bool BrainFeedDisposition::staged() const noexcept {
  return status.starts_with("STAGED_");
}

bool BrainFeedDisposition::admitted() const noexcept {
  return status == "REGISTERED_EVIDENCE" ||
         status == "ADMITTED_SEMANTIC_CONCEPT" ||
         status == "REGISTERED_FAILURE";
}

bool BrainFeedDisposition::duplicate() const noexcept {
  return status.starts_with("DUPLICATE_");
}

std::string BrainFeedDisposition::object_id() const {
  return contracts::typed_id("brain-feed-disposition", to_json(*this));
}

std::string BrainFeedBatchReceipt::object_id() const {
  return contracts::typed_id("brain-feed-batch", to_json(*this));
}

Json to_json(const BrainFeedItem &item) {
  return {{"content_signature", item.content_signature},
          {"depends_on", item.depends_on},
          {"evidence_from", item.evidence_from},
          {"item_id", item.item_id},
          {"item_signature", item.item_signature},
          {"kind", item.kind},
          {"payload", item.payload},
          {"source_path", item.source_path}};
}

Json to_json(const BrainFeedDisposition &disposition) {
  return {{"admitted", disposition.admitted()},
          {"canonical_algorithm_admission_attempted",
           disposition.canonical_algorithm_admission_attempted},
          {"content_signature", disposition.content_signature},
          {"disposition_signature", disposition.disposition_signature},
          {"duplicate", disposition.duplicate()},
          {"duplicate_of_item_signature",
           disposition.duplicate_of_item_signature},
          {"item_id", disposition.item_id},
          {"item_signature", disposition.item_signature},
          {"kind", disposition.kind},
          {"quarantined", disposition.quarantined()},
          {"reasons", disposition.reasons},
          {"route", disposition.route},
          {"staged", disposition.staged()},
          {"status", disposition.status},
          {"target_refs", disposition.target_refs}};
}

Json to_json(const BrainFeedBatchReceipt &receipt) {
  Json dispositions = Json::array();
  for (const auto &item : receipt.dispositions) {
    dispositions.push_back(to_json(item));
  }
  return {{"admitted_count", receipt.admitted_count},
          {"batch_id", receipt.batch_id},
          {"batch_signature", receipt.batch_signature},
          {"canonical_algorithm_admissions",
           receipt.canonical_algorithm_admissions},
          {"dispositions", dispositions},
          {"duplicate_count", receipt.duplicate_count},
          {"item_count", receipt.item_count},
          {"quarantined_count", receipt.quarantined_count},
          {"source_label", receipt.source_label},
          {"source_signature", receipt.source_signature},
          {"staged_count", receipt.staged_count},
          {"status", receipt.status},
          {"strict", receipt.strict}};
}

BrainFeedItem brain_feed_item_from_json(const Json &value) {
  try {
    return {.item_id = value.at("item_id"),
            .kind = value.at("kind"),
            .payload = value.at("payload"),
            .depends_on = value.at("depends_on"),
            .evidence_from = value.at("evidence_from"),
            .source_path = value.at("source_path"),
            .content_signature = value.at("content_signature"),
            .item_signature = value.at("item_signature")};
  } catch (const std::exception &exception) {
    feed_error(std::string("invalid item: ") + exception.what());
  }
}

BrainFeedDisposition brain_feed_disposition_from_json(const Json &value) {
  try {
    return {.item_id = value.at("item_id"),
            .item_signature = value.at("item_signature"),
            .content_signature = value.at("content_signature"),
            .kind = value.at("kind"),
            .status = value.at("status"),
            .route = value.at("route"),
            .target_refs = value.at("target_refs"),
            .reasons = value.at("reasons"),
            .duplicate_of_item_signature =
                value.at("duplicate_of_item_signature"),
            .canonical_algorithm_admission_attempted =
                value.at("canonical_algorithm_admission_attempted"),
            .disposition_signature = value.at("disposition_signature")};
  } catch (const std::exception &exception) {
    feed_error(std::string("invalid disposition: ") + exception.what());
  }
}

BrainFeedBatchReceipt brain_feed_batch_from_json(const Json &value) {
  try {
    std::vector<BrainFeedDisposition> dispositions;
    for (const auto &item : value.at("dispositions")) {
      dispositions.push_back(brain_feed_disposition_from_json(item));
    }
    return {.batch_id = value.at("batch_id"),
            .source_signature = value.at("source_signature"),
            .source_label = value.at("source_label"),
            .strict = value.at("strict"),
            .item_count = value.at("item_count"),
            .admitted_count = value.at("admitted_count"),
            .staged_count = value.at("staged_count"),
            .quarantined_count = value.at("quarantined_count"),
            .duplicate_count = value.at("duplicate_count"),
            .dispositions = std::move(dispositions),
            .status = value.at("status"),
            .canonical_algorithm_admissions =
                value.at("canonical_algorithm_admissions"),
            .batch_signature = value.at("batch_signature")};
  } catch (const std::exception &exception) {
    feed_error(std::string("invalid batch: ") + exception.what());
  }
}

BrainFeedItem make_brain_feed_item(
    std::string item_id, std::string kind, Json payload,
    std::vector<std::string> depends_on,
    std::vector<std::string> evidence_from, std::string source_path) {
  item_id = normalized_text(std::move(item_id));
  kind = uppercase(normalized_text(std::move(kind)));
  if (item_id.empty() || !item_kinds.contains(kind) || !payload.is_object()) {
    feed_error("item requires an id, supported kind, and object payload");
  }
  depends_on = canonical_strings(std::move(depends_on));
  evidence_from = canonical_strings(std::move(evidence_from));
  const Json content_material = {{"depends_on", depends_on},
                                 {"evidence_from", evidence_from},
                                 {"kind", kind},
                                 {"payload", payload},
                                 {"version", brain_feed_version}};
  const std::string content_signature =
      contracts::sha256_json(content_material);
  auto item_material = content_material;
  item_material["item_id"] = item_id;
  return {.item_id = std::move(item_id),
          .kind = std::move(kind),
          .payload = std::move(payload),
          .depends_on = std::move(depends_on),
          .evidence_from = std::move(evidence_from),
          .source_path = std::move(source_path),
          .content_signature = content_signature,
          .item_signature = contracts::sha256_json(item_material)};
}

BrainFeedDisposition make_brain_feed_disposition(
    const BrainFeedItem &item, std::string status, std::string route,
    std::vector<std::string> target_refs, std::vector<std::string> reasons,
    std::string duplicate_of_item_signature) {
  status = uppercase(normalized_text(std::move(status)));
  route = normalized_text(std::move(route));
  target_refs = canonical_strings(std::move(target_refs));
  reasons = canonical_strings(std::move(reasons));
  const Json material = {
      {"canonical_algorithm_admission_attempted", false},
      {"duplicate_of_item_signature", duplicate_of_item_signature},
      {"item_signature", item.item_signature},
      {"reasons", reasons},
      {"route", route},
      {"status", status},
      {"target_refs", target_refs},
      {"version", brain_feed_version}};
  return {.item_id = item.item_id,
          .item_signature = item.item_signature,
          .content_signature = item.content_signature,
          .kind = item.kind,
          .status = std::move(status),
          .route = std::move(route),
          .target_refs = std::move(target_refs),
          .reasons = std::move(reasons),
          .duplicate_of_item_signature =
              std::move(duplicate_of_item_signature),
          .canonical_algorithm_admission_attempted = false,
          .disposition_signature = contracts::sha256_json(material)};
}

BrainFeedBatchReceipt make_brain_feed_batch_receipt(
    std::string batch_id, std::string source_signature,
    std::string source_label, bool strict,
    std::vector<BrainFeedDisposition> dispositions) {
  batch_id = normalized_text(std::move(batch_id));
  if (batch_id.empty() || !exact_sha(source_signature)) {
    feed_error("batch requires an id and lowercase SHA-256 source signature");
  }
  const auto admitted = static_cast<std::size_t>(std::ranges::count_if(
      dispositions, &BrainFeedDisposition::admitted));
  const auto staged = static_cast<std::size_t>(std::ranges::count_if(
      dispositions, &BrainFeedDisposition::staged));
  const auto quarantined = static_cast<std::size_t>(std::ranges::count_if(
      dispositions, &BrainFeedDisposition::quarantined));
  const auto duplicates = static_cast<std::size_t>(std::ranges::count_if(
      dispositions, &BrainFeedDisposition::duplicate));
  std::string status = "BRAIN_FEED_BATCH_ACCEPTED";
  if (quarantined != 0U) {
    status = strict ? "BRAIN_FEED_BATCH_STRICT_FAILURE"
                    : "BRAIN_FEED_BATCH_PARTIAL_WITH_QUARANTINE";
  }
  std::vector<std::string> disposition_signatures;
  for (const auto &item : dispositions) {
    disposition_signatures.push_back(item.disposition_signature);
  }
  const Json material = {
      {"batch_id", batch_id},
      {"canonical_algorithm_admissions", 0},
      {"disposition_signatures", disposition_signatures},
      {"source_label", source_label},
      {"source_signature", source_signature},
      {"status", status},
      {"strict", strict},
      {"version", brain_feed_version}};
  return {.batch_id = std::move(batch_id),
          .source_signature = std::move(source_signature),
          .source_label = normalized_text(std::move(source_label)),
          .strict = strict,
          .item_count = dispositions.size(),
          .admitted_count = admitted,
          .staged_count = staged,
          .quarantined_count = quarantined,
          .duplicate_count = duplicates,
          .dispositions = std::move(dispositions),
          .status = std::move(status),
          .canonical_algorithm_admissions = 0,
          .batch_signature = contracts::sha256_json(material)};
}

BrainFeedProcessor::BrainFeedProcessor(EgcfStore &store)
    : store_(store), evidence_(store), semantics_(store) {}

BrainFeedDisposition BrainFeedProcessor::process_new(
    const BrainFeedItem &item, std::string_view item_ref,
    std::string_view source_signature,
    const std::vector<BrainFeedDisposition> &resolved) {
  std::vector<std::string> dependency_failures;
  for (const auto &dependency : item.depends_on) {
    const auto *disposition = find_resolved(resolved, dependency);
    if (disposition != nullptr && disposition->quarantined()) {
      dependency_failures.push_back("DEPENDENCY_QUARANTINED:" + dependency);
    }
  }
  if (!dependency_failures.empty()) {
    return make_brain_feed_disposition(
        item, "STAGED_DEPENDENCY_REQUIRED", "brain-feed-staging",
        {std::string(item_ref)}, std::move(dependency_failures));
  }

  if (item.kind == "MEASUREMENT" || item.kind == "EVIDENCE") {
    static const std::array<std::string_view, 6> required = {
        "subject_id", "producer", "method", "target", "oracle",
        "independence_group"};
    std::vector<std::string> reasons;
    for (const auto key : required) {
      if (normalized_text(item.payload.value(std::string(key), std::string{}))
              .empty()) {
        reasons.push_back("MISSING_EVIDENCE_METADATA:" + std::string(key));
      }
    }
    const auto producer = item.payload.value("producer", std::string{});
    const auto method = item.payload.value("method", std::string{});
    if (!producer.starts_with("deterministic-") &&
        !producer.starts_with("human-")) {
      reasons.push_back("PRODUCER_MUST_START_DETERMINISTIC_OR_HUMAN");
    }
    if (method == "reported") {
      reasons.push_back("REPORTED_ONLY_MEASUREMENT_IS_NOT_GROUNDED");
    }
    if (item.payload.value("simulated", false)) {
      reasons.push_back("SIMULATED_MEASUREMENT_CANNOT_BE_GROUNDED_REAL_EVIDENCE");
    }
    if (!item.payload.value("success", true)) {
      reasons.push_back("MEASUREMENT_EVIDENCE_MUST_BE_SUCCESSFUL");
    }
    if (!reasons.empty()) {
      return make_brain_feed_disposition(
          item, "STAGED_EVIDENCE_METADATA_REQUIRED", "brain-feed-staging",
          {std::string(item_ref)}, std::move(reasons));
    }
    const Json content = item.payload.contains("content")
                             ? item.payload.at("content")
                             : item.payload.value("value", Json(nullptr));
    const auto evidence_id = evidence_.collect(
        {.subject_id = item.payload.at("subject_id"),
         .content = content,
         .category = item.payload.value("category", "measurement"),
         .producer = producer,
         .method = method,
         .source_snapshot_hash = item.payload.value(
             "source_snapshot_hash", std::string(source_signature)),
         .target = item.payload.at("target"),
         .oracle = item.payload.at("oracle"),
         .environment = item.payload.value("environment", Json::object()),
         .command_id = item.payload.value("command_id", "brain.feed@1"),
         .algorithm_id = item.payload.value("algorithm_id", std::string{}),
         .claim_ids = item.payload.value("claim_ids", std::vector<std::string>{}),
         .requirement_ids = item.payload.value(
             "requirement_ids", std::vector<std::string>{}),
         .success = true,
         .limitations = item.payload.value(
             "limitations", std::vector<std::string>{}),
         .independence_group = item.payload.at("independence_group"),
         .simulated = false,
         .path = item.source_path});
    return make_brain_feed_disposition(
        item, "REGISTERED_EVIDENCE", "egcf-evidence", {evidence_id},
        {"GROUNDED_EVIDENCE_REGISTERED"});
  }

  const auto evidence_ids = resolved_evidence_ids(item, resolved);
  if (item.kind == "SEMANTIC_CONCEPT") {
    const auto semantic_status = uppercase(item.payload.value(
        "semantic_status", std::string("UNRESOLVED_SEMANTICS")));
    if (semantic_status != "SEMANTICALLY_RESOLVED") {
      return make_brain_feed_disposition(
          item, "STAGED_SEMANTIC_RESOLUTION_REQUIRED", "brain-feed-staging",
          {std::string(item_ref)}, {"SEMANTIC_STATUS:" + semantic_status});
    }
    const auto reasons = grounding_failures(store_, evidence_ids);
    if (!reasons.empty()) {
      return make_brain_feed_disposition(
          item, "STAGED_EVIDENCE_REQUIRED", "brain-feed-staging",
          {std::string(item_ref)}, reasons);
    }
    std::optional<saa::PhysicalDimensionVector> dimension;
    if (item.payload.contains("physical_dimension")) {
      dimension = dimension_from_json(item.payload.at("physical_dimension"));
    }
    std::optional<saa::PhysicalUnit> unit;
    if (item.payload.contains("canonical_unit") &&
        item.payload.at("canonical_unit").is_string() &&
        !item.payload.at("canonical_unit").get<std::string>().empty()) {
      unit = saa::physical_unit(
          item.payload.at("canonical_unit").get<std::string>());
    }
    const auto semantic_concept = saa::make_semantic_concept(
        item.payload.value("name", std::string{}),
        item.payload.value("meaning", std::string{}),
        item.payload.value("domain", std::string{}),
        item.payload.value("quantity_kind", std::string{}),
        item.payload.value("aliases", std::vector<std::string>{}), dimension,
        unit, evidence_ids, semantic_status);
    const auto concept_id = semantics_.admit_concept(semantic_concept);
    return make_brain_feed_disposition(
        item, "ADMITTED_SEMANTIC_CONCEPT", "semantic-ontology", {concept_id},
        {"MEANING_AND_EVIDENCE_RESOLVED"});
  }

  if (item.kind == "FAILURE") {
    const auto reasons = grounding_failures(store_, evidence_ids);
    if (!reasons.empty()) {
      return make_brain_feed_disposition(
          item, "STAGED_EVIDENCE_REQUIRED", "brain-feed-staging",
          {std::string(item_ref)}, reasons);
    }
    const FailureRecord failure = {
        .subject_id = item.payload.value("subject_id", item.item_id),
        .expected = item.payload.value("expected", std::string{}),
        .observed = item.payload.value("observed", std::string{}),
        .active_dimension =
            item.payload.value("active_dimension", "observation"),
        .frozen_dimensions = item.payload.value(
            "frozen_dimensions", std::vector<std::string>{}),
        .evidence_ids = evidence_ids,
        .retry_count = item.payload.value("retry_count", 0),
        .status = item.payload.value("status", "FAILED"),
        .created_at = item.payload.value("created_at", ledger_support::utc_now())};
    const auto failure_id = store_.register_record(
        {.object_type = "failure", .payload = to_json(failure)},
        "saa_brain_feed_failure_registered");
    return make_brain_feed_disposition(
        item, "REGISTERED_FAILURE", "egcf-failure", {failure_id},
        {"GROUNDED_FAILURE_REGISTERED"});
  }

  std::vector<std::string> validation;
  std::string status;
  if (item.kind == "ALGORITHM_CANDIDATE") {
    if (item.payload.value("name", std::string{}).empty()) {
      validation.push_back("MISSING_ALGORITHM_NAME");
    }
    if (!item.payload.contains("inputs")) {
      validation.push_back("MISSING_ALGORITHM_INPUTS");
    }
    if (!item.payload.contains("outputs")) {
      validation.push_back("MISSING_ALGORITHM_OUTPUTS");
    }
    if (!item.payload.contains("equation") &&
        !item.payload.contains("implementation") &&
        !item.payload.contains("procedure") &&
        !item.payload.contains("graph")) {
      validation.push_back("MISSING_ALGORITHM_REPRESENTATION");
    }
    status = "STAGED_ALGORITHM_CANDIDATE_QUALIFICATION_REQUIRED";
  } else if (item.kind == "REASONING_CANDIDATE") {
    if (item.payload.value("name", std::string{}).empty()) {
      validation.push_back("MISSING_REASONING_NAME");
    }
    if (!item.payload.contains("operators") &&
        !item.payload.contains("procedure") &&
        !item.payload.contains("graph")) {
      validation.push_back("MISSING_REASONING_PROCEDURE");
    }
    status = "STAGED_REASONING_CANDIDATE_QUALIFICATION_REQUIRED";
  } else if (item.kind == "EXPERIMENT_CANDIDATE") {
    if (item.payload.value("objective", std::string{}).empty()) {
      validation.push_back("MISSING_EXPERIMENT_OBJECTIVE");
    }
    if (!item.payload.contains("metrics")) {
      validation.push_back("MISSING_EXPERIMENT_METRICS");
    }
    status = "STAGED_EXPERIMENT_CANDIDATE_REVIEW_REQUIRED";
  } else if (item.kind == "DATASET") {
    status = "STAGED_DATASET_QUALIFICATION_REQUIRED";
  } else if (item.kind == "CLAIM") {
    status = "STAGED_CLAIM_VERIFICATION_REQUIRED";
  } else if (item.kind == "INVARIANT_CANDIDATE") {
    status = "STAGED_INVARIANT_VALIDATION_REQUIRED";
  } else {
    status = "STAGED_SOURCE_DOCUMENT_REVIEW_REQUIRED";
  }
  if (!validation.empty()) {
    return make_brain_feed_disposition(
        item, "QUARANTINED", "brain-feed-quarantine",
        {std::string(item_ref)}, std::move(validation));
  }
  return make_brain_feed_disposition(
      item, std::move(status), "brain-feed-staging", {std::string(item_ref)},
      {"QUALIFICATION_OR_REVIEW_REQUIRED"});
}

BrainFeedBatchReceipt BrainFeedProcessor::feed(
    std::string batch_id, std::string source_signature,
    std::string source_label, std::vector<BrainFeedItem> items, bool strict) {
  if (items.size() > maximum_brain_feed_items) {
    feed_error("batch item limit exceeded");
  }
  std::map<std::string, BrainFeedItem> pending;
  for (auto &item : items) {
    if (!pending.emplace(item.item_id, item).second) {
      feed_error("duplicate item id in batch: " + item.item_id);
    }
  }
  std::vector<BrainFeedDisposition> resolved;
  const auto existing = dispositions();
  std::map<std::string, BrainFeedDisposition> exact;
  std::map<std::string, BrainFeedDisposition> content;
  for (const auto &item : existing) {
    exact.emplace(item.item_signature, item);
    content.emplace(item.content_signature, item);
  }
  for (const auto &item : items) {
    std::vector<std::string> missing;
    for (const auto &reference : item.depends_on) {
      if (!pending.contains(reference)) {
        missing.push_back("MISSING_BATCH_REFERENCE:" + reference);
      }
    }
    for (const auto &reference : item.evidence_from) {
      if (!pending.contains(reference)) {
        missing.push_back("MISSING_BATCH_REFERENCE:" + reference);
      }
    }
    if (!missing.empty()) {
      const auto item_ref = store_.register_record(
          {.object_type = "brain-feed-item", .payload = to_json(item)},
          "saa_brain_feed_item_registered");
      auto disposition = make_brain_feed_disposition(
          item, "QUARANTINED", "brain-feed-quarantine", {item_ref},
          std::move(missing));
      static_cast<void>(store_.register_record(
          {.object_type = "brain-feed-disposition",
           .payload = to_json(disposition)},
          "saa_brain_feed_disposition_registered"));
      resolved.push_back(disposition);
      pending.erase(item.item_id);
    }
  }
  while (!pending.empty()) {
    bool progressed = false;
    for (auto iterator = pending.begin(); iterator != pending.end();) {
      const auto &item = iterator->second;
      const auto ready = [&resolved](const std::string &reference) {
        return find_resolved(resolved, reference) != nullptr;
      };
      if (!std::ranges::all_of(item.depends_on, ready) ||
          !std::ranges::all_of(item.evidence_from, ready)) {
        ++iterator;
        continue;
      }
      const auto item_ref = store_.register_record(
          {.object_type = "brain-feed-item", .payload = to_json(item)},
          "saa_brain_feed_item_registered");
      BrainFeedDisposition disposition;
      if (const auto exact_match = exact.find(item.item_signature);
          exact_match != exact.end()) {
        disposition = make_brain_feed_disposition(
            item, "DUPLICATE_EXACT_ITEM", "brain-feed-deduplication",
            exact_match->second.target_refs,
            {"EXACT_ITEM_ALREADY_PROCESSED"},
            item.item_signature);
      } else if (const auto content_match =
                     content.find(item.content_signature);
                 content_match != content.end()) {
        disposition = make_brain_feed_disposition(
            item, "DUPLICATE_CONTENT", "brain-feed-deduplication",
            content_match->second.target_refs,
            {"EQUIVALENT_CONTENT_ALREADY_PROCESSED"},
            content_match->second.item_signature);
      } else {
        try {
          disposition = process_new(item, item_ref, source_signature, resolved);
        } catch (const std::exception &exception) {
          disposition = make_brain_feed_disposition(
              item, "QUARANTINED", "brain-feed-quarantine", {item_ref},
              {exception.what()});
        }
      }
      static_cast<void>(store_.register_record(
          {.object_type = "brain-feed-disposition",
           .payload = to_json(disposition)},
          "saa_brain_feed_disposition_registered"));
      resolved.push_back(disposition);
      exact.emplace(item.item_signature, disposition);
      content.emplace(item.content_signature, disposition);
      iterator = pending.erase(iterator);
      progressed = true;
    }
    if (!progressed) {
      for (const auto &[unused, item] : pending) {
        static_cast<void>(unused);
        const auto item_ref = store_.register_record(
            {.object_type = "brain-feed-item", .payload = to_json(item)},
            "saa_brain_feed_item_registered");
        const auto disposition = make_brain_feed_disposition(
            item, "QUARANTINED", "brain-feed-quarantine", {item_ref},
            {"CYCLIC_OR_UNRESOLVED_BATCH_DEPENDENCY"});
        static_cast<void>(store_.register_record(
            {.object_type = "brain-feed-disposition",
             .payload = to_json(disposition)},
            "saa_brain_feed_disposition_registered"));
        resolved.push_back(disposition);
      }
      pending.clear();
    }
  }
  std::vector<BrainFeedDisposition> ordered;
  ordered.reserve(items.size());
  for (const auto &item : items) {
    const auto *disposition = find_resolved(resolved, item.item_id);
    if (disposition == nullptr) {
      feed_error("internal disposition loss for " + item.item_id);
    }
    ordered.push_back(*disposition);
  }
  auto receipt = make_brain_feed_batch_receipt(
      std::move(batch_id), std::move(source_signature),
      std::move(source_label), strict, std::move(ordered));
  static_cast<void>(store_.register_record(
      {.object_type = "brain-feed-batch", .payload = to_json(receipt)},
      "saa_brain_feed_batch_recorded"));
  return receipt;
}

std::vector<BrainFeedDisposition> BrainFeedProcessor::dispositions() {
  std::vector<BrainFeedDisposition> result;
  for (const auto &stored : store_.list("brain-feed-disposition")) {
    result.push_back(brain_feed_disposition_from_json(stored.payload));
  }
  return result;
}

std::vector<BrainFeedBatchReceipt> BrainFeedProcessor::batches() {
  std::vector<BrainFeedBatchReceipt> result;
  for (const auto &stored : store_.list("brain-feed-batch")) {
    result.push_back(brain_feed_batch_from_json(stored.payload));
  }
  return result;
}

Json to_json(const RepositoryFeedPlan &plan) {
  return {{"file_count", plan.file_count},
          {"language_counts", plan.language_counts},
          {"repository_name", plan.repository_name},
          {"repository_signature", plan.repository_signature},
          {"skipped", plan.skipped},
          {"source_root", plan.source_root.generic_string()},
          {"symbol_count", plan.symbol_count},
          {"total_bytes", plan.total_bytes}};
}

RepositoryFeedPlan scan_repository(const std::filesystem::path &source,
                                   const RepositoryScanPolicy &policy) {
  if (policy.max_files == 0U || policy.max_total_bytes == 0U ||
      policy.max_file_bytes == 0U || policy.max_symbols == 0U) {
    feed_error("repository scan limits must be positive");
  }
  const auto root = std::filesystem::weakly_canonical(source);
  if (!std::filesystem::is_directory(root)) {
    feed_error("repository source must be a directory");
  }
  struct File final {
    std::string relative;
    std::string language;
    std::string digest;
    std::string text;
    std::size_t size = 0U;
  };
  std::vector<File> files;
  std::vector<Json> skipped;
  std::size_t total_bytes = 0U;
  std::vector<std::filesystem::path> paths;
  for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
    const auto relative = std::filesystem::relative(entry.path(), root);
    if (ignored_path(relative)) {
      continue;
    }
    if (entry.is_regular_file()) {
      paths.push_back(entry.path());
    }
  }
  std::ranges::sort(paths);
  for (const auto &path : paths) {
    const auto relative = std::filesystem::relative(path, root).generic_string();
    if (files.size() >= policy.max_files) {
      skipped.push_back({{"path", relative}, {"reason", "MAX_FILES"}});
      continue;
    }
    const auto size = static_cast<std::size_t>(std::filesystem::file_size(path));
    if (size > policy.max_file_bytes) {
      skipped.push_back({{"path", relative}, {"reason", "MAX_FILE_BYTES"}});
      continue;
    }
    if (total_bytes + size > policy.max_total_bytes) {
      skipped.push_back({{"path", relative}, {"reason", "MAX_TOTAL_BYTES"}});
      continue;
    }
    const auto bytes = core::read_bytes(path);
    if (!likely_text(bytes)) {
      skipped.push_back({{"path", relative}, {"reason", "BINARY"}});
      continue;
    }
    std::string text;
    text.reserve(bytes.size());
    for (const auto byte : bytes) {
      text.push_back(static_cast<char>(byte));
    }
    const auto language = extension_language(path);
    if (!policy.include_docs && language == "Document") {
      continue;
    }
    files.push_back({.relative = relative,
                     .language = language,
                     .digest = contracts::sha256_bytes(bytes),
                     .text = std::move(text),
                     .size = size});
    total_bytes += size;
  }
  Json source_material = Json::array();
  Json language_counts = Json::object();
  for (const auto &file : files) {
    source_material.push_back({{"language", file.language},
                               {"path", file.relative},
                               {"sha256", file.digest},
                               {"size", file.size}});
    language_counts[file.language] =
        language_counts.value(file.language, std::size_t{0}) + 1U;
  }
  const auto signature = contracts::sha256_json(
      {{"files", source_material}, {"version", "repository-brain-feed-v1"}});
  std::vector<BrainFeedItem> items;
  const auto name = root.filename().empty() ? root.string()
                                             : root.filename().string();
  items.push_back(make_brain_feed_item(
      "repository-summary", "SOURCE_DOCUMENT",
      {{"file_count", files.size()},
       {"language_counts", language_counts},
       {"name", name},
       {"repository_signature", signature},
       {"scan_status", skipped.empty() ? "COMPLETE" : "BOUNDED_PARTIAL"},
       {"total_bytes", total_bytes}}));
  std::size_t symbol_count = 0U;
  for (const auto &file : files) {
    const auto evidence_item_id = "file:" + file.relative;
    items.push_back(make_brain_feed_item(
        evidence_item_id, "EVIDENCE",
        {{"category", "source"},
         {"content", {{"language", file.language},
                      {"path", file.relative},
                      {"sha256", file.digest},
                      {"size", file.size}}},
         {"independence_group", "repository-file:" + file.relative},
         {"method", "content-addressed static repository scan"},
         {"oracle", "sha256"},
         {"producer", "deterministic-statewright-repository-scanner"},
         {"simulated", false},
         {"source_snapshot_hash", signature},
         {"subject_id", "repository:sha256:" + signature},
         {"success", true},
         {"target", file.relative}},
        {}, {}, file.relative));
    const auto remaining = policy.max_symbols - symbol_count;
    if (remaining == 0U) {
      continue;
    }
    for (const auto &[symbol, line] :
         symbols(file.text, file.language, remaining)) {
      const bool is_test = file.relative.find("test") != std::string::npos ||
                           symbol.starts_with("test");
      if (is_test && !policy.include_tests) {
        continue;
      }
      const auto kind = is_test ? "EXPERIMENT_CANDIDATE"
                                : "ALGORITHM_CANDIDATE";
      Json payload = is_test
                         ? Json{{"metrics", {"pass", "failure"}},
                                {"objective", "execute " + symbol},
                                {"source", file.relative},
                                {"source_line", line}}
                         : Json{{"inputs", Json::array()},
                                {"meanings", {{"status", "UNRESOLVED_FROM_SOURCE_CODE"}}},
                                {"name", symbol},
                                {"outputs", Json::array()},
                                {"procedure", "static source symbol"},
                                {"source", file.relative},
                                {"source_line", line}};
      items.push_back(make_brain_feed_item(
          "symbol:" + file.relative + ":" + symbol, kind, std::move(payload),
          {}, {evidence_item_id}, file.relative));
      ++symbol_count;
    }
  }
  return {.source_root = root,
          .repository_name = name,
          .repository_signature = signature,
          .file_count = files.size(),
          .symbol_count = symbol_count,
          .total_bytes = total_bytes,
          .language_counts = std::move(language_counts),
          .skipped = std::move(skipped),
          .items = std::move(items)};
}

BrainFeedBatchReceipt feed_repository(BrainFeedProcessor &processor,
                                      const std::filesystem::path &source,
                                      const RepositoryScanPolicy &policy,
                                      bool strict) {
  auto plan = scan_repository(source, policy);
  return processor.feed(
      "repo-" + plan.repository_name + "-" +
          plan.repository_signature.substr(0U, 12U),
      plan.repository_signature,
      "Static repository feed " + plan.repository_name,
      std::move(plan.items), strict);
}

} // namespace statewright::egcf
