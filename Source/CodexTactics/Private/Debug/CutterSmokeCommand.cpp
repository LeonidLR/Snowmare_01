// Dev-only console command for a headless cutter check on L_MovementTest (exploration, the squad holds fire):
//   Scripts/smoke.ps1 -Command CodexTactics.CutterSmoke
// Godot enemy_cutter.gd: 75 HP / 18 damage stats; 6 m from the commander it pounces (jump config from
// DA_EnemyAnim_cutter), the landing hurts the squad in 2.2 m ("POUNCE"), then a 6 s cooldown; shot down mid-leap it
// crashes («SHOT DOWN MID-AIR», «CRASH»).

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
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interactables/InteractableActor.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UI/FloatingTextSubsystem.h"

namespace CutterSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bSawJump = false;
		float SquadHealth = 0.f;
		TWeakObjectPtr<AEnemyCharacter> Cutter;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("CutterSmoke"));
		return false;
	}

	float SquadHealth(USquadSubsystem* Squad)
	{
		float Sum = 0.f;
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			Sum += Member->HealthComponent->GetCurrentHealth();
		}
		return Sum;
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
		UFloatingTextSubsystem* Floating = World->GetSubsystem<UFloatingTextSubsystem>();
		if (State.Time > 30.f)
		{
			Check(State, false, FString::Printf(TEXT("timeout (stage %d)"), State.Stage));
			return Finish(State, false);
		}
		auto Next = [&State]() { ++State.Stage; State.StageTime = 0.f; };
		switch (State.Stage)
		{
		case 0:
		{
			// Small enemies rush the level generator first (Godot weight 0.4): set it aside so the cutter goes for the squad.
			for (TActorIterator<AInteractableActor> It(World); It; ++It)
			{
				if (It->ObjectType == EInteractableType::Generator)
				{
					It->bGeneratorBroken = true;
				}
			}
			State.Cutter = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Cutter,
				SmokeUtils::LevelPoint(World, FVector(600.f, 0.f, 100.f)), SmokeUtils::LayoutTransform(World).Rotator() + FRotator(0.f, 180.f, 0.f));
			AEnemyCharacter* Cutter = State.Cutter.Get();
			Check(State, Cutter && FMath::IsNearlyEqual(Cutter->GetHealthComponent()->GetMaxHealth(), 75.f) && FMath::IsNearlyEqual(Cutter->GetAttackDamage(), 18.f),
				TEXT("cutter stats: 75 HP, 18 damage"));
			State.SquadHealth = SquadHealth(Squad);
			Next();
			return true;
		}
		case 1:
		{
			AEnemyCharacter* Cutter = State.Cutter.Get();
			State.bSawJump |= Cutter && Cutter->IsJumpAttacking();
			if (!(State.bSawJump && Cutter && !Cutter->IsJumpAttacking()) && State.StageTime < 5.f)
			{
				return true;
			}
			Check(State, State.bSawJump, TEXT("cutter pounced from 6 m"));
			Check(State, SquadHealth(Squad) < State.SquadHealth || Floating->HasShown(TEXT("DODGE")), TEXT("landing hurt the squad"));
			Check(State, Floating->HasShown(TEXT("POUNCE")) && Cutter && Cutter->GetJumpCooldown() > 4.f,
				FString::Printf(TEXT("\"POUNCE\" and the 6 s cooldown (%.1f)"), Cutter ? Cutter->GetJumpCooldown() : -1.f));
			if (Cutter)
			{
				Cutter->Destroy();
			}
			// Shot down mid-leap.
			State.Cutter = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Cutter,
				SmokeUtils::LevelPoint(World, FVector(-800.f, 900.f, 100.f)), FRotator::ZeroRotator);
			State.bSawJump = false;
			Next();
			return true;
		}
		case 2:
		{
			AEnemyCharacter* Cutter = State.Cutter.Get();
			if (Cutter && Cutter->IsJumpAttacking() && Cutter->GetCharacterMovement()->IsFalling())
			{
				Cutter->GetHealthComponent()->ApplyDirectHealthLoss(1000.f, TEXT("Smoke"));
				Next();
				return true;
			}
			if (State.StageTime > 6.f)
			{
				Check(State, false, TEXT("second cutter never took off"));
				return Finish(State, false);
			}
			return true;
		}
		case 3:
			if (!Floating->HasShown(TEXT("CRASH")) && State.StageTime < 3.f)
			{
				return true;
			}
			Check(State, Floating->HasShown(TEXT("SHOT DOWN MID-AIR")) && Floating->HasShown(TEXT("CRASH")), TEXT("shot down mid-leap: crash landing"));
			return Finish(State, true);
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
		TEXT("CodexTactics.CutterSmoke"),
		TEXT("Dev check: cutter stats, pounce, landing damage, cooldown, airborne death; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
