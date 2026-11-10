// Dev-only headless check of the fast regroup on B (the game designer's bug 2026-11-10) on L_MovementTest:
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands FastRegroupSmoke
// "The game starts in SOLO. One operative is far away (tens of metres), B: the others switched into the formation at once and followed him
// SLOWLY in formation pacing. Wanted: members far from their slot first close the distance as fast as possible, then form up."
// Runs with test_map.json "squadStart": "level" (the level's real start mode, Solo):
//  pass 0 - the old rule (Codex.Squad.FastRegroup 0): A sprints ~40 m away and stops, B: the time until B and C are within 6 m of A, then the
//           time until everyone stands at his formation slot (the "before" numbers);
//  pass 1 - the fix: Solo again, A sprints ~40 m back, B: both armed / regrouping fast, sprinting (no enemy known, no fight), within 6 m of A
//           well faster than pass 0, the fast regroup over (they joined) close to the formation, then everyone at his slot;
//  pass 2 - the leader is not stopped: Solo, A ~40 m away again, B, then A walks ~10 m further on: A really moved, B and C keep regrouping fast
//           while far (chasing his live position), reach him and form up.
// The log prints "Smoke info: before / after" seconds. Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadRegroupRules.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

namespace FastRegroupSmoke
{
	constexpr float Timeout = 420.f;
	/** The far man's distance from the others (the bug report: "tens of metres"), cm. */
	constexpr float FarCm = 4000.f;
	constexpr float MinFarCm = 3200.f;

