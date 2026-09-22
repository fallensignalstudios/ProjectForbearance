// T10-T15: freight, the expedition and world efficiency (TDD 18.2).
#include "expansion/derived.hpp"
#include "harness.hpp"

using namespace expansion;

namespace {

// Enables the route with an outbound manifest, then steps until the colony is
// founded and its Spaceport is working.
void configure_route(Session& session, const std::map<int, Milli>& outbound) {
  Command c;
  c.id = "route_outbound";
  c.kind = CommandKind::UpdateRoute;
  c.content_id = "outbound";
  c.resources = outbound;
  CHECK(session.apply_command(c).accepted);
}

void launch_and_found(Session& session) {
  Command launch;
  launch.id = "launch";
  launch.kind = CommandKind::LaunchColonization;
  CHECK(session.apply_command(launch).accepted);
  session.step_day();
  session.step_day();   // arrival and founding
  CHECK(session.state().colonisation.founded);
}

// Builds the first Spaceport at Frontier out of the delivered founding package.
void build_frontier_spaceport(Session& session) {
  Command hub_staff;
  hub_staff.id = "hub_staff";
  hub_staff.kind = CommandKind::AssignWorkers;
  hub_staff.planet_id = "frontier";
  hub_staff.facility_id = 0;
  hub_staff.to_facility_id = testing::find_facility(session, "frontier", "hub_water");
  hub_staff.count = 10;
  CHECK(session.apply_command(hub_staff).accepted);

  Command build;
  build.id = "build_port";
  build.kind = CommandKind::StartConstruction;
  build.planet_id = "frontier";
  build.content_id = "spaceport";
  CommandResult r = session.apply_command(build);
  CHECK_MSG(r.accepted, r.reason + " " + r.detail);
  const InstanceId job = session.state().facilities.back().id;

  Command crew;
  crew.id = "port_crew";
  crew.kind = CommandKind::AssignWorkers;
  crew.planet_id = "frontier";
  crew.facility_id = 0;
  crew.to_facility_id = job;
  crew.count = 40;
  CHECK(session.apply_command(crew).accepted);
  for (int i = 0; i < 4; ++i) session.step_day();
  const FacilityState* port = session.state().find_facility(job);
  CHECK_MSG(port->lifecycle == FacilityLifecycle::Active, "the first Spaceport should be finished and usable");

  Command staff;
  staff.id = "port_staff";
  staff.kind = CommandKind::AssignWorkers;
  staff.planet_id = "frontier";
  staff.facility_id = 0;
  staff.to_facility_id = job;
  staff.count = 20;
  CHECK(session.apply_command(staff).accepted);
  session.step_day();
  session.step_day();
}

// Runs one outbound package, matching the founding sequence of TDD 11.3.
void deliver_first_package(Session& session, const Catalog& catalog) {
  configure_route(session, {{catalog.food(), 60000}, {catalog.steel(), 30000}, {catalog.machinery(), 6000}});
  Command depart;
  depart.id = "deliver_depart";
  depart.kind = CommandKind::AuthoriseDeparture;
  CHECK(session.apply_command(depart).accepted);
  for (int i = 0; i < 4; ++i) session.step_day();
  CHECK_MSG(session.state().ship.cargo.empty(), "the first package should be fully unloaded");
}

}  // namespace

