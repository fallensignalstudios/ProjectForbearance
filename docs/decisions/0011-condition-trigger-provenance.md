# 0011 — An event instance never records the facts that opened it: a finding

Status: **open finding**. No simulation behaviour has been changed. The finding
was raised while building the structured views a graphical host binds, because
that is where the missing information becomes visible.

## Context

Section 15.2 puts the interface's first job as "what is at risk, when, and why",
and Section 13.3 requires a news headline to be supported by recorded facts. A
decision the player is asked to answer is the sharpest case of "why": before
choosing a remedy, the player needs to know what made the decision appear.

`EventInstance::trigger_facts` exists for exactly that. It is declared in
`core/include/expansion/state.hpp`, encoded and decoded by the save codec, and
carried into the canonical hash.

## What is actually true

Nothing populates it.

There are two sites that open an event, and both pass an empty list:

| Site | Call |
| --- | --- |
| A standing condition qualifying (`Session::update_events`) | `sim::request_event(state_, c.event_id, c.planet_id, c.facility_id, {})` |
| The `OpenEvent` effect escalating a chain (`sim_effects.cpp`) | `request_event(state, e.key, planet_id, facility_id, {})` |

`sim::open_event` faithfully forwards whatever it is handed, and emits an
`event_opened` fact whose `causal_parents` are that same empty list. So the
consequence reaches the archive too: every `event_opened` fact in a shipped run
has no causal parents, and the causal chain from a Safety Warning to the Mining
Accident it escalates into exists only in the catalog, never in the state.

`tests/integration/test_view_models.cpp:view_pins_open_finding_0011` pins this.
It fails, with a message pointing here, the moment the behaviour changes — which
is the intent: closing the finding should be a deliberate act.

## Why it was not simply fixed

The fix is small — two call sites, plus a fact naming the metric that qualified a
condition trigger. It is also a change to simulation output: new facts and new
causal parents change the canonical day hash, so all four recorded golden replay
journals would have to be re-recorded, and the recorded Complete runs for both
factions would carry different hashes than the ones currently committed.

Re-recording a golden is exactly the event a golden exists to make visible. Doing
it silently while preparing the codebase for an engine host would defeat that. So
the finding is recorded and the change is left for an explicit decision.

## What the views do in the meantime

`view::DecisionCard` reports a `subject_label` — for a facility-scoped decision,
the site, its world, and its current condition; for a world-scoped one, the world
— alongside the event's own opening line. For the worn-mine chain that reads:

> **Mining accident** — Extraction Site at Homeworld — condition 68%
> A mining accident occurred at Homeworld.

That is specific and true, and it is enough for a player to act on. What it is
not is provenance: it describes the site's state now, not the recorded sequence
that led here.

The worn-mine fixture shows how far apart those two things can drift. The warning
opens on day 3 at 66.5% condition and the accident escalates on day 6 — by which
time servicing has carried the condition *up* to 68%, and it keeps climbing while
the accident card sits open. A player reading the subject line sees a site that is
recovering and an accident card that will not explain itself. A host cannot show
"because condition was below 60% on day 1 and stayed there for three days" until
the facts exist.

## Options

1. **Populate both sites.** A condition trigger emits a
   `condition_qualified` fact carrying the metric, its value and the threshold,
   and passes it; `OpenEvent` passes the source instance's `event_opened` fact.
   Re-record the four journals. Cost: one round of golden churn, and a small
   growth in the fact archive. This is the option that makes the field mean what
   its name says.
2. **Populate the chain only.** `OpenEvent` forwards the parent's fact; a
   condition trigger keeps passing nothing. Cheaper, and it restores the chain
   link the archive is missing, but a first-in-chain decision still has no "why".
3. **Remove the field.** If a decision's provenance is to be the authored
   condition rather than recorded facts, `trigger_facts` is dead weight in the
   save and the hash, and the subject label is the whole answer. Honest, and it
   gives up the ability to explain a decision after the fact.

Option 1 is the one the design's own stated principles point to. It is not taken
here because it is a behavioural change, not a presentation one.
