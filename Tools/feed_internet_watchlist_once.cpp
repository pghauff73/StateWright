#include "statewright/contracts/hash.hpp"
#include "statewright/contracts/typed_id.hpp"
#include "statewright/egcf/internet_feed.hpp"
#include "statewright/egcf/internet_metrics.hpp"
#include "statewright/egcf/internet_source_coordinator.hpp"
#include "statewright/egcf/registry.hpp"
#include "statewright/sources/watchlist.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
using namespace statewright;
using Json = contracts::Json;
Json read_json(const std::filesystem::path &path) {
  std::ifstream stream(path);
  if (!stream)
    throw std::runtime_error("cannot read " + path.string());
  Json value;
  stream >> value;
  return value;
}
void write_json(const std::filesystem::path &path, const Json &value) {
  std::ofstream stream(path);
  stream << value.dump(2) << '\n';
  if (!stream)
    throw std::runtime_error("cannot write " + path.string());
}
std::string timestamp(int offset = 0) {
  const auto time = std::chrono::system_clock::to_time_t(
      std::chrono::system_clock::now() + std::chrono::seconds(offset));
  const auto *parts = std::gmtime(&time);
  if (!parts)
    throw std::runtime_error("cannot read clock");
  std::ostringstream stream;
  stream << std::put_time(parts, "%Y-%m-%dT%H:%M:%SZ");
  return stream.str();
}
struct OwnedWatch {
  sources::InternetWatch watch;
  Json registration;
  std::string registration_id;
  bool disabled = false;
};
struct WatchCleanup {
  egcf::EgcfStore &store;
  egcf::InternetImprovementStore &internet;
  std::vector<OwnedWatch> watches;
  Json receipts = Json::array();
  bool disable_all() noexcept {
    bool success = true;
    for (auto &owned : watches) {
      if (!owned.watch.enabled || owned.disabled)
        continue;
      try {
        auto successor = owned.watch;
        successor.supersedes_watch_id = owned.watch.object_id();
        successor.enabled = false;
        ++successor.schedule_generation;
        successor = sources::canonical_watch(successor);
        const auto id = internet.register_watch(successor);
        std::string registration_id;
        if (store.objects().contains(owned.registration_id)) {
          const auto carried = sources::supersede_watchlist_registration(
              owned.registration, owned.watch, successor,
              owned.registration_id);
          registration_id = store.register_record(
              {.object_type = "internet-watch-registration",
               .payload = carried},
              "internet_watch_registration_registered");
        }
        receipts.push_back(
            {{"watch_id", id}, {"registration_id", registration_id}});
        owned.disabled = true;
      } catch (const std::exception &error) {
        success = false;
        std::cerr << "watch cleanup failed: " << error.what() << '\n';
      }
    }
    return success;
  }
  ~WatchCleanup() { static_cast<void>(disable_all()); }
};
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc != 5)
      throw std::runtime_error("usage: feed_watchlist_once WORKSPACE RESOURCES "
                               "MANIFEST NEW_EVIDENCE_DIRECTORY");
    const std::filesystem::path workspace = argv[1], resources = argv[2],
                                output = argv[4];
    static_cast<void>(egcf::verify_resource_manifest(resources));
    const auto manifest = read_json(argv[3]);
    const auto registry =
        read_json(resources / "watchlists/internet/source-groups-v1.json");
    sources::validate_watchlist_manifest(manifest, registry);
    if (manifest.at("watches").empty() ||
        manifest.at("watches").size() > 100U ||
        manifest.at("source_policy_ref") !=
            "resources/policies/internet/default-source-policy-v1.json")
      throw std::runtime_error(
          "use 1-100 curated entries and the default source policy");
    const auto base = sources::source_policy_from_json(read_json(
        resources / "policies/internet/default-source-policy-v1.json"));
    if (!std::filesystem::create_directory(output))
      throw std::runtime_error("evidence directory must be new");
    write_json(output / "manifest.json", manifest);

    // License blockers are resolved before network access; do not fetch data
    // merely to rediscover a known policy blocker.
    sources::CurlHttpFetchProvider provider;
    Json report = {{"schema_version", 1},
                   {"checked_at", timestamp()},
                   {"manifest_sha256", contracts::sha256_json(manifest)},
                   {"source_registry_sha256", contracts::sha256_json(registry)},
                   {"watchlist_version", manifest.at("watchlist_version")},
                   {"results", Json::array()}};
    for (const auto &entry : manifest.at("watches")) {
      std::cerr << "Preflight " << entry.at("name").get<std::string>() << '\n';
      const auto policy = sources::watchlist_source_policy(entry, base);
      if (entry.at("license").at("status") != "verified" ||
          entry.at("license").at("evidence_urls").empty()) {
        report["results"].push_back(
            {{"entry_name", entry.at("name")},
             {"canonical_url", entry.at("canonical_url")},
             {"entry_sha256", contracts::sha256_json(entry)},
             {"source_policy_id", policy.object_id()},
             {"eligible", false},
             {"status", "PREFLIGHT_SKIPPED_LICENSE_REVIEW"},
             {"blocking_reasons", {"license-not-verified"}}});
      } else {
        auto single = manifest;
        single["watches"] = Json::array({entry});
        const auto checked = sources::preflight_watchlist_manifest(
            single, registry, base, provider, timestamp());
        report["results"].push_back(checked.at("results").front());
      }
    }
    report["report_signature"] = contracts::sha256_json(report);
    report["report_id"] =
        contracts::typed_id("internet-watchlist-preflight", report);
    write_json(output / "preflight.json", report);

    egcf::EgcfStore store(workspace, resources);
    egcf::InternetImprovementStore internet(store);
    write_json(output / "baseline-metrics.json",
               egcf::internet_improvement_metrics(store));
    WatchCleanup cleanup{store, internet, {}, Json::array()};
    // Refuse ownership of pre-existing watches. Cleanup must never disable
    // unrelated user work, even if a manifest happens to address the same URL.
    for (std::size_t i = 0; i < manifest.at("watches").size(); ++i) {
      const auto &entry = manifest.at("watches").at(i);
      const auto policy = sources::watchlist_source_policy(entry, base);
      const bool eligible = sources::watchlist_preflight_eligible(
          entry, report.at("results").at(i), policy);
      const auto watch =
          sources::watchlist_watch(entry, policy.object_id(), eligible, true);
      if (store.objects().contains(watch.object_id()))
        throw std::runtime_error("refusing to alter pre-existing watch: " +
                                 watch.canonical_url);
    }
    Json registrations = Json::array();
    for (std::size_t i = 0; i < manifest.at("watches").size(); ++i) {
      const auto &entry = manifest.at("watches").at(i);
      const auto policy = sources::watchlist_source_policy(entry, base);
      const bool eligible = sources::watchlist_preflight_eligible(
          entry, report.at("results").at(i), policy);
      const auto policy_id = internet.register_source_policy(policy);
      const auto watch =
          sources::watchlist_watch(entry, policy_id, eligible, true);
      std::string independence = watch.source_group;
      for (const auto &group : registry.at("source_groups"))
        if (group.at("source_group") == watch.source_group)
          independence =
              group.at("evidence_independence_group").get<std::string>();
      const auto status = !eligible       ? "QUARANTINED"
                          : watch.enabled ? "REGISTERED_ENABLED"
                                          : "REGISTERED_DISABLED";
      auto registration = sources::make_watchlist_registration(
          manifest, entry, watch.object_id(), policy_id,
          contracts::sha256_json(report), status, independence);
      const egcf::EgcfRecord record{.object_type =
                                        "internet-watch-registration",
                                    .payload = registration};
      static_cast<void>(internet.register_watch(watch));
      cleanup.watches.push_back(
          {watch, registration, record.object_id(), false});
      const auto registration_id = store.register_record(
          record, "internet_watch_registration_registered");
      registrations.push_back({{"entry_name", entry.at("name")},
                               {"source_group", watch.source_group},
                               {"watch_id", watch.object_id()},
                               {"registration_id", registration_id},
                               {"status", status}});
    }
    write_json(output / "registrations.json", registrations);
    egcf::InternetSourceCoordinator source(store);
    egcf::InternetFeedCoordinator feed(store);
    Json results = Json::array();
    for (const auto &owned : cleanup.watches) {
      if (!owned.watch.enabled)
        continue;
      const auto &watch = owned.watch;
      Json result = {{"watch_id", watch.object_id()},
                     {"url", watch.canonical_url},
                     {"source_group", watch.source_group}};
      std::cerr << "Feed " << watch.canonical_url << '\n';
      try {
        const auto now = timestamp();
        const auto job =
            sources::make_fetch_job(watch, now, now, timestamp(120), 0, 0);
        static_cast<void>(internet.register_fetch_job(job));
        const auto lease = sources::acquire_fetch_lease(
            job.object_id(), "watchlist-feed-once", now, timestamp(120));
        static_cast<void>(internet.register_fetch_lease(lease));
        const auto fetched = source.execute_fetch(
            job.object_id(), lease.object_id(), timestamp(), provider);
        result["fetch"] = egcf::to_json(fetched);
        if (fetched.source_assessment_input_id.empty())
          throw std::runtime_error(
              "source lacks automatic license/robots assessment evidence");
        const auto input = egcf::internet_source_assessment_input_from_json(
            store.get(fetched.source_assessment_input_id).payload);
        const auto assessed =
            source.assess(fetched.snapshot_id, fetched.fetch_receipt_id,
                          watch.source_policy_id, input.robots_allowed,
                          input.license_classification);
        result["assessment"] = egcf::to_json(assessed);
        if (!assessed.assessment.admissible()) {
          result["status"] = "SOURCE_BLOCKED";
        } else {
          sources::InternetExtractionLimits limits;
          limits.maximum_fragments = 256U;
          limits.maximum_fragment_bytes = 64U * 1024U;
          const auto extracted = source.extract(fetched.snapshot_id, limits);
          result["extraction_receipt_id"] = extracted.extraction_receipt_id;
          result["fragments"] = extracted.extraction.fragments.size();
          result["truncated"] = extracted.extraction.receipt.truncated;
          const auto fed =
              feed.process(assessed.assessment, extracted.extraction,
                           watch.source_group, true);
          result["brain_feed_batch_id"] = fed.brain_feed_batch.object_id();
          result["candidates"] = fed.candidates.size();
          result["retrievals"] = fed.retrieval_receipts.size();
          result["status"] = "FED";
        }
      } catch (const std::exception &error) {
        result["status"] = "FAILED";
        result["error"] = error.what();
      }
      results.push_back(std::move(result));
      write_json(output / "feed-results.json", results);
      std::cerr << results.back().at("status").get<std::string>() << '\n';
    }
    if (!cleanup.disable_all())
      throw std::runtime_error("one or more new watches could not be disabled");
    write_json(output / "disabled.json", cleanup.receipts);
    internet.verify_integrity();
    write_json(output / "final-metrics.json",
               egcf::internet_improvement_metrics(store));
    write_json(output / "integrity.json",
               {{"ok", true}, {"event_head", store.event_head()}});
    std::cout << Json{{"registered", registrations.size()},
                      {"processed", results.size()},
                      {"evidence_directory", output.string()},
                      {"integrity", "PASS"}}
                     .dump()
              << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
