// T01, T03-T09: the daily economy (TDD 18.2).
#include <algorithm>
#include <cstddef>

#include "expansion/derived.hpp"
#include "harness.hpp"

using namespace expansion;

namespace {

const FacilityExplanation& explain(const Session& session, InstanceId id) {
  return session.state().find_facility(id)->last_explanation;
}

}  // namespace

TEST(t01_neutral_day, "T01: the neutral one-day economy matches Section 9.3 exactly") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  std::vector<Milli> opening = session->state().planet("homeworld").inventory.on_hand;
  session->step_day();
  const PlanetState& p = session->state().planet("homeworld");
  auto net = [&](const char* id) {
    const int i = catalog.resource_index(id);
    return p.inventory.on_hand[static_cast<std::size_t>(i)] - opening[static_cast<std::size_t>(i)];
  };
  CHECK_EQ(net("food"), 60000);
  CHECK_EQ(net("water"), 66000);
  CHECK_EQ(net("iron_ore"), 18000);
  CHECK_EQ(net("coal"), 2000);
  CHECK_EQ(net("steel"), 12000);
  CHECK_EQ(net("machinery"), 3280);
  CHECK_EQ(net("fuel"), 12000);
  CHECK_EQ(p.last_day.power.generated, 110000);
  CHECK_EQ(p.last_day.power.used, 96000);
  CHECK_EQ(p.last_day.power.unused, 14000);
  CHECK_EQ(p.last_day.housing_capacity, 1350);
  CHECK_EQ(p.last_day.clinic_capacity, 1200);
  CHECK_EQ(p.last_day.workers_assigned + p.last_day.workers_crew, 410);
  CHECK_EQ(p.last_day.workers_reserve, 90);
  // Health and fatigue hold; stability climbs toward a full target by 100 bp.
  CHECK_EQ(p.health_bp, 10000);
  CHECK_EQ(p.fatigue_bp, 0);
  CHECK_EQ(p.last_day.stability_target_bp, 10000);
  CHECK_EQ(p.stability_bp, 7100);
}

TEST(t01_conservation, "T01: every resource reconciles against explicit sources and sinks") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  for (int day = 0; day < 5; ++day) {
    std::vector<Milli> opening = session->state().planet("homeworld").inventory.on_hand;
    session->step_day();
    const SessionState& s = session->state();
    for (int r = 0; r < catalog.resource_count(); ++r) {
      Milli delta = 0;
      for (const auto& t : s.ledger) {
        if (t.day != s.day || t.resource != r) continue;
        const bool from_owned = is_sector_owned_account(t.from_account);
        const bool to_owned = is_sector_owned_account(t.to_account);
        if (to_owned && !from_owned) delta += t.quantity;
        if (from_owned && !to_owned) delta -= t.quantity;
      }
      const Milli actual = s.planet("homeworld").inventory.on_hand[static_cast<std::size_t>(r)] -
                           opening[static_cast<std::size_t>(r)];
      CHECK_MSG(delta == actual, catalog.resource(r).id + " day " + to_decimal_string(s.day) + ": ledger " +
                                     format_milli(delta) + " vs balance " + format_milli(actual));
    }
  }
}

TEST(t03_staged_production, "T03: ore mined today cannot feed today's foundry at any recipe order") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  const InstanceId foundry = testing::find_facility(*session, "homeworld", "foundry_steel");
  const InstanceId mine = testing::find_facility(*session, "homeworld", "extraction_iron");

  // Put the mine ahead of the foundry, then behind it. Neither ordering may let
  // the foundry consume the same day's output.
  auto run_with_bands = [&](int mine_band, int foundry_band) {
    auto s = testing::new_session(catalog, "first_dependency");
    const InstanceId m = testing::find_facility(*s, "homeworld", "extraction_iron");
    const InstanceId f = testing::find_facility(*s, "homeworld", "foundry_steel");
    Command a;
    a.id = "band_mine";
    a.kind = CommandKind::SetProductionPriority;
    a.facility_id = m;
    a.priority_band = mine_band;
    CHECK(s->apply_command(a).accepted);
    Command b;
    b.id = "band_foundry";
    b.kind = CommandKind::SetProductionPriority;
    b.facility_id = f;
    b.priority_band = foundry_band;
    CHECK(s->apply_command(b).accepted);
    s->step_day();
    return s->state().planet("homeworld").inventory.on_hand[
        static_cast<std::size_t>(catalog.resource_index("iron_ore"))];
  };
  const Milli mine_first = run_with_bands(10, 40);
  const Milli foundry_first = run_with_bands(40, 10);
  CHECK_EQ(mine_first, foundry_first);
  CHECK_EQ(mine_first, 318000);   // 300 opening + 48 mined - 30 consumed from opening
  (void)foundry;
  (void)mine;
}

