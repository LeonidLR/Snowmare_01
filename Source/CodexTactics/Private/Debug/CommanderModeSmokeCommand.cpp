// Dev-only headless check of Commander Mode (Sprint 07) on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.CommanderModeSmoke -Log Smoke-CommanderMode.log
// 1. The mode is off by default. 2. «Начать бой», wave 1; one operative is wounded to 15 % next to a mate with two
// medkits; Commander Mode on. 3. 25 s of autonomous fighting: nobody leaves his leash (7 m, 10 m for aid), the
// wounded one gets first aid, targets are picked. 4. A tactical pause freezes the autonomy at once (no autonomy target
// left). PASS when all hold. The squad is unkillable and kept warm: the check is about decisions, not survival.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadAutonomySubsystem.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Data/SquadROE.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"

namespace CommanderModeSmoke
{
	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		bool bDefaultOff = false;
		TWeakObjectPtr<AOperativeCharacter> Medic;
		TWeakObjectPtr<AOperativeCharacter> Patient;
		float PatientHealthBefore = 0.f;
		float WorstLeashCm = 0.f;
		FString WorstLeashName;
		int32 FreezesBefore = 0;
		bool bFrozeClean = false;
	};

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += 0.1f; // fixed step like the other smokes
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		USquadAutonomySubsystem* Autonomy = World->GetSubsystem<USquadAutonomySubsystem>();
		const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
		for (AOperativeCharacter* Member : Members)
		{
			Member->ColdLevel = 0.f; // no freezing / panic while we watch
		}
		if (State.Stage == 0)
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			State.bDefaultOff = !Squad->IsAutonomousSquadCombat() && !Flow->IsAutonomousSquadCombat();
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			for (AOperativeCharacter* Member : Members)
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
			}
			// The closest pair: the medic and the wounded one (the patient walks next to the medic if far).
			float Best = TNumericLimits<float>::Max();
			for (AOperativeCharacter* A : Members)
			{
				for (AOperativeCharacter* B : Members)
				{
					const float Distance = A != B ? FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation()) : Best;
					if (Distance < Best)
					{
						Best = Distance;
						State.Medic = A;
						State.Patient = B;
					}
				}
			}
			if (!State.Medic.IsValid())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: FAIL (fewer than two operatives)"));
				FPlatformMisc::RequestExit(false, TEXT("CommanderModeSmoke"));
				return false;
			}
			if (Best > 600.f)
			{
				const FVector Side = (State.Patient->GetActorLocation() - State.Medic->GetActorLocation()).GetSafeNormal2D() * 400.f;
				State.Patient->OrderMoveTo(State.Medic->GetActorLocation() + Side, false);
			}
			State.Medic->MedkitsCount = 2;
			State.Stage = 1;
			State.Time = 0.f;
			return true;
		}
		if (State.Stage == 1)
		{
			if (State.Time < 3.f)
			{
				return true;
			}
			State.Patient->HealthComponent->ApplyDirectHealthLoss(85000.f, TEXT("smoke"));
			State.PatientHealthBefore = State.Patient->HealthComponent->GetCurrentHealth();
			Squad->SetAutonomousSquadCombat(true);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: medic %s (medkits %d), patient %s at %.0f%% HP, %.1f m apart"),
				*State.Medic->DisplayName.ToString(), State.Medic->MedkitsCount, *State.Patient->DisplayName.ToString(),
				State.Patient->HealthComponent->GetHealthFraction() * 100.f,
				FVector::Dist2D(State.Medic->GetActorLocation(), State.Patient->GetActorLocation()) / 100.f);
			State.Stage = 2;
			State.Time = 0.f;
			return true;
		}
		if (State.Stage == 2)
		{
			for (const AOperativeCharacter* Member : Members)
			{
				if (!Member->TacticalAnchor.bIsActive || Member->IsMoving())
				{
					continue; // walking back counts only once he stops
				}
				const float Off = FVector::Dist2D(Member->TacticalAnchor.Location, Member->GetActorLocation());
				if (Off > State.WorstLeashCm)
				{
					State.WorstLeashCm = Off;
					State.WorstLeashName = Member->DisplayName.ToString();
				}
			}
			if (State.Time < 25.f)
			{
				return true;
			}
			State.FreezesBefore = Autonomy->GetStats().Freezes;
			Flow->ToggleTacticalPause();
			State.Stage = 3;
			State.Time = 0.f;
			return true;
		}
		// Stage 3: one step into the pause.
		bool bTargetsLeft = false;
		for (const AOperativeCharacter* Member : Members)
		{
			bTargetsLeft |= Member->GetAutonomyTarget() != nullptr;
		}
		const bool bPaused = Flow->GetCombatMode() == ECodexCombatMode::TacticalPause;
		State.bFrozeClean = bPaused && !Flow->IsSquadAutonomyActive() && Autonomy->GetStats().Freezes > State.FreezesBefore && !bTargetsLeft;
		const FSquadAutonomyStats& Stats = Autonomy->GetStats();
		const float AidLeash = SquadAutonomyRules::LeashRadius(SquadROE::Get(), true);
		const float PatientHealth = State.Patient.IsValid() ? State.Patient->HealthComponent->GetCurrentHealth() : 0.f;
		const bool bLeash = State.WorstLeashCm <= AidLeash + 150.f;
		const bool bAid = Stats.AidGiven > 0 && PatientHealth > State.PatientHealthBefore;
		UE_LOG(LogCodexTactics, Display,
			TEXT("Smoke: cover %d, stance %d, prone for sniper %d, reload %d, sidearm %d, flank %d, leash returns %d, aid %d (moves %d, unsafe %d), targets %d"),
			Stats.CoverMoves, Stats.StanceChanges, Stats.ProneForSniper, Stats.Reloads, Stats.SidearmSwitches, Stats.FlankShifts, Stats.LeashReturns,
			Stats.AidGiven, Stats.AidMoves, Stats.AidRefusedUnsafe, Stats.TargetPicks);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: default off %d | worst leash %.1f m (%s, limit %.1f m) %d | aid %d (HP %.0f -> %.0f) | pause freeze %d | targets picked %d"),
			State.bDefaultOff ? 1 : 0, State.WorstLeashCm / 100.f, *State.WorstLeashName, (AidLeash + 150.f) / 100.f, bLeash ? 1 : 0, bAid ? 1 : 0,
			State.PatientHealthBefore, PatientHealth, State.bFrozeClean ? 1 : 0, Stats.TargetPicks);
		const bool bPass = State.bDefaultOff && bLeash && bAid && State.bFrozeClean && Stats.TargetPicks > 0;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("CommanderModeSmoke"));
		return false;
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), 0.1f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CommanderModeSmoke"),
		TEXT("Dev check of Commander Mode: default off, leash, field aid, pause freeze; PASS / FAIL."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
