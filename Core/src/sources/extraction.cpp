#include "statewright/sources/extraction.hpp"

#include "statewright/common/error.hpp"
#include "statewright/common/utf8.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/sources/fetch.hpp"

#include <algorithm>
#include <cctype>
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

std::string text_from_bytes(std::span<const std::byte> bytes) {
  return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

std::string classify_text(std::string_view text, bool heading,
                          bool table_row, bool code_block) {
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

void extract_html(InternetExtractionResult &result,
                  const InternetExtractionLimits &limits,
                  std::string_view snapshot_id, std::string_view text) {
  std::size_t position = 0U;
  std::size_t text_index = 0U;
  std::size_t nesting = 0U;
  int hidden_depth = 0;
  std::string current_tag;
  while (position < text.size() && !result.receipt.truncated) {
    const auto tag_start = text.find('<', position);
    const std::size_t segment_end =
        tag_start == std::string_view::npos ? text.size() : tag_start;
    if (hidden_depth == 0 && segment_end > position) {
      const bool heading = current_tag.size() == 2U && current_tag[0] == 'h' &&
                           current_tag[1] >= '1' && current_tag[1] <= '6';
      append_fragment(result, limits, snapshot_id,
                      classify_text(text.substr(position, segment_end - position),
                                    heading, current_tag == "tr", false),
                      position, segment_end,
                      "html:text:" + std::to_string(text_index++),
                      std::string(text.substr(position, segment_end - position)));
    }
    if (tag_start == std::string_view::npos) {
      break;
    }
    const auto tag_end = text.find('>', tag_start + 1U);
    if (tag_end == std::string_view::npos) {
      result.receipt.rejected_fragments.push_back("html:MALFORMED_TAG");
      break;
    }
    std::string tag = lower(trim(std::string(
        text.substr(tag_start + 1U, tag_end - tag_start - 1U))));
    const bool closing = tag.starts_with('/');
    if (closing) {
      tag.erase(tag.begin());
    }
    const auto separator = tag.find_first_of(" \t/");
    const std::string name = tag.substr(0U, separator);
    const bool self_closing = tag.ends_with('/') || name == "meta" ||
                              name == "link" || name == "br" || name == "img";
    if (closing) {
      if ((name == "script" || name == "style" || name == "noscript") &&
          hidden_depth > 0) {
        --hidden_depth;
      }
      if (nesting > 0U) {
        --nesting;
      }
      current_tag.clear();
    } else {
      if (!self_closing) {
        ++nesting;
        if (nesting > limits.maximum_nesting_depth) {
          extraction_error("HTML nesting exceeds extraction policy");
        }
      }
      if (name == "script" || name == "style" || name == "noscript") {
        ++hidden_depth;
        result.receipt.rejected_fragments.push_back("html:" + name +
                                                    ":NON_EXECUTABLE");
      }
      current_tag = name;
    }
    position = tag_end + 1U;
  }
}

} // namespace

InternetPolicyAssessment assess_internet_source(
    const InternetSourceSnapshot &snapshot_value,
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
  assessment.robots_allowed = robots_allowed || !policy.require_robots_permission;
  assessment.license_classification = std::move(license_classification);
  const bool known_license = !assessment.license_classification.empty() &&
                             assessment.license_classification != "UNKNOWN";
  assessment.mime_valid =
      std::find(policy.accepted_mime_types.begin(),
                policy.accepted_mime_types.end(), snapshot.content_type) !=
      policy.accepted_mime_types.end();
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
    std::span<const std::byte> bytes, const InternetExtractionLimits &limits) {
  if (snapshot_id.empty() || limits.maximum_input_bytes == 0U ||
      limits.maximum_fragments == 0U || limits.maximum_fragment_bytes == 0U ||
      limits.maximum_nesting_depth == 0U) {
    extraction_error("internet extraction inputs are invalid");
  }
  InternetExtractionResult result;
  result.receipt.snapshot_id = snapshot_id;
  result.receipt.extractor_versions = {
      std::string(internet_extractor_version)};
  result.receipt.decoded_text_signature = contracts::sha256_bytes(bytes);
  if (bytes.empty() || bytes.size() > limits.maximum_input_bytes) {
    result.receipt.rejected_fragments.push_back("snapshot:INPUT_SIZE_INVALID");
    result.receipt.truncated = bytes.size() > limits.maximum_input_bytes;
  } else {
    const std::string text = text_from_bytes(bytes);
    if (!common::is_valid_utf8(text)) {
      result.receipt.rejected_fragments.push_back("snapshot:INVALID_UTF8");
    } else if (content_type == "text/plain") {
      extract_plain(result, limits, snapshot_id, text);
    } else if (content_type == "text/html") {
      extract_html(result, limits, snapshot_id, text);
    } else if (content_type == "application/json") {
      try {
        const auto value = contracts::Json::parse(text);
        static_cast<void>(json_depth(value, 0U, limits.maximum_nesting_depth));
        const std::string canonical = contracts::canonical_json(value);
        append_fragment(result, limits, snapshot_id, "METADATA", 0U,
                        bytes.size(), "json:$", canonical);
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