TEST(t04_power_brownout, "T04: residential demand is served first and inputs scale with outputs") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  const InstanceId generator = testing::find_facility(*session, "homeworld", "thermal_power");

  // Halve the generator crew: 45 thermal + 20 solar, against 20 residential and
  // 76 facility demand.
  Command c;
  c.id = "cut_generator_crew";
  c.kind = CommandKind::AssignWorkers;
  c.planet_id = "homeworld";
  c.facility_id = generator;
  c.to_facility_id = 0;
  c.count = 15;
  CHECK(session->apply_command(c).accepted);
  session->step_day();   // the transfer completes
  session->step_day();   // the brownout day

  const PlanetState& p = session->state().planet("homeworld");
  CHECK_EQ(p.last_day.power.generated, 65000);
  CHECK_EQ(p.last_day.power.residential_demand, 20000);
  CHECK_EQ(p.last_day.power.residential_served, 20000);      // protected first
  CHECK_EQ(p.last_day.power_fulfilment_bp, 10000);
  CHECK_EQ(p.last_day.power.used, p.last_day.power.generated - p.last_day.power.unused);

  // Some facility ran short of power, and its inputs and outputs fell together.
  bool saw_power_limited = false;
  for (const auto& f : session->state().facilities) {
    if (f.planet_id != "homeworld") continue;
    const FacilityExplanation& x = f.last_explanation;
    if (!x.power_limited) continue;
    saw_power_limited = true;
    CHECK(x.actual_throughput_bp < x.desired_throughput_bp);
    CHECK_EQ(x.primary_reason, std::string("power_shortfall"));
    const RecipeDef& r = catalog.facility(f.facility_id)
                             .recipes[static_cast<std::size_t>(
                                 catalog.facility(f.facility_id).recipe_index(f.recipe_id))];
    for (const auto& [idx, base] : r.outputs) {
      const Milli expected = mul_div_floor(base, x.actual_throughput_bp, kBpOne);
      const Milli made = x.outputs_produced.count(idx) != 0 ? x.outputs_produced.at(idx) : 0;
      CHECK_EQ(made, expected);
    }
    for (const auto& [idx, base] : r.inputs) {
      const Milli expected = mul_div_ceil(base, x.actual_throughput_bp, kBpOne);
      const Milli used = x.inputs_consumed.count(idx) != 0 ? x.inputs_consumed.at(idx) : 0;
      CHECK_EQ(used, expected);
    }
  }
  CHECK(saw_power_limited);
}

TEST(t05_bottleneck_math, "T05: half water and half power yield a 50% run, not a 25% run") {
  const Catalog& catalog = *testing::fixture_catalog("bottleneck");
  auto session = testing::new_session(catalog, "half_water_half_power");
  const InstanceId farm = testing::find_facility(*session, "testworld", "test_farm_food");
  session->step_day();
  const FacilityExplanation& x = explain(*session, farm);
  CHECK_EQ(x.desired_throughput_bp, 10000);
  CHECK_EQ(x.actual_throughput_bp, 5000);
  CHECK_EQ(x.outputs_produced.at(catalog.resource_index("food")), 20000);
  CHECK_EQ(x.inputs_consumed.at(catalog.resource_index("water")), 20000);
  CHECK_EQ(x.power_granted, 5000);
  CHECK(x.power_limited);
  CHECK(std::find(x.missing_inputs.begin(), x.missing_inputs.end(), catalog.resource_index("water")) !=
        x.missing_inputs.end());
}

