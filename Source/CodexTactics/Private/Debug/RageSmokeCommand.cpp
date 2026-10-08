// Dev-only console command for a headless rage check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.RageSmoke
// Godot rage_component.gd: two crits from the same hound (forced roll) put the commander into rage («ENRAGED!», radio
// shout); orders are refused («IGNORING ORDERS»); in the fight he sprays the enemies in reach without spending
// rounds; when the rage runs out the calm-down line follows.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/RageComponent.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UI/FloatingTextSubsystem.h"
#include "UI/GameMessageSubsystem.h"

namespace RageSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		int32 Clip = 0;
		float HoundHealth = 0.f;
		bool bAuraSeen = false;
		TWeakObjectPtr<AEnemyCharacter> Hound;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("RageSmoke"));
		return false;
	}

	float EnemyHealthSum(UWorld* World)
	{
		float Sum = 0.f;
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			Sum += It->FindComponentByClass<UHealthComponent>()->GetCurrentHealth();
		}
		return Sum;
	}

	bool HasMessage(UWorld* World, const FString& Part)
	{
		for (const FGameMessage& Message : World->GetSubsystem<UGameMessageSubsystem>()->GetHistory())
		{
			if (Message.Text.ToString().Contains(Part))
			{
				return true;
			}
		}
		return false;
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
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		AOperativeCharacter* Commander = World->GetSubsystem<USquadSubsystem>()->GetLeader();
		UFloatingTextSubsystem* Floating = World->GetSubsystem<UFloatingTextSubsystem>();
		if (!Flow || !Commander || !Commander->RageComponent || State.Time > 40.f)
		{
			Check(State, false, FString::Printf(TEXT("commander / timeout (stage %d)"), State.Stage));
			return Finish(State, false);
		}
		URageComponent* Rage = Commander->RageComponent;
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			It->CustomTimeDilation = 0.f; // targets only
		}
		switch (State.Stage)
		{
		case 0:
		{
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			State.Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
				Commander->GetActorLocation() + Commander->GetActorForwardVector() * 700.f);
			if (!State.Hound.IsValid())
			{
				Check(State, false, TEXT("hound spawned"));
				return Finish(State, false);
			}
			Check(State, !Commander->IsRaging() && Rage->Config.RequiredCrits == 2 && FMath::IsNearlyEqual(Rage->Config.Duration, 9.f),
				FString::Printf(TEXT("commander rage config: %d crits, %.1f s"), Rage->Config.RequiredCrits, Rage->Config.Duration));
			Commander->ForcedDodgeRollForTesting = 0.f;
			Commander->TakeHit(5.f, TEXT("Hound"), true, false, State.Hound.Get());
			Check(State, !Commander->IsRaging(), TEXT("one crit: no rage yet"));
			Commander->ForcedDodgeRollForTesting = 0.f;
			Rage->ForcedRollForTesting = 0.f;
			Commander->TakeHit(5.f, TEXT("Hound"), true, false, State.Hound.Get());
			Check(State, Commander->IsRaging() && Floating->HasShown(TEXT("ENRAGED!")) && HasMessage(World, TEXT("tear you all to pieces")),
				TEXT("second crit from the same hound: rage, text, shout"));
			Check(State, Commander->OrderMoveTo(Commander->GetActorLocation() + FVector(300.f, 0.f, 0.f), false) == EOperativeOrderResult::Refused
				&& Floating->HasShown(TEXT("IGNORING ORDERS")), TEXT("orders refused"));
			Rage->Config.Duration = 3.f; // shorten the wait
			State.Clip = Commander->CurrentClip;
			State.HoundHealth = EnemyHealthSum(World); // the chaotic target may be any enemy in reach
			++State.Stage;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
			if (State.StageTime < 1.5f)
			{
				return true;
			}
		{
			const float Health = EnemyHealthSum(World);
			Check(State, Commander->CurrentClip == State.Clip && Health < State.HoundHealth,
				FString::Printf(TEXT("sprayed without spending rounds (clip %d -> %d, enemies %.0f -> %.0f HP)"), State.Clip, Commander->CurrentClip, State.HoundHealth, Health));
			++State.Stage;
			State.StageTime = 0.f;
			return true;
		}
		case 2:
			State.bAuraSeen |= Rage->IsAuraShown();
			if (Commander->IsRaging() && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, State.bAuraSeen && !Rage->IsAuraShown(), TEXT("fiery aura under the feet while raging, gone after"));
			Check(State, !Commander->IsRaging() && HasMessage(World, TEXT("the rage is gone")), TEXT("rage wears off with the calm line"));
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
		TEXT("CodexTactics.RageSmoke"),
		TEXT("Dev check: rage from two crits, refused orders, chaotic fire without ammo, calm-down; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
