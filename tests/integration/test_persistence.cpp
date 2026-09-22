// T22-T24, T26, T27: replay determinism, save safety, forecast purity, lifecycle
// and a bounded soak.
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "expansion/replay.hpp"
#include "expansion/save_file.hpp"
#include "expansion/state_codec.hpp"
#include "harness.hpp"

using namespace expansion;

namespace {

std::string temp_path(const std::string& name) {
  namespace fs = std::filesystem;
  const std::string dir = fs::temp_directory_path().string() + "/expansion_tests";
  fs::create_directories(dir);
  return dir + "/" + name;
}

// A short scripted campaign used by several tests: it launches the colony,
// builds the first port, runs freight, and touches a policy.
CommandJournal scripted_run(const Catalog& catalog, const std::string& faction, Day days) {
  auto session = Session::create(catalog, "first_dependency", faction, 0);
  JournalRecorder recorder(*session, catalog);
  Command launch;
  launch.id = "launch";
  launch.kind = CommandKind::LaunchColonization;
  recorder.apply(launch);
  recorder.step();
  recorder.step();
  Command hub;
  hub.id = "hub";
  hub.kind = CommandKind::AssignWorkers;
  hub.planet_id = "frontier";
  hub.facility_id = 0;
  hub.to_facility_id = testing::find_facility(*session, "frontier", "hub_water");
  hub.count = 10;
  recorder.apply(hub);
  Command build;
  build.id = "build_port";
  build.kind = CommandKind::StartConstruction;
  build.planet_id = "frontier";
  build.content_id = "spaceport";
  recorder.apply(build);
  const InstanceId job = session->state().facilities.back().id;
  Command crew;
  crew.id = "crew";
  crew.kind = CommandKind::AssignWorkers;
  crew.planet_id = "frontier";
  crew.facility_id = 0;
  crew.to_facility_id = job;
  crew.count = 40;
  recorder.apply(crew);
  Command ration;
  ration.id = "ration";
  ration.kind = CommandKind::SelectPolicy;
  ration.planet_id = "homeworld";
  ration.content_id = "rationing";
  recorder.apply(ration);
  // Keep the colony supplied so the journal covers a surviving campaign.
  Command targets;
  targets.id = "targets";
  targets.kind = CommandKind::UpdateRoute;
  targets.content_id = "outbound";
  targets.resources = {{catalog.food(), 60000}, {catalog.water(), 45000}};
  recorder.apply(targets);
  Command enable;
  enable.id = "enable";
  enable.kind = CommandKind::UpdateRoute;
  enable.content_id = "enable";
  enable.flag = true;
  recorder.apply(enable);
  while (session->state().day < days && session->state().lifecycle == SessionLifecycle::Running) recorder.step();
  return recorder.journal();
}

}  // namespace

TEST(t22_replay_determinism, "T22: the same journal hashes identically on repeated runs") {
  const Catalog& catalog = testing::shipped_catalog();
  CommandJournal journal = scripted_run(catalog, "dominion", 25);
  ReplayResult a = replay_journal(catalog, journal);
  ReplayResult b = replay_journal(catalog, journal);
  CHECK(!a.difference.differs);
  CHECK(!b.difference.differs);
  CHECK_EQ(a.final_hash, b.final_hash);
  CHECK_EQ(a.day_hashes.size(), journal.day_hashes.size());
  for (std::size_t i = 0; i < a.day_hashes.size(); ++i) CHECK_EQ(a.day_hashes[i], journal.day_hashes[i]);
}

TEST(t22_journal_round_trip, "T22: a journal survives encoding and names its catalog") {
  const Catalog& catalog = testing::shipped_catalog();
  CommandJournal journal = scripted_run(catalog, "reformation", 15);
  const std::string bytes = encode_journal(journal);
  CommandJournal back = decode_journal(bytes);
  CHECK_EQ(back.catalog_hash, catalog.hash());
  CHECK_EQ(back.entries.size(), journal.entries.size());
  CHECK_EQ(encode_journal(back), bytes);
  ReplayResult r = replay_journal(catalog, back);
  CHECK(!r.difference.differs);
}

