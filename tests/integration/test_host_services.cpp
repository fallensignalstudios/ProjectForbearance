// The two host services an engine needs and that can still be checked here:
// fixed-step time (TDD 5.3) and event-driven view invalidation (TDD 17.3, E3).
#include <algorithm>
#include <cmath>
#include <cstddef>

#include "expansion/host_services.hpp"
#include "harness.hpp"

using namespace expansion;
using host::ChangeSet;
using host::ChangeTracker;
using host::SimSpeed;
using host::TickConfig;
using host::TickScheduler;

namespace {

bool close_to(double a, double b) { return std::fabs(a - b) < 1e-9; }

bool contains(const std::vector<InstanceId>& v, InstanceId id) {
  return std::find(v.begin(), v.end(), id) != v.end();
}

bool contains(const std::vector<std::string>& v, const std::string& id) {
  return std::find(v.begin(), v.end(), id) != v.end();
}

// Frame deltas the scheduler is happy with, so a test measures the accumulator
// rather than the per-frame stall clamp.
int advance_in_frames(TickScheduler& t, double seconds, double frame = 0.1) {
  int steps = 0;
  const int frames = static_cast<int>(std::llround(seconds / frame));
  for (int i = 0; i < frames; ++i) steps += t.advance(frame);
  return steps;
}

}  // namespace

TEST(tick_rate_matches_the_design, "ten seconds is one day at 1x and eight at 8x") {
  TickScheduler t;
  t.set_speed(SimSpeed::X1);
  CHECK_EQ(advance_in_frames(t, 10.0), 1);
  CHECK(close_to(t.accumulated_seconds(), 0.0));

  // At 4x a day is due every 2.5 s, so ten seconds is four days -- which is
  // also exactly the per-frame cap, spread over frames here.
  TickScheduler four;
  four.set_speed(SimSpeed::X4);
  CHECK_EQ(advance_in_frames(four, 10.0), 4);

  TickScheduler eight;
  eight.set_speed(SimSpeed::X8);
  CHECK_EQ(advance_in_frames(eight, 10.0), 8);
}

TEST(tick_retains_the_remainder, "a partial day carries across frames instead of being lost") {
  TickScheduler t;
  t.set_speed(SimSpeed::X1);
  for (int i = 0; i < 9; ++i) CHECK_EQ(t.advance(0.1), 0);   // 0.9 s
  CHECK(t.progress_to_next_day() > 0.0);
  CHECK(t.progress_to_next_day() < 0.1);
  // Nine tenths of a second banked plus nine more seconds is one whole day and
  // nothing over: the remainder was neither dropped nor double-counted.
  CHECK_EQ(advance_in_frames(t, 9.0), 0);
  CHECK_EQ(t.advance(0.1), 1);
  CHECK(close_to(t.accumulated_seconds(), 0.0));
}

TEST(tick_never_exceeds_the_frame_cap, "one advance call never asks for more days than the cap") {
  TickConfig config;
  config.max_steps_per_frame = 2;
  config.max_frame_seconds = 60.0;   // let the stall clamp out of the way
  TickScheduler t(config);
  t.set_speed(SimSpeed::X8);         // 1.25 s per day
  CHECK_EQ(t.advance(10.0), 2);
  // The retained excess is capped at one further day, so a long frame resumes
  // where it stopped rather than owing six more days.
  CHECK(t.accumulated_seconds() <= 1.25 + 1e-9);
  CHECK_EQ(t.advance(0.0), 0);
}

TEST(tick_clamps_a_stalled_frame, "a single huge delta is a stall, not sixty banked days") {
  TickScheduler t;                   // max_frame_seconds 0.5, 10 s per day at 1x
  t.set_speed(SimSpeed::X1);
  CHECK_EQ(t.advance(600.0), 0);
  CHECK(close_to(t.accumulated_seconds(), 0.5));
  CHECK(close_to(t.progress_to_next_day(), 0.05));
}

TEST(tick_stops_when_it_should, "paused, suspended and decision-held all yield no days") {
  TickScheduler t;
  CHECK(!t.running());               // a fresh scheduler starts paused
  CHECK_EQ(advance_in_frames(t, 30.0), 0);

  t.set_speed(SimSpeed::X1);
  CHECK(t.running());
  t.suspend();
  CHECK(!t.running());
  CHECK_EQ(advance_in_frames(t, 30.0), 0);
  t.resume();
  // No catch-up debt: the suspended half minute is gone, so the next day still
  // takes a full ten seconds.
  CHECK(close_to(t.accumulated_seconds(), 0.0));
  CHECK_EQ(advance_in_frames(t, 9.0), 0);
  CHECK_EQ(advance_in_frames(t, 1.0), 1);

  t.hold_for_decision();
  CHECK(t.held_for_decision());
  CHECK(!t.running());
  CHECK_EQ(advance_in_frames(t, 30.0), 0);
  t.release_decision_hold();
  CHECK(t.running());
  CHECK_EQ(advance_in_frames(t, 10.0), 1);

  // Pausing discards the partial day rather than handing it back on resume.
  CHECK_EQ(advance_in_frames(t, 5.0), 0);
  CHECK(t.accumulated_seconds() > 0.0);
  t.set_speed(SimSpeed::Paused);
  CHECK(close_to(t.accumulated_seconds(), 0.0));
  CHECK(close_to(t.progress_to_next_day(), 0.0));
  t.set_speed(SimSpeed::X1);
  CHECK_EQ(advance_in_frames(t, 9.0), 0);

  t.reset();
  CHECK(close_to(t.accumulated_seconds(), 0.0));
}

