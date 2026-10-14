// Dev-only console command for a headless solo-rejoin check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.SoloRejoinSmoke
// Bug 2026-10-11: squad prone (Alt + V) -> solo (B) -> the leader scouts away -> solo off: the followers slid towards the
// leader while still rising. Rule: nobody translates while a stance clip roots him; the follower gets the leader's stance
// first (the stand-up / kneel / lie-down clip plays in place) and only then walks to his formation slot.
// Variants: prone followers + standing leader, crouched followers + standing leader, prone followers + crawling leader.
// Headless runs do not refresh bone poses: the actor locations, the stance and the move-hold (clip) state are checked.
// Rev 5g (game designer play-test 2026-10-14), after the variants:
//  - orders survive a selection switch: group mode - A ordered 10 m away, B selected: A reaches his goal, then joins B's
//    formation; Solo - A ordered 9 m away, B selected: A reaches it and holds there, the third mate stays put.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
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
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
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
			Member->StopOperative();
			Member->SetStance(EOperativeStance::Standing);
		}
		State.A = Members[0];
		State.B = Members[1];
		State.C = Members[2];
		Squad->SetLeader(Members[0]);
	}

	/** Rev 5g phases (stage 100+): orders survive a selection switch (group / Solo mode). */
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
		if (!Leader || Squad->GetMembers().Num() < 3 || State.Time > 240.f)
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
		TEXT("Dev check: squad prone -> solo -> solo off: followers stand up in place, then walk to the leader (no sliding); orders survive a selection switch (group / Solo); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
