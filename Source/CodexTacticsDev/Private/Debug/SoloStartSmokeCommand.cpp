// Dev-only headless check of the game designer's decision 2026-11-09 (branch feat/solo-start) on L_MovementTest:
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands SoloStartSmoke
// "The game starts in SOLO. Solo has NO distance limit. No dependence between operatives: each stays exactly where he was sent, holds his
// position / stance / weapon; the others do not follow, regroup or crouch. With autonomy OFF an operative still fires at visible enemies from
// his spot but never moves or takes cover on his own. B gathers everyone into the formation from wherever they are; B again = Solo, all stay."
// Runs with test_map.json "squadStart": "level" (the level's real start mode):
//  0. the level started in Solo (USquadSubsystem::ApplyLevelStartMode), no guard crouch waits; the Lake's JSON asks for the formation start;
//  1. A and B are sent (OrderMoveTo, the selected one) to two points 60+ m apart in different directions; C is never touched;
//  2. both arrive and stay there 3 s later, Solo still on (no 25 m leash), C did not move and still stands (no guard crouch);
//  3. a frozen, tough frost hound in C's sight, the squad Aggressive, autonomy off: C fires from his spot (shots > 0) without moving;
//  4. the fight in real time, C under fire (NoteUnderFire): he does not manoeuvre (no cover, no sidestep, no prone - UnderFireRules::bSoloHold);
//  5. B (the controller's handler): every follower walks to his formation slot round the selected leader (no distance cap);
//  6. B again: Solo, everyone stays where he is 3 s later;
//  7. A/B: Codex.Squad.SoloIndependent 0 (the old rules) - C under fire manoeuvres (cover / sidestep / prone): the hold check is meaningful.
// Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Data/LevelJsonRules.h"
#include "Data/WaveConfigTypes.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFlow/LevelEncounterSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

