// NOT COMPILED HERE. See docs/unreal_integration.md for what is verified and
// what is not. The conversions below are mechanical; the behaviour they wrap is
// covered by the headless suite, which the engine build compiles from the same
// sources.

#include "ExpansionSessionSubsystem.h"

#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "expansion/api.hpp"
#include "expansion/content_source.hpp"
#include "expansion/host_services.hpp"
#include "expansion/text.hpp"
#include "expansion/view_models.hpp"

namespace
{
    FText AsText(const std::string& In) { return FText::FromString(FString(UTF8_TO_TCHAR(In.c_str()))); }
    FName AsName(const std::string& In) { return FName(*FString(UTF8_TO_TCHAR(In.c_str()))); }
    std::string AsStd(FName In) { return std::string(TCHAR_TO_UTF8(*In.ToString())); }

    EExpansionErrorCode AsCode(expansion::ErrorCode Code)
    {
        using expansion::ErrorCode;
        switch (Code)
        {
        case ErrorCode::Ok:                return EExpansionErrorCode::Ok;
        case ErrorCode::InvalidContent:    return EExpansionErrorCode::InvalidContent;
        case ErrorCode::InvalidArgument:   return EExpansionErrorCode::InvalidArgument;
        case ErrorCode::NotFound:          return EExpansionErrorCode::NotFound;
        case ErrorCode::IncompatibleSave:  return EExpansionErrorCode::IncompatibleSave;
        case ErrorCode::CorruptSave:       return EExpansionErrorCode::CorruptSave;
        case ErrorCode::Overflow:          return EExpansionErrorCode::Overflow;
        case ErrorCode::InvariantViolated: return EExpansionErrorCode::InvariantViolated;
        case ErrorCode::Internal:          return EExpansionErrorCode::Internal;
        }
        return EExpansionErrorCode::Internal;
    }

    FExpansionResult Ok()
    {
        FExpansionResult R;
        R.bOk = true;
        R.Reason = TEXT("ok");
        return R;
    }

    FExpansionResult Fail(const expansion::api::Error& Error)
    {
        FExpansionResult R;
        R.bOk = false;
        R.Code = AsCode(Error.code);
        R.Reason = FName(Error.id());
        R.Detail = AsText(Error.message);
        return R;
    }

    FExpansionResult FromCommand(const expansion::CommandResult& Result)
    {
        FExpansionResult R;
        R.bOk = Result.accepted;
        R.Reason = AsName(Result.reason);
        // The detail is what makes a rejection useful: "short by 4.2 machinery",
        // not a greyed-out button.
        R.Detail = AsText(Result.detail.empty() ? expansion::text::reason_text(Result.reason) : Result.detail);
        R.Code = Result.accepted ? EExpansionErrorCode::Ok : EExpansionErrorCode::InvalidArgument;
        return R;
    }

    FExpansionQuantity AsQuantity(const expansion::view::Quantity& Q)
    {
        FExpansionQuantity Out;
        Out.Value = Q.value;
        Out.Text = AsText(Q.text);
        return Out;
    }

    FExpansionStockRow AsStockRow(const expansion::view::StockRow& Row)
    {
        FExpansionStockRow Out;
        Out.ResourceId = AsName(Row.resource_id);
        Out.Label = AsText(Row.label);
        Out.OnHand = AsQuantity(Row.on_hand);
        Out.Reserved = AsQuantity(Row.reserved);
        Out.Available = AsQuantity(Row.available);
        Out.Capacity = AsQuantity(Row.capacity);
        Out.FillBasisPoints = static_cast<int32>(Row.fill_bp);
        Out.ProducedToday = AsQuantity(Row.produced_today);
        Out.ConsumedToday = AsQuantity(Row.consumed_today);
        Out.NetToday = Row.net_today;
        Out.DaysOfCover = Row.days_of_cover;
        Out.bFull = Row.full;
        Out.bShortageOpen = Row.shortage_open;
        return Out;
    }

