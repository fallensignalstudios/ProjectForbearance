# 0002 — Five effect kinds beyond the design's whitelist

Status: accepted.

## Context

Section 4.3 lists eight effect kinds: `TransferResource`, `ConsumeResource`,
`ApplyModifier`, `ScheduleEffect`, `SetFlag`, `AdjustAdherence`,
`AdjustStabilityTarget` and `OpenEvent`. It also says the supplied JSON is a
numerical seed and that the programmer must add the complete schemas and all event
and policy effect definitions before declaring catalog coverage complete.

Section 13.2 then specifies an accident chain that those eight kinds cannot express:

- "Restore condition to 80%" needs a way to set condition.
- "Clear the accident modifier" needs a way to remove a modifier by tag.
- "Replace crews: requires ten Reserve workers; disable production for two days
  while swapping ten assigned staff" needs a worker swap that never adds net
  workforce.
- "If restored, close it with a successful-prevention fact" needs a way to close an
  instance.
- Section 14.4's relief contract is "an explicit external grant recorded in the
  ledger", which is a different thing from moving stock between owned accounts.

## Decision

Five additional typed effect kinds, and seven additional event fields, all data:

| Addition | Purpose |
| --- | --- |
| `SetConditionAtLeast` | Raise a facility's condition to a floor, never lower it |
| `ClearModifiersByTag` | Remove active modifiers carrying a tag |
| `ReplaceCrews` | Swap equal numbers of workers, with the outgoing staff in transition until the swap completes |
| `CloseEvent` | Close an open instance or a whole chain on a planet |
| `GrantExternal` | Add stock from the `external` account, recorded as a grant |
| `condition_resolved` + `on_resolve` + `news_template_resolved` | A warning that closes successfully when the condition it warned about is fixed |
| `expire_keeps_open` | On expiry, drop the deadline but keep the remedies selectable |
| `sector_unique` | At most one open instance across the sector, for Faction Review |
| `applies_to_facility` | Restrict a facility-scope trigger to one facility type, so a Safety Warning applies to Extraction Sites and not to clinics |
| choice `resolution: keep_open` | Deferral leaves the other remedies selectable |
| choice `cancels_scheduled_from` | A later paid remedy cancels a pending consequence that has not opened |
| effect `faction` | A consequence that differs by faction stays authored data rather than a branch in code |

## Consequences

The whole P1 chain is authored in `content/events/mining_safety.json` with no
chain-specific code. Conditions remain strictly the seven whitelisted kinds; nothing
here introduces executable content.

The cost is that the design's Section 4.3 list is no longer the complete effect
vocabulary. Any future review of that section should either adopt these five or
say how the specified chain should be expressed instead.
