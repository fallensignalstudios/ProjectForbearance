// Canonical encoding of the authoritative state (TDD 16.1).
//
// The same encoding backs the save payload and the canonical economic hash, so a
// value that is saved is a value that is compared. All 64-bit quantities and
// instance identifiers are decimal strings; keys are bytewise sorted by the
// canonical serialiser.
#pragma once

#include "expansion/catalog.hpp"
#include "expansion/json.hpp"
#include "expansion/state.hpp"

namespace expansion {

json::Value encode_state(const SessionState& state, const Catalog& catalog);
// Validates the candidate in isolation and throws SimError on any violation. The
// caller replaces the active session only on success (TDD 16.1).
SessionState decode_state(const json::Value& value, const Catalog& catalog);

constexpr int kStateSchemaVersion = 1;

}  // namespace expansion
