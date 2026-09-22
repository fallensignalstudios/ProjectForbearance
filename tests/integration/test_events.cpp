// T16-T21: policies, the accident chain, political meters, the mandate, relief.
#include "harness.hpp"

using namespace expansion;

namespace {

const EventInstance* open_event(const Session& session, const std::string& event_id) {
  for (const auto& e : session.state().events) {
    if (e.event_id == event_id && e.resolution == EventResolution::Open) return &e;
  }
  return nullptr;
}

// Runs the seeded worn-mine fixture until the Safety Warning opens.
std::unique_ptr<Session> run_to_safety_warning(const Catalog& catalog) {
  auto session = testing::new_session(catalog, "worn_mine", "dominion");
  for (int i = 0; i < 12; ++i) {
    session->step_day();
    if (open_event(*session, "safety_warning") != nullptr) return session;
  }
  throw SimError("test: the seeded worn mine never raised a Safety Warning");
}

}  // namespace

TEST(t16_policy_lifecycle, "T16: one policy at a time, a seven-day minimum, and no paused adherence farming") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency", "dominion");
  const int adherence_start = session->state().political.adherence;

  Command mobilise;
  mobilise.id = "mobilise";
  mobilise.kind = CommandKind::SelectPolicy;
  mobilise.planet_id = "homeworld";
  mobilise.content_id = "emergency_mobilization";
  CHECK(session->apply_command(mobilise).accepted);
  // The one-time adherence change commits with the command.
  CHECK_EQ(session->state().political.adherence, adherence_start + 2);

  // Re-selecting the same policy is rejected, so oscillating while paused
  // cannot farm adherence.
  Command again = mobilise;
  again.id = "mobilise_again";
  CHECK(!session->apply_command(again).accepted);

  Command back;
  back.id = "back_to_normal";
  back.kind = CommandKind::SelectPolicy;
  back.planet_id = "homeworld";
  back.content_id = "normal";
  CommandResult early = session->apply_command(back);
  CHECK(!early.accepted);
  CHECK_EQ(early.reason, std::string("policy_minimum_not_met"));

  // Economic effects begin with the next day, and fatigue climbs instead of resting.
  session->step_day();
  CHECK_EQ(session->state().planet("homeworld").fatigue_bp, 250);
  const InstanceId foundry = testing::find_facility(*session, "homeworld", "foundry_steel");
  const FacilityState* foundry_state = session->state().find_facility(foundry);
  const FacilityExplanation& x = foundry_state->last_explanation;
  CHECK_EQ(x.policy_bp, 11500);

  for (int i = 0; i < 6; ++i) session->step_day();
  CHECK_EQ(session->state().planet("homeworld").policy_effective_days, 7);
  Command back_now;
  back_now.id = "back_now";
  back_now.kind = CommandKind::SelectPolicy;
  back_now.planet_id = "homeworld";
  back_now.content_id = "normal";
  CHECK(session->apply_command(back_now).accepted);

  // It cannot be reapplied until the cooldown expires.
  Command retry;
  retry.id = "retry";
  retry.kind = CommandKind::SelectPolicy;
  retry.planet_id = "homeworld";
  retry.content_id = "emergency_mobilization";
  CommandResult cooling = session->apply_command(retry);
  CHECK(!cooling.accepted);
  CHECK_EQ(cooling.reason, std::string("policy_cooldown"));
}

TEST(t16_rationing_reduces_demand, "T16: Rationing reduces Food demand to 85% and adjusts the stability target") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency", "dominion");
  session->step_day();
  const Bp target_before = session->state().planet("homeworld").last_day.stability_target_bp;
  Command c;
  c.id = "ration";
  c.kind = CommandKind::SelectPolicy;
  c.planet_id = "homeworld";
  c.content_id = "rationing";
  CHECK(session->apply_command(c).accepted);
  session->step_day();
  const DailyReport& d = session->state().planet("homeworld").last_day;
  CHECK_EQ(d.food_demand, 85000);
  CHECK_EQ(d.stability_target_bp, target_before - 500);
}

