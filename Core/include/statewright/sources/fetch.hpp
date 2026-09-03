#pragma once

#include "statewright/sources/policy.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::sources {

struct ParsedUrl final {
  std::string scheme;
  std::string host;
  int port = 0;
  std::string path_and_query;
  std::string canonical_url;
};

struct FetchRequest final {
  std::string url;
  std::string method = "GET";
  std::map<std::string, std::string> headers;
  InternetSourcePolicy policy;
  std::function<bool()> cancellation_requested;
};

struct FetchResponse final {
  std::string requested_url;
  std::string final_url;
  std::vector<std::string> resolved_addresses;
  std::vector<std::string> redirect_chain;
  int http_status = 0;
  std::map<std::string, std::string> headers;
  std::vector<std::byte> body;
  bool tls_verified = false;
  std::size_t compressed_bytes = 0;
  std::size_t decompressed_bytes = 0;
  long long total_time_milliseconds = 0;
  std::string provider_identity;
};

[[nodiscard]] ParsedUrl parse_and_validate_url(
    std::string_view url, const InternetSourcePolicy &policy);
[[nodiscard]] bool is_public_address(std::string_view address,
                                     bool allow_loopback_for_tests = false);
[[nodiscard]] std::vector<std::string>
resolve_validated_addresses(const ParsedUrl &url,
                            const InternetSourcePolicy &policy);
[[nodiscard]] ParsedUrl resolve_and_validate_redirect(
    std::string_view base_url, std::string_view location,
    const InternetSourcePolicy &policy);
[[nodiscard]] std::string header_value(const FetchResponse &response,
                                       std::string_view name);

} // namespace statewright::sources
