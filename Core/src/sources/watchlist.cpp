#include "statewright/sources/watchlist.hpp"

#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/contracts/typed_id.hpp"
#include "statewright/sources/fetch.hpp"
#include "statewright/sources/snapshot.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace statewright::sources {
namespace {

using Json = contracts::Json;

[[noreturn]] void watchlist_error(std::string message) {
  throw common::Error(common::ErrorCode::invalid_argument, std::move(message));
}

[[nodiscard]] const Json &source_group(const Json &registry,
                                       std::string_view name) {
  if (!registry.is_object() || registry.value("schema_version", 0) != 1 ||
      !registry.contains("source_groups") ||
      !registry.at("source_groups").is_array()) {
    watchlist_error("watchlist source-group registry is invalid");
  }
  for (const auto &group : registry.at("source_groups")) {
    if (group.is_object() &&
        group.value("source_group", std::string{}) == name) {
      return group;
    }
  }
  watchlist_error("unknown watchlist source group: " + std::string(name));
}

[[nodiscard]] std::vector<std::string> string_array(const Json &value,
                                                    std::string_view key) {
  if (!value.contains(key) || !value.at(key).is_array()) {
    watchlist_error(std::string(key) + " must be an array");
  }
  try {
    return value.at(key).get<std::vector<std::string>>();
  } catch (const Json::exception &) {
    watchlist_error(std::string(key) + " must contain strings");
  }
}

[[nodiscard]] bool contains_string(const Json &values,
                                   std::string_view expected) {
  if (!values.is_array()) {
    return false;
  }
  return std::any_of(values.begin(), values.end(), [&](const Json &value) {
    return value.is_string() &&
           value.get_ref<const std::string &>() == expected;
  });
}

[[nodiscard]] bool safe_slug(std::string_view value) {
  return !value.empty() &&
         std::all_of(value.begin(), value.end(), [](unsigned char character) {
           return std::islower(character) != 0 ||
                  std::isdigit(character) != 0 || character == '-';
         });
}

[[nodiscard]] bool numeric_section(std::string_view value) {
  if (value.empty() || value.front() == '.' || value.back() == '.') {
    return false;
  }
  bool previous_dot = false;
  for (const char character : value) {
    if (character == '.') {
      if (previous_dot) {
        return false;
      }
      previous_dot = true;
    } else if (std::isdigit(static_cast<unsigned char>(character)) != 0) {
      previous_dot = false;
    } else {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::string section_slug(std::string section) {
  static const std::map<std::string, std::string> known = {
      {"3.2", "linear-algebra"},
      {"3.3", "interpolation"},
      {"3.5", "quadrature"},
      {"3.8", "nonlinear-equations"},
      {"3.9", "convergence-acceleration"},
      {"3.11", "approximation-techniques"},
      {"3.12", "mathematical-constants"}};
  const auto found = known.find(section);
  if (found != known.end()) {
    return found->second;
  }
  std::ranges::replace(section, '.', '-');
  return "section-" + section;
}

[[nodiscard]] int deterministic_jitter(std::string_view name, int interval) {
  const int ceiling = std::min(interval / 10, 21'600);
  if (ceiling <= 0) {
    return 0;
  }
  const std::string digest = contracts::sha256_text(name);
  std::uint64_t prefix = 0U;
  for (std::size_t index = 0; index < 8U; ++index) {
    prefix <<= 4U;
    const char character = digest[index];
    prefix |= static_cast<std::uint64_t>(character >= '0' && character <= '9'
                                             ? character - '0'
                                             : character - 'a' + 10);
  }
  return static_cast<int>(prefix % static_cast<std::uint64_t>(ceiling + 1));
}

[[nodiscard]] Json normalized_entry(Json entry, const Json &group) {
  const std::string source_group_name =
      entry.value("source_group", group.at("source_group").get<std::string>());
  entry["source_group"] = source_group_name;
  entry["controlling_publisher"] =
      entry.value("controlling_publisher",
                  group.at("controlling_publisher").get<std::string>());
  entry["purpose"] =
      entry.value("purpose", group.at("default_purpose").get<std::string>());
  entry["subject"] =
      entry.value("subject", group.at("default_subject").get<std::string>());
  entry["tier"] = entry.value("tier", group.at("tier").get<int>());
  entry["accepted_mime_types"] =
      entry.value("accepted_mime_types", group.at("default_mime_types"));
  entry["polling_interval_seconds"] =
      entry.value("polling_interval_seconds",
                  group.at("default_polling_interval_seconds").get<int>());
  entry["license"] = entry.value("license", group.at("license"));
  entry["robots"] = entry.value(
      "robots", Json{{"required", true}, {"declared_status", "pending"}});
  entry["stability"] = entry.value(
      "stability", group.at("default_stability").get<std::string>());
  entry["extraction_strategy"] =
      entry.value("extraction_strategy",
                  group.at("default_extraction_strategy").get<std::string>());
  entry["enabled"] = entry.value("enabled", false);

  auto policy = InternetSourcePolicy{};
  policy.require_robots_permission = false;
  const auto parsed = parse_and_validate_url(
      entry.at("canonical_url").get<std::string>(), policy);
  entry["canonical_url"] = parsed.canonical_url;
  if (group.contains("reviewed_sources") &&
      group.at("reviewed_sources").contains(parsed.canonical_url) &&
      !entry.contains("source_review")) {
    entry["source_review"] = group.at("source_review_common");
    entry["source_review"]["body_sha256"] =
        group.at("reviewed_sources").at(parsed.canonical_url);
  }
  const int interval = entry.at("polling_interval_seconds").get<int>();
  entry["deterministic_jitter_seconds"] = entry.value(
      "deterministic_jitter_seconds",
      deterministic_jitter(entry.at("name").get<std::string>(), interval));
  return entry;
}

[[nodiscard]] Json template_entry(const Json &request, const Json &group,
                                  std::string name, std::string url) {
  Json entry = {{"name", std::move(name)},
                {"canonical_url", std::move(url)},
                {"source_group", group.at("source_group")}};
  if (request.contains("purpose")) {
    entry["purpose"] = request.at("purpose");
  }
  if (request.contains("subject")) {
    entry["subject"] = request.at("subject");
  }
  if (request.contains("enabled")) {
    entry["enabled"] = request.at("enabled");
  }
  return normalized_entry(std::move(entry), group);
}

[[nodiscard]] std::string entry_hash(const Json &entry) {
  return contracts::sha256_json(entry);
}

[[nodiscard]] bool license_verified(const Json &entry) {
  const auto &license = entry.at("license");
  return license.value("status", std::string{}) == "verified" &&
         license.contains("evidence_urls") &&
         license.at("evidence_urls").is_array() &&
         !license.at("evidence_urls").empty();
}

[[nodiscard]] Json selected_headers(const FetchResponse &response) {
  Json headers = Json::object();
  for (const std::string name : {"content-type", "etag", "last-modified",
                                 "retry-after", "content-length"}) {
    const std::string value = header_value(response, name);
    if (!value.empty()) {
      headers[name] = value;
    }
  }
  return headers;
}

void add_blocker(Json &result, std::string blocker) {
  result["blocking_reasons"].push_back(std::move(blocker));
}

} // namespace

bool valid_mathematical_source_review(const Json &review) {
  const auto hex = [](const Json &value, std::size_t size) {
    if (!value.is_string())
      return false;
    const auto text = value.get<std::string>();
    return text.size() == size && std::ranges::all_of(text, [](char c) {
             return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
  };
  if (!review.is_object() || review.value("status", "") != "approved" ||
      !hex(review.value("revision", Json{}), 40U) ||
      !hex(review.value("body_sha256", Json{}), 64U))
    return false;
  for (const auto *key :
       {"reviewed_at", "reviewer", "scope", "third_party_review"}) {
    if (!review.contains(key) || !review.at(key).is_string() ||
        review.at(key).get<std::string>().empty())
      return false;
  }
  if (!review.contains("license_notices") ||
      !review.at("license_notices").is_array() ||
      review.at("license_notices").empty())
    return false;
  for (const auto &notice : review.at("license_notices")) {
    if (!notice.is_object() || !notice.contains("text") ||
        !notice.at("text").is_string() ||
        notice.at("text").get<std::string>().empty() ||
        !notice.contains("url") || !notice.at("url").is_string() ||
        !notice.at("url").get<std::string>().starts_with("https://") ||
        notice.value("sha256", "") !=
            contracts::sha256_text(notice.at("text").get<std::string>()))
      return false;
  }
  return true;
}

Json create_watchlist_manifest(const Json &request,
                               const Json &source_registry) {
  if (!request.is_object()) {
    watchlist_error("watchlist creation request must be an object");
  }
  const std::string template_name =
      request.value("template", std::string("exact"));
  Json watches = Json::array();
  if (template_name == "exact") {
    if (!request.contains("watches") || !request.at("watches").is_array()) {
      watchlist_error("exact watchlist creation requires watches");
    }
    for (auto entry : request.at("watches")) {
      if (!entry.is_object() || !entry.contains("source_group")) {
        watchlist_error("exact watch entry requires source_group");
      }
      const auto &group = source_group(
          source_registry, entry.at("source_group").get<std::string>());
      watches.push_back(normalized_entry(std::move(entry), group));
    }
  } else if (template_name == "nist-dlmf-section") {
    const auto &group = source_group(source_registry, "nist-dlmf");
    if (!contains_string(group.at("allowed_templates"), template_name)) {
      watchlist_error("NIST DLMF template is not enabled by the registry");
    }
    for (const auto &section : string_array(request, "sections")) {
      if (!numeric_section(section)) {
        watchlist_error("invalid NIST DLMF section: " + section);
      }
      watches.push_back(template_entry(request, group,
                                       "nist-dlmf-" + section_slug(section),
                                       "https://dlmf.nist.gov/" + section));
    }
  } else if (template_name == "rfc-number") {
    const auto &group = source_group(source_registry, "ietf-rfc-editor");
    if (!contains_string(group.at("allowed_templates"), template_name)) {
      watchlist_error("RFC template is not enabled by the registry");
    }
    if (!request.contains("numbers") || !request.at("numbers").is_array()) {
      watchlist_error("RFC template requires numbers");
    }
    for (const auto &number : request.at("numbers")) {
      if (!number.is_number_integer() || number.get<int>() <= 0) {
        watchlist_error("RFC numbers must be positive integers");
      }
      const std::string value = std::to_string(number.get<int>());
      watches.push_back(template_entry(request, group, "rfc-" + value,
                                       "https://www.rfc-editor.org/rfc/rfc" +
                                           value + ".html"));
    }
  } else if (template_name == "w3c-recommendation") {
    const auto &group = source_group(source_registry, "w3c");
    if (!contains_string(group.at("allowed_templates"), template_name)) {
      watchlist_error("W3C template is not enabled by the registry");
    }
    for (const auto &slug : string_array(request, "slugs")) {
      if (!safe_slug(slug)) {
        watchlist_error("invalid W3C recommendation slug: " + slug);
      }
      watches.push_back(template_entry(request, group, "w3c-" + slug,
                                       "https://www.w3.org/TR/" + slug + "/"));
    }
  } else {
    watchlist_error("unsupported watchlist URL template: " + template_name);
  }

  Json manifest = {
      {"schema_version", 1},
      {"watchlist_version",
       request.value("watchlist_version", std::string("saa-generated-v1"))},
      {"description",
       request.value("description",
                     std::string("Generated SAA internet watchlist."))},
      {"source_policy_ref",
       request.value(
           "source_policy_ref",
           std::string(
               "resources/policies/internet/default-source-policy-v1.json"))},
      {"registration_defaults",
       Json{{"enabled", false}, {"deterministic_jitter_seconds", 0}}},
      {"watches", std::move(watches)}};
  validate_watchlist_manifest(manifest, source_registry);
  return manifest;
}

void validate_watchlist_manifest(const Json &manifest,
                                 const Json &source_registry) {
  if (!manifest.is_object() || manifest.value("schema_version", 0) != 1 ||
      manifest.value("watchlist_version", std::string{}).empty() ||
      !manifest.contains("watches") || !manifest.at("watches").is_array()) {
    watchlist_error("watchlist manifest envelope is invalid");
  }
  static const std::set<std::string> purposes = {
      "discovery", "authoritative-evidence", "reference-implementation",
      "verification"};
  static const std::set<std::string> mime_types = {"application/json",
                                                   "text/html", "text/plain"};
  static const std::set<std::string> license_statuses = {
      "verified", "review-required", "prohibited"};
  static const std::set<std::string> robots_statuses = {"pending", "allowed",
                                                        "denied"};
  static const std::set<std::string> stabilities = {
      "living", "stable-section", "dated", "immutable", "api-query"};

  std::set<std::string> names;
  std::set<std::string> urls;
  std::set<std::string> registry_names;
  for (const auto &group : source_registry.at("source_groups")) {
    const std::string name = group.at("source_group").get<std::string>();
    if (!registry_names.insert(name).second) {
      watchlist_error("duplicate source group in registry: " + name);
    }
  }

  for (const auto &entry : manifest.at("watches")) {
    if (!entry.is_object()) {
      watchlist_error("watchlist entry must be an object");
    }
    const std::string name = entry.at("name").get<std::string>();
    const std::string group_name = entry.at("source_group").get<std::string>();
    const auto &group = source_group(source_registry, group_name);
    if (name.empty() || !names.insert(name).second) {
      watchlist_error("duplicate or empty watchlist name: " + name);
    }
    if (entry.at("controlling_publisher") !=
            group.at("controlling_publisher") ||
        entry.at("tier") != group.at("tier")) {
      watchlist_error("watchlist publisher or tier disagrees with registry: " +
                      name);
    }
    const std::string purpose = entry.at("purpose").get<std::string>();
    if (!purposes.contains(purpose)) {
      watchlist_error("unsupported watchlist purpose: " + purpose);
    }
    if (entry.at("subject").get<std::string>().empty()) {
      watchlist_error("watchlist subject is empty: " + name);
    }

    auto policy = InternetSourcePolicy{};
    policy.require_robots_permission = false;
    const auto parsed = parse_and_validate_url(
        entry.at("canonical_url").get<std::string>(), policy);
    if (parsed.canonical_url != entry.at("canonical_url").get<std::string>()) {
      watchlist_error("watchlist URL is not canonical: " + name);
    }
    if (!urls.insert(parsed.canonical_url).second) {
      watchlist_error("duplicate canonical watchlist URL: " +
                      parsed.canonical_url);
    }
    if (!contains_string(group.at("allowed_hostnames"), parsed.host)) {
      watchlist_error("watchlist hostname is outside its source group: " +
                      parsed.host);
    }

    if (group.contains("reviewed_sources")) {
      const auto &reviewed = group.at("reviewed_sources");
      if (!reviewed.contains(parsed.canonical_url))
        watchlist_error(
            "mathematical source has not received file-level review");
      Json expected = group.at("source_review_common");
      expected["body_sha256"] = reviewed.at(parsed.canonical_url);
      if (!entry.contains("source_review") ||
          entry.at("source_review") != expected ||
          !valid_mathematical_source_review(expected) ||
          entry.at("license") != group.at("license") ||
          entry.at("extraction_strategy") != "mathematical-source-v1" ||
          entry.at("stability") != "immutable" ||
          parsed.canonical_url.find(
              "/" + expected.at("revision").get<std::string>() + "/") ==
              std::string::npos)
        watchlist_error("mathematical source review, pin or license disagrees "
                        "with registry");
    } else if (entry.contains("source_review") ||
               entry.at("extraction_strategy") == "mathematical-source-v1") {
      watchlist_error(
          "mathematical sources require a file-reviewed registry group");
    }

    const auto accepted = string_array(entry, "accepted_mime_types");
    if (accepted.empty()) {
      watchlist_error("watchlist MIME list is empty: " + name);
    }
    for (const auto &mime : accepted) {
      if (!mime_types.contains(mime) ||
          !contains_string(group.at("allowed_mime_types"), mime)) {
        watchlist_error("unsupported watchlist MIME type: " + mime);
      }
    }
    const int interval = entry.at("polling_interval_seconds").get<int>();
    const int jitter = entry.at("deterministic_jitter_seconds").get<int>();
    if (interval <= 0 || jitter < 0 || jitter >= interval) {
      watchlist_error("watchlist polling interval or jitter is invalid: " +
                      name);
    }

    const auto &license = entry.at("license");
    const std::string license_status = license.at("status").get<std::string>();
    if (!license_statuses.contains(license_status) ||
        license.at("classification").get<std::string>().empty() ||
        !license.at("evidence_urls").is_array()) {
      watchlist_error("watchlist license declaration is invalid: " + name);
    }
    if (license_status == "verified" && !license_verified(entry)) {
      watchlist_error("verified watchlist license lacks evidence: " + name);
    }
    const auto &robots = entry.at("robots");
    if (!robots.at("required").is_boolean() ||
        !robots_statuses.contains(
            robots.at("declared_status").get<std::string>())) {
      watchlist_error("watchlist robots declaration is invalid: " + name);
    }
    if (!stabilities.contains(entry.at("stability").get<std::string>()) ||
        entry.at("extraction_strategy").get<std::string>().empty() ||
        !entry.at("enabled").is_boolean()) {
      watchlist_error(
          "watchlist stability or extraction declaration is invalid: " + name);
    }
  }
}

Json preflight_watchlist_manifest(const Json &manifest,
                                  const Json &source_registry,
                                  const InternetSourcePolicy &base_policy,
                                  HttpFetchProvider &provider,
                                  std::string checked_at) {
  validate_watchlist_manifest(manifest, source_registry);
  if (checked_at.empty()) {
    watchlist_error("watchlist preflight requires checked_at");
  }
  Json results = Json::array();
  for (const auto &entry : manifest.at("watches")) {
    const auto policy = watchlist_source_policy(entry, base_policy);
    Json result = {{"blocking_reasons", Json::array()},
                   {"canonical_url", entry.at("canonical_url")},
                   {"eligible", false},
                   {"entry_name", entry.at("name")},
                   {"entry_sha256", entry_hash(entry)},
                   {"source_policy_id", policy.object_id()},
                   {"status", "PREFLIGHT_FAILED"}};
    if (!license_verified(entry)) {
      add_blocker(result, "license-not-verified");
    }
    if (entry.at("robots").at("declared_status") == "denied") {
      add_blocker(result, "robots-declared-denied");
    }
    try {
      const auto response =
          provider.fetch({.url = entry.at("canonical_url").get<std::string>(),
                          .method = "GET",
                          .headers = {},
                          .policy = policy,
                          .cancellation_requested = {}});
      result["compressed_bytes"] = response.compressed_bytes;
      result["decompressed_bytes"] = response.decompressed_bytes;
      result["final_url"] = response.final_url;
      result["http_status"] = response.http_status;
      result["provider_identity"] = response.provider_identity;
      result["redirect_chain"] = response.redirect_chain;
      result["resolved_addresses"] = response.resolved_addresses;
      result["robots_allowed"] = response.robots_allowed;
      result["robots_evidence"] = response.robots_evidence;
      result["robots_policy_evaluated"] = response.robots_policy_evaluated;
      result["selected_headers"] = selected_headers(response);
      result["tls_verified"] = response.tls_verified;
      result["total_time_milliseconds"] = response.total_time_milliseconds;
      if (entry.contains("source_review")) {
        result["body_sha256"] = contracts::sha256_bytes(response.body);
        if (result.at("body_sha256") !=
            entry.at("source_review").at("body_sha256"))
          add_blocker(result, "pinned-content-mismatch");
      }

      if (response.http_status == 429) {
        add_blocker(result, "rate-limited");
        result["status"] = "DEFERRED_RATE_LIMIT";
      } else if (response.http_status == 401 || response.http_status == 403) {
        add_blocker(result, "authentication-required");
      } else if (response.http_status < 200 || response.http_status >= 300 ||
                 response.http_status == 204) {
        add_blocker(result, "http-status-not-eligible");
      }
      if (!response.tls_verified) {
        add_blocker(result, "tls-not-verified");
      }
      if (!response.redirect_chain.empty() ||
          response.final_url != entry.at("canonical_url").get<std::string>()) {
        add_blocker(result, "canonical-url-redirected");
      }
      if (policy.require_robots_permission &&
          (!response.robots_policy_evaluated || !response.robots_allowed)) {
        add_blocker(result, "robots-not-authorized");
      }
      const std::string content_type =
          normalized_response_content_type(response);
      result["content_type"] = content_type;
      if (!contains_string(entry.at("accepted_mime_types"), content_type)) {
        add_blocker(result, "mime-type-not-accepted");
      }
      if (response.body.empty()) {
        add_blocker(result, "empty-response");
      }
      if (response.compressed_bytes > policy.maximum_response_bytes ||
          response.decompressed_bytes > policy.maximum_decompressed_bytes ||
          response.body.size() > policy.maximum_decompressed_bytes) {
        add_blocker(result, "response-size-exceeded");
      }
      if (response.resolved_addresses.empty() ||
          std::any_of(response.resolved_addresses.begin(),
                      response.resolved_addresses.end(),
                      [&](const std::string &address) {
                        return !is_public_address(
                            address, policy.allow_loopback_for_tests);
                      })) {
        add_blocker(result, "public-address-not-verified");
      }
      if (result.at("blocking_reasons").empty()) {
        result["eligible"] = true;
        result["status"] = "PREFLIGHT_ELIGIBLE";
        if (!watchlist_preflight_eligible(entry, result, policy)) {
          add_blocker(result, "incomplete-preflight-evidence");
          result["eligible"] = false;
          result["status"] = "PREFLIGHT_FAILED";
        }
      }
    } catch (const std::exception &error) {
      add_blocker(result, std::string("fetch-failed: ") + error.what());
    }
    results.push_back(std::move(result));
  }

  Json report = {
      {"checked_at", std::move(checked_at)},
      {"manifest_sha256", contracts::sha256_json(manifest)},
      {"source_registry_sha256", contracts::sha256_json(source_registry)},
      {"results", std::move(results)},
      {"schema_version", 1},
      {"watchlist_version", manifest.at("watchlist_version")}};
  report["report_signature"] = contracts::sha256_json(report);
  report["report_id"] =
      contracts::typed_id("internet-watchlist-preflight", report);
  return report;
}

InternetSourcePolicy watchlist_source_policy(const Json &entry,
                                             InternetSourcePolicy base_policy) {
  for (const auto &mime : entry.at("accepted_mime_types")) {
    if (std::find(base_policy.accepted_mime_types.begin(),
                  base_policy.accepted_mime_types.end(),
                  mime.get<std::string>()) ==
        base_policy.accepted_mime_types.end()) {
      watchlist_error(
          "watchlist MIME type is not accepted by the source policy");
    }
  }
  base_policy.accepted_mime_types =
      entry.at("accepted_mime_types").get<std::vector<std::string>>();
  base_policy.require_robots_permission =
      base_policy.require_robots_permission ||
      entry.at("robots").at("required").get<bool>();
  base_policy.require_known_license = true;
  base_policy.policy_signature.clear();
  return canonical_source_policy(std::move(base_policy));
}

InternetWatch watchlist_watch(const Json &entry, std::string source_policy_id,
                              bool eligible, bool enable_eligible) {
  InternetWatch watch;
  watch.canonical_url = entry.at("canonical_url").get<std::string>();
  watch.enabled = entry.at("enabled").get<bool>() && eligible &&
                  enable_eligible && license_verified(entry);
  watch.source_policy_id = std::move(source_policy_id);
  watch.source_group = entry.at("source_group").get<std::string>();
  watch.accepted_mime_types =
      entry.at("accepted_mime_types").get<std::vector<std::string>>();
  watch.polling_interval_seconds =
      entry.at("polling_interval_seconds").get<int>();
  watch.deterministic_jitter_seconds =
      entry.at("deterministic_jitter_seconds").get<int>();
  return canonical_watch(std::move(watch));
}

bool watchlist_preflight_eligible(const Json &entry, const Json &result,
                                  const InternetSourcePolicy &policy) {
  if (entry.contains("source_review") &&
      (!valid_mathematical_source_review(entry.at("source_review")) ||
       result.value("body_sha256", "") !=
           entry.at("source_review").at("body_sha256").get<std::string>()))
    return false;
  if (!result.is_object() || !license_verified(entry) ||
      entry.at("robots").at("declared_status") == "denied" ||
      !result.value("eligible", false) ||
      result.value("status", std::string{}) != "PREFLIGHT_ELIGIBLE" ||
      !result.contains("blocking_reasons") ||
      !result.at("blocking_reasons").is_array() ||
      !result.at("blocking_reasons").empty() ||
      result.value("entry_sha256", std::string{}) != entry_hash(entry) ||
      result.value("source_policy_id", std::string{}) != policy.object_id() ||
      result.value("canonical_url", std::string{}) !=
          entry.at("canonical_url").get<std::string>() ||
      result.value("final_url", std::string{}) !=
          entry.at("canonical_url").get<std::string>() ||
      !result.contains("redirect_chain") ||
      !result.at("redirect_chain").is_array() ||
      !result.at("redirect_chain").empty() ||
      !result.value("tls_verified", false) ||
      result.value("provider_identity", std::string{}).empty() ||
      !result.contains("resolved_addresses") ||
      !result.at("resolved_addresses").is_array() ||
      result.at("resolved_addresses").empty() ||
      !contains_string(entry.at("accepted_mime_types"),
                       result.value("content_type", std::string{})) ||
      (policy.require_robots_permission &&
       (!result.value("robots_policy_evaluated", false) ||
        !result.value("robots_allowed", false)))) {
    return false;
  }
  const int status = result.value("http_status", 0);
  if (status < 200 || status >= 300 || status == 204) {
    return false;
  }
  for (const auto &address : result.at("resolved_addresses")) {
    if (!address.is_string() ||
        !is_public_address(address.get<std::string>(),
                           policy.allow_loopback_for_tests)) {
      return false;
    }
  }
  for (const auto &[key, limit] :
       {std::pair{"compressed_bytes", policy.maximum_response_bytes},
        std::pair{"decompressed_bytes", policy.maximum_decompressed_bytes}}) {
    if (!result.contains(key) || !result.at(key).is_number_unsigned() ||
        result.at(key).get<std::size_t>() == 0U ||
        result.at(key).get<std::size_t>() > limit) {
      return false;
    }
  }
  return true;
}

Json supersede_watchlist_registration(const Json &registration,
                                      const InternetWatch &previous,
                                      const InternetWatch &replacement,
                                      std::string predecessor_registration_id) {
  Json material = registration;
  const std::string signature =
      material.value("registration_signature", std::string{});
  material.erase("registration_signature");
  if (signature.empty() || contracts::sha256_json(material) != signature ||
      registration.at("watch_id") != previous.object_id() ||
      registration.at("source_policy_id") != previous.source_policy_id ||
      registration.at("source_group") != previous.source_group ||
      replacement.supersedes_watch_id != previous.object_id()) {
    watchlist_error("watchlist registration does not bind its predecessor");
  }
  auto previous_identity = to_json(previous);
  auto replacement_identity = to_json(replacement);
  for (const auto *key : {"enabled", "supersedes_watch_id",
                          "schedule_generation", "polling_interval_seconds",
                          "deterministic_jitter_seconds", "watch_signature"}) {
    previous_identity.erase(key);
    replacement_identity.erase(key);
  }
  const bool eligible =
      previous_identity == replacement_identity &&
      registration.at("license_status") == "verified" &&
      !registration.at("license_evidence_urls").empty() &&
      !registration.at("preflight_report_sha256").get<std::string>().empty() &&
      registration.at("eligibility_status") != "QUARANTINED";
  if (replacement.enabled && !eligible) {
    watchlist_error("watchlist source changes or quarantine require fresh "
                    "preflight before enabling");
  }
  material["watch_id"] = replacement.object_id();
  material["source_policy_id"] = replacement.source_policy_id;
  material["source_group"] = replacement.source_group;
  material["predecessor_registration_id"] =
      std::move(predecessor_registration_id);
  material["eligibility_status"] =
      eligible
          ? replacement.enabled ? "REGISTERED_ENABLED" : "REGISTERED_DISABLED"
          : "QUARANTINED";
  if (!eligible) {
    material["license_status"] = "review-required";
    material["license_classification"] = "UNKNOWN";
    material["license_evidence_urls"] = Json::array();
    material["preflight_report_sha256"] = "";
  }
  material["registration_signature"] = contracts::sha256_json(material);
  return material;
}

Json make_watchlist_registration(const Json &manifest, const Json &entry,
                                 std::string watch_id,
                                 std::string source_policy_id,
                                 std::string preflight_report_sha256,
                                 std::string eligibility_status,
                                 std::string evidence_independence_group) {
  Json registration = {
      {"controlling_publisher", entry.at("controlling_publisher")},
      {"eligibility_status", std::move(eligibility_status)},
      {"entry_name", entry.at("name")},
      {"entry_sha256", entry_hash(entry)},
      {"extraction_strategy", entry.at("extraction_strategy")},
      {"evidence_independence_group",
       evidence_independence_group.empty()
           ? entry.at("source_group").get<std::string>()
           : std::move(evidence_independence_group)},
      {"license_classification", entry.at("license").at("classification")},
      {"license_evidence_urls", entry.at("license").at("evidence_urls")},
      {"license_status", entry.at("license").at("status")},
      {"preflight_report_sha256", std::move(preflight_report_sha256)},
      {"purpose", entry.at("purpose")},
      {"schema_version", 1},
      {"source_group", entry.at("source_group")},
      {"source_policy_id", std::move(source_policy_id)},
      {"subject", entry.at("subject")},
      {"tier", entry.at("tier")},
      {"watch_id", std::move(watch_id)},
      {"watchlist_sha256", contracts::sha256_json(manifest)},
      {"watchlist_version", manifest.at("watchlist_version")}};
  if (entry.contains("source_review"))
    registration["source_review"] = entry.at("source_review");
  registration["registration_signature"] = contracts::sha256_json(registration);
  return registration;
}

} // namespace statewright::sources
