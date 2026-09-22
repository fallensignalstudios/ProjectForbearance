// The structured views a widget host binds (TDD 15.2). The text read models are
// checked in test_derived.cpp; these check that the same information survives as
// fields, with identity and display text kept apart.
#include <algorithm>
#include <cstddef>

#include "expansion/text.hpp"
#include "expansion/view_models.hpp"
#include "harness.hpp"

using namespace expansion;

namespace {

const view::StockRow* store_of(const view::PlanetView& p, const std::string& resource_id) {
  for (const auto& row : p.stores) {
    if (row.resource_id == resource_id) return &row;
  }
  return nullptr;
}

const view::NeedRow* need_of(const view::PlanetView& p, const std::string& need_id) {
  for (const auto& row : p.needs) {
    if (row.need_id == need_id) return &row;
  }
  return nullptr;
}

}  // namespace

TEST(view_sector_answers_the_first_question, "the sector view carries risk, cause and the clock") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  auto v_out = view::sector(*session);
  CHECK_MSG(v_out.ok(), v_out.error().message);
  const view::SectorView& v = v_out.value();
  CHECK_EQ(v.day, 0);
  CHECK_EQ(v.evaluation_day, 120);
  CHECK_EQ(v.days_to_evaluation, 120);
  CHECK_EQ(v.lifecycle, SessionLifecycle::Running);
  CHECK(!v.lifecycle_label.empty());
  CHECK_EQ(v.planet_ids.size(), session->state().planets.size());
  CHECK(!v.mandate_issued);
  CHECK(v.mandate_status_label.empty());

  for (int i = 0; i < 40; ++i) session->step_day();
  auto later_out = view::sector(*session);
  CHECK(later_out.ok());
  const view::SectorView& later = later_out.value();
  CHECK_EQ(later.day, 40);
  CHECK_EQ(later.days_to_evaluation, 80);
  CHECK_EQ(later.revision, session->state().revision);
  // Every concern names a cause, because a warning without one is not actionable.
  for (const auto& c : later.concerns) {
    CHECK(!c.headline.empty());
    CHECK(!c.cause.empty());
  }
}

TEST(view_planet_separates_identity_from_display, "a row carries the id and the words, never one as the other") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  session->step_day();
  auto p_out = view::planet(*session, "homeworld");
  CHECK_MSG(p_out.ok(), p_out.error().message);
  const view::PlanetView& p = p_out.value();
  CHECK_EQ(p.planet_id, std::string("homeworld"));
  CHECK_EQ(p.label, std::string("Homeworld"));
  CHECK_EQ(p.stores.size(), static_cast<std::size_t>(catalog.resource_count()));

  const view::StockRow* ore = store_of(p, "iron_ore");
  CHECK(ore != nullptr);
  CHECK_EQ(ore->resource_id, std::string("iron_ore"));
  CHECK_EQ(ore->label, std::string("Iron Ore"));
  // The exact integer and its rendering agree, and the text was not re-derived.
  CHECK_EQ(ore->on_hand.value, testing::stock(*session, "homeworld", "iron_ore"));
  CHECK_EQ(ore->on_hand.text, format_milli(ore->on_hand.value));
  // The mine produced 48 units; the foundry consumed 30 of them, so the store
  // moved by 18. A row reports both, because one without the other misleads.
  CHECK_EQ(ore->produced_today.value, 48000);
  CHECK_EQ(ore->consumed_today.value, 30000);
  CHECK_EQ(ore->net_today, 18000);
  CHECK_EQ(ore->days_of_cover, -1);   // a rising store is not a countdown
  CHECK(!ore->full);
}