TEST(tick_ignores_nonsense_deltas, "a negative or non-finite delta advances nothing") {
  TickScheduler t;
  t.set_speed(SimSpeed::X1);
  CHECK_EQ(t.advance(-5.0), 0);
  CHECK_EQ(t.advance(0.0), 0);
  CHECK_EQ(t.advance(std::nan("")), 0);
  CHECK(close_to(t.accumulated_seconds(), 0.0));
}

TEST(changes_start_with_everything, "a freshly bound interface is told to draw all of it") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  ChangeTracker tracker;
  ChangeSet first = tracker.publish(*session);
  CHECK(first.first_publish);
  CHECK(first.sector);
  CHECK(first.freight);
  CHECK(first.decisions);
  CHECK(first.history);
  CHECK_EQ(first.planets.size(), session->state().planets.size());
  CHECK_EQ(first.facilities.size(), session->state().facilities.size());
  CHECK_EQ(first.revision, session->state().revision);

  // Nothing has happened since, so nothing is redrawn. This is the property the
  // whole design rests on: no per-frame rebinding.
  ChangeSet again = tracker.publish(*session);
  CHECK(!again.first_publish);
  CHECK(!again.any());
}

TEST(changes_are_local_to_one_facility, "a priority change repaints one card, not thirteen") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  ChangeTracker tracker;
  tracker.publish(*session);
  CHECK(session->state().facilities.size() > 3);

  const InstanceId mine = testing::find_facility(*session, "homeworld", "extraction_iron");
  Command c;
  c.id = "priority";
  c.kind = CommandKind::SetProductionPriority;
  c.facility_id = mine;
  c.priority_band = 42;
  CHECK(session->apply_command(c).accepted);

  ChangeSet set = tracker.publish(*session);
  CHECK_EQ(set.facilities.size(), static_cast<std::size_t>(1));
  CHECK(contains(set.facilities, mine));
  // The revision moved, so the sector header (which shows it) changed; no
  // planet inventory, freight leg, decision or archive entry did.
  CHECK(set.sector);
  CHECK(set.planets.empty());
  CHECK(!set.freight);
  CHECK(!set.decisions);
  CHECK(!set.history);
}

TEST(changes_follow_a_day, "a resolved day marks the planet, the producers and the archive") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  ChangeTracker tracker;
  tracker.publish(*session);

  session->step_day();
  ChangeSet set = tracker.publish(*session);
  CHECK(set.sector);
  CHECK(set.history);            // the day produced ledger and fact material
  CHECK(contains(set.planets, std::string("homeworld")));
  // Every facility that resolved carries a fresh explanation, so its card moved.
  const InstanceId mine = testing::find_facility(*session, "homeworld", "extraction_iron");
  CHECK(contains(set.facilities, mine));
  CHECK_EQ(set.day, session->state().day);
  CHECK_EQ(set.revision, session->state().revision);
}

TEST(changes_report_a_removed_facility, "a cancelled job is reported so its card can go") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  ChangeTracker tracker;

  Command start;
  start.id = "start";
  start.kind = CommandKind::StartConstruction;
  start.planet_id = "homeworld";
  start.content_id = "public_clinic";
  CHECK(session->apply_command(start).accepted);
  const InstanceId job = session->state().facilities.back().id;
  tracker.publish(*session);

  Command cancel;
  cancel.id = "cancel";
  cancel.kind = CommandKind::CancelConstruction;
  cancel.facility_id = job;
  CHECK(session->apply_command(cancel).accepted);
  CHECK(session->state().find_facility(job) == nullptr);

  ChangeSet set = tracker.publish(*session);
  CHECK(contains(set.facilities, job));
  // The refund landed in the planet's stores, so the planet inspector moved too.
  CHECK(contains(set.planets, std::string("homeworld")));

  // And once it is gone it stops being reported.
  ChangeSet quiet = tracker.publish(*session);
  CHECK(!contains(quiet.facilities, job));
  CHECK(!quiet.any());
}

TEST(changes_can_be_forgotten, "forget() makes the next publish a full rebind") {
  const Catalog& catalog = testing::shipped_catalog();
  auto session = testing::new_session(catalog, "first_dependency");
  ChangeTracker tracker;
  tracker.publish(*session);
  CHECK(!tracker.publish(*session).any());
  tracker.forget();
  ChangeSet set = tracker.publish(*session);
  CHECK(set.first_publish);
  CHECK_EQ(set.facilities.size(), session->state().facilities.size());
}

TEST(tick_does_not_drift, "the same elapsed time yields the same days at any frame rate") {
  // The accumulator is integer microseconds precisely so this holds: a host
  // running at 30, 60 or 144 fps reaches the same day at the same second, and
  // thousands of frames of summing neither lose nor invent a day.
  auto days_over = [](double frame, double seconds) {
    TickScheduler t;
    t.set_speed(SimSpeed::X1);
    int steps = 0;
    const int frames = static_cast<int>(std::llround(seconds / frame));
    for (int i = 0; i < frames; ++i) steps += t.advance(frame);
    return steps;
  };
  // Measured mid-day, because whether the tenth day lands on frame 6000 or 6001
  // of a 100.000 s run is not a meaningful question to ask of a float delta.
  for (double frame : {0.1, 1.0 / 30.0, 1.0 / 60.0, 1.0 / 72.0, 1.0 / 144.0}) {
    CHECK_EQ(days_over(frame, 105.0), 10);
    CHECK_EQ(days_over(frame, 305.0), 30);
  }

  // Where the frame period is a whole number of microseconds there is no
  // conversion error at all, so the boundary is exact even after 8000 frames.
  CHECK_EQ(days_over(0.0125, 100.0), 10);
  CHECK_EQ(days_over(0.1, 100.0), 10);
}
