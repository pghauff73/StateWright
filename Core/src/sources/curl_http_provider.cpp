#include "statewright/sources/http_provider.hpp"

#include "statewright/common/error.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace statewright::sources {
namespace {

[[noreturn]] void curl_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied, std::move(message));
}

struct CurlDeleter final {
  void operator()(CURL *handle) const noexcept { curl_easy_cleanup(handle); }
};

struct CurlListDeleter final {
  void operator()(curl_slist *list) const noexcept { curl_slist_free_all(list); }
};

using CurlHandle = std::unique_ptr<CURL, CurlDeleter>;
using CurlList = std::unique_ptr<curl_slist, CurlListDeleter>;

struct Capture final {
  FetchResponse response;
  std::size_t maximum_compressed = 0;
  std::size_t maximum_decompressed = 0;
  std::size_t maximum_headers = 0;
  std::size_t header_bytes = 0;
  std::size_t maximum_download_seen = 0;
  bool header_limit_exceeded = false;
  bool compressed_limit_exceeded = false;
  bool decompressed_limit_exceeded = false;
  const std::function<bool()> *cancellation_requested = nullptr;
};

std::once_flag global_curl_once;

void initialize_curl() {
  std::call_once(global_curl_once, [] {
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
      curl_error("cannot initialize libcurl");
    }
  });
}

[[nodiscard]] std::size_t callback_bytes(std::size_t size,
                                         std::size_t count) {
  if (size != 0U && count > std::numeric_limits<std::size_t>::max() / size) {
    return std::numeric_limits<std::size_t>::max();
  }
  return size * count;
}

