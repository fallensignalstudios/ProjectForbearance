# 0005 — Modifier durations count from the next day to resolve

Status: accepted.

## Context

Section 13.2 specifies consequences such as "+5 stability target for ten days" and
"a 0.50 strike factor for five days", and Section 12.4 an "Industry throughput factor
of 0.90 for ten days". Section 5.1 says a command executes atomically between
simulation days, and Section 5.2 says an effect opened in the consequence phase
begins no earlier than the next day.

The design states durations in days without fixing which day is the first.

## Decision

A modifier created while resolving or committing day D expires at the end of day
`D + duration_days`, and is active on any day `d` where `d <= expires_day`. A
duration of zero means until explicitly cleared.

Because day D is already committed when a command applies, and because the
consequence phase runs after that day's production, this gives exactly
`duration_days` affected days in both the cases the chain uses:

- A choice taken between days D and D+1 affects days D+1 through D+duration.
- An effect executed in the consequence phase of day D affects days D+1 through
  D+duration.

An effect executed in the due-work phase of day D affects day D as well, giving one
extra affected day. The accident chain schedules no durated modifier that way, so
this does not arise in the shipped content, and the resolver's behaviour is
consistent rather than special-cased.

## Consequences

"Repair dates are absolute ready days" holds: a scheduled restoration lands in the
due-work phase of its exact day, and saving or reopening a window cannot shorten it.
`t18_outcome_idempotency` checks this across a reload.

The crew-replacement window is two days of stopped production, visible in the
facility's explanation as a `crew_swap` effect rather than as an unexplained zero.