TEST(t17_event_prevention, "T17: restoring condition in time prevents the accident") {
  const Catalog& catalog = *testing::fixture_catalog("worn_mine");
  auto session = run_to_safety_warning(catalog);
  const EventInstance* warning = open_event(*session, "safety_warning");
  CHECK(warning != nullptr);
  const InstanceId mine = warning->facility_id;
  CHECK_EQ(session->state().find_facility(mine)->facility_id, std::string("extraction_site"));

  Command service;
  service.id = "service";
  service.kind = CommandKind::ServiceFacility;
  service.facility_id = mine;
  CommandResult r = session->apply_command(service);
  CHECK_MSG(r.accepted, r.reason + " " + r.detail);

  for (int i = 0; i < 4; ++i) session->step_day();
  CHECK(session->state().find_facility(mine)->condition_bp >= 7500);
  CHECK(open_event(*session, "safety_warning") == nullptr);
  CHECK(open_event(*session, "mining_accident") == nullptr);
  bool prevented = false;
  for (const auto& f : session->state().facts) {
    if (f.kind == "event_prevented") prevented = true;
  }
  CHECK(prevented);
  // No forced replacement crisis follows.
  for (int i = 0; i < 10; ++i) session->step_day();
  CHECK(open_event(*session, "mining_accident") == nullptr);
  CHECK(open_event(*session, "worker_demands") == nullptr);
}

TEST(t17_accident_opens_when_unaddressed, "T17: an unaddressed warning becomes an accident with a halved run") {
  const Catalog& catalog = *testing::fixture_catalog("worn_mine");
  auto session = run_to_safety_warning(catalog);
  const InstanceId mine = open_event(*session, "safety_warning")->facility_id;
  for (int i = 0; i < 4; ++i) session->step_day();
  const EventInstance* accident = open_event(*session, "mining_accident");
  CHECK(accident != nullptr);
  CHECK_EQ(accident->facility_id, mine);
  // The simulation pauses for the decision by default.
  CHECK(session->state().paused_for_decision);
  session->step_day();
  const FacilityState* mine_state = session->state().find_facility(mine);
  const FacilityExplanation& x = mine_state->last_explanation;
  CHECK_EQ(x.effects_bp, 5000);
  bool tagged = false;
  for (const auto& t : x.modifier_tags) {
    if (t == "accident") tagged = true;
  }
  CHECK(tagged);
  CHECK_EQ(x.primary_reason, std::string("active_modifier"));
}

TEST(t18_outcome_idempotency, "T18: an accident response applies its cost and effect exactly once across a reload") {
  const Catalog& catalog = *testing::fixture_catalog("worn_mine");
  auto session = run_to_safety_warning(catalog);
  for (int i = 0; i < 4; ++i) session->step_day();
  const EventInstance* accident = open_event(*session, "mining_accident");
  CHECK(accident != nullptr);
  const InstanceId instance = accident->id;
  const InstanceId mine = accident->facility_id;
  const Milli machinery_before = testing::stock(*session, "homeworld", "machinery");
  const int adherence_before = session->state().political.adherence;

  Command choose;
  choose.id = "repair";
  choose.kind = CommandKind::ResolveEvent;
  choose.event_instance_id = instance;
  choose.content_id = "repair_immediately";
  CHECK(session->apply_command(choose).accepted);
  CHECK_EQ(testing::stock(*session, "homeworld", "machinery"), machinery_before - 8000);
  CHECK_EQ(session->state().political.adherence, adherence_before + 2);   // Dominion

  // Repeating the same choice, and repeating the same command id, both no-op.
  Command repeat = choose;
  repeat.id = "repair_again";
  CommandResult second = session->apply_command(repeat);
  CHECK(!second.accepted);
  CHECK_EQ(testing::stock(*session, "homeworld", "machinery"), machinery_before - 8000);

  // Save and reload around the scheduled restoration.
  SessionState copy = session->state();
  auto reloaded = Session::from_state(catalog, copy);
  for (int i = 0; i < 3; ++i) {
    session->step_day();
    reloaded->step_day();
  }
  CHECK_EQ(session->canonical_hash(), reloaded->canonical_hash());
  CHECK(session->state().find_facility(mine)->condition_bp >= 8000);
  const FacilityState* mine_state = session->state().find_facility(mine);
  const FacilityExplanation& x = mine_state->last_explanation;
  CHECK_EQ(x.effects_bp, 10000);   // the accident modifier was cleared once
}