namespace SoloStartSmoke
{
	constexpr float Timeout = 360.f;
	constexpr float ApartCm = 6000.f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bDone = false;
		bool bBound = false;
		TWeakObjectPtr<AOperativeCharacter> A, B, C;
		FVector GoalA = FVector::ZeroVector, GoalB = FVector::ZeroVector, CSpot = FVector::ZeroVector;
		TMap<TWeakObjectPtr<AOperativeCharacter>, FVector> Held;
		TWeakObjectPtr<AEnemyCharacter> Hound;
	};

	/** Shots per operative (OnWeaponFiredNative; outlives the state - the process exits after the smoke). */
	TMap<TWeakObjectPtr<AOperativeCharacter>, int32> GShots;

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void SetIndependent(int32 Value)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Squad.SoloIndependent")))
		{
			CVar->Set(Value, ECVF_SetByConsole);
		}
	}

	bool Finish(FState& State, bool bComplete)
	{
		SetIndependent(1);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.bDone = true;
		FPlatformMisc::RequestExit(false, TEXT("SoloStartSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	void Remember(FState& State, USquadSubsystem& Squad)
	{
		State.Held.Reset();
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			State.Held.Add(Member, Member->GetActorLocation());
		}
	}

	float MovedSince(const FState& State, const AOperativeCharacter* Operative)
	{
		const FVector* Was = State.Held.Find(const_cast<AOperativeCharacter*>(Operative));
		return Was && Operative ? static_cast<float>(FVector::Dist2D(*Was, Operative->GetActorLocation())) : 0.f;
	}

	/** Two navigable points reachable from From, as far apart as a ring of RadiusCm gives (the map may be re-laid by the user). */
	bool PickApartGoals(UWorld* World, const FVector& From, float RadiusCm, FVector& OutA, FVector& OutB)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Nav)
		{
			return false;
		}
		TArray<FVector> Points;
		for (int32 Index = 0; Index < 16; ++Index)
		{
			const float Angle = Index * 22.5f;
			const FVector Wanted = From + FRotator(0.f, Angle, 0.f).RotateVector(FVector(RadiusCm, 0.f, 0.f));
			FNavLocation OnNav;
			if (!Nav->ProjectPointToNavigation(Wanted, OnNav, FVector(400.f, 400.f, 500.f)))
			{
				continue;
			}
			const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, From, OnNav.Location);
			if (Path && Path->IsValid() && !Path->IsPartial())
			{
				Points.Add(SmokeUtils::FreeSpot(World, OnNav.Location));
			}
		}
		float Best = 0.f;
		for (int32 I = 0; I < Points.Num(); ++I)
		{
			for (int32 J = I + 1; J < Points.Num(); ++J)
			{
				const float Dist = static_cast<float>(FVector::Dist2D(Points[I], Points[J]));
				if (Dist > Best)
				{
					Best = Dist;
					OutA = Points[I];
					OutB = Points[J];
				}
			}
		}
		return Best >= ApartCm;
	}

	bool Step(UWorld* World, FState& State, float DeltaTime)
	{
		State.Time += DeltaTime;
		State.StageTime += DeltaTime;
		if (State.Time > Timeout)
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d"), State.Stage));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(World->GetFirstPlayerController());
		if (!Squad || !Flow || !PC || Squad->GetMembers().Num() < 3)
		{
			return State.Time < 10.f || Finish(State, false);
		}
		AOperativeCharacter* A = State.A.Get();
		AOperativeCharacter* B = State.B.Get();
		AOperativeCharacter* C = State.C.Get();
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
			Member->HealthComponent->SetMaxHealth(100000.f, false);
			Member->HealthComponent->Heal(100000.f);
		}
		if (AEnemyCharacter* Hound = State.Hound.Get())
		{
			Hound->GetHealthComponent()->Heal(100000.f);
		}

		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f)
			{
				return true; // navmesh warm-up
			}
			TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			Members.Sort([](const AOperativeCharacter& L, const AOperativeCharacter& R) { return L.SquadIndex < R.SquadIndex; });
			State.A = A = Members[0];
			State.B = B = Members[1];
			State.C = C = Members[2];
			Check(State, Squad->IsStartModeResolved() && Squad->IsSoloMode() && !Squad->IsTrapSolo(), TEXT("0: the level started in Solo"));
			for (AOperativeCharacter* Member : Members)
			{
				Check(State, !Squad->IsSoloGuardCrouchWaiting(Member) && Member->GetStance() == EOperativeStance::Standing,
					FString::Printf(TEXT("0: %s stands, no guard crouch waits"), *Member->DisplayName.ToString()));
			}
			FLevelCombatConfig Lake;
			FString Error;
			Check(State, LevelJsonRules::LoadLevel(TEXT("L_Demo01_A_FrozenLake.json"), Lake, Error) && Lake.bStartInFormation,
				TEXT("0: the Lake tutorial level asks for the formation start (start_mode formation - its first lesson is B)"));
			Squad->SetAutonomousSquadCombat(false);
			Squad->SetSquadPosture(ESquadFirePosture::Passive); // no fire until the hound stage
			if (!PickApartGoals(World, A->GetActorLocation(), 3300.f, State.GoalA, State.GoalB)
				&& !PickApartGoals(World, A->GetActorLocation(), 4500.f, State.GoalA, State.GoalB))
			{
				Check(State, false, TEXT("0: two reachable points 60 m apart"));
				return Finish(State, false);
			}
			State.CSpot = C->GetActorLocation();
			Remember(State, *Squad);
			// The selected one is sent, then the next one is selected and sent (rev 5g: an order runs on through a selection switch).
			Squad->SetLeader(A);
			A->OrderMoveTo(State.GoalA, false);
			Squad->SetLeader(B);
			B->OrderMoveTo(State.GoalB, false);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: A -> (%.0f, %.0f), B -> (%.0f, %.0f), %.1f m apart"), State.GoalA.X, State.GoalA.Y,
				State.GoalB.X, State.GoalB.Y, FVector::Dist2D(State.GoalA, State.GoalB) / 100.f);
			Next(State);
			return true;
		}
		case 1:
		{
			const bool bArrived = FVector::Dist2D(A->GetActorLocation(), State.GoalA) < 150.f && FVector::Dist2D(B->GetActorLocation(), State.GoalB) < 150.f
				&& !A->IsMoving() && !B->IsMoving();
			if (!bArrived && State.StageTime < 60.f)
			{
				if (!Squad->IsSoloMode())
				{
					Check(State, false, TEXT("1: Solo ended while they walked apart (a leash regroup)"));
					return Finish(State, false);
				}
				return true;
			}
			Check(State, bArrived, FString::Printf(TEXT("1: A and B reached their goals (%.0f / %.0f cm)"), FVector::Dist2D(A->GetActorLocation(), State.GoalA),
				FVector::Dist2D(B->GetActorLocation(), State.GoalB)));
			Next(State);
			return true;
		}
		case 2:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			const float Apart = static_cast<float>(FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation()));
			Check(State, Squad->IsSoloMode() && Apart >= ApartCm, FString::Printf(TEXT("2: %.1f m apart, still Solo (no distance limit)"), Apart / 100.f));
			Check(State, FVector::Dist2D(A->GetActorLocation(), State.GoalA) < 150.f && FVector::Dist2D(B->GetActorLocation(), State.GoalB) < 150.f,
				TEXT("2: each stays where he was sent (no regroup)"));
			Check(State, MovedSince(State, C) < 40.f && C->GetStance() == EOperativeStance::Standing,
				FString::Printf(TEXT("2: C (never ordered) did not follow (%.0f cm) and still stands - no guard crouch"), MovedSince(State, C)));
			// A frozen, tough hound in C's sight (C is far from both others).
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			if (ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>())
			{
				Encounter->SetCombatStartOverride(ECombatStartMode::Button);
			}
			const FVector F = C->GetActorForwardVector().GetSafeNormal2D();
			const FVector Feet = C->GetActorLocation() - FVector(0.f, 0.f, C->GetSimpleCollisionHalfHeight());
			const FVector Spot = SmokeUtils::ClearPoint(World, Feet, Feet + F * 900.f);
			AEnemyCharacter* Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound, Spot + FVector(0.f, 0.f, 80.f),
				(C->GetActorLocation() - Spot).GetSafeNormal2D().Rotation());
			if (!Hound)
			{
				Check(State, false, TEXT("2: hound spawned"));
				return Finish(State, false);
			}
			State.Hound = Hound;
			Hound->GetHealthComponent()->SetMaxHealth(100000.f);
			Hound->bOverridePerception = true;
			Hound->PerceptionOverride.SightRangeCm = 0.f;
			Hound->PerceptionOverride.ProximityCm = 0.f;
			Hound->CustomTimeDilation = 0.f; // it never moves or attacks
			if (ULevelEncounterSubsystem* Encounter = World->GetSubsystem<ULevelEncounterSubsystem>())
			{
				Encounter->MarkLevelEnemy(Hound);
			}
			if (!State.bBound)
			{
				State.bBound = true;
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					Member->OnWeaponFiredNative.AddLambda([](AOperativeCharacter* Shooter, AActor*, bool) { ++GShots.FindOrAdd(Shooter); });
				}
			}
			Remember(State, *Squad);
			Squad->SetSquadPosture(ESquadFirePosture::Aggressive);
			Next(State);
			return true;
		}
		case 3:
		{
			const int32 CShots = GShots.FindRef(C);
			if (CShots < 2 && State.StageTime < 12.f)
			{
				return true;
			}
			Check(State, CShots > 0, FString::Printf(TEXT("3: the idle C fires at the hound in sight by his fire mode (%d shots, AGG, autonomy off)"), CShots));
			Check(State, MovedSince(State, C) < 40.f && !C->bInCover && !C->HasPendingCover() && C->GetStance() == EOperativeStance::Standing,
				FString::Printf(TEXT("3: ... from his spot (moved %.0f cm, cover %d, stance kept)"), MovedSince(State, C), C->bInCover ? 1 : 0));
			if (Flow->GetCombatMode() == ECodexCombatMode::TacticalPause)
			{
				Flow->ToggleTacticalPause();
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: phase %d, combat mode %d"), static_cast<int32>(Flow->GetPhase()), static_cast<int32>(Flow->GetCombatMode()));
			Remember(State, *Squad);
			Next(State);
			return true;
		}
		case 4:
		{
			// Under fire (a stand-in shooter, the flash at the hound): Solo, autonomy off - he holds where he was sent.
			if (AEnemyCharacter* Hound = State.Hound.Get())
			{
				C->NoteUnderFire(A, Hound->GetActorLocation() + FVector(0.f, 0.f, 60.f));
			}
			if (State.StageTime < 6.f)
			{
				return true;
			}
			const bool bRealTime = Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetCombatMode() == ECodexCombatMode::RealTime;
			Check(State, bRealTime, TEXT("4: the fight runs in real time (the manoeuvre's gate)"));
			Check(State, Squad->IsSoloHoldingInPlace() && MovedSince(State, C) < 40.f && !C->bInCover && !C->HasPendingCover()
				&& C->GetStance() == EOperativeStance::Standing,
				FString::Printf(TEXT("4: under fire C holds: no cover / sidestep / prone of his own (moved %.0f cm, stance %d)"), MovedSince(State, C),
					static_cast<int32>(C->GetStance())));
			// B: the formation round the selected one (B), from wherever they are.
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			PC->ToggleSoloMode();
			Check(State, !Squad->IsSoloMode(), TEXT("5: B - formation"));
			Next(State);
			return true;
		}
		case 5:
		{
			AOperativeCharacter* LeaderRef = Squad->GetLeader();
			bool bAll = true;
			FString Where;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member == LeaderRef)
				{
					continue;
				}
				FVector Slot;
				const bool bSlot = Squad->GetFormationGivenSlot(Member, Slot);
				const float ToSlot = bSlot ? static_cast<float>(FVector::Dist2D(Slot, Member->GetActorLocation())) : -1.f;
				const float ToLeader = static_cast<float>(FVector::Dist2D(LeaderRef->GetActorLocation(), Member->GetActorLocation()));
				bAll &= bSlot && ToSlot < 150.f && ToLeader < 600.f && Member->GetVelocity().Size2D() < 30.f;
				Where += FString::Printf(TEXT(" %s %.0f cm from his slot, %.0f cm from the leader;"), *Member->DisplayName.ToString(), ToSlot, ToLeader);
			}
			if (!bAll && State.StageTime < 90.f)
			{
				return true;
			}
			Check(State, bAll, FString::Printf(TEXT("5: every follower walked to his formation slot round %s (%.0f s):%s"), *LeaderRef->DisplayName.ToString(),
				State.StageTime, *Where));
			PC->ToggleSoloMode();
			Check(State, Squad->IsSoloMode(), TEXT("6: B again - Solo"));
			Remember(State, *Squad);
			Next(State);
			return true;
		}
		case 6:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Check(State, MovedSince(State, Member) < 40.f && !Squad->IsSoloGuardCrouchWaiting(Member),
					FString::Printf(TEXT("6: %s stays where he was (%.0f cm), no guard crouch"), *Member->DisplayName.ToString(), MovedSince(State, Member)));
			}
			// A/B: the old rules (all within the old leash now) - under fire C manoeuvres.
			SetIndependent(0);
			Squad->SetLeader(A);
			Remember(State, *Squad);
			Next(State);
			return true;
		}
		case 7:
		{
			if (AEnemyCharacter* Hound = State.Hound.Get())
			{
				C->NoteUnderFire(A, Hound->GetActorLocation() + FVector(0.f, 0.f, 60.f));
			}
			const bool bManoeuvred = MovedSince(State, C) > 50.f || C->bInCover || C->HasPendingCover() || C->GetStance() == EOperativeStance::Prone;
			if (!bManoeuvred && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, bManoeuvred, FString::Printf(TEXT("7: Codex.Squad.SoloIndependent 0 (the old rules): under fire C manoeuvres on his own (moved %.0f cm, cover %d, stance %d)"),
				MovedSince(State, C), C->bInCover || C->HasPendingCover() ? 1 : 0, static_cast<int32>(C->GetStance())));
			return Finish(State, true);
		}
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
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			UWorld* W = WeakWorld.Get();
			return W && !State->bDone && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.SoloStartSmoke"),
		TEXT("Dev check (the game designer's decision 2026-11-09, L_MovementTest): the level starts in Solo; two operatives sent 60+ m apart each stay, ")
		TEXT("nobody follows / crouches; the idle one fires at an enemy in sight from his spot and holds under fire (autonomy off); B gathers all into ")
		TEXT("the formation slots, B again = Solo, all stay; SoloIndependent 0 = the old manoeuvre. PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