TEST(t13_expedition_accounting, "T13: the expedition debits exactly once and delivers exactly what survives transit") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  const ScenarioDef& sc = catalog.scenario("first_dependency");
  std::vector<Milli> before = session->state().planet("homeworld").inventory.on_hand;
  const People pop_before = session->state().planet("homeworld").population;
  const int reserve_before = reserve_workers_of(session->state(), session->state().planet("homeworld"));

  Command launch;
  launch.id = "launch";
  launch.kind = CommandKind::LaunchColonization;
  CommandResult r = session->apply_command(launch);
  CHECK_MSG(r.accepted, r.reason + " " + r.detail);

  // The exact launch sink leaves Homeworld once.
  for (const auto& [idx, qty] : sc.expedition.launch_cost) {
    CHECK_EQ(session->state().planet("homeworld").inventory.on_hand[static_cast<std::size_t>(idx)],
             before[static_cast<std::size_t>(idx)] - qty);
  }
  CHECK_EQ(session->state().planet("homeworld").population, pop_before - 100);
  CHECK_EQ(reserve_workers_of(session->state(), session->state().planet("homeworld")), reserve_before - 50);

  session->step_day();   // one transit day
  session->step_day();   // the final transit day, then founding
  const SessionState& s = session->state();
  CHECK(s.colonisation.founded);
  // Two days of transit consumption, then arrival.
  CHECK_EQ(testing::stock(*session, "frontier", "steel"), 40000);
  // The Hub's first maintenance bill is paid on the founding day, so 0.02
  // Machinery has already left the delivered ten.
  CHECK_EQ(testing::stock(*session, "frontier", "machinery"), 9980);
  CHECK_EQ(s.colonisation.consumed_transit.at(catalog.food()), 20000);
  CHECK_EQ(s.colonisation.consumed_transit.at(catalog.water()), 30000);
  CHECK_EQ(s.planet("frontier").population, 100);
  CHECK_EQ(s.planet("frontier").workers_total, 50);
  // All fifty arriving workers enter Reserve; the Hub is created once, unstaffed.
  int hubs = 0;
  for (const auto& f : s.facilities) {
    if (f.planet_id == "frontier" && f.facility_id == "colony_hub") ++hubs;
  }
  CHECK_EQ(hubs, 1);
  CHECK_EQ(s.find_facility(testing::find_facility(*session, "frontier", "hub_water"))->assigned_workers, 0);
  CHECK_EQ(reserve_workers_of(s, s.planet("frontier")), 50);
  // Consumption starts on the founding day, so the buffer matters.
  CHECK(s.planet("frontier").last_day.food_demand > 0);

  Command again;
  again.id = "launch_again";
  again.kind = CommandKind::LaunchColonization;
  CHECK(!session->apply_command(again).accepted);
}

TEST(t14_colonial_bootstrap, "T14: the delivered package funds the first port with no import-before-port loop") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  launch_and_found(*session);
  // Before a working Spaceport exists, the freighter cannot deliver.
  configure_route(*session, {{catalog.food(), 60000}});
  Command depart;
  depart.id = "early_depart";
  depart.kind = CommandKind::AuthoriseDeparture;
  CHECK(session->apply_command(depart).accepted);
  session->step_day();
  CHECK_EQ(session->state().ship.phase, ShipPhase::Docked);
  bool blocked_for_port = false;
  for (const auto& f : session->state().facts) {
    if (f.kind == "authorised_departure_blocked" && f.reason_id == "no_spaceport") blocked_for_port = true;
  }
  CHECK(blocked_for_port);

  build_frontier_spaceport(*session);
  CHECK(session->state().planet("frontier").last_day.port_handling_capacity > 0);
}