TEST(view_planet_reports_needs_and_power, "the five civilian needs are all present with their fulfilment") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  session->step_day();
  auto p_out = view::planet(*session, "homeworld");
  CHECK_MSG(p_out.ok(), p_out.error().message);
  const view::PlanetView& p = p_out.value();
  CHECK_EQ(p.needs.size(), static_cast<std::size_t>(5));
  for (const char* id : {"food", "water", "power", "housing", "clinic"}) {
    const view::NeedRow* row = need_of(p, id);
    CHECK_MSG(row != nullptr, std::string("missing need ") + id);
    CHECK(!row->label.empty());
  }
  CHECK(need_of(p, "food")->met);
  CHECK_EQ(p.power_generated.value, 110000);
  CHECK_EQ(p.power_used.value, 96000);
  CHECK_EQ(p.power_spare.value, 14000);
  CHECK_EQ(p.workers_reserve, 90);
  CHECK(!p.facilities.empty());
  CHECK_EQ(p.health_bp, session->state().planet("homeworld").health_bp);

  // An unknown world is a typed error, not a throw and not an empty view a host
  // would draw as zeroes.
  auto missing = view::planet(*session, "nowhere");
  CHECK(!missing.ok());
  CHECK_EQ(missing.error().code, ErrorCode::NotFound);
}

TEST(view_store_counts_down_when_it_drains, "a falling store reports days of cover") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  // Idle the iron mine and the foundry eats the standing ore, so the store falls
  // with nothing replacing it. That is the case a countdown exists for.
  Command idle;
  idle.id = "idle";
  idle.kind = CommandKind::SetFacilityIdle;
  idle.facility_id = testing::find_facility(*session, "homeworld", "extraction_iron");
  idle.flag = true;
  CHECK(session->apply_command(idle).accepted);
  session->step_day();

  auto p_out = view::planet(*session, "homeworld");
  CHECK_MSG(p_out.ok(), p_out.error().message);
  const view::PlanetView& p = p_out.value();
  const view::StockRow* ore = store_of(p, "iron_ore");
  CHECK(ore != nullptr);
  CHECK_EQ(ore->produced_today.value, 0);
  CHECK(ore->net_today < 0);
  CHECK(ore->days_of_cover >= 0);
  // The countdown is the honest floor: stock divided by today's drain.
  CHECK_EQ(ore->days_of_cover, div_floor(ore->on_hand.value, -ore->net_today));

  // Every other row agrees with the same rule, in both directions.
  for (const auto& row : p.stores) {
    if (row.net_today < 0) {
      CHECK_EQ(row.days_of_cover, div_floor(row.on_hand.value, -row.net_today));
    } else {
      CHECK_EQ(row.days_of_cover, -1);   // a steady or rising store is not a countdown
    }
  }
}

TEST(view_facility_names_the_real_cause, "a card reports the typed reason, not a summary word") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  session->step_day();
  const InstanceId mine = testing::find_facility(*session, "homeworld", "extraction_iron");
  auto card_out = view::facility(*session, mine);
  CHECK_MSG(card_out.ok(), card_out.error().message);
  const view::FacilityCard& card = card_out.value();
  CHECK_EQ(card.id, mine);
  CHECK_EQ(card.facility_id, std::string("extraction_site"));
  CHECK_EQ(card.recipe_id, std::string("extraction_iron"));
  // A facility with fixed recipe variants is ambiguous without the variant named.
  CHECK(card.label.find("Extraction Site") != std::string::npos);
  CHECK(card.label.find("--") != std::string::npos);
  CHECK_EQ(card.state_label, std::string("Active"));
  CHECK_EQ(card.throughput_bp, kBpOne);
  CHECK(!card.reason_id.empty());
  CHECK(!card.reason_text.empty());
  CHECK(card.reason_text != card.reason_id);   // display text, not the id
  CHECK(!card.power_limited);
  CHECK(!card.labour_limited);
  CHECK(card.missing_inputs.empty());
  CHECK_EQ(card.required_workers, 40);
  CHECK_EQ(card.assigned_workers, 40);
  CHECK_EQ(card.staffing_bp, kBpOne);
  CHECK(!card.outputs.empty());
  CHECK_EQ(card.outputs.front().label, std::string("Iron Ore"));

  auto missing_facility = view::facility(*session, 999999);
  CHECK(!missing_facility.ok());
  CHECK_EQ(missing_facility.error().code, ErrorCode::NotFound);
}

