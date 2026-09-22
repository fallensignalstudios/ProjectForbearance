// Scenario-level behaviour: the shared-capacity tradeoff and the completion
// contract (TDD 14).
#include "expansion/derived.hpp"
#include "expansion/read_models.hpp"
#include "expansion/text.hpp"
#include "harness.hpp"

using namespace expansion;

TEST(scenario_dates_are_visible, "the scenario briefing dates are readable from the catalog, not a surprise") {
  const Catalog& catalog = testing::shipped_catalog();
  const ScenarioDef& sc = catalog.scenario("first_dependency");
  CHECK_EQ(sc.expedition.launch_deadline_day, 60);
  CHECK_EQ(sc.mandate.issue_day, 75);
  CHECK_EQ(sc.mandate.deadline_day, 100);
  CHECK_EQ(sc.mandate.last_departure_day, 95);
  CHECK_EQ(sc.completion.evaluation_day, 120);
}

TEST(shared_capacity_tradeoff, "a strategic mission blocks the colonial route and the cost is recorded") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency", "dominion");
  const ScenarioDef& sc = catalog.scenario("first_dependency");

  // Enable the colonial route with a positive Food target, then commit the ship
  // to the front. The missed manifest must be recorded, not hidden.
  Command targets;
  targets.id = "targets";
  targets.kind = CommandKind::UpdateRoute;
  targets.content_id = "outbound";
  targets.resources = {{catalog.food(), 60000}};
  CHECK(session->apply_command(targets).accepted);
  Command enable;
  enable.id = "enable";
  enable.kind = CommandKind::UpdateRoute;
  enable.content_id = "enable";
  enable.flag = true;
  CHECK(session->apply_command(enable).accepted);

  while (session->state().day < sc.mandate.issue_day) session->step_day();
  Command mission;
  mission.id = "mission";
  mission.kind = CommandKind::LaunchStrategicMission;
  mission.resources = {{catalog.fuel(), 90000}, {catalog.steel(), 20000}};
  CommandResult r = session->apply_command(mission);
  CHECK_MSG(r.accepted, r.reason + " " + r.detail);
  CHECK(r.detail.find("unavailable") != std::string::npos);

  const int missed_before = session->state().history.missed_colonial_food_manifests;
  for (int i = 0; i < 9; ++i) session->step_day();
  CHECK(session->state().history.missed_colonial_food_manifests > missed_before);
  bool reported = false;
  for (const auto& n : session->state().news) {
    if (n.template_key == "news.missed_colonial_manifest") reported = true;
  }
  CHECK(reported);
}

TEST(completion_is_compromised_when_predicates_fail, "an unfulfilled mandate ends the evaluation as Compromised") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency", "dominion");
  // Never launch the colony, never answer the mandate: the civilisation survives
  // but the predicates fail.
  while (session->state().day < 120 && session->state().lifecycle == SessionLifecycle::Running) {
    session->step_day();
  }
  CHECK_EQ(session->state().lifecycle, SessionLifecycle::Compromised);
  bool evaluated = false;
  for (const auto& f : session->state().facts) {
    if (f.kind != "scenario_evaluated") continue;
    evaluated = true;
    CHECK(!f.text_args.empty());
    bool named_colony = false;
    for (const auto& t : f.text_args) {
      if (t.find("colony") != std::string::npos) named_colony = true;
    }
    CHECK(named_colony);
  }
  CHECK(evaluated);
  // Failure is distinguished from political compromise.
  CHECK(session->state().planet("homeworld").population == 1000);
}

TEST(read_models_render, "every required view renders from a committed snapshot") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency", "reformation");
  Command launch;
  launch.id = "launch";
  launch.kind = CommandKind::LaunchColonization;
  CHECK(session->apply_command(launch).accepted);
  for (int i = 0; i < 6; ++i) session->step_day();

  CHECK(read::civilization_overview(*session).find("CIVILIZATION") != std::string::npos);
  CHECK(read::network_map(*session).find("freighter") != std::string::npos);
  const std::string planet = read::planet_inspector(*session, "homeworld");
  CHECK(planet.find("workforce") != std::string::npos);
  CHECK(planet.find("current-rate estimate") != std::string::npos);
  const InstanceId farm = testing::find_facility(*session, "homeworld", "agriculture_food");
  const std::string facility = read::facility_inspector(*session, farm);
  CHECK(facility.find("run factor") != std::string::npos);
  CHECK(facility.find("primary reason") != std::string::npos);
  CHECK(read::freight_inspector(*session).find("reserve floors") != std::string::npos);
  CHECK(read::decision_drawer(*session).find("DECISIONS") != std::string::npos);
  CHECK(read::history_drawer(*session, 20, "").find("HISTORY") != std::string::npos);
  CHECK(read::ledger_view(*session, session->state().day).find("sector-owned") != std::string::npos);
  CHECK(read::outcome_report(*session).find("relief contract used") != std::string::npos);
  // An alert can be traced to a real cause.
  auto concerns = read::top_concerns(*session, 3);
  for (const auto& c : concerns) CHECK(!c.headline.empty());
}

