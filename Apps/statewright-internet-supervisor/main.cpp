#include "statewright/common/error.hpp"
#include "statewright/contracts/canonical_json.hpp"
#include "statewright/egcf/internet_improvement_supervisor.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using Json = statewright::contracts::Json;
using Invocation = statewright::egcf::InternetSupervisorInvocation;
using Policy = statewright::egcf::InternetSupervisorPolicy;

volatile std::sig_atomic_t stop_requested = 0;
constexpr std::size_t maximum_configuration_bytes = 64U * 1024U;

void handle_stop_signal(int) { stop_requested = 1; }

[[noreturn]] void fail(std::string message) {
  throw std::runtime_error(std::move(message));
}

std::string read_text(const std::filesystem::path &path) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (!error && size > maximum_configuration_bytes) {
    fail("file exceeds the 64 KiB configuration limit: " + path.string());
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    fail("cannot open file: " + path.string());
  }

  std::string contents;
  if (!error) {
    contents.reserve(static_cast<std::size_t>(size));
  }
  std::array<char, 8192> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = input.gcount();
    if (count <= 0) {
      break;
    }
    if (contents.size() + static_cast<std::size_t>(count) >
        maximum_configuration_bytes) {
      fail("file exceeds the 64 KiB configuration limit: " + path.string());
    }
    contents.append(buffer.data(), static_cast<std::size_t>(count));
  }
  if (!input.eof()) {
    fail("cannot read file: " + path.string());
  }
  return contents;
}

int parse_int(std::string_view name, std::string_view value) {
  std::size_t consumed = 0;
  long long parsed = 0;
  try {
    parsed = std::stoll(std::string(value), &consumed, 10);
  } catch (const std::exception &) {
    fail(std::string(name) + " must be an integer");
  }
  if (consumed != value.size() || parsed < std::numeric_limits<int>::min() ||
      parsed > std::numeric_limits<int>::max()) {
    fail(std::string(name) + " is out of range");
  }
  return static_cast<int>(parsed);
}

std::size_t parse_size(std::string_view name, std::string_view value) {
  std::size_t consumed = 0;
  unsigned long long parsed = 0;
  try {
    parsed = std::stoull(std::string(value), &consumed, 10);
  } catch (const std::exception &) {
    fail(std::string(name) + " must be a non-negative integer");
  }
  if (consumed != value.size() ||
      parsed > std::numeric_limits<std::size_t>::max()) {
    fail(std::string(name) + " is out of range");
  }
  return static_cast<std::size_t>(parsed);
}

void usage(std::ostream &output) {
  output
      << "Usage:\n"
      << "  statewright-internet-supervisor --workspace PATH --worker-id ID [options]\n\n"
      << "Required:\n"
      << "  --workspace PATH\n"
      << "  --worker-id ID\n\n"
      << "Options:\n"
      << "  --statewright PATH\n"
      << "  --resource-root PATH\n"
      << "  --request-file PATH\n"
      << "  --event-log PATH\n"
      << "  --maximum-cycles N\n"
      << "  --maximum-failures N\n"
      << "  --maximum-wall-seconds N\n"
      << "  --child-timeout-seconds N\n"
      << "  --cycle-interval-seconds N\n"
      << "  --action-lease-seconds N\n"
      << "  --fetch-lease-seconds N\n"
      << "  --action-deadline-seconds N\n"
      << "  --success-delay-milliseconds N\n"
      << "  --failure-backoff-initial-milliseconds N\n"
      << "  --failure-backoff-maximum-milliseconds N\n"
      << "  --maximum-child-output-bytes N\n"
      << "  --once\n"
      << "  --version\n"
      << "  --help\n";
}

std::filesystem::path default_statewright_path(const char *argv0) {
  std::error_code error;
  const auto self = std::filesystem::canonical("/proc/self/exe", error);
  if (!error) {
    return self.parent_path() / "statewright";
  }
  const auto supplied = std::filesystem::absolute(argv0, error);
  if (!error) {
    return supplied.parent_path() / "statewright";
  }
  return "statewright";
}

