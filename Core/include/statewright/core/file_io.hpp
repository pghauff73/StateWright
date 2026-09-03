#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace statewright::core {

[[nodiscard]] std::vector<std::byte>
read_bytes(const std::filesystem::path &path);
[[nodiscard]] std::string read_text(const std::filesystem::path &path);

void atomic_write_bytes(
    const std::filesystem::path &path, std::span<const std::byte> content,
    std::optional<std::filesystem::perms> permissions = std::nullopt);
void atomic_write_text(
    const std::filesystem::path &path, std::string_view content,
    std::optional<std::filesystem::perms> permissions = std::nullopt);
void durable_remove(const std::filesystem::path &path);

} // namespace statewright::core