TEST(t06_worker_conservation, "T06: assignments, transitions, Reserve and crew equal the pool every day") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  const InstanceId farm = testing::find_facility(*session, "homeworld", "agriculture_food");
  Command c;
  c.id = "farm_out";
  c.kind = CommandKind::AssignWorkers;
  c.planet_id = "homeworld";
  c.facility_id = farm;
  c.to_facility_id = 0;
  c.count = 20;
  CHECK(session->apply_command(c).accepted);
  for (int i = 0; i < 6; ++i) {
    session->step_day();
    const PlanetState& p = session->state().planet("homeworld");
    const DailyReport& d = p.last_day;
    CHECK_EQ(d.workers_assigned + d.workers_transitioning + d.workers_reserve + d.workers_crew, p.workers_total);
    CHECK(p.workers_total <= p.population);
  }
}

TEST(t07_transfer_exploit, "T07: a job-to-job transfer loses a production day and cannot be undone early") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  const InstanceId farm = testing::find_facility(*session, "homeworld", "agriculture_food");
  const InstanceId mine = testing::find_facility(*session, "homeworld", "extraction_iron");

  // A full job cannot absorb more workers.
  Command overfill;
  overfill.id = "farm_to_mine";
  overfill.kind = CommandKind::AssignWorkers;
  overfill.planet_id = "homeworld";
  overfill.facility_id = farm;
  overfill.to_facility_id = mine;
  overfill.count = 10;
  CommandResult full = session->apply_command(overfill);
  CHECK(!full.accepted);
  CHECK_EQ(full.reason, std::string("overstaffed"));

  // Open a construction job and move ten farm workers onto it: an occupied job
  // to another occupied job is ready on D+2.
  Command build;
  build.id = "build";
  build.kind = CommandKind::StartConstruction;
  build.planet_id = "homeworld";
  build.content_id = "public_clinic";
  CHECK(session->apply_command(build).accepted);
  const InstanceId job = session->state().facilities.back().id;

  const Day issued_on = session->state().day;
  Command move;
  move.id = "farm_to_job";
  move.kind = CommandKind::AssignWorkers;
  move.planet_id = "homeworld";
  move.facility_id = farm;
  move.to_facility_id = job;
  move.count = 10;
  CommandResult r = session->apply_command(move);
  CHECK(r.accepted);
  CHECK_EQ(session->state().transfers.back().ready_day, issued_on + 2);
  // They leave the farm immediately and are counted as transitioning.
  CHECK_EQ(session->state().find_facility(farm)->assigned_workers, 70);
  CHECK_EQ(session->state().find_facility(job)->construction->assigned_workers, 0);

  // Trying to send them straight back is rejected: the job holds nobody yet.
  Command undo;
  undo.id = "undo";
  undo.kind = CommandKind::AssignWorkers;
  undo.planet_id = "homeworld";
  undo.facility_id = job;
  undo.to_facility_id = farm;
  undo.count = 10;
  CommandResult undone = session->apply_command(undo);
  CHECK(!undone.accepted);
  CHECK_EQ(undone.reason, std::string("not_enough_workers"));

  // One full production day is lost, and the ready day cannot be shortened.
  session->step_day();
  const FacilityState* farm_state = session->state().find_facility(farm);
  CHECK_EQ(farm_state->assigned_workers, 70);
  CHECK_EQ(farm_state->last_explanation.staffing_bp, 8750);
  CHECK_EQ(session->state().find_facility(job)->construction->assigned_workers, 0);
  session->step_day();
  CHECK_EQ(session->state().find_facility(job)->construction->assigned_workers, 10);
}

