# 0007 — The presentation layer is headless read models

Status: accepted for this delivery. Blocks nothing in the core; blocks T28 and T30.

## Context

Section 15 requires a single interface shell with a civilization summary, a two-world
network map, a contextual inspector and an event and history drawer, plus
accessibility acceptance at two resolutions and two UI scales. Section 3.3 recommends
Unreal Engine 5 with UMG as the presentation adapter and records that host approval
is Approval A01, which has not been given. Section 18.4 requires a runnable packaged
scenario for P1.

Section 3.2 also requires that the presentation module own read models, explanation
text and formatting, and own no resource or worker mutation.

## Decision

This delivery implements `presentation` as read models and display strings rendered
as text, and `cli` as the host that shows them. Every required view's *content*
exists and is exercised by `read_models_render`:

- Civilization overview: people, health and stability by world, stock risks,
  obligations with dates, and the three most actionable concerns, cause first.
- Network map: both worlds, the single route, the freighter's physical location,
  cargo, ETA, blocked states, handling capacity and next departure.
- Planet inspector: stocks, needs, net daily flows, housing, power generation and
  use, workforce by job, construction and active policies.
- Facility inspector: inputs and outputs, actual run factors, the precise bottleneck,
  staffing, priority, maintenance and condition, and the active effects with their
  sources.
- Freight inspector: manifests, reserve floors, tank and propulsion costs, the
  handling limit and the timeline including strategic occupancy.
- Decision and history drawer: costs with exact deficits for unaffordable options,
  deadlines, source facts, and searchable chronological news.

## Consequences

The explanation contract is testable now, ahead of any engine decision: a low output
caused by labour is reported as understaffing and never as missing material, and the
alert text is assembled from the current forecast and the actual mission record.

What is not delivered: layout, colour, input, audio, focus behaviour, text scaling,
reduced motion and every other accessibility requirement. T28 and T30 therefore
remain unmet, and this repository claims no compliance. Approval A01 is still the
gate for substantial presentation work, and no engine-specific code exists to
prejudge it.
