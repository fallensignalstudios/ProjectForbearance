// The host boundary: nothing in api.hpp may throw, whatever it is handed.
// Unreal disables exceptions in most module configurations, so a throw crossing
// this line is a crash in a packaged build rather than a handled error.
#include <algorithm>
#include <cstddef>
#include <string>

#include "expansion/api.hpp"
#include "expansion/content_source.hpp"
#include "expansion/host_files.hpp"
#include "harness.hpp"

using namespace expansion;

namespace {

// Every shipped definition file, served from memory: the case a packaged build
// hits when it reads content out of a pak file rather than off a disk.
MemoryContentSource shipped_in_memory() {
  host::DirectoryContentSource dir(EXPANSION_CONTENT_DIR);
  return MemoryContentSource(dir.read_all(), "memory");
}

// Runs `fn` and reports whether anything escaped it.
template <typename Fn>
bool escapes(Fn&& fn) {
  try {
    fn();
    return false;
  } catch (...) {
    return true;
  }
}

}  // namespace

TEST(api_loads_content_from_memory, "the core loads a catalog with no filesystem of its own") {
  MemoryContentSource memory = shipped_in_memory();
  auto loaded = api::load_catalog(memory);
  CHECK_MSG(loaded.ok(), loaded.error().message);
  // A catalog read from memory must hash identically to the same bytes read
  // from a directory: the hash cannot depend on how a host enumerated files.
  CHECK_EQ(loaded.value()->hash(), testing::shipped_catalog().hash());
  CHECK_EQ(loaded.value()->facilities().size(), testing::shipped_catalog().facilities().size());
}

TEST(api_content_order_does_not_matter, "the catalog hash is independent of the order files arrive in") {
  auto files = shipped_in_memory().read_all();
  auto forward = api::load_catalog(MemoryContentSource(files));
  std::reverse(files.begin(), files.end());
  auto reversed = api::load_catalog(MemoryContentSource(files));
  CHECK(forward.ok());
  CHECK(reversed.ok());
  CHECK_EQ(forward.value()->hash(), reversed.value()->hash());
}

TEST(api_rejects_bad_content_without_throwing, "malformed content returns a typed error") {
  MemoryContentSource empty;
  auto none = api::load_catalog(empty);
  CHECK(!none.ok());
  CHECK_EQ(none.error().code, ErrorCode::InvalidContent);
  CHECK(!none.error().message.empty());

  MemoryContentSource garbage;
  garbage.add("resources/resources.json", "{ this is not json");
  auto bad = api::load_catalog(garbage);
  CHECK(!bad.ok());
  CHECK_EQ(bad.error().code, ErrorCode::InvalidContent);

  MemoryContentSource duplicated;
  duplicated.add("a.json", "{\"schema\":\"resources\",\"resources\":[]}");
  duplicated.add("a.json", "{\"schema\":\"resources\",\"resources\":[]}");
  auto dup = api::load_catalog(duplicated);
  CHECK(!dup.ok());
  CHECK(dup.error().message.find("twice") != std::string::npos);
}

TEST(api_never_throws, "every public operation returns an error rather than throwing") {
  MemoryContentSource memory = shipped_in_memory();
  auto loaded = api::load_catalog(memory);
  CHECK(loaded.ok());
  api::CatalogPtr catalog = loaded.take();

  // Bad arguments.
  CHECK(!escapes([&] {
    auto r = api::create_session(catalog, "no_such_scenario", "dominion", 0);
    CHECK(!r.ok());
    CHECK_EQ(r.error().code, ErrorCode::NotFound);
  }));
  CHECK(!escapes([&] {
    auto r = api::create_session(catalog, "first_dependency", "no_such_faction", 0);
    CHECK(!r.ok());
    CHECK_EQ(r.error().code, ErrorCode::NotFound);
  }));
  CHECK(!escapes([&] {
    auto r = api::create_session(nullptr, "first_dependency", "dominion", 0);
    CHECK(!r.ok());
    CHECK_EQ(r.error().code, ErrorCode::InvalidArgument);
  }));
  CHECK(!escapes([&] {
    auto r = api::decode_save(catalog, "not a save at all");
    CHECK(!r.ok());
    CHECK_EQ(r.error().code, ErrorCode::CorruptSave);
  }));
  CHECK(!escapes([&] {
    auto r = api::decode_save(catalog, "{}");
    CHECK(!r.ok());
  }));

  // The working path, and a forecast with a nonsense horizon.
  auto made = api::create_session(catalog, "first_dependency", "dominion", 0);
  CHECK_MSG(made.ok(), made.error().message);
  api::SessionPtr session = made.take();

  CHECK(!escapes([&] {
    auto r = api::forecast(*session, {}, -5);
    CHECK(r.ok());
    CHECK_EQ(r.value().days_run, 0);
  }));

  for (int i = 0; i < 10; ++i) {
    auto day = api::step_day(*session);
    CHECK_MSG(day.ok(), day.error().message);
  }
  auto hash = api::canonical_hash(*session);
  CHECK(hash.ok());
  CHECK_EQ(hash.value().size(), static_cast<std::size_t>(64));

  auto bytes = api::encode_save(*session, "test");
  CHECK(bytes.ok());
  auto reloaded = api::decode_save(catalog, bytes.value());
  CHECK_MSG(reloaded.ok(), reloaded.error().message);
  auto reloaded_hash = api::canonical_hash(*reloaded.value());
  CHECK(reloaded_hash.ok());
  CHECK_EQ(reloaded_hash.value(), hash.value());
}

TEST(api_reports_an_incompatible_save_distinctly, "a host can tell an incompatible save from a corrupt one") {
  MemoryContentSource memory = shipped_in_memory();
  api::CatalogPtr catalog = api::load_catalog(memory).take();
  auto session = api::create_session(catalog, "first_dependency", "dominion", 0).take();
  api::step_day(*session);
  std::string bytes = api::encode_save(*session, "test").value();

  // Repoint the payload at a catalog this build does not have.
  const std::string hash = catalog->hash();
  const std::size_t at = bytes.find(hash);
  CHECK(at != std::string::npos);
  bytes.replace(at, hash.size(), std::string(64, 'c'));
  auto r = api::decode_save(catalog, bytes);
  CHECK(!r.ok());
  CHECK_EQ(r.error().code, ErrorCode::IncompatibleSave);
  CHECK(std::string(r.error().id()) == "incompatible_save");
}

TEST(api_lists_what_a_menu_needs, "a host can build a scenario and faction menu without hard-coded ids") {
  const Catalog& catalog = testing::shipped_catalog();
  auto scenarios = api::scenarios(catalog);
  CHECK_EQ(scenarios.size(), static_cast<std::size_t>(1));
  CHECK_EQ(scenarios.front().id, std::string("first_dependency"));
  CHECK_EQ(scenarios.front().evaluation_day, 120);

  auto factions = api::playable_factions(catalog);
  CHECK_EQ(factions.size(), static_cast<std::size_t>(2));
  for (const auto& f : factions) {
    CHECK(f.id != std::string("neutral_calibration"));
    CHECK(!f.display_key.empty());
  }
}