std::size_t body_callback(char *data, std::size_t size, std::size_t count,
                          void *userdata) {
  auto &capture = *static_cast<Capture *>(userdata);
  const std::size_t bytes = callback_bytes(size, count);
  if (bytes > capture.maximum_decompressed - capture.response.body.size()) {
    capture.decompressed_limit_exceeded = true;
    return 0U;
  }
  const auto *begin = reinterpret_cast<const std::byte *>(data);
  capture.response.body.insert(capture.response.body.end(), begin,
                               begin + bytes);
  return bytes;
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

std::size_t header_callback(char *data, std::size_t size, std::size_t count,
                            void *userdata) {
  auto &capture = *static_cast<Capture *>(userdata);
  const std::size_t bytes = callback_bytes(size, count);
  if (bytes > capture.maximum_headers - capture.header_bytes) {
    capture.header_limit_exceeded = true;
    return 0U;
  }
  capture.header_bytes += bytes;
  std::string line(data, bytes);
  const auto separator = line.find(':');
  if (separator == std::string::npos) {
    return bytes;
  }
  std::string name = trim(line.substr(0, separator));
  std::ranges::transform(name, name.begin(), [](unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  const std::string value = trim(line.substr(separator + 1));
  if (name == "content-length") {
    std::size_t declared = 0;
    const auto result =
        std::from_chars(value.data(), value.data() + value.size(), declared);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
      return 0U;
    }
    if (declared > capture.maximum_compressed) {
      capture.compressed_limit_exceeded = true;
      return 0U;
    }
  }
  if (name == "content-type" || name == "content-length" || name == "etag" ||
      name == "last-modified" || name == "location" ||
      name == "content-encoding" || name == "cache-control" ||
      name == "retry-after") {
    capture.response.headers[name] = value;
  }
  return bytes;
}

int transfer_callback(void *userdata, curl_off_t, curl_off_t downloaded,
                      curl_off_t, curl_off_t) {
  auto &capture = *static_cast<Capture *>(userdata);
  if (downloaded > 0) {
    const auto current = static_cast<unsigned long long>(downloaded);
    if (current > std::numeric_limits<std::size_t>::max()) {
      capture.compressed_limit_exceeded = true;
      return 1;
    }
    capture.maximum_download_seen =
        std::max(capture.maximum_download_seen,
                 static_cast<std::size_t>(current));
    if (capture.maximum_download_seen > capture.maximum_compressed) {
      capture.compressed_limit_exceeded = true;
      return 1;
    }
  }
  if (capture.cancellation_requested != nullptr &&
      *capture.cancellation_requested && (*capture.cancellation_requested)()) {
    return 1;
  }
  return 0;
}

void validate_request_headers(
    const std::map<std::string, std::string> &headers) {
  static const std::set<std::string> allowed = {
      "accept", "if-modified-since", "if-none-match"};
  for (const auto &[raw_name, value] : headers) {
    std::string name = raw_name;
    std::ranges::transform(name, name.begin(), [](unsigned char character) {
      return static_cast<char>(std::tolower(character));
    });
    if (!allowed.contains(name) || value.empty() ||
        raw_name.find_first_of("\r\n:") != std::string::npos ||
        value.find_first_of("\r\n") != std::string::npos) {
      curl_error("HTTP request header is not allowed: " + raw_name);
    }
  }
}

[[nodiscard]] std::string bracketed(std::string value) {
  if (value.find(':') != std::string::npos && !value.starts_with('[')) {
    return "[" + value + "]";
  }
  return value;
}

[[nodiscard]] CurlList pinned_resolution(
    const ParsedUrl &url, const std::vector<std::string> &addresses) {
  std::string entry =
      bracketed(url.host) + ":" + std::to_string(url.port) + ":";
  for (std::size_t index = 0; index < addresses.size(); ++index) {
    if (index != 0U) {
      entry += ',';
    }
    entry += bracketed(addresses[index]);
  }
  curl_slist *list = curl_slist_append(nullptr, entry.c_str());
  if (list == nullptr) {
    curl_error("cannot allocate pinned HTTP resolution");
  }
  return CurlList(list);
}

[[nodiscard]] CurlList request_headers(
    const std::map<std::string, std::string> &headers) {
  curl_slist *list = nullptr;
  for (const auto &[name, value] : headers) {
    const std::string header = name + ": " + value;
    curl_slist *next = curl_slist_append(list, header.c_str());
    if (next == nullptr) {
      curl_slist_free_all(list);
      curl_error("cannot allocate HTTP request headers");
    }
    list = next;
  }
  return CurlList(list);
}

[[nodiscard]] bool redirect_status(int status) noexcept {
  return status == 301 || status == 302 || status == 303 || status == 307 ||
         status == 308;
}

[[nodiscard]] FetchResponse perform_single_request(
    const FetchRequest &request, const ParsedUrl &parsed,
    const InternetSourcePolicy &policy,
    const std::vector<std::string> &addresses, long timeout_milliseconds) {
  CurlHandle handle(curl_easy_init());
  if (!handle) {
    curl_error("cannot create libcurl request");
  }
  Capture capture;
  capture.maximum_compressed = policy.maximum_response_bytes;
  capture.maximum_decompressed = policy.maximum_decompressed_bytes;
  capture.maximum_headers = policy.maximum_header_bytes;
  capture.cancellation_requested = &request.cancellation_requested;
  capture.response.requested_url = parsed.canonical_url;
  capture.response.final_url = parsed.canonical_url;
  capture.response.resolved_addresses = addresses;
  capture.response.provider_identity = std::string("libcurl/") + curl_version();

  const auto resolve = pinned_resolution(parsed, addresses);
  const auto headers = request_headers(request.headers);
  curl_easy_setopt(handle.get(), CURLOPT_URL, parsed.canonical_url.c_str());
  curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 0L);
  curl_easy_setopt(handle.get(), CURLOPT_PROTOCOLS_STR, parsed.scheme.c_str());
  curl_easy_setopt(handle.get(), CURLOPT_REDIR_PROTOCOLS_STR, parsed.scheme.c_str());
  curl_easy_setopt(handle.get(), CURLOPT_RESOLVE, resolve.get());
  curl_easy_setopt(handle.get(), CURLOPT_DNS_CACHE_TIMEOUT, 0L);
  curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT_MS,
                   std::min(timeout_milliseconds,
                            static_cast<long>(policy.connect_timeout_seconds) *
                                1000L));
  curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT_MS, timeout_milliseconds);
  curl_easy_setopt(handle.get(), CURLOPT_USERAGENT, policy.user_agent.c_str());
  curl_easy_setopt(handle.get(), CURLOPT_PROXY, "");
  curl_easy_setopt(handle.get(), CURLOPT_NOPROXY, "*");
  curl_easy_setopt(handle.get(), CURLOPT_NETRC, CURL_NETRC_IGNORED);
  curl_easy_setopt(handle.get(), CURLOPT_HTTPAUTH, CURLAUTH_NONE);
  curl_easy_setopt(handle.get(), CURLOPT_PROXYAUTH, CURLAUTH_NONE);
  curl_easy_setopt(handle.get(), CURLOPT_UNRESTRICTED_AUTH, 0L);
  curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYPEER,
                   policy.require_tls_verification ? 1L : 0L);
  curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYHOST,
                   policy.require_tls_verification ? 2L : 0L);
  curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(handle.get(), CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(handle.get(), CURLOPT_HTTP_CONTENT_DECODING, 1L);
  curl_easy_setopt(handle.get(), CURLOPT_HTTP_TRANSFER_DECODING, 1L);
  curl_easy_setopt(handle.get(), CURLOPT_MAXFILESIZE_LARGE,
                   static_cast<curl_off_t>(policy.maximum_response_bytes));
  curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, body_callback);
  curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &capture);
  curl_easy_setopt(handle.get(), CURLOPT_HEADERFUNCTION, header_callback);
  curl_easy_setopt(handle.get(), CURLOPT_HEADERDATA, &capture);
  curl_easy_setopt(handle.get(), CURLOPT_XFERINFOFUNCTION, transfer_callback);
  curl_easy_setopt(handle.get(), CURLOPT_XFERINFODATA, &capture);
  curl_easy_setopt(handle.get(), CURLOPT_NOPROGRESS, 0L);
  if (request.method == "HEAD") {
    curl_easy_setopt(handle.get(), CURLOPT_NOBODY, 1L);
  }
  if (headers) {
    curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, headers.get());
  }

  const auto started = std::chrono::steady_clock::now();
  const CURLcode result = curl_easy_perform(handle.get());
  capture.response.total_time_milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - started)
          .count();
  if (result != CURLE_OK) {
    if (capture.header_limit_exceeded) {
      curl_error("HTTP response header limit exceeded");
    }
    if (capture.compressed_limit_exceeded || result == CURLE_FILESIZE_EXCEEDED) {
      curl_error("HTTP compressed response limit exceeded");
    }
    if (capture.decompressed_limit_exceeded) {
      curl_error("HTTP decompressed response limit exceeded");
    }
    if (request.cancellation_requested && request.cancellation_requested()) {
      curl_error("HTTP fetch cancelled");
    }
    curl_error("HTTP fetch failed: " + std::string(curl_easy_strerror(result)));
  }

  long status = 0;
  curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
  capture.response.http_status = static_cast<int>(status);
  char *effective_url = nullptr;
  curl_easy_getinfo(handle.get(), CURLINFO_EFFECTIVE_URL, &effective_url);
  if (effective_url != nullptr && parsed.canonical_url != effective_url) {
    curl_error("libcurl changed the validated effective URL");
  }
  char *primary_ip = nullptr;
  curl_easy_getinfo(handle.get(), CURLINFO_PRIMARY_IP, &primary_ip);
  if (primary_ip == nullptr ||
      std::find(addresses.begin(), addresses.end(), primary_ip) ==
          addresses.end()) {
    curl_error("HTTP connection escaped pinned public addresses");
  }
  curl_off_t downloaded = 0;
  curl_easy_getinfo(handle.get(), CURLINFO_SIZE_DOWNLOAD_T, &downloaded);
  if (downloaded < 0 ||
      static_cast<unsigned long long>(downloaded) >
          std::numeric_limits<std::size_t>::max()) {
    curl_error("HTTP compressed response size is invalid");
  }
  capture.response.compressed_bytes =
      static_cast<std::size_t>(static_cast<unsigned long long>(downloaded));
  capture.response.decompressed_bytes = capture.response.body.size();
  if (capture.response.compressed_bytes > policy.maximum_response_bytes ||
      capture.response.decompressed_bytes > policy.maximum_decompressed_bytes) {
    curl_error("HTTP response size exceeded policy");
  }
  capture.response.tls_verified =
      parsed.scheme == "https" && policy.require_tls_verification;
  return capture.response;
}

} // namespace

