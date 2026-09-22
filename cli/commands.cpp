#include "commands.hpp"

#include <algorithm>
#include <cstddef>
#include <sstream>

namespace expansion::cli {
namespace {

std::vector<std::string> split_words(const std::string& line) {
  std::vector<std::string> out;
  std::istringstream ss(line);
  std::string w;
  while (ss >> w) out.push_back(w);
  return out;
}

std::pair<std::string, std::string> split_pair(const std::string& token, int line_number) {
  const std::size_t eq = token.find('=');
  if (eq == std::string::npos || eq == 0) {
    throw SimError("script line " + std::to_string(line_number) + ": expected key=value, got '" + token + "'");
  }
  return {token.substr(0, eq), token.substr(eq + 1)};
}

std::int64_t parse_number(const std::string& s, int line_number) {
  // Accepts a decimal quantity and converts it to milli-units.
  bool negative = false;
  std::size_t i = 0;
  if (!s.empty() && (s[0] == '-' || s[0] == '+')) {
    negative = s[0] == '-';
    i = 1;
  }
  std::int64_t whole = 0;
  bool any = false;
  for (; i < s.size() && s[i] != '.'; ++i) {
    if (s[i] < '0' || s[i] > '9') {
      throw SimError("script line " + std::to_string(line_number) + ": '" + s + "' is not a number");
    }
    whole = whole * 10 + (s[i] - '0');
    any = true;
  }
  std::int64_t frac = 0;
  int digits = 0;
  if (i < s.size() && s[i] == '.') {
    ++i;
    for (; i < s.size(); ++i) {
      if (s[i] < '0' || s[i] > '9') {
        throw SimError("script line " + std::to_string(line_number) + ": '" + s + "' is not a number");
      }
      if (digits < 3) {
        frac = frac * 10 + (s[i] - '0');
        ++digits;
      }
      any = true;
    }
  }
  if (!any) throw SimError("script line " + std::to_string(line_number) + ": '" + s + "' is not a number");
  while (digits < 3) {
    frac *= 10;
    ++digits;
  }
  std::int64_t value = whole * 1000 + frac;
  return negative ? -value : value;
}

std::int64_t parse_integer(const std::string& s, int line_number) {
  std::int64_t v = parse_number(s, line_number);
  if (v % 1000 != 0) {
    throw SimError("script line " + std::to_string(line_number) + ": '" + s + "' must be a whole number");
  }
  return v / 1000;
}

}  // namespace

InstanceId resolve_reference(const Session& session, const std::string& reference, int line_number) {
  if (reference == "@last") {
    InstanceId best = 0;
    for (const auto& f : session.state().facilities) best = std::max(best, f.id);
    if (best == 0) throw SimError("script line " + std::to_string(line_number) + ": no facility exists yet");
    return best;
  }
  if (reference == "@open") {
    const EventInstance* best = nullptr;
    for (const auto& e : session.state().events) {
      if (e.resolution != EventResolution::Open) continue;
      if (best == nullptr || (e.deadline_day >= 0 && e.deadline_day < best->deadline_day)) best = &e;
    }
    if (best == nullptr) throw SimError("script line " + std::to_string(line_number) + ": no event is open");
    return best->id;
  }
  const std::size_t colon = reference.find(':');
  if (reference.size() < 3 || colon == std::string::npos) {
    throw SimError("script line " + std::to_string(line_number) + ": '" + reference +
                   "' is not @world:recipe, @last or @open");
  }
  const std::string planet = reference.substr(1, colon - 1);
  const std::string recipe = reference.substr(colon + 1);
  for (const auto& f : session.state().facilities) {
    if (f.planet_id == planet && f.recipe_id == recipe) return f.id;
  }
  throw SimError("script line " + std::to_string(line_number) + ": no facility on " + planet + " runs '" + recipe + "'");
}

std::vector<ScriptStep> parse_script(const std::string& text, const Catalog& catalog) {
  std::vector<ScriptStep> out;
  std::istringstream lines(text);
  std::string line;
  int line_number = 0;
  while (std::getline(lines, line)) {
    ++line_number;
    const std::size_t hash = line.find('#');
    if (hash != std::string::npos) line = line.substr(0, hash);
    std::vector<std::string> words = split_words(line);
    if (words.empty()) continue;

    ScriptStep step;
    step.line_number = line_number;
    const std::string verb = words.front();

    if (verb == "note") {
      step.kind = ScriptStep::Kind::Note;
      for (std::size_t i = 1; i < words.size(); ++i) step.text += (i > 1 ? " " : "") + words[i];
      out.push_back(step);
      continue;
    }
    if (verb == "step") {
      step.kind = ScriptStep::Kind::Step;
      step.days = words.size() > 1 ? parse_integer(words[1], line_number) : 1;
      out.push_back(step);
      continue;
    }
    if (verb == "until") {
      step.kind = ScriptStep::Kind::UntilDay;
      if (words.size() < 2) throw SimError("script line " + std::to_string(line_number) + ": until needs day=N");
      auto [key, value] = split_pair(words[1], line_number);
      if (key != "day") throw SimError("script line " + std::to_string(line_number) + ": until needs day=N");
      step.days = parse_integer(value, line_number);
      out.push_back(step);
      continue;
    }
    if (verb == "expect") {
      step.kind = ScriptStep::Kind::Expect;
      for (std::size_t i = 1; i < words.size(); ++i) step.text += (i > 1 ? " " : "") + words[i];
      out.push_back(step);
      continue;
    }

    auto kind = parse_command_kind(verb);
    if (!kind) {
      throw SimError("script line " + std::to_string(line_number) + ": unknown command '" + verb + "'");
    }
    step.kind = ScriptStep::Kind::Command;
    step.command.kind = *kind;
    step.command.id = "script:" + std::to_string(line_number);
    for (std::size_t i = 1; i < words.size(); ++i) {
      auto [key, value] = split_pair(words[i], line_number);
      if (key == "id") {
        step.command.id = value;
      } else if (key == "planet") {
        step.command.planet_id = value;
      } else if (key == "content" || key == "policy" || key == "choice" || key == "response" || key == "field" ||
                 key == "facility_type") {
        step.command.content_id = value;
      } else if (key == "recipe") {
        step.command.recipe_id = value;
      } else if (key == "facility" || key == "from") {
        if (!value.empty() && value[0] == '@') {
          step.facility_ref = value;
        } else {
          step.command.facility_id = static_cast<InstanceId>(parse_integer(value, line_number));
        }
      } else if (key == "to") {
        if (!value.empty() && value[0] == '@') {
          step.to_facility_ref = value;
        } else {
          step.command.to_facility_id = static_cast<InstanceId>(parse_integer(value, line_number));
        }
      } else if (key == "event") {
        if (!value.empty() && value[0] == '@') {
          step.event_ref = value;
        } else {
          step.command.event_instance_id = static_cast<InstanceId>(parse_integer(value, line_number));
        }
      } else if (key == "count") {
        step.command.count = static_cast<int>(parse_integer(value, line_number));
      } else if (key == "band") {
        step.command.priority_band = static_cast<int>(parse_integer(value, line_number));
      } else if (key == "flag") {
        step.command.flag = value == "true" || value == "1" || value == "yes";
      } else if (key == "override") {
        int idx = catalog.find_resource(value);
        if (idx < 0) throw SimError("script line " + std::to_string(line_number) + ": unknown resource '" + value + "'");
        step.command.floor_overrides.push_back(idx);
      } else if (key.rfind("floor_", 0) == 0) {
        int idx = catalog.find_resource(key.substr(6));
        if (idx < 0) throw SimError("script line " + std::to_string(line_number) + ": unknown resource in '" + key + "'");
        step.command.floors[idx] = parse_number(value, line_number);
      } else {
        int idx = catalog.find_resource(key);
        if (idx < 0) {
          throw SimError("script line " + std::to_string(line_number) + ": unknown key or resource '" + key + "'");
        }
        step.command.resources[idx] = parse_number(value, line_number);
      }
    }
    // `facility_type` names a definition; `facility` names an instance.
    if (step.command.kind == CommandKind::StartConstruction && step.command.content_id.empty()) {
      throw SimError("script line " + std::to_string(line_number) +
                     ": start_construction needs facility_type=<id>");
    }
    out.push_back(step);
  }
  return out;
}

}  // namespace expansion::cli
