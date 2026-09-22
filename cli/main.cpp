// ExpansionCLI: the headless scenario host, diagnostics and replay comparison
// (TDD 3.2). It contains no economy of its own: every change goes through the
// same command API the interface uses.
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "commands.hpp"
#include "expansion/derived.hpp"
#include "expansion/read_models.hpp"
#include "expansion/replay.hpp"
#include "expansion/save_file.hpp"
#include "expansion/session.hpp"
#include "expansion/state_codec.hpp"
#include "expansion/text.hpp"

namespace {

using namespace expansion;

constexpr const char* kBuildId = "expansion-cli/0.1.0";

struct Options {
  std::map<std::string, std::string> values;
  std::string get(const std::string& key, const std::string& fallback = "") const {
    auto it = values.find(key);
    return it == values.end() ? fallback : it->second;
  }
  bool has(const std::string& key) const { return values.count(key) != 0; }
  long long number(const std::string& key, long long fallback) const {
    auto it = values.find(key);
    if (it == values.end()) return fallback;
    return std::stoll(it->second);
  }
};

Options parse_options(int argc, char** argv, int from) {
  Options o;
  for (int i = from; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg.rfind("--", 0) != 0) throw SimError("unexpected argument '" + arg + "'");
    arg = arg.substr(2);
    const std::size_t eq = arg.find('=');
    if (eq == std::string::npos) {
      o.values[arg] = "true";
    } else {
      o.values[arg.substr(0, eq)] = arg.substr(eq + 1);
    }
  }
  return o;
}

void print_usage() {
  std::cout << R"(Sovereign Call: Expansion -- headless simulation host

  expansion validate  --content=DIR
      Load and validate the content catalog, then run the neutral one-day
      preflight from Section 9.3 of the technical design.

  expansion run       --content=DIR [--scenario=ID] [--faction=ID] [--days=N]
                      [--script=FILE] [--save=FILE] [--journal=FILE] [--quiet]
      Create a session, apply a command script, and step the simulation.

  expansion show      --content=DIR --save=FILE [--view=overview|network|planet|
                      facility|freight|decisions|history|ledger|outcome]
                      [--planet=ID] [--facility=N] [--day=N] [--search=TEXT]

  expansion forecast  --content=DIR --save=FILE [--days=N]
      Run an arrival-aware forecast on a clone. The live state is untouched.

  expansion replay    --content=DIR --journal=FILE
      Re-run an accepted command journal and compare canonical day hashes.

  expansion verify    --content=DIR --journal=FILE
      Replay twice in this build and report the first differing day.

  expansion hash      --content=DIR --save=FILE
      Print the catalog hash and the canonical economic state hash.
)";
}

Catalog load_catalog(const Options& o) {
  const std::string dir = o.get("content", "content");
  return Catalog::load_from_directory(dir);
}

// The neutral one-day calibration from TDD 9.3, run through the real resolver.
int command_validate(const Options& o) {
  Catalog catalog = load_catalog(o);
  std::cout << "catalog loaded: " << catalog.resources().size() << " resources, " << catalog.facilities().size()
            << " facility types, " << catalog.events().size() << " events, " << catalog.scenarios().size()
            << " scenarios\n";
  std::cout << "catalog sha256: " << catalog.hash() << "\n";

  const std::string scenario_id = o.get("scenario", catalog.scenarios().front().id);
  const ScenarioDef& sc = catalog.scenario(scenario_id);
  // The calibration fixture deliberately has no faction, policy, event,
  // transfer or construction effect (TDD 9.3).
  const FactionDef* fixture = catalog.fixture_faction();
  if (fixture == nullptr) throw SimError("this catalog has no neutral verification profile to calibrate against");
  auto session = Session::create(catalog, scenario_id, fixture->id, 0);
  const std::string home = sc.freight.origin_planet;
  const PlanetState& before = session->state().planet(home);
  std::vector<Milli> opening = before.inventory.on_hand;
  session->step_day();
  const PlanetState& after = session->state().planet(home);

  std::cout << "\nneutral one-day calibration on " << home << "\n";
  int failures = 0;
  auto check = [&](const std::string& label, std::int64_t actual, std::int64_t expected, bool milli) {
    const bool ok = actual == expected;
    if (!ok) ++failures;
    std::cout << "  " << (ok ? "ok   " : "FAIL ") << label << ": "
              << (milli ? format_milli(actual) : std::to_string(actual)) << " (expected "
              << (milli ? format_milli(expected) : std::to_string(expected)) << ")\n";
  };
  auto net = [&](const char* id) {
    const int idx = catalog.resource_index(id);
    return after.inventory.on_hand[static_cast<std::size_t>(idx)] - opening[static_cast<std::size_t>(idx)];
  };
  check("workers assigned", after.last_day.workers_assigned + after.last_day.workers_crew, 410, false);
  check("workers in reserve", after.last_day.workers_reserve, 90, false);
  check("food net", net("food"), 60000, true);
  check("water net", net("water"), 66000, true);
  check("iron ore net", net("iron_ore"), 18000, true);
  check("coal net", net("coal"), 2000, true);
  check("steel net", net("steel"), 12000, true);
  check("machinery net", net("machinery"), 3280, true);
  check("fuel net", net("fuel"), 12000, true);
  check("power generated", after.last_day.power.generated, 110000, true);
  check("power used", after.last_day.power.used, 96000, true);
  check("power spare", after.last_day.power.unused, 14000, true);
  check("housing capacity", after.last_day.housing_capacity, 1350, false);
  check("clinic capacity", after.last_day.clinic_capacity, 1200, false);

  std::cout << (failures == 0 ? "\npreflight passed\n" : "\npreflight FAILED\n");
  std::cout << "This preflight checks catalog references and the neutral arithmetic only. It is not a balance,\n"
               "playtest or performance result.\n";
  return failures == 0 ? 0 : 1;
}