TEST(view_facility_shows_a_build_in_progress, "an unfinished job reports its crew and its progress") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  Command start;
  start.id = "start";
  start.kind = CommandKind::StartConstruction;
  start.planet_id = "homeworld";
  start.content_id = "public_clinic";
  CHECK(session->apply_command(start).accepted);
  const InstanceId job = session->state().facilities.back().id;

  Command crew;
  crew.id = "crew";
  crew.kind = CommandKind::AssignWorkers;
  crew.planet_id = "homeworld";
  crew.facility_id = 0;
  crew.to_facility_id = job;
  crew.count = 15;
  CHECK(session->apply_command(crew).accepted);
  session->step_day();

  auto card_out = view::facility(*session, job);
  CHECK_MSG(card_out.ok(), card_out.error().message);
  const view::FacilityCard& card = card_out.value();
  CHECK(card.under_construction);
  CHECK_EQ(card.lifecycle, FacilityLifecycle::UnderConstruction);
  CHECK_EQ(card.assigned_workers, 15);   // the build crew, not the operating staff
  CHECK(card.construction_progress_bp > 0);
  CHECK(card.construction_progress_bp < kBpOne);
  CHECK(card.state_label.find("Under construction") != std::string::npos);
  CHECK(card.state_label.find('%') != std::string::npos);
}

TEST(view_freight_describes_the_leg, "the freight view carries the phase, the clock and the manifest") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  auto f_out = view::freight(*session);
  CHECK_MSG(f_out.ok(), f_out.error().message);
  const view::FreightView& f = f_out.value();
  CHECK_EQ(f.phase, ShipPhase::Docked);
  CHECK(!f.phase_label.empty());
  CHECK_EQ(f.location_label, std::string("Homeworld"));
  CHECK_EQ(f.days_remaining, -1);
  CHECK(f.cargo.empty());
  CHECK_EQ(f.cargo_volume.value, 0);
  CHECK_EQ(f.missed_manifests, 0);
}

TEST(view_history_is_newest_first, "the archive reads backwards and renders through the text layer") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  for (int i = 0; i < 30; ++i) session->step_day();
  auto entries_out = view::history(*session, 5);
  CHECK_MSG(entries_out.ok(), entries_out.error().message);
  const std::vector<view::HistoryEntry>& entries = entries_out.value();
  CHECK(!entries.empty());
  CHECK(entries.size() <= static_cast<std::size_t>(5));
  for (std::size_t i = 1; i < entries.size(); ++i) {
    CHECK(entries[i - 1].day >= entries[i].day);
  }
  for (const auto& e : entries) {
    CHECK(!e.text.empty());
    // A template key that reached the reader is a bug, not a headline.
    CHECK(e.text.find("news.") == std::string::npos);
  }
  auto none = view::history(*session, 0);
  auto negative = view::history(*session, -1);
  CHECK(none.ok());
  CHECK(none.value().empty());
  CHECK(negative.ok());
  CHECK(negative.value().empty());
}

TEST(view_decisions_show_what_each_choice_costs, "an unaffordable option is marked, not hidden") {
  const Catalog& catalog = *testing::fixture_catalog("worn_mine");
  auto session = testing::new_session(catalog, "worn_mine");
  // The chain opens with a warning that carries no remedies, then an accident
  // that does. Both are decision cards; only the second has options.
  bool saw_a_warning_without_options = false;
  view::DecisionCard with_options;
  for (int i = 0; i < 60 && with_options.id == 0; ++i) {
    session->step_day();
    auto opened = view::decisions(*session);
    CHECK_MSG(opened.ok(), opened.error().message);
    for (const auto& card : opened.value()) {
      CHECK(card.id != 0);
      CHECK(!card.event_id.empty());
      CHECK(!card.title.empty());
      // A key that reached the reader is a bug, not a headline.
      CHECK(card.title.find("event.") == std::string::npos);
      CHECK(card.body.find("{planet}") == std::string::npos);
      // Every decision must say what it is about. A decision an earlier event
      // opened also carries that event's facts; one a standing condition opened
      // does not, and names its subject instead (decision 0011).
      CHECK_MSG(!card.subject_label.empty(), "decision " + card.event_id + " names no subject");
      if (card.options.empty()) {
        saw_a_warning_without_options = true;
      } else if (with_options.id == 0) {
        with_options = card;
      }
    }
  }
  CHECK(saw_a_warning_without_options);
  CHECK_MSG(with_options.id != 0, "no decision with remedies opened within sixty days");
  CHECK(with_options.deadline_day >= 0);
  CHECK(with_options.days_remaining >= 0);
  CHECK(!with_options.subject_label.empty());
  CHECK(with_options.facility_id != 0);   // the accident is about a specific site
  for (const auto& sentence : with_options.because) CHECK(!sentence.empty());
  for (const auto& option : with_options.options) {
    CHECK(!option.choice_id.empty());
    CHECK(!option.label.empty());
    CHECK(option.label.find("choice.") == std::string::npos);
    CHECK(option.label != option.choice_id);
    const EventDef* def = catalog.find_event(with_options.event_id);
    CHECK(def != nullptr);
    for (const auto& choice : def->choices) {
      if (choice.id != option.choice_id) continue;
      const bool free = choice.cost.empty() && choice.required_reserve_workers == 0 &&
                        choice.required_assigned_workers == 0;
      // A costed option states its cost; a free one states nothing rather than "0".
      CHECK_EQ(option.cost_summary.empty(), free);
    }
  }
}

