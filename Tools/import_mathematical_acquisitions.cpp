#include "statewright/contracts/hash.hpp"
#include "statewright/egcf/internet_feed.hpp"
#include "statewright/egcf/internet_metrics.hpp"
#include "statewright/egcf/internet_source_coordinator.hpp"
#include "statewright/sources/watchlist.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

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
void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}
} // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 6,
            "usage: import_mathematical_acquisitions SOURCE_WORKSPACE "
            "TARGET_WORKSPACE RESOURCES PILOT_EVIDENCE NEW_OUTPUT");
    const std::filesystem::path source_root = argv[1], target_root = argv[2],
                                resources = argv[3], evidence = argv[4],
                                output = argv[5];
    require(std::filesystem::weakly_canonical(source_root) !=
                std::filesystem::weakly_canonical(target_root),
            "source and target must differ");
    require(!std::filesystem::exists(output), "output directory must be new");
    static_cast<void>(egcf::verify_resource_manifest(resources));
    const auto manifest = read_json(evidence / "manifest.json");
    const auto report = read_json(evidence / "preflight.json");
    const auto registry =
        read_json(resources / "watchlists/internet/source-groups-v1.json");
    sources::validate_watchlist_manifest(manifest, registry);
    require(manifest.at("watches").size() == 20U,
            "expected the 20 mathematical acquisitions");
    require(report.at("manifest_sha256") == contracts::sha256_json(manifest) &&
                report.at("source_registry_sha256") ==
                    contracts::sha256_json(registry),
            "pilot preflight does not bind the current manifest and registry");
    auto report_material = report;
    report_material.erase("report_signature");
    report_material.erase("report_id");
    require(report.at("report_signature") ==
                contracts::sha256_json(report_material),
            "invalid preflight signature");
    const auto base_policy = sources::source_policy_from_json(read_json(
        resources / "policies/internet/default-source-policy-v1.json"));
    std::map<std::string, Json> entries;
    for (const auto &entry : manifest.at("watches")) {
      require(entry.at("source_group") == "fungrim" ||
                  entry.at("source_group") == "boost-math",
              "only reviewed mathematical lanes may be imported");
      const auto policy = sources::watchlist_source_policy(entry, base_policy);
      const auto found =
          std::ranges::find_if(report.at("results"), [&](const Json &item) {
            return item.at("entry_name") == entry.at("name");
          });
      require(found != report.at("results").end() &&
                  sources::watchlist_preflight_eligible(entry, *found, policy),
              "source lacks matching eligible preflight evidence");
      entries.emplace(entry.at("canonical_url").get<std::string>(), entry);
    }

    egcf::EgcfStore source(source_root, resources);
    egcf::InternetImprovementStore source_internet(source);
    source_internet.verify_integrity();
    const auto source_head = source.event_head();
    require(read_json(evidence / "integrity.json").at("event_head") ==
                source_head,
            "source pilot changed since its recorded integrity check");
    const auto snapshots = source.list("internet-source-snapshot");
    const auto receipts = source.list("internet-fetch-receipt");
    require(snapshots.size() == 20U && receipts.size() == 20U,
            "pilot must contain exactly 20 snapshots and successful fetches");
    require(source_internet.active_watch_ids().size() == 20U,
            "pilot has unexpected watch lineage");
    for (const auto &id : source_internet.active_watch_ids())
      require(!source.get(id).payload.at("enabled").get<bool>(),
              "pilot must be stopped and disabled");

    std::set<std::string> digests, urls, watch_ids;
    for (const auto &snapshot : snapshots) {
      const auto url = snapshot.payload.at("canonical_url").get<std::string>();
      require(entries.contains(url) && urls.insert(url).second,
              "unexpected or duplicate snapshot URL");
      const auto &entry = entries.at(url);
      const auto body = source_internet.snapshot_bytes(snapshot.object_id);
      require(snapshot.payload.at("final_url") == url &&
                  snapshot.payload.at("source_group") ==
                      entry.at("source_group") &&
                  contracts::sha256_bytes(body) == entry.at("source_review")
                                                       .at("body_sha256")
                                                       .get<std::string>(),
              "snapshot does not match its file-level review");
      digests.insert(contracts::sha256_bytes(body));
    }
    const auto registrations = source.list("internet-watch-registration");
    for (const auto &watch : source.list("internet-watch")) {
      const auto url = watch.payload.at("canonical_url").get<std::string>();
      require(entries.contains(url), "unexpected source watch");
      watch_ids.insert(watch.object_id);
      const auto found =
          std::ranges::find_if(registrations, [&](const auto &r) {
            return r.payload.at("watch_id") == watch.object_id;
          });
      require(found != registrations.end() &&
                  found->payload.at("source_review") ==
                      entries.at(url).at("source_review") &&
                  found->payload.at("preflight_report_sha256") ==
                      contracts::sha256_json(report) &&
                  found->payload.at("license_status") == "verified",
              "watch is missing its exact reviewed provenance");
    }
    for (const auto &receipt : receipts) {
      const auto fetch =
          sources::internet_fetch_receipt_from_json(receipt.payload);
      require(fetch.successful() && fetch.http_status == 200 &&
                  urls.contains(fetch.requested_url),
              "unexpected source fetch receipt");
    }

    // Only acquisition evidence is transferred. Pilot novelty, candidates and
    // brain-feed conclusions are not valid in the target knowledge context.
    const std::set<std::string> acquisition_types = {
        "artifact",
        "internet-source-policy",
        "internet-watch",
        "internet-watch-registration",
        "internet-fetch-job",
        "internet-fetch-lease",
        "internet-fetch-receipt",
        "internet-source-snapshot",
        "internet-source-assessment-input",
        "supersedence"};
    std::vector<egcf::EgcfRecord> records;
    std::map<std::string, std::vector<std::byte>> artifacts;
    for (const auto &record : source.list()) {
      if (!acquisition_types.contains(record.object_type))
        continue;
      if (record.object_type == "artifact") {
        const auto digest = record.payload.at("sha256").get<std::string>();
        require(digests.contains(digest),
                "unexpected artifact outside acquisition scope");
        artifacts.emplace(
            digest, source.artifacts().get("artifact-bytes:sha256:" + digest));
      }
      if (record.object_type == "supersedence")
        require(watch_ids.contains(
                    record.payload.at("old_id").get<std::string>()) &&
                    watch_ids.contains(
                        record.payload.at("new_id").get<std::string>()),
                "only watch supersedence may be imported");
      records.push_back(
          {.object_type = record.object_type, .payload = record.payload});
    }
    // Persist disabling successors before their historical enabled
    // predecessors; no imported watch is intentionally made pollable, even on a
    // replay.
    std::stable_sort(records.begin(), records.end(),
                     [](const auto &a, const auto &b) {
                       const auto disabled = [](const auto &r) {
                         return r.object_type == "internet-watch" &&
                                !r.payload.at("enabled").template get<bool>();
                       };
                       return disabled(a) && !disabled(b);
                     });

    // Nonblocking exclusive lock: a live earlier batch is never interrupted.
    egcf::EgcfStore target(target_root, resources);
    egcf::InternetImprovementStore internet(target);
    internet.verify_integrity();
    const auto baseline = egcf::internet_improvement_metrics(target);
    const auto canonical_before = target.list("algorithm-definition").size();
    for (const auto &id : internet.active_watch_ids()) {
      const auto watch = target.get(id);
      if (entries.contains(
              watch.payload.at("canonical_url").get<std::string>()))
        require(watch_ids.contains(id) &&
                    !watch.payload.at("enabled").get<bool>(),
                "target already has a different or enabled watch for an "
                "imported URL");
    }
    require(std::filesystem::create_directory(output),
            "cannot create output directory");
    write_json(output / "baseline-metrics.json", baseline);
    write_json(output / "provenance.json",
               {{"source_workspace", source_root.string()},
                {"source_event_head", source_head},
                {"manifest_sha256", contracts::sha256_json(manifest)},
                {"network_requests", 0},
                {"imported_historical_costs", true}});
    egcf::ArtifactStore artifact_sink(target.artifacts().root());
    for (const auto &[digest, body] : artifacts)
      require(artifact_sink.put(body).digest == digest,
              "copied artifact digest mismatch");
    const auto imported =
        target.register_records(records, "mathematical_acquisition_imported");
    write_json(output / "imported-records.json", imported);

    egcf::InternetSourceCoordinator coordinator(target);
    egcf::InternetFeedCoordinator feed(target);
    Json results = Json::array();
    for (const auto &receipt : receipts) {
      const auto fetch =
          sources::internet_fetch_receipt_from_json(receipt.payload);
      const auto job = target.get(fetch.job_id);
      const auto watch =
          target.get(job.payload.at("watch_id").get<std::string>());
      const auto &entry = entries.at(fetch.requested_url);
      const auto assessed = coordinator.assess(
          fetch.snapshot_id, receipt.object_id,
          watch.payload.at("source_policy_id").get<std::string>(),
          fetch.robots_allowed,
          entry.at("license").at("classification").get<std::string>());
      require(assessed.assessment.admissible(),
              "imported acquisition failed target source assessment");
      const auto extracted = coordinator.extract(fetch.snapshot_id);
      require(extracted.extraction.fragments.size() == 1 &&
                  !extracted.extraction.receipt.truncated,
              "mathematical context extraction incomplete");
      const auto fed =
          feed.process(assessed.assessment, extracted.extraction,
                       entry.at("source_group").get<std::string>(), true);
      require(fed.candidates.size() == 1 &&
                  fed.candidates.front().status == "QUARANTINED",
              "mathematical acquisition must remain quarantined");
      results.push_back(
          {{"snapshot_id", fetch.snapshot_id},
           {"url", fetch.requested_url},
           {"brain_feed_batch_id", fed.brain_feed_batch.object_id()},
           {"candidate_id", fed.candidates.front().object_id()},
           {"status", "QUARANTINED"}});
      write_json(output / "feed-results.json", results);
      std::cerr << "Fed " << results.size()
                << "/20 acquired mathematical sources\n";
    }
    internet.verify_integrity();
    const auto final_metrics = egcf::internet_improvement_metrics(target);
    require(final_metrics.at("watches").at("enabled") ==
                baseline.at("watches").at("enabled"),
            "import changed enabled watch count");
    require(target.list("algorithm-definition").size() == canonical_before,
            "import changed canonical algorithms");
    write_json(output / "final-metrics.json", final_metrics);
    write_json(output / "integrity.json",
               {{"ok", true}, {"event_head", target.event_head()}});
    std::cout << Json{{"fed", results.size()},
                      {"network_requests", 0},
                      {"canonical_admissions", 0},
                      {"integrity", "PASS"},
                      {"evidence", output.string()}}
                     .dump()
              << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
