// Dev-only console command for a headless solo-rejoin check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.SoloRejoinSmoke
// Bug 2026-10-11: squad prone (Alt + V) -> solo (B) -> the leader scouts away -> solo off: the followers slid towards the
// leader while still rising. Rule: nobody translates while a stance clip roots him; the follower gets the leader's stance
// first (the stand-up / kneel / lie-down clip plays in place) and only then walks to his formation slot.
// Variants: prone followers + standing leader, crouched followers + standing leader, prone followers + crawling leader.
// Headless runs do not refresh bone poses: the actor locations, the stance and the move-hold (clip) state are checked.
// Rev 5g (game designer play-test 2026-10-14), after the variants:
//  - orders survive a selection switch: group mode - A ordered 10 m away, B selected: A reaches his goal, then joins B's
//    formation; Solo - A ordered 9 m away, B selected: A reaches it and holds there, the third mate stays put;
//  - followers in cover: B ordered into cover through a selection switch (his order runs on); group mode with the leader
//    standing he stays at the wall; Solo on + the leader 6 m away: he stays; Solo off (Regroup): he leaves the cover and
//    rejoins; back into cover, then the leader walks 15 m away in group mode: B leaves the cover and reaches the formation.
// Bug 2026-10-20 (lake demo, after the hut lesson: Regroup, then the Commander RUNS - the two others followed WALKING; Solo on / off
// again cured it): Solo, C stacked in cover at the wall, B (selected, crouched by Solo) walks "inside", Regroup with B leading (the
// others copy his crouch), A selected and sent sprinting 15 m: B and C stand up and sprint too (speed >= 90 % of A's within 2.5 s).
// Run twice: Codex.Squad.SprintStandUp 0 (the old rule) reproduces the bug (they stay crouched), 1 (default) is checked; then the
// player's own squad crouch (Alt + C) is kept when the leader sprints.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Tactics/CoverTraceRules.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace SoloRejoinSmoke
{
	/** A follower must not move more than this (cm) while a stance change holds him. */
	constexpr float MaxSlideCm = 5.f;
	constexpr float SettleSeconds = 3.5f;
	constexpr float SampleSeconds = 11.f;

	struct FVariant
	{
		const TCHAR* Name;
		EOperativeStance FollowerStance;
		EOperativeStance LeaderStance;
		float ScoutCm;
	};

	const FVariant Variants[] = {
		{ TEXT("prone followers, standing leader"), EOperativeStance::Prone, EOperativeStance::Standing, 900.f },
		{ TEXT("crouched followers, standing leader"), EOperativeStance::Crouching, EOperativeStance::Standing, 900.f },
		{ TEXT("prone followers, crawling leader"), EOperativeStance::Prone, EOperativeStance::Prone, 450.f },
	};

	struct FFollowerProbe
	{
		TWeakObjectPtr<AOperativeCharacter> Operative;
		FVector Start = FVector::ZeroVector;
		float InitialHold = 0.f;
		float MaxSlideInHold = 0.f;
		float MoveStartTime = -1.f;
		float StartDistToLeader = 0.f;
	};

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Variant = 0;
		int32 Failures = 0;
		FVector LeaderStart = FVector::ZeroVector;
		TArray<FFollowerProbe> Probes;

		// Rev 5g order / cover phases.
		TWeakObjectPtr<AOperativeCharacter> A;
		TWeakObjectPtr<AOperativeCharacter> B;
		TWeakObjectPtr<AOperativeCharacter> C;
		FVector Target = FVector::ZeroVector;
		FVector Mark = FVector::ZeroVector;
		float MinToTarget = 1.0e9f;
		bool bFlag = false;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		FCoverSlot Slot;
		TWeakObjectPtr<AStaticMeshActor> Wall;

		// Regroup + sprint (bug 2026-10-20): pass 0 with the old rule, pass 1 with the fix; when B / C first kept up with A.
		int32 SprintPass = 0;
		float FastAt[2] = { -1.f, -1.f };
		float BestRatio[2] = { 0.f, 0.f };
		bool bStoodUp[2] = { false, false };
	};

	void SetSprintStandUp(int32 Value)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Squad.SprintStandUp")))
		{
			CVar->Set(Value, ECVF_SetByConsole);
		}
	}

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		if (State.Wall.IsValid())
		{
			State.Wall->Destroy();
		}
		FPlatformMisc::RequestExit(false, TEXT("SoloRejoinSmoke"));
		return false;
	}

	void NextStage(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	TArray<AOperativeCharacter*> GetFollowers(USquadSubsystem* Squad)
	{
		TArray<AOperativeCharacter*> Followers;
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			if (Member != Squad->GetLeader())
			{
				Followers.Add(Member);
			}
		}
		return Followers;
	}

	AStaticMeshActor* SpawnWall(UWorld* World, const FVector& Centre, const FRotator& Rotation, const FVector& Scale)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		AStaticMeshActor* Block = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(),
			FTransform(Rotation, Centre, Scale), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
		if (!Block)
		{
			return nullptr;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
		Block->FinishSpawning(FTransform(Rotation, Centre, Scale));
		return Block;
	}

	/** Every operative standing and still at the test start, A (roster 0) leading. */
	void ResetSquad(UWorld* World, USquadSubsystem* Squad, FState& State)
	{
		if (Squad->IsSoloMode())
		{
			Squad->ExitSoloMode(false, 0.f);
		}
		SmokeUtils::PlaceSquadAtTestStart(World);
		const TArray<AOperativeCharacter*> Members = Squad->GetMembers();
		for (AOperativeCharacter* Member : Members)
		{
			Member->LeaveCover(TEXT("smoke reset"));
			Member->StopOperative();
			Member->SetStance(EOperativeStance::Standing);
		}
		State.A = Members[0];
		State.B = Members[1];
		State.C = Members[2];
		Squad->SetLeader(Members[0]);
	}

	/** Rev 5g phases (stage 100+): orders survive a selection switch; followers in cover in group / Solo mode. */
	bool StepOrders(UWorld* World, USquadSubsystem* Squad, FState& State)
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Member->ColdLevel = 0.f;
		}
		AOperativeCharacter* A = State.A.Get();
		AOperativeCharacter* B = State.B.Get();
		AOperativeCharacter* C = State.C.Get();
		if (State.Stage > 100 && (!A || !B || !C))
		{
			Check(State, false, TEXT("[orders] squad members gone"));
			return Finish(State, false);
		}
		switch (State.Stage)
		{
		case 100:
			ResetSquad(World, Squad, State);
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				Member->HealthComponent->SetMaxHealth(100000.f);
			}
			NextStage(State);
			return true;
		case 101:
		{
			if (State.StageTime < SettleSeconds)
			{
				return true;
			}
			// Group mode: A (leader) gets a move order 10 m ahead, then the player selects B.
			State.Target = SmokeUtils::ClearPoint(World, A->GetActorLocation(), A->GetActorLocation() + A->GetActorForwardVector() * 1000.f);
			State.MinToTarget = 1.0e9f;
			A->OrderMoveTo(State.Target, false);
			Squad->SetLeader(B);
			Check(State, Squad->GetLeader() == B && A->HasOwnMoveOrder() && A->IsMoving(),
				TEXT("[orders/group] A ordered 10 m ahead, B selected: A keeps his order (not stopped, not re-anchored)"));
			NextStage(State);
			return true;
		}
		case 102:
		{
			State.MinToTarget = FMath::Min(State.MinToTarget, static_cast<float>(FVector::Dist2D(A->GetActorLocation(), State.Target)));
			if (State.MinToTarget > 150.f && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, State.MinToTarget <= 150.f, FString::Printf(TEXT("[orders/group] A reached his ordered goal after B was selected (%.0f cm off)"), State.MinToTarget));
			NextStage(State);
			return true;
		}
		case 103:
		{
			const float ToB = FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation());
			if (ToB > 600.f && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, ToB <= 600.f, FString::Printf(TEXT("[orders/group] then A joined the new leader's formation (%.0f cm from B)"), ToB));
			NextStage(State);
			return true;
		}
		case 104:
			// Solo: A leads, Solo on (B and C hold crouched).
			Squad->SetLeader(A);
			Squad->EnterSoloMode();
			NextStage(State);
			return true;
		case 105:
		{
			if (State.StageTime < SettleSeconds)
			{
				return true;
			}
			State.Target = SmokeUtils::ClearPoint(World, A->GetActorLocation(), A->GetActorLocation() - A->GetActorForwardVector() * 900.f);
			State.MinToTarget = 1.0e9f;
			State.Mark = C->GetActorLocation();
			A->OrderMoveTo(State.Target, false);
			Squad->SetLeader(B);
			Check(State, Squad->IsSoloMode() && Squad->GetLeader() == B && A->HasOwnMoveOrder() && A->IsMoving(),
				TEXT("[orders/solo] A ordered 9 m away, B selected in Solo: A keeps walking (was: stopped)"));
			NextStage(State);
			return true;
		}
		case 106:
		{
			State.MinToTarget = FMath::Min(State.MinToTarget, static_cast<float>(FVector::Dist2D(A->GetActorLocation(), State.Target)));
			if (State.MinToTarget > 150.f && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, State.MinToTarget <= 150.f, FString::Printf(TEXT("[orders/solo] A reached his ordered goal (%.0f cm off)"), State.MinToTarget));
			NextStage(State);
			return true;
		}
		case 107:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			const float AtGoal = FVector::Dist2D(A->GetActorLocation(), State.Target);
			const float CMoved = FVector::Dist2D(C->GetActorLocation(), State.Mark);
			Check(State, AtGoal < 200.f && CMoved < 50.f && Squad->IsSoloMode(),
				FString::Printf(TEXT("[orders/solo] A holds at his goal (%.0f cm), the third mate stayed put (moved %.0f cm)"), AtGoal, CMoved));
			NextStage(State);
			return true;
		}
		case 108:
			// Cover: a fresh start, an 8 m wall 4 m ahead of the squad.
			ResetSquad(World, Squad, State);
			NextStage(State);
			return true;
		case 109:
		{
			if (State.StageTime < SettleSeconds)
			{
				return true;
			}
			State.F = A->GetActorForwardVector().GetSafeNormal2D();
			State.R = FVector::CrossProduct(FVector::UpVector, State.F);
			State.P = A->GetActorLocation();
			const float GroundZ = State.P.Z - A->GetSimpleCollisionHalfHeight();
			const FVector WallCentre = State.P + State.F * 400.f;
			State.Wall = SpawnWall(World, FVector(WallCentre.X, WallCentre.Y, GroundZ + 150.f), State.F.Rotation(), FVector(0.4f, 8.f, 3.f));
			Check(State, State.Wall.IsValid(), TEXT("[cover] 8 m wall spawned 4 m ahead"));
			NextStage(State);
			return true;
		}
		case 110:
		{
			if (State.StageTime < 2.f)
			{
				return true; // the navmesh rebuilds around the wall
			}
			const bool bSlot = CoverTraceRules::FindCoverSlotAt(World, State.P + State.F * 380.f + State.R * 200.f + FVector(0.f, 0.f, 90.f), State.F, State.Slot);
			Check(State, bSlot, TEXT("[cover] a cover slot on the wall"));
			if (!bSlot)
			{
				return Finish(State, false);
			}
			// The player selects B, orders him into cover and selects A again: B's cover order runs on (rev 5g item 1).
			Squad->SetLeader(B);
			B->OrderTakeCover(State.Slot, false);
			Squad->SetLeader(A);
			NextStage(State);
			return true;
		}
		case 111:
		{
			if (!B->bInCover && State.StageTime < 12.f)
			{
				return true;
			}
			Check(State, B->bInCover, TEXT("[cover] B (ordered, then A selected) reached the wall and took cover"));
			NextStage(State);
			return true;
		}
		case 112:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, B->bInCover, TEXT("[cover] group mode, leader standing: B stays in cover"));
			// Solo on, the leader walks 6 m away: B holds his wall.
			Squad->EnterSoloMode();
			State.Target = SmokeUtils::ClearPoint(World, A->GetActorLocation(), A->GetActorLocation() - State.F * 600.f);
			A->OrderMoveTo(State.Target, false);
			NextStage(State);
			return true;
		}
		case 113:
		{
			if ((A->IsMoving() || State.StageTime < 2.f) && State.StageTime < 12.f)
			{
				return true;
			}
			const float Apart = FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation());
			Check(State, B->bInCover && Apart > 450.f, FString::Printf(TEXT("[cover] Solo, leader %.0f cm away: B stays in cover"), Apart));
			// Regroup (Solo off): B leaves the wall and rejoins.
			Squad->ExitSoloMode(false, 0.f);
			NextStage(State);
			return true;
		}
		case 114:
		{
			const float ToA = FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation());
			if ((B->bInCover || ToA > 600.f) && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, !B->bInCover && ToA <= 600.f, FString::Printf(TEXT("[cover] Regroup: B left the cover and rejoined (%.0f cm from A)"), ToA));
			// B back into cover (selected, ordered, A selected again), then A walks 15 m away in group mode.
			Squad->SetLeader(B);
			B->OrderTakeCover(State.Slot, false);
			Squad->SetLeader(A);
			NextStage(State);
			return true;
		}
		case 115:
		{
			if (!B->bInCover && State.StageTime < 12.f)
			{
				return true;
			}
			Check(State, B->bInCover, TEXT("[cover] B in cover again"));
			State.Mark = A->GetActorLocation();
			State.Target = SmokeUtils::ClearPoint(World, A->GetActorLocation(), A->GetActorLocation() - State.F * 1500.f);
			A->OrderMoveTo(State.Target, false);
			State.bFlag = false;
			NextStage(State);
			return true;
		}
		case 116:
		{
			State.bFlag |= !B->bInCover;
			const float ToA = FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation());
			const bool bArrived = !A->IsMoving() && State.StageTime > 2.f;
			if ((!bArrived || ToA > 600.f) && State.StageTime < 25.f)
			{
				return true;
			}
			const float Walked = FVector::Dist2D(A->GetActorLocation(), State.Mark);
			Check(State, State.bFlag && !B->bInCover && Walked > 1000.f && ToA <= 600.f,
				FString::Printf(TEXT("[cover] group mode: the leader walked %.0f cm away, B left the cover %d and reached the formation (%.0f cm from A)"),
					Walked, State.bFlag ? 1 : 0, ToA));
			State.SprintPass = 0;
			NextStage(State);
			return true;
		}
		case 117:
			// Regroup + sprint (bug 2026-10-20): a fresh start at the wall; pass 0 = the old rule, pass 1 = the fix.
			SetSprintStandUp(State.SprintPass == 0 ? 0 : 1);
			ResetSquad(World, Squad, State);
			NextStage(State);
			return true;
		case 118:
		{
			if (State.StageTime < SettleSeconds)
			{
				return true;
			}
			// Solo (B, C crouch as guards); C stacks at the wall (door), B is selected and walks 4 m "inside", still crouched.
			Squad->EnterSoloMode();
			Squad->SetLeader(C);
			C->OrderTakeCover(State.Slot, false);
			Squad->SetLeader(B);
			State.Target = SmokeUtils::ClearPoint(World, B->GetActorLocation(), B->GetActorLocation() - State.F * 400.f);
			B->OrderMoveTo(State.Target, false);
			NextStage(State);
			return true;
		}
		case 119:
		{
			if ((!C->bInCover || B->IsMoving() || State.StageTime < 2.f) && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, C->bInCover && B->GetStance() == EOperativeStance::Crouching && A->GetStance() == EOperativeStance::Crouching,
				FString::Printf(TEXT("[regroup sprint %d] Solo: C stacked in cover %d, B (selected) crouched %d, A guards crouched %d"), State.SprintPass,
					C->bInCover ? 1 : 0, B->GetStance() == EOperativeStance::Crouching ? 1 : 0, A->GetStance() == EOperativeStance::Crouching ? 1 : 0));
			// Regroup (B) with B leading: A and C copy his crouch; then the Commander (A) is selected and runs (double-click = sprint).
			Squad->ToggleSoloMode();
			Check(State, !Squad->IsSoloMode() && A->GetStance() == EOperativeStance::Crouching && C->GetStance() == EOperativeStance::Crouching && !C->bInCover,
				FString::Printf(TEXT("[regroup sprint %d] Regroup: A and C took the leader's crouch, C left the cover"), State.SprintPass));
			NextStage(State);
			return true;
		}
		case 120:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			Squad->SetLeader(A);
			State.Target = SmokeUtils::ClearPoint(World, A->GetActorLocation(), A->GetActorLocation() - State.F * 1500.f);
			A->OrderMoveTo(State.Target, true);
			Check(State, A->IsSprinting() && A->GetStance() == EOperativeStance::Standing,
				FString::Printf(TEXT("[regroup sprint %d] A selected and sent sprinting: up and sprinting"), State.SprintPass));
			for (int32 Index = 0; Index < 2; ++Index)
			{
				State.FastAt[Index] = -1.f;
				State.BestRatio[Index] = 0.f;
				State.bStoodUp[Index] = false;
			}
			NextStage(State);
			return true;
		}
		case 121:
		{
			// Every frame: B and C keep up with the sprinting leader (>= 90 % of his speed) and sprint themselves.
			const float LeaderSpeed = static_cast<float>(A->GetVelocity().Size2D());
			AOperativeCharacter* Mates[2] = { B, C };
			for (int32 Index = 0; Index < 2; ++Index)
			{
				State.bStoodUp[Index] |= Mates[Index]->GetStance() == EOperativeStance::Standing;
				if (LeaderSpeed > 300.f && A->IsSprinting())
				{
					const float Ratio = static_cast<float>(Mates[Index]->GetVelocity().Size2D()) / LeaderSpeed;
					State.BestRatio[Index] = FMath::Max(State.BestRatio[Index], Ratio);
					if (State.FastAt[Index] < 0.f && Ratio >= 0.9f && Mates[Index]->IsSprinting())
					{
						State.FastAt[Index] = State.StageTime;
					}
				}
			}
			if (State.StageTime < 3.f && A->IsSprinting())
			{
				return true;
			}
			for (int32 Index = 0; Index < 2; ++Index)
			{
				const FString What = FString::Printf(TEXT("[regroup sprint, %s] %s: stood up %d, sprinting at >= 90 %% of the leader's speed after %.2f s (best %.0f %%)"),
					State.SprintPass == 0 ? TEXT("old rule Codex.Squad.SprintStandUp 0") : TEXT("fix"), *Mates[Index]->DisplayName.ToString(),
					State.bStoodUp[Index] ? 1 : 0, State.FastAt[Index], State.BestRatio[Index] * 100.f);
				if (State.SprintPass == 0)
				{
					Check(State, !State.bStoodUp[Index] && State.FastAt[Index] < 0.f, What + TEXT(" - the bug reproduced (crouched, walking behind him)"));
				}
				else
				{
					Check(State, State.bStoodUp[Index] && State.FastAt[Index] >= 0.f && State.FastAt[Index] <= 2.5f, What);
				}
			}
			if (State.SprintPass == 0)
			{
				State.SprintPass = 1;
				State.Stage = 117;
				State.StageTime = 0.f;
				return true;
			}
			SetSprintStandUp(1);
			State.bFlag = false;
			NextStage(State);
			return true;
		}
		case 122:
		{
			// A stance the player gave (Alt + C) is kept: the squad crouched by the player, the leader sprints - the others stay down.
			if (State.StageTime < 0.5f)
			{
				for (AOperativeCharacter* Member : Squad->GetMembers())
				{
					Member->StopOperative();
				}
				return true;
			}
			if (State.StageTime < SettleSeconds)
			{
				if (A->GetStance() != EOperativeStance::Crouching)
				{
					Squad->SetSquadStance(EOperativeStance::Crouching);
				}
				return true;
			}
			if (!State.bFlag)
			{
				State.bFlag = true;
				State.Target = SmokeUtils::ClearPoint(World, A->GetActorLocation(), A->GetActorLocation() + State.F * 1200.f);
				A->OrderMoveTo(State.Target, true);
				return true;
			}
			if (State.StageTime < SettleSeconds + 2.5f)
			{
				return true;
			}
			Check(State, B->GetStance() == EOperativeStance::Crouching && C->GetStance() == EOperativeStance::Crouching,
				TEXT("[regroup sprint] the player's squad crouch (Alt + C) is kept when the leader sprints (user decision 2026-10-01)"));
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State, float DeltaTime)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		State.Time += DeltaTime;
		State.StageTime += DeltaTime;
		if (State.Time < 3.f)
		{
			return true;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Leader || Squad->GetMembers().Num() < 3 || State.Time > 420.f)
		{
			Check(State, false, FString::Printf(TEXT("squad / timeout (stage %d, variant %d)"), State.Stage, State.Variant));
			return Finish(State, false);
		}
		if (State.Variant >= UE_ARRAY_COUNT(Variants))
		{
			if (State.Stage < 100)
			{
				State.Stage = 100;
				State.StageTime = 0.f;
			}
			return StepOrders(World, Squad, State);
		}
		const FVariant& Variant = Variants[State.Variant];
		const TArray<AOperativeCharacter*> Followers = GetFollowers(Squad);
		switch (State.Stage)
		{
		case 0:
			// Alt + V / Alt + C: the whole squad takes the stance (clips play in place), then B: solo.
			Squad->SetSquadStance(Variant.FollowerStance);
			NextStage(State);
			return true;
		case 1:
			if (State.StageTime < SettleSeconds)
			{
				return true;
			}
			Squad->EnterSoloMode();
			NextStage(State);
			return true;
		case 2:
			if (State.StageTime < SettleSeconds)
			{
				return true;
			}
			// Solo makes the followers crouch; the player lays them down again (Alt + V in solo) / keeps them crouched.
			for (AOperativeCharacter* Follower : Followers)
			{
				Follower->SetStance(Variant.FollowerStance);
			}
			Leader->SetStance(Variant.LeaderStance);
			NextStage(State);
			return true;
		case 3:
		{
			if (State.StageTime < SettleSeconds)
			{
				return true;
			}
			State.LeaderStart = Leader->GetActorLocation();
			State.Probes.Reset();
			for (AOperativeCharacter* Follower : Followers)
			{
				FFollowerProbe& Probe = State.Probes.AddDefaulted_GetRef();
				Probe.Operative = Follower;
				Probe.Start = Follower->GetActorLocation();
			}
			Leader->OrderMoveTo(SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Leader->GetActorLocation() + Leader->GetActorForwardVector() * Variant.ScoutCm), false);
			NextStage(State);
			return true;
		}
		case 4:
		{
			const bool bScouted = FVector::Dist2D(Leader->GetActorLocation(), State.LeaderStart) > Variant.ScoutCm * 0.7f;
			if (!bScouted && State.StageTime < 20.f)
			{
				return true;
			}
			Check(State, bScouted, FString::Printf(TEXT("[%s] the leader scouted away (%.0f cm)"), Variant.Name,
				FVector::Dist2D(Leader->GetActorLocation(), State.LeaderStart)));
			Leader->StopOperative();
			bool bHeld = true;
			for (const FFollowerProbe& Probe : State.Probes)
			{
				bHeld &= Probe.Operative.IsValid() && FVector::Dist2D(Probe.Operative->GetActorLocation(), Probe.Start) < 30.f
					&& Probe.Operative->GetStance() == Variant.FollowerStance;
			}
			Check(State, bHeld, FString::Printf(TEXT("[%s] solo: the followers held their spot in their stance"), Variant.Name));
			NextStage(State);
			return true;
		}
		case 5:
		{
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			// B again: solo off. The followers take the leader's stance and rejoin.
			for (FFollowerProbe& Probe : State.Probes)
			{
				if (AOperativeCharacter* Follower = Probe.Operative.Get())
				{
					Probe.Start = Follower->GetActorLocation();
					Probe.StartDistToLeader = FVector::Dist2D(Probe.Start, Leader->GetActorLocation());
				}
			}
			Squad->ExitSoloMode(false, 0.f);
			for (FFollowerProbe& Probe : State.Probes)
			{
				if (const AOperativeCharacter* Follower = Probe.Operative.Get())
				{
					Probe.InitialHold = Follower->GetMoveHoldSeconds();
					Check(State, Follower->GetStance() == Variant.LeaderStance, FString::Printf(TEXT("[%s] %s took the leader's stance, move hold %.2f s"),
						Variant.Name, *Follower->DisplayName.ToString(), Probe.InitialHold));
				}
			}
			NextStage(State);
			return true;
		}
		case 6:
		{
			// Sample every frame: while a stance clip holds a follower he must not slide; afterwards he walks to the leader.
			bool bAllJoined = true;
			for (FFollowerProbe& Probe : State.Probes)
			{
				const AOperativeCharacter* Follower = Probe.Operative.Get();
				if (!Follower)
				{
					continue;
				}
				const float Moved = FVector::Dist2D(Follower->GetActorLocation(), Probe.Start);
				if (OperativeMovementRules::ShouldDeferMove(Follower->GetMoveHoldSeconds()))
				{
					Probe.MaxSlideInHold = FMath::Max(Probe.MaxSlideInHold, Moved);
				}
				if (Probe.MoveStartTime < 0.f && Moved > 15.f)
				{
					Probe.MoveStartTime = State.StageTime;
				}
				bAllJoined &= Probe.StartDistToLeader - FVector::Dist2D(Follower->GetActorLocation(), Leader->GetActorLocation()) > 150.f;
			}
			if (!bAllJoined && State.StageTime < SampleSeconds)
			{
				return true;
			}
			for (const FFollowerProbe& Probe : State.Probes)
			{
				const AOperativeCharacter* Follower = Probe.Operative.Get();
				if (!Follower)
				{
					continue;
				}
				const bool bNoSlide = Probe.MaxSlideInHold < MaxSlideCm;
				// He starts walking once the clip is (nearly) over - never earlier than the clip that held him at the start.
				const bool bWaited = Probe.InitialHold < 0.1f || Probe.MoveStartTime >= Probe.InitialHold - 0.35f;
				const bool bClosed = Probe.StartDistToLeader - FVector::Dist2D(Follower->GetActorLocation(), Leader->GetActorLocation()) > 150.f;
				Check(State, bNoSlide && bWaited && bClosed, FString::Printf(TEXT("[%s] %s: slide in the stance clip %.1f cm (< %.0f), walk starts at %.2f s (hold was %.2f s), rejoined %d"),
					Variant.Name, *Follower->DisplayName.ToString(), Probe.MaxSlideInHold, MaxSlideCm, Probe.MoveStartTime, Probe.InitialHold, bClosed ? 1 : 0));
			}
			for (AOperativeCharacter* Follower : Followers)
			{
				Follower->StopOperative();
			}
			++State.Variant;
			State.Stage = 0;
			State.StageTime = 0.f;
			return true;
		}
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
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float DeltaTime)
		{
			return Step(WeakWorld, *State, DeltaTime);
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.SoloRejoinSmoke"),
		TEXT("Dev check: squad prone -> solo -> solo off: followers stand up in place, then walk to the leader (no sliding); orders survive a selection switch (group / Solo); followers in cover follow a leader walking away in group mode; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
