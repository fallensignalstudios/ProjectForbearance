// The host boundary. Nothing here throws.
//
// TDD 19.2: "All public operations return typed error information. Authoritative
// code does not throw through the host adapter." This header is that boundary.
// Every operation catches the core's internal exceptions and returns an
// `Outcome`, so a host module compiled with exceptions disabled -- which is the
// Unreal default for most module configurations -- can call all of it safely.
//
// The classes underneath still throw internally; that is how the core keeps its
// invariants honest. They are just never allowed past this file.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "expansion/catalog.hpp"
#include "expansion/commands.hpp"
#include "expansion/content_source.hpp"
#include "expansion/session.hpp"

namespace expansion::api {

struct Error {
  ErrorCode code = ErrorCode::Ok;
  std::string message;
  bool failed() const { return code != ErrorCode::Ok; }
  // A stable identifier a host can log or switch on without parsing prose.
  const char* id() const { return error_code_id(code); }
};

// A value or an error, never both. `ok()` must be checked before `value()`.
template <typename T>
class Outcome {
 public:
  static Outcome success(T value) {
    Outcome o;
    o.value_ = std::move(value);
    return o;
  }
  static Outcome failure(ErrorCode code, std::string message) {
    Outcome o;
    o.error_ = Error{code, std::move(message)};
    return o;
  }

  bool ok() const { return !error_.failed(); }
  explicit operator bool() const { return ok(); }
  const Error& error() const { return error_; }
  // Valid only when ok(). Calling it on a failure returns the default value
  // rather than throwing, because nothing here may throw.
  const T& value() const { return value_; }
  T& value() { return value_; }
  T take() { return std::move(value_); }

 private:
  T value_{};
  Error error_;
};

// An operation with no value to return.
struct Status {
  Error error;
  bool ok() const { return !error.failed(); }
  explicit operator bool() const { return ok(); }
  static Status success() { return Status{}; }
  static Status failure(ErrorCode code, std::string message) { return Status{Error{code, std::move(message)}}; }
};

// A catalog is immutable once loaded and is shared by every session built on it.
using CatalogPtr = std::shared_ptr<const Catalog>;
using SessionPtr = std::shared_ptr<Session>;

// --- the operations of TDD 19.2, each non-throwing --------------------------

Outcome<CatalogPtr> load_catalog(const ContentSource& source);

Outcome<SessionPtr> create_session(CatalogPtr catalog, const std::string& scenario_id,
                                   const std::string& faction_id, std::uint64_t seed);

// Already all-or-nothing in the core; wrapped here so a host has one call style.
CommandResult apply_command(Session& session, const Command& command,
                            std::optional<Revision> expected_revision = std::nullopt);

Outcome<DayResult> step_day(Session& session);

Outcome<ForecastResult> forecast(const Session& session, const std::vector<Command>& candidate_commands, int days);

Outcome<std::string> encode_save(const Session& session, const std::string& build_id);

Outcome<SessionPtr> decode_save(CatalogPtr catalog, const std::string& bytes);

Outcome<std::string> canonical_hash(const Session& session);

// Names the scenarios and the playable factions a host may offer, so a menu
// never has to hard-code content ids.
struct ScenarioSummary {
  std::string id;
  std::string display_key;
  Day evaluation_day = 0;
};
struct FactionSummary {
  std::string id;
  std::string display_key;
};
std::vector<ScenarioSummary> scenarios(const Catalog& catalog);
std::vector<FactionSummary> playable_factions(const Catalog& catalog);

}  // namespace expansion::api
