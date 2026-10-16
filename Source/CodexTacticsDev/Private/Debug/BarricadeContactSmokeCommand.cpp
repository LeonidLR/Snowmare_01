// Dev-only console command for a headless check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.BarricadeContactSmoke
// Godot barricade.gd contact damage: a fire barricade burns an enemy standing next to it (half the contact damage
// every interval + burning), and strikes back at an enemy hitting it (the full contact damage).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
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

namespace BarricadeContactSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		float StartHealth = 0.f;
		TWeakObjectPtr<AEnemyCharacter> Hound;
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
		FPlatformMisc::RequestExit(false, TEXT("BarricadeContactSmoke"));
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 30.f)
		{
			return World ? Finish(State, false) : false;
		}
		switch (State.Stage)
		{
		case 0:
		{
			const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
			if (State.StageTime < 3.f || !Squad->GetLeader())
			{
				return true;
			}
			// Far from the squad: nobody shoots, the hound stays frozen next to the barricade.
			const FVector Spot = Squad->GetLeader()->GetActorLocation() + Squad->GetLeader()->GetActorForwardVector() * 2500.f;
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ABarricadeActor* Barricade = World->SpawnActor<ABarricadeActor>(Spot, FRotator::ZeroRotator, Params);
			Barricade->ContactType = EBarricadeContact::Fire;
			Barricade->ContactDamage = 10.f;
			State.Barricade = Barricade;
			AEnemyCharacter* Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound, Spot + FVector(0.f, 150.f, 40.f));
			Check(State, Hound != nullptr, TEXT("hound next to a fire barricade"));
			if (!Hound)
			{
				return Finish(State, false);
			}
			Hound->CustomTimeDilation = 0.f;
			State.Hound = Hound;
			State.StartHealth = Hound->GetHealthComponent()->GetCurrentHealth();
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			const UHealthComponent* Health = State.Hound.IsValid() ? State.Hound->GetHealthComponent() : nullptr;
			Check(State, Health && Health->GetCurrentHealth() < State.StartHealth,
				FString::Printf(TEXT("contact damage (%.0f -> %.0f HP)"), State.StartHealth, Health ? Health->GetCurrentHealth() : 0.f));
			Check(State, Health && Health->HasStatusEffect(EStatusEffect::Burning), TEXT("set on fire"));
			const float Before = Health ? Health->GetCurrentHealth() : 0.f;
			State.Barricade->RetaliateAgainst(State.Hound.Get());
			Check(State, Health && Health->GetCurrentHealth() < Before, TEXT("strikes back at an enemy hitting it"));
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
		TEXT("CodexTactics.BarricadeContactSmoke"),
		TEXT("Dev check: barricade contact damage (area tick with burning, retaliation); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