TEST(reserve_floor_protects_export_not_civilians, "the default Food floor holds back export, not civilian supply") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency", "dominion");
  const PlanetState& home = session->state().planet("homeworld");
  // Three days of civilian Food demand.
  CHECK_EQ(default_outbound_floor(session->state(), catalog, home, catalog.food()), 300000);
  // Civilians are still served from stock on day one.
  session->step_day();
  CHECK_EQ(session->state().planet("homeworld").last_day.food_fulfilment_bp, kBpOne);
}

TEST(news_renders_without_placeholders, "no rendered news line ships with an unsubstituted placeholder") {
  const Catalog& catalog = testing::shipped_catalog();
  // Walk several runs so most templates get exercised, including the accident
  // chain and the relief path.
  struct Run {
    const char* scenario;
    const char* faction;
    bool break_water;
  };
  const Run runs[] = {{"first_dependency", "dominion", false}, {"first_dependency", "reformation", true}};
  int rendered = 0;
  for (const auto& r : runs) {
    auto session = testing::new_session(catalog, r.scenario, r.faction);
    Command launch;
    launch.id = "launch";
    launch.kind = CommandKind::LaunchColonization;
    session->apply_command(launch);
    if (r.break_water) {
      for (const char* recipe : {"waterworks_water", "agriculture_food"}) {
        Command idle;
        idle.id = std::string("idle_") + recipe;
        idle.kind = CommandKind::SetFacilityIdle;
        idle.facility_id = testing::find_facility(*session, "homeworld", recipe);
        idle.flag = true;
        session->apply_command(idle);
      }
    }
    Command policy;
    policy.id = "policy";
    policy.kind = CommandKind::SelectPolicy;
    policy.planet_id = "homeworld";
    policy.content_id = "rationing";
    session->apply_command(policy);
    for (int i = 0; i < 40 && session->state().lifecycle == SessionLifecycle::Running; ++i) session->step_day();

    for (const auto& n : session->state().news) {
      const std::string line = text::news_line(n);
      ++rendered;
      CHECK_MSG(line.find('{') == std::string::npos,
                "news template '" + n.template_key + "' left a placeholder unfilled: " + line);
      CHECK_MSG(line.find('}') == std::string::npos,
                "news template '" + n.template_key + "' left a placeholder unfilled: " + line);
      // A template with no entry at all renders as its own key, which is also a
      // defect rather than a message.
      CHECK_MSG(line.find(n.template_key) == std::string::npos,
                "news template '" + n.template_key + "' has no display string");
    }
  }
  CHECK(rendered > 20);
}

TEST(display_names_are_readable, "content ids are rendered as words, never as raw identifiers") {
  CHECK_EQ(text::display_name("iron_ore"), std::string("Iron Ore"));
  CHECK_EQ(text::display_name("extraction_site"), std::string("Extraction Site"));
  CHECK_EQ(text::display_name("homeworld"), std::string("Homeworld"));
  CHECK_EQ(text::display_name(""), std::string(""));

  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency", "dominion");
  Command launch;
  launch.id = "launch";
  launch.kind = CommandKind::LaunchColonization;
  CHECK(session->apply_command(launch).accepted);
  for (int i = 0; i < 20; ++i) session->step_day();
  bool saw_resource_line = false;
  for (const auto& n : session->state().news) {
    const std::string line = text::news_line(n);
    CHECK_MSG(line.find("iron_ore") == std::string::npos, "a raw resource id reached the reader: " + line);
    CHECK_MSG(line.find("extraction_site") == std::string::npos, "a raw facility id reached the reader: " + line);
    if (line.find("Iron Ore") != std::string::npos) saw_resource_line = true;
  }
  CHECK(saw_resource_line);
}