struct Configuration final {
  std::filesystem::path statewright;
  std::filesystem::path workspace;
  std::filesystem::path resource_root;
  std::filesystem::path event_log;
  std::string worker_id;
  Policy policy;
};

Configuration parse_arguments(int argc, char **argv) {
  Configuration result;
  result.statewright = default_statewright_path(argv[0]);
  auto next = [&](int &index, std::string_view option) -> std::string_view {
    if (index + 1 >= argc) {
      fail(std::string(option) + " requires a value");
    }
    ++index;
    return argv[index];
  };
  for (int index = 1; index < argc; ++index) {
    const std::string_view option = argv[index];
    if (option == "--help") {
      usage(std::cout);
      std::exit(0);
    }
    if (option == "--version") {
      std::cout
          << statewright::egcf::internet_improvement_supervisor_version
          << '\n';
      std::exit(0);
    }
    if (option == "--once") {
      result.policy.maximum_cycles_per_wake = 1;
    } else if (option == "--statewright") {
      result.statewright = next(index, option);
    } else if (option == "--workspace") {
      result.workspace = next(index, option);
    } else if (option == "--worker-id") {
      result.worker_id = next(index, option);
    } else if (option == "--resource-root") {
      result.resource_root = next(index, option);
    } else if (option == "--request-file") {
      const auto path = std::filesystem::path(next(index, option));
      result.policy.request_template =
          statewright::contracts::parse_json(read_text(path));
    } else if (option == "--event-log") {
      result.event_log = next(index, option);
    } else if (option == "--maximum-cycles") {
      result.policy.maximum_cycles_per_wake =
          parse_int(option, next(index, option));
    } else if (option == "--maximum-failures") {
      result.policy.maximum_consecutive_failures =
          parse_int(option, next(index, option));
    } else if (option == "--maximum-wall-seconds") {
      result.policy.maximum_wall_seconds =
          parse_int(option, next(index, option));
    } else if (option == "--child-timeout-seconds") {
      result.policy.child_timeout_seconds =
          parse_int(option, next(index, option));
    } else if (option == "--cycle-interval-seconds") {
      result.policy.cycle_interval_seconds =
          parse_int(option, next(index, option));
    } else if (option == "--action-lease-seconds") {
      result.policy.action_lease_seconds =
          parse_int(option, next(index, option));
    } else if (option == "--fetch-lease-seconds") {
      result.policy.fetch_lease_seconds =
          parse_int(option, next(index, option));
    } else if (option == "--action-deadline-seconds") {
      result.policy.action_deadline_seconds =
          parse_int(option, next(index, option));
    } else if (option == "--success-delay-milliseconds") {
      result.policy.success_delay_milliseconds =
          parse_int(option, next(index, option));
    } else if (option == "--failure-backoff-initial-milliseconds") {
      result.policy.failure_backoff_initial_milliseconds =
          parse_int(option, next(index, option));
    } else if (option == "--failure-backoff-maximum-milliseconds") {
      result.policy.failure_backoff_maximum_milliseconds =
          parse_int(option, next(index, option));
    } else if (option == "--maximum-child-output-bytes") {
      result.policy.maximum_child_output_bytes =
          parse_size(option, next(index, option));
    } else {
      fail("unsupported option: " + std::string(option));
    }
  }
  if (result.workspace.empty() || result.worker_id.empty()) {
    fail("--workspace and --worker-id are required");
  }
  std::filesystem::create_directories(result.workspace);
  result.workspace = std::filesystem::weakly_canonical(result.workspace);
  if (!result.resource_root.empty()) {
    result.resource_root =
        std::filesystem::weakly_canonical(result.resource_root);
  }
  result.statewright = std::filesystem::weakly_canonical(result.statewright);
  if (!std::filesystem::is_regular_file(result.statewright) ||
      ::access(result.statewright.c_str(), X_OK) != 0) {
    fail("statewright executable is unavailable: " +
         result.statewright.string());
  }
  result.policy = statewright::egcf::canonical_internet_supervisor_policy(
      std::move(result.policy));
  return result;
}

