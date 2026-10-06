// Dev-only headless check of the squad number keys in a real-time wave fight and in the tactical pause on L_MovementTest
// (user report 2026-10-05: «1-4 do nothing in the real-time fight / the pause, the camera does not follow»):
//   Scripts/smoke.ps1 -Command CodexTactics.RealtimeSelectSmoke -Log Smoke-RealtimeSelect.log
// «Начать бой»; the keys 2, 3, 1 (real key events through Enhanced Input) in real time, then 2, 1 in the tactical pause:
// each makes that operative the leader and the camera follows him. Orders (RTS control, user request 2026-10-06 — the
// 2026-10-05 lock is gone): a medkit works in real time and in the pause; the Commander Mode switch works in real time.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/TacticalCameraPawn.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Core/CodexTacticsPlayerController.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"

namespace RealtimeSelectSmoke
{
	struct FKeyCase
	{
		FKey Key;
		int32 SquadIndex;
		bool bPause;
	};

	struct FState
	{
		int32 Step = 0;
		int32 Failures = 0;
	};

	void PressKey(APlayerController* PC, const FKey& Key, EInputEvent Event)
	{
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Event == IE_Pressed ? 1.f : 0.f));
	}

	bool Finish(const FState& State)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("RealtimeSelectSmoke"));
		return false;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		const int32 Step = State.Step++;
		if (Step < 12)
		{
			if (Step == 11)
			{
				Flow->TriggerCombatZone();
				Flow->FinishCutscene();
				Flow->FinishPreparation();
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					Member->HealthComponent->SetMaxHealth(100000.f);
				}
			}
			return true;
		}
		static const FKeyCase Cases[] = {
			{ EKeys::Two, 1, false }, { EKeys::Three, 2, false }, { EKeys::One, 0, false }, { EKeys::Two, 1, true }, { EKeys::One, 0, true } };
		ACodexTacticsPlayerController* CodexPC = Cast<ACodexTacticsPlayerController>(PC);
		AOperativeCharacter* Leader = Squad->GetLeader();
		if (Step == 12 && CodexPC && Leader)
		{
			Leader->MedkitsCount = 2;
			Leader->HealthComponent->ApplyDirectHealthLoss(50000.f, TEXT("smoke"));
			const float Before = Leader->HealthComponent->GetCurrentHealth();
			CodexPC->UseSquadItem(EPersonalItem::Medkit);
			const bool bRefused = Leader->MedkitsCount == 1 && Leader->HealthComponent->GetCurrentHealth() > Before; // used at once
			const bool bWasAuto = Squad->IsAutonomousSquadCombat();
			CodexPC->ToggleAutonomy();
			const bool bToggled = Squad->IsAutonomousSquadCombat() != bWasAuto;
			CodexPC->ToggleAutonomy();
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: real time: medkit used at once %d, Commander Mode switch works %d"),
				bRefused && bToggled ? TEXT("ok  ") : TEXT("FAIL"), bRefused ? 1 : 0, bToggled ? 1 : 0);
			State.Failures += bRefused && bToggled ? 0 : 1;
		}
		if (Step == 12 + 4 * 5 && CodexPC && Leader && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause)
		{
			const int32 Medkits = Leader->MedkitsCount;
			CodexPC->UseSquadItem(EPersonalItem::Medkit);
			const bool bUsed = Leader->MedkitsCount == Medkits - 1;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: pause: medkit used %d"), bUsed ? TEXT("ok  ") : TEXT("FAIL"), bUsed ? 1 : 0);
			State.Failures += bUsed ? 0 : 1;
		}
		const int32 CaseStep = Step - 12;
		const int32 CaseIndex = CaseStep / 4;
		if (CaseIndex >= UE_ARRAY_COUNT(Cases))
		{
			return Finish(State);
		}
		const FKeyCase& Case = Cases[CaseIndex];
		switch (CaseStep % 4)
		{
		case 0:
			if (Case.bPause && Flow->GetCombatMode() != ECodexCombatMode::TacticalPause)
			{
				Flow->ToggleTacticalPause();
			}
			break;
		case 1:
			PressKey(PC, Case.Key, IE_Pressed);
			break;
		case 2:
			PressKey(PC, Case.Key, IE_Released);
			break;
		default:
		{
			const AOperativeCharacter* Picked = Squad->GetLeader();
			const ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(PC->GetPawn());
			const bool bOk = Picked && Picked->SquadIndex == Case.SquadIndex && Camera && Camera->GetFollowTarget() == Picked;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: key %s (%s, mode %d): leader %s, camera on %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"),
				*Case.Key.ToString(), Case.bPause ? TEXT("pause") : TEXT("real time"), static_cast<int32>(Flow->GetCombatMode()),
				Picked ? *Picked->DisplayName.ToString() : TEXT("none"), Camera && Camera->GetFollowTarget() ? *Camera->GetFollowTarget()->GetName() : TEXT("none"));
			State.Failures += bOk ? 0 : 1;
			break;
		}
		}
		return true;
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), 0.25f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.RealtimeSelectSmoke"),
		TEXT("Dev check: keys 1-3 select the leader (camera follows) in a real-time wave fight and in the tactical pause."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
