#include "statewright/sources/extraction.hpp"

#include "statewright/common/error.hpp"
#include "statewright/common/utf8.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/sources/fetch.hpp"
#include "statewright/sources/watchlist.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace statewright::sources {
namespace {

[[noreturn]] void extraction_error(std::string message) {
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

std::string text_from_bytes(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

std::string classify_text(std::string_view text, bool heading, bool table_row,
                          bool code_block) {
  if (code_block) {
    return "CODE_BLOCK";
  }
  if (heading) {
    return "HEADING";
  }
  if (table_row) {
    return "TABLE_ROW";
  }
  const std::string normalized = lower(std::string(text));
  if (normalized.find("retract") != std::string::npos ||
      normalized.find("withdrawn") != std::string::npos) {
    return "RETRACTION";
  }
  if (normalized.find("correction") != std::string::npos ||
      normalized.find("erratum") != std::string::npos) {
    return "CORRECTION";
  }
  if (normalized.find("version ") != std::string::npos ||
      normalized.find("revision ") != std::string::npos) {
    return "VERSION_MARKER";
  }
  if (normalized.find("algorithm") != std::string::npos ||
      normalized.find("procedure") != std::string::npos ||
      normalized.find("input:") != std::string::npos ||
      normalized.find("output:") != std::string::npos) {
    return "ALGORITHM_DESCRIPTION";
  }
  if (normalized.find("doi:") != std::string::npos ||
      normalized.find("https://") != std::string::npos ||
      normalized.find("http://") != std::string::npos) {
    return "CITATION";
  }
  if (normalized.find('$') != std::string::npos ||
      normalized.find("\\(") != std::string::npos ||
      normalized.find("\\[") != std::string::npos) {
    return "MATH_EXPRESSION";
  }
  return "TEXT";
}

void append_fragment(InternetExtractionResult &result,
                     const InternetExtractionLimits &limits,
                     std::string_view snapshot_id, std::string kind,
                     std::size_t byte_start, std::size_t byte_end,
                     std::string selector, std::string text,
                     std::string language = {},
                     contracts::Json metadata = contracts::Json::object()) {
  text = trim(std::move(text));
  if (text.empty()) {
    return;
  }
  if (result.fragments.size() >= limits.maximum_fragments) {
    result.receipt.truncated = true;
    result.receipt.diagnostics.push_back("MAXIMUM_FRAGMENT_COUNT_REACHED");
    return;
  }
  if (text.size() > limits.maximum_fragment_bytes) {
    result.receipt.rejected_fragments.push_back(std::move(selector) +
                                                ":FRAGMENT_TOO_LARGE");
    return;
  }
  const std::string normalized = lower(text);
  metadata["quoted_source"] = true;
  metadata["prompt_injection_like"] =
      normalized.find("ignore previous instructions") != std::string::npos ||
      normalized.find("system prompt") != std::string::npos ||
      normalized.find("call this tool") != std::string::npos;
  InternetSourceFragment fragment;
  fragment.snapshot_id = std::string(snapshot_id);
  fragment.fragment_kind = std::move(kind);
  fragment.byte_start = byte_start;
  fragment.byte_end = byte_end;
  fragment.selector = std::move(selector);
  fragment.text = std::move(text);
  fragment.language = std::move(language);
  fragment.metadata = std::move(metadata);
  result.fragments.push_back(canonical_source_fragment(std::move(fragment)));
}

std::size_t json_depth(const contracts::Json &value, std::size_t depth,
                       std::size_t maximum) {
  if (depth > maximum) {
    extraction_error("JSON nesting exceeds extraction policy");
  }
  std::size_t result = depth;
  if (value.is_array()) {
    for (const auto &entry : value) {
      result = std::max(result, json_depth(entry, depth + 1U, maximum));
    }
  } else if (value.is_object()) {
    for (const auto &[name, entry] : value.items()) {
      static_cast<void>(name);
      result = std::max(result, json_depth(entry, depth + 1U, maximum));
    }
  }
  return result;
}

void extract_plain(InternetExtractionResult &result,
                   const InternetExtractionLimits &limits,
                   std::string_view snapshot_id, std::string_view text) {
  std::size_t offset = 0U;
  bool code_block = false;
  std::string language;
  std::size_t line_number = 0U;
  while (offset < text.size() && !result.receipt.truncated) {
    const auto newline = text.find('\n', offset);
    const std::size_t end =
        newline == std::string_view::npos ? text.size() : newline;
    const std::string line(text.substr(offset, end - offset));
    const std::string stripped = trim(line);
    if (stripped.starts_with("```")) {
      if (!code_block) {
        language = trim(stripped.substr(3U));
      } else {
        language.clear();
      }
      code_block = !code_block;
    } else {
      const bool heading = stripped.starts_with('#');
      const bool table = stripped.find('|') != std::string::npos;
      append_fragment(result, limits, snapshot_id,
                      classify_text(stripped, heading, table, code_block),
                      offset, end, "line:" + std::to_string(line_number), line,
                      language);
    }
    offset = newline == std::string_view::npos ? text.size() : newline + 1U;
    ++line_number;
  }
  if (code_block) {
    result.receipt.diagnostics.push_back("UNCLOSED_CODE_FENCE");
  }
}

std::string decoded_html_text(std::string_view text) {
  std::string result;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '&') {
      const auto end = text.find(';', i + 1);
      if (end != std::string_view::npos && end - i <= 12U) {
        const auto entity = text.substr(i, end - i + 1U);
        static const std::map<std::string_view, std::string_view> entities = {
            {"&amp;", "&"},  {"&lt;", "<"},  {"&gt;", ">"},  {"&quot;", "\""},
            {"&apos;", "'"}, {"&#39;", "'"}, {"&nbsp;", " "}};
        if (const auto found = entities.find(entity); found != entities.end()) {
          result += found->second;
          i = end;
          continue;
        }
      }
    }
    result += text[i];
  }
  return result;
}

// Preserve contiguous block context across inline markup. Raw offsets continue
// to address the captured bytes; decoded text is a deterministic projection.
void extract_html(InternetExtractionResult &result,
                  const InternetExtractionLimits &limits,
                  std::string_view snapshot_id, std::string_view text) {
  std::vector<std::string> stack;
  const std::string normalized_html = lower(std::string(text));
  std::string block_tag;
  std::string block_text;
  std::size_t block_start = 0U;
  std::size_t block_depth = 0U;
  std::size_t sequence = 0U;
  const auto flush = [&](std::size_t end) {
    if (block_text.empty())
      return;
    const bool heading = block_tag.size() == 2U && block_tag.front() == 'h';
    std::string kind =
        classify_text(block_text, heading, block_tag == "tr", false);
    if (block_tag == "pre" && kind == "TEXT")
      kind = "CODE_BLOCK";
    if (block_tag == "math")
      kind = "MATH_EXPRESSION";
    contracts::Json metadata = {{"html_tag", block_tag}};
    if (block_tag == "math") {
      metadata["raw_mathml"] =
          std::string(text.substr(block_start, end - block_start));
    }
    append_fragment(result, limits, snapshot_id, kind, block_start, end,
                    "html:block:" + std::to_string(sequence++), block_text, {},
                    metadata);
    block_text.clear();
  };
  for (std::size_t position = 0;
       position < text.size() && !result.receipt.truncated;) {
    const auto opening = text.find('<', position);
    const auto end_text =
        opening == std::string_view::npos ? text.size() : opening;
    if (end_text > position) {
      if (block_text.empty() && block_tag.empty())
        block_start = position;
      block_text +=
          decoded_html_text(text.substr(position, end_text - position));
    }
    if (opening == std::string_view::npos) {
      flush(text.size());
      break;
    }
    if (text.substr(opening).starts_with("<!--")) {
      const auto end_comment = text.find("-->", opening + 4U);
      if (end_comment == std::string_view::npos)
        break;
      position = end_comment + 3U;
      continue;
    }
    auto end = opening + 1U;
    char quote = 0;
    for (; end < text.size(); ++end) {
      const char c = text[end];
      if (quote != 0) {
        if (c == quote)
          quote = 0;
      } else if (c == '\'' || c == '"')
        quote = c;
      else if (c == '>')
        break;
    }
    if (end == text.size()) {
      result.receipt.rejected_fragments.push_back("html:MALFORMED_TAG");
      break;
    }
    std::string tag =
        lower(trim(std::string(text.substr(opening + 1U, end - opening - 1U))));
    const bool closing = tag.starts_with('/');
    if (closing)
      tag.erase(tag.begin());
    const auto separator = tag.find_first_of(" \t\r\n/");
    const std::string name = tag.substr(0, separator);
    if (!closing &&
        (name == "script" || name == "style" || name == "noscript")) {
      result.receipt.rejected_fragments.push_back("html:" + name +
                                                  ":NON_EXECUTABLE");
      const auto hidden_end = normalized_html.find("</" + name, end + 1U);
      if (hidden_end == std::string::npos)
        break;
      const auto final = text.find('>', hidden_end);
      if (final == std::string_view::npos)
        break;
      position = final + 1U;
      continue;
    }
    const bool boundary = name == "p" || name == "li" || name == "pre" ||
                          name == "tr" || name == "math" ||
                          (name.size() == 2U && name[0] == 'h' &&
                           name[1] >= '1' && name[1] <= '6');
    const bool void_tag = tag.ends_with('/') || name == "br" || name == "hr" ||
                          name == "img" || name == "meta" || name == "link" ||
                          name == "input" || name == "wbr" ||
                          name.starts_with('!');
    if (!closing) {
      if (boundary && block_tag.empty()) {
        flush(opening);
        block_start = opening;
        block_tag = name;
        block_depth = stack.size() + 1U;
      }
      if (name == "br")
        block_text += '\n';
      if (!void_tag) {
        stack.push_back(name);
        if (stack.size() > limits.maximum_nesting_depth)
          extraction_error("HTML nesting exceeds extraction policy");
      }
    } else {
      if (name == "td" || name == "th")
        block_text += '\t';
      if (!block_tag.empty() && name == block_tag &&
          stack.size() == block_depth) {
        flush(end + 1U);
        block_tag.clear();
      }
      const auto found = std::find(stack.rbegin(), stack.rend(), name);
      if (found != stack.rend())
        stack.resize(stack.size() -
                     static_cast<std::size_t>(found - stack.rbegin()) - 1U);
    }
    position = end + 1U;
  }
  flush(text.size());
}

std::string doi_url(std::string doi) {
  doi = lower(trim(std::move(doi)));
  if (doi.starts_with("https://doi.org/"))
    doi.erase(0U, 16U);
  if (!doi.starts_with("10.") || doi.find('/') == std::string::npos)
    return {};
  std::string encoded;
  constexpr std::string_view hex = "0123456789ABCDEF";
  for (const char character : doi) {
    const auto c = static_cast<unsigned char>(character);
    if (std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == '/' ||
        c == '~')
      encoded += static_cast<char>(c);
    else {
      encoded += '%';
      encoded += hex[c >> 4U];
      encoded += hex[c & 15U];
    }
  }
  return "https://doi.org/" + encoded;
}

void extract_discovery(InternetExtractionResult &result,
                       const InternetExtractionLimits &limits,
                       std::string_view snapshot_id,
                       const contracts::Json &value, std::size_t input_size,
                       std::string_view strategy) {
  const bool crossref = strategy == "crossref-json";
  const auto &items = crossref ? value.at("message").at("items")
                               : value.at("resultList").at("result");
  if (!items.is_array())
    extraction_error("discovery response must contain an item array");
  std::set<std::string> identities;
  std::set<std::string> urls;
  for (std::size_t index = 0U; index < items.size(); ++index) {
    if (result.fragments.size() >= limits.maximum_fragments ||
        index >= std::min<std::size_t>(limits.maximum_fragments, 1024U) * 4U) {
      result.receipt.truncated = true;
      result.receipt.diagnostics.push_back("MAXIMUM_DISCOVERY_ITEMS_REACHED");
      break;
    }
    const auto selector =
        (crossref ? "json:/message/items/" : "json:/resultList/result/") +
        std::to_string(index);
    try {
      const auto &item = items.at(index);
      std::string doi = item.value(crossref ? "DOI" : "doi", std::string{});
      std::string url;
      if (crossref) {
        url = item.value("URL", std::string{});
        if (item.contains("link") && item.at("link").is_array()) {
          for (const auto &link : item.at("link")) {
            const auto mime = link.value("content-type", std::string{});
            if (mime == "text/html" || mime == "text/plain") {
              url = link.at("URL").get<std::string>();
              break;
            }
          }
        }
      } else if (item.contains("fullTextUrlList")) {
        for (const auto &link : item.at("fullTextUrlList").at("fullTextUrl")) {
          if (lower(link.value("documentStyle", std::string{})) == "html") {
            url = link.at("url").get<std::string>();
            break;
          }
        }
      }
      if (url.empty())
        url = doi_url(doi);
      auto policy = InternetSourcePolicy{};
      const auto parsed = parse_and_validate_url(url, policy);
      const std::string identity =
          doi.empty() ? parsed.canonical_url : doi_url(doi);
      if (identity.empty())
        extraction_error("invalid discovery DOI");
      if (!identities.insert(identity).second ||
          !urls.insert(parsed.canonical_url).second)
        continue;
      std::string title;
      if (crossref && item.contains("title") && item.at("title").is_array() &&
          !item.at("title").empty())
        title = item.at("title").front().get<std::string>();
      else
        title = item.value("title", std::string{});
      contracts::Json proposal = {
          {"canonical_url", parsed.canonical_url},
          {"doi", lower(doi)},
          {"enabled", false},
          {"name",
           "discovered-" + contracts::sha256_text(identity).substr(0, 20)},
          {"purpose", "authoritative-evidence"},
          {"title", title},
          {"claimed_publisher",
           item.value(crossref ? "publisher" : "journalTitle", std::string{})},
          {"license_status", "review-required"},
          {"robots_status", "pending"},
          {"source_snapshot_id", snapshot_id},
          {"source_selector", selector}};
      append_fragment(result, limits, snapshot_id, "CITATION", 0U, input_size,
                      selector, title + "\n" + parsed.canonical_url, {},
                      {{"discovery_watch_proposal", proposal},
                       {"json_pointer", selector.substr(5U)}});
    } catch (const std::exception &) {
      result.receipt.rejected_fragments.push_back(selector +
                                                  ":INVALID_DISCOVERY_ITEM");
    }
  }
}

} // namespace

