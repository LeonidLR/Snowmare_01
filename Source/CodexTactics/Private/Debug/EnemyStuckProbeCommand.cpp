// Dev-only diagnostic on L_MovementTest (user report 2026-10-05: frostbitten stood still in a wave while hounds
// attacked; the log showed «no way to …» for every operative):
//   Scripts/smoke.ps1 -Command CodexTactics.EnemyStuckProbe
// «Начать бой», wave 1, the squad stays put; after 20 s every living enemy is logged: type, position, speed, target,
// whether a full navmesh path (the enemies' NoVault filter) leads to the nearest operative, whether it waits at a fire
// zone. PASS when every melee enemy has a full path (or is in melee reach) and is not standing still far from it.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Core/MissionSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/VaultNavigation.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

namespace EnemyStuckProbe
{
	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		TMap<TWeakObjectPtr<AEnemyCharacter>, FVector> Positions;
	};

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State, float DeltaTime)
	{
		State.Time += 0.1f; // fixed step like the other smokes (the ticker's DeltaTime is not the 0.1 s interval)
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		if (State.Stage == 0)
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			for (AOperativeCharacter* Member : World->GetSubsystem<USquadSubsystem>()->GetMembers())
			{
				Member->bTacticalCeaseFire = true; // nobody dies: we watch them walk
				Member->HealthComponent->SetMaxHealth(100000.f);
			}
			State.Stage = 1;
			State.Time = 0.f;
			return true;
		}
		for (AOperativeCharacter* Member : World->GetSubsystem<USquadSubsystem>()->GetMembers())
		{
			Member->ColdLevel = 0.f; // no freezing / panic while we watch
		}
		if (State.Stage == 1)
		{
			if (State.Time < 8.f)
			{
				return true;
			}
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				State.Positions.Add(*It, It->GetActorLocation());
			}
			State.Stage = 2;
			State.Time = 0.f;
			return true;
		}
		if (State.Time < 5.f)
		{
			return true;
		}
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const TArray<AOperativeCharacter*> Squad = World->GetSubsystem<USquadSubsystem>()->GetMembers();
		TMap<FString, int32> StuckByType;
		TMap<FString, int32> AllByType;
		int32 NoPath = 0;
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			if (It->IsDying())
			{
				continue;
			}
			const FString Type = It->GetEnemyDisplayName();
			AllByType.FindOrAdd(Type)++;
			const AOperativeCharacter* Nearest = nullptr;
			float NearestDistance = TNumericLimits<float>::Max();
			for (const AOperativeCharacter* Member : Squad)
			{
				const float Distance = FVector::Dist2D(Member->GetActorLocation(), It->GetActorLocation());
				if (Distance < NearestDistance)
				{
					NearestDistance = Distance;
					Nearest = Member;
				}
			}
			const UNavigationPath* Path = Nav && Nearest ? Nav->FindPathToLocationSynchronously(World, It->GetActorLocation(), Nearest->GetActorLocation(),
				*It, UNavFilter_NoVault::StaticClass()) : nullptr;
			const bool bFull = Path && Path->IsValid() && !Path->IsPartial();
			const FVector* Before = State.Positions.Find(*It);
			const float Moved = Before ? static_cast<float>(FVector::Dist2D(*Before, It->GetActorLocation())) : -1.f;
			const bool bStuck = NearestDistance > 300.f && Moved >= 0.f && Moved < 100.f;
			if (bStuck)
			{
				StuckByType.FindOrAdd(Type)++;
			}
			NoPath += (!bFull && NearestDistance > 300.f) ? 1 : 0;
			const AActor* Target = It->GetCurrentTarget();
			UE_LOG(LogCodexTactics, Display, TEXT("Probe %-12s at %s: %.0f m from %s, moved %.0f cm in 5 s, speed %.0f, target %s, path %s (%.0f m), standing on obstacle %d%s"),
				*Type, *It->GetActorLocation().ToCompactString(), NearestDistance / 100.f, Nearest ? *Nearest->DisplayName.ToString() : TEXT("-"),
				Moved, It->GetVelocity().Size2D(), Target ? *Target->GetName() : TEXT("none"),
				!Path || !Path->IsValid() ? TEXT("NONE") : (Path->IsPartial() ? TEXT("PARTIAL") : TEXT("full")),
				Path && Path->IsValid() ? Path->GetPathLength() / 100.f : 0.f, VaultNavigation::IsStandingOnObstacle(**It) ? 1 : 0,
				bStuck ? TEXT("  <-- STUCK") : TEXT(""));
			if (bStuck)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Probe     state: %s"), *It->GetDebugState());
			}
		}
		FString Summary;
		for (const TPair<FString, int32>& Entry : AllByType)
		{
			Summary += FString::Printf(TEXT("%s%s %d/%d stuck"), Summary.IsEmpty() ? TEXT("") : TEXT(", "), *Entry.Key, StuckByType.FindRef(Entry.Key), Entry.Value);
		}
		int32 Stuck = 0;
		for (const TPair<FString, int32>& Entry : StuckByType)
		{
			Stuck += Entry.Value;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Probe summary: %s; no full path to the squad: %d"), *Summary, NoPath);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), Stuck == 0 && NoPath == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("EnemyStuckProbe"));
		return false;
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float DeltaTime)
		{
			return Step(WeakWorld, *State, DeltaTime);
		}), 0.1f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.EnemyStuckProbe"),
		TEXT("Dev check: after 20 s of wave 1, logs every enemy's path / movement to the squad; PASS when nobody is stuck."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