TEST(t08_build_escrow, "T08: cancellation refunds only the unconsumed escrow, rounding included") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  const Milli steel_before = testing::stock(*session, "homeworld", "steel");
  const Milli machinery_before = testing::stock(*session, "homeworld", "machinery");

  Command start;
  start.id = "start";
  start.kind = CommandKind::StartConstruction;
  start.planet_id = "homeworld";
  start.content_id = "public_clinic";   // 20 steel / 4 machinery, 60 person-days
  CHECK(session->apply_command(start).accepted);
  const InstanceId job = session->state().facilities.back().id;
  // Escrow left the planet immediately.
  CHECK_EQ(testing::stock(*session, "homeworld", "steel"), steel_before - 20000);

  Command crew;
  crew.id = "crew";
  crew.kind = CommandKind::AssignWorkers;
  crew.planet_id = "homeworld";
  crew.facility_id = 0;
  crew.to_facility_id = job;
  crew.count = 15;
  CHECK(session->apply_command(crew).accepted);
  // The crew arrives in the due-work phase and works the same day.
  session->step_day();

  const FacilityState* job_state = session->state().find_facility(job);
  const ConstructionJob& j = *job_state->construction;
  CHECK_EQ(j.work_done, 15000);
  const Milli consumed_steel = j.consumed.at(catalog.steel());
  CHECK_EQ(consumed_steel, mul_div_floor(20000, j.work_done, j.work_total));
  CHECK_EQ(j.escrow.at(catalog.steel()), 20000 - consumed_steel);

  const Milli steel_before_cancel = testing::stock(*session, "homeworld", "steel");
  const Milli escrow_steel = j.escrow.at(catalog.steel());
  Command cancel;
  cancel.id = "cancel";
  cancel.kind = CommandKind::CancelConstruction;
  cancel.facility_id = job;
  CHECK(session->apply_command(cancel).accepted);
  // Only the unconsumed remainder comes back; consumed materials do not.
  CHECK_EQ(testing::stock(*session, "homeworld", "steel"), steel_before_cancel + escrow_steel);
  CHECK_EQ(escrow_steel, 20000 - consumed_steel);
  CHECK(session->state().find_facility(job) == nullptr);
  // The planetary slot is released for another job.
  Command again;
  again.id = "again";
  again.kind = CommandKind::StartConstruction;
  again.planet_id = "homeworld";
  again.content_id = "public_clinic";
  CHECK(session->apply_command(again).accepted);
  (void)machinery_before;
  (void)steel_before;
}

TEST(t08_escrow_cannot_be_farmed, "T08: repeated start and cancel never creates materials") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  const Milli steel_before = testing::stock(*session, "homeworld", "steel");
  for (int i = 0; i < 20; ++i) {
    Command start;
    start.id = "start" + std::to_string(i);
    start.kind = CommandKind::StartConstruction;
    start.planet_id = "homeworld";
    start.content_id = "public_clinic";
    CHECK(session->apply_command(start).accepted);
    const InstanceId job = session->state().facilities.back().id;
    Command cancel;
    cancel.id = "cancel" + std::to_string(i);
    cancel.kind = CommandKind::CancelConstruction;
    cancel.facility_id = job;
    CHECK(session->apply_command(cancel).accepted);
  }
  CHECK_EQ(testing::stock(*session, "homeworld", "steel"), steel_before);
}

TEST(t09_full_store, "T09: a full output store blocks production without losing the inputs") {
  const Catalog& catalog = *testing::fixture_catalog("bottleneck");
  auto session = testing::new_session(catalog, "full_store");
  const InstanceId farm = testing::find_facility(*session, "smallworld", "test_farm_food");
  const Milli water_before = testing::stock(*session, "smallworld", "water");
  session->step_day();
  const FacilityExplanation& x = explain(*session, farm);
  // Capacity is 10 units and 5 are already stored, so only 5 can be produced.
  CHECK_EQ(x.actual_throughput_bp, 1250);
  CHECK_EQ(x.outputs_produced.at(catalog.resource_index("food")), 5000);
  CHECK_EQ(testing::stock(*session, "smallworld", "food"), 10000);
  CHECK_EQ(x.primary_reason, std::string("output_store_full"));
  // The inputs that were not needed are still in the store.
  CHECK_EQ(testing::stock(*session, "smallworld", "water"), water_before - 5000);
}

TEST(explanation_names_the_real_cause, "a labour shortfall is never reported as missing material") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  const InstanceId farm = testing::find_facility(*session, "homeworld", "agriculture_food");
  Command c;
  c.id = "strip_farm";
  c.kind = CommandKind::AssignWorkers;
  c.planet_id = "homeworld";
  c.facility_id = farm;
  c.to_facility_id = 0;
  c.count = 40;
  CHECK(session->apply_command(c).accepted);
  session->step_day();
  session->step_day();
  const FacilityExplanation& x = explain(*session, farm);
  CHECK_EQ(x.staffing_bp, 5000);
  CHECK_EQ(x.actual_throughput_bp, 5000);
  CHECK_EQ(x.primary_reason, std::string("understaffed"));
  CHECK(x.missing_inputs.empty());
  CHECK(x.labour_limited);
}

