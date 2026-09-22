// Replay journal and determinism comparison (TDD 16.3, 18.2 T22).
//
// A replay contains the initial state and catalog hashes and the ordered
// accepted command journal. Failed commands may appear in a diagnostic log but
// are never silently reapplied as accepted commands.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "expansion/catalog.hpp"
#include "expansion/session.hpp"

namespace expansion {

inline constexpr const char* kJournalMagic = "SCEXPJ";
inline constexpr int kJournalFormatVersion = 1;

struct JournalEntry {
  bool is_step = false;          // StepDay boundary
  Day day = 0;                   // day at the time of recording
  std::uint64_t command_sequence = 0;
  Command command;               // valid when !is_step
};

struct CommandJournal {
  std::string simulation_version;
  std::string catalog_hash;
  std::string scenario_id;
  std::string faction_id;
  std::uint64_t seed = 0;
  std::string initial_state_hash;
  std::vector<JournalEntry> entries;
  // Day hashes recorded by the run that produced this journal, in order.
  std::vector<std::string> day_hashes;
};

std::string encode_journal(const CommandJournal& journal);
CommandJournal decode_journal(const std::string& bytes);

// A recorder that wraps a session, keeping the journal in step with what was
// actually accepted.
class JournalRecorder {
 public:
  JournalRecorder(Session& session, const Catalog& catalog);
  CommandResult apply(const Command& c, std::optional<Revision> expected_revision = std::nullopt);
  DayResult step();
  const CommandJournal& journal() const { return journal_; }
  // Commands that were rejected, for diagnostics only.
  const std::vector<std::pair<Command, CommandResult>>& rejected() const { return rejected_; }

 private:
  Session* session_;
  CommandJournal journal_;
  std::vector<std::pair<Command, CommandResult>> rejected_;
};

struct ReplayDifference {
  bool differs = false;
  Day day = -1;
  std::string expected_hash;
  std::string actual_hash;
  std::string first_differing_field;
};

struct ReplayResult {
  std::vector<std::string> day_hashes;
  std::string final_hash;
  ReplayDifference difference;
  int commands_applied = 0;
  int commands_rejected = 0;
};

// Replay(initial_state, command_journal) -> Hashes | FirstDifference.
ReplayResult replay_journal(const Catalog& catalog, const CommandJournal& journal);

// Names the first field whose canonical encoding differs between two states.
std::string first_differing_field(const SessionState& a, const SessionState& b, const Catalog& catalog);

}  // namespace expansion
