// Dev probe of the stealth lure on any map (user report 2026-10-18: «the whistle only makes them turn / spin in place»):
//   UnrealEditor-Cmd <uproject> /Game/Maps/L_PatrolTest -game -nullrhi -ExecCmds="CodexTactics.LureProbe [throw]"
// Skips the tutorial, emits a whistle (or 'throw') noise on the navmesh 10 m behind the enemy on patrol / guard duty nearest to the
// leader (the squad is not moved) and logs every 0.5 s for 20 s each listener's state: distance to the noise, its navmesh path
// length there (negative = partial), speed, yaw, search flags and its search origin.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/StealthLureSubsystem.h"
#include "AI/WorldAIPauseSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Tutorial/TutorialSubsystem.h"

namespace LureProbe
{
	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		bool bThrow = false;
		FVector Noise = FVector::ZeroVector;
		float NextLog = 0.f;
		TArray<TWeakObjectPtr<AEnemyCharacter>> Listeners;
	};

	float PathLength(UWorld* World, const AActor* From, const FVector& To)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const UNavigationPath* Path = Nav && From ? Nav->FindPathToLocationSynchronously(World, From->GetActorLocation(), To, const_cast<AActor*>(From)) : nullptr;
		if (!Path || !Path->IsValid())
		{
			return -1.f;
		}
		return Path->IsPartial() ? -Path->GetPathLength() - 1.f : Path->GetPathLength();
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += 0.1f;
		UWorld* World = WeakWorld.Get();
		USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		UStealthLureSubsystem* Lure = World ? World->GetSubsystem<UStealthLureSubsystem>() : nullptr;
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!World || !Squad || !Lure || !Leader || State.Time > 90.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[LureProbe] end"));
			FPlatformMisc::RequestExit(false, TEXT("LureProbe"));
			return false;
		}
		switch (State.Stage)
		{
		case 0:
			if (State.Time < 4.f)
			{
				return true;
			}
			if (UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>())
			{
				Tutorial->SkipTutorial();
			}
			State.Stage = 1;
			State.Time = 0.f;
			return true;
		case 1:
		{
			if (State.Time < 2.f || UWorldAIPauseSubsystem::IsPausedIn(World))
			{
				return State.Time < 30.f || true;
			}
			AEnemyCharacter* Target = nullptr;
			float Best = TNumericLimits<float>::Max();
			for (AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
			{
				if (Enemy && Enemy->IsOnPatrol())
				{
					const float Distance = FVector::Dist(Enemy->GetActorLocation(), Leader->GetActorLocation());
					if (Distance < Best)
					{
						Best = Distance;
						Target = Enemy;
					}
				}
			}
			if (!Target)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[LureProbe] no enemy on patrol / guard duty"));
				State.Time = 1000.f;
				return true;
			}
			// The noise point: on the navmesh 10 m behind it (the squad itself is not moved - moving it makes noise).
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			FNavLocation OnNav;
			const FVector Behind = Target->GetActorLocation() - Target->GetActorForwardVector() * 1000.f;
			State.Noise = Nav && Nav->ProjectPointToNavigation(Behind, OnNav, FVector(200.f, 200.f, 400.f)) ? OnNav.Location : Behind;
			UE_LOG(LogCodexTactics, Display, TEXT("[LureProbe] target %s at %s (%s), noise at %s"), *Target->GetName(), *Target->GetActorLocation().ToCompactString(),
				Target->IsGuardingPost() ? TEXT("guard") : TEXT("patrol"), *State.Noise.ToCompactString());
			State.Stage = 2;
			State.Time = 0.f;
			return true;
		}
		case 2:
		{
			if (State.Time < 1.f)
			{
				return true;
			}
			Lure->EmitNoise(State.Noise, State.bThrow ? EStealthLureKind::Throw : EStealthLureKind::Whistle, Leader);
			for (const TWeakObjectPtr<AEnemyCharacter>& Weak : Lure->GetLastListeners())
			{
				State.Listeners.Add(Weak);
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[LureProbe] %s at %s: %d listeners, AI paused %d"), State.bThrow ? TEXT("throw") : TEXT("whistle"),
				*State.Noise.ToCompactString(), State.Listeners.Num(), UWorldAIPauseSubsystem::IsPausedIn(World) ? 1 : 0);
			State.Stage = 3;
			State.Time = 0.f;
			return true;
		}
		case 3:
		{
			if (State.Time >= State.NextLog)
			{
				State.NextLog += 0.5f;
				for (const TWeakObjectPtr<AEnemyCharacter>& Weak : State.Listeners)
				{
					if (const AEnemyCharacter* Enemy = Weak.Get())
					{
						UE_LOG(LogCodexTactics, Display, TEXT("[LureProbe] t %.1f %s dist %.0f path %.0f speed %.0f yaw %.0f search %d lure %d reached %d origin %s hunting %d paused %d"),
							State.Time, *Enemy->GetName(), FVector::Dist2D(Enemy->GetActorLocation(), State.Noise), PathLength(World, Enemy, State.Noise),
							Enemy->GetVelocity().Size2D(), Enemy->GetActorRotation().Yaw, Enemy->IsSearching() ? 1 : 0, Enemy->IsInvestigatingLure() ? 1 : 0,
							Enemy->HasReachedSearchOrigin() ? 1 : 0, *Enemy->GetSearchOrigin().ToCompactString(), Enemy->IsHuntingSquad() ? 1 : 0,
							UWorldAIPauseSubsystem::IsPausedIn(World) ? 1 : 0);
					}
				}
			}
			if (State.Time > 20.f)
			{
				State.Time = 1000.f;
			}
			return true;
		}
		default:
			return true;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TSharedRef<FState> State = MakeShared<FState>();
		State->bThrow = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("throw"), ESearchCase::IgnoreCase); });
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), 0.1f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.LureProbe"),
		TEXT("Dev probe: a whistle (or 'throw') noise 10 m behind the nearest patrol / guard enemy on this map; logs the listeners' approach for 20 s."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
