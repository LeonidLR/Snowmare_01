// Dev-only console command for a headless check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.EnemyHuntSmoke
// Enemies keep hunting (user report 2026-09-30: some froze at the edge of warm zones and could be shot freely):
// a hound that fears fire picks the operative outside the fire zone over a nearer one warming up at a burning barrel,
// runs at him and closes in instead of standing at the zone's edge.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "EngineUtils.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/InteractableActor.h"
#include "TimerManager.h"

namespace EnemyHuntSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		TWeakObjectPtr<AOperativeCharacter> Warm;
		TWeakObjectPtr<AOperativeCharacter> Cold;
		float StartDistance = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("EnemyHuntSmoke"));
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 40.f)
		{
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			if (Members.Num() < 3)
			{
				return Finish(State, false);
			}
			// Exploration: no wave, nobody shoots, and the engineer does not set up a turret (a preparation would, and
			// small enemies rightly prefer turrets). Followers hold so the cold one stays away from the fire.
			Squad->SetFollowersHolding(true);
			// Out of the picture: small enemies rightly go for a working generator first (the test start is near it).
			for (TActorIterator<AInteractableActor> It(World); It; ++It)
			{
				if (It->ObjectType == EInteractableType::Generator)
				{
					It->bGeneratorBroken = true;
				}
			}
			const FVector Origin = Members[0]->GetActorLocation();
			const FVector Forward = Members[0]->GetActorForwardVector();
			const FVector Right = Members[0]->GetActorRightVector();
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ABarrelActor* Barrel = World->SpawnActor<ABarrelActor>(Origin - FVector(0.f, 0.f, 50.f), FRotator::ZeroRotator, Params);
			Check(State, Barrel && Barrel->IgniteForTurnBased(), TEXT("burning barrel next to the first operative"));
			State.Warm = Members[0]; // stays by the fire
			State.Cold = Members[1];
			Members[1]->TeleportTo(Origin + Right * 900.f, Members[1]->GetActorRotation(), false, true);
			Members[2]->TeleportTo(Origin - Forward * 3000.f, Members[2]->GetActorRotation(), false, true);
			// The hound is nearer to the warm operative than to the cold one.
			State.Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound, Origin + Forward * 1200.f - Right * 300.f);
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			AEnemyCharacter* Hound = State.Hound.Get();
			if (!Hound || State.StageTime < 1.f)
			{
				return Hound ? true : Finish(State, false);
			}
			Check(State, Hound->FindTarget() == State.Cold.Get(), TEXT("hunts the operative outside the fire zone, not the one warming up"));
			State.StartDistance = FVector::Dist2D(Hound->GetActorLocation(), State.Cold->GetActorLocation());
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
		{
			if (State.StageTime < 4.f)
			{
				return true;
			}
			AEnemyCharacter* Hound = State.Hound.Get();
			const float Distance = Hound ? FVector::Dist2D(Hound->GetActorLocation(), State.Cold->GetActorLocation()) : 0.f;
			Check(State, Hound && (Distance < State.StartDistance - 500.f || Distance < 250.f),
				FString::Printf(TEXT("closes in: %.0f -> %.0f cm (not standing at the zone's edge)"), State.StartDistance, Distance));
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		{
			FTimerHandle PlaceHandle;
			TWeakObjectPtr<UWorld> PlaceWorld(World);
			World->GetTimerManager().SetTimer(PlaceHandle, FTimerDelegate::CreateLambda([PlaceWorld]()
			{
				SmokeUtils::PlaceSquadAtTestStart(PlaceWorld.Get());
			}), 0.5f, false);
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.EnemyHuntSmoke"),
		TEXT("Dev check: fire-fearing enemies hunt targets outside warm zones and keep closing in; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