    expansion::host::SimSpeed AsSimSpeed(EExpansionSimSpeed Speed)
    {
        using expansion::host::SimSpeed;
        switch (Speed)
        {
        case EExpansionSimSpeed::Paused: return SimSpeed::Paused;
        case EExpansionSimSpeed::X1:     return SimSpeed::X1;
        case EExpansionSimSpeed::X2:     return SimSpeed::X2;
        case EExpansionSimSpeed::X4:     return SimSpeed::X4;
        case EExpansionSimSpeed::X8:     return SimSpeed::X8;
        }
        return SimSpeed::Paused;
    }

    EExpansionSimSpeed FromSimSpeed(expansion::host::SimSpeed Speed)
    {
        using expansion::host::SimSpeed;
        switch (Speed)
        {
        case SimSpeed::Paused: return EExpansionSimSpeed::Paused;
        case SimSpeed::X1:     return EExpansionSimSpeed::X1;
        case SimSpeed::X2:     return EExpansionSimSpeed::X2;
        case SimSpeed::X4:     return EExpansionSimSpeed::X4;
        case SimSpeed::X8:     return EExpansionSimSpeed::X8;
        }
        return EExpansionSimSpeed::Paused;
    }

    // Reads the shipped definition files through the engine's file layer, so the
    // same code serves a loose editor build and a packaged pak. The core never
    // opens a file itself (TDD 19.4), which is the whole reason this class exists.
    class FEngineContentSource final : public expansion::ContentSource
    {
    public:
        explicit FEngineContentSource(const FString& InRoot) : Root(InRoot) {}

        std::vector<expansion::ContentFile> read_all() const override
        {
            std::vector<expansion::ContentFile> Files;
            TArray<FString> Found;
            IFileManager::Get().FindFilesRecursive(Found, *Root, TEXT("*.json"), true, false);
            Found.Sort();   // the catalog hash is order-independent, but a stable order keeps logs readable
            for (const FString& Absolute : Found)
            {
                FString Contents;
                if (!FFileHelper::LoadFileToString(Contents, *Absolute))
                {
                    // A definition file that cannot be read is a content error, not
                    // a missing file to skip: skipping one would silently change the
                    // catalog hash and make every save incompatible.
                    continue;
                }
                FString Relative = Absolute;
                FPaths::MakePathRelativeTo(Relative, *(Root / TEXT("")));
                expansion::ContentFile File;
                File.path = std::string(TCHAR_TO_UTF8(*Relative));
                File.bytes = std::string(TCHAR_TO_UTF8(*Contents));
                Files.push_back(std::move(File));
            }
            return Files;
        }

        std::string describe() const override { return std::string(TCHAR_TO_UTF8(*Root)); }

    private:
        FString Root;
    };
}  // namespace

// The simulation's state, kept out of the header so no engine header and no
// std:: header have to agree on anything.
struct UExpansionSessionSubsystem::FSimulation
{
    expansion::api::CatalogPtr Catalog;
    expansion::api::SessionPtr Session;
    expansion::host::TickScheduler Clock;
    expansion::host::ChangeTracker Tracker;
};

void UExpansionSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Sim = MakeUnique<FSimulation>();
}

void UExpansionSessionSubsystem::Deinitialize()
{
    Sim.Reset();
    Super::Deinitialize();
}

bool UExpansionSessionSubsystem::IsTickable() const
{
    return Sim.IsValid() && Sim->Session != nullptr && Sim->Clock.running();
}

TStatId UExpansionSessionSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UExpansionSessionSubsystem, STATGROUP_Tickables);
}

void UExpansionSessionSubsystem::Tick(float DeltaTime)
{
    if (!IsTickable())
    {
        return;
    }
    const int Steps = Sim->Clock.advance(static_cast<double>(DeltaTime));
    for (int i = 0; i < Steps; ++i)
    {
        const auto Day = expansion::api::step_day(*Sim->Session);
        if (!Day.ok())
        {
            // A day that cannot be resolved stops the clock rather than retrying
            // sixty times a second.
            Sim->Clock.set_speed(expansion::host::SimSpeed::Paused);
            break;
        }
        OnDayResolved.Broadcast(Day.value().day, Day.value().paused_for_decision);
        if (Day.value().paused_for_decision)
        {
            // A critical decision holds the calendar. The host releases the hold
            // when the player answers.
            Sim->Clock.hold_for_decision();
            break;
        }
    }
    if (Steps > 0)
    {
        PublishChanges();
    }
}

