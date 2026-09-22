// T29 and the packaged-build gate (TDD 18.2, 18.4): recorded runs that must keep
// producing the same committed days, including one Complete run per faction.
#include "expansion/host_files.hpp"
#include "expansion/read_models.hpp"
#include "expansion/replay.hpp"
#include "harness.hpp"

using namespace expansion;

namespace {

struct Golden {
  const char* file;
  const char* faction;
  SessionLifecycle expected;
  bool relief_expected;
};

ReplayResult replay_golden(const Catalog& catalog, const Golden& g, SessionLifecycle* outcome,
                           bool* relief_used) {
  const std::string path = std::string(EXPANSION_REPLAY_DIR) + "/" + g.file;
  CommandJournal journal = decode_journal(host::read_file(path));
  CHECK_MSG(journal.catalog_hash == catalog.hash(),
            std::string(g.file) + " was recorded against a different catalog; re-record it deliberately");
  CHECK_MSG(journal.faction_id == std::string(g.faction), std::string(g.file) + " names an unexpected faction");
  ReplayResult r = replay_journal(catalog, journal);

  // Re-run the journal to read the final outcome from a live session.
  auto session = Session::create(catalog, journal.scenario_id, journal.faction_id, journal.seed);
  for (const auto& e : journal.entries) {
    if (e.is_step) {
      session->step_day();
    } else {
      session->apply_command(e.command, std::nullopt);
    }
  }
  *outcome = session->state().lifecycle;
  *relief_used = false;
  for (const auto& p : session->state().planets) {
    if (p.relief_used) *relief_used = true;
  }
  return r;
}

void check_golden(const Golden& g) {
  const Catalog& catalog = testing::shipped_catalog();
  SessionLifecycle outcome = SessionLifecycle::Running;
  bool relief = false;
  ReplayResult r = replay_golden(catalog, g, &outcome, &relief);
  CHECK_MSG(!r.difference.differs,
            std::string(g.file) + " diverged on day " + to_decimal_string(r.difference.day) +
                ": expected " + r.difference.expected_hash + ", got " + r.difference.actual_hash);
  CHECK_MSG(r.commands_rejected == 0, std::string(g.file) + " contains a command the resolver now rejects");
  CHECK_EQ(outcome, g.expected);
  CHECK_EQ(relief, g.relief_expected);
}

}  // namespace

TEST(t29_complete_run_dominion, "T29: a recorded Complete run exists for Dominion on the shipped catalog") {
  check_golden({"first_dependency_dominion.journal.json", "dominion", SessionLifecycle::Complete, false});
}

TEST(t29_complete_run_reformation, "T29: a recorded Complete run exists for Reformation on the shipped catalog") {
  check_golden({"first_dependency_reformation.journal.json", "reformation", SessionLifecycle::Complete, false});
}

TEST(golden_compromised_run, "a recorded run that declines the mandate ends Compromised, not Failed") {
  check_golden({"first_dependency_compromised.journal.json", "dominion", SessionLifecycle::Compromised, false});
}

TEST(golden_relief_and_loss, "a recorded run reproduces the relief contract and the explicit loss") {
  check_golden({"first_dependency_collapse.journal.json", "dominion", SessionLifecycle::Failed, true});
}

TEST(golden_no_developer_grants, "no recorded run relies on a grant outside the authored content") {
  const Catalog& catalog = testing::shipped_catalog();
  for (const char* file : {"first_dependency_dominion.journal.json", "first_dependency_reformation.journal.json"}) {
    CommandJournal journal = decode_journal(host::read_file(std::string(EXPANSION_REPLAY_DIR) + "/" + file));
    auto session = Session::create(catalog, journal.scenario_id, journal.faction_id, journal.seed);
    for (const auto& e : journal.entries) {
      if (e.is_step) {
        session->step_day();
      } else {
        CHECK(session->apply_command(e.command, std::nullopt).accepted);
      }
    }
    // The only external inflow a scenario permits is the relief contract, and
    // neither Complete run uses one.
    for (const auto& t : session->state().ledger) {
      CHECK_MSG(t.from_account != std::string("external"),
                std::string(file) + " received an external grant");
    }
  }
}
