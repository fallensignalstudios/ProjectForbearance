// Derived quantities that presentation and tooling need without reaching into
// the simulation internals. Everything here is recomputed from committed state.
#pragma once

#include "expansion/catalog.hpp"
#include "expansion/state.hpp"

namespace expansion {

// Total cargo volume of a manifest, in milli cargo-volume units.
Milli cargo_volume_of(const Catalog& catalog, const ResourceMap& cargo);

// The default outbound reserve floor for a resource: three days of that world's
// civilian Food demand, or three days of its thermal-generator Coal demand
// (TDD 6.3). Zero for every other resource.
Milli default_outbound_floor(const SessionState& state, const Catalog& catalog, const PlanetState& planet,
                             int resource);

// True for ledger accounts inside sector ownership: planet stocks, construction
// escrow, ship cargo and tanks, and expedition cargo (TDD 6.2).
bool is_sector_owned_account(const std::string& account);

// Usable housing capacity on a world.
People housing_capacity_of(const SessionState& state, const Catalog& catalog, const std::string& planet_id);

// Workers currently in Reserve on a world.
int reserve_workers_of(const SessionState& state, const PlanetState& planet);

}  // namespace expansion