class TemporaryFile final {
public:
  explicit TemporaryFile(std::string_view label) {
    std::string pattern = "/tmp/statewright-supervisor-" +
                          std::string(label) + "-XXXXXX";
    std::vector<char> mutable_pattern(pattern.begin(), pattern.end());
    mutable_pattern.push_back('\0');
    descriptor_ = ::mkstemp(mutable_pattern.data());
    if (descriptor_ < 0) {
      fail("cannot create supervisor temporary file");
    }
    path_ = mutable_pattern.data();
  }

  ~TemporaryFile() {
    close_descriptor();
    std::error_code ignored;
    std::filesystem::remove(path_, ignored);
  }

  TemporaryFile(const TemporaryFile &) = delete;
  TemporaryFile &operator=(const TemporaryFile &) = delete;

  [[nodiscard]] int descriptor() const noexcept { return descriptor_; }
  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return path_;
  }
  void close_descriptor() noexcept {
    if (descriptor_ >= 0) {
      static_cast<void>(::close(descriptor_));
      descriptor_ = -1;
    }
  }

private:
  int descriptor_ = -1;
  std::filesystem::path path_;
};

std::string read_bounded(const std::filesystem::path &path,
                         std::size_t maximum, bool &exceeded) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    fail("cannot read supervisor child output");
  }
  std::string result;
  result.resize(maximum + 1U);
  input.read(result.data(), static_cast<std::streamsize>(result.size()));
  result.resize(static_cast<std::size_t>(input.gcount()));
  if (result.size() > maximum) {
    exceeded = true;
    result.resize(maximum);
  }
  return result;
}

void signal_process_group(pid_t child, int signal_value) {
  if (::kill(-child, signal_value) != 0) {
    static_cast<void>(::kill(child, signal_value));
  }
}

