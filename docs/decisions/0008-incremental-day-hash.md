# 0008 — The canonical day hash folds the archives incrementally

Status: accepted.

## Context

Section 5.2 phase 10 requires appending a canonical day hash at every commit, and
Section 16.3 requires comparing canonical hashes after every day when debugging.
Section 17.1 sets a provisional budget of 2 ms at the 95th percentile for a daily
step at the two-world size, and a stress envelope of eight planets, 128 facilities
per planet, 32 ships and 64 active event instances with no unbounded allocation
growth over 10,000 days.

The first implementation hashed the whole canonical state every day, archives
included: the transaction ledger, the fact and news records, and the bounded metric
history. Measured on this container, a 120-day two-world run spent 10.4 ms at the
95th percentile per step, and essentially all of it in that serialisation. The cost
grows with elapsed history, so the work over a campaign is quadratic. At the stress
envelope's 10,000 days it would be unusable, and the growth alone fails the design's
own requirement.

A second measurement, over 10,000 days on a long-running fixture, showed the step
cost rising from 0.83 ms to 51.8 ms as the campaign advanced. Two causes: the
whole-state hash above, and a compaction bug. Once the metric-sample ring was full,
every day compacted the dropped samples into a *new* weekly summary, so the weekly
archive grew by one entry per world per day and was never bounded.

## Decision

Split the canonical encoding.

`encode_live_state` covers everything that can affect a later day: planets,
facilities, transfers, the ship, the route, colonisation, politics, events, the
scheduled queue, the pending event requests, the mandate, flags, cooldowns,
condition streaks, recorded command results, and the history counters. It also
carries the size of each archive, so a truncated archive cannot pass unnoticed.

Each archive carries a rolling digest in the state itself:
`digest_k = sha256(digest_{k-1} || entry_k)`, extended when an entry is appended and
never recomputed. `SessionState::archive_digests` holds one per archive, and
`encode_live_state` emits them, so the day hash is simply the SHA-256 of the live
payload.

Keeping the digest in the state, rather than caching it outside, is what makes
pruning harmless. A digest covers every entry ever recorded, so dropping a
transaction from the bounded ledger cannot weaken it: two runs agree only if they
recorded the same entries in the same order, whether or not those entries survive in
the save. A cache outside the state would instead have to be rebuilt on every prune,
which for a ring pruned daily is the same linear cost it was meant to remove.

Digest entries are encoded compactly and without the catalog, so any append site can
extend a digest without reaching for the catalog or the JSON writer.

Separately, the weekly compaction now merges each dropped sample into the fixed
weekly bucket its day belongs to, instead of appending a fresh summary per
compaction, and the weekly archive is itself bounded at 520 weeks per world. A
summary is digested as it leaves, so a dropped week still shapes the hash.

`encode_state` still emits the complete state including every archive entry. That is
what a save contains, and its integrity is separately protected by the envelope's own
payload checksum.

## Consequences

Measured on this container, the same 120-day run now costs 0.35 ms at the median and
0.43 ms at the 95th percentile per step, and the day hash alone is 0.27 ms at day 120.
Over 10,000 days on a long-running fixture the step cost is flat, 0.14 ms at the
median from day 200 through day 10,000, with resident memory stable near 5.6 MiB.

That is inside the design's provisional 2 ms budget and satisfies its requirement of
no unbounded growth over 10,000 days. The budget names a reference Windows machine
that has not been approved, so these numbers describe this container and are not a
benchmark result. No stress-size measurement has been taken, because eight-planet
content does not exist.

The hash covers the same information as before, not the same bytes. Two runs that
agree on the live state and on every archive entry produce the same hash; a run that
differs in any archive entry produces a different one. `hash_matches_across_a_reload`
and `archive_digests_survive_pruning` check that a save and reload always agree, the
second on a fixture whose archives are capped at five news entries and three metric
samples per world so pruning happens within a few days.

Re-recording the golden journals was required, and it is what caught this change:
four fixtures failed with an exact first differing day before they were re-recorded
deliberately. The four recorded outcomes are unchanged.
