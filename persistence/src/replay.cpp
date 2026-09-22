#include "expansion/replay.hpp"

#include "expansion/json.hpp"
#include "expansion/state_codec.hpp"

namespace expansion {
namespace {

json::Value encode_resource_map(const std::map<int, Milli>& m, const std::string& kind) {
  json::Value out = json::Value::object({});
  for (const auto& [idx, qty] : m) out.set(kind + std::to_string(idx), json::dec(qty));
  return out;
}

json::Value encode_command(const Command& c) {
  json::Value e = json::Value::object({});
  e.set("id", json::Value::string(c.id));
  e.set("kind", json::Value::string(command_kind_id(c.kind)));
  if (!c.planet_id.empty()) e.set("planet_id", json::Value::string(c.planet_id));
  if (!c.content_id.empty()) e.set("content_id", json::Value::string(c.content_id));
  if (!c.recipe_id.empty()) e.set("recipe_id", json::Value::string(c.recipe_id));
  if (c.facility_id != 0) e.set("facility_id", json::dec_u(c.facility_id));
  if (c.to_facility_id != 0) e.set("to_facility_id", json::dec_u(c.to_facility_id));
  if (c.event_instance_id != 0) e.set("event_instance_id", json::dec_u(c.event_instance_id));
  if (c.count != 0) e.set("count", json::Value::integer(c.count));
  if (c.priority_band != 0) e.set("priority_band", json::Value::integer(c.priority_band));
  if (c.flag) e.set("flag", json::Value::boolean(true));
  if (!c.resources.empty()) e.set("resources", encode_resource_map(c.resources, "r"));
  if (!c.floors.empty()) e.set("floors", encode_resource_map(c.floors, "r"));
  if (!c.floor_overrides.empty()) {
    json::Value v = json::Value::array({});
    for (int idx : c.floor_overrides) v.push_back(json::Value::integer(idx));
    e.set("floor_overrides", v);
  }
  return e;
}

std::map<int, Milli> decode_resource_map(const json::Value& v) {
  std::map<int, Milli> out;
  for (const auto& [key, val] : v.as_object()) {
    out[std::stoi(key.substr(1))] = parse_decimal_string(val.as_string());
  }
  return out;
}

Command decode_command(const json::Value& e) {
  const std::string ctx = "journal command";
  Command c;
  c.id = e.require_string("id", ctx);
  auto kind = parse_command_kind(e.require_string("kind", ctx));
  if (!kind) throw SimError("journal: unknown command kind");
  c.kind = *kind;
  c.planet_id = e.string_or("planet_id", "");
  c.content_id = e.string_or("content_id", "");
  c.recipe_id = e.string_or("recipe_id", "");
  if (const json::Value* v = e.find("facility_id")) c.facility_id = parse_decimal_string_u(v->as_string());
  if (const json::Value* v = e.find("to_facility_id")) c.to_facility_id = parse_decimal_string_u(v->as_string());
  if (const json::Value* v = e.find("event_instance_id")) {
    c.event_instance_id = parse_decimal_string_u(v->as_string());
  }
  c.count = static_cast<int>(e.int_or("count", 0));
  c.priority_band = static_cast<int>(e.int_or("priority_band", 0));
  c.flag = e.bool_or("flag", false);
  if (const json::Value* v = e.find("resources")) c.resources = decode_resource_map(*v);
  if (const json::Value* v = e.find("floors")) c.floors = decode_resource_map(*v);
  if (const json::Value* v = e.find("floor_overrides")) {
    for (const auto& x : v->as_array()) c.floor_overrides.push_back(static_cast<int>(x.as_int()));
  }
  return c;
}

}  // namespace

std::string encode_journal(const CommandJournal& j) {
  json::Value root = json::Value::object({});
  root.set("magic", json::Value::string(kJournalMagic));
  root.set("format_version", json::Value::integer(kJournalFormatVersion));
  root.set("simulation_version", json::Value::string(j.simulation_version));
  root.set("catalog_hash", json::Value::string(j.catalog_hash));
  root.set("scenario_id", json::Value::string(j.scenario_id));
  root.set("faction_id", json::Value::string(j.faction_id));
  root.set("seed", json::dec_u(j.seed));
  root.set("initial_state_hash", json::Value::string(j.initial_state_hash));
  json::Value entries = json::Value::array({});
  for (const auto& e : j.entries) {
    json::Value x = json::Value::object({});
    x.set("type", json::Value::string(e.is_step ? "step" : "command"));
    x.set("day", json::dec(e.day));
    x.set("command_sequence", json::dec_u(e.command_sequence));
    if (!e.is_step) x.set("command", encode_command(e.command));
    entries.push_back(x);
  }
  root.set("entries", entries);
  json::Value hashes = json::Value::array({});
  for (const auto& h : j.day_hashes) hashes.push_back(json::Value::string(h));
  root.set("day_hashes", hashes);
  return json::serialize_canonical(root);
}

CommandJournal decode_journal(const std::string& bytes) {
  json::Value root = json::parse(bytes);
  const std::string ctx = "journal";
  if (root.require_string("magic", ctx) != kJournalMagic) throw SimError("journal: wrong file magic");
  const int version = static_cast<int>(root.require_int("format_version", ctx));
  if (version != kJournalFormatVersion) {
    throw SimError("journal: unsupported format version " + std::to_string(version));
  }
  CommandJournal j;
  j.simulation_version = root.require_string("simulation_version", ctx);
  j.catalog_hash = root.require_string("catalog_hash", ctx);
  j.scenario_id = root.require_string("scenario_id", ctx);
  j.faction_id = root.require_string("faction_id", ctx);
  j.seed = root.require_decimal_u("seed", ctx);
  j.initial_state_hash = root.require_string("initial_state_hash", ctx);
  for (const auto& e : root.require_array("entries", ctx)) {
    JournalEntry entry;
    const std::string type = e.require_string("type", ctx);
    entry.is_step = type == "step";
    if (!entry.is_step && type != "command") throw SimError("journal: unknown entry type '" + type + "'");
    entry.day = e.require_decimal("day", ctx);
    entry.command_sequence = e.require_decimal_u("command_sequence", ctx);
    if (!entry.is_step) entry.command = decode_command(e.require("command", ctx));
    j.entries.push_back(entry);
  }
  for (const auto& h : root.require_array("day_hashes", ctx)) j.day_hashes.push_back(h.as_string());
  return j;
}

JournalRecorder::JournalRecorder(Session& session, const Catalog& catalog) : session_(&session) {
  journal_.simulation_version = catalog.simulation_version();
  journal_.catalog_hash = catalog.hash();
  journal_.scenario_id = session.state().scenario_id;
  journal_.faction_id = session.state().faction_id;
  journal_.seed = session.state().seed;
  journal_.initial_state_hash = session.canonical_hash();
}

CommandResult JournalRecorder::apply(const Command& c, std::optional<Revision> expected_revision) {
  CommandResult r = session_->apply_command(c, expected_revision);
  if (r.accepted && !r.replayed) {
    JournalEntry e;
    e.is_step = false;
    e.day = session_->state().day;
    e.command_sequence = session_->state().command_sequence;
    e.command = c;
    journal_.entries.push_back(e);
  } else if (!r.accepted) {
    rejected_.emplace_back(c, r);
  }
  return r;
}

DayResult JournalRecorder::step() {
  DayResult r = session_->step_day();
  JournalEntry e;
  e.is_step = true;
  e.day = r.day;
  e.command_sequence = session_->state().command_sequence;
  journal_.entries.push_back(e);
  journal_.day_hashes.push_back(r.day_hash);
  return r;
}

std::string first_differing_field(const SessionState& a, const SessionState& b, const Catalog& catalog) {
  const json::Value ja = encode_state(a, catalog);
  const json::Value jb = encode_state(b, catalog);
  for (const auto& [key, va] : ja.as_object()) {
    const json::Value* vb = jb.find(key);
    if (vb == nullptr) return key + " (missing in the second state)";
    if (json::serialize_canonical(va) != json::serialize_canonical(*vb)) return key;
  }
  for (const auto& [key, unused] : jb.as_object()) {
    (void)unused;
    if (ja.find(key) == nullptr) return key + " (missing in the first state)";
  }
  return "";
}

ReplayResult replay_journal(const Catalog& catalog, const CommandJournal& journal) {
  ReplayResult result;
  if (journal.catalog_hash != catalog.hash()) {
    throw SimError("replay: the journal was recorded against catalog " + journal.catalog_hash +
                   "; this content set is " + catalog.hash());
  }
  auto session = Session::create(catalog, journal.scenario_id, journal.faction_id, journal.seed);
  if (!journal.initial_state_hash.empty() && session->canonical_hash() != journal.initial_state_hash) {
    result.difference.differs = true;
    result.difference.day = session->state().day;
    result.difference.expected_hash = journal.initial_state_hash;
    result.difference.actual_hash = session->canonical_hash();
    result.difference.first_differing_field = "initial state";
    return result;
  }
  std::size_t hash_index = 0;
  for (const auto& e : journal.entries) {
    if (e.is_step) {
      DayResult d = session->step_day();
      result.day_hashes.push_back(d.day_hash);
      if (hash_index < journal.day_hashes.size()) {
        if (journal.day_hashes[hash_index] != d.day_hash && !result.difference.differs) {
          result.difference.differs = true;
          result.difference.day = d.day;
          result.difference.expected_hash = journal.day_hashes[hash_index];
          result.difference.actual_hash = d.day_hash;
        }
        ++hash_index;
      }
    } else {
      CommandResult r = session->apply_command(e.command, std::nullopt);
      if (r.accepted) {
        result.commands_applied += 1;
      } else {
        result.commands_rejected += 1;
      }
    }
  }
  result.final_hash = session->canonical_hash();
  return result;
}

}  // namespace expansion