	struct FMate
	{
		TWeakObjectPtr<AOperativeCharacter> Operative;
		float StartDist = 0.f;
		float ReachAt = -1.f;
		bool bFastSeen = false;
		bool bSprintSeen = false;
		float JoinGap = -1.f;
		float MaxSpeed = 0.f;
	};

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Pass = 0;
		int32 Failures = 0;
		bool bDone = false;
		TWeakObjectPtr<AOperativeCharacter> A;
		FMate Mates[2];
		FVector Home = FVector::ZeroVector;
		FVector Far = FVector::ZeroVector;
		FVector LeaderAtB = FVector::ZeroVector;
		float ReachSeconds[3] = { -1.f, -1.f, -1.f };
		float FormSeconds[3] = { -1.f, -1.f, -1.f };
		float FormAt = -1.f;
		bool bLeaderWalkOrdered = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void SetFastRegroup(int32 Value)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Squad.FastRegroup")))
		{
			CVar->Set(Value, ECVF_SetByConsole);
		}
	}

	bool Finish(FState& State, bool bComplete)
	{
		SetFastRegroup(1);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: B with one man ~40 m away - within 6 m of him: before (old rule) %.1f s, after (fix) %.1f s, leader walking on %.1f s; all at their slots: before %.1f s, after %.1f s, leader walking on %.1f s"),
			State.ReachSeconds[0], State.ReachSeconds[1], State.ReachSeconds[2], State.FormSeconds[0], State.FormSeconds[1], State.FormSeconds[2]);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.bDone = true;
		FPlatformMisc::RequestExit(false, TEXT("FastRegroupSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	/** A navigable point reachable from From, as far as a ring of RadiusCm gives (the map may be re-laid by the user). */
	bool PickFarGoal(UWorld* World, const FVector& From, float RadiusCm, FVector& OutGoal)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!Nav)
		{
			return false;
		}
		float Best = 0.f;
		for (int32 Index = 0; Index < 16; ++Index)
		{
			const FVector Wanted = From + FRotator(0.f, Index * 22.5f, 0.f).RotateVector(FVector(RadiusCm, 0.f, 0.f));
			FNavLocation OnNav;
			if (!Nav->ProjectPointToNavigation(Wanted, OnNav, FVector(400.f, 400.f, 500.f)))
			{
				continue;
			}
			const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, From, OnNav.Location);
			const float Dist = static_cast<float>(FVector::Dist2D(From, OnNav.Location));
			// The straightest way there (a path at most 30 % longer): the timing compares like with like.
			if (Path && Path->IsValid() && !Path->IsPartial() && Path->GetPathLength() < Dist * 1.3f && Dist > Best)
			{
				Best = Dist;
				OutGoal = SmokeUtils::FreeSpot(World, OnNav.Location);
			}
		}
		return Best >= MinFarCm;
	}

	/** Everyone but the leader stands still at his formation slot near the leader. */
	bool AllFormed(USquadSubsystem& Squad, AOperativeCharacter& LeaderRef, FString& OutWhere)
	{
		bool bAll = true;
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			if (Member == &LeaderRef)
			{
				continue;
			}
			FVector Slot;
			const bool bSlot = Squad.GetFormationGivenSlot(Member, Slot);
			const float ToSlot = bSlot ? static_cast<float>(FVector::Dist2D(Slot, Member->GetActorLocation())) : -1.f;
			const float ToLeader = static_cast<float>(FVector::Dist2D(LeaderRef.GetActorLocation(), Member->GetActorLocation()));
			bAll &= bSlot && ToSlot < 150.f && ToLeader < 600.f && Member->GetVelocity().Size2D() < 30.f && !Squad.IsFastRegrouping(Member);
			OutWhere += FString::Printf(TEXT(" %s %.0f cm from his slot, %.0f cm from the leader;"), *Member->DisplayName.ToString(), ToSlot, ToLeader);
		}
		return bAll;
	}

	bool Step(UWorld* World, FState& State, float DeltaTime)
	{
		State.Time += DeltaTime;
		State.StageTime += DeltaTime;
		if (State.Time > Timeout)
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d (pass %d)"), State.Stage, State.Pass));
			return Finish(State, false);
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (!Squad || Squad->GetMembers().Num() < 3)
		{
			return State.Time < 10.f || Finish(State, false);
		}
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
			Member->HealthComponent->SetMaxHealth(100000.f, false);
			Member->HealthComponent->Heal(100000.f);
		}
		AOperativeCharacter* A = State.A.Get();

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
			State.Mates[0].Operative = Members[1];
			State.Mates[1].Operative = Members[2];
			Check(State, Squad->IsSoloMode(), TEXT("0: the level started in Solo"));
			// No enemy anywhere (no combat: the out-of-combat sprint is checked), the autonomy off, no fire.
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			Squad->SetAutonomousSquadCombat(false);
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			State.Home = A->GetActorLocation();
			if (!PickFarGoal(World, State.Home, FarCm, State.Far) && !PickFarGoal(World, State.Home, MinFarCm, State.Far))
			{
				Check(State, false, TEXT("0: a reachable point ~40 m away"));
				return Finish(State, false);
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: home (%.0f, %.0f), far point (%.0f, %.0f), %.1f m"), State.Home.X, State.Home.Y, State.Far.X,
				State.Far.Y, FVector::Dist2D(State.Home, State.Far) / 100.f);
			Next(State);
			return true;
		}
		case 1:
		{
			// Each pass: Solo, A selected and sprinting to the point the others are not at (pass 0 / 2: far, pass 1: home).
			if (!Squad->IsSoloMode())
			{
				Squad->ToggleSoloMode();
			}
			SetFastRegroup(State.Pass == 0 ? 0 : 1);
			Squad->SetLeader(A);
			A->OrderMoveTo(State.Pass == 1 ? State.Home : State.Far, true);
			Next(State);
			return true;
		}
		case 2:
		{
			const FVector Goal = State.Pass == 1 ? State.Home : State.Far;
			const bool bArrived = FVector::Dist2D(A->GetActorLocation(), Goal) < 200.f && !A->IsMoving();
			if ((!bArrived || State.StageTime < 2.f) && State.StageTime < 40.f)
			{
				return true;
			}
			Check(State, bArrived && Squad->IsSoloMode(), FString::Printf(TEXT("[pass %d] Solo: A reached the far point (%.0f cm off), the others held"), State.Pass,
				FVector::Dist2D(A->GetActorLocation(), Goal)));
			// B (the selected far man leads): the regroup.
			for (FMate& Mate : State.Mates)
			{
				Mate = FMate{ Mate.Operative };
				Mate.StartDist = static_cast<float>(FVector::Dist2D(Mate.Operative->GetActorLocation(), A->GetActorLocation()));
			}
			State.LeaderAtB = A->GetActorLocation();
			State.FormAt = -1.f;
			State.bLeaderWalkOrdered = false;
			Squad->ToggleSoloMode();
			Check(State, !Squad->IsSoloMode() && Squad->GetFormationLeader() == A, FString::Printf(TEXT("[pass %d] B: formation round A, B %.1f m / C %.1f m from him"),
				State.Pass, State.Mates[0].StartDist / 100.f, State.Mates[1].StartDist / 100.f));
			if (State.Pass >= 1)
			{
				for (FMate& Mate : State.Mates)
				{
					Check(State, Squad->IsFastRegrouping(Mate.Operative.Get()), FString::Printf(TEXT("[pass %d] %s (%.1f m away) armed for the fast regroup"), State.Pass,
						*Mate.Operative->DisplayName.ToString(), Mate.StartDist / 100.f));
				}
			}
			else
			{
				Check(State, !Squad->IsFastRegrouping(State.Mates[0].Operative.Get()) && !Squad->IsFastRegrouping(State.Mates[1].Operative.Get()),
					TEXT("[pass 0] old rule (Codex.Squad.FastRegroup 0): nobody regroups fast"));
			}
			Next(State);
			return true;
		}
		case 3:
		{
			// Pass 2: right after B the leader walks ~10 m further on (his own order; the leader is never stopped).
			if (State.Pass == 2 && !State.bLeaderWalkOrdered && State.StageTime > 0.5f)
			{
				State.bLeaderWalkOrdered = true;
				const FVector Away = (State.Far - State.Home).GetSafeNormal2D();
				A->OrderMoveTo(SmokeUtils::ClearPoint(World, A->GetActorLocation(), A->GetActorLocation() + Away * 1000.f), false);
			}
			bool bAllReached = true;
			for (FMate& Mate : State.Mates)
			{
				AOperativeCharacter* Operative = Mate.Operative.Get();
				const bool bFast = Squad->IsFastRegrouping(Operative);
				const float ToLeader = static_cast<float>(FVector::Dist2D(Operative->GetActorLocation(), A->GetActorLocation()));
				Mate.MaxSpeed = FMath::Max(Mate.MaxSpeed, static_cast<float>(Operative->GetVelocity().Size2D()));
				if (bFast)
				{
					Mate.bFastSeen = true;
					Mate.bSprintSeen |= Operative->IsSprinting();
				}
				else if (Mate.bFastSeen && Mate.JoinGap < 0.f)
				{
					// The frame he joined the formation: his gap to it (the nearer of his slot and the leader - a slot on his side of the leader
					// is reached first).
					FVector Slot;
					const float ToSlot = Squad->GetFormationGivenSlot(Operative, Slot) ? static_cast<float>(FVector::Dist2D(Slot, Operative->GetActorLocation())) : ToLeader;
					Mate.JoinGap = FMath::Min(ToSlot, ToLeader);
				}
				if (Mate.ReachAt < 0.f && ToLeader <= 600.f)
				{
					Mate.ReachAt = State.StageTime;
				}
				bAllReached &= Mate.ReachAt >= 0.f;
			}
			FString Where;
			const bool bFormed = bAllReached && AllFormed(*Squad, *A, Where) && !A->IsMoving();
			if (bFormed && State.FormAt < 0.f)
			{
				State.FormAt = State.StageTime;
			}
			if ((!bFormed || State.StageTime < 1.f) && State.StageTime < 90.f)
			{
				return true;
			}
			State.ReachSeconds[State.Pass] = State.Mates[0].ReachAt >= 0.f && State.Mates[1].ReachAt >= 0.f
				? FMath::Max(State.Mates[0].ReachAt, State.Mates[1].ReachAt) : -1.f;
			State.FormSeconds[State.Pass] = State.FormAt;
			for (const FMate& Mate : State.Mates)
			{
				const FString Name = Mate.Operative->DisplayName.ToString();
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: [pass %d] %s: %.1f m from A at B, within 6 m after %.1f s, top speed %.0f cm/s, fast regroup %d, sprinting %d, joined %.0f cm from the formation"),
					State.Pass, *Name, Mate.StartDist / 100.f, Mate.ReachAt, Mate.MaxSpeed, Mate.bFastSeen ? 1 : 0, Mate.bSprintSeen ? 1 : 0, Mate.JoinGap);
				if (State.Pass >= 1)
				{
					// Closing at a run / sprint: 40 m in well under the formation's walking catch-up (~275 cm/s plus its slow-down).
					const float Budget = FMath::Max(Mate.StartDist - 600.f, 0.f) / 400.f + 3.f;
					Check(State, Mate.bFastSeen && Mate.bSprintSeen && Mate.MaxSpeed > 600.f, FString::Printf(TEXT("[pass %d] %s regrouped fast, sprinting (no enemy, no fight): top %.0f cm/s"),
						State.Pass, *Name, Mate.MaxSpeed));
					Check(State, Mate.ReachAt >= 0.f && Mate.ReachAt <= Budget, FString::Printf(TEXT("[pass %d] %s within 6 m after %.1f s (budget %.1f s for %.1f m)"), State.Pass, *Name,
						Mate.ReachAt, Budget, Mate.StartDist / 100.f));
					const float JoinCm = SquadRegroupTuning::Get().JoinCm;
					Check(State, Mate.JoinGap >= 0.f && Mate.JoinGap <= JoinCm + 100.f, FString::Printf(TEXT("[pass %d] %s joined the formation close to it (%.0f cm from it, join_m %.0f cm)"),
						State.Pass, *Name, Mate.JoinGap, JoinCm));
				}
			}
			// The old rule's pass only measures ("before"); the fix must form up.
			Check(State, bFormed || State.Pass == 0, FString::Printf(TEXT("[pass %d] then everyone at his formation slot round A (%.1f s):%s"), State.Pass, State.FormAt,
				*Where));
			if (State.Pass == 1)
			{
				const float Before = State.ReachSeconds[0] < 0.f ? 90.f : State.ReachSeconds[0]; // the old rule not there within 90 s: counted as 90
				Check(State, State.ReachSeconds[1] >= 0.f && State.ReachSeconds[1] < 0.6f * Before,
					FString::Printf(TEXT("[fix] within 6 m after %.1f s vs %.1f s with the old rule"), State.ReachSeconds[1], State.ReachSeconds[0]));
			}
			if (State.Pass == 2)
			{
				const float LeaderMoved = static_cast<float>(FVector::Dist2D(A->GetActorLocation(), State.LeaderAtB));
				Check(State, LeaderMoved > 500.f, FString::Printf(TEXT("[pass 2] the leader was not stopped: he walked %.1f m on after B"), LeaderMoved / 100.f));
				return Finish(State, true);
			}
			++State.Pass;
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
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
		TEXT("CodexTactics.FastRegroupSmoke"),
		TEXT("Dev check (the game designer's bug 2026-11-10, L_MovementTest): Solo, one man ~40 m away, B - the old rule (walking formation catch-up) vs the fix ")
		TEXT("(the far ones sprint to the leader, join the formation within 6 m, then form up); the leader walking on after B is chased. Logs before / after seconds, PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
