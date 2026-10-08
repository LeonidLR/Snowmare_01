// Dev-only console command for a headless AI grenade / empty-weapon switch check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.AIGrenadeSmoke
// Godot player.gd _evaluate_ai_grenade_opportunity / execute_ai_grenade_throw / _auto_switch_on_empty: a pack of three
// hounds 9 m ahead in the fight draws an autonomous grenade with a radio callout; an operative whose M16 is dry switches
// to the pistol, and with no pistol rounds to the knife.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "UI/GameMessageSubsystem.h"

namespace AIGrenadeSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		int32 GrenadesBefore = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("AIGrenadeSmoke"));
		return false;
	}

	int32 SquadGrenades(USquadSubsystem* Squad)
	{
		int32 Count = 0;
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			Count += Member->GrenadesCount;
		}
		return Count;
	}

	bool HasCallout(UWorld* World)
	{
		for (const FGameMessage& Message : World->GetSubsystem<UGameMessageSubsystem>()->GetHistory())
		{
			const FString Text = Message.Text.ToString();
			if (Text.Contains(TEXT("Grenade out")) || Text.Contains(TEXT("Catch this")) || Text.Contains(TEXT("Grenade away"))
				|| Text.Contains(TEXT("Eat this")))
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
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad->GetLeader();
		if (!Flow || !Leader || State.Time > 30.f)
		{
			Check(State, false, FString::Printf(TEXT("squad / timeout (stage %d)"), State.Stage));
			return Finish(State, false);
		}
		switch (State.Stage)
		{
		case 0:
		{
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy(); // only the test pack
			}
			UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
			for (const FVector& Offset : { FVector(900.f, 0.f, 100.f), FVector(1000.f, 100.f, 100.f), FVector(950.f, -150.f, 100.f) })
			{
				if (AEnemyCharacter* Hound = Waves->SpawnEnemy(EEnemyArchetype::FrostHound, SmokeUtils::LevelPoint(World, Offset), SmokeUtils::LayoutTransform(World).Rotator()))
				{
					Hound->CustomTimeDilation = 0.f;
				}
			}
			State.GrenadesBefore = SquadGrenades(Squad);
			++State.Stage;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
			if (SquadGrenades(Squad) == State.GrenadesBefore && State.StageTime < 5.f)
			{
				return true;
			}
			Check(State, SquadGrenades(Squad) < State.GrenadesBefore && HasCallout(World),
				FString::Printf(TEXT("pack of 3 at 9 m: grenade thrown with a callout (%d -> %d)"), State.GrenadesBefore, SquadGrenades(Squad)));
		{
			// Empty M16: the pistol, then the knife.
			AOperativeCharacter* Medic = Squad->GetMembers()[2];
			Medic->SwitchToWeaponById(TEXT("m16"));
			Medic->CurrentClip = 0;
			Medic->ReserveAmmo = 0;
			Medic->GrenadesCount = 0;
			Medic->AutoSwitchOnEmpty();
			const bool bPistol = Medic->CurrentWeapon && Medic->CurrentWeapon->WeaponId == TEXT("pistol");
			Medic->CurrentClip = 0;
			Medic->ReserveAmmo = 0;
			Medic->AutoSwitchOnEmpty();
			const bool bKnife = Medic->CurrentWeapon && Medic->CurrentWeapon->WeaponId == TEXT("knife");
			Check(State, bPistol && bKnife, TEXT("empty M16 -> pistol, empty pistol -> knife"));
		}
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
		TEXT("CodexTactics.AIGrenadeSmoke"),
		TEXT("Dev check: autonomous grenade at a cluster, empty-weapon switch to pistol / knife; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