InternetPolicyAssessment
assess_internet_source(const InternetSourceSnapshot &snapshot_value,
                       const InternetFetchReceipt &fetch_receipt_value,
                       const InternetSourcePolicy &source_policy_value,
                       std::span<const std::byte> bytes, bool robots_allowed,
                       std::string license_classification) {
  const auto snapshot = canonical_source_snapshot(snapshot_value);
  const auto receipt = canonical_fetch_receipt(fetch_receipt_value);
  const auto policy = canonical_source_policy(source_policy_value);
  InternetPolicyAssessment assessment;
  assessment.snapshot_id = snapshot.object_id();
  assessment.fetch_receipt_id = receipt.object_id();
  assessment.source_policy_id = policy.object_id();
  assessment.public_address_valid =
      !receipt.resolved_addresses.empty() &&
      std::ranges::all_of(receipt.resolved_addresses, [&](const auto &address) {
        return is_public_address(address, policy.allow_loopback_for_tests);
      });
  assessment.redirects_valid = true;
  try {
    for (const auto &redirect : receipt.redirect_chain) {
      static_cast<void>(parse_and_validate_url(redirect, policy));
    }
  } catch (const common::Error &) {
    assessment.redirects_valid = false;
  }
  assessment.robots_allowed =
      robots_allowed || !policy.require_robots_permission;
  assessment.license_classification = std::move(license_classification);
  const bool known_license = !assessment.license_classification.empty() &&
                             assessment.license_classification != "UNKNOWN";
  assessment.mime_valid =
      std::find(policy.accepted_mime_types.begin(),
                policy.accepted_mime_types.end(),
                snapshot.content_type) != policy.accepted_mime_types.end();
  const std::string text = text_from_bytes(bytes);
  assessment.encoding_valid = common::is_valid_utf8(text);
  assessment.credential_free = true;
  assessment.size_valid =
      bytes.size() == snapshot.body_size &&
      receipt.compressed_bytes <= policy.maximum_response_bytes &&
      receipt.decompressed_bytes <= policy.maximum_decompressed_bytes;
  if (!assessment.public_address_valid) {
    assessment.blocking_reasons.push_back("PUBLIC_ADDRESS_INVALID");
  }
  if (!assessment.redirects_valid) {
    assessment.blocking_reasons.push_back("REDIRECT_INVALID");
  }
  if (!assessment.robots_allowed) {
    assessment.blocking_reasons.push_back("ROBOTS_DISALLOWED");
  }
  if (policy.require_known_license && !known_license) {
    assessment.blocking_reasons.push_back("LICENSE_UNKNOWN");
  }
  if (!assessment.mime_valid) {
    assessment.blocking_reasons.push_back("MIME_INVALID");
  }
  if (!assessment.encoding_valid) {
    assessment.blocking_reasons.push_back("ENCODING_INVALID");
  }
  if (!assessment.size_valid) {
    assessment.blocking_reasons.push_back("SIZE_INVALID");
  }
  return canonical_policy_assessment(std::move(assessment));
}

