// Blueprint-facing mirrors of the simulation's view models.
//
// NOT COMPILED HERE. See docs/unreal_integration.md.
//
// These exist because USTRUCT cannot wrap std::string or std::int64_t directly,
// and because a Blueprint must not hold a pointer into simulation state. Each
// struct is a copy taken at a change notification and is valid until the next
// one.
//
// Quantities cross as int64 in the simulation's own units -- milli-units for a
// commodity, basis points for a ratio -- alongside the string the simulation
// formatted. A widget prints the string. It must never divide the integer by
// 1000.0f to make its own: that reintroduces the floating point the whole
// economy is built to avoid, and two widgets would disagree in the last digit.
#pragma once

#include "CoreMinimal.h"
#include "ExpansionTypes.generated.h"

UENUM(BlueprintType)
enum class EExpansionSimSpeed : uint8
{
    Paused  UMETA(DisplayName = "Paused"),
    X1      UMETA(DisplayName = "1x"),
    X2      UMETA(DisplayName = "2x"),
    X4      UMETA(DisplayName = "4x"),
    X8      UMETA(DisplayName = "8x"),
};

UENUM(BlueprintType)
enum class EExpansionLifecycle : uint8
{
    Running,
    Complete,
    Compromised,
    Failed,
    Surrendered,
};

UENUM(BlueprintType)
enum class EExpansionErrorCode : uint8
{
    Ok,
    InvalidContent,
    InvalidArgument,
    NotFound,
    IncompatibleSave,
    CorruptSave,
    Overflow,
    InvariantViolated,
    Internal,
};

// The result of an operation that can fail. Every simulation call returns one of
// these rather than raising: a throw crossing into engine code is a crash in a
// packaged build.
USTRUCT(BlueprintType)
struct FExpansionResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    bool bOk = false;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    EExpansionErrorCode Code = EExpansionErrorCode::Ok;

    // A typed identifier, safe to branch on. Never shown to a player.
    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FName Reason;

    // The specifics, in plain language: the exact deficit, the missing id.
    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FText Detail;
};

// A number and the text the simulation rendered for it.
USTRUCT(BlueprintType)
struct FExpansionQuantity
{
    GENERATED_BODY()

    // Milli-units, or basis points for a ratio. Exact.
    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int64 Value = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FText Text;
};

USTRUCT(BlueprintType)
struct FExpansionStockRow
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FName ResourceId;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FText Label;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FExpansionQuantity OnHand;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FExpansionQuantity Reserved;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FExpansionQuantity Available;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FExpansionQuantity Capacity;

    // 0..10000. A progress bar divides by 10000, not by a magic number.
    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int32 FillBasisPoints = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FExpansionQuantity ProducedToday;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FExpansionQuantity ConsumedToday;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int64 NetToday = 0;

    // Days of cover at today's drain, or -1 when the store is not falling. A
    // widget must not show "-1 days"; it shows no countdown at all.
    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int64 DaysOfCover = -1;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    bool bFull = false;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    bool bShortageOpen = false;
};

USTRUCT(BlueprintType)
struct FExpansionNeedRow
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FName NeedId;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FText Label;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int32 FulfilmentBasisPoints = 10000;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FExpansionQuantity Demand;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FExpansionQuantity Served;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    bool bMet = false;
};

USTRUCT(BlueprintType)
struct FExpansionConcern
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FName PlanetId;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FText Headline;

    // The typed cause. A warning without one is not actionable, so this is never
    // empty and never a summary word like "efficiency".
    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FName Cause;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FText CauseText;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int64 DaysRemaining = -1;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int32 Severity = 0;
};

USTRUCT(BlueprintType)
struct FExpansionSectorView
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int64 Day = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    EExpansionLifecycle Lifecycle = EExpansionLifecycle::Running;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FText LifecycleLabel;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FText FactionLabel;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int32 Adherence = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int64 EvaluationDay = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int64 DaysToEvaluation = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    bool bPausedForDecision = false;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int32 OpenDecisions = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    TArray<FExpansionConcern> Concerns;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    TArray<FName> PlanetIds;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    bool bMandateIssued = false;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FText MandateStatusLabel;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int64 MandateDeadlineDay = -1;
};

