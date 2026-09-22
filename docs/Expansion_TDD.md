# Sovereign Call: Expansion — implementation contract

This document is the implementation's restatement of *Sovereign Call: Expansion —
Technical Design Document, Prototype Baseline v0.1* (19 September 2026). The PDF
remains the design authority. This file records what the code actually does, so a
reader can check the two against each other without guessing.

The design's authority labels are preserved. **SOURCE** means stated in the game
design document. **ACCEPTED DIRECTION** means the two-planet, prototype-first
approach accepted in review. **PROPOSED** means a technical or numerical decision
the technical design introduced. **DEFERRED** means retained from the larger design
but not scheduled for the first playable.

Unless a paragraph says otherwise, every formula, threshold and balance value below
is PROPOSED. They are working defaults, not approved canon.

## 1. Scope

P1 has exactly two playable world nodes and at most one player freighter. Eleven
facility types, with Extraction Site represented by two fixed recipes. Eight
workforce categories, seven stored commodities, two playable faction profiles,
three economic policies including Normal, one accident chain, one strategic mandate.

Healthcare, housing, fatigue, public stability and faction adherence are active.
Education, recreation, security forces, planetary defences, research, foreign
markets and full diplomacy are not. Section 20 of the design records their entry
contracts; none is implemented here.

Population growth is exactly zero. Colonisation transfers existing people.

## 2. Architecture

One authoritative mutable state. Views, forecasts, news and host adapters cannot
bypass it.

```
JSON definitions + scenario seed -> validated immutable catalog
                                        |
UI / CLI -> commands -> Core Simulation -> committed state
                             |                  |
                             |             read-only snapshots
                             |                  |
                        fact ledger       UI / forecasts / news
                             |
                       save + replay
```

The diagram is a dependency description, not a threading requirement. The resolver
is single-threaded. A forecast runs on a clone whose results cannot write into live
state.

| Module | Owns | Must not own |
| --- | --- | --- |
| `core` | State, commands, daily systems, ledger, conditions, deterministic scheduling, the canonical state codec | Widgets, rendering, file dialogs, networking |
| `persistence` | Save envelope, compatibility checks, slot safety, replay journal | Gameplay effects of loading |
| `presentation` | Read models, explanation text, formatting | Direct resource or worker mutation |
| `cli` | Headless scenario host, diagnostics, test fixtures, replay comparison | A different economy implementation |

No Unreal host exists in this repository. The design recommends UE5 with UMG as the
presentation adapter, pinned at E0; that approval has not been given and no
engine-specific code has been written.

## 3. Units and identity

Commodity values are signed 64-bit integers in milli-units: 1 displayed unit equals
1000 internal units. Ratios are basis points: 10000 equals 100%. Population,
workers, days and slots are integers. Power is power-unit milli-units, never an
inventory commodity. Work is milli person-days.

For nonnegative quantities, output rounds down and required inputs round up. A job
that would produce no minimum output quantum consumes no inputs. Multiplications
use checked wide intermediates; overflow raises an error rather than wrapping or
clamping.

Content ids are stable lowercase ASCII identifiers. Instance ids are monotonically
allocated unsigned integers with the next allocator value saved. Display text is
localisation data, never identity.

Runtime saves encode all 64-bit quantities and instance identifiers as decimal
strings. The authoring seed uses plain integers. The loader rejects fractional and
exponent literals outright, so the two formats never blur.

## 4. The daily order

`StepDay()` produces day D+1 in exactly this order.

