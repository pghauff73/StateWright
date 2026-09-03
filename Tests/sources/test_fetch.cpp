#include "statewright/common/error.hpp"
#include "statewright/sources/fetch.hpp"
#include "statewright/sources/http_provider.hpp"
#include "statewright/sources/policy.hpp"

#include <catch2/catch_test_macros.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

class HttpFixtureServer final {
public:
  HttpFixtureServer() {
    listener_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listener_ < 0) {
      throw std::runtime_error("cannot create fixture socket");
    }
    const int enabled = 1;
    static_cast<void>(::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &enabled,
                                  sizeof(enabled)));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(listener_, reinterpret_cast<const sockaddr *>(&address),
               sizeof(address)) != 0 ||
        ::listen(listener_, 16) != 0) {
      const std::string message = std::strerror(errno);
      ::close(listener_);
      listener_ = -1;
      throw std::runtime_error("cannot bind fixture socket: " + message);
    }
    socklen_t size = sizeof(address);
    if (::getsockname(listener_, reinterpret_cast<sockaddr *>(&address),
                      &size) != 0) {
      ::close(listener_);
      listener_ = -1;
      throw std::runtime_error("cannot inspect fixture socket");
    }
    port_ = static_cast<int>(ntohs(address.sin_port));
    worker_ = std::jthread([this](std::stop_token stop) { serve(stop); });
  }

  ~HttpFixtureServer() {
    worker_.request_stop();
    if (listener_ >= 0) {
      static_cast<void>(::shutdown(listener_, SHUT_RDWR));
      static_cast<void>(::close(listener_));
      listener_ = -1;
    }
  }

  HttpFixtureServer(const HttpFixtureServer &) = delete;
  HttpFixtureServer &operator=(const HttpFixtureServer &) = delete;

  [[nodiscard]] int port() const noexcept { return port_; }