TEST(t10_t11_normal_voyage, "T10/T11: a day-10 departure arrives day 12, returns day 15, and reloads day 16") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  launch_and_found(*session);
  build_frontier_spaceport(*session);
  configure_route(*session, {{catalog.food(), 60000}, {catalog.steel(), 30000}, {catalog.machinery(), 6000}});

  // Step to the eve of day 10 and then authorise the departure.
  while (session->state().day < 9) session->step_day();
  const Milli fuel_before = testing::stock(*session, "homeworld", "fuel");
  Command depart;
  depart.id = "depart";
  depart.kind = CommandKind::AuthoriseDeparture;
  CHECK(session->apply_command(depart).accepted);
  session->step_day();   // day 10: loads and departs
  const SessionState& s = session->state();
  CHECK_EQ(s.day, 10);
  CHECK_EQ(s.ship.phase, ShipPhase::Transit);
  CHECK_EQ(s.ship.departure_day, 10);
  CHECK_EQ(s.ship.arrival_day, 12);
  CHECK_EQ(cargo_volume_of(catalog, s.ship.cargo), 96000);
  // Four Fuel reserved, two burned outbound, two carried in the tank.
  CHECK_EQ(s.ship.tank, 2000);
  const Milli fuel_made = s.planet("homeworld").last_day.produced[static_cast<std::size_t>(catalog.fuel())];
  CHECK_EQ(testing::stock(*session, "homeworld", "fuel"), fuel_before + fuel_made - 4000);
  // The round-trip Machinery charge is paid exactly once, at the home departure.
  Milli machinery_to_propulsion = 0;
  for (const auto& t : s.ledger) {
    if (t.day == 10 && t.resource == catalog.machinery() && t.cause == std::string("ship_maintenance")) {
      machinery_to_propulsion += t.quantity;
    }
  }
  CHECK_EQ(machinery_to_propulsion, 100);

  session->step_day();   // day 11
  CHECK_EQ(session->state().ship.phase, ShipPhase::Transit);
  session->step_day();   // day 12: arrival and unload
  CHECK_EQ(session->state().day, 12);
  CHECK_EQ(session->state().ship.phase, ShipPhase::Docked);
  CHECK_EQ(session->state().ship.location_planet, std::string("frontier"));
  CHECK_EQ(session->state().ship.earliest_departure_day, 13);
  CHECK_EQ(testing::stock(*session, "frontier", "steel"), 40000 + 30000 - 40000);   // the port consumed 40

  // Frontier holds no Fuel at all, and the return must still work.
  CHECK_EQ(testing::stock(*session, "frontier", "fuel"), 0);
  Command ret;
  ret.id = "return_targets";
  ret.kind = CommandKind::UpdateRoute;
  ret.content_id = "return";
  ret.resources = {{catalog.iron_ore(), 120000}};
  CHECK(session->apply_command(ret).accepted);
  Command enable;
  enable.id = "enable";
  enable.kind = CommandKind::UpdateRoute;
  enable.content_id = "enable";
  enable.flag = true;
  CHECK(session->apply_command(enable).accepted);

  session->step_day();   // day 13: the return leg departs
  CHECK_EQ(session->state().day, 13);
  CHECK_EQ(session->state().ship.phase, ShipPhase::Transit);
  CHECK_EQ(session->state().ship.arrival_day, 15);
  CHECK_EQ(session->state().ship.tank, 0);
  session->step_day();   // 14
  session->step_day();   // 15: home arrival
  CHECK_EQ(session->state().day, 15);
  CHECK_EQ(session->state().ship.location_planet, std::string("homeworld"));
  CHECK_EQ(session->state().ship.earliest_departure_day, 16);
}

TEST(t15_world_efficiency_exact, "T15: twenty mine workers on a 1.5 world produce 36 ore for four power") {
  const Catalog& catalog = *testing::fixture_catalog("bottleneck");
  auto session = testing::new_session(catalog, "mining_efficiency", "testing_profile");
  const InstanceId mine = testing::find_facility(*session, "testworld", "test_mine_iron");
  session->step_day();
  const FacilityState* mine_state = session->state().find_facility(mine);
  const FacilityExplanation& x = mine_state->last_explanation;
  CHECK_EQ(x.staffing_bp, 5000);
  CHECK_EQ(x.actual_throughput_bp, 5000);
  // 48 base at half staff is 24; the world's mining modifier scales the output
  // only, giving 36 ore, and the power draw still follows the run factor.
  CHECK_EQ(x.outputs_produced.at(catalog.resource_index("iron_ore")), 36000);
  CHECK_EQ(x.power_granted, 4000);
}