TEST(t22_mismatch_names_the_day, "T22: a mismatch report names the first differing day, not just 'desync'") {
  const Catalog& catalog = testing::shipped_catalog();
  CommandJournal journal = scripted_run(catalog, "dominion", 10);
  CHECK(journal.day_hashes.size() >= 5);
  journal.day_hashes[3] = std::string(64, 'a');
  ReplayResult r = replay_journal(catalog, journal);
  CHECK(r.difference.differs);
  CHECK_EQ(r.difference.day, 4);
  CHECK_EQ(r.difference.expected_hash, std::string(64, 'a'));
  CHECK(!r.difference.actual_hash.empty());
}

TEST(t23_save_round_trip, "T23: a save round-trips exactly and keeps a backup slot") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency", "dominion");
  for (int i = 0; i < 12; ++i) session->step_day();
  const std::string hash = session->canonical_hash();
  const std::string bytes = encode_save(session->state(), catalog, "test");

  SaveHeader header;
  SessionState state = decode_save(bytes, catalog, &header);
  CHECK_EQ(header.magic, std::string(kSaveMagic));
  CHECK_EQ(header.catalog_hash, catalog.hash());
  CHECK_EQ(header.day, session->state().day);
  auto reloaded = Session::from_state(catalog, state);
  CHECK_EQ(reloaded->canonical_hash(), hash);
  // Continuing from the reload stays identical.
  for (int i = 0; i < 6; ++i) {
    session->step_day();
    reloaded->step_day();
  }
  CHECK_EQ(session->canonical_hash(), reloaded->canonical_hash());

  const std::string path = temp_path("slot.scexp.json");
  write_save_slot(path, bytes);
  write_save_slot(path, encode_save(session->state(), catalog, "test"));
  CHECK(std::filesystem::exists(path + ".bak"));
  SessionState from_backup = decode_save(json::read_file(path + ".bak"), catalog);
  CHECK_EQ(from_backup.day, 12);
}

TEST(t23_rejects_tampered_and_mismatched, "T23: a changed payload or catalog is rejected without mutating state") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency", "dominion");
  session->step_day();
  std::string bytes = encode_save(session->state(), catalog, "test");

  // A truncated file is rejected.
  CHECK_THROWS(decode_save(bytes.substr(0, bytes.size() / 2), catalog));
  // A changed payload fails the checksum.
  std::string tampered = bytes;
  const std::size_t at = tampered.find("\"population\":\"1000\"");
  CHECK(at != std::string::npos);
  tampered.replace(at, std::string("\"population\":\"1000\"").size(), "\"population\":\"9000\"");
  CHECK_THROWS(decode_save(tampered, catalog));
  // A save from another catalog is rejected with a clear message.
  std::string other = bytes;
  const std::size_t hash_at = other.find(catalog.hash());
  CHECK(hash_at != std::string::npos);
  other.replace(hash_at, 64, std::string(64, 'b'));
  CHECK_THROWS(decode_save(other, catalog));
  // Nothing above touched the live session.
  CHECK_EQ(session->state().planet("homeworld").population, 1000);
}

TEST(t23_rejects_untrusted_input, "T23: malformed and oversized payloads are rejected rather than trusted") {
  const Catalog& catalog = testing::shipped_catalog();
  CHECK_THROWS(decode_save("{}", catalog));
  CHECK_THROWS(decode_save("not json", catalog));
  CHECK_THROWS(decode_save(R"({"magic":"OTHER"})", catalog));
  auto session = testing::new_session(catalog, "first_dependency", "dominion");
  json::Value payload = encode_state(session->state(), catalog);
  // A negative inventory must not load.
  json::Value& planets = const_cast<json::Value&>(payload.require("planets", "t"));
  planets.mutable_array()[0].set("population", json::dec(-5));
  CHECK_THROWS(decode_state(payload, catalog));
}