InternetExtractionResult extract_internet_snapshot(
    std::string snapshot_id, std::string content_type,
    std::span<const std::byte> bytes, const InternetExtractionLimits &limits,
    std::string extraction_strategy, const contracts::Json &source_review) {
  if (snapshot_id.empty() || limits.maximum_input_bytes == 0U ||
      limits.maximum_fragments == 0U || limits.maximum_fragment_bytes == 0U ||
      limits.maximum_nesting_depth == 0U) {
    extraction_error("internet extraction inputs are invalid");
  }
  InternetExtractionResult result;
  result.receipt.snapshot_id = snapshot_id;
  result.receipt.extractor_versions = {std::string(internet_extractor_version)};
  if (!extraction_strategy.empty())
    result.receipt.extractor_versions.push_back(
        std::string(internet_extractor_version) + ":" + extraction_strategy);
  result.receipt.decoded_text_signature = contracts::sha256_bytes(bytes);
  static const std::set<std::string> strategies = {"",
                                                   "plain-text",
                                                   "html-section",
                                                   "rfc-document",
                                                   "w3c-specification",
                                                   "crossref-json",
                                                   "mathematical-source-v1",
                                                   "europe-pmc-json"};
  if (!strategies.contains(extraction_strategy) ||
      ((extraction_strategy == "crossref-json" ||
        extraction_strategy == "europe-pmc-json") &&
       content_type != "application/json")) {
    result.receipt.rejected_fragments.push_back(
        "snapshot:UNSUPPORTED_EXTRACTION_STRATEGY");
  } else if (bytes.empty() || bytes.size() > limits.maximum_input_bytes) {
    result.receipt.rejected_fragments.push_back("snapshot:INPUT_SIZE_INVALID");
    result.receipt.truncated = bytes.size() > limits.maximum_input_bytes;
  } else {
    const std::string text = text_from_bytes(bytes);
    if (!common::is_valid_utf8(text)) {
      result.receipt.rejected_fragments.push_back("snapshot:INVALID_UTF8");
    } else if (extraction_strategy == "mathematical-source-v1") {
      if (content_type != "text/plain" ||
          !valid_mathematical_source_review(source_review) ||
          source_review.at("body_sha256") != contracts::sha256_bytes(bytes)) {
        result.receipt.rejected_fragments.push_back(
            "snapshot:MATHEMATICAL_SOURCE_REVIEW_OR_HASH_INVALID");
      } else if (trim(text).empty()) {
        result.receipt.rejected_fragments.push_back(
            "snapshot:EMPTY_MATHEMATICAL_SOURCE");
      } else if (text.size() > limits.maximum_fragment_bytes) {
        result.receipt.rejected_fragments.push_back(
            "snapshot:MATHEMATICAL_CONTEXT_TOO_LARGE");
        result.receipt.truncated = true;
      } else {
        // Never split a formula away from its domain, branch conventions,
        // accuracy discussion or copyright. Includes/imports remain inert text;
        // external definitions are unresolved, not silently guessed.
        append_fragment(result, limits, snapshot_id, "ALGORITHM_DESCRIPTION",
                        0U, bytes.size(), "mathematical-source:whole-file",
                        text, {},
                        {{"mathematical_context_review_required", true},
                         {"context_preservation", "whole-file-verbatim"},
                         {"assumptions", "PRESERVED_UNINTERPRETED"},
                         {"branch_conventions", "PRESERVED_UNINTERPRETED"},
                         {"error_bounds", "PRESERVED_UNINTERPRETED"},
                         {"external_dependencies", "NOT_EXPANDED_OR_EXECUTED"},
                         {"source_review", source_review}});
        // append_fragment normally trims prose. Mathematical source retains all
        // bytes, including leading whitespace and the final newline.
        result.fragments.back().text = text;
        result.fragments.back() =
            canonical_source_fragment(std::move(result.fragments.back()));
        result.receipt.diagnostics.push_back(
            "MATHEMATICAL_CONTEXT_REVIEW_REQUIRED");
      }
    } else if (content_type == "text/plain") {
      extract_plain(result, limits, snapshot_id, text);
    } else if (content_type == "text/html") {
      extract_html(result, limits, snapshot_id, text);
    } else if (content_type == "application/json") {
      try {
        const auto value = contracts::Json::parse(text);
        static_cast<void>(json_depth(value, 0U, limits.maximum_nesting_depth));
        if (extraction_strategy == "crossref-json" ||
            extraction_strategy == "europe-pmc-json") {
          extract_discovery(result, limits, snapshot_id, value, bytes.size(),
                            extraction_strategy);
        } else {
          const std::string canonical = contracts::canonical_json(value);
          append_fragment(result, limits, snapshot_id, "METADATA", 0U,
                          bytes.size(), "json:$", canonical);
        }
      } catch (const std::exception &error) {
        result.receipt.rejected_fragments.push_back(
            "json:INVALID_OR_OVER_LIMIT");
        result.receipt.diagnostics.push_back(error.what());
      }
    } else {
      result.receipt.rejected_fragments.push_back("snapshot:UNSUPPORTED_MIME:" +
                                                  content_type);
    }
  }
  for (const auto &fragment : result.fragments) {
    result.receipt.fragment_ids.push_back(fragment.object_id());
  }
  result.receipt = canonical_extraction_receipt(std::move(result.receipt));
  return result;
}

contracts::Json to_json(const InternetExtractionResult &value) {
  contracts::Json fragments = contracts::Json::array();
  for (const auto &fragment : value.fragments) {
    fragments.push_back(to_json(fragment));
  }
  return {{"fragments", std::move(fragments)},
          {"receipt", to_json(value.receipt)}};
}

} // namespace statewright::sources
