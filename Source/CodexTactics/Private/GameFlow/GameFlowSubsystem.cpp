#include "GameFlow/GameFlowSubsystem.h"

#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"

void UGameFlowSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	StateChangedHandle = Machine.OnStateChanged.AddUObject(this, &UGameFlowSubsystem::HandleStateChanged);
	PauseReleasedHandle = Machine.OnTacticalPauseReleased.AddUObject(this, &UGameFlowSubsystem::HandlePauseReleased);
}

void UGameFlowSubsystem::Deinitialize()
{
	Machine.OnStateChanged.Remove(StateChangedHandle);
	Machine.OnTacticalPauseReleased.Remove(PauseReleasedHandle);
	Super::Deinitialize();
}

void UGameFlowSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	ApplyTimeDilation();
}

bool UGameFlowSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UGameFlowSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// DeltaTime is dilated by the world; flow timers must run on real time - but a hitch (the first frame after a map load
	// reports the whole load, 3.6 s by now) must not eat the timers: the 4 s pre-combat cutscene ended in the first frame
	// after a quick restart (MainMenuSmoke "quick restart: cutscene again", 2026-10-07).
	Machine.Tick(FMath::Min(static_cast<float>(FApp::GetDeltaTime()), MaxFlowStepSeconds));
}

TStatId UGameFlowSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGameFlowSubsystem, STATGROUP_Tickables);
}

void UGameFlowSubsystem::ResetFlow(const FGameFlowConfig& Config)
{
	Machine.Reset(Config);
	HandleStateChanged(Machine.GetPhase(), Machine.GetCombatMode());
}

void UGameFlowSubsystem::RestoreForLoad(bool bCombatUnlocked, bool bCombatPhase, int32 WaveIndex, float PreparationSeconds)
{
	Machine.RestoreForLoad(bCombatUnlocked, bCombatPhase, WaveIndex, PreparationSeconds);
	HandleStateChanged(Machine.GetPhase(), Machine.GetCombatMode());
}

void UGameFlowSubsystem::HandleStateChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode)
{
	UE_LOG(LogCodexTactics, Log, TEXT("GameFlow: %s / %s (wave %d)"),
		*UEnum::GetValueAsString(Phase), *UEnum::GetValueAsString(CombatMode), Machine.GetWaveIndex());
	const ECodexGamePhase OldPhase = LastBroadcastPhase;
	LastBroadcastPhase = Phase;
	OnBeforeGameFlowChanged.Broadcast(OldPhase, Phase, CombatMode);
	ApplyTimeDilation();
	OnGameFlowChanged.Broadcast(Phase, CombatMode);
}

void UGameFlowSubsystem::HandlePauseReleased()
{
	OnTacticalPauseReleased.Broadcast();
}

void UGameFlowSubsystem::ApplyTimeDilation() const
{
	// The engine clamps to AWorldSettings::MinGlobalTimeDilation, so 0 becomes a near-stop.
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::SetGlobalTimeDilation(World, Machine.GetTimeDilation());
	}
}

bool UGameFlowSubsystem::IsAutonomousSquadCombat() const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	return Squad && Squad->IsAutonomousSquadCombat();
}

bool UGameFlowSubsystem::IsSquadAutonomyActive() const
{
	return IsAutonomousSquadCombat() && GetPhase() == ECodexGamePhase::WaveCombat && GetCombatMode() == ECodexCombatMode::RealTime;
}
