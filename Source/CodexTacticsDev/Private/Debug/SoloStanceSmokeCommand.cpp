// Dev-only headless check of the game designer's PIE report 2026-11-08 on L_MovementTest:
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands SoloStanceSmoke
// "I switched the operatives to SOLO mode. I gave one operative the order to go prone. I switched to the next one and gave him the same order -
// he went prone. But the previous one got up into a crouch. In solo mode every operative must be autonomous. They may run together via box
// select, but they must execute all orders autonomously."
// Root cause: USquadSubsystem::SetLeader in Solo re-armed Solo's guard crouch on every holding follower - the old leader too - and a prone /
// crouched one "takes it at once" (SquadAutonomyRules::StartSoloGuardCrouch). Fix: KeepsOwnStanceInSolo (Codex.Squad.SoloOwnStance 1).
// The real keys through Enhanced Input (1 / 2 / 3 select, V / C / Z stance), Solo on (B's handler: USquadSubsystem::EnterSoloMode):
//  1. prone: A selected -> V; key 2 -> V: A stays prone at once and hold_crouch_after_sec + 2 s later, B prone; C (no stance order, standing
//     when Solo came on) still takes the guard crouch (Solo's "teammates hold crouched" stays as designed);
//  2. crouch: C selected -> C; key 1 -> C: both crouched, C's crouch still the player's (not turned into the squad's);
//  3. stand: A -> Z; key 3: A still stands hold_crouch_after_sec + 2 s later (no guard crouch over the player's stand);
//  4. box select A + B -> V: both prone, C untouched; key 3: A and B stay prone;
//  5. A/B: Codex.Squad.SoloOwnStance 0 (the old rule): A selected (prone), key 2: A gets up into a crouch - the reported bug reproduces.
// Godot reference: none (UE-only; Godot _sync_squad_stances made the squad copy the leader).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Data/SquadROE.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

