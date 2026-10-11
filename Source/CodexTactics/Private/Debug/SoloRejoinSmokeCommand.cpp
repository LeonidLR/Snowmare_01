// Dev-only console command for a headless solo-rejoin check on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.SoloRejoinSmoke
// Bug 2026-10-11: squad prone (Alt + V) -> solo (B) -> the leader scouts away -> solo off: the followers slid towards the
// leader while still rising. Rule: nobody translates while a stance clip roots him; the follower gets the leader's stance
// first (the stand-up / kneel / lie-down clip plays in place) and only then walks to his formation slot.
// Variants: prone followers + standing leader, crouched followers + standing leader, prone followers + crawling leader.
// Headless runs do not refresh bone poses: the actor locations, the stance and the move-hold (clip) state are checked.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
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
		if (!Leader || Squad->GetMembers().Num() < 3 || State.Time > 160.f)
		{
			Check(State, false, FString::Printf(TEXT("squad / timeout (stage %d, variant %d)"), State.Stage, State.Variant));
			return Finish(State, false);
		}
		if (State.Variant >= UE_ARRAY_COUNT(Variants))
		{
			return Finish(State, true);
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
		TEXT("Dev check: squad prone -> solo -> solo off: followers stand up in place, then walk to the leader (no sliding); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