TEST(t18_deferral_then_remedy, "T18: a later paid remedy after deferral cancels the pending Worker Demands") {
  const Catalog& catalog = *testing::fixture_catalog("worn_mine");
  auto session = run_to_safety_warning(catalog);
  for (int i = 0; i < 4; ++i) session->step_day();
  const EventInstance* accident = open_event(*session, "mining_accident");
  CHECK(accident != nullptr);
  const InstanceId instance = accident->id;

  Command defer;
  defer.id = "defer";
  defer.kind = CommandKind::ResolveEvent;
  defer.event_instance_id = instance;
  defer.content_id = "defer_action";
  CHECK(session->apply_command(defer).accepted);
  // Deferral leaves the remedies selectable and the accident in place.
  CHECK(open_event(*session, "mining_accident") != nullptr);
  CHECK_EQ(open_event(*session, "mining_accident")->deadline_day, -1);
  CHECK(!session->state().scheduled.empty());

  session->step_day();
  session->step_day();
  Command repair;
  repair.id = "late_repair";
  repair.kind = CommandKind::ResolveEvent;
  repair.event_instance_id = instance;
  repair.content_id = "investigate_and_repair";
  CommandResult r = session->apply_command(repair);
  CHECK_MSG(r.accepted, r.reason + " " + r.detail);
  // The pending consequence was cancelled because it had not opened.
  for (const auto& s : session->state().scheduled) CHECK(s.choice_id != std::string("defer_action"));
  for (int i = 0; i < 8; ++i) session->step_day();
  CHECK(open_event(*session, "worker_demands") == nullptr);
}

TEST(t19_distinct_political_meters, "T19: changing adherence alone does not move public stability") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency", "dominion");
  session->step_day();
  const Bp stability_before = session->state().planet("homeworld").stability_bp;
  const Bp target_before = session->state().planet("homeworld").last_day.stability_target_bp;
  const int adherence_before = session->state().political.adherence;

  Command mobilise;
  mobilise.id = "mobilise";
  mobilise.kind = CommandKind::SelectPolicy;
  mobilise.planet_id = "homeworld";
  mobilise.content_id = "emergency_mobilization";
  CHECK(session->apply_command(mobilise).accepted);
  // Adherence moved with the command; stability did not move at all.
  CHECK_EQ(session->state().political.adherence, adherence_before + 2);
  CHECK_EQ(session->state().planet("homeworld").stability_bp, stability_before);
  // The policy's own stability-target adjustment is separate and explicit.
  session->step_day();
  CHECK_EQ(session->state().planet("homeworld").last_day.stability_target_bp, target_before - 500);
}