TEST(t24_forecast_purity, "T24: a forecast changes neither live state nor event scheduling") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency", "dominion");
  for (int i = 0; i < 5; ++i) session->step_day();
  const std::string hash_before = session->canonical_hash();
  const Revision revision_before = session->state().revision;
  const std::size_t facts_before = session->state().facts.size();

  ForecastResult f = session->forecast({}, 30);
  CHECK_EQ(session->canonical_hash(), hash_before);
  CHECK_EQ(session->state().revision, revision_before);
  CHECK_EQ(session->state().facts.size(), facts_before);
  CHECK_EQ(f.days_run, 30);
  CHECK(f.suppressed_discretionary_events);
  CHECK(!f.assumptions.empty());

  // The clone's arithmetic matches stepping the real session the same way.
  auto control = Session::from_state(catalog, session->state());
  for (int i = 0; i < 30; ++i) control->step_day();
  CHECK_EQ(f.closing_stock.at("homeworld"), control->state().planet("homeworld").inventory.on_hand);
  // A forecast beyond the 30-day horizon is clamped rather than silently run.
  CHECK_EQ(session->forecast({}, 100).days_run, 30);
}

TEST(t26_step_batching, "T26: the same number of days produces the same result at any batch size") {
  const Catalog& catalog = testing::shipped_catalog();
  auto a = testing::new_session(catalog, "first_dependency", "dominion");
  auto b = testing::new_session(catalog, "first_dependency", "dominion");
  for (int i = 0; i < 24; ++i) a->step_day();
  for (int i = 0; i < 6; ++i) {
    for (int j = 0; j < 4; ++j) b->step_day();
  }
  CHECK_EQ(a->state().day, b->state().day);
  CHECK_EQ(a->canonical_hash(), b->canonical_hash());
}

TEST(t26_finished_scenario_is_idle, "T26: a finished scenario does not accumulate catch-up days") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency", "dominion");
  Command surrender;
  surrender.id = "surrender";
  surrender.kind = CommandKind::Surrender;
  CHECK(session->apply_command(surrender).accepted);
  const std::string hash = session->canonical_hash();
  const Day day = session->state().day;
  for (int i = 0; i < 10; ++i) session->step_day();
  CHECK_EQ(session->state().day, day);
  CHECK_EQ(session->canonical_hash(), hash);
}

TEST(t27_soak, "T27: randomised valid commands over many days preserve every invariant") {
  const Catalog& catalog = testing::shipped_catalog();
  // Section 18.2 asks for 10,000 randomised valid-command days. That belongs in a
  // nightly job, so the default here is short and EXPANSION_SOAK_DAYS raises it.
  int soak_days = 400;
  if (const char* env = std::getenv("EXPANSION_SOAK_DAYS")) {
    const int requested = std::atoi(env);
    if (requested > 0) soak_days = requested;
  }
  auto session = testing::new_session(catalog, "first_dependency", "dominion");
  // A fixed linear congruential sequence keeps the soak reproducible. This is
  // test-side randomness only: the simulation itself uses no generator.
  std::uint64_t seed = 0x2545F4914F6CDD1DULL;
  auto next = [&](int n) {
    seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<int>((seed >> 33) % static_cast<std::uint64_t>(n));
  };
  int accepted = 0;
  int rejected = 0;
  int duplicate_ids = 0;
  for (int day = 0; day < soak_days; ++day) {
    for (int k = 0; k < 3; ++k) {
      Command c;
      const int choice = next(7);
      // Reuse an id sometimes, to exercise duplicate submission.
      c.id = next(10) == 0 ? "repeat" : "soak" + std::to_string(day) + "_" + std::to_string(k);
      switch (choice) {
        case 0: {
          c.kind = CommandKind::AssignWorkers;
          c.planet_id = "homeworld";
          const auto& facilities = session->state().facilities;
          c.facility_id = facilities[static_cast<std::size_t>(next(static_cast<int>(facilities.size())))].id;
          c.to_facility_id = 0;
          c.count = next(5) + 1;
          break;
        }
        case 1:
          c.kind = CommandKind::StartConstruction;
          c.planet_id = "homeworld";
          c.content_id = catalog.facilities()[static_cast<std::size_t>(next(
                                                 static_cast<int>(catalog.facilities().size())))]
                             .id;
          break;
        case 2: {
          c.kind = CommandKind::SetProductionPriority;
          const auto& facilities = session->state().facilities;
          c.facility_id = facilities[static_cast<std::size_t>(next(static_cast<int>(facilities.size())))].id;
          c.priority_band = next(120) - 10;   // sometimes out of range on purpose
          break;
        }
        case 3:
          c.kind = CommandKind::SelectPolicy;
          c.planet_id = "homeworld";
          c.content_id = catalog.policies()[static_cast<std::size_t>(next(
                                               static_cast<int>(catalog.policies().size())))]
                             .id;
          break;
        case 4:
          c.kind = CommandKind::UpdateRoute;
          c.content_id = "outbound";
          c.resources = {{catalog.food(), next(80) * 1000}};
          break;
        case 5:
          c.kind = CommandKind::AuthoriseDeparture;
          break;
        default:
          c.kind = CommandKind::ResolveEvent;
          c.event_instance_id = static_cast<InstanceId>(next(50));
          c.content_id = "repair_immediately";
          break;
      }
      CommandResult r = session->apply_command(c);
      if (r.replayed) {
        ++duplicate_ids;
      } else if (r.accepted) {
        ++accepted;
      } else {
        ++rejected;
      }
    }
    session->step_day();   // commits, which validates every invariant
    if (session->state().lifecycle != SessionLifecycle::Running) break;
  }
  CHECK(accepted > 50);
  CHECK(rejected > 50);
  CHECK(duplicate_ids > 0);
  // The state is still loadable and still balances.
  const std::string bytes = encode_save(session->state(), catalog, "soak");
  auto reloaded = Session::from_state(catalog, decode_save(bytes, catalog));
  CHECK_EQ(reloaded->canonical_hash(), session->canonical_hash());
}

