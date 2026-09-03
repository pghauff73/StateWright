#include "statewright/common/error.hpp"
#include "statewright/contracts/canonical_json.hpp"
#include "statewright/core/workspace.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

class Fixture final {
public:
  Fixture() {
    const auto unique = std::to_string(::getpid()) + "-" +
                        std::to_string(std::chrono::steady_clock::now()
                                           .time_since_epoch()
                                           .count());
    base = std::filesystem::temp_directory_path() / ("statewright-" + unique);
    root = base / "repo";
    std::filesystem::create_directories(root);
  }

  ~Fixture() { std::filesystem::remove_all(base); }

  void write(const std::filesystem::path &relative, std::string_view content,
             std::filesystem::perms permissions =
                 std::filesystem::perms::owner_read |
                 std::filesystem::perms::owner_write |
                 std::filesystem::perms::group_read |
                 std::filesystem::perms::others_read) const {
    const auto path = root / relative;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << content;
    output.close();
    std::filesystem::permissions(path, permissions,
                                 std::filesystem::perm_options::replace);
  }

  std::filesystem::path base;
  std::filesystem::path root;
};

statewright::contracts::Json load_fixtures() {
  std::ifstream input(STATEWRIGHT_ORACLE_FIXTURES);
  REQUIRE(input.is_open());
  return statewright::contracts::Json::parse(input);
}

} // namespace

TEST_CASE("workspace rejects escape, absolute, and internal paths") {
  Fixture fixture;
  const statewright::core::Workspace workspace(fixture.root);
  REQUIRE_THROWS_AS(workspace.canonical("../escape"), statewright::common::Error);
  REQUIRE_THROWS_AS(workspace.canonical("/tmp/escape"), statewright::common::Error);
  REQUIRE_THROWS_AS(workspace.canonical(".ourd-agent/state.json"),
                    statewright::common::Error);
  REQUIRE_THROWS_AS(workspace.canonical(".statewright/events.jsonl"),
                    statewright::common::Error);
}

TEST_CASE("workspace scope uses the canonical resolved path") {
  Fixture fixture;
  fixture.write("secret.txt", "secret");
  const statewright::core::Workspace workspace(fixture.root);
  REQUIRE_THROWS_AS(workspace.require_scope("src/../secret.txt", {"src/**"}),
                    statewright::common::Error);
  REQUIRE(workspace.require_scope("src/main.cpp", {"src/**"}) ==
          "src/main.cpp");
}

TEST_CASE("workspace rejects symlink escape and skips symlink files") {
  Fixture fixture;
  fixture.write("inside.txt", "inside");
  const auto outside = fixture.base / "outside.txt";
  std::ofstream(outside) << "outside";
  std::filesystem::create_symlink(outside, fixture.root / "linked.txt");
  const statewright::core::Workspace workspace(fixture.root);
  REQUIRE_THROWS_AS(workspace.canonical("linked.txt"),
                    statewright::common::Error);
  REQUIRE_FALSE(workspace.snapshot().contains("linked.txt"));
}

TEST_CASE("workspace snapshot ignores internal and packaging artifacts") {
  Fixture fixture;
  fixture.write("README.md", "hello\n");
  const statewright::core::Workspace workspace(fixture.root);
  const auto before = workspace.snapshot_hash();
  fixture.write(".ourd-agent/state.json", "{}\n");
  fixture.write(".statewright/runtime.json", "{}\n");
  fixture.write("build/generated.cpp", "generated\n");
  fixture.write("project.egg-info/PKG-INFO", "generated\n");
  REQUIRE(workspace.snapshot_hash() == before);
}

TEST_CASE("workspace snapshot matches the frozen Python oracle") {
  Fixture fixture;
  fixture.write("README.md", "StateWright\n");
  fixture.write("src/main.cpp", "int main() { return 0; }\n",
                std::filesystem::perms::owner_read |
                    std::filesystem::perms::owner_write |
                    std::filesystem::perms::owner_exec |
                    std::filesystem::perms::group_read |
                    std::filesystem::perms::group_exec |
                    std::filesystem::perms::others_read |
                    std::filesystem::perms::others_exec);
  fixture.write("build/ignored.txt", "ignored\n");
  fixture.write(".ourd-agent/state.json", "{}\n");
  const statewright::core::Workspace workspace(fixture.root);
  const auto fixtures = load_fixtures();
  REQUIRE(workspace.snapshot() ==
          fixtures.at("workspace_case").at("snapshot")
              .get<std::map<std::string, std::string>>());
  REQUIRE(workspace.snapshot_hash() ==
          fixtures.at("workspace_case").at("snapshot_hash").get<std::string>());
}

