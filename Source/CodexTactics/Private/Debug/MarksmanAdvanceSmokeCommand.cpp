// Dev-only console command for a headless check of the marksman's wave aggression (Sprint 06-D) on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.MarksmanAdvanceSmoke
// A marksman spawned during a wave at the enemy spawn point farthest from the (unkillable, cease-fire) squad behind the
// gate must not idle: he engages, advances over the navmesh when needed, settles at 20-35 m, aims with the beam and fires.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/MarksmanEnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/EnemySpawnPoint.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Core/MissionSessionSubsystem.h"
#include "Core/MissionSubsystem.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace MarksmanAdvanceSmoke
{
	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		float StartDistance = 0.f;
		float ClosestDistance = TNumericLimits<float>::Max();
		bool bSawAim = false;
		TWeakObjectPtr<AMarksmanEnemyCharacter> Marksman;
	};

	bool Finish(bool bPass)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("MarksmanAdvanceSmoke"));
		return false;
	}

	float SquadDistance(const USquadSubsystem* Squad, const AActor* From)
	{
		float Best = TNumericLimits<float>::Max();
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			Best = FMath::Min(Best, static_cast<float>(FVector::Dist(Member->GetActorLocation(), From->GetActorLocation())));
		}
		return Best;
	}

	bool Step(UWorld* World, FState& State)
	{
		State.Time += 0.1f;
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Flow || !Leader)
		{
			return Finish(false);
		}
		if (State.Stage == 0)
		{
			// As in the game: «Начать бой» puts the squad behind the gate, then the wave.
			if (UMissionSubsystem* Mission = World->GetSubsystem<UMissionSubsystem>())
			{
				Mission->StartMission(EMissionStartMode::Combat);
			}
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
				Member->bTacticalCeaseFire = true;
			}
			FVector Far = Leader->GetActorLocation() + Leader->GetActorForwardVector() * 4500.f;
			float FarDistance = 0.f;
			for (TActorIterator<AEnemySpawnPoint> It(World); It; ++It)
			{
				const float Distance = FVector::Dist2D(It->GetActorLocation(), Leader->GetActorLocation());
				if (Distance > FarDistance)
				{
					FarDistance = Distance;
					Far = It->GetActorLocation();
				}
			}
			// At the farthest wave spawn point (where the level's marksmen appear; a point beyond it can be outside the yard
			// walls with no way in).
			AMarksmanEnemyCharacter* Marksman = Cast<AMarksmanEnemyCharacter>(World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Marksman, Far));
			if (!Marksman)
			{
				return Finish(false);
			}
			Marksman->GetHealthComponent()->SetMaxHealth(100000.f);
			State.Marksman = Marksman;
			State.StartDistance = SquadDistance(Squad, Marksman);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke marksman spawned %.0f m from the squad in wave %d"), State.StartDistance / 100.f, Flow->GetWaveIndex());
			State.Stage = 1;
			State.Time = 0.f;
			return true;
		}
		AMarksmanEnemyCharacter* Marksman = State.Marksman.Get();
		if (!Marksman)
		{
			return Finish(false);
		}
		const float Distance = SquadDistance(Squad, Marksman);
		State.ClosestDistance = FMath::Min(State.ClosestDistance, Distance);
		State.bSawAim |= Marksman->IsAimingAtTarget();
		if (FMath::Fmod(State.Time, 2.f) < 0.1f)
		{
			const AOperativeCharacter* Target = Cast<AOperativeCharacter>(Marksman->GetCurrentTarget());
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(SmokeLine), false, Marksman);
			const bool bBlocked = Target && World->LineTraceSingleByChannel(Hit, Marksman->GetActorLocation() + FVector(0.f, 0.f, 60.f),
				Target->GetActorLocation(), ECC_Visibility, Params) && Hit.GetActor() != Target;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke trace t=%.0f: state %d stance %d, %.0f m, %.0f cm/s, line of fire %d, blocked by %s at %s, me %s, target %s"),
				State.Time, static_cast<int32>(Marksman->GetAIState()), static_cast<int32>(Marksman->GetStance()), Distance / 100.f,
				Marksman->GetVelocity().Size2D(), Marksman->HasLineOfFireTo(Target) ? 1 : 0, bBlocked && Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("-"),
				*Hit.ImpactPoint.ToCompactString(), *Marksman->GetActorLocation().ToCompactString(), Target ? *Target->GetActorLocation().ToCompactString() : TEXT("-"));
		}
		if (Marksman->GetShotsFired() == 0 && State.Time < 60.f)
		{
			return true;
		}
		const bool bEngaged = Marksman->GetAIState() != EMarksmanAIState::Patrol;
		const bool bBand = Distance >= 1200.f && Distance <= 3800.f;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: engaged %d, advanced %.0f -> %.0f m (in the 12-38 m band %d), aimed %d, shots %d, %.1f s"),
			bEngaged && bBand && State.bSawAim && Marksman->GetShotsFired() > 0 ? TEXT("ok  ") : TEXT("FAIL"), bEngaged ? 1 : 0,
			State.StartDistance / 100.f, Distance / 100.f, bBand ? 1 : 0, State.bSawAim ? 1 : 0, Marksman->GetShotsFired(), State.Time);
		return Finish(bEngaged && bBand && State.bSawAim && Marksman->GetShotsFired() > 0);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		FTimerHandle StartHandle;
		World->GetTimerManager().SetTimer(StartHandle, FTimerDelegate::CreateLambda([WeakWorld, State, Handle]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				W->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda([WeakWorld, State, Handle]()
				{
					UWorld* W2 = WeakWorld.Get();
					if (W2 && !Step(W2, *State))
					{
						W2->GetTimerManager().ClearTimer(*Handle);
					}
				}), 0.1f, true);
			}
		}), 3.f, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(TEXT("CodexTactics.MarksmanAdvanceSmoke"),
		TEXT("Dev check: a wave marksman advances to 20-35 m, aims and fires instead of idling; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