TEST(t20_mandate_freight, "T20: the 220-volume mandate needs more than one voyage and no instant submission") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency", "dominion");
  const ScenarioDef& sc = catalog.scenario("first_dependency");
  // Volume check first: the requirement exceeds one hold.
  Milli total = 0;
  for (const auto& [idx, qty] : sc.mandate.requirement) total += qty;
  CHECK_EQ(total, 220000);
  CHECK(total > sc.freight.cargo_capacity);

  while (session->state().day < sc.mandate.issue_day) session->step_day();
  CHECK(session->state().demand.issued);
  // There is no way to submit resources without moving them.
  Command over;
  over.id = "over_manifest";
  over.kind = CommandKind::LaunchStrategicMission;
  over.resources = {{catalog.fuel(), 180000}, {catalog.steel(), 40000}};
  CommandResult r = session->apply_command(over);
  CHECK(!r.accepted);
  CHECK_EQ(r.reason, std::string("manifest_exceeds_capacity"));

  Command beyond;
  beyond.id = "beyond_demand";
  beyond.kind = CommandKind::LaunchStrategicMission;
  beyond.resources = {{catalog.food(), 10000}};
  CHECK_EQ(session->apply_command(beyond).reason, std::string("manifest_exceeds_demand"));

  Command mission;
  mission.id = "mission_one";
  mission.kind = CommandKind::LaunchStrategicMission;
  mission.resources = {{catalog.fuel(), 90000}, {catalog.steel(), 20000}};
  CommandResult m = session->apply_command(mission);
  CHECK_MSG(m.accepted, m.reason + " " + m.detail);
  const Milli fuel_before = testing::stock(*session, "homeworld", "fuel");
  session->step_day();
  CHECK_EQ(session->state().ship.mission, ShipMission::StrategicOutbound);
  // Cargo Fuel and the eight propulsion Fuel are separate charges, over and
  // above whatever the refinery made that day.
  const Milli fuel_made = session->state().planet("homeworld").last_day.produced[
      static_cast<std::size_t>(catalog.fuel())];
  CHECK_EQ(testing::stock(*session, "homeworld", "fuel"), fuel_before + fuel_made - 90000 - 8000);
  Milli propulsion = 0;
  for (const auto& t : session->state().ledger) {
    if (t.resource == catalog.fuel() && t.cause == std::string("mandate_propulsion")) propulsion += t.quantity;
  }
  CHECK_EQ(propulsion, 8000);
  CHECK(!session->state().demand.resolved);

  const Day departure = session->state().ship.departure_day;
  for (int i = 0; i < 5; ++i) session->step_day();
  CHECK_EQ(session->state().day, departure + 5);
  CHECK_EQ(session->state().demand.delivered.at(catalog.fuel()), 90000);
  CHECK(!session->state().demand.resolved);   // one voyage is not enough
  for (int i = 0; i < 5; ++i) session->step_day();
  CHECK_EQ(session->state().ship.mission, ShipMission::None);
  CHECK_EQ(session->state().ship.location_planet, std::string("homeworld"));
  CHECK_EQ(session->state().day, departure + 10);
}

TEST(t20_negotiation, "T20: a negotiated mandate fits one full-service mission and keeps the deadline") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency", "reformation");
  const ScenarioDef& sc = catalog.scenario("first_dependency");
  while (session->state().day < sc.mandate.issue_day) session->step_day();
  const Day deadline_before = session->state().demand.deadline_day;
  const int adherence_before = session->state().political.adherence;

  Command negotiate;
  negotiate.id = "negotiate";
  negotiate.kind = CommandKind::RespondToDemand;
  negotiate.content_id = "negotiate";
  CHECK(session->apply_command(negotiate).accepted);
  CHECK_EQ(session->state().demand.deadline_day, deadline_before);
  CHECK_EQ(session->state().political.adherence, adherence_before + 2);   // Reformation
  Milli total = 0;
  for (const auto& [idx, qty] : session->state().demand.required) total += qty;
  CHECK_EQ(total, 110000);
  CHECK(total <= sc.freight.cargo_capacity);

  Command twice;
  twice.id = "negotiate_twice";
  twice.kind = CommandKind::RespondToDemand;
  twice.content_id = "negotiate";
  CHECK(!session->apply_command(twice).accepted);
}