private:
  static void send_all(int descriptor, const char *data, std::size_t size) {
    std::size_t sent = 0;
    while (sent < size) {
      const ssize_t count =
          ::send(descriptor, data + sent, size - sent, MSG_NOSIGNAL);
      if (count <= 0) {
        return;
      }
      sent += static_cast<std::size_t>(count);
    }
  }

  static void respond(int descriptor, std::string status,
                      std::vector<std::string> headers,
                      std::vector<std::byte> body) {
    std::string head = "HTTP/1.1 " + std::move(status) + "\r\n";
    for (const auto &header : headers) {
      head += header + "\r\n";
    }
    head += "Connection: close\r\n\r\n";
    send_all(descriptor, head.data(), head.size());
    if (!body.empty()) {
      send_all(descriptor, reinterpret_cast<const char *>(body.data()),
               body.size());
    }
  }

  static std::vector<std::byte> body(std::string_view value) {
    std::vector<std::byte> result;
    result.reserve(value.size());
    for (const char character : value) {
      result.push_back(
          static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return result;
  }

  static std::string request_path(std::string_view request) {
    const auto first_space = request.find(' ');
    const auto second_space =
        first_space == std::string_view::npos
            ? std::string_view::npos
            : request.find(' ', first_space + 1U);
    if (first_space == std::string_view::npos ||
        second_space == std::string_view::npos) {
      return "/";
    }
    return std::string(request.substr(first_space + 1U,
                                      second_space - first_space - 1U));
  }

  static void handle(int descriptor, std::string request) {
    const std::string path = request_path(request);
    if (path == "/redirect") {
      respond(descriptor, "302 Found",
              {"Location: /ok", "Content-Length: 0"}, {});
      return;
    }
    if (path == "/loop-a") {
      respond(descriptor, "302 Found",
              {"Location: /loop-b", "Content-Length: 0"}, {});
      return;
    }
    if (path == "/loop-b") {
      respond(descriptor, "302 Found",
              {"Location: /loop-a", "Content-Length: 0"}, {});
      return;
    }
    if (path == "/large") {
      const std::vector<std::byte> content(512U, std::byte{'X'});
      respond(descriptor, "200 OK",
              {"Content-Type: text/plain", "Content-Length: 512"}, content);
      return;
    }
    if (path == "/headers") {
      const auto content = body("ok");
      respond(descriptor, "200 OK",
              {"Content-Type: text/plain", "Content-Length: 2",
               "X-Padding: " + std::string(2048U, 'p')},
              content);
      return;
    }
    if (path == "/gzip") {
      static constexpr std::array<unsigned char, 29> compressed = {
          0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
          0x73, 0x74, 0x1c, 0x05, 0xa3, 0x60, 0x14, 0x8c, 0x54, 0x00,
          0x00, 0x1a, 0xfb, 0x37, 0xb7, 0x00, 0x04, 0x00, 0x00};
      std::vector<std::byte> content;
      content.reserve(compressed.size());
      for (const unsigned char value : compressed) {
        content.push_back(static_cast<std::byte>(value));
      }
      respond(descriptor, "200 OK",
              {"Content-Type: text/plain", "Content-Encoding: gzip",
               "Content-Length: 29"},
              std::move(content));
      return;
    }
    if (path == "/conditional" &&
        request.find("If-None-Match: \"fixture-v1\"") != std::string::npos) {
      respond(descriptor, "304 Not Modified",
              {"ETag: \"fixture-v1\"", "Content-Length: 0"}, {});
      return;
    }
    const auto content = body("fixture algorithm");
    respond(descriptor, "200 OK",
            {"Content-Type: text/plain; charset=utf-8",
             "ETag: \"fixture-v1\"", "Content-Length: 17"},
            content);
  }

  void serve(std::stop_token stop) const {
    while (!stop.stop_requested()) {
      const int descriptor = ::accept4(listener_, nullptr, nullptr, SOCK_CLOEXEC);
      if (descriptor < 0) {
        return;
      }
      std::string request;
      std::array<char, 4096> buffer{};
      while (request.find("\r\n\r\n") == std::string::npos &&
             request.size() < 16384U) {
        const ssize_t count = ::recv(descriptor, buffer.data(), buffer.size(), 0);
        if (count <= 0) {
          break;
        }
        request.append(buffer.data(), static_cast<std::size_t>(count));
      }
      handle(descriptor, std::move(request));
      static_cast<void>(::close(descriptor));
    }
  }

  mutable int listener_ = -1;
  int port_ = 0;
  std::jthread worker_;
};

statewright::sources::InternetSourcePolicy fixture_policy(int port) {
  statewright::sources::InternetSourcePolicy policy;
  policy.allowed_schemes = {"http"};
  policy.allowed_ports = {port};
  policy.maximum_header_bytes = 4096U;
  policy.maximum_response_bytes = 4096U;
  policy.maximum_decompressed_bytes = 4096U;
  policy.connect_timeout_seconds = 1;
  policy.request_timeout_seconds = 2;
  policy.require_tls_verification = false;
  policy.allow_loopback_for_tests = true;
  return statewright::sources::canonical_source_policy(std::move(policy));
}

std::string fixture_url(const HttpFixtureServer &server,
                        std::string_view path) {
  return "http://127.0.0.1:" + std::to_string(server.port()) +
         std::string(path);
}

} // namespace

TEST_CASE("internet URL validation is policy bounded") {
  using namespace statewright;
  const auto policy = sources::canonical_source_policy({});
  const auto parsed =
      sources::parse_and_validate_url("https://example.com/a?b=1", policy);
  REQUIRE(parsed.scheme == "https");
  REQUIRE(parsed.host == "example.com");
  REQUIRE(parsed.port == 443);
  REQUIRE(parsed.path_and_query == "/a?b=1");

  REQUIRE_THROWS_AS(
      sources::parse_and_validate_url("https://user:secret@example.com/", policy),
      common::Error);
  REQUIRE_THROWS_AS(
      sources::parse_and_validate_url("http://example.com/", policy),
      common::Error);
  REQUIRE_THROWS_AS(
      sources::parse_and_validate_url("https://example.com:8443/", policy),
      common::Error);
}

TEST_CASE("internet address validation blocks local and reserved networks") {
  using namespace statewright;
  REQUIRE_FALSE(sources::is_public_address("127.0.0.1"));
  REQUIRE(sources::is_public_address("127.0.0.1", true));
  REQUIRE_FALSE(sources::is_public_address("10.0.0.1"));
  REQUIRE_FALSE(sources::is_public_address("169.254.1.1"));
  REQUIRE_FALSE(sources::is_public_address("192.168.1.1"));
  REQUIRE_FALSE(sources::is_public_address("100.64.0.1"));
  REQUIRE_FALSE(sources::is_public_address("198.18.0.1"));
  REQUIRE_FALSE(sources::is_public_address("240.0.0.1"));
  REQUIRE_FALSE(sources::is_public_address("192.0.2.1"));
  REQUIRE_FALSE(sources::is_public_address("::1"));
  REQUIRE(sources::is_public_address("::1", true));
  REQUIRE_FALSE(sources::is_public_address("::ffff:127.0.0.1"));
  REQUIRE_FALSE(sources::is_public_address("2002:c0a8:0101::"));
  REQUIRE(sources::is_public_address("8.8.8.8"));
  REQUIRE(sources::is_public_address("2606:4700:4700::1111"));
}

