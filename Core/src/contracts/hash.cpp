#include "statewright/contracts/hash.hpp"

#include "statewright/common/error.hpp"

#include <array>
#include <iomanip>
#include <fstream>
#include <memory>
#include <openssl/evp.h>
#include <sstream>

namespace statewright::contracts {
namespace {

struct EvpContextDeleter final {
  void operator()(EVP_MD_CTX *context) const noexcept { EVP_MD_CTX_free(context); }
};

[[nodiscard]] std::string finish_digest(EVP_MD_CTX *context) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int digest_size = 0;
  if (EVP_DigestFinal_ex(context, digest.data(), &digest_size) != 1 ||
      digest_size != 32U) {
    throw common::Error(common::ErrorCode::cryptographic_failure,
                        "OpenSSL SHA-256 finalization failed");
  }

  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (unsigned int index = 0; index < digest_size; ++index) {
    output << std::setw(2) << static_cast<unsigned int>(digest[index]);
  }
  return output.str();
}

[[nodiscard]] std::unique_ptr<EVP_MD_CTX, EvpContextDeleter>
new_sha256_context() {
  std::unique_ptr<EVP_MD_CTX, EvpContextDeleter> context(EVP_MD_CTX_new());
  if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
    throw common::Error(common::ErrorCode::cryptographic_failure,
                        "OpenSSL SHA-256 initialization failed");
  }
  return context;
}

} // namespace

std::string sha256_bytes(std::span<const std::byte> bytes) {
  auto context = new_sha256_context();
  if (EVP_DigestUpdate(context.get(), bytes.data(), bytes.size()) != 1) {
    throw common::Error(common::ErrorCode::cryptographic_failure,
                        "OpenSSL SHA-256 update failed");
  }
  return finish_digest(context.get());
}

std::string sha256_text(std::string_view text) {
  const auto *data = reinterpret_cast<const std::byte *>(text.data());
  return sha256_bytes(std::span<const std::byte>(data, text.size()));
}

std::string sha256_json(const Json &value) {
  return sha256_text(canonical_json(value));
}

std::string sha256_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    throw common::Error(common::ErrorCode::filesystem_failure,
                        "cannot open file for SHA-256: " + path.string());
  }

  auto context = new_sha256_context();
  std::array<char, 1024U * 1024U> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize count = input.gcount();
    if (count > 0 &&
        EVP_DigestUpdate(context.get(), buffer.data(),
                         static_cast<std::size_t>(count)) != 1) {
      throw common::Error(common::ErrorCode::cryptographic_failure,
                          "OpenSSL SHA-256 file update failed");
    }
  }
  if (!input.eof()) {
    throw common::Error(common::ErrorCode::filesystem_failure,
                        "cannot read file for SHA-256: " + path.string());
  }
  return finish_digest(context.get());
}

} // namespace statewright::contracts