TEST(t15_world_efficiency_in_scenario, "T15: Frontier mining scales output only, never the inputs or power") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  launch_and_found(*session);
  build_frontier_spaceport(*session);
  deliver_first_package(*session, catalog);

  Command build;
  build.id = "build_mine";
  build.kind = CommandKind::StartConstruction;
  build.planet_id = "frontier";
  build.content_id = "extraction_site";
  build.recipe_id = "extraction_iron";
  CommandResult r = session->apply_command(build);
  CHECK_MSG(r.accepted, r.reason + " " + r.detail);
  const InstanceId mine = session->state().facilities.back().id;

  // Workers come from the colony's own Reserve: the game cannot invent a crew.
  Command crew;
  crew.id = "mine_crew";
  crew.kind = CommandKind::AssignWorkers;
  crew.planet_id = "frontier";
  crew.facility_id = 0;
  crew.to_facility_id = mine;
  crew.count = 20;
  CommandResult crew_result = session->apply_command(crew);
  CHECK_MSG(crew_result.accepted, crew_result.reason + " " + crew_result.detail);
  for (int i = 0; i < 8; ++i) session->step_day();
  CHECK_EQ(session->state().find_facility(mine)->lifecycle, FacilityLifecycle::Active);

  Command staff;
  staff.id = "mine_staff";
  staff.kind = CommandKind::AssignWorkers;
  staff.planet_id = "frontier";
  staff.facility_id = 0;
  staff.to_facility_id = mine;
  staff.count = 20;
  CHECK(session->apply_command(staff).accepted);
  session->step_day();
  session->step_day();

  const FacilityState* mine_state = session->state().find_facility(mine);
  const FacilityExplanation& x = mine_state->last_explanation;
  CHECK_EQ(x.staffing_bp, 5000);
  // The output carries the 1.5 modifier; the power draw follows the run factor
  // alone, so a world bonus never becomes a hidden efficiency bonus.
  const Milli base_output = mul_div_floor(48000, x.actual_throughput_bp, kBpOne);
  CHECK_EQ(x.outputs_produced.at(catalog.iron_ore()), mul_div_floor(base_output, 15000, kBpOne));
  CHECK_EQ(x.power_granted, mul_div_ceil(8000, x.actual_throughput_bp, kBpOne));
  CHECK(x.inputs_consumed.empty());
}

TEST(t12_partial_unload_save, "T12: a save taken mid-unload finishes with identical totals") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  launch_and_found(*session);
  build_frontier_spaceport(*session);
  // One Spaceport worker gives a small handling budget, so the unload spans days.
  Command thin_crew;
  thin_crew.id = "thin_crew";
  thin_crew.kind = CommandKind::AssignWorkers;
  thin_crew.planet_id = "frontier";
  thin_crew.facility_id = testing::find_facility(*session, "frontier", "spaceport_handling");
  thin_crew.to_facility_id = 0;
  thin_crew.count = 10;
  CHECK(session->apply_command(thin_crew).accepted);
  session->step_day();
  session->step_day();

  configure_route(*session, {{catalog.food(), 60000}, {catalog.steel(), 30000}});
  Command depart;
  depart.id = "depart";
  depart.kind = CommandKind::AuthoriseDeparture;
  CHECK(session->apply_command(depart).accepted);
  session->step_day();
  const Milli shipped = cargo_volume_of(catalog, session->state().ship.cargo);
  CHECK(shipped > 0);
  session->step_day();
  session->step_day();   // arrival; only part of the cargo can be handled today
  CHECK_MSG(!session->state().ship.cargo.empty(), "the thinned dock should not finish the unload in one day");
  CHECK_EQ(session->state().ship.phase, ShipPhase::Unloading);

  // Save mid-unload, reload, and finish. The destination must not gain cargo.
  const std::string hash_before = session->canonical_hash();
  SessionState copy = session->state();
  auto reloaded = Session::from_state(catalog, copy);
  CHECK_EQ(reloaded->canonical_hash(), hash_before);
  for (int i = 0; i < 6; ++i) {
    session->step_day();
    reloaded->step_day();
  }
  CHECK_EQ(session->canonical_hash(), reloaded->canonical_hash());
  CHECK_EQ(testing::stock(*session, "frontier", "steel"), testing::stock(*reloaded, "frontier", "steel"));
  CHECK(session->state().ship.cargo.empty());
}
