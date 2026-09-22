// Command-script parsing for the headless host (TDD 3.2 ExpansionCLI).
// The CLI is a host: it issues the same commands the interface would, and never
// contains a second economy implementation.
#pragma once

#include <string>
#include <vector>

#include "expansion/catalog.hpp"
#include "expansion/commands.hpp"

namespace expansion::cli {

struct ScriptStep {
  enum class Kind { Command, Step, UntilDay, Expect, Note };
  Kind kind = Kind::Note;
  Command command;
  Day days = 1;
  std::string text;
  int line_number = 0;
};

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
