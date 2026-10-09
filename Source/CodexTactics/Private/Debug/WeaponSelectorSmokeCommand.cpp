// Dev-only console command for a headless weapon selector check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.WeaponSelectorSmoke
// 1. the squad carries the Godot arsenal (M16 in hands, pistol, grenade, knife); 2. the weapon slot opens
// «ВЫБОР ВООРУЖЕНИЯ» with the M16 line marked «EQUIPPED»; 3. the pistol / knife are taken (slot text follows, the
// selector closes), the grenade starts the throw aim (cancel: the rifle comes back); 4. in turn-based combat the selector
// switches the active operative's weapon (Godot main.gd _select_weapon_from_selector, switch_active_unit_weapon_to).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Blueprint/UserWidget.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/GrenadeSubsystem.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Data/WeaponDataAsset.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "TimerManager.h"
#include "UI/ActionBarWidget.h"

namespace WeaponSelectorSmoke
{
	constexpr float StepSeconds = 0.25f;

	struct FState
	{
		float Time = 0.f;
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
		FPlatformMisc::RequestExit(false, TEXT("WeaponSelectorSmoke"));
		return false;
	}

	FString WeaponId(const AOperativeCharacter* Operative)
	{
		return Operative && Operative->CurrentWeapon ? Operative->CurrentWeapon->WeaponId : FString();
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time < 3.f)
		{
			return World != nullptr;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad->GetLeader();
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		UActionBarWidget* Bar = PC ? CreateWidget<UActionBarWidget>(PC, UActionBarWidget::StaticClass()) : nullptr;
		if (!Leader || !Bar)
		{
			Check(State, false, TEXT("leader and action bar"));
			return Finish(State, false);
		}
		bool bArsenal = true;
		for (const AOperativeCharacter* Member : Squad->GetMembers())
		{
			// User request 2026-10-09: the Medic-Sapper also carries the sniper rifle (5 weapons).
			const int32 Expected = Member->SquadRole == EOperativeRole::MedicSapper ? 5 : 4;
			bArsenal &= Member->AvailableWeapons.Num() == Expected && WeaponId(Member) == TEXT("m16") && Member->ReserveAmmo == 60;
		}
		Check(State, bArsenal, TEXT("every operative: 4 weapons (the Medic-Sapper 5: + sniper rifle), M16 in hands, reserve 60"));

		Check(State, !Bar->IsWeaponSelectorOpen(), TEXT("selector closed at start"));
		Bar->ToggleWeaponSelector();
		Check(State, Bar->IsWeaponSelectorOpen(), TEXT("weapon slot opens the selector"));
		const FString M16Line = Bar->GetSelectorText(0).ToString();
		Check(State, M16Line.Contains(TEXT("[1] M16 Rifle [30 / 60]")) && M16Line.Contains(TEXT("EQUIPPED")), M16Line);
		const FString PistolLine = Bar->GetSelectorText(1).ToString();
		Check(State, PistolLine.Contains(TEXT("[2] Beretta Pistol")) && !PistolLine.Contains(TEXT("EQUIPPED")), PistolLine);
		Check(State, Bar->GetSelectorText(2).ToString().Contains(TEXT("[3] Grenade [x2 | 85 dmg | R:4.0m]")), Bar->GetSelectorText(2).ToString());

		Check(State, Bar->SelectWeapon(TEXT("pistol")) && WeaponId(Leader) == TEXT("pistol"), TEXT("pistol taken"));
		Check(State, !Bar->IsWeaponSelectorOpen(), TEXT("selector closes after the choice"));
		UGrenadeSubsystem* Grenades = World->GetSubsystem<UGrenadeSubsystem>();
		Check(State, Bar->SelectWeapon(TEXT("grenade")) && WeaponId(Leader) == TEXT("grenade") && Grenades->IsAiming(),
			TEXT("grenade taken: throw aim starts"));
		Grenades->CancelAim(true);
		Check(State, !Grenades->IsAiming() && WeaponId(Leader) == TEXT("m16"), TEXT("cancelled aim: the rifle is back"));
		const bool bKnife = Bar->SelectWeapon(TEXT("knife"));
		const FString KnifeSlot = Bar->GetWeaponText().ToString();
		Check(State, bKnife && KnifeSlot.StartsWith(TEXT("Knife")), FString::Printf(TEXT("knife taken, slot: %s"), *KnifeSlot.Replace(TEXT("\n"), TEXT(" / "))));
		Check(State, !Leader->UsesAmmo(), TEXT("the knife uses no ammo"));

		// Turn-based: the selector goes through the combat for the active operative.
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		Flow->TriggerCombatZone();
		Flow->FinishCutscene();
		Flow->FinishPreparation();
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			It->Destroy();
		}
		World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound, Leader->GetActorLocation() + Leader->GetActorForwardVector() * 600.f);
		UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
		Check(State, Flow->RequestEnterTurnBased(true) == EGameFlowResult::Ok && TurnBased->IsActive(), TEXT("turn-based combat started"));
		AOperativeCharacter* Active = TurnBased->GetActiveUnit();
		const int32 AP = Active ? TurnBased->GetUnitState(Active)->AP : -1;
		Check(State, Bar->SelectWeapon(TEXT("m16")) && WeaponId(Active) == TEXT("m16"), TEXT("turn-based: active operative takes the M16"));
		Check(State, TurnBased->GetUnitState(Active)->AP == AP, TEXT("switching costs no AP"));
		return Finish(State, true);
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
		TEXT("CodexTactics.WeaponSelectorSmoke"),
		TEXT("Dev check: arsenal, weapon selector, switching (also in turn-based combat); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