int command_run(const Options& o) {
  Catalog catalog = load_catalog(o);
  const std::string scenario_id = o.get("scenario", catalog.scenarios().front().id);
  const std::string faction_id = o.get("faction", catalog.playable_factions().front()->id);
  const auto seed = static_cast<std::uint64_t>(o.number("seed", 0));
  const bool quiet = o.has("quiet");

  auto session = Session::create(catalog, scenario_id, faction_id, seed);
  JournalRecorder recorder(*session, catalog);

  std::vector<cli::ScriptStep> script;
  if (o.has("script")) script = cli::parse_script(json::read_file(o.get("script")), catalog);

  const Day target_day = static_cast<Day>(o.number("days", 0));
  std::size_t next = 0;
  int rejected = 0;
  const auto started = std::chrono::steady_clock::now();

  auto run_steps = [&](Day count) {
    for (Day i = 0; i < count; ++i) {
      if (session->state().lifecycle != SessionLifecycle::Running) return;
      DayResult r = recorder.step();
      if (!quiet) {
        for (InstanceId id : r.new_news) {
          for (const auto& n : session->state().news) {
            if (n.id == id) std::cout << "  " << text::news_line(n) << "\n";
          }
        }
      }
    }
  };

  while (next < script.size()) {
    const cli::ScriptStep& step = script[next++];
    switch (step.kind) {
      case cli::ScriptStep::Kind::Note:
        if (!quiet) std::cout << "note: " << step.text << "\n";
        break;
      case cli::ScriptStep::Kind::Step:
        run_steps(step.days);
        break;
      case cli::ScriptStep::Kind::UntilDay:
        run_steps(step.days - session->state().day);
        break;
      case cli::ScriptStep::Kind::Expect:
        if (!quiet) std::cout << "expect: " << step.text << "\n";
        break;
      case cli::ScriptStep::Kind::Command: {
        Command command = step.command;
        if (!step.facility_ref.empty()) {
          command.facility_id = cli::resolve_reference(*session, step.facility_ref, step.line_number);
        }
        if (!step.to_facility_ref.empty()) {
          command.to_facility_id = cli::resolve_reference(*session, step.to_facility_ref, step.line_number);
        }
        if (!step.event_ref.empty()) {
          command.event_instance_id = cli::resolve_reference(*session, step.event_ref, step.line_number);
        }
        CommandResult r = recorder.apply(command);
        if (!r.accepted) {
          ++rejected;
          std::cout << "  rejected (line " << step.line_number << ", " << command_kind_id(command.kind)
                    << "): " << r.reason << (r.detail.empty() ? "" : " -- " + r.detail) << "\n";
        } else if (!quiet && !r.detail.empty()) {
          std::cout << "  day " << session->state().day << " " << command_kind_id(command.kind) << ": " << r.detail
                    << "\n";
        }
        break;
      }
    }
  }
  if (target_day > session->state().day) run_steps(target_day - session->state().day);

  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                             started);
  std::cout << "\n" << read::civilization_overview(*session);
  if (session->state().lifecycle != SessionLifecycle::Running) {
    std::cout << "\n" << read::outcome_report(*session);
  }
  std::cout << "\nran to day " << session->state().day << " in " << elapsed.count() << " ms; " << rejected
            << " commands rejected\n";
  std::cout << "state hash " << session->canonical_hash() << "\n";

  if (o.has("save")) {
    write_save_slot(o.get("save"), encode_save(session->state(), catalog, kBuildId));
    std::cout << "saved to " << o.get("save") << "\n";
  }
  if (o.has("journal")) {
    json::write_file_atomic(o.get("journal"), encode_journal(recorder.journal()));
    std::cout << "journal written to " << o.get("journal") << "\n";
  }
  return 0;
}

std::unique_ptr<Session> load_session(const Options& o, const Catalog& catalog) {
  SaveHeader header;
  SessionState state = decode_save(json::read_file(o.get("save")), catalog, &header);
  return Session::from_state(catalog, std::move(state));
}

