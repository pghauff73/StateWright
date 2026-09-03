#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

volatile std::sig_atomic_t stopping = 0;
volatile std::sig_atomic_t listener_descriptor = -1;

void stop_server(int) {
  stopping = 1;
  if (listener_descriptor >= 0) {
    static_cast<void>(::close(static_cast<int>(listener_descriptor)));
    listener_descriptor = -1;
  }
}

void send_all(int descriptor, std::string_view value) {
  std::size_t sent = 0;
  while (sent < value.size()) {
    const ssize_t count = ::send(descriptor, value.data() + sent,
                                 value.size() - sent, MSG_NOSIGNAL);
    if (count <= 0) {
      return;
    }
    sent += static_cast<std::size_t>(count);
  }
}

void respond(int descriptor, std::string_view body) {
  const std::string head =
      "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n"
      "Content-Length: " +
      std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
  send_all(descriptor, head);
  send_all(descriptor, body);
}

int listen_loopback() {
  const int listener = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (listener < 0) {
    throw std::runtime_error("cannot create fixture socket");
  }
  const int enabled = 1;
  static_cast<void>(::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enabled,
                                sizeof(enabled)));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  if (::bind(listener, reinterpret_cast<const sockaddr *>(&address),
             sizeof(address)) != 0 ||
      ::listen(listener, 16) != 0) {
    const std::string message = std::strerror(errno);
    ::close(listener);
    throw std::runtime_error("cannot bind fixture socket: " + message);
  }
  return listener;
}

int listener_port(int listener) {
  sockaddr_in address{};
  socklen_t size = sizeof(address);
  if (::getsockname(listener, reinterpret_cast<sockaddr *>(&address), &size) !=
      0) {
    throw std::runtime_error("cannot inspect fixture socket");
  }
  return static_cast<int>(ntohs(address.sin_port));
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 2) {
      std::cerr << "usage: statewright_internet_fixture_server PORT_FILE\n";
      return 2;
    }
    std::signal(SIGINT, stop_server);
    std::signal(SIGTERM, stop_server);
    const int listener = listen_loopback();
    listener_descriptor = listener;
    const int port = listener_port(listener);
    {
      std::ofstream output(argv[1], std::ios::trunc);
      output.exceptions(std::ios::failbit | std::ios::badbit);
      output << port << '\n';
    }
    constexpr std::string_view body =
        "Identity algorithm; inputs: x; outputs: y; procedure: return the input\n";
    while (stopping == 0) {
      const int descriptor =
          ::accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
      if (descriptor < 0) {
        if (errno == EINTR) {
          continue;
        }
        break;
      }
      std::string request;
      std::array<char, 4096> buffer{};
      while (request.find("\r\n\r\n") == std::string::npos &&
             request.size() < 16384U) {
        const ssize_t count =
            ::recv(descriptor, buffer.data(), buffer.size(), 0);
        if (count <= 0) {
          break;
        }
        request.append(buffer.data(), static_cast<std::size_t>(count));
      }
      respond(descriptor, body);
      static_cast<void>(::close(descriptor));
    }
    if (listener_descriptor >= 0) {
      static_cast<void>(::close(listener));
      listener_descriptor = -1;
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