| Phase | Action |
| --- | --- |
| 1. Due work | Expire timed effects; apply scheduled consequences; complete worker transfers; charge expedition transit and found a due expedition; promote facilities commissioned yesterday; complete service jobs; deliver due relief; mark ship arrivals |
| 2. Maintenance | Pay facility machinery maintenance from unreserved opening stock in priority order; update condition. Construction jobs pay no facility maintenance |
| 3. Opening snapshot | Freeze eligible workers, condition, prior-day health and fatigue, available inventories and free output capacity |
| 4. Power | Generate local capacity, consume thermal fuel from opening stock, reserve residential demand first |
| 5. Production | Allocate inputs and power in priority order; consume from opening stock; stage all outputs |
| 6. Commit and needs | Commit outputs; consume civilian Food and Water; measure power and clinic coverage; update health, fatigue and stability |
| 7. Construction | Advance funded jobs with effective workers and remaining power; a completion becomes usable the next day |
| 8. Freight | Unload arrivals with today's port service, then depart eligible ships |
| 9. Consequences | Shortage counters, survival, mandate, politics, conditional events, facts, news, completion |
| 10. Commit | Validate invariants, publish the snapshot, append the canonical day hash |

Cargo arriving in phase 8 cannot supply phase 5 production that day. A construction
completion in phase 7 starts tomorrow. A decision taken while paused can alter the
next day but cannot repair the day just committed.

## 5. Determinism

For the same simulation version, catalog hash, initial state, seed and ordered
commands, this build emits the same economic state and fact sequence. JSON key
order, localisation, frame rate and host speed cannot influence it: object keys are
stored bytewise-sorted, and every container in the state is a sorted vector or an
ordered map.

P1 uses no random critical event selection and adds no random-number generator. The
session reserves a seed for future seeded variants and otherwise leaves it unused.

This is verified within one build and one compiler. Cross-compiler and cross-host
comparison is a separate test that has not been run.

## 6. What this implementation adds to the design

Each of these is a documented decision in `docs/decisions/`, not a silent change.

| Addition | Why | Record |
| --- | --- | --- |
| A hand-rolled strict JSON reader and writer | Pinned dependencies and a core that builds offline with no engine | `0001` |
| Five effect kinds beyond the design's eight | The accident chain cannot be authored without them | `0002` |
| A neutral verification faction profile | The neutral one-day calibration cannot be checked under a faction | `0003` |
| Construction resolves in priority band 40 | The design's band list does not place construction crews | `0004` |
| Modifier durations count from the next day to resolve | The design states durations in days without fixing the boundary | `0005` |
| Civilian needs draw from unreserved stock | Keeps the reservation invariant exact | `0006` |
| The canonical day hash folds the append-only archives incrementally | Hashing the whole state every day made a campaign's work quadratic | `0008` |
| `condition_resolved`, `on_resolve`, `expire_keeps_open`, `sector_unique`, `applies_to_facility`, per-choice `resolution` and `cancels_scheduled_from`, per-effect `faction` | Authoring the specified chain behaviour as data instead of code | `0002` |

## 7. What this implementation narrows

- The presentation layer is headless read models, not the required graphical shell.
  The content of every required view exists and is tested; its layout, accessibility
  and comprehension do not.
- Autosave scheduling, coalescing and the rolling slot policy are specified in the
  save module but the CLI writes only the slots it is asked to write.
- The ledger and the news archive are bounded and included in the canonical hash.
  Pruning is deterministic; a pruned routine fact is marked compacted rather than
  left dangling.
- Performance budgets are unmeasured against a named machine. The figures in
  `docs/acceptance_matrix.md` describe this container only.

## 8. Open findings

The deliberately broken Coal-supply fixture that Section 21.2 names as the next step
after the neutral calibration raises one contradiction in the seed: Section 8.3's
intent that solar permits partial recovery after a fuel shortage does not hold at
Homeworld's population, where the Hub's 20 solar exactly equals 20 of protected
residential demand. Recorded, with options, in
`docs/decisions/0009-coal-exhaustion-is-unrecoverable.md`. Nothing was changed
silently.

## 9. Traceability

`docs/acceptance_matrix.md` maps each test in Section 18.2 of the design to the
test that exercises it and the evidence level reached. `docs/metrics.md` lists the
readable metric names a condition may use. `content/schemas/` documents the
authoring contract for each definition file.
