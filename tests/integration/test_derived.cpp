// Derived helpers exposed to presentation and tooling.
#include "expansion/derived.hpp"
#include "harness.hpp"

using namespace expansion;

TEST(derived_cargo_volume, "cargo volume is one unit of volume per unit of every stored resource") {
  const Catalog& catalog = testing::shipped_catalog();
  ResourceMap cargo = {{catalog.food(), 60000}, {catalog.steel(), 30000}, {catalog.machinery(), 6000}};
  CHECK_EQ(cargo_volume_of(catalog, cargo), 96000);
  CHECK_EQ(cargo_volume_of(catalog, {}), 0);
}

TEST(derived_accounts, "only sector-owned accounts count toward owned quantity") {
  CHECK(is_sector_owned_account("planet:homeworld"));
  CHECK(is_sector_owned_account("escrow:12"));
  CHECK(is_sector_owned_account("ship:3"));
  CHECK(is_sector_owned_account("expedition:4"));
  CHECK(!is_sector_owned_account("external"));
  CHECK(!is_sector_owned_account("front"));
  CHECK(!is_sector_owned_account("sink:civilian"));
}

TEST(derived_coal_floor, "the default Coal floor is three days of thermal-generator demand") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  const PlanetState& home = session->state().planet("homeworld");
  CHECK_EQ(default_outbound_floor(session->state(), catalog, home, catalog.coal()), 24000);
  CHECK_EQ(default_outbound_floor(session->state(), catalog, home, catalog.iron_ore()), 0);
}
