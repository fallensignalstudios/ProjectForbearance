#include "expansion/host_files.hpp"
// Catalog structural and semantic validation (TDD 4.3).
#include <filesystem>
#include <fstream>

#include "harness.hpp"

using namespace expansion;

namespace {

// Writes a copy of the shipped content with one file patched, so a specific
// violation can be provoked without hand-writing a whole catalog.
std::string patched_content(const std::string& relative_path, const std::string& find, const std::string& replace) {
  namespace fs = std::filesystem;
  static int counter = 0;
  const std::string dir = fs::temp_directory_path().string() + "/expansion_catalog_test_" + std::to_string(counter++);
  fs::remove_all(dir);
  fs::copy(EXPANSION_CONTENT_DIR, dir, fs::copy_options::recursive);
  const std::string path = dir + "/" + relative_path;
  std::string text = host::read_file(path);
  const std::size_t at = text.find(find);
  if (at == std::string::npos) throw SimError("test setup: pattern not found in " + relative_path);
  text.replace(at, find.size(), replace);
  std::ofstream(path, std::ios::binary) << text;
  return dir;
}

}  // namespace

TEST(catalog_loads, "the shipped catalog validates and hashes deterministically") {
  const Catalog& a = testing::shipped_catalog();
  Catalog b = host::load_catalog_from_directory(EXPANSION_CONTENT_DIR);
  CHECK_EQ(a.hash(), b.hash());
  CHECK_EQ(a.hash().size(), static_cast<std::size_t>(64));
  CHECK_EQ(a.resource_count(), 7);
  CHECK_EQ(a.facilities().size(), static_cast<std::size_t>(11));
  CHECK_EQ(a.playable_factions().size(), static_cast<std::size_t>(2));
  CHECK(a.fixture_faction() != nullptr);
  CHECK(a.find_facility("extraction_site")->recipes.size() == 2);
}

TEST(catalog_rejects_unknown_reference, "a reference to a missing id fails to load") {
  const std::string dir = patched_content("scenarios/first_dependency.json", "\"extraction_site\"",
                                          "\"extraction_site_typo\"");
  CHECK_THROWS(host::load_catalog_from_directory(dir));
}

TEST(catalog_rejects_unknown_field, "an unexpected field fails to load rather than being ignored") {
  const std::string dir = patched_content("facilities/facilities.json", "\"id\": \"waterworks\"",
                                          "\"id\": \"waterworks\", \"maintenence\": 1");
  CHECK_THROWS(host::load_catalog_from_directory(dir));
}

TEST(catalog_rejects_bad_condition_kind, "a condition outside the whitelist fails to load") {
  const std::string dir = patched_content("events/mining_safety.json", "\"kind\": \"CompareMetric\"",
                                          "\"kind\": \"EvaluateScript\"");
  CHECK_THROWS(host::load_catalog_from_directory(dir));
}

TEST(catalog_rejects_free_expedition_cargo, "an expedition cannot deliver more than its debited cost") {
  const std::string dir = patched_content("scenarios/first_dependency.json",
                                          "\"cargo\": {\"steel\": 40000",
                                          "\"cargo\": {\"steel\": 90000");
  CHECK_THROWS(host::load_catalog_from_directory(dir));
}

TEST(catalog_content_ids, "content ids are stable lowercase ASCII identifiers") {
  CHECK(is_valid_content_id("extraction_site"));
  CHECK(is_valid_content_id("food"));
  CHECK(!is_valid_content_id("Extraction"));
  CHECK(!is_valid_content_id("extraction-site"));
  CHECK(!is_valid_content_id(""));
  CHECK(!is_valid_content_id("_leading"));
}

TEST(catalog_power_not_a_commodity, "power cannot be authored as a stored commodity") {
  const std::string dir = patched_content("resources/resources.json", "\"id\": \"fuel\"", "\"id\": \"power\"");
  CHECK_THROWS(host::load_catalog_from_directory(dir));
}