void UExpansionSessionSubsystem::PublishChanges()
{
    if (!Sim.IsValid() || Sim->Session == nullptr)
    {
        return;
    }
    const expansion::host::ChangeSet Changes = Sim->Tracker.publish(*Sim->Session);
    if (!Changes.any())
    {
        return;   // nothing moved; do not wake a single widget
    }
    FExpansionChangeSet Out;
    Out.Day = Changes.day;
    Out.bFirstPublish = Changes.first_publish;
    Out.bSector = Changes.sector;
    Out.bFreight = Changes.freight;
    Out.bDecisions = Changes.decisions;
    Out.bHistory = Changes.history;
    for (const std::string& Planet : Changes.planets)
    {
        Out.Planets.Add(AsName(Planet));
    }
    for (expansion::InstanceId Id : Changes.facilities)
    {
        Out.Facilities.Add(static_cast<int64>(Id));
    }
    OnChanged.Broadcast(Out);
}

FExpansionResult UExpansionSessionSubsystem::LoadCatalog()
{
    // Editor and packaged builds both resolve to the plugin's Content/Definitions
    // directory; the packaged one is served out of the pak by the same file layer.
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ExpansionUE"));
    const FString Root = Plugin.IsValid()
        ? FPaths::Combine(Plugin->GetContentDir(), TEXT("Definitions"))
        : FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Definitions"));

    const FEngineContentSource Source(Root);
    auto Loaded = expansion::api::load_catalog(Source);
    if (!Loaded.ok())
    {
        return Fail(Loaded.error());
    }
    Sim->Catalog = Loaded.take();
    return Ok();
}

FExpansionResult UExpansionSessionSubsystem::NewSession(FName ScenarioId, FName FactionId, int64 Seed)
{
    if (Sim->Catalog == nullptr)
    {
        const FExpansionResult Load = LoadCatalog();
        if (!Load.bOk)
        {
            return Load;
        }
    }
    auto Made = expansion::api::create_session(Sim->Catalog, AsStd(ScenarioId), AsStd(FactionId),
                                               static_cast<std::uint64_t>(Seed));
    if (!Made.ok())
    {
        return Fail(Made.error());
    }
    Sim->Session = Made.take();
    Sim->Tracker.forget();
    Sim->Clock.reset();
    Sim->Clock.release_decision_hold();
    PublishChanges();   // the first publish binds every panel
    return Ok();
}

bool UExpansionSessionSubsystem::HasSession() const
{
    return Sim.IsValid() && Sim->Session != nullptr;
}

void UExpansionSessionSubsystem::SetSpeed(EExpansionSimSpeed Speed)
{
    if (!Sim.IsValid())
    {
        return;
    }
    Sim->Clock.set_speed(AsSimSpeed(Speed));
    if (Speed != EExpansionSimSpeed::Paused)
    {
        // Choosing a speed is the player saying they have dealt with the decision.
        Sim->Clock.release_decision_hold();
    }
}

EExpansionSimSpeed UExpansionSessionSubsystem::GetSpeed() const
{
    return Sim.IsValid() ? FromSimSpeed(Sim->Clock.speed()) : EExpansionSimSpeed::Paused;
}

float UExpansionSessionSubsystem::GetProgressToNextDay() const
{
    return Sim.IsValid() ? static_cast<float>(Sim->Clock.progress_to_next_day()) : 0.0f;
}

