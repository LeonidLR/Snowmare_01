#include "GameFlow/GameFlowSubsystem.h"
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
	// DeltaTime is dilated by the world; flow timers must run on real time.
	Machine.Tick(static_cast<float>(FApp::GetDeltaTime()));
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

void UGameFlowSubsystem::RestoreForLoad(bool bCombatUnlocked, bool bCombatPhase, int32 WaveIndex)
{
	Machine.RestoreForLoad(bCombatUnlocked, bCombatPhase, WaveIndex);
	HandleStateChanged(Machine.GetPhase(), Machine.GetCombatMode());
}

void UGameFlowSubsystem::HandleStateChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode)
{
	UE_LOG(LogCodexTactics, Log, TEXT("GameFlow: %s / %s (wave %d)"),
		*UEnum::GetValueAsString(Phase), *UEnum::GetValueAsString(CombatMode), Machine.GetWaveIndex());
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
