// Dev-only headless check of the Sprint 11 outpost patrols on L_MovementTest (the map is not saved):
//   Scripts/smoke.ps1 -Command CodexTactics.PatrolSmoke -Log Smoke-Patrol.log
// Exploration (no wave); the level's enemies are removed. A three-point spline route (APatrolRouteActor, loop, 1 s
// pauses) is laid 12 m ahead of the squad; a marksman walks it, an escort frost hound follows him. Sight ranges are cut
// to 1 m so nobody is spotted. Checks: the marksman reaches waypoints 1 and 2 at the patrol pace; the hound stays within
// the tether band (<= 6 m once caught up) and keeps patrolling; a simulated tripwire blast 25 m away leaves both on
// patrol, one 15 m away breaks both into Engage (the escort mirrors its leader).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/PatrolRouteActor.h"
#include "AI/PatrolRouteRules.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Components/SplineComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"

namespace PatrolSmoke
{
	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		FVector Origin = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		TWeakObjectPtr<AMarksmanEnemyCharacter> Marksman;
		TWeakObjectPtr<AEnemyCharacter> Hound;
		TWeakObjectPtr<APatrolRouteActor> Route;
		bool bReached1 = false;
		bool bReached2 = false;
		float MaxEscortDistance = 0.f;
		float MaxMarksmanSpeed = 0.f;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("PatrolSmoke"));
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += 0.1f;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		for (AOperativeCharacter* Member : Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>())
		{
			Member->ColdLevel = 0.f;
			Member->bTacticalCeaseFire = true;
		}
		AMarksmanEnemyCharacter* Marksman = State.Marksman.Get();
		AEnemyCharacter* Hound = State.Hound.Get();
		switch (State.Stage)
		{
		case 0:
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
			if (!Leader)
			{
				Check(State, false, TEXT("squad leader present"));
				return Finish(State);
			}
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			State.F = Leader->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			const FVector Feet = Leader->GetActorLocation() - FVector(0.f, 0.f, Leader->GetSimpleCollisionHalfHeight());
			// Waypoints on the navmesh (the user re-lays the map: SmokeUtils adapts the directions).
			const FVector A = SmokeUtils::ClearPoint(World, Feet, Feet + State.F * 1200.f);
			const FVector B = SmokeUtils::ClearPoint(World, A, A + State.R * 700.f);
			const FVector C = SmokeUtils::ClearPoint(World, B, B + State.F * 600.f);
			State.Origin = A;
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APatrolRouteActor* Route = World->SpawnActor<APatrolRouteActor>(A, FRotator::ZeroRotator, Params);
			State.Route = Route;
			USplineComponent* Spline = Route ? Route->GetRouteSpline() : nullptr;
			if (!Spline)
			{
				Check(State, false, TEXT("route spawned"));
				return Finish(State);
			}
			Spline->ClearSplinePoints(false);
			for (const FVector& Point : { A, B, C })
			{
				Spline->AddSplinePoint(Point, ESplineCoordinateSpace::World, false);
			}
			Spline->UpdateSpline();
			Route->bIsLoop = true;
			Route->DefaultWaitTimeSeconds = 1.f;
			UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			State.Marksman = Cast<AMarksmanEnemyCharacter>(Waves->SpawnEnemy(EEnemyArchetype::Marksman, A + FVector(0.f, 0.f, 100.f), State.R.Rotation()));
			State.Hound = Waves->SpawnEnemy(EEnemyArchetype::FrostHound, A - State.F * 300.f + FVector(0.f, 0.f, 80.f), State.R.Rotation());
			Marksman = State.Marksman.Get();
			Hound = State.Hound.Get();
			if (!Marksman || !Hound)
			{
				Check(State, false, TEXT("marksman and hound spawned"));
				return Finish(State);
			}
			// Nobody is spotted in this check (sight is covered by CodexTactics.SightSmoke and the alert rule tests).
			Marksman->MarksmanConfig.DetectionRange = 100.f;
			Marksman->PatrolSightRange = 100.f;
			Hound->PatrolSightRange = 100.f;
			Marksman->StartPatrol(Route, nullptr);
			Hound->StartPatrol(nullptr, Marksman);
			Check(State, Marksman->IsOnPatrol() && Marksman->GetAIState() == EMarksmanAIState::Patrol, TEXT("marksman patrols the spline route"));
			Check(State, Hound->IsOnPatrol() && Hound->GetEscortLeader() == Marksman, TEXT("hound escorts him"));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: route A %s, B %s, C %s"), *A.ToCompactString(), *B.ToCompactString(), *C.ToCompactString());
			State.Stage = 1;
			State.Time = 0.f;
			return true;
		}
		case 1:
		{
			if (!Marksman || !Hound)
			{
				Check(State, false, TEXT("patrol alive"));
				return Finish(State);
			}
			// Walks the route.
			const int32 Index = Marksman->GetPatrolWaypointIndex();
			State.bReached1 |= Index >= 1;
			State.bReached2 |= Index == 2 || (State.bReached1 && Index == 0);
			State.MaxMarksmanSpeed = FMath::Max(State.MaxMarksmanSpeed, Marksman->GetVelocity().Size2D());
			if (State.Time > 6.f)
			{
				State.MaxEscortDistance = FMath::Max(State.MaxEscortDistance, FVector::Dist2D(Marksman->GetActorLocation(), Hound->GetActorLocation()));
			}
			if (!Marksman->IsOnPatrol() || !Hound->IsOnPatrol())
			{
				Check(State, false, TEXT("nobody alerted while unseen"));
				return Finish(State);
			}
			if ((State.bReached1 && State.bReached2 && State.Time > 12.f) || State.Time > 40.f)
			{
				Check(State, State.bReached1, TEXT("marksman reached waypoint 1"));
				Check(State, State.bReached2, TEXT("marksman went on to waypoint 2"));
				Check(State, State.MaxMarksmanSpeed > 50.f && State.MaxMarksmanSpeed <= Marksman->PatrolWalkSpeed + 30.f,
					FString::Printf(TEXT("patrol pace (max %.0f cm/s, patrol %.0f)"), State.MaxMarksmanSpeed, Marksman->PatrolWalkSpeed));
				Check(State, State.MaxEscortDistance <= 600.f,
					FString::Printf(TEXT("escort keeps the tether (max %.0f cm after catching up)"), State.MaxEscortDistance));
				// A blast 25 m off: not heard.
				AEnemyCharacter::AlertPatrolsNearTrap(World, Marksman->GetActorLocation() + State.R * 2500.f);
				Check(State, Marksman->IsOnPatrol() && Hound->IsOnPatrol(), TEXT("tripwire at 25 m: both stay on patrol"));
				State.Stage = 2;
				State.Time = 0.f;
			}
			return true;
		}
		default:
		{
			if (!Marksman || !Hound)
			{
				Check(State, false, TEXT("patrol alive"));
				return Finish(State);
			}
			if (State.Time < 0.5f)
			{
				return true;
			}
			if (State.Stage == 2)
			{
				// A blast 15 m off the marksman (more than 20 m from the hound possibly): he breaks, the escort mirrors him.
				AEnemyCharacter::AlertPatrolsNearTrap(World, Marksman->GetActorLocation() + State.R * 1500.f);
				Check(State, !Marksman->IsOnPatrol() && Marksman->GetAIState() != EMarksmanAIState::Patrol, TEXT("tripwire at 15 m: marksman engages"));
				State.Stage = 3;
				State.Time = 0.f;
				return true;
			}
			Check(State, !Hound->IsOnPatrol(), TEXT("escort hound broke off with him"));
			return Finish(State);
		}
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), 0.1f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.PatrolSmoke"),
		TEXT("Dev check of the Sprint 11 spline patrols: route walk, escort tether, tripwire alert 25 m / 15 m; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
