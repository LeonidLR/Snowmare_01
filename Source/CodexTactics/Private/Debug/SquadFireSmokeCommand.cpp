// Dev-only console command for a headless real-time squad fire check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.SquadFireSmoke
// Godot player.gd _find_shoot_target / _shoot_at_target: a barricade between the commander and a hound blocks a prone
// shot («Баррикада блокирует огонь»), gives cover 0.8 crouched and 1 standing; a current target is kept until a much
// closer hound has been closer for the stance delay (0.15 s standing); a crouched crit hits for 2.5x a standing plain
// shot («КРИТ x2!»).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarricadeActor.h"
#include "TimerManager.h"
#include "UI/FloatingTextSubsystem.h"

namespace SquadFireSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		float Plain = 0.f;
		TWeakObjectPtr<AEnemyCharacter> Far;
		TWeakObjectPtr<ABarricadeActor> Barricade;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("SquadFireSmoke"));
		return false;
	}

	AEnemyCharacter* SpawnHound(UWorld* World, const FVector& DesignPoint)
	{
		AEnemyCharacter* Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
			SmokeUtils::LevelPoint(World, DesignPoint), SmokeUtils::LayoutTransform(World).Rotator());
		if (Hound)
		{
			Hound->CustomTimeDilation = 0.f;
		}
		return Hound;
	}

	float HealthOf(const AActor* Actor)
	{
		return Actor->FindComponentByClass<UHealthComponent>()->GetCurrentHealth();
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time < 3.f)
		{
			return World != nullptr;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Commander = Squad->GetLeader();
		if (!Commander || State.Time > 30.f)
		{
			Check(State, false, TEXT("commander / timeout"));
			return Finish(State, false);
		}
		UFloatingTextSubsystem* Floating = World->GetSubsystem<UFloatingTextSubsystem>();
		// Stance changes resize the capsule: let it settle before measuring the line of fire.
		if (State.Stage > 0 && State.StageTime < 0.75f)
		{
			return true;
		}
		State.StageTime = 0.f;
		switch (State.Stage++)
		{
		case 0:
		{
			// The other operatives step aside so only the commander's line matters.
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member != Commander)
				{
					Member->TeleportTo(SmokeUtils::LevelPoint(World, FVector(-600.f, 600.f * Member->SquadIndex, 100.f)), Member->GetActorRotation(), false, true);
				}
			}
			Commander->TeleportTo(SmokeUtils::LevelPoint(World, FVector(0.f, 0.f, 100.f)), SmokeUtils::LayoutTransform(World).Rotator(), false, true);
			State.Far = SpawnHound(World, FVector(800.f, 0.f, 100.f));
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			State.Barricade = World->SpawnActor<ABarricadeActor>(SmokeUtils::LevelPoint(World, FVector(300.f, 0.f, 50.f)),
				SmokeUtils::LayoutTransform(World).Rotator() + FRotator(0.f, 90.f, 0.f), Params);
			if (!State.Far.IsValid() || !State.Barricade.IsValid())
			{
				Check(State, false, TEXT("hound and barricade spawned"));
				return Finish(State, false);
			}
			Commander->SetStance(EOperativeStance::Prone);
			return true;
		}
		case 1:
		{
			const FShootCandidate Prone = Commander->FindShootTarget(0.1f);
			Check(State, !Prone.Enemy && Floating->HasShown(TEXT("Баррикада блокирует огонь")), TEXT("prone behind the barricade: blocked, text"));
			Commander->SetStance(EOperativeStance::Crouching);
			return true;
		}
		case 2:
		{
			const FShootCandidate Crouched = Commander->FindShootTarget(0.1f);
			Check(State, Crouched.Enemy == State.Far.Get() && FMath::IsNearlyEqual(Crouched.Cover, 0.8f), FString::Printf(TEXT("crouched: cover %.2f"), Crouched.Cover));
			Commander->SetStance(EOperativeStance::Standing);
			return true;
		}
		case 3:
		{
			const FShootCandidate Standing = Commander->FindShootTarget(0.1f);
			Check(State, Standing.Enemy == State.Far.Get() && FMath::IsNearlyEqual(Standing.Cover, 1.f), TEXT("standing: over the barricade, cover 1"));
			State.Barricade->Destroy();

			// Flank switch after the stance delay (standing 0.15 s).
			Commander->FindShootTarget(0.1f);
			AEnemyCharacter* Near = SpawnHound(World, FVector(200.f, 200.f, 100.f));
			const FShootCandidate First = Commander->FindShootTarget(0.1f);
			const FShootCandidate Second = Commander->FindShootTarget(0.1f);
			Check(State, Near && First.Enemy == State.Far.Get() && Second.Enemy == Near, TEXT("flank hound taken after 0.15 s, not at once"));
			if (Near)
			{
				Near->Destroy();
			}

			// Damage: a brute (survives both shots) at the same spot.
			const FVector FarSpot = State.Far->GetActorLocation();
			State.Far->Destroy();
			AEnemyCharacter* Brute = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Brute, FarSpot, SmokeUtils::LayoutTransform(World).Rotator());
			if (!Brute)
			{
				Check(State, false, TEXT("brute spawned"));
				return Finish(State, false);
			}
			Brute->CustomTimeDilation = 0.f;
			State.Far = Brute;
			Commander->bForceHitForTesting = true;
			Commander->ForcedCritRollForTesting = 0.f;
			const float Before = HealthOf(Brute);
			Commander->ShootAtTarget(Brute, 1.f);
			State.Plain = Before - HealthOf(Brute);
			Commander->SetStance(EOperativeStance::Crouching);
			return true;
		}
		case 4:
		{
			AActor* Brute = State.Far.Get();
			Commander->ForcedCritRollForTesting = 1.f;
			const float Before = HealthOf(Brute);
			Commander->ShootAtTarget(Brute, 1.f);
			const float Crit = Before - HealthOf(Brute);
			Commander->bForceHitForTesting = false;
			Check(State, State.Plain > 0.f && FMath::IsNearlyEqual(Crit, State.Plain * 2.5f, 0.6f) && Floating->HasShown(TEXT("КРИТ x2!")),
				FString::Printf(TEXT("plain %.1f, crouched crit %.1f (x2.5)"), State.Plain, Crit));
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
		TEXT("CodexTactics.SquadFireSmoke"),
		TEXT("Dev check: squad fire barricade rules, flank switch delay, crit / stance damage; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