void terminate_child(pid_t child, int &status) {
  signal_process_group(child, SIGTERM);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
  while (std::chrono::steady_clock::now() < deadline) {
    const pid_t waited = ::waitpid(child, &status, WNOHANG);
    if (waited == child) {
      return;
    }
    if (waited < 0 && errno != EINTR) {
      return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  signal_process_group(child, SIGKILL);
  while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
  }
}

Invocation run_child(const std::filesystem::path &executable,
                     const Json &request, std::chrono::milliseconds timeout,
                     std::size_t maximum_output_bytes) {
  TemporaryFile stdout_file("stdout");
  TemporaryFile stderr_file("stderr");
  const std::string request_text =
      statewright::contracts::canonical_json(request);
  const std::string executable_text = executable.string();
  const pid_t child = ::fork();
  if (child < 0) {
    fail("cannot fork statewright supervisor child");
  }
  if (child == 0) {
    static_cast<void>(::setpgid(0, 0));
    if (::dup2(stdout_file.descriptor(), STDOUT_FILENO) < 0 ||
        ::dup2(stderr_file.descriptor(), STDERR_FILENO) < 0) {
      _exit(126);
    }
    rlimit limit{};
    const auto file_limit = maximum_output_bytes ==
                                    std::numeric_limits<std::size_t>::max()
                                ? maximum_output_bytes
                                : maximum_output_bytes + 1U;
    limit.rlim_cur = static_cast<rlim_t>(file_limit);
    limit.rlim_max = static_cast<rlim_t>(file_limit);
    static_cast<void>(::setrlimit(RLIMIT_FSIZE, &limit));
    std::vector<char *> arguments = {
        const_cast<char *>(executable_text.c_str()),
        const_cast<char *>("internet-improvement"),
        const_cast<char *>(request_text.c_str()), nullptr};
    ::execv(executable_text.c_str(), arguments.data());
    const std::string message =
        "cannot execute statewright: " + std::string(std::strerror(errno)) +
        "\n";
    static_cast<void>(
        ::write(STDERR_FILENO, message.data(), message.size()));
    _exit(127);
  }

  static_cast<void>(::setpgid(child, child));
  Invocation result;
  int status = 0;
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (true) {
    const pid_t waited = ::waitpid(child, &status, WNOHANG);
    if (waited == child) {
      break;
    }
    if (waited < 0 && errno != EINTR) {
      terminate_child(child, status);
      fail("cannot wait for statewright supervisor child");
    }
    if (stop_requested != 0) {
      result.cancelled = true;
      terminate_child(child, status);
      break;
    }
    if (std::chrono::steady_clock::now() >= deadline) {
      result.timed_out = true;
      terminate_child(child, status);
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  stdout_file.close_descriptor();
  stderr_file.close_descriptor();
  if (WIFEXITED(status)) {
    result.exit_code = WEXITSTATUS(status);
  }
  if (WIFSIGNALED(status)) {
    result.termination_signal = WTERMSIG(status);
  }
  bool stdout_exceeded = false;
  bool stderr_exceeded = false;
  result.stdout_text =
      read_bounded(stdout_file.path(), maximum_output_bytes, stdout_exceeded);
  result.stderr_text =
      read_bounded(stderr_file.path(), maximum_output_bytes, stderr_exceeded);
  result.output_limit_exceeded = stdout_exceeded || stderr_exceeded;
  return result;
}

class EventWriter final {
public:
  explicit EventWriter(const std::filesystem::path &path) {
    if (path.empty()) {
      return;
    }
    if (!path.parent_path().empty()) {
      std::filesystem::create_directories(path.parent_path());
    }
    output_.emplace(path, std::ios::binary | std::ios::app);
    if (!*output_) {
      fail("cannot open supervisor event log: " + path.string());
    }
    std::filesystem::permissions(
        path, std::filesystem::perms::owner_read |
                  std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace);
  }

  void write(const Json &event) {
    const auto line = statewright::contracts::canonical_json(event);
    std::cout << line << '\n' << std::flush;
    if (output_) {
      *output_ << line << '\n' << std::flush;
      if (!*output_) {
        fail("cannot write supervisor event log");
      }
    }
  }

private:
  std::optional<std::ofstream> output_;
};

} // namespace

int main(int argc, char **argv) {
  try {
    std::signal(SIGINT, handle_stop_signal);
    std::signal(SIGTERM, handle_stop_signal);
    auto configuration = parse_arguments(argc, argv);
    EventWriter writer(configuration.event_log);
    statewright::egcf::InternetImprovementSupervisor supervisor(
        configuration.workspace, configuration.resource_root,
        configuration.worker_id,
        [&](const Json &request, std::chrono::milliseconds timeout,
            std::size_t maximum_output_bytes) {
          return run_child(configuration.statewright, request, timeout,
                           maximum_output_bytes);
        },
        [&](const Json &event) { writer.write(event); }, {},
        [](std::chrono::milliseconds delay) {
          const auto deadline = std::chrono::steady_clock::now() + delay;
          while (stop_requested == 0 &&
                 std::chrono::steady_clock::now() < deadline) {
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now());
            std::this_thread::sleep_for(
                std::min(std::chrono::milliseconds(50), remaining));
          }
        },
        [] { return stop_requested != 0; });
    const auto summary = supervisor.run(configuration.policy);
    return summary.successful ? 0 : 1;
  } catch (const statewright::common::Error &error) {
    std::cerr << statewright::common::error_code_name(error.code()) << ": "
              << error.what() << '\n';
    return 2;
  } catch (const std::exception &error) {
    std::cerr << "internet_supervisor_failure: " << error.what() << '\n';
    return 2;
  }
}
