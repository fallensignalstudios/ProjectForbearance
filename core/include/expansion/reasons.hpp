// Typed reason identifiers. Every rejection, bottleneck and ledger entry names
// one of these rather than a free-text string, so the interface can explain a
// cause instead of calling every loss "efficiency" (TDD 8.4, 10.4, 5.1).
#pragma once

namespace expansion::reason {

// Command rejections
inline constexpr const char* kOk = "ok";
inline constexpr const char* kRevisionMismatch = "revision_mismatch";
inline constexpr const char* kUnknownCommand = "unknown_command";
inline constexpr const char* kUnknownPlanet = "unknown_planet";
inline constexpr const char* kUnknownFacility = "unknown_facility";
inline constexpr const char* kUnknownRecipe = "unknown_recipe";
inline constexpr const char* kUnknownPolicy = "unknown_policy";
inline constexpr const char* kUnknownEvent = "unknown_event";
inline constexpr const char* kUnknownChoice = "unknown_choice";
inline constexpr const char* kUnknownResource = "unknown_resource";
inline constexpr const char* kScenarioFinished = "scenario_finished";
inline constexpr const char* kInvalidArgument = "invalid_argument";
inline constexpr const char* kNotEnoughWorkers = "not_enough_workers";
inline constexpr const char* kOverstaffed = "overstaffed";
inline constexpr const char* kWorkerInTransit = "worker_in_transit";
inline constexpr const char* kNoFreeSlot = "no_free_slot";
inline constexpr const char* kNotBuildable = "not_buildable";
inline constexpr const char* kAlreadyPresent = "already_present";
inline constexpr const char* kInsufficientStock = "insufficient_stock";
inline constexpr const char* kInsufficientCapacity = "insufficient_capacity";
inline constexpr const char* kNotUnderConstruction = "not_under_construction";
inline constexpr const char* kConditionTooHigh = "condition_too_high";
inline constexpr const char* kServiceInProgress = "service_in_progress";
inline constexpr const char* kServiceCooldown = "service_cooldown";
inline constexpr const char* kPolicyMinimumNotMet = "policy_minimum_not_met";
inline constexpr const char* kPolicyCooldown = "policy_cooldown";
inline constexpr const char* kPolicyAlreadyActive = "policy_already_active";
inline constexpr const char* kShipUnavailable = "ship_unavailable";
inline constexpr const char* kShipNotDocked = "ship_not_docked";
inline constexpr const char* kShipHasCargo = "ship_has_cargo";
inline constexpr const char* kDwellNotElapsed = "dwell_not_elapsed";
inline constexpr const char* kInsufficientFuel = "insufficient_fuel";
inline constexpr const char* kNoSpaceport = "no_spaceport";
inline constexpr const char* kDestinationNotColonised = "destination_not_colonised";
inline constexpr const char* kZeroCargoNotAuthorised = "zero_cargo_not_authorised";
inline constexpr const char* kExpeditionAlreadyLaunched = "expedition_already_launched";
inline constexpr const char* kTargetAlreadyColonised = "target_already_colonised";
inline constexpr const char* kEventNotOpen = "event_not_open";
inline constexpr const char* kChoiceUnaffordable = "choice_unaffordable";
inline constexpr const char* kChoiceAlreadyApplied = "choice_already_applied";
inline constexpr const char* kMandateNotIssued = "mandate_not_issued";
inline constexpr const char* kMandateResolved = "mandate_resolved";
inline constexpr const char* kNegotiationWindowClosed = "negotiation_window_closed";
inline constexpr const char* kManifestExceedsDemand = "manifest_exceeds_demand";
inline constexpr const char* kManifestExceedsCapacity = "manifest_exceeds_capacity";
inline constexpr const char* kInsufficientHandling = "insufficient_handling";
inline constexpr const char* kDepartureTooLate = "departure_too_late";
inline constexpr const char* kNoSurvivalEmergency = "no_survival_emergency";
inline constexpr const char* kReliefAlreadyUsed = "relief_already_used";
inline constexpr const char* kReliefPending = "relief_pending";
inline constexpr const char* kReserveFloorNotOverridden = "reserve_floor_not_overridden";

// Production bottlenecks
inline constexpr const char* kFullThroughput = "full_throughput";
inline constexpr const char* kIdleByChoice = "idle_by_choice";
inline constexpr const char* kServicingDowntime = "servicing_downtime";
inline constexpr const char* kUnderConstruction = "under_construction";
inline constexpr const char* kCommissioning = "commissioning";
inline constexpr const char* kNoStaff = "no_staff";
inline constexpr const char* kUnderstaffed = "understaffed";
inline constexpr const char* kPoorHealth = "poor_health";
inline constexpr const char* kFatigued = "fatigued";
inline constexpr const char* kPoorCondition = "poor_condition";
inline constexpr const char* kMissingInput = "missing_input";
inline constexpr const char* kPowerShortfall = "power_shortfall";
inline constexpr const char* kOutputStoreFull = "output_store_full";
inline constexpr const char* kActiveModifier = "active_modifier";

// Ledger causes
inline constexpr const char* kCauseProduction = "production";
inline constexpr const char* kCauseRecipeInput = "recipe_input";
inline constexpr const char* kCauseGeneration = "power_generation";
inline constexpr const char* kCauseMaintenance = "maintenance";
inline constexpr const char* kCauseCivilian = "civilian_need";
inline constexpr const char* kCauseConstructionEscrow = "construction_escrow";
inline constexpr const char* kCauseConstructionConsume = "construction_consume";
inline constexpr const char* kCauseConstructionRefund = "construction_refund";
inline constexpr const char* kCauseShipLoad = "ship_load";
inline constexpr const char* kCauseShipUnload = "ship_unload";
inline constexpr const char* kCauseShipFuel = "ship_fuel";
inline constexpr const char* kCauseShipMaintenance = "ship_maintenance";
inline constexpr const char* kCauseExpeditionLaunch = "expedition_launch";
inline constexpr const char* kCauseExpeditionTransit = "expedition_transit";
inline constexpr const char* kCauseExpeditionDeliver = "expedition_deliver";
inline constexpr const char* kCauseExpeditionOutfitting = "expedition_outfitting";
inline constexpr const char* kCauseMandateLoad = "mandate_load";
inline constexpr const char* kCauseMandateAccepted = "mandate_accepted";
inline constexpr const char* kCauseMandatePropulsion = "mandate_propulsion";
inline constexpr const char* kCauseReliefGrant = "relief_grant";
inline constexpr const char* kCauseEventCost = "event_cost";
inline constexpr const char* kCauseServiceCost = "service_cost";

}  // namespace expansion::reason

namespace expansion::account {

inline constexpr const char* kExternal = "external";
inline constexpr const char* kSinkCivilian = "sink:civilian";
inline constexpr const char* kSinkMaintenance = "sink:maintenance";
inline constexpr const char* kSinkRecipe = "sink:recipe";
inline constexpr const char* kSinkConstruction = "sink:construction";
inline constexpr const char* kSinkPropulsion = "sink:propulsion";
inline constexpr const char* kSinkOutfitting = "sink:outfitting";
inline constexpr const char* kSinkEvent = "sink:event";
inline constexpr const char* kSourceRecipe = "source:recipe";
inline constexpr const char* kFront = "front";

}  // namespace expansion::account