TEST_CASE("internet redirects are resolved and policy validated") {
  using namespace statewright;
  auto policy = sources::InternetSourcePolicy{};
  policy.allowed_schemes = {"http", "https"};
  policy.allowed_ports = {80, 443};
  policy = sources::canonical_source_policy(std::move(policy));
  const auto relative = sources::resolve_and_validate_redirect(
      "https://example.com/a/b", "../algorithm?q=1", policy);
  REQUIRE(relative.canonical_url == "https://example.com/algorithm?q=1");
  REQUIRE_THROWS_AS(sources::resolve_and_validate_redirect(
                        "https://example.com/", "file:///etc/passwd", policy),
                    common::Error);
}

TEST_CASE("curl provider pins addresses and follows bounded redirects") {
  using namespace statewright;
  const HttpFixtureServer server;
  sources::CurlHttpFetchProvider provider;
  sources::FetchRequest request;
  request.url = fixture_url(server, "/redirect");
  request.policy = fixture_policy(server.port());
  const auto response = provider.fetch(request);
  REQUIRE(response.http_status == 200);
  REQUIRE(response.requested_url == request.url);
  REQUIRE(response.final_url == fixture_url(server, "/ok"));
  REQUIRE(response.redirect_chain ==
          std::vector<std::string>{fixture_url(server, "/ok")});
  REQUIRE(response.resolved_addresses ==
          std::vector<std::string>{"127.0.0.1"});
  REQUIRE(response.body.size() == 17U);
  REQUIRE(response.decompressed_bytes == 17U);
  REQUIRE(response.provider_identity.starts_with("libcurl/"));
}

TEST_CASE("curl provider rejects loops credentials headers and cancellation") {
  using namespace statewright;
  const HttpFixtureServer server;
  sources::CurlHttpFetchProvider provider;
  sources::FetchRequest loop;
  loop.url = fixture_url(server, "/loop-a");
  loop.policy = fixture_policy(server.port());
  REQUIRE_THROWS_AS(provider.fetch(loop), common::Error);

  auto credential = loop;
  credential.url = fixture_url(server, "/ok");
  credential.headers = {{"Authorization", "Bearer secret"}};
  REQUIRE_THROWS_AS(provider.fetch(credential), common::Error);

  auto cancelled = loop;
  cancelled.url = fixture_url(server, "/ok");
  cancelled.cancellation_requested = [] { return true; };
  REQUIRE_THROWS_AS(provider.fetch(cancelled), common::Error);
}

TEST_CASE("curl provider enforces header body and decompression limits") {
  using namespace statewright;
  const HttpFixtureServer server;
  sources::CurlHttpFetchProvider provider;

  sources::FetchRequest request;
  request.url = fixture_url(server, "/large");
  request.policy = fixture_policy(server.port());
  request.policy.maximum_response_bytes = 64U;
  request.policy.maximum_decompressed_bytes = 128U;
  REQUIRE_THROWS_AS(provider.fetch(request), common::Error);

  request.url = fixture_url(server, "/headers");
  request.policy = fixture_policy(server.port());
  request.policy.maximum_header_bytes = 128U;
  REQUIRE_THROWS_AS(provider.fetch(request), common::Error);

  request.url = fixture_url(server, "/gzip");
  request.policy = fixture_policy(server.port());
  request.policy.maximum_response_bytes = 64U;
  request.policy.maximum_decompressed_bytes = 128U;
  REQUIRE_THROWS_AS(provider.fetch(request), common::Error);
}

TEST_CASE("curl provider supports conditional not-modified requests") {
  using namespace statewright;
  const HttpFixtureServer server;
  sources::CurlHttpFetchProvider provider;
  sources::FetchRequest request;
  request.url = fixture_url(server, "/conditional");
  request.policy = fixture_policy(server.port());
  request.headers = {{"If-None-Match", "\"fixture-v1\""}};
  const auto response = provider.fetch(request);
  REQUIRE(response.http_status == 304);
  REQUIRE(response.body.empty());
  REQUIRE(sources::header_value(response, "etag") == "\"fixture-v1\"");
}
