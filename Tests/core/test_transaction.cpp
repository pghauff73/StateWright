#include "statewright/common/error.hpp"
#include "statewright/contracts/hash.hpp"
#include "statewright/core/event_store.hpp"
#include "statewright/core/file_io.hpp"
#include "statewright/core/transaction.hpp"
#include "statewright/core/workspace.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {

class TransactionFixture final {
public:
  TransactionFixture() {
    base = std::filesystem::temp_directory_path() /
           ("statewright-transactions-" + std::to_string(::getpid()) + "-" +
            std::to_string(std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count()));
    root = base / "repo";
    state = root / ".statewright";
    std::filesystem::create_directories(root / "src");
    std::ofstream(root / "README.md") << "before\n";
    std::ofstream(root / "src/main.cpp") << "int value = 1;\n";
  }
  ~TransactionFixture() { std::filesystem::remove_all(base); }

  std::filesystem::path base;
  std::filesystem::path root;
  std::filesystem::path state;
};

} // namespace

TEST_CASE("transaction stages candidates without mutating the workspace") {
  TransactionFixture fixture;
  const statewright::core::Workspace workspace(fixture.root);
  statewright::core::EventStore events(fixture.state / "events.jsonl");
  statewright::core::TransactionManager manager(workspace, fixture.state,
                                                 "authority-hash", events);
  const auto before = workspace.snapshot_hash();
  const auto record = manager.prepare_changes({
      {.type = statewright::core::ChangeType::write,
       .path = "README.md",
       .content = "after\n",
       .old_text = {},
       .new_text = {},
       .count = 1},
      {.type = statewright::core::ChangeType::replace,
       .path = "src/main.cpp",
       .content = {},
       .old_text = "1",
       .new_text = "2",
       .count = 1},
  });
  REQUIRE(record.status == "PREPARED");
  REQUIRE(record.operation == "multi_file");
  REQUIRE(statewright::core::read_text(fixture.root / "README.md") == "before\n");
  REQUIRE(workspace.snapshot_hash() == before);
  manager.verify_candidate(record);
  REQUIRE_FALSE(events.head().empty());
}

TEST_CASE("source drift blocks transaction apply") {
  TransactionFixture fixture;
  const statewright::core::Workspace workspace(fixture.root);
  statewright::core::EventStore events(fixture.state / "events.jsonl");
  statewright::core::TransactionManager manager(workspace, fixture.state,
                                                 "authority-hash", events);
  auto record = manager.prepare_write("README.md", "after\n");
  std::ofstream(fixture.root / "drift.txt") << "drift\n";
  REQUIRE_THROWS_AS(manager.apply(record), statewright::common::Error);
  REQUIRE(statewright::core::read_text(fixture.root / "README.md") == "before\n");
}

TEST_CASE("transaction applies verifies finalizes and rolls back exact bytes") {
  TransactionFixture fixture;
  const statewright::core::Workspace workspace(fixture.root);
  statewright::core::EventStore events(fixture.state / "events.jsonl");
  statewright::core::TransactionManager manager(workspace, fixture.state,
                                                 "authority-hash", events);
  const auto original_readme_hash = workspace.file_hash("README.md");
  auto record = manager.prepare_changes({
      {.type = statewright::core::ChangeType::write,
       .path = "README.md",
       .content = "after\n",
       .old_text = {},
       .new_text = {},
       .count = 1},
      {.type = statewright::core::ChangeType::write,
       .path = "new.txt",
       .content = "new\n",
       .old_text = {},
       .new_text = {},
       .count = 1},
  });
  manager.apply(record);
  REQUIRE(record.status == "APPLIED");
  REQUIRE(statewright::core::read_text(fixture.root / "README.md") == "after\n");
  REQUIRE(statewright::core::read_text(fixture.root / "new.txt") == "new\n");
  manager.verify_applied(record);
  manager.finalize(record, {"evidence-1", "evidence-1", "evidence-2"});
  REQUIRE(record.status == "VERIFIED");
  REQUIRE(record.verification_evidence_ids ==
          std::vector<std::string>{"evidence-1", "evidence-2"});
  manager.rollback(record);
  REQUIRE(record.status == "ROLLED_BACK");
  REQUIRE(workspace.file_hash("README.md") == original_readme_hash);
  REQUIRE_FALSE(std::filesystem::exists(fixture.root / "new.txt"));
  REQUIRE(events.events().size() == 4U);
}

TEST_CASE("transaction reload supports restart-safe verified rollback") {
  TransactionFixture fixture;
  const statewright::core::Workspace workspace(fixture.root);
  const auto before = workspace.snapshot_hash();
  std::string transaction_id;
  {
    statewright::core::EventStore events(fixture.state / "events.jsonl");
    statewright::core::TransactionManager manager(
        workspace, fixture.state, "authority-hash", events);
    auto record = manager.prepare_write("README.md", "after\n");
    transaction_id = record.transaction_id;
    manager.apply(record);
    manager.verify_applied(record);
    manager.finalize(record, {"evidence:sha256:" + std::string(64U, '1')});
  }

  statewright::core::EventStore recovered_events(fixture.state / "events.jsonl");
  statewright::core::TransactionManager recovered(
      workspace, fixture.state, "authority-hash", recovered_events);
  auto record = recovered.load(transaction_id);
  REQUIRE(record.status == "VERIFIED");
  REQUIRE(record.verification_evidence_ids.size() == 1U);
  recovered.rollback(record);
  REQUIRE(record.status == "ROLLED_BACK");
  REQUIRE(statewright::core::read_text(fixture.root / "README.md") == "before\n");
  REQUIRE(workspace.snapshot_hash() == before);
}

TEST_CASE("candidate tampering blocks apply and prepared transactions discard") {
  TransactionFixture fixture;
  const statewright::core::Workspace workspace(fixture.root);
  statewright::core::EventStore events(fixture.state / "events.jsonl");
  statewright::core::TransactionManager manager(workspace, fixture.state,
                                                 "authority-hash", events);
  auto record = manager.prepare_write("README.md", "after\n");
  std::ofstream(fixture.state / record.candidate_files.at("README.md"))
      << "tampered\n";
  REQUIRE_THROWS_AS(manager.apply(record), statewright::common::Error);
  manager.discard(record);
  REQUIRE(record.status == "DISCARDED");
  REQUIRE(statewright::core::read_text(fixture.root / "README.md") == "before\n");
}
