#include "expansion/host_files.hpp"
#include "harness.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <map>
#include <memory>

namespace testing {

std::vector<TestCase>& registry() {
  static std::vector<TestCase> cases;
  return cases;
}

Registrar::Registrar(const char* id, const char* name, std::function<void()> fn) {
  registry().push_back({id, name, std::move(fn)});
}

void fail(const std::string& what, const char* file, int line) {
  std::ostringstream ss;
  ss << file << ":" << line << ": " << what;
  throw Failure(ss.str());
}

const expansion::Catalog& shipped_catalog() {
  static expansion::Catalog catalog = expansion::host::load_catalog_from_directory(EXPANSION_CONTENT_DIR);
  return catalog;
}

const expansion::Catalog* fixture_catalog(const char* name) {
  static std::map<std::string, std::unique_ptr<expansion::Catalog>> cache;
  const std::string key(name);
  auto it = cache.find(key);
  if (it == cache.end()) {
    auto catalog = std::make_unique<expansion::Catalog>(
        expansion::host::load_catalog_from_directory(std::string(EXPANSION_FIXTURE_DIR) + "/" + key));
    it = cache.emplace(key, std::move(catalog)).first;
  }
  return it->second.get();
}

std::unique_ptr<expansion::Session> new_session(const expansion::Catalog& catalog, const std::string& scenario,
                                                const std::string& faction) {
  return expansion::Session::create(catalog, scenario, faction, 0);
}

expansion::InstanceId find_facility(const expansion::Session& session, const std::string& planet,
                                    const std::string& recipe_id) {
  for (const auto& f : session.state().facilities) {
    if (f.planet_id == planet && f.recipe_id == recipe_id) return f.id;
  }
  throw expansion::SimError("test: no facility with recipe '" + recipe_id + "' on " + planet);
}

expansion::Milli stock(const expansion::Session& session, const std::string& planet, const char* resource) {
  const int idx = session.catalog().resource_index(resource);
  return session.state().planet(planet).inventory.on_hand[static_cast<std::size_t>(idx)];
}

}  // namespace testing

int main(int argc, char** argv) {
  std::string filter;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg.rfind("--filter=", 0) == 0) filter = arg.substr(9);
  }
  auto& cases = testing::registry();
  std::sort(cases.begin(), cases.end(),
            [](const testing::TestCase& a, const testing::TestCase& b) { return a.id < b.id; });

  int passed = 0;
  int failed = 0;
  int skipped = 0;
  for (const auto& c : cases) {
    if (!filter.empty() && c.id.find(filter) == std::string::npos) {
      ++skipped;
      continue;
    }
    try {
      c.fn();
      std::cout << "  pass  " << c.id << "  " << c.name << "\n";
      ++passed;
    } catch (const testing::Failure& e) {
      std::cout << "  FAIL  " << c.id << "  " << c.name << "\n        " << e.what() << "\n";
      ++failed;
    } catch (const std::exception& e) {
      std::cout << "  ERROR " << c.id << "  " << c.name << "\n        unexpected exception: " << e.what() << "\n";
      ++failed;
    }
  }
  std::cout << "\n" << passed << " passed, " << failed << " failed";
  if (skipped > 0) std::cout << ", " << skipped << " skipped by filter";
  std::cout << "\n";
  return failed == 0 ? 0 : 1;
}
