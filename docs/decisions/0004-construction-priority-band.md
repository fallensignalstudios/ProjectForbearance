# 0004 — Construction crews resolve in priority band 40

Status: accepted.

## Context

Section 8.2 gives default priority bands for production: Hub 5; Waterworks and Farm
10; Clinic 15; Coal Extraction and Spaceport 20; Industry and Iron Extraction 30.
Section 5.2 phase 4 says to "reserve power for facilities and work crews by
priority", and phase 7 says to advance construction "with assigned effective workers
and reserved power". The band list does not place construction crews.

## Decision

Construction draws power after every production recipe, at an implied band of 40. In
practice: phase 4 reserves residential demand, phase 5 allocates to production
recipes in band order, and phase 7 gives construction whatever remains.

Partial power gives proportional progress, as Section 9.2 requires: a job needs 0.1
power per assigned worker per day, and the work completed that day scales with the
fraction granted.

## Consequences

A power-tight world finishes its buildings more slowly rather than browning out a
farm to raise a wall. That is the conservative reading, and it matches the design's
instruction that residential demand comes first and that a construction job "paused
for lack of labour or power keeps its escrow and displays the specific blockage".

If a later revision wants construction to outrank some production, the band becomes
a per-job setting; nothing in the resolver assumes 40 specifically, because the order
comes from the same band comparison every facility uses.
