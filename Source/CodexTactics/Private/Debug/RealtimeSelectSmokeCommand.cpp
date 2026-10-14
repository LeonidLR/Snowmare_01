// Dev-only headless check of the squad number keys in a real-time wave fight and in the tactical pause on L_MovementTest
// (user report 2026-10-05: «1-4 do nothing in the real-time fight / the pause, the camera does not follow»):
//   Scripts/smoke.ps1 -Command CodexTactics.RealtimeSelectSmoke -Log Smoke-RealtimeSelect.log
// «Начать бой»; the keys 2, 3, 1 (real key events through Enhanced Input) in real time, then 2, 1 in the tactical pause:
// each makes that operative the leader and the camera follows him. Orders (RTS control, user request 2026-10-06 — the
// 2026-10-05 lock is gone): a medkit works in real time and in the pause; the Commander Mode switch works in real time.
// Rev 5g (game designer 2026-10-14): selecting another operative never cancels an order (StepOrderKeep).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Debug/SmokeUtils.h"
#include "Camera/TacticalCameraPawn.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Core/CodexTacticsPlayerController.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "EngineUtils.h"
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
		FVector TargetA = FVector::ZeroVector;
		FVector TargetB = FVector::ZeroVector;
		float MinA = 1.0e9f;
		float MinB = 1.0e9f;
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

	/**
	 * Rev 5g (game designer 2026-10-14): in the real-time fight a selection never cancels an order. A (roster 0) is sent 8 m away,
	 * key 2 selects B, B is sent 8 m away, key 1 selects A again (selecting him used to StopOperative him): both reach their goals.
	 * Enemies are parked far below the map so nothing reacts.
	 */
	bool StepOrderKeep(UWorld* World, FState& State, int32 KeepStep)
	{
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
		if (Members.Num() < 2 || !PC)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke FAIL: order keep - squad / controller missing"));
			++State.Failures;
			return Finish(State);
		}
		AOperativeCharacter* A = Members[0];
		AOperativeCharacter* B = Members[1];
		switch (KeepStep)
		{
		case 0:
			if (Flow->GetCombatMode() == ECodexCombatMode::TacticalPause)
			{
				Flow->ToggleTacticalPause();
			}
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->CustomTimeDilation = 0.f;
				It->SetActorLocation(It->GetActorLocation() + FVector(0.f, 0.f, -5000.f), false, nullptr, ETeleportType::TeleportPhysics);
			}
			// A clean stretch (the test start corridor, away from the fight's corpses) and no fire / grenades of their own.
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			SmokeUtils::PlaceSquadAtTestStart(World);
			return true;
		case 4:
			Squad->SetLeaderByIndex(0);
			State.TargetA = SmokeUtils::ClearPoint(World, A->GetActorLocation(), A->GetActorLocation() + A->GetActorForwardVector() * 800.f);
			A->OrderMoveTo(State.TargetA, false);
			PressKey(PC, EKeys::Two, IE_Pressed);
			return true;
		case 5:
			PressKey(PC, EKeys::Two, IE_Released);
			State.TargetB = SmokeUtils::ClearPoint(World, B->GetActorLocation(), B->GetActorLocation() + B->GetActorForwardVector() * 800.f);
			B->OrderMoveTo(State.TargetB, false);
			PressKey(PC, EKeys::One, IE_Pressed);
			return true;
		case 6:
		{
			PressKey(PC, EKeys::One, IE_Released);
			const bool bOk = Squad->GetLeader() == A && A->HasOwnMoveOrder() && B->HasOwnMoveOrder()
				&& Flow->GetCombatMode() == ECodexCombatMode::RealTime;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: real time: A ordered, key 2, B ordered, key 1: both keep their orders (leader %s, A %d, B %d)"),
				bOk ? TEXT("ok  ") : TEXT("FAIL"), Squad->GetLeader() ? *Squad->GetLeader()->DisplayName.ToString() : TEXT("none"),
				A->HasOwnMoveOrder() ? 1 : 0, B->HasOwnMoveOrder() ? 1 : 0);
			State.Failures += bOk ? 0 : 1;
			State.MinA = State.MinB = 1.0e9f;
			return true;
		}
		default:
			break;
		}
		if (KeepStep < 6)
		{
			return true;
		}
		State.MinA = FMath::Min(State.MinA, static_cast<float>(FVector::Dist2D(A->GetActorLocation(), State.TargetA)));
		State.MinB = FMath::Min(State.MinB, static_cast<float>(FVector::Dist2D(B->GetActorLocation(), State.TargetB)));
		if (KeepStep % 8 == 0)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[RealtimeSelect] A %.0f cm/s order %d own %d, B %.0f cm/s order %d own %d, mode %d"),
				A->GetVelocity().Size2D(), A->IsMoving() ? 1 : 0, A->HasOwnMoveOrder() ? 1 : 0, B->GetVelocity().Size2D(), B->IsMoving() ? 1 : 0,
				B->HasOwnMoveOrder() ? 1 : 0, static_cast<int32>(Flow->GetCombatMode()));
		}
		const bool bDone = State.MinA < 150.f && State.MinB < 150.f;
		if (!bDone && KeepStep < 6 + 60)
		{
			return true;
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: real time: both reached their ordered goals after the selection switches (A %.0f cm, B %.0f cm off)"),
			bDone ? TEXT("ok  ") : TEXT("FAIL"), State.MinA, State.MinB);
		State.Failures += bDone ? 0 : 1;
		return Finish(State);
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
				SmokeUtils::StartWaveFight(Flow); // sudden horde start, its free pause released
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
			return StepOrderKeep(World, State, CaseStep - 4 * UE_ARRAY_COUNT(Cases));
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