FExpansionResult UExpansionSessionSubsystem::StepDay()
{
    if (!HasSession())
    {
        FExpansionResult R;
        R.Code = EExpansionErrorCode::InvalidArgument;
        R.Reason = TEXT("no_session");
        R.Detail = FText::FromString(TEXT("There is no session to advance."));
        return R;
    }
    auto Day = expansion::api::step_day(*Sim->Session);
    if (!Day.ok())
    {
        return Fail(Day.error());
    }
    OnDayResolved.Broadcast(Day.value().day, Day.value().paused_for_decision);
    if (Day.value().paused_for_decision)
    {
        Sim->Clock.hold_for_decision();
    }
    PublishChanges();
    return Ok();
}

FText UExpansionSessionSubsystem::FormatQuantity(int64 MilliUnits)
{
    return AsText(expansion::format_milli(MilliUnits));
}

FText UExpansionSessionSubsystem::FormatPercent(int32 BasisPoints)
{
    return AsText(expansion::format_bp_percent(static_cast<expansion::Bp>(BasisPoints)));
}

FExpansionSectorView UExpansionSessionSubsystem::GetSectorView() const
{
    FExpansionSectorView Out;
    if (!HasSession())
    {
        return Out;
    }
    const auto Built = expansion::view::sector(*Sim->Session);
    if (!Built.ok())
    {
        return Out;
    }
    const expansion::view::SectorView& V = Built.value();
    Out.Day = V.day;
    Out.Lifecycle = static_cast<EExpansionLifecycle>(static_cast<uint8>(V.lifecycle));
    Out.LifecycleLabel = AsText(V.lifecycle_label);
    Out.FactionLabel = AsText(V.faction_label);
    Out.Adherence = V.adherence;
    Out.EvaluationDay = V.evaluation_day;
    Out.DaysToEvaluation = V.days_to_evaluation;
    Out.bPausedForDecision = V.paused_for_decision;
    Out.OpenDecisions = V.open_decisions;
    Out.bMandateIssued = V.mandate_issued;
    Out.MandateStatusLabel = AsText(V.mandate_status_label);
    Out.MandateDeadlineDay = V.mandate_deadline_day;
    for (const expansion::read::Concern& C : V.concerns)
    {
        FExpansionConcern Concern;
        Concern.PlanetId = AsName(C.planet_id);
        Concern.Headline = AsText(C.headline);
        Concern.Cause = AsName(C.cause);
        Concern.CauseText = AsText(expansion::text::reason_text(C.cause));
        Concern.DaysRemaining = C.days_remaining;
        Concern.Severity = C.severity;
        Out.Concerns.Add(Concern);
    }
    for (const std::string& Planet : V.planet_ids)
    {
        Out.PlanetIds.Add(AsName(Planet));
    }
    return Out;
}

FExpansionPlanetView UExpansionSessionSubsystem::GetPlanetView(FName PlanetId) const
{
    FExpansionPlanetView Out;
    if (!HasSession())
    {
        return Out;
    }
    const auto Built = expansion::view::planet(*Sim->Session, AsStd(PlanetId));
    if (!Built.ok())
    {
        // An unknown world returns an empty view with its id intact rather than
        // nothing at all, so a widget still knows what it failed to draw.
        Out.PlanetId = PlanetId;
        return Out;
    }
    {
        const expansion::view::PlanetView& V = Built.value();
        Out.PlanetId = AsName(V.planet_id);
        Out.Label = AsText(V.label);
        Out.bColonised = V.colonised;
        Out.Population = V.population;
        Out.WorkersAssigned = V.workers_assigned;
        Out.WorkersReserve = V.workers_reserve;
        Out.WorkersTransitioning = V.workers_transitioning;
        Out.WorkersCrew = V.workers_crew;
        Out.HealthBasisPoints = static_cast<int32>(V.health_bp);
        Out.StabilityBasisPoints = static_cast<int32>(V.stability_bp);
        Out.FatigueBasisPoints = static_cast<int32>(V.fatigue_bp);
        Out.PolicyLabel = AsText(V.policy_label);
        Out.bSurvivalEmergency = V.survival_emergency;
        Out.PowerGenerated = AsQuantity(V.power_generated);
        Out.PowerUsed = AsQuantity(V.power_used);
        Out.PowerSpare = AsQuantity(V.power_spare);
        for (const expansion::view::NeedRow& N : V.needs)
        {
            FExpansionNeedRow Need;
            Need.NeedId = AsName(N.need_id);
            Need.Label = AsText(N.label);
            Need.FulfilmentBasisPoints = static_cast<int32>(N.fulfilment_bp);
            Need.Demand = AsQuantity(N.demand);
            Need.Served = AsQuantity(N.served);
            Need.bMet = N.met;
            Out.Needs.Add(Need);
        }
        for (const expansion::view::StockRow& Row : V.stores)
        {
            Out.Stores.Add(AsStockRow(Row));
        }
        for (expansion::InstanceId Id : V.facilities)
        {
            Out.FacilityIds.Add(static_cast<int64>(Id));
        }
    }
    return Out;
}