int command_show(const Options& o) {
  Catalog catalog = load_catalog(o);
  auto session = load_session(o, catalog);
  const std::string view = o.get("view", "overview");
  if (view == "overview") {
    std::cout << read::civilization_overview(*session);
  } else if (view == "network") {
    std::cout << read::network_map(*session);
  } else if (view == "planet") {
    std::cout << read::planet_inspector(*session, o.get("planet", session->state().planets.front().planet_id));
  } else if (view == "facility") {
    std::cout << read::facility_inspector(*session, static_cast<InstanceId>(o.number("facility", 1)));
  } else if (view == "freight") {
    std::cout << read::freight_inspector(*session);
  } else if (view == "decisions") {
    std::cout << read::decision_drawer(*session);
  } else if (view == "history") {
    std::cout << read::history_drawer(*session, static_cast<int>(o.number("limit", 40)), o.get("search"));
  } else if (view == "ledger") {
    std::cout << read::ledger_view(*session, static_cast<Day>(o.number("day", session->state().day)));
  } else if (view == "outcome") {
    std::cout << read::outcome_report(*session);
  } else {
    throw SimError("unknown view '" + view + "'");
  }
  return 0;
}

int command_forecast(const Options& o) {
  Catalog catalog = load_catalog(o);
  auto session = load_session(o, catalog);
  const std::string before = session->canonical_hash();
  ForecastResult f = session->forecast({}, static_cast<int>(o.number("days", 30)));
  const std::string after = session->canonical_hash();
  std::cout << "FORECAST from day " << f.from_day << " over " << f.days_run << " days (snapshot revision "
            << f.snapshot_revision << ")\n";
  for (const auto& a : f.assumptions) std::cout << "  assumption: " << a << "\n";
  if (f.first_missed_need_day < 0) {
    std::cout << "  no civilian need is missed within the horizon\n";
  } else {
    std::cout << "  first missed need: " << f.first_missed_resource << " at " << f.first_missed_planet << " on day "
              << f.first_missed_need_day << "\n";
  }
  for (const auto& [planet, stock] : f.closing_stock) {
    std::cout << "  projected closing stock at " << planet << ":";
    for (int r = 0; r < catalog.resource_count(); ++r) {
      std::cout << "  " << catalog.resource(r).id << " " << format_milli(stock[static_cast<std::size_t>(r)]);
    }
    std::cout << "\n";
  }
  std::cout << "  live state unchanged: " << (before == after ? "yes" : "NO") << "\n";
  return before == after ? 0 : 1;
}

int command_replay(const Options& o) {
  Catalog catalog = load_catalog(o);
  CommandJournal journal = decode_journal(json::read_file(o.get("journal")));
  ReplayResult r = replay_journal(catalog, journal);
  std::cout << "replayed " << r.commands_applied << " accepted commands over " << r.day_hashes.size() << " days\n";
  if (r.difference.differs) {
    std::cout << "MISMATCH on day " << r.difference.day << "\n  expected " << r.difference.expected_hash
              << "\n  actual   " << r.difference.actual_hash << "\n";
    return 1;
  }
  std::cout << "every recorded day hash matched\nfinal hash " << r.final_hash << "\n";
  return 0;
}

int command_verify(const Options& o) {
  Catalog catalog = load_catalog(o);
  CommandJournal journal = decode_journal(json::read_file(o.get("journal")));
  ReplayResult a = replay_journal(catalog, journal);
  ReplayResult b = replay_journal(catalog, journal);
  if (a.final_hash != b.final_hash) {
    std::cout << "two replays in this build disagree\n";
    return 1;
  }
  std::cout << "two replays in this build agree: " << a.final_hash << "\n";
  if (a.difference.differs) {
    std::cout << "but both disagree with the recorded journal on day " << a.difference.day << "\n";
    return 1;
  }
  std::cout << "and both match the recorded journal\n";
  std::cout << "This is a same-build determinism check. Cross-compiler and cross-host comparison is a separate,\n"
               "unperformed test (TDD 18.2 T22).\n";
  return 0;
}

int command_hash(const Options& o) {
  Catalog catalog = load_catalog(o);
  std::cout << "catalog " << catalog.hash() << "\n";
  if (o.has("save")) {
    auto session = load_session(o, catalog);
    std::cout << "state   " << session->canonical_hash() << "\n";
    std::cout << "day     " << session->state().day << "\n";
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    print_usage();
    return 2;
  }
  const std::string verb = argv[1];
  if (verb == "help" || verb == "--help" || verb == "-h") {
    print_usage();
    return 0;
  }
  try {
    Options o = parse_options(argc, argv, 2);
    if (verb == "validate") return command_validate(o);
    if (verb == "run") return command_run(o);
    if (verb == "show") return command_show(o);
    if (verb == "forecast") return command_forecast(o);
    if (verb == "replay") return command_replay(o);
    if (verb == "verify") return command_verify(o);
    if (verb == "hash") return command_hash(o);
    std::cerr << "unknown command '" << verb << "'\n\n";
    print_usage();
    return 2;
  } catch (const SimError& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
