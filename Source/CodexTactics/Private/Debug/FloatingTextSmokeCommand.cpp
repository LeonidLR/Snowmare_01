// Dev-only console command for a headless floating combat text / operative damage check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.FloatingTextSmoke
// Operative hits follow Godot player.gd take_damage: a luck dodge floats «💨 УКЛОНЕНИЕ!» and costs nothing; a hit takes
// max(1, amount * stance defense * (1 - fortitude cut)) with «-N»; a crit floats «💥 КРИТИЧЕСКИЙ УДАР! -N»; a bypass hit
// (grenade / trap) ignores dodge and cuts. Enemy numbers carry the armor prefix (brute «🛡️ -N»); a medkit floats
// «+N HP»; guard floats «🛡️ ОБОРОНА: ФИКСАЦИЯ». Texts expire after their duration.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/PersonalItemRules.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Survival/ColdSurvivalComponent.h"
#include "TimerManager.h"
#include "UI/FloatingTextSubsystem.h"

namespace FloatingTextSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("FloatingTextSmoke"));
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
		UFloatingTextSubsystem* Floating = World->GetSubsystem<UFloatingTextSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad->GetLeader();
		if (!Floating || !Leader || !Leader->HealthComponent || State.Time > 30.f)
		{
			Check(State, false, TEXT("floating texts, leader / timeout"));
			return Finish(State, false);
		}
		UHealthComponent* Health = Leader->HealthComponent;
		switch (State.Stage)
		{
		case 0:
		{
			const float Before = Health->GetCurrentHealth();
			Leader->ForcedDodgeRollForTesting = 1.f;
			Check(State, Leader->TakeHit(30.f, TEXT("Тест")) == 0.f && Health->GetCurrentHealth() == Before && Floating->HasShown(TEXT("УКЛОНЕНИЕ")),
				TEXT("dodge: no damage, «УКЛОНЕНИЕ»"));

			Leader->SetStance(EOperativeStance::Crouching);
			const float Fortitude = Leader->ColdSurvival ? Leader->ColdSurvival->Fortitude : 15.f;
			const float Expected = FMath::Max(1.f, 40.f * 0.75f * (1.f - FMath::Clamp(Fortitude * 0.015f, 0.f, 0.5f)));
			Leader->ForcedDodgeRollForTesting = 0.f;
			const float Taken = Leader->TakeHit(40.f, TEXT("Тест"));
			Check(State, FMath::IsNearlyEqual(Taken, Expected, 0.01f) && FMath::IsNearlyEqual(Health->GetCurrentHealth(), Before - Expected, 0.01f)
				&& Floating->HasShown(FString::Printf(TEXT("-%d"), FMath::FloorToInt(Expected))),
				FString::Printf(TEXT("crouched hit: %.2f (expected %.2f, fortitude %.0f)"), Taken, Expected, Fortitude));

			Leader->ForcedDodgeRollForTesting = 0.f;
			Leader->TakeHit(20.f, TEXT("Тест"), true);
			Check(State, Floating->HasShown(TEXT("КРИТИЧЕСКИЙ УДАР! -")), TEXT("crit text"));

			const float BeforeBypass = Health->GetCurrentHealth();
			Leader->ForcedDodgeRollForTesting = 1.f;
			Check(State, FMath::IsNearlyEqual(Leader->TakeHit(10.f, TEXT("Граната"), false, true), 10.f)
				&& FMath::IsNearlyEqual(Health->GetCurrentHealth(), BeforeBypass - 10.f), TEXT("bypass: no dodge, no cut"));
			Leader->ForcedDodgeRollForTesting = -1.f;
			Leader->SetStance(EOperativeStance::Standing);

			Leader->MedkitsCount = FMath::Max(Leader->MedkitsCount, 1);
			Check(State, Leader->UsePersonalItem(EPersonalItem::Medkit) && Floating->HasShown(TEXT(" HP")) && Floating->HasShown(TEXT("+")),
				TEXT("medkit: «+N HP»"));

			Squad->ToggleGuard(Squad->GetMembers()[1]);
			Check(State, Floating->HasShown(TEXT("ОБОРОНА: ФИКСАЦИЯ")), TEXT("guard text"));
			Squad->ToggleGuard(Squad->GetMembers()[1]);
			Check(State, Floating->HasShown(TEXT("В СТРОЙ")), TEXT("guard off text"));

			AEnemyCharacter* Brute = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::Brute,
				Leader->GetActorLocation() + Leader->GetActorForwardVector() * 900.f);
			if (Brute)
			{
				Brute->CustomTimeDilation = 0.f;
				FDamageSpec Spec;
				Spec.Amount = 100.f;
				Spec.DamageType = EDamageType::Kinetic;
				Brute->FindComponentByClass<UHealthComponent>()->TakeDamage(Spec);
			}
			Check(State, Brute && Floating->HasShown(TEXT("🛡️ -")), TEXT("brute armor number «🛡️ -N»"));
			Check(State, Floating->GetTexts().Num() >= 6, FString::Printf(TEXT("%d texts live"), Floating->GetTexts().Num()));
			++State.Stage;
			State.StageTime = 0.f;
			return true;
		}
		case 1:
			if (State.StageTime < 2.5f)
			{
				return true;
			}
			Check(State, Floating->GetTexts().IsEmpty(), FString::Printf(TEXT("texts expired (%d left)"), Floating->GetTexts().Num()));
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
		TEXT("CodexTactics.FloatingTextSmoke"),
		TEXT("Dev check: operative hit formula (dodge, stance, fortitude, crit, bypass), enemy / heal / guard floating texts; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
