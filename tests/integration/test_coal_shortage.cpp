// The deliberately broken Coal-supply fixture the technical design names as the
// step immediately after the neutral calibration (Section 21.2). Its purpose is
// not that the economy fails, but that the failure is legible: every stage names
// the cause the player can act on.
#include "expansion/read_models.hpp"
#include "expansion/text.hpp"
#include "harness.hpp"

using namespace expansion;

namespace {

void idle(Session& session, const char* recipe, bool flag, const std::string& id) {
  Command c;
  c.id = id;
  c.kind = CommandKind::SetFacilityIdle;
  c.facility_id = testing::find_facility(session, "homeworld", recipe);
  c.flag = flag;
  CHECK(session.apply_command(c).accepted);
}

}  // namespace

TEST(coal_shortage_is_explainable, "a broken Coal supply names Coal, then power, and never 'efficiency'") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  const InstanceId generator = testing::find_facility(*session, "homeworld", "thermal_power");
  const InstanceId foundry = testing::find_facility(*session, "homeworld", "foundry_steel");

  // Stop mining Coal. Nothing else changes.
  idle(*session, "extraction_coal", true, "stop_coal");

  // While the stockpile lasts, the generator runs at full throughput.
  session->step_day();
  const FacilityState* gen_early = session->state().find_facility(generator);
  CHECK_EQ(gen_early->last_explanation.actual_throughput_bp, kBpOne);
  CHECK_EQ(gen_early->last_explanation.primary_reason, std::string("full_throughput"));

  // Run until the generator is first limited by its input.
  Day starved_on = -1;
  for (int i = 0; i < 40 && starved_on < 0; ++i) {
    session->step_day();
    const FacilityState* g = session->state().find_facility(generator);
    if (g->last_explanation.actual_throughput_bp < kBpOne) starved_on = session->state().day;
  }
  CHECK_MSG(starved_on > 0, "the generator should eventually run out of Coal");

  // The generator's own explanation names the missing material, by resource.
  const FacilityState* gen = session->state().find_facility(generator);
  const FacilityExplanation& gx = gen->last_explanation;
  CHECK_EQ(gx.primary_reason, std::string("missing_input"));
  CHECK(!gx.missing_inputs.empty());
  CHECK_EQ(gx.missing_inputs.front(), catalog.coal());
  // Staffing, health, fatigue and condition are all full, so none of them is
  // blamed. A material shortage is never reported as a labour or wear problem.
  CHECK_EQ(gx.staffing_bp, kBpOne);
  CHECK_EQ(gx.condition_bp, kBpOne);
  CHECK(!gx.labour_limited);

  // Downstream, the loss shows up as a power shortfall, not as a second Coal
  // story and not as an unexplained drop.
  for (int i = 0; i < 4; ++i) session->step_day();
  const PlanetState& home = session->state().planet("homeworld");
  CHECK(home.last_day.power.generated < 110000);
  // Residential demand is still protected first while solar covers it.
  CHECK_EQ(home.last_day.power.residential_served, home.last_day.power.residential_demand);
  const FacilityState* f = session->state().find_facility(foundry);
  CHECK(f->last_explanation.actual_throughput_bp < kBpOne);
  const std::string reason = f->last_explanation.primary_reason;
  CHECK_MSG(reason == "power_shortfall" || reason == "missing_input",
            "the foundry's loss should be power or material, not a vague efficiency figure; got " + reason);

  // A shortage was recorded as a real missed operation, with news to match.
  bool coal_shortage = false;
  for (const auto& n : session->state().news) {
    if (n.template_key != "news.shortage_opened") continue;
    for (const auto& t : n.text_args) {
      if (t == "coal") coal_shortage = true;
    }
  }
  CHECK(coal_shortage);

  // The overview leads with the cause, and the facility view spells it out.
  const auto concerns = read::top_concerns(*session, 3);
  CHECK(!concerns.empty());
  const std::string card = read::facility_inspector(*session, generator);
  CHECK(card.find("short of an input material") != std::string::npos);
  CHECK(card.find("coal") != std::string::npos);
}

