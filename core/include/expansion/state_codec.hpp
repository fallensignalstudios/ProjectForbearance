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

// The whole state, including the append-only archives. This is the save payload.
json::Value encode_state(const SessionState& state, const Catalog& catalog);

// Everything except the append-only archives: the transaction ledger, the fact
// and news records, and the bounded metric history. The canonical economic hash
// pairs this with a rolling digest over those archives, so hashing a committed day
// costs time proportional to that day's new entries rather than to the whole
// campaign (see docs/decisions/0008-incremental-day-hash.md).
json::Value encode_live_state(const SessionState& state, const Catalog& catalog);

// Canonical encoding of one archive entry, used to extend a rolling digest.
json::Value encode_transaction(const Transaction& t, const Catalog& catalog);
json::Value encode_fact(const FactRecord& f);
json::Value encode_news(const NewsRecord& n);
json::Value encode_metric_sample(const MetricSample& s);
json::Value encode_weekly_summary(const WeeklySummary& w);
// Validates the candidate in isolation and throws SimError on any violation. The
// caller replaces the active session only on success (TDD 16.1).
SessionState decode_state(const json::Value& value, const Catalog& catalog);

constexpr int kStateSchemaVersion = 1;

}  // namespace expansion