FExpansionFacilityCard UExpansionSessionSubsystem::GetFacilityCard(int64 FacilityId) const
{
    FExpansionFacilityCard Out;
    if (!HasSession())
    {
        return Out;
    }
    const auto Built = expansion::view::facility(*Sim->Session, static_cast<expansion::InstanceId>(FacilityId));
    if (!Built.ok())
    {
        Out.Id = FacilityId;
        return Out;
    }
    {
        const expansion::view::FacilityCard& C = Built.value();
        Out.Id = static_cast<int64>(C.id);
        Out.FacilityId = AsName(C.facility_id);
        Out.PlanetId = AsName(C.planet_id);
        Out.Label = AsText(C.label);
        Out.StateLabel = AsText(C.state_label);
        Out.AssignedWorkers = C.assigned_workers;
        Out.RequiredWorkers = C.required_workers;
        Out.ConditionBasisPoints = static_cast<int32>(C.condition_bp);
        Out.PriorityBand = C.priority_band;
        Out.bIdle = C.idle;
        Out.bUnderConstruction = C.under_construction;
        Out.ConstructionProgressBasisPoints = static_cast<int32>(C.construction_progress_bp);
        Out.ThroughputBasisPoints = static_cast<int32>(C.throughput_bp);
        Out.ReasonId = AsName(C.reason_id);
        Out.ReasonText = AsText(C.reason_text);
        Out.bPowerLimited = C.power_limited;
        Out.bLabourLimited = C.labour_limited;
        for (const std::string& Input : C.missing_inputs)
        {
            Out.MissingInputs.Add(AsText(Input));
        }
        for (const std::string& Output : C.blocked_outputs)
        {
            Out.BlockedOutputs.Add(AsText(Output));
        }
        for (const expansion::view::StockRow& Row : C.outputs)
        {
            Out.Outputs.Add(AsStockRow(Row));
        }
    }
    return Out;
}

FExpansionFreightView UExpansionSessionSubsystem::GetFreightView() const
{
    FExpansionFreightView Out;
    if (!HasSession())
    {
        return Out;
    }
    const auto Built = expansion::view::freight(*Sim->Session);
    if (!Built.ok())
    {
        return Out;
    }
    const expansion::view::FreightView& V = Built.value();
    Out.PhaseLabel = AsText(V.phase_label);
    Out.LocationLabel = AsText(V.location_label);
    Out.DestinationLabel = AsText(V.destination_label);
    Out.DepartureDay = V.departure_day;
    Out.ArrivalDay = V.arrival_day;
    Out.DaysRemaining = V.days_remaining;
    Out.NextScheduledDepartureDay = V.next_scheduled_departure_day;
    Out.bRouteEnabled = V.route_enabled;
    Out.bDepartureAuthorised = V.departure_authorised;
    Out.FuelInTank = AsQuantity(V.fuel_in_tank);
    Out.CargoVolume = AsQuantity(V.cargo_volume);
    for (const expansion::view::StockRow& Row : V.cargo)
    {
        Out.Cargo.Add(AsStockRow(Row));
    }
    for (const expansion::view::StockRow& Row : V.outbound_targets)
    {
        Out.OutboundTargets.Add(AsStockRow(Row));
    }
    for (const expansion::view::StockRow& Row : V.return_targets)
    {
        Out.ReturnTargets.Add(AsStockRow(Row));
    }
    Out.MissedManifests = V.missed_manifests;
    return Out;
}