TEST(view_every_authored_name_resolves, "no display_key reaches a reader as a key") {
  // Content declares a display_key for every definition. A key with no string
  // behind it renders as the key itself, which is how "event.mining_accident"
  // ends up on a decision card; this is the check that catches that before a
  // host does (TDD 15.5).
  const Catalog& catalog = testing::shipped_catalog();
  auto resolves = [](const std::string& key) {
    if (key.empty()) return true;   // a definition may decline to declare one
    return text::lookup(key) != key;
  };
  auto require = [&](const std::string& key, const std::string& what) {
    CHECK_MSG(resolves(key), "no display string for " + what + " key '" + key + "'");
  };
  for (const auto& r : catalog.resources()) require(r.display_key, "resource " + r.id);
  for (const auto& f : catalog.facilities()) {
    require(f.display_key, "facility " + f.id);
    for (const auto& recipe : f.recipes) require(recipe.display_key, "recipe " + recipe.id);
  }
  for (const auto& f : catalog.factions()) require(f.display_key, "faction " + f.id);
  for (const auto& p : catalog.policies()) require(p.display_key, "policy " + p.id);
  for (const auto& e : catalog.events()) {
    require(e.display_key, "event " + e.id);
    for (const auto& c : e.choices) require(c.display_key, "choice " + c.id);
    require(e.news_template_open, "news template of event " + e.id);
    require(e.news_template_resolved, "resolved news template of event " + e.id);
    require(e.news_template_expire, "expiry news template of event " + e.id);
  }
  for (const auto& sc : catalog.scenarios()) {
    require(sc.display_key, "scenario " + sc.id);
    for (const auto& planet : sc.planets) {
      const PlanetDef* def = catalog.find_planet(planet.planet_id);
      CHECK(def != nullptr);
      require(def->display_key, "planet " + planet.planet_id);
    }
  }
}

TEST(view_pins_open_finding_0011, "no event instance records the facts that opened it") {
  // EventInstance::trigger_facts exists, is saved and is hashed, and nothing
  // populates it: a condition trigger passes none, and so does the OpenEvent
  // effect that escalates a chain. The consequence is that a decision card has
  // no recorded provenance to show, which is why the view falls back to naming
  // its subject. This test pins the current behaviour so that closing the
  // finding is a deliberate, visible change rather than a quiet one
  // (docs/decisions/0011-condition-trigger-provenance.md).
  const Catalog& catalog = *testing::fixture_catalog("worn_mine");
  auto session = testing::new_session(catalog, "worn_mine");
  int events_seen = 0;
  for (int i = 0; i < 60; ++i) {
    session->step_day();
    for (const auto& e : session->state().events) {
      events_seen += 1;
      CHECK_MSG(e.trigger_facts.empty(), "finding 0011 appears to be fixed: " + e.event_id +
                                             " now records its trigger facts. Update this test and the "
                                             "decision-card fallback in view_models.cpp.");
    }
    for (const auto& f : session->state().facts) {
      if (f.kind == "event_opened") CHECK(f.causal_parents.empty());
    }
  }
  CHECK(events_seen > 0);
}