TEST(t21_relief_and_loss, "T21: relief arrives after two days, once per world, and the loss rule holds") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency", "dominion");
  // Starve Homeworld: idle the farm and the waterworks.
  for (const char* recipe : {"agriculture_food", "waterworks_water", "hub_water"}) {
    Command idle;
    idle.id = std::string("idle_") + recipe;
    idle.kind = CommandKind::SetFacilityIdle;
    idle.facility_id = testing::find_facility(*session, "homeworld", recipe);
    idle.flag = true;
    CHECK(session->apply_command(idle).accepted);
  }
  const PlanetState* home = &session->state().planet("homeworld");
  int guard = 0;
  while (!home->survival_emergency_active && guard++ < 60) {
    session->step_day();
    home = &session->state().planet("homeworld");
  }
  CHECK(home->survival_emergency_active);
  const int adherence_before = session->state().political.adherence;

  Command relief;
  relief.id = "relief";
  relief.kind = CommandKind::RequestRelief;
  relief.planet_id = "homeworld";
  CommandResult r = session->apply_command(relief);
  CHECK_MSG(r.accepted, r.reason + " " + r.detail);
  CHECK_EQ(session->state().political.adherence, adherence_before - 10);
  CHECK(session->state().flags.count("relief_used") != 0);
  Command pending;
  pending.id = "relief_pending";
  pending.kind = CommandKind::RequestRelief;
  pending.planet_id = "homeworld";
  CHECK_EQ(session->apply_command(pending).reason, std::string("relief_pending"));

  auto relief_grant_on = [&](Day day) {
    Milli total = 0;
    for (const auto& t : session->state().ledger) {
      if (t.day == day && t.from_account == std::string("external") && t.cause == std::string("relief_grant")) {
        total += t.quantity;
      }
    }
    return total;
  };
  session->step_day();
  CHECK_EQ(relief_grant_on(session->state().day), 0);   // not yet
  session->step_day();
  // The grant is an explicit external delivery recorded in the ledger. The same
  // day's civilian consumption may absorb it, so the ledger is the evidence.
  CHECK_EQ(relief_grant_on(session->state().day), 250000);
  CHECK(session->state().planet("homeworld").last_day.food_fulfilment_bp > 0);

  // A second request while one is in flight is rejected as pending, and once the
  // emergency returns it is rejected because relief is used up for this world.
  int guard2 = 0;
  const PlanetState* home2 = &session->state().planet("homeworld");
  while (!home2->survival_emergency_active && guard2++ < 30) {
    session->step_day();
    home2 = &session->state().planet("homeworld");
  }
  CHECK(home2->survival_emergency_active);
  Command again;
  again.id = "relief_again";
  again.kind = CommandKind::RequestRelief;
  again.planet_id = "homeworld";
  CHECK_EQ(session->apply_command(again).reason, std::string("relief_already_used"));

  // With no relief pending, ten consecutive emergency days end the scenario.
  guard = 0;
  while (session->state().lifecycle == SessionLifecycle::Running && guard++ < 40) session->step_day();
  CHECK_EQ(session->state().lifecycle, SessionLifecycle::Failed);
  // Population was never silently reduced.
  CHECK_EQ(session->state().planet("homeworld").population, 1000);
}

TEST(t25_news_truth, "T25: every headline has supporting facts, and a transition is reported once") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency", "dominion");
  Command idle;
  idle.id = "idle_farm";
  idle.kind = CommandKind::SetFacilityIdle;
  idle.facility_id = testing::find_facility(*session, "homeworld", "agriculture_food");
  idle.flag = true;
  CHECK(session->apply_command(idle).accepted);
  for (int i = 0; i < 20; ++i) session->step_day();

  std::map<std::string, int> by_key;
  for (const auto& n : session->state().news) {
    CHECK_MSG(!n.template_key.empty(), "a news entry must name its template");
    CHECK_MSG(!n.source_facts.empty(), "news entry " + n.template_key + " has no supporting fact");
    for (InstanceId f : n.source_facts) {
      bool found = false;
      for (const auto& fact : session->state().facts) {
        if (fact.id == f) found = true;
      }
      CHECK_MSG(found, "news entry " + n.template_key + " cites a fact that is not recorded");
    }
    if (!n.dedupe_key.empty()) by_key[n.dedupe_key] += 1;
  }
  for (const auto& [key, count] : by_key) CHECK_MSG(count == 1, "duplicate news for key " + key);

  // A shortage opens after two missed days, not on the first low stock reading.
  int shortage_entries = 0;
  for (const auto& n : session->state().news) {
    if (n.template_key == "news.shortage_opened") ++shortage_entries;
  }
  CHECK_EQ(shortage_entries, 1);
}
