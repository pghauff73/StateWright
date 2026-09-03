#include "statewright/common/error.hpp"
#include "statewright/contracts/canonical_json.hpp"
#include "statewright/core/authority.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

class AuthorityFixture final {
public:
  AuthorityFixture() {
    root = std::filesystem::temp_directory_path() /
           ("statewright-authority-" + std::to_string(::getpid()) + "-" +
            std::to_string(std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count()));
    std::filesystem::create_directories(root);
    std::ofstream(root / "README.md") << "StateWright\n";
    std::filesystem::create_directories(root / "src");
    const auto main = root / "src/main.cpp";
    std::ofstream(main) << "int main() { return 0; }\n";
    std::filesystem::permissions(
        main,
        std::filesystem::perms::owner_read |
            std::filesystem::perms::owner_write |
            std::filesystem::perms::owner_exec |
            std::filesystem::perms::group_read |
            std::filesystem::perms::group_exec |
            std::filesystem::perms::others_read |
            std::filesystem::perms::others_exec,
        std::filesystem::perm_options::replace);
    std::filesystem::create_directories(root / "build");
    std::ofstream(root / "build/ignored.txt") << "ignored\n";
    std::filesystem::create_directories(root / ".ourd-agent");
    std::ofstream(root / ".ourd-agent/state.json") << "{}\n";
  }
  ~AuthorityFixture() { std::filesystem::remove_all(root); }
  std::filesystem::path root;
};

statewright::contracts::Json load_authority_fixture() {
  std::ifstream input(STATEWRIGHT_ORACLE_FIXTURES);
  REQUIRE(input.is_open());
  return statewright::contracts::Json::parse(input).at("authority_case");
}

} // namespace

TEST_CASE("read-only authority matches the frozen Python oracle") {
  AuthorityFixture fixture;
  const statewright::core::Workspace workspace(fixture.root);
  const auto authority = statewright::core::read_only_authority(workspace);
  const auto expected = load_authority_fixture();
  REQUIRE(statewright::core::to_json(authority) == expected.at("manifest"));
  REQUIRE(authority.authority_hash ==
          expected.at("authority_hash").get<std::string>());
  statewright::core::validate_authority(authority, workspace);
}

TEST_CASE("authority rejects snapshot drift and invalid retry bounds") {
  AuthorityFixture fixture;
  const statewright::core::Workspace workspace(fixture.root);
  auto authority = statewright::core::read_only_authority(workspace);
  std::ofstream(fixture.root / "changed.txt") << "changed\n";
  REQUIRE_THROWS_AS(statewright::core::validate_authority(authority, workspace),
                    statewright::common::Error);
  authority.source_snapshot_hash = workspace.snapshot_hash();
  authority.max_retries_per_action = 11;
  REQUIRE_THROWS_AS(statewright::core::validate_authority(authority, workspace),
                    statewright::common::Error);
}

TEST_CASE("read-only authority cannot grant mutation capabilities") {
  AuthorityFixture fixture;
  const statewright::core::Workspace workspace(fixture.root);
  auto authority = statewright::core::read_only_authority(workspace);
  authority.command_capabilities = {"process.execute"};
  REQUIRE_THROWS_AS(statewright::core::validate_authority(authority, workspace),
                    statewright::common::Error);
  authority.command_capabilities.clear();
  authority.semantic_capability_ceiling = "C3";
  REQUIRE_THROWS_AS(statewright::core::validate_authority(authority, workspace),
                    statewright::common::Error);
}

TEST_CASE("authority expiry is evaluated against an explicit clock") {
  AuthorityFixture fixture;
  const statewright::core::Workspace workspace(fixture.root);
  auto authority = statewright::core::read_only_authority(workspace);
  authority.expires_at = "2026-09-02T00:00:00Z";
  const auto after_expiry =
      std::chrono::system_clock::from_time_t(static_cast<std::time_t>(1788307201));
  REQUIRE_THROWS_AS(
      statewright::core::validate_authority(authority, workspace, true,
                                             after_expiry),
      statewright::common::Error);
}

TEST_CASE("authority parser rejects unknown fields") {
  auto fixture = load_authority_fixture().at("manifest");
  fixture["unexpected"] = true;
  REQUIRE_THROWS_AS(statewright::core::authority_from_json(fixture),
                    statewright::common::Error);
}
