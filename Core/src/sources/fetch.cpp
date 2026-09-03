#include "statewright/sources/fetch.hpp"

#include "statewright/common/error.hpp"

#include <curl/curl.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <memory>
#include <set>
#include <utility>

namespace statewright::sources {
namespace {

[[noreturn]] void fetch_error(std::string message) {
  throw common::Error(common::ErrorCode::policy_denied, std::move(message));
}

struct CurlUrlDeleter final {
  void operator()(CURLU *url) const noexcept { curl_url_cleanup(url); }
};

using CurlUrl = std::unique_ptr<CURLU, CurlUrlDeleter>;

[[nodiscard]] std::string component(CURLU *url, CURLUPart part,
                                    unsigned int flags = 0U) {
  char *raw = nullptr;
  const auto result = curl_url_get(url, part, &raw, flags);
  if (result != CURLUE_OK || raw == nullptr) {
    return {};
  }
  std::string value(raw);
  curl_free(raw);
  return value;
}

[[nodiscard]] bool allowed_port(const InternetSourcePolicy &policy, int port) {
  return std::find(policy.allowed_ports.begin(), policy.allowed_ports.end(),
                   port) != policy.allowed_ports.end();
}

[[nodiscard]] bool ipv4_public(const std::array<unsigned char, 4> &bytes,
                               bool allow_loopback) {
  if (bytes[0] == 0U || bytes[0] >= 224U || bytes[0] == 10U ||
      (bytes[0] == 100U && bytes[1] >= 64U && bytes[1] <= 127U) ||
      (bytes[0] == 169U && bytes[1] == 254U) ||
      (bytes[0] == 172U && bytes[1] >= 16U && bytes[1] <= 31U) ||
      (bytes[0] == 192U && bytes[1] == 0U && bytes[2] == 0U) ||
      (bytes[0] == 192U && bytes[1] == 168U) ||
      (bytes[0] == 192U && bytes[1] == 0U && bytes[2] == 2U) ||
      (bytes[0] == 192U && bytes[1] == 88U && bytes[2] == 99U) ||
      (bytes[0] == 198U && (bytes[1] == 18U || bytes[1] == 19U)) ||
      (bytes[0] == 198U && bytes[1] == 51U && bytes[2] == 100U) ||
      (bytes[0] == 203U && bytes[1] == 0U && bytes[2] == 113U)) {
    return false;
  }
  if (bytes[0] == 127U) {
    return allow_loopback;
  }
  return true;
}

[[nodiscard]] bool ipv6_public(const std::array<unsigned char, 16> &bytes,
                               bool allow_loopback) {
  const bool unspecified =
      std::ranges::all_of(bytes, [](unsigned char value) { return value == 0U; });
  if (unspecified || bytes[0] == 0xffU || (bytes[0] & 0xfeU) == 0xfcU ||
      (bytes[0] == 0xfeU && (bytes[1] & 0xc0U) == 0x80U) ||
      (bytes[0] == 0x20U && bytes[1] == 0x01U && bytes[2] == 0x0dU &&
       bytes[3] == 0xb8U)) {
    return false;
  }
  const bool loopback =
      std::ranges::all_of(bytes.begin(), bytes.end() - 1,
                          [](unsigned char value) { return value == 0U; }) &&
      bytes.back() == 1U;
  if (loopback) {
    return allow_loopback;
  }
  const bool ipv4_mapped =
      std::ranges::all_of(bytes.begin(), bytes.begin() + 10,
                          [](unsigned char value) { return value == 0U; }) &&
      bytes[10] == 0xffU && bytes[11] == 0xffU;
  if (ipv4_mapped) {
    std::array<unsigned char, 4> ipv4{};
    std::copy(bytes.begin() + 12, bytes.end(), ipv4.begin());
    return ipv4_public(ipv4, allow_loopback);
  }
  const bool ipv4_compatible =
      std::ranges::all_of(bytes.begin(), bytes.begin() + 12,
                          [](unsigned char value) { return value == 0U; });
  if (ipv4_compatible ||
      (bytes[0] == 0x01U && bytes[1] == 0x00U) ||
      (bytes[0] == 0x20U && bytes[1] == 0x01U && bytes[2] == 0x00U) ||
      (bytes[0] == 0x20U && bytes[1] == 0x01U && bytes[2] == 0x00U &&
       (bytes[3] & 0xf0U) == 0x10U) ||
      (bytes[0] == 0x20U && bytes[1] == 0x02U)) {
    return false;
  }
  return true;
}

} // namespace

ParsedUrl parse_and_validate_url(std::string_view url_text,
                                 const InternetSourcePolicy &policy) {
  if (url_text.empty() || url_text.size() > 8192U ||
      url_text.find('\0') != std::string_view::npos) {
    fetch_error("internet URL must not be empty");
  }
  CurlUrl url(curl_url());
  if (!url || curl_url_set(url.get(), CURLUPART_URL,
                           std::string(url_text).c_str(),
                           CURLU_DISALLOW_USER) != CURLUE_OK) {
    fetch_error("internet URL is malformed or contains credentials");
  }
  std::string scheme = component(url.get(), CURLUPART_SCHEME);
  std::string host = component(url.get(), CURLUPART_HOST);
  std::ranges::transform(scheme, scheme.begin(), [](unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  std::ranges::transform(host, host.begin(), [](unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  if (host.ends_with('.')) {
    host.pop_back();
  }
  if (std::find(policy.allowed_schemes.begin(), policy.allowed_schemes.end(),
                scheme) == policy.allowed_schemes.end() ||
      host.empty()) {
    fetch_error("internet URL scheme or host is not allowed");
  }
  if (!component(url.get(), CURLUPART_USER).empty() ||
      !component(url.get(), CURLUPART_PASSWORD).empty() ||
      !component(url.get(), CURLUPART_FRAGMENT).empty()) {
    fetch_error("internet URL credentials and fragments are prohibited");
  }
  const std::string port_text = component(url.get(), CURLUPART_PORT,
                                          CURLU_DEFAULT_PORT);
  const int port = std::stoi(port_text);
  if (!allowed_port(policy, port)) {
    fetch_error("internet URL port is not allowed");
  }
  if (host.empty() || host.find('%') != std::string::npos) {
    fetch_error("internet URL host is invalid");
  }
  if (curl_url_set(url.get(), CURLUPART_SCHEME, scheme.c_str(), 0U) !=
          CURLUE_OK ||
      curl_url_set(url.get(), CURLUPART_HOST, host.c_str(), 0U) != CURLUE_OK) {
    fetch_error("internet URL cannot be canonicalized");
  }
  const std::string canonical = component(url.get(), CURLUPART_URL);
  const std::string path = component(url.get(), CURLUPART_PATH);
  const std::string query = component(url.get(), CURLUPART_QUERY);
  return {.scheme = scheme,
          .host = host,
          .port = port,
          .path_and_query = query.empty() ? path : path + "?" + query,
          .canonical_url = canonical};
}

ParsedUrl resolve_and_validate_redirect(std::string_view base_url,
                                        std::string_view location,
                                        const InternetSourcePolicy &policy) {
  if (location.empty() || location.size() > 8192U ||
      location.find('\0') != std::string_view::npos) {
    fetch_error("internet redirect location is invalid");
  }
  CurlUrl url(curl_url());
  if (!url ||
      curl_url_set(url.get(), CURLUPART_URL, std::string(base_url).c_str(),
                   CURLU_DISALLOW_USER) != CURLUE_OK ||
      curl_url_set(url.get(), CURLUPART_URL, std::string(location).c_str(),
                   CURLU_DISALLOW_USER) != CURLUE_OK) {
    fetch_error("internet redirect URL is malformed");
  }
  return parse_and_validate_url(component(url.get(), CURLUPART_URL), policy);
}

bool is_public_address(std::string_view address, bool allow_loopback_for_tests) {
  std::array<unsigned char, 16> bytes{};
  if (inet_pton(AF_INET, std::string(address).c_str(), bytes.data()) == 1) {
    std::array<unsigned char, 4> ipv4{};
    std::copy_n(bytes.begin(), ipv4.size(), ipv4.begin());
    return ipv4_public(ipv4, allow_loopback_for_tests);
  }
  if (inet_pton(AF_INET6, std::string(address).c_str(), bytes.data()) == 1) {
    return ipv6_public(bytes, allow_loopback_for_tests);
  }
  return false;
}

std::vector<std::string>
resolve_validated_addresses(const ParsedUrl &url,
                            const InternetSourcePolicy &policy) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *raw = nullptr;
  const std::string port = std::to_string(url.port);
  const int result = getaddrinfo(url.host.c_str(), port.c_str(), &hints, &raw);
  if (result != 0 || raw == nullptr) {
    fetch_error("internet host resolution failed: " +
                std::string(gai_strerror(result)));
  }
  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addresses(raw, freeaddrinfo);
  std::set<std::string> unique;
  for (auto *entry = addresses.get(); entry != nullptr; entry = entry->ai_next) {
    std::array<char, INET6_ADDRSTRLEN> buffer{};
    const void *source = nullptr;
    if (entry->ai_family == AF_INET) {
      source = &reinterpret_cast<const sockaddr_in *>(entry->ai_addr)->sin_addr;
    } else if (entry->ai_family == AF_INET6) {
      source = &reinterpret_cast<const sockaddr_in6 *>(entry->ai_addr)->sin6_addr;
    }
    if (source == nullptr ||
        inet_ntop(entry->ai_family, source, buffer.data(), buffer.size()) ==
            nullptr) {
      continue;
    }
    const std::string address(buffer.data());
    if (!is_public_address(address, policy.allow_loopback_for_tests)) {
      fetch_error("internet host resolved to a prohibited address");
    }
    unique.insert(address);
  }
  if (unique.empty()) {
    fetch_error("internet host did not resolve to a usable address");
  }
  return {unique.begin(), unique.end()};
}

std::string header_value(const FetchResponse &response, std::string_view name) {
  std::string normalized(name);
  std::ranges::transform(normalized, normalized.begin(), [](unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  const auto found = response.headers.find(normalized);
  return found == response.headers.end() ? std::string{} : found->second;
}

} // namespace statewright::sources
