#include "statewright/core/file_io.hpp"

#include "statewright/common/error.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace statewright::core {
namespace {

void synchronize_directory(const std::filesystem::path &directory) {
  const int descriptor =
      ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (descriptor < 0) {
    throw common::Error(common::ErrorCode::filesystem_failure,
                        "cannot open parent directory for synchronization");
  }
  if (::fsync(descriptor) != 0) {
    const std::string message = std::strerror(errno);
    ::close(descriptor);
    throw common::Error(common::ErrorCode::filesystem_failure,
                        "cannot synchronize parent directory: " + message);
  }
  if (::close(descriptor) != 0) {
    throw common::Error(common::ErrorCode::filesystem_failure,
                        "cannot close synchronized parent directory");
  }
}

[[nodiscard]] mode_t mode_value(std::filesystem::perms permissions) {
  return static_cast<mode_t>(static_cast<unsigned int>(permissions) & 0777U);
}

} // namespace

std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input.is_open()) {
    throw common::Error(common::ErrorCode::filesystem_failure,
                        "cannot open file: " + path.string());
  }
  const std::streamsize size = input.tellg();
  if (size < 0) {
    throw common::Error(common::ErrorCode::filesystem_failure,
                        "cannot determine file size: " + path.string());
  }
  input.seekg(0);
  std::vector<std::byte> result(static_cast<std::size_t>(size));
  if (size > 0) {
    input.read(reinterpret_cast<char *>(result.data()), size);
  }
  if (!input) {
    throw common::Error(common::ErrorCode::filesystem_failure,
                        "cannot read file: " + path.string());
  }
  return result;
}

std::string read_text(const std::filesystem::path &path) {
  const auto bytes = read_bytes(path);
  return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

void atomic_write_bytes(const std::filesystem::path &path,
                        std::span<const std::byte> content,
                        std::optional<std::filesystem::perms> permissions) {
  std::filesystem::create_directories(path.parent_path());
  std::string pattern =
      (path.parent_path() / ("." + path.filename().string() + ".XXXXXX"))
          .string();
  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back('\0');
  const int descriptor = ::mkstemp(buffer.data());
  if (descriptor < 0) {
    throw common::Error(common::ErrorCode::filesystem_failure,
                        "cannot create temporary file: " +
                            std::string(std::strerror(errno)));
  }
  const std::filesystem::path temporary(buffer.data());
  try {
    std::size_t written = 0;
    while (written < content.size()) {
      const ssize_t count = ::write(
          descriptor, content.data() + written, content.size() - written);
      if (count < 0) {
        throw common::Error(common::ErrorCode::filesystem_failure,
                            "cannot write temporary file: " +
                                std::string(std::strerror(errno)));
      }
      written += static_cast<std::size_t>(count);
    }
    if (permissions && ::fchmod(descriptor, mode_value(*permissions)) != 0) {
      throw common::Error(common::ErrorCode::filesystem_failure,
                          "cannot set temporary file permissions");
    }
    if (::fsync(descriptor) != 0) {
      throw common::Error(common::ErrorCode::filesystem_failure,
                          "cannot synchronize temporary file");
    }
    if (::close(descriptor) != 0) {
      throw common::Error(common::ErrorCode::filesystem_failure,
                          "cannot close temporary file");
    }
    if (::rename(temporary.c_str(), path.c_str()) != 0) {
      throw common::Error(common::ErrorCode::filesystem_failure,
                          "cannot replace target file: " +
                              std::string(std::strerror(errno)));
    }
    synchronize_directory(path.parent_path());
  } catch (...) {
    ::close(descriptor);
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    throw;
  }
}

void atomic_write_text(const std::filesystem::path &path, std::string_view content,
                       std::optional<std::filesystem::perms> permissions) {
  const auto *data = reinterpret_cast<const std::byte *>(content.data());
  atomic_write_bytes(path, std::span<const std::byte>(data, content.size()),
                     permissions);
}

void durable_remove(const std::filesystem::path &path) {
  std::error_code error;
  const bool removed = std::filesystem::remove(path, error);
  if (error) {
    throw common::Error(common::ErrorCode::filesystem_failure,
                        "cannot remove file: " + path.string());
  }
  if (removed) {
    synchronize_directory(path.parent_path());
  }
}

} // namespace statewright::core

