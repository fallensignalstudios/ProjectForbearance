// Inventory laws and competing reservations (TDD 6.1, test T02).
#include "harness.hpp"

using namespace expansion;

namespace {
InventoryState make_inventory(int resources, Milli capacity) {
  InventoryState inv;
  inv.resize(resources, capacity);
  return inv;
}
}  // namespace

TEST(t02_inventory_laws, "T02: available = on hand minus valid reservations, and never negative") {
  InventoryState inv = make_inventory(3, 1000);
  inv.on_hand[0] = 500;
  CHECK_EQ(inv.available(0), 500);
  CHECK_EQ(inv.free_space(0), 500);

  StockReservation r;
  r.id = 1;
  r.resource = 0;
  r.quantity = 300;
  r.owner_kind = "construction";
  inv.reservations.push_back(r);
  // Reserved stock remains part of on-hand.
  CHECK_EQ(inv.on_hand[0], 500);
  CHECK_EQ(inv.reserved(0), 300);
  CHECK_EQ(inv.available(0), 200);

  IncomingClaim c;
  c.id = 2;
  c.resource = 0;
  c.quantity = 400;
  inv.incoming_claims.push_back(c);
  // Incoming-space claims are separate from on-hand reservations.
  CHECK_EQ(inv.claimed_space(0), 400);
  CHECK_EQ(inv.free_space(0), 100);
  CHECK_EQ(inv.available(0), 200);
}

TEST(t02_over_reservation, "T02: a reservation beyond on-hand is detected, never clamped away") {
  InventoryState inv = make_inventory(1, 1000);
  inv.on_hand[0] = 100;
  StockReservation r;
  r.id = 1;
  r.resource = 0;
  r.quantity = 200;
  inv.reservations.push_back(r);
  CHECK_THROWS(inv.available(0));
}

TEST(t02_competing_reservations, "T02: a second over-reserving command fails atomically") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  const Milli steel_before = testing::stock(*session, "homeworld", "steel");

  // The Spaceport costs 40 Steel; Homeworld holds 180. Four fit; the fifth does
  // not, and must leave nothing behind.
  int accepted = 0;
  for (int i = 0; i < 5; ++i) {
    Command c;
    c.id = "build" + std::to_string(i);
    c.kind = CommandKind::StartConstruction;
    c.planet_id = "homeworld";
    c.content_id = "spaceport";
    CommandResult r = session->apply_command(c);
    if (r.accepted) {
      ++accepted;
    } else {
      CHECK_EQ(r.reason, std::string("insufficient_stock"));
      CHECK(!r.detail.empty());
    }
  }
  CHECK_EQ(accepted, 4);
  const PlanetState& home = session->state().planet("homeworld");
  CHECK(home.inventory.available(catalog.steel()) >= 0);
  CHECK_EQ(testing::stock(*session, "homeworld", "steel"), steel_before - 4 * 40000);
}

TEST(command_idempotency, "a duplicate command id returns the recorded result without applying again") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  Command c;
  c.id = "only_once";
  c.kind = CommandKind::StartConstruction;
  c.planet_id = "homeworld";
  c.content_id = "public_clinic";
  CommandResult first = session->apply_command(c);
  CHECK(first.accepted);
  CHECK(!first.replayed);
  const Milli steel_after_first = testing::stock(*session, "homeworld", "steel");
  const Revision revision_after_first = session->state().revision;

  CommandResult second = session->apply_command(c);
  CHECK(second.replayed);
  CHECK_EQ(second.revision, first.revision);
  CHECK_EQ(testing::stock(*session, "homeworld", "steel"), steel_after_first);
  CHECK_EQ(session->state().revision, revision_after_first);
}

TEST(command_revision_check, "a stale expected revision is rejected without touching state") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  const std::string before = session->canonical_hash();
  Command c;
  c.id = "stale";
  c.kind = CommandKind::StartConstruction;
  c.planet_id = "homeworld";
  c.content_id = "public_clinic";
  CommandResult r = session->apply_command(c, Revision{99});
  CHECK(!r.accepted);
  CHECK_EQ(r.reason, std::string("revision_mismatch"));
  CHECK_EQ(session->canonical_hash(), before);
}