namespace SoloStanceSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float Timeout = 240.f;
	/** A key is held this long (game time) before its release. */
	constexpr float KeyHoldSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		/** The key to release on the next step (EKeys::Invalid = none) and when it went down. */
		FKey HeldKey;
		float HeldSince = 0.f;
		bool bDone = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void SetOwnStanceRule(int32 Value)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Squad.SoloOwnStance")))
		{
			CVar->Set(Value, ECVF_SetByConsole);
		}
	}

	bool Finish(UWorld* World, FState& State, bool bComplete)
	{
		SetOwnStanceRule(1);
		if (USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr; Squad && Squad->IsSoloMode())
		{
			Squad->ExitSoloMode(false, 0.f);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.bDone = true;
		FPlatformMisc::RequestExit(false, TEXT("SoloStanceSmoke"));
		return false;
	}

	void NextStage(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
		UE_LOG(LogCodexTactics, Display, TEXT("[SoloStance] stage %d at %.1f s"), State.Stage, State.Time);
	}

	/** Presses a key through the player input (Enhanced Input sees it this frame); it is released KeyHoldSeconds later. */
	void PressKey(APlayerController* PC, FState& State, const FKey& Key)
	{
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1.f));
		State.HeldKey = Key;
		State.HeldSince = State.Time;
	}

	const TCHAR* StanceName(const AOperativeCharacter* Operative)
	{
		if (!Operative)
		{
			return TEXT("-");
		}
		switch (Operative->GetStance())
		{
		case EOperativeStance::Prone: return TEXT("prone");
		case EOperativeStance::Crouching: return TEXT("crouched");
		default: return TEXT("standing");
		}
	}

	FString Who(const AOperativeCharacter* Operative)
	{
		return Operative ? Operative->DisplayName.ToString() : FString(TEXT("?"));
	}

	/** Stance check with the operative's state in the line. */
	void CheckStance(FState& State, const AOperativeCharacter* Operative, EOperativeStance Want, const FString& What)
	{
		Check(State, Operative && Operative->GetStance() == Want, FString::Printf(TEXT("%s (%s: %s, player's %d, squad's %d)"), *What, *Who(Operative),
			StanceName(Operative), Operative && Operative->IsStanceFromPlayer() ? 1 : 0, Operative && Operative->IsStanceFromSquad() ? 1 : 0));
	}

	bool Step(UWorld* World, FState& State, float Dt)
	{
		State.Time += Dt;
		State.StageTime += Dt;
		if (State.Time > Timeout)
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d"), State.Stage));
			return Finish(World, State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
		const TArray<AOperativeCharacter*> Members = Squad ? Squad->GetMembers() : TArray<AOperativeCharacter*>();
		if (!Squad || !PC || Members.Num() < 3)
		{
			Check(State, false, FString::Printf(TEXT("squad of three and a player controller (%d)"), Members.Num()));
			return Finish(World, State, false);
		}
		for (AOperativeCharacter* Member : Members)
		{
			Member->ColdLevel = 0.f; // no frostbite (a frostbitten operative cannot leave prone) on the test map
		}
		if (State.HeldKey.IsValid() && State.Time - State.HeldSince >= KeyHoldSeconds)
		{
			PC->InputKey(FInputKeyEventArgs::CreateSimulated(State.HeldKey, IE_Released, 0.f));
			State.HeldKey = FKey();
		}
		if (State.HeldKey.IsValid())
		{
			return true; // one key at a time
		}
		// Roster order = the number keys (ACodexTacticsPlayerController::SelectMember).
		AOperativeCharacter* A = Members[0];
		AOperativeCharacter* B = Members[1];
		AOperativeCharacter* C = Members[2];
		// The guard crouch's delay and a margin: a stance still held then is kept for good.
		const float LongWait = SquadROE::Get().HoldCrouchAfterSeconds + 2.f;

		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < NavWarmupSeconds)
			{
				return true;
			}
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode(false, 0.f);
			}
			for (AOperativeCharacter* Member : Members)
			{
				Member->StopOperative();
				Member->SetStance(EOperativeStance::Standing); // the smoke's, not the player's
			}
			Squad->SetLeaderByIndex(0);
			Squad->EnterSoloMode(); // B
			Check(State, Squad->IsSoloMode() && Squad->GetLeader() == A, FString::Printf(TEXT("Solo on, %s selected"), *Who(A)));
			NextStage(State);
			return true;
		}

		// --- 1. prone (the designer's exact steps) ---
		case 1:
			if (State.StageTime < 1.f)
			{
				return true;
			}
			PressKey(PC, State, EKeys::V);
			NextStage(State);
			return true;
		case 2:
			if (State.StageTime < 1.f)
			{
				return true;
			}
			CheckStance(State, A, EOperativeStance::Prone, TEXT("V: the selected operative goes prone"));
			CheckStance(State, B, EOperativeStance::Standing, TEXT("... nobody else (B waits for the guard crouch)"));
			PressKey(PC, State, EKeys::Two);
			NextStage(State);
			return true;
		case 3:
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, Squad->GetLeader() == B, FString::Printf(TEXT("key 2 selects %s"), *Who(B)));
			CheckStance(State, A, EOperativeStance::Prone, TEXT("selecting the next operative leaves the previous one prone (was: up into a crouch at once)"));
			PressKey(PC, State, EKeys::V);
			NextStage(State);
			return true;
		case 4:
			if (State.StageTime < LongWait)
			{
				return true;
			}
			CheckStance(State, A, EOperativeStance::Prone, FString::Printf(TEXT("%.0f s later the first one is still prone"), LongWait));
			Check(State, !Squad->IsSoloGuardCrouchWaiting(A), TEXT("... no guard crouch waits for him"));
			CheckStance(State, B, EOperativeStance::Prone, TEXT("the second one prone too"));
			CheckStance(State, C, EOperativeStance::Crouching, TEXT("the third (no stance order, standing when Solo came on) takes Solo's guard crouch as designed"));
			// Reset for the crouch variant: everybody standing (the smoke's stance), C leads, Solo on again.
			Squad->ExitSoloMode(false, 0.f);
			for (AOperativeCharacter* Member : Members)
			{
				Member->StopOperative();
				Member->SetStance(EOperativeStance::Standing);
			}
			Squad->SetLeaderByIndex(2);
			Squad->EnterSoloMode();
			NextStage(State);
			return true;

		// --- 2. crouch ---
		case 5:
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, Squad->IsSoloMode() && Squad->GetLeader() == C, FString::Printf(TEXT("Solo again, %s selected"), *Who(C)));
			PressKey(PC, State, EKeys::C);
			NextStage(State);
			return true;
		case 6:
			if (State.StageTime < 1.f)
			{
				return true;
			}
			CheckStance(State, C, EOperativeStance::Crouching, TEXT("C: the selected operative crouches"));
			PressKey(PC, State, EKeys::One);
			NextStage(State);
			return true;
		case 7:
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, Squad->GetLeader() == A, FString::Printf(TEXT("key 1 selects %s"), *Who(A)));
			PressKey(PC, State, EKeys::C);
			NextStage(State);
			return true;
		case 8:
			if (State.StageTime < LongWait)
			{
				return true;
			}
			CheckStance(State, C, EOperativeStance::Crouching, TEXT("the first one ordered crouched stays crouched"));
			Check(State, C->IsStanceFromPlayer() && !C->IsStanceFromSquad(), TEXT("... and his crouch is still the player's (was: turned into the squad's guard crouch)"));
			CheckStance(State, A, EOperativeStance::Crouching, TEXT("the second one crouched"));
			// --- 3. stand: A (selected) stands up by Z, key 3 selects C.
			PressKey(PC, State, EKeys::Z);
			NextStage(State);
			return true;
		case 9:
			if (State.StageTime < 1.f)
			{
				return true;
			}
			CheckStance(State, A, EOperativeStance::Standing, TEXT("Z: the selected operative stands up"));
			PressKey(PC, State, EKeys::Three);
			NextStage(State);
			return true;
		case 10:
			if (State.StageTime < LongWait)
			{
				return true;
			}
			Check(State, Squad->GetLeader() == C, FString::Printf(TEXT("key 3 selects %s"), *Who(C)));
			CheckStance(State, A, EOperativeStance::Standing, FString::Printf(TEXT("%.0f s after the switch the one the player stood up still stands (no guard crouch)"), LongWait));
			Check(State, !Squad->IsSoloGuardCrouchWaiting(A), TEXT("... no guard crouch waits for him"));
			// --- 4. box select A + B (the selection's leader becomes A), V.
			Squad->SetSelectedGroup({ A, B }, true);
			NextStage(State);
			return true;
		case 11:
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, Squad->HasMultiSelection() && Squad->IsGroupSelected(A) && Squad->IsGroupSelected(B) && !Squad->IsGroupSelected(C),
				TEXT("box selection: A + B"));
			PressKey(PC, State, EKeys::V);
			NextStage(State);
			return true;
		case 12:
			if (State.StageTime < 1.f)
			{
				return true;
			}
			CheckStance(State, A, EOperativeStance::Prone, TEXT("box selection, V: the first selected goes prone"));
			CheckStance(State, B, EOperativeStance::Prone, TEXT("... the second selected too (was: the selection's leader alone)"));
			CheckStance(State, C, EOperativeStance::Crouching, TEXT("... the unselected one keeps his crouch"));
			PressKey(PC, State, EKeys::Three);
			NextStage(State);
			return true;
		case 13:
			if (State.StageTime < LongWait)
			{
				return true;
			}
			Check(State, Squad->GetLeader() == C && !Squad->HasMultiSelection(), TEXT("key 3 selects C alone"));
			CheckStance(State, A, EOperativeStance::Prone, TEXT("after the switch the box-selected pair stays prone"));
			CheckStance(State, B, EOperativeStance::Prone, TEXT("... both of them"));
			// --- 5. A/B: the old rule reproduces the bug (A prone by the player's order, selected, then key 2).
			SetOwnStanceRule(0);
			PressKey(PC, State, EKeys::One);
			NextStage(State);
			return true;
		case 14:
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, Squad->GetLeader() == A, FString::Printf(TEXT("SoloOwnStance 0: key 1 selects %s"), *Who(A)));
			PressKey(PC, State, EKeys::Two);
			NextStage(State);
			return true;
		case 15:
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			CheckStance(State, A, EOperativeStance::Crouching, TEXT("Codex.Squad.SoloOwnStance 0 (the old rule): key 2 gets the previous operative up into a crouch - the reported bug reproduces"));
			return Finish(World, State, true);
		default:
			return false;
		}
	}

	void Run(const TArray<FString>&, UWorld* World)
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
			UWorld* W = WeakWorld.Get();
			return W && !State->bDone && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.SoloStanceSmoke"),
		TEXT("Dev check (the game designer's PIE report 2026-11-08, L_MovementTest): in Solo a stance order (V / C / Z, a box selection's V) stays with the ")
		TEXT("operatives it was given to through later selection switches; Codex.Squad.SoloOwnStance 0 reproduces the bug. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
