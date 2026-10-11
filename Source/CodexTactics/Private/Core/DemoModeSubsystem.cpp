#include "Core/DemoModeSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/PanicComponent.h"
#include "CodexTactics.h"
#include "Core/CodexTacticsGameMode.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "UI/CodexTacticsHUD.h"

bool UDemoModeSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UDemoModeSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	SpawnedHandle = InWorld.AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UDemoModeSubsystem::HandleActorSpawned));
	// The game mode reads the level JSON in StartPlay, after the subsystems' OnWorldBeginPlay.
	TWeakObjectPtr<UDemoModeSubsystem> WeakThis(this);
	InWorld.GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [WeakThis]()
	{
		if (UDemoModeSubsystem* Self = WeakThis.Get())
		{
			Self->ApplyFromLevel();
		}
	}));
}

void UDemoModeSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld(); World && SpawnedHandle.IsValid())
	{
		World->RemoveOnActorSpawnedHandler(SpawnedHandle);
	}
	SpawnedHandle.Reset();
	Super::Deinitialize();
}

void UDemoModeSubsystem::ApplyFromLevel()
{
	const UWorld* World = GetWorld();
	const ACodexTacticsGameMode* GameMode = World ? World->GetAuthGameMode<ACodexTacticsGameMode>() : nullptr;
	const ULevelConfigAsset* Level = GameMode ? GameMode->GetActiveLevelConfig() : nullptr;
	if (Level && Level->Config.Demo.bDemo)
	{
		ApplySettings(Level->Config.Demo);
	}
}

void UDemoModeSubsystem::ApplySettings(const FDemoModeSettings& InSettings)
{
	Settings = InSettings;
	UE_LOG(LogCodexTactics, Display, TEXT("Demo mode: demo %d, panic %d, vision cones %s, tutorial %d (%s), demo HUD %d"), Settings.bDemo ? 1 : 0,
		Settings.bPanicEnabled ? 1 : 0, Settings.bOverrideVisionCones ? (Settings.bVisionCones ? TEXT("on") : TEXT("off")) : TEXT("BP defaults"),
		Settings.bTutorial ? 1 : 0, *Settings.TutorialSteps, Settings.bDemoHud ? 1 : 0);
	ApplyToWorld();
}

void UDemoModeSubsystem::ApplyToWorld()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		ApplyToActor(*It);
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (ACodexTacticsHUD* Hud = It->Get() ? It->Get()->GetHUD<ACodexTacticsHUD>() : nullptr)
		{
			Hud->bDemoHudPreset = Settings.bDemo && Settings.bDemoHud;
		}
	}
}

void UDemoModeSubsystem::ApplyToActor(AActor* Actor) const
{
	if (!Settings.bDemo || !Actor)
	{
		return;
	}
	if (AOperativeCharacter* Operative = Cast<AOperativeCharacter>(Actor))
	{
		if (!Settings.bPanicEnabled && Operative->PanicComponent)
		{
			Operative->PanicComponent->bEnabled = false;
		}
		if (Settings.bOverrideVisionCones)
		{
			Operative->bShowVisionCone = Settings.bVisionCones;
		}
	}
	else if (AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Actor))
	{
		if (Settings.bOverrideVisionCones)
		{
			Enemy->bShowVisionCone = Settings.bVisionCones;
		}
	}
}

void UDemoModeSubsystem::HandleActorSpawned(AActor* Actor)
{
	if (!Settings.bDemo || !Actor || !(Actor->IsA<AOperativeCharacter>() || Actor->IsA<AEnemyCharacter>()))
	{
		return;
	}
	// After its BeginPlay (BP / balance defaults are applied there).
	TWeakObjectPtr<AActor> WeakActor(Actor);
	TWeakObjectPtr<const UDemoModeSubsystem> WeakThis(this);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [WeakThis, WeakActor]()
		{
			if (const UDemoModeSubsystem* Self = WeakThis.Get())
			{
				Self->ApplyToActor(WeakActor.Get());
			}
		}));
	}
}