TEST(coal_shortage_recovers_while_stock_remains, "restarting the mine before the stockpile empties recovers fully") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  const InstanceId generator = testing::find_facility(*session, "homeworld", "thermal_power");
  idle(*session, "extraction_coal", true, "stop_coal");

  // Act while Coal is still short but not gone. This is the breathing room the
  // design asks competent planning to buy.
  while (testing::stock(*session, "homeworld", "coal") > 20000) session->step_day();
  CHECK(testing::stock(*session, "homeworld", "coal") > 0);
  idle(*session, "extraction_coal", false, "restart_coal");

  for (int i = 0; i < 15; ++i) session->step_day();
  const FacilityState* gen = session->state().find_facility(generator);
  CHECK_EQ(gen->last_explanation.actual_throughput_bp, kBpOne);
  CHECK_EQ(gen->last_explanation.primary_reason, std::string("full_throughput"));
  const PlanetState& home = session->state().planet("homeworld");
  CHECK_EQ(home.last_day.power.generated, 110000);
  CHECK_EQ(home.last_day.food_fulfilment_bp, kBpOne);
  CHECK_EQ(home.last_day.water_fulfilment_bp, kBpOne);
  CHECK_EQ(session->state().lifecycle, SessionLifecycle::Running);

  // Competent planning buys breathing room rather than causing the event system
  // to invent new punishment (Section 2.3): acting in time means the shortage
  // never opens at all, so there is nothing to report and nothing to recover from.
  for (const auto& n : session->state().news) {
    for (const auto& t : n.text_args) {
      CHECK_MSG(!(n.template_key == "news.shortage_opened" && t == "coal"),
                "acting before the stockpile emptied should not have opened a Coal shortage");
    }
  }
  for (const auto& e : session->state().events) {
    CHECK_MSG(e.resolution != EventResolution::Open, "no new pressure event should follow a clean recovery");
  }
}

TEST(coal_exhaustion_is_a_dead_end_on_homeworld,
     "with Coal at zero, Homeworld cannot restart: a finding against the seed, not a rule") {
  // Section 8.3 intends solar to leave enough headroom for partial recovery after
  // a fuel shortage, and Section 21.1 names an unrecoverable bootstrap loop as a
  // risk to control. On Homeworld the shipped seed does not meet that intent: the
  // Hub's 20 solar exactly equals a thousand residents' protected demand, so at
  // zero Coal there is nothing left to run the mine that would produce Coal.
  //
  // This test records the behaviour as it is, so the contradiction stays visible
  // until approval A04 revises the seed. See
  // docs/decisions/0009-coal-exhaustion-is-unrecoverable.md.
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  const InstanceId generator = testing::find_facility(*session, "homeworld", "thermal_power");
  const InstanceId mine = testing::find_facility(*session, "homeworld", "extraction_coal");
  idle(*session, "extraction_coal", true, "stop_coal");
  while (testing::stock(*session, "homeworld", "coal") > 0) session->step_day();

  idle(*session, "extraction_coal", false, "restart_coal");
  for (int i = 0; i < 15; ++i) session->step_day();

  const PlanetState& home = session->state().planet("homeworld");
  // Solar still covers residential demand exactly, and nothing is left over.
  CHECK_EQ(home.last_day.power.generated, 20000);
  CHECK_EQ(home.last_day.power.residential_served, home.last_day.power.residential_demand);
  CHECK_EQ(home.last_day.power.facility_granted, 0);
  // The mine cannot run for want of the power its own output would generate.
  const FacilityState* m = session->state().find_facility(mine);
  CHECK_EQ(m->last_explanation.actual_throughput_bp, 0);
  CHECK_EQ(m->last_explanation.primary_reason, std::string("power_shortfall"));
  CHECK_EQ(session->state().find_facility(generator)->last_explanation.primary_reason,
           std::string("missing_input"));
  // The loop is at least legible: the player is told it is power, and that the
  // generator wants Coal. Nothing here is reported as "efficiency".
  CHECK(!m->last_explanation.labour_limited);
}

TEST(solar_floor_permits_colonial_recovery, "the Hub's solar keeps a colony's residential power whole") {
  // Section 8.3 intends solar to leave room for partial recovery after a fuel
  // shortage. On the colony that holds: twenty solar against two of residential
  // demand. On Homeworld it does not, and the figures say so plainly.
  const Catalog& catalog = testing::shipped_catalog();
  const FacilityDef& hub = catalog.facility("colony_hub");
  CHECK_EQ(hub.passive.power_output, 20000);
  auto session = testing::new_session(catalog, "first_dependency");
  session->step_day();
  const PlanetState& home = session->state().planet("homeworld");
  // Homeworld's thousand residents need the whole solar supply themselves, so a
  // total Coal failure there leaves nothing for facilities.
  CHECK_EQ(home.last_day.power.residential_demand, 20000);
  CHECK_EQ(hub.passive.power_output, home.last_day.power.residential_demand);
  // A hundred colonists need a tenth of it, which is the headroom the design
  // relies on.
  const Milli colony_demand = 100 * catalog.needs().power_per_resident;
  CHECK(colony_demand * 5 < hub.passive.power_output);
}
