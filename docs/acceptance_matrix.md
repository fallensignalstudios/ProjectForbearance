# Acceptance matrix

Evidence levels from Section 18.1 of the technical design: **Defined** when rules
and data exist, **Implemented** when code exists, **Unit-verified** when isolated
tests pass, **Integrated** when a packaged host demonstrates it, **Playtested** when
observed players complete it. These are distinct states, and this table does not
convert them into a percentage.

No test below is Integrated or Playtested: there is no packaged build and no player
session.

| ID | Test | Evidence | Where |
| --- | --- | --- | --- |
| T01 | Neutral one-day economy matches Section 9.3 exactly | Unit-verified | `t01_neutral_day`, `t01_conservation`, and `expansion validate` |
| T02 | Competing inventory reservations fail atomically | Unit-verified | `t02_inventory_laws`, `t02_over_reservation`, `t02_competing_reservations` |
| T03 | Staged production; recipe order cannot change it | Unit-verified | `t03_staged_production` |
| T04 | Power brownout serves residential first; inputs and outputs scale together | Unit-verified | `t04_power_brownout` |
| T05 | Half water and half power yield a 50% run, not 25% | Unit-verified | `t05_bottleneck_math` |
| T06 | Worker conservation across assignments, transitions, Reserve and crew | Unit-verified | `t06_worker_conservation`, and the commit-time invariant on every day |
| T07 | A transfer's lost production day cannot be undone | Unit-verified | `t07_transfer_exploit` |
| T08 | Build escrow refunds only the unconsumed remainder | Unit-verified | `t08_build_escrow`, `t08_escrow_cannot_be_farmed` |
| T09 | Full storage blocks production without losing inputs or cargo | Unit-verified | `t09_full_store` |
| T10 | Normal voyage: depart 10, arrive 12, return 13, arrive 15, reload 16 | Unit-verified | `t10_t11_normal_voyage` |
| T11 | Round-trip budget charged once; the return needs no destination fuel | Unit-verified | `t10_t11_normal_voyage` |
| T12 | A save taken mid-unload finishes with identical totals | Unit-verified | `t12_partial_unload_save` |
| T13 | Expedition accounting: sink, transit, arrival, one founding Hub | Unit-verified | `t13_expedition_accounting` |
| T14 | Colonial bootstrap with no import-before-port loop | Unit-verified | `t14_colonial_bootstrap` |
| T15 | World efficiency: twenty mine workers produce 36 ore | Unit-verified | `t15_world_efficiency_exact`, `t15_world_efficiency_in_scenario` |
| T16 | Policy lifecycle, minimum, cooldown, no paused adherence farming | Unit-verified | `t16_policy_lifecycle`, `t16_rationing_reduces_demand` |
| T17 | Restoring condition in time prevents the accident | Unit-verified | `t17_event_prevention`, `t17_accident_opens_when_unaddressed` |
| T18 | Event outcomes apply once across save and reload | Unit-verified | `t18_outcome_idempotency`, `t18_deferral_then_remedy` |
| T19 | Adherence and public stability are distinct meters | Unit-verified | `t19_distinct_political_meters` |
| T20 | The mandate needs multiple voyages; no instant submission | Unit-verified | `t20_mandate_freight`, `t20_negotiation` |
| T21 | Relief is delayed, once-only, and the ten-day loss rule holds | Unit-verified | `t21_relief_and_loss` |
| T22 | Replay determinism | Unit-verified within a build; also compared across two compilers on one platform | `t22_replay_determinism`, `t22_journal_round_trip`, `t22_mismatch_names_the_day`, and the golden replays |
| T23 | Save safety and incompatible-catalog rejection | Unit-verified (durability not verified on Windows) | `t23_save_round_trip`, `t23_rejects_tampered_and_mismatched`, `t23_rejects_untrusted_input` |
| T24 | Forecast purity and arithmetic agreement with the live resolver | Unit-verified | `t24_forecast_purity` |
| T25 | News truth, deduplication and transition-only reporting | Unit-verified | `t25_news_truth` |
| T26 | Same day count gives the same result at any batch size | Unit-verified | `t26_step_batching`, `t26_finished_scenario_is_idle` |
| T27 | Randomised valid commands preserve every invariant | Unit-verified (400 days by default; `EXPANSION_SOAK_DAYS` raises it for a nightly run) | `t27_soak` |
| T28 | A player can trace an alert to its cause and act through the same API | **Defined and partly Implemented** | The view content is verified by `read_models_render` and `news_renders_without_placeholders`; without a graphical shell the test itself cannot be run |
| T29 | One recorded Complete run per faction on the shipped catalog | Unit-verified | `t29_complete_run_dominion`, `t29_complete_run_reformation`, plus `golden_compromised_run`, `golden_relief_and_loss`, `golden_no_developer_grants` |
| T30 | Four of five first-time testers identify a recorded cause | **Defined only** | Requires players; not attempted |

## Invariants checked at every commit

Commodity inventory, cargo, tank contents, reserved capacity, incoming claims,
workers, population, job work and slots stay within bounds. Reservations never
exceed on-hand. Incoming claims never overbook capacity. Workers never exceed
population, and assignments plus transitions plus Reserve plus ship crew equal the
worker pool on every world. Construction escrow never exceeds the original cost. A
ship in transit is never also at a planet. Every active modifier and every scheduled
effect still resolves against the catalog. No open decision window is past its
deadline after a commit.

`t01_conservation` additionally reconciles each resource against explicit sources
and sinks from the transaction ledger, for every day of a run.

`day_hash_cost_is_not_quadratic` guards the growth property the stress envelope
depends on: the payload a day hash re-serialises must not grow with elapsed history.
See `docs/decisions/0008-incremental-day-hash.md`.

## Open findings against the numerical seed

Section 21.2's named next step after the neutral calibration is a deliberately
broken Coal-supply fixture with a readable explanation. That fixture exists, and it
raises one finding.

**Coal exhaustion on Homeworld is unrecoverable.** The Colony Hub's 20 passive solar
exactly equals a thousand residents' protected demand, so once the Coal stockpile
reaches zero on day 11 no facility can run, including the mine whose output would
restore generation. Acting one day earlier recovers completely. This contradicts the
stated intent of Section 8.3 and realises the risk named in Section 21.1. No balance
value has been changed, because the fix moves figures that Section 9.3 states
exactly. Four options are laid out for approval A04 in
`docs/decisions/0009-coal-exhaustion-is-unrecoverable.md`.

## Deliberate gaps

- **No packaged build.** A Linux headless pass does not prove Windows packaging or
  an Unreal build, and this repository claims neither.
- **No performance measurement against a named machine.** Section 17 names budgets
  but no reference machine has been approved, so every target remains provisional.
  The figures observed on this container, for the two-world size, are a median of
  0.35 ms and a 95th percentile of 0.43 ms per daily step, with the canonical day
  hash costing 0.27 ms at day 120. Over 10,000 days on a long-running fixture the
  step cost is flat at 0.14 ms median and resident memory is stable near 5.6 MiB,
  which is the design's no-unbounded-growth requirement. They describe this
  container, not the reference hardware the design asks for, and no stress-size
  measurement has been taken because eight-planet content does not exist.
- **Partial cross-toolchain determinism.** GCC 13 and Clang 18 builds of this
  repository produce identical final hashes for all four recorded runs, and the
  continuous integration job builds both. That is one platform. Comparing a Windows
  build, or an Unreal-hosted build, remains an unperformed test.
- **No accessibility audit.** Screen-reader support and controller scope are
  untested, and no compliance is claimed.