TArray<FExpansionDecisionCard> UExpansionSessionSubsystem::GetOpenDecisions() const
{
    TArray<FExpansionDecisionCard> Out;
    if (!HasSession())
    {
        return Out;
    }
    const auto Built = expansion::view::decisions(*Sim->Session);
    if (!Built.ok())
    {
        return Out;
    }
    for (const expansion::view::DecisionCard& C : Built.value())
    {
        FExpansionDecisionCard Card;
        Card.Id = static_cast<int64>(C.id);
        Card.EventId = AsName(C.event_id);
        Card.PlanetId = AsName(C.planet_id);
        Card.FacilityId = static_cast<int64>(C.facility_id);
        Card.Title = AsText(C.title);
        Card.Body = AsText(C.body);
        Card.SubjectLabel = AsText(C.subject_label);
        Card.DeadlineDay = C.deadline_day;
        Card.DaysRemaining = C.days_remaining;
        Card.bCritical = C.critical;
        for (const expansion::view::DecisionOption& O : C.options)
        {
            FExpansionDecisionOption Option;
            Option.ChoiceId = AsName(O.choice_id);
            Option.Label = AsText(O.label);
            Option.CostSummary = AsText(O.cost_summary);
            Option.bAffordable = O.affordable;
            Card.Options.Add(Option);
        }
        for (const std::string& Sentence : C.because)
        {
            Card.Because.Add(AsText(Sentence));
        }
        Out.Add(Card);
    }
    return Out;
}

