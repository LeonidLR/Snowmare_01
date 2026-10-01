// Dev-only console command for a headless check of body turning on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.FacingSmoke
// User report 2026-10-01 (video UE_AnimBugs_01): slowing operatives and enemies trembled / spun on the spot, and the
// formation turned in jerks. The body turn (FacingRules, Godot lerp_angle) must not oscillate: the yaw is sampled every
// 0.05 s while the leader walks 6 m and the formation re-forms, and while four hounds crowd round the (unkillable)
// leader; a "flip" is a turn reversing its direction by more than 2 degrees a sample (a tremor).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace FacingSmoke
{
	constexpr float StepSeconds = 0.05f;
	/** Reversals tolerated per tracked body over a sampling window (a settling turn may overshoot once or twice). */
	constexpr int32 MaxFlips = 4;

	struct FTrack
	{
		TWeakObjectPtr<AActor> Actor;
		FString Name;
		float LastYaw = 0.f;
		float LastDelta = 0.f;
		int32 Flips = 0;
		float MaxStep = 0.f;
		bool bStarted = false;
		FString Trace;
	};

	struct FState
	{
		float Time = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TArray<FTrack> Tracks;
	};

	void Sample(FTrack& Track)
	{
		const AActor* Actor = Track.Actor.Get();
		if (!Actor)
		{
			return;
		}
		const float Yaw = Actor->GetActorRotation().Yaw;
		if (Track.bStarted)
		{
			const float Delta = FRotator::NormalizeAxis(Yaw - Track.LastYaw);
			if (FMath::Abs(Delta) > 2.f && FMath::Abs(Track.LastDelta) > 2.f && FMath::Sign(Delta) != FMath::Sign(Track.LastDelta))
			{
				++Track.Flips;
			}
			Track.MaxStep = FMath::Max(Track.MaxStep, FMath::Abs(Delta));
			if (FMath::Abs(Delta) > 2.f)
			{
				Track.LastDelta = Delta;
			}
		}
		Track.LastYaw = Yaw;
		Track.bStarted = true;
		Track.Trace += FString::Printf(TEXT(" %.0f/%.0f"), Yaw, Actor->GetVelocity().Size2D());
	}

	void Report(FState& State, const TCHAR* Phase)
	{
		for (const FTrack& Track : State.Tracks)
		{
			const bool bOk = Track.Flips <= MaxFlips;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s %s: %d turn reversals, largest step %.1f deg / 0.05 s"),
				bOk ? TEXT("ok  ") : TEXT("FAIL"), Phase, *Track.Name, Track.Flips, Track.MaxStep);
			State.Failures += bOk ? 0 : 1;
			UE_LOG(LogCodexTactics, Display, TEXT("Facing trace %s (yaw/speed per 0.05 s):%s"), *Track.Name, *Track.Trace);
		}
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("FacingSmoke"));
		return false;
	}

	bool Step(UWorld* World, FState& State)
	{
		State.Time += StepSeconds;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Leader)
		{
			return Finish(State, false);
		}
		for (FTrack& Track : State.Tracks)
		{
			Sample(Track);
		}
		switch (State.Stage)
		{
		case 0:
			// The leader walks 6 m; the formation follows and re-forms around him.
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				const UCharacterMovementComponent* Movement = Member->GetCharacterMovement();
				if (Movement && Movement->bOrientRotationToMovement)
				{
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke FAIL: %s still orients to movement"), *Member->DisplayName.ToString());
					++State.Failures;
				}
				State.Tracks.Add({ Member, Member->DisplayName.ToString() });
			}
			Leader->OrderMoveTo(Leader->GetActorLocation() + Leader->GetActorForwardVector() * 600.f, false);
			State.Stage = 1;
			State.Time = 0.f;
			return true;
		case 1:
			if (State.Time < 7.f) // the walk, the braking at the goal and the formation settling
			{
				return true;
			}
			Report(State, TEXT("walk + formation"));
			State.Tracks.Reset();
			{
				// Four hounds rush the leader (made unkillable) and crowd round him.
				Leader->HealthComponent->SetMaxHealth(100000.f);
				for (int32 Index = 0; Index < 4; ++Index)
				{
					const FVector Offset = Leader->GetActorForwardVector().RotateAngleAxis(-30.f + 20.f * Index, FVector::UpVector) * 1200.f;
					if (AEnemyCharacter* Hound = World->GetSubsystem<UWaveSubsystem>()->SpawnEnemy(EEnemyArchetype::FrostHound,
						Leader->GetActorLocation() + Offset + FVector(0.f, 0.f, 20.f)))
					{
						State.Tracks.Add({ Hound, FString::Printf(TEXT("hound %d"), Index + 1) });
						const UCharacterMovementComponent* Movement = Hound->GetCharacterMovement();
						if (Movement && Movement->bOrientRotationToMovement)
						{
							UE_LOG(LogCodexTactics, Display, TEXT("Smoke FAIL: hound still orients to movement"));
							++State.Failures;
						}
					}
				}
			}
			State.Stage = 2;
			State.Time = 0.f;
			return true;
		case 2:
			// Let them arrive, then sample 4 s of crowding / attacking.
			if (State.Time < 3.f)
			{
				for (FTrack& Track : State.Tracks)
				{
					Track.Flips = 0;
					Track.MaxStep = 0.f;
					Track.Trace.Reset();
				}
				return true;
			}
			if (State.Time < 7.f)
			{
				return true;
			}
			Report(State, TEXT("hounds crowding"));
			return Finish(State, true);
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
		TSharedRef<FState> State = MakeShared<FState>();
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		FTimerHandle StartHandle;
		World->GetTimerManager().SetTimer(StartHandle, FTimerDelegate::CreateLambda([WeakWorld, State, Handle]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				W->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda([WeakWorld, State, Handle]()
				{
					UWorld* W2 = WeakWorld.Get();
					if (W2 && !Step(W2, *State))
					{
						W2->GetTimerManager().ClearTimer(*Handle);
					}
				}), StepSeconds, true);
			}
		}), 3.f, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FacingSmoke"),
		TEXT("Dev check: no trembling / spinning turns while walking, re-forming and crowding; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
