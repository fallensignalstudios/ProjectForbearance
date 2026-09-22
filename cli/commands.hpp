// Command-script parsing for the headless host (TDD 3.2 ExpansionCLI).
// The CLI is a host: it issues the same commands the interface would, and never
// contains a second economy implementation.
#pragma once

#include <string>
#include <vector>

#include "expansion/catalog.hpp"
#include "expansion/commands.hpp"
#include "expansion/session.hpp"

namespace expansion::cli {

struct ScriptStep {
  enum class Kind { Command, Step, UntilDay, Expect, Note };
  Kind kind = Kind::Note;
  Command command;
  Day days = 1;
  std::string text;
  int line_number = 0;
  // Symbolic references resolved against the live session when the step runs,
  // because instance ids are allocated as the simulation proceeds:
  //   @world:recipe_id  the facility on that world running that recipe
  //   @last             the most recently created facility instance
  //   @open             the open event instance with the nearest deadline
  std::string facility_ref;
  std::string to_facility_ref;
  std::string event_ref;
};

// Resolves one symbolic reference against a session. Throws SimError naming the
// reference when it cannot be resolved.
InstanceId resolve_reference(const Session& session, const std::string& reference, int line_number);

// Parses a line-oriented script:
//   # comment
//   step 5
//   until day=42
//   note anything
//   assign_workers planet=homeworld to=3 count=10
//   start_construction planet=frontier facility=spaceport
//   update_route field=outbound food=60 steel=30
// Throws SimError with the line number on any syntax or reference error.
std::vector<ScriptStep> parse_script(const std::string& text, const Catalog& catalog);

}  // namespace expansion::cli