TArray<FExpansionHistoryEntry> UExpansionSessionSubsystem::GetHistory(int32 MaxEntries) const
{
    TArray<FExpansionHistoryEntry> Out;
    if (!HasSession())
    {
        return Out;
    }
    const auto Built = expansion::view::history(*Sim->Session, MaxEntries);
    if (!Built.ok())
    {
        return Out;
    }
    for (const expansion::view::HistoryEntry& E : Built.value())
    {
        FExpansionHistoryEntry Entry;
        Entry.Day = E.day;
        Entry.Text = AsText(E.text);
        Entry.PlanetId = AsName(E.planet_id);
        Entry.Priority = E.priority;
        Out.Add(Entry);
    }
    return Out;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

namespace
{
    // A command id has to be stable: replaying the same id returns the recorded
    // result rather than applying twice (TDD 5.1). The revision the command was
    // issued against makes it unique per state, which is exactly the property a
    // double-click on a button needs.
    std::string CommandId(const expansion::Session& Session, const char* Verb)
    {
        return std::string(Verb) + ":" + expansion::to_decimal_string_u(Session.state().revision);
    }
}

#define EXPANSION_REQUIRE_SESSION()                                             \
    if (!HasSession())                                                          \
    {                                                                           \
        FExpansionResult R;                                                     \
        R.Code = EExpansionErrorCode::InvalidArgument;                           \
        R.Reason = TEXT("no_session");                                           \
        R.Detail = FText::FromString(TEXT("There is no session."));              \
        return R;                                                                \
    }

FExpansionResult UExpansionSessionSubsystem::AssignWorkers(FName PlanetId, int64 FromFacilityId,
                                                           int64 ToFacilityId, int32 Count)
{
    EXPANSION_REQUIRE_SESSION()
    expansion::Command C;
    C.id = CommandId(*Sim->Session, "assign");
    C.kind = expansion::CommandKind::AssignWorkers;
    C.planet_id = AsStd(PlanetId);
    C.facility_id = static_cast<expansion::InstanceId>(FromFacilityId);
    C.to_facility_id = static_cast<expansion::InstanceId>(ToFacilityId);
    C.count = Count;
    const FExpansionResult R = FromCommand(expansion::api::apply_command(*Sim->Session, C));
    PublishChanges();
    return R;
}

FExpansionResult UExpansionSessionSubsystem::SetProductionPriority(int64 FacilityId, int32 PriorityBand)
{
    EXPANSION_REQUIRE_SESSION()
    expansion::Command C;
    C.id = CommandId(*Sim->Session, "priority");
    C.kind = expansion::CommandKind::SetProductionPriority;
    C.facility_id = static_cast<expansion::InstanceId>(FacilityId);
    C.priority_band = PriorityBand;
    const FExpansionResult R = FromCommand(expansion::api::apply_command(*Sim->Session, C));
    PublishChanges();
    return R;
}

FExpansionResult UExpansionSessionSubsystem::SetFacilityIdle(int64 FacilityId, bool bIdle)
{
    EXPANSION_REQUIRE_SESSION()
    expansion::Command C;
    C.id = CommandId(*Sim->Session, "idle");
    C.kind = expansion::CommandKind::SetFacilityIdle;
    C.facility_id = static_cast<expansion::InstanceId>(FacilityId);
    C.flag = bIdle;
    const FExpansionResult R = FromCommand(expansion::api::apply_command(*Sim->Session, C));
    PublishChanges();
    return R;
}

FExpansionResult UExpansionSessionSubsystem::StartConstruction(FName PlanetId, FName FacilityId, FName RecipeId)
{
    EXPANSION_REQUIRE_SESSION()
    expansion::Command C;
    C.id = CommandId(*Sim->Session, "build");
    C.kind = expansion::CommandKind::StartConstruction;
    C.planet_id = AsStd(PlanetId);
    C.content_id = AsStd(FacilityId);
    if (!RecipeId.IsNone())
    {
        C.recipe_id = AsStd(RecipeId);
    }
    const FExpansionResult R = FromCommand(expansion::api::apply_command(*Sim->Session, C));
    PublishChanges();
    return R;
}

FExpansionResult UExpansionSessionSubsystem::CancelConstruction(int64 FacilityId)
{
    EXPANSION_REQUIRE_SESSION()
    expansion::Command C;
    C.id = CommandId(*Sim->Session, "cancel");
    C.kind = expansion::CommandKind::CancelConstruction;
    C.facility_id = static_cast<expansion::InstanceId>(FacilityId);
    const FExpansionResult R = FromCommand(expansion::api::apply_command(*Sim->Session, C));
    PublishChanges();
    return R;
}

FExpansionResult UExpansionSessionSubsystem::ServiceFacility(int64 FacilityId)
{
    EXPANSION_REQUIRE_SESSION()
    expansion::Command C;
    C.id = CommandId(*Sim->Session, "service");
    C.kind = expansion::CommandKind::ServiceFacility;
    C.facility_id = static_cast<expansion::InstanceId>(FacilityId);
    const FExpansionResult R = FromCommand(expansion::api::apply_command(*Sim->Session, C));
    PublishChanges();
    return R;
}

FExpansionResult UExpansionSessionSubsystem::SelectPolicy(FName PlanetId, FName PolicyId)
{
    EXPANSION_REQUIRE_SESSION()
    expansion::Command C;
    C.id = CommandId(*Sim->Session, "policy");
    C.kind = expansion::CommandKind::SelectPolicy;
    C.planet_id = AsStd(PlanetId);
    C.content_id = AsStd(PolicyId);
    const FExpansionResult R = FromCommand(expansion::api::apply_command(*Sim->Session, C));
    PublishChanges();
    return R;
}

FExpansionResult UExpansionSessionSubsystem::AuthoriseDeparture(bool bAllowEmpty)
{
    EXPANSION_REQUIRE_SESSION()
    expansion::Command C;
    C.id = CommandId(*Sim->Session, "depart");
    C.kind = expansion::CommandKind::AuthoriseDeparture;
    C.flag = bAllowEmpty;
    const FExpansionResult R = FromCommand(expansion::api::apply_command(*Sim->Session, C));
    PublishChanges();
    return R;
}

FExpansionResult UExpansionSessionSubsystem::LaunchColonization()
{
    EXPANSION_REQUIRE_SESSION()
    expansion::Command C;
    C.id = CommandId(*Sim->Session, "colonise");
    C.kind = expansion::CommandKind::LaunchColonization;
    const FExpansionResult R = FromCommand(expansion::api::apply_command(*Sim->Session, C));
    PublishChanges();
    return R;
}

FExpansionResult UExpansionSessionSubsystem::ResolveEvent(int64 EventInstanceId, FName ChoiceId)
{
    EXPANSION_REQUIRE_SESSION()
    expansion::Command C;
    C.id = CommandId(*Sim->Session, "resolve");
    C.kind = expansion::CommandKind::ResolveEvent;
    C.event_instance_id = static_cast<expansion::InstanceId>(EventInstanceId);
    C.content_id = AsStd(ChoiceId);
    const FExpansionResult R = FromCommand(expansion::api::apply_command(*Sim->Session, C));
    if (R.bOk)
    {
        // Answering the decision releases the calendar.
        Sim->Clock.release_decision_hold();
    }
    PublishChanges();
    return R;
}

FExpansionResult UExpansionSessionSubsystem::RequestRelief(FName PlanetId)
{
    EXPANSION_REQUIRE_SESSION()
    expansion::Command C;
    C.id = CommandId(*Sim->Session, "relief");
    C.kind = expansion::CommandKind::RequestRelief;
    C.planet_id = AsStd(PlanetId);
    const FExpansionResult R = FromCommand(expansion::api::apply_command(*Sim->Session, C));
    PublishChanges();
    return R;
}

// ---------------------------------------------------------------------------
// Saves
// ---------------------------------------------------------------------------

FExpansionResult UExpansionSessionSubsystem::SaveToSlot(int32 Slot)
{
    EXPANSION_REQUIRE_SESSION()
    auto Bytes = expansion::api::encode_save(*Sim->Session, std::string(TCHAR_TO_UTF8(*FApp::GetBuildVersion())));
    if (!Bytes.ok())
    {
        return Fail(Bytes.error());
    }
    // Write through the engine's save directory rather than hostfs: a packaged
    // build on a console has no writable path of the shape hostfs assumes.
    const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Expansion"),
                                         FString::Printf(TEXT("slot%d.scexp"), Slot));
    const FString Contents = FString(UTF8_TO_TCHAR(Bytes.value().c_str()));
    if (!FFileHelper::SaveStringToFile(Contents, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        FExpansionResult R;
        R.Code = EExpansionErrorCode::Internal;
        R.Reason = TEXT("write_failed");
        R.Detail = FText::FromString(FString::Printf(TEXT("Could not write %s."), *Path));
        return R;
    }
    return Ok();
}

FExpansionResult UExpansionSessionSubsystem::LoadFromSlot(int32 Slot)
{
    if (Sim->Catalog == nullptr)
    {
        const FExpansionResult Load = LoadCatalog();
        if (!Load.bOk)
        {
            return Load;
        }
    }
    const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Expansion"),
                                         FString::Printf(TEXT("slot%d.scexp"), Slot));
    FString Contents;
    if (!FFileHelper::LoadFileToString(Contents, *Path))
    {
        FExpansionResult R;
        R.Code = EExpansionErrorCode::NotFound;
        R.Reason = TEXT("no_such_slot");
        R.Detail = FText::FromString(FString::Printf(TEXT("There is no save in slot %d."), Slot));
        return R;
    }
    auto Restored = expansion::api::decode_save(Sim->Catalog, std::string(TCHAR_TO_UTF8(*Contents)));
    if (!Restored.ok())
    {
        // IncompatibleSave and CorruptSave are distinct on purpose: the first is
        // "this save belongs to another build", the second is "this file is
        // damaged", and a player needs to be told which.
        return Fail(Restored.error());
    }
    Sim->Session = Restored.take();
    Sim->Tracker.forget();
    Sim->Clock.reset();
    Sim->Clock.release_decision_hold();
    PublishChanges();
    return Ok();
}

#undef EXPANSION_REQUIRE_SESSION
