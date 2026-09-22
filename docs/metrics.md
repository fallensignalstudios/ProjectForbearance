# Readable metrics

A data-authored condition reads committed facts through `CompareMetric`. Only the
names below resolve; anything else fails catalog validation rather than silently
evaluating to zero.

## Planet scope

Needs a planet id.

| Metric | Meaning |
| --- | --- |
| `population` | Residents on the world |
| `workers_total` | Working-age workers belonging to the world |
| `workers_reserve` | Workers currently in Reserve |
| `health_bp` | Health, 0..10000 |
| `stability_bp` | Public stability, 0..10000 |
| `fatigue_bp` | Fatigue, 0..10000 |
| `food_fulfilment_bp` | Food fulfilment on the last resolved day |
| `water_fulfilment_bp` | Water fulfilment on the last resolved day |
| `power_fulfilment_bp` | Residential power fulfilment on the last resolved day |
| `housing_fulfilment_bp` | Usable housing against population |
| `clinic_coverage_bp` | Staffed clinic capacity against population |
| `survival_emergency_days` | The longer of the Food and Water emergency counters |
| `colonised` | 1 when the world is settled |
| `housing_capacity` | People housed |
| `clinic_capacity` | People covered at the day's actual service level |
| `free_slots` | Unoccupied planetary slots |
| `stock_<resource>` | On-hand quantity in milli-units |
| `available_<resource>` | On-hand minus valid reservations |

## Facility scope

Needs a facility instance.

| Metric | Meaning |
| --- | --- |
| `condition_bp` | Machinery condition, 2500..10000 |
| `assigned_workers` | Workers staffing the facility |
| `actual_throughput_bp` | The run factor achieved on the last resolved day |
| `desired_throughput_bp` | The run factor the facility wanted |
| `operating` | 1 when active, not idle, not being serviced, and it produced something |

## Sector scope

| Metric | Meaning |
| --- | --- |
| `day` | The last committed day |
| `adherence` | Faction adherence, 0..100 |
| `missed_colonial_food_manifests` | Count of scheduled colonial Food shipments missed |
| `mandate_issued`, `mandate_resolved` | 1 or 0 |
| `mandate_days_remaining` | Deadline minus the current day |
| `colony_launched`, `colony_founded` | 1 or 0 |