TEST(hash_matches_across_a_reload, "the day hash is identical after a save and reload") {
  const Catalog& catalog = testing::shipped_catalog();
  auto warm = testing::new_session(catalog, "first_dependency", "dominion");
  for (int i = 0; i < 40; ++i) warm->step_day();
  const std::string a = warm->canonical_hash();
  CHECK_EQ(warm->canonical_hash(), a);
  auto cold = Session::from_state(catalog, decode_save(encode_save(warm->state(), catalog, "t"), catalog));
  CHECK_EQ(cold->canonical_hash(), a);
}

TEST(archive_digests_survive_pruning, "a pruned archive entry still shapes the canonical hash") {
  // The fixture caps the news archive at five entries and the metric history at
  // three samples per world, so both prune within a few days.
  const Catalog& catalog = *testing::fixture_catalog("bottleneck");
  CHECK(catalog.news_rules().max_entries <= 5);
  auto session = testing::new_session(catalog, "half_water_half_power", "testing_profile");
  std::string previous;
  for (int day = 0; day < 30; ++day) {
    session->step_day();
    // A save taken here and reloaded carries the digests, so the hash is stable
    // across the round trip even after entries have been pruned away.
    auto reloaded = Session::from_state(catalog, decode_save(encode_save(session->state(), catalog, "t"), catalog));
    CHECK_MSG(reloaded->canonical_hash() == session->canonical_hash(),
              "hash diverged across a reload on day " + to_decimal_string(session->state().day));
    CHECK_MSG(session->canonical_hash() != previous, "the day hash must change as the day advances");
    previous = session->canonical_hash();
  }
  // Pruning happened, and the digests still carry the entries that left.
  CHECK(static_cast<int>(session->state().news.size()) <= catalog.news_rules().max_entries);
  CHECK(!session->state().archive_digests.news.empty());
  CHECK(!session->state().archive_digests.samples.empty());
}

TEST(day_hash_cost_is_not_quadratic, "hashing a committed day does not scale with the whole campaign") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency", "dominion");
  auto hash_bytes_at = [&](int days) {
    while (session->state().day < days) session->step_day();
    // The live payload is what a day hash re-serialises; the archives are folded
    // incrementally. Growth in this figure is what a quadratic hash would show.
    return json::serialize_canonical(encode_live_state(session->state(), catalog)).size();
  };
  const std::size_t early = hash_bytes_at(20);
  const std::size_t late = hash_bytes_at(100);
  // The live payload grows with facilities and open work, not with history. Five
  // times the elapsed days must not mean five times the work.
  CHECK_MSG(late < early * 2, "live payload grew from " + std::to_string(early) + " to " + std::to_string(late) +
                                  " bytes between day 20 and day 100");
}