CurlHttpFetchProvider::CurlHttpFetchProvider() { initialize_curl(); }

FetchResponse CurlHttpFetchProvider::fetch(const FetchRequest &request) {
  const auto policy = canonical_source_policy(request.policy);
  if (request.method != "GET" && request.method != "HEAD") {
    curl_error("HTTP fetch method is not allowed");
  }
  validate_request_headers(request.headers);
  ParsedUrl current = parse_and_validate_url(request.url, policy);
  const std::string requested_url = current.canonical_url;
  std::set<std::string> visited{current.canonical_url};
  std::set<std::string> all_addresses;
  std::vector<std::string> redirect_chain;
  long long total_time_milliseconds = 0;
  const auto started = std::chrono::steady_clock::now();

  while (true) {
    if (request.cancellation_requested && request.cancellation_requested()) {
      curl_error("HTTP fetch cancelled");
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    const long long remaining =
        static_cast<long long>(policy.request_timeout_seconds) * 1000LL -
        elapsed.count();
    if (remaining <= 0) {
      curl_error("HTTP fetch timed out");
    }
    const auto addresses = resolve_validated_addresses(current, policy);
    all_addresses.insert(addresses.begin(), addresses.end());
    FetchResponse response = perform_single_request(
        request, current, policy, addresses,
        static_cast<long>(std::min<long long>(remaining,
                                               std::numeric_limits<long>::max())));
    total_time_milliseconds += response.total_time_milliseconds;
    if (!redirect_status(response.http_status)) {
      if (response.http_status >= 300 && response.http_status < 400 &&
          response.http_status != 304) {
        curl_error("HTTP response used an unsupported redirect status");
      }
      if (response.http_status == 304 && !response.body.empty()) {
        curl_error("HTTP not-modified response unexpectedly contained a body");
      }
      response.requested_url = requested_url;
      response.final_url = current.canonical_url;
      response.redirect_chain = std::move(redirect_chain);
      response.resolved_addresses.assign(all_addresses.begin(),
                                         all_addresses.end());
      response.total_time_milliseconds = total_time_milliseconds;
      return response;
    }
    if (redirect_chain.size() >=
        static_cast<std::size_t>(policy.maximum_redirects)) {
      curl_error("HTTP redirect limit exceeded");
    }
    const std::string location = header_value(response, "location");
    ParsedUrl next = resolve_and_validate_redirect(current.canonical_url,
                                                   location, policy);
    if (current.scheme == "https" && next.scheme != "https") {
      curl_error("HTTPS redirect downgrade is prohibited");
    }
    if (!visited.insert(next.canonical_url).second) {
      curl_error("HTTP redirect loop detected");
    }
    redirect_chain.push_back(next.canonical_url);
    current = std::move(next);
  }
}

} // namespace statewright::sources