USTRUCT(BlueprintType)
struct FExpansionPlanetView
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FName PlanetId;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FText Label;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    bool bColonised = false;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int64 Population = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int32 WorkersAssigned = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int32 WorkersReserve = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int32 WorkersTransitioning = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int32 WorkersCrew = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int32 HealthBasisPoints = 10000;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int32 StabilityBasisPoints = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int32 FatigueBasisPoints = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FText PolicyLabel;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    bool bSurvivalEmergency = false;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FExpansionQuantity PowerGenerated;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FExpansionQuantity PowerUsed;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FExpansionQuantity PowerSpare;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    TArray<FExpansionNeedRow> Needs;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    TArray<FExpansionStockRow> Stores;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    TArray<int64> FacilityIds;
};

USTRUCT(BlueprintType)
struct FExpansionFacilityCard
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int64 Id = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FName FacilityId;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FName PlanetId;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FText Label;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FText StateLabel;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int32 AssignedWorkers = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int32 RequiredWorkers = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int32 ConditionBasisPoints = 10000;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int32 PriorityBand = 30;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    bool bIdle = false;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    bool bUnderConstruction = false;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int32 ConstructionProgressBasisPoints = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int32 ThroughputBasisPoints = 0;

    // The typed cause of the day's throughput, and its plain-language form. A
    // card shows the text; a style rule branches on the id.
    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FName ReasonId;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FText ReasonText;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    bool bPowerLimited = false;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    bool bLabourLimited = false;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    TArray<FText> MissingInputs;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    TArray<FText> BlockedOutputs;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    TArray<FExpansionStockRow> Outputs;
};

// The freight leg: one ship, one route (TDD 10).
USTRUCT(BlueprintType)
struct FExpansionFreightView
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FText PhaseLabel;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FText LocationLabel;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FText DestinationLabel;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int64 DepartureDay = -1;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int64 ArrivalDay = -1;

    // Days until arrival, or -1 when the freighter is not in transit.
    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int64 DaysRemaining = -1;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int64 NextScheduledDepartureDay = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    bool bRouteEnabled = false;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    bool bDepartureAuthorised = false;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FExpansionQuantity FuelInTank;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FExpansionQuantity CargoVolume;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    TArray<FExpansionStockRow> Cargo;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    TArray<FExpansionStockRow> OutboundTargets;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    TArray<FExpansionStockRow> ReturnTargets;

    // Scheduled colonial shipments the freighter was unavailable for. It is a
    // count of broken promises, and the colony feels each one.
    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int32 MissedManifests = 0;
};

USTRUCT(BlueprintType)
struct FExpansionDecisionOption
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FName ChoiceId;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FText Label;

    // What it costs, empty when it costs nothing. Never the string "0".
    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FText CostSummary;

    // An option that cannot be paid for stays visible and disabled. Hiding it
    // hides the reason it is out of reach.
    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    bool bAffordable = true;
};

USTRUCT(BlueprintType)
struct FExpansionDecisionCard
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int64 Id = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FName EventId;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FName PlanetId;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int64 FacilityId = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FText Title;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FText Body;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FText SubjectLabel;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int64 DeadlineDay = -1;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int64 DaysRemaining = -1;

    // True when the decision holds the calendar until it is answered.
    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    bool bCritical = false;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    TArray<FExpansionDecisionOption> Options;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    TArray<FText> Because;
};

USTRUCT(BlueprintType)
struct FExpansionHistoryEntry
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int64 Day = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FText Text;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    FName PlanetId;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int32 Priority = 2;
};

// Which panels a committed change invalidated. A host rebinds exactly these and
// leaves the rest alone: no per-frame attribute binding, no ticking widget.
USTRUCT(BlueprintType)
struct FExpansionChangeSet
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    int64 Day = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    bool bFirstPublish = false;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    bool bSector = false;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    bool bFreight = false;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    bool bDecisions = false;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    bool bHistory = false;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    TArray<FName> Planets;

    UPROPERTY(BlueprintReadOnly, Category = "Expansion")
    TArray<int64> Facilities;
};
