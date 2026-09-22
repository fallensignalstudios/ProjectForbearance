// The one object the game holds: the authoritative session, its clock, and the
// change notifications a UMG host binds to.
//
// NOT COMPILED HERE. See docs/unreal_integration.md.
//
// It is a UGameInstanceSubsystem because a session outlives a level and there is
// exactly one of it. Nothing about the simulation is replicated, nothing is
// ticked per facility, and no widget reads simulation state directly: a widget
// receives a view struct when OnChanged says its panel moved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"

#include "ExpansionTypes.h"

#include "ExpansionSessionSubsystem.generated.h"

// Fired once per committed change, naming the panels that moved. A widget binds
// this and rebinds only its own fields.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FExpansionChanged, const FExpansionChangeSet&, Changes);

// Fired when a day has been resolved, for a one-off presentation beat: a report,
// a sound, an animation. It is not where panels refresh; OnChanged is.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FExpansionDayResolved, int64, Day, bool, bPausedForDecision);

UCLASS()
class EXPANSIONUE_API UExpansionSessionSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
    GENERATED_BODY()

public:
    // --- UGameInstanceSubsystem ---
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // --- FTickableGameObject ---
    // The only thing the tick does is hand the frame delta to the scheduler and
    // call StepDay for each whole day it reports. No economic quantity is ever
    // derived from a frame delta.
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickable() const override;
    virtual bool IsTickableWhenPaused() const override { return false; }

    // --- Lifecycle ---

    // Loads the shipped catalog. In an editor build this reads the repository's
    // content directory; in a packaged build it reads the pak file. Both paths
    // hash to the same catalog, which is what makes a save portable between them.
    UFUNCTION(BlueprintCallable, Category = "Expansion|Lifecycle")
    FExpansionResult LoadCatalog();

    UFUNCTION(BlueprintCallable, Category = "Expansion|Lifecycle")
    FExpansionResult NewSession(FName ScenarioId, FName FactionId, int64 Seed);

    UFUNCTION(BlueprintCallable, Category = "Expansion|Lifecycle")
    FExpansionResult SaveToSlot(int32 Slot);

    UFUNCTION(BlueprintCallable, Category = "Expansion|Lifecycle")
    FExpansionResult LoadFromSlot(int32 Slot);

    UFUNCTION(BlueprintPure, Category = "Expansion|Lifecycle")
    bool HasSession() const;

    // --- Time (TDD 5.3) ---

    UFUNCTION(BlueprintCallable, Category = "Expansion|Time")
    void SetSpeed(EExpansionSimSpeed Speed);

    UFUNCTION(BlueprintPure, Category = "Expansion|Time")
    EExpansionSimSpeed GetSpeed() const;

    // 0..1 through the current day, for a progress ring. Safe to read per frame:
    // it is arithmetic on the scheduler's accumulator and touches no state.
    UFUNCTION(BlueprintPure, Category = "Expansion|Time")
    float GetProgressToNextDay() const;

    // Resolves one day immediately, whatever the clock is doing. This is the
    // "advance a day" button, and it is the only way to advance while paused.
    UFUNCTION(BlueprintCallable, Category = "Expansion|Time")
    FExpansionResult StepDay();

    // --- Views ---
    //
    // A view is a copy. Call it when OnChanged says the panel moved, not every
    // frame: assembling a planet view walks that world's stores and facilities.

    UFUNCTION(BlueprintCallable, Category = "Expansion|Views")
    FExpansionSectorView GetSectorView() const;

    UFUNCTION(BlueprintCallable, Category = "Expansion|Views")
    FExpansionPlanetView GetPlanetView(FName PlanetId) const;

    UFUNCTION(BlueprintCallable, Category = "Expansion|Views")
    FExpansionFacilityCard GetFacilityCard(int64 FacilityId) const;

    UFUNCTION(BlueprintCallable, Category = "Expansion|Views")
    FExpansionFreightView GetFreightView() const;

    UFUNCTION(BlueprintCallable, Category = "Expansion|Views")
    TArray<FExpansionDecisionCard> GetOpenDecisions() const;

    UFUNCTION(BlueprintCallable, Category = "Expansion|Views")
    TArray<FExpansionHistoryEntry> GetHistory(int32 MaxEntries) const;

    // --- Commands (TDD 5.1) ---
    //
    // A command applies atomically between days, including while paused. Each
    // returns the typed reason on rejection, so a widget can say what is short
    // by how much rather than greying a button out.

    UFUNCTION(BlueprintCallable, Category = "Expansion|Commands")
    FExpansionResult AssignWorkers(FName PlanetId, int64 FromFacilityId, int64 ToFacilityId, int32 Count);

    UFUNCTION(BlueprintCallable, Category = "Expansion|Commands")
    FExpansionResult SetProductionPriority(int64 FacilityId, int32 PriorityBand);

    UFUNCTION(BlueprintCallable, Category = "Expansion|Commands")
    FExpansionResult SetFacilityIdle(int64 FacilityId, bool bIdle);

    UFUNCTION(BlueprintCallable, Category = "Expansion|Commands")
    FExpansionResult StartConstruction(FName PlanetId, FName FacilityId, FName RecipeId);

    UFUNCTION(BlueprintCallable, Category = "Expansion|Commands")
    FExpansionResult CancelConstruction(int64 FacilityId);

    UFUNCTION(BlueprintCallable, Category = "Expansion|Commands")
    FExpansionResult ServiceFacility(int64 FacilityId);

    UFUNCTION(BlueprintCallable, Category = "Expansion|Commands")
    FExpansionResult SelectPolicy(FName PlanetId, FName PolicyId);

    UFUNCTION(BlueprintCallable, Category = "Expansion|Commands")
    FExpansionResult AuthoriseDeparture(bool bAllowEmpty);

    UFUNCTION(BlueprintCallable, Category = "Expansion|Commands")
    FExpansionResult LaunchColonization();

    UFUNCTION(BlueprintCallable, Category = "Expansion|Commands")
    FExpansionResult ResolveEvent(int64 EventInstanceId, FName ChoiceId);

    UFUNCTION(BlueprintCallable, Category = "Expansion|Commands")
    FExpansionResult RequestRelief(FName PlanetId);

    // --- Formatting ---
    //
    // A widget formats a number by asking the simulation, never by casting to
    // float. These are the two conversions a widget is allowed to do.

    UFUNCTION(BlueprintPure, Category = "Expansion|Format")
    static FText FormatQuantity(int64 MilliUnits);

    UFUNCTION(BlueprintPure, Category = "Expansion|Format")
    static FText FormatPercent(int32 BasisPoints);

    // --- Notifications ---

    UPROPERTY(BlueprintAssignable, Category = "Expansion")
    FExpansionChanged OnChanged;

    UPROPERTY(BlueprintAssignable, Category = "Expansion")
    FExpansionDayResolved OnDayResolved;

private:
    // Publishes a change set if anything moved. Called after every accepted
    // command and every resolved day, and never from Tick unless one of those
    // happened.
    void PublishChanges();

    // The simulation is held behind a pointer so this header stays free of
    // simulation includes: an engine header and a std:: header in the same
    // translation unit is a fight nobody needs to have. The definition lives in
    // ExpansionSessionSubsystem.cpp.
    struct FSimulation;
    TUniquePtr<FSimulation> Sim;
};
