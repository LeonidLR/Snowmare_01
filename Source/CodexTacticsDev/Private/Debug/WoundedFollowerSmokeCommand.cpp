// Dev-only headless check of the wounded member in the formation (the game designer's bug + decision "a" 2026-11-11) on L_MovementTest:
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands WoundedFollowerSmoke
// "A WOUNDED operative who is not the leader but walks in the formation does not keep the formation, looks only along his movement, falls far
// behind while his injured clip looks as if he tries to keep the healthy pace." Decision: while a wounded member is in the formation, the whole
// squad slows to the most wounded member's pace (SquadWoundPaceRules, Content/Data/AI/squad_wound_pace.json, Codex.Squad.WoundPace).
// A formation of 3 on a clear 25 m stretch; the Commander leads a straight walk, every pass from the same start:
//   off_light    - Codex.Squad.WoundPace 0, a follower at 40 % HP: the old behaviour (no wound pace, the leader at the fit patrol pace);
//   on_light     - the rule on, 40 %: the squad pace = 0.8 x his allowed walk (the leader's speed), he never walks faster than his allowed
//                  walk, keeps his slot (p95 <= 150 cm, contract 6), his body faces his formation look (sector) while he walks;
//   on_heavy     - 25 % (the heavy tier x0.45): the same, slower;
//   leader_light - the LEADER at 40 %, the followers fit: the leader keeps his own (wounded) patrol pace, the followers never overrun him;
//   on_sprint    - the rule on, 40 %, the leader ordered to sprint: the sprint ends, the squad walks at the wound pace;
//   on_critical  - 15 % (the critical wound, crawling): he never sets the pace (the leader at the fit pace), he crawls after his slot.
// Arg "shots" (rendered run, -RenderOffscreen): the player's GAME camera frames Saved/Screenshots/WoundedFollower/game_<pass>.png mid-walk.
// Nothing is saved. Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Animation/AnimSequenceBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Characters/SquadWoundPaceRules.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Formation/FormationTuning.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace WoundedFollowerSmoke
{
	constexpr float Timeout = 560.f;
	constexpr float NavWarmupSeconds = 3.f;
	/** The walk's length, cm. */
	constexpr float WalkCm = 2500.f;
	/** Sampling starts this long after the leader's order (the start-up), s. */
	constexpr float SampleAfterSeconds = 3.f;
	/** Contract 6 (pace): a man keeps within this of his slot at patrol pace (p95), cm. */
	constexpr float SlotToleranceCm = 150.f;
	/** His body faces his formation look within this, deg. */
	constexpr float LookToleranceDeg = 30.f;
	/** At least this share of the walking samples face the look (the sector) ... */
	constexpr float LookShareMin = 0.85f;

	enum class EPass : uint8 { OffLight, OnLight, OnHeavy, LeaderLight, OnSprint, OnCritical, Count };

	const TCHAR* PassName(EPass Pass)
	{
		switch (Pass)
		{
		case EPass::OffLight: return TEXT("off_light");
		case EPass::OnLight: return TEXT("on_light");
		case EPass::OnHeavy: return TEXT("on_heavy");
		case EPass::LeaderLight: return TEXT("leader_light");
		case EPass::OnSprint: return TEXT("on_sprint");
		default: return TEXT("on_critical");
		}
	}

	/** The wounded follower's HP (%) per pass (100 = fit), the leader's HP (%) per pass. */
	float FollowerHealth(EPass Pass)
	{
		switch (Pass)
		{
		case EPass::OnHeavy: return 25.f;
		case EPass::LeaderLight: return 100.f;
		case EPass::OnCritical: return 15.f;
		default: return 40.f;
		}
	}

	float LeaderHealth(EPass Pass)
	{
		return Pass == EPass::LeaderLight ? 40.f : 100.f;
	}

	/** One follower's samples over a walk. */
	struct FFollowerSamples
	{
		TArray<float> SlotDist;
		/** Ahead of his slot along the walk (> 0 = ahead), cm. */
		float MaxAheadOfSlot = -1.e6f;
		float MaxSpeed = 0.f;
		float Allowed = 0.f;
		int32 OverAllowed = 0;
		int32 Samples = 0;
		int32 LookSet = 0;
		int32 LookFaced = 0;
		int32 RearTurned = 0;
		double MoveOffBody = 0.0;
		double InjuredWeight = 0.0;
		TMap<FString, int32> Clips;
	};

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		EPass Pass = EPass::OffLight;
		int32 Failures = 0;
		bool bDone = false;
		bool bShots = false;
		bool bShotTaken = false;
		FString Dir;
		TWeakObjectPtr<AOperativeCharacter> A;
		TWeakObjectPtr<AOperativeCharacter> W;
		TWeakObjectPtr<AOperativeCharacter> Other;
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		FRotator StartRotation = FRotator::ZeroRotator;
		TArray<float> LeaderSpeed;
		FFollowerSamples WS;
		FFollowerSamples OS;
		int32 PaceFrames = 0;
		int32 Frames = 0;
		float CapSum = 0.f;
		FString PaceBy;
		bool bSprintSeenAtOrder = false;
		float SprintEndedAt = -1.f;
		TArray<FString> Report;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void SetWoundPace(int32 Value)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Squad.WoundPace")))
		{
			CVar->Set(Value, ECVF_SetByConsole);
		}
	}

	bool Finish(FState& State, bool bComplete)
	{
		SetWoundPace(-1);
		for (const FString& Line : State.Report)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s"), *Line);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		State.bDone = true;
		FPlatformMisc::RequestExit(false, TEXT("WoundedFollowerSmoke"));
		return false;
	}

	float Percentile(TArray<float> Values, float P)
	{
		if (Values.IsEmpty())
		{
			return -1.f;
		}
		Values.Sort();
		return Values[FMath::Clamp(FMath::FloorToInt32(P * (Values.Num() - 1)), 0, Values.Num() - 1)];
	}

	void SetHealthPct(AOperativeCharacter* Op, float Pct)
	{
		if (Op && Op->HealthComponent && !FMath::IsNearlyEqual(Op->HealthComponent->GetCurrentHealth(), Pct, 0.01f))
		{
			Op->HealthComponent->RestoreHealth(Pct, 100.f); // re-applied every frame: the medic's heal never changes the pass
		}
	}

	void ResetSquad(UWorld* World, FState& State)
	{
		AOperativeCharacter* Leader = State.A.Get();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, State.Forward);
		auto Place = [World, &State](AOperativeCharacter* Who, const FVector& Spot)
		{
			if (Who)
			{
				Who->StopOperative();
				if (!Who->IsCriticallyWounded())
				{
					Who->SetStance(EOperativeStance::Standing);
				}
				Who->TeleportTo(SmokeUtils::FreeSpot(World, Spot, Who), State.StartRotation, false, true);
			}
		};
		Place(Leader, State.Start);
		Place(State.W.Get(), State.Start - State.Forward * 250.f - Right * 150.f);
		Place(State.Other.Get(), State.Start - State.Forward * 250.f + Right * 150.f);
	}

	void Sample(USquadSubsystem& Squad, FState& State, AOperativeCharacter& Op, FFollowerSamples& S)
	{
		FVector Slot;
		if (Squad.GetFormationGivenSlot(&Op, Slot))
		{
			S.SlotDist.Add(static_cast<float>(FVector::Dist2D(Slot, Op.GetActorLocation())));
			S.MaxAheadOfSlot = FMath::Max(S.MaxAheadOfSlot, static_cast<float>(FVector::DotProduct(Op.GetActorLocation() - Slot, State.Forward)));
		}
		const FVector Velocity = Op.GetVelocity();
		const float Speed = static_cast<float>(Velocity.Size2D());
		S.Allowed = Op.GetAllowedWalkSpeed();
		S.MaxSpeed = FMath::Max(S.MaxSpeed, Speed);
		S.OverAllowed += Speed > S.Allowed + 8.f ? 1 : 0;
		++S.Samples;
		const TOptional<float> Look = Op.GetFormationLookYaw();
		const float Facing = static_cast<float>(Op.GetActorRotation().Yaw);
		if (Look.IsSet() && Op.IsFacingFormationSector())
		{
			++S.LookSet;
			S.LookFaced += FMath::Abs(FMath::FindDeltaAngleDegrees(Facing, Look.GetValue())) <= LookToleranceDeg ? 1 : 0;
		}
		S.RearTurned += Squad.IsFormationRearTurned(&Op) ? 1 : 0;
		if (Speed > 30.f)
		{
			S.MoveOffBody += FMath::Abs(FMath::FindDeltaAngleDegrees(Facing, static_cast<float>(Velocity.Rotation().Yaw)));
		}
		if (const UOperativeAnimInstance* Anim = Op.GetMesh() ? Cast<UOperativeAnimInstance>(Op.GetMesh()->GetAnimInstance()) : nullptr)
		{
			S.InjuredWeight += Anim->GetInjuredWeight();
			const UAnimSequenceBase* Clip = Anim->GetInjuredDominantClip();
			S.Clips.FindOrAdd(Clip && Anim->GetInjuredWeight() > 0.5f ? Clip->GetName() : FString(TEXT("-")))++;
		}
	}

	FString TopClips(const TMap<FString, int32>& Clips)
	{
		TArray<TPair<FString, int32>> Sorted = Clips.Array();
		Sorted.Sort([](const TPair<FString, int32>& L, const TPair<FString, int32>& R) { return L.Value > R.Value; });
		FString Out;
		for (int32 Index = 0; Index < FMath::Min(3, Sorted.Num()); ++Index)
		{
			Out += FString::Printf(TEXT("%s%s x%d"), Index ? TEXT(", ") : TEXT(""), *Sorted[Index].Key, Sorted[Index].Value);
		}
		return Out;
	}

	void Shot(UWorld* World, FState& State)
	{
		if (!State.bShots || State.bShotTaken)
		{
			return;
		}
		State.bShotTaken = true;
		const FString File = FString::Printf(TEXT("%s/game_%s.png"), *State.Dir, PassName(State.Pass));
		FScreenshotRequest::RequestScreenshot(File, true, false);
		const APlayerController* PC = World->GetFirstPlayerController();
		const APlayerCameraManager* Camera = PC ? PC->PlayerCameraManager.Get() : nullptr;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: frame %s (game camera %.0f cm from the leader, pitch %.0f)"), *File,
			Camera && State.A.IsValid() ? FVector::Dist(Camera->GetCameraLocation(), State.A->GetActorLocation()) : -1.f, Camera ? Camera->GetCameraRotation().Pitch : 0.f);
	}

	/** The pass's verdicts from its samples. */
	void Judge(USquadSubsystem& Squad, FState& State)
	{
		const EPass Pass = State.Pass;
		const FString Name = PassName(Pass);
		AOperativeCharacter* A = State.A.Get();
		AOperativeCharacter* W = State.W.Get();
		const float FitPace = A->MovementConfig.WalkSpeed * FormationTuning::Get().PacePatrol;
		const float LeaderMedian = Percentile(State.LeaderSpeed, 0.5f);
		const float Cap = State.PaceFrames > 0 ? State.CapSum / State.PaceFrames : -1.f;
		const float PaceShare = State.Frames > 0 ? static_cast<float>(State.PaceFrames) / State.Frames : 0.f;
		const auto Describe = [](const TCHAR* Who, const FFollowerSamples& S)
		{
			return FString::Printf(TEXT("%s: slot p50 %.0f / p95 %.0f / max %.0f cm, ahead of his slot max %.0f cm, speed max %.0f (allowed %.0f, %d of %d samples above), look faced %d of %d (set %d), rear-turned %d, move off his body %.0f deg avg, injured weight %.2f, clips %s"),
				Who, Percentile(S.SlotDist, 0.5f), Percentile(S.SlotDist, 0.95f), Percentile(S.SlotDist, 1.f), S.MaxAheadOfSlot, S.MaxSpeed, S.Allowed, S.OverAllowed,
				S.Samples, S.LookFaced, S.Samples, S.LookSet, S.RearTurned, S.Samples ? S.MoveOffBody / S.Samples : 0.0, S.Samples ? S.InjuredWeight / S.Samples : 0.0, *TopClips(S.Clips));
		};
		State.Report.Add(FString::Printf(TEXT("[%s] leader %s %.0f %% HP: speed p50 %.0f cm/s (fit patrol %.0f), wound pace in %.0f %% of the frames, cap %.0f cm/s by %s"),
			*Name, *A->DisplayName.ToString(), A->HealthComponent->GetCurrentHealth(), LeaderMedian, FitPace, 100.f * PaceShare, Cap, *State.PaceBy));
		State.Report.Add(FString::Printf(TEXT("[%s] %s"), *Name, *Describe(*FString::Printf(TEXT("%s %.0f %% HP"), *W->DisplayName.ToString(), W->HealthComponent->GetCurrentHealth()), State.WS)));
		State.Report.Add(FString::Printf(TEXT("[%s] %s"), *Name, *Describe(*State.Other->DisplayName.ToString(), State.OS)));

		const FFollowerSamples& S = State.WS;
		const float LookShare = S.Samples > 0 ? static_cast<float>(S.LookFaced) / S.Samples : 0.f;
		switch (Pass)
		{
		case EPass::OffLight:
			Check(State, PaceShare == 0.f, FString::Printf(TEXT("[%s] Codex.Squad.WoundPace 0: no wound pace (%.0f %% of the frames)"), *Name, 100.f * PaceShare));
			Check(State, FMath::Abs(LeaderMedian - FitPace) <= 0.08f * FitPace, FString::Printf(TEXT("[%s] the old behaviour: the leader at the fit patrol pace %.0f cm/s (%.0f)"),
				*Name, LeaderMedian, FitPace));
			break;
		case EPass::OnLight:
		case EPass::OnHeavy:
		case EPass::OnSprint:
		{
			const float Expected = SquadWoundPaceTuning::Get().PaceShare * S.Allowed;
			Check(State, PaceShare > 0.95f && FMath::Abs(Cap - Expected) <= 2.f, FString::Printf(TEXT("[%s] the squad pace = %.2f x his allowed walk %.0f = %.0f cm/s (cap %.0f, %.0f %% of the frames, by %s)"),
				*Name, SquadWoundPaceTuning::Get().PaceShare, S.Allowed, Expected, Cap, 100.f * PaceShare, *State.PaceBy));
			Check(State, State.PaceBy == W->DisplayName.ToString(), FString::Printf(TEXT("[%s] the wounded follower sets it (%s)"), *Name, *State.PaceBy));
			Check(State, LeaderMedian > 0.f && FMath::Abs(LeaderMedian - Expected) <= 0.1f * Expected, FString::Printf(TEXT("[%s] the leader walks at it: p50 %.0f cm/s (%.0f; fit patrol %.0f)"),
				*Name, LeaderMedian, Expected, FitPace));
			Check(State, S.OverAllowed <= FMath::Max(2, S.Samples / 50), FString::Printf(TEXT("[%s] he never walks faster than his allowed walk %.0f cm/s (max %.0f, %d of %d samples above + 8)"),
				*Name, S.Allowed, S.MaxSpeed, S.OverAllowed, S.Samples));
			if (Pass != EPass::OnSprint)
			{
				Check(State, Percentile(S.SlotDist, 0.95f) >= 0.f && Percentile(S.SlotDist, 0.95f) <= SlotToleranceCm, FString::Printf(TEXT("[%s] he keeps his slot: p95 %.0f cm (<= %.0f, contract 6)"),
					*Name, Percentile(S.SlotDist, 0.95f), SlotToleranceCm));
				Check(State, LookShare >= LookShareMin, FString::Printf(TEXT("[%s] his body faces his formation look (sector) in %.0f %% of the walk (>= %.0f %%, within %.0f deg), rear-turned %d"),
					*Name, 100.f * LookShare, 100.f * LookShareMin, LookToleranceDeg, S.RearTurned));
				Check(State, S.Samples > 0 && S.InjuredWeight / S.Samples > 0.5f, FString::Printf(TEXT("[%s] his legs walk the injured clips (weight %.2f; %s)"), *Name,
					S.Samples ? S.InjuredWeight / S.Samples : 0.0, *TopClips(S.Clips)));
			}
			if (Pass == EPass::OnSprint)
			{
				Check(State, State.bSprintSeenAtOrder && State.SprintEndedAt >= 0.f && State.SprintEndedAt <= 0.5f,
					FString::Printf(TEXT("[%s] the leader's sprint order ends at once (sprinting at the order %d, walking after %.2f s)"), *Name, State.bSprintSeenAtOrder ? 1 : 0, State.SprintEndedAt));
			}
			break;
		}
		case EPass::LeaderLight:
		{
			const float LeaderWalk = A->GetAllowedWalkSpeed();
			const float Own = LeaderWalk * FormationTuning::Get().PacePatrol;
			Check(State, FMath::Abs(LeaderMedian - Own) <= 0.1f * Own, FString::Printf(TEXT("[%s] the wounded leader keeps his own patrol pace: p50 %.0f cm/s (%.0f = 0.8 x %.0f)"),
				*Name, LeaderMedian, Own, LeaderWalk));
			for (const FFollowerSamples* Each : { &State.WS, &State.OS })
			{
				Check(State, Percentile(Each->SlotDist, 0.95f) <= SlotToleranceCm && Each->MaxAheadOfSlot <= 100.f,
					FString::Printf(TEXT("[%s] a fit follower does not overrun him: slot p95 %.0f cm, at most %.0f cm ahead of his slot"), *Name, Percentile(Each->SlotDist, 0.95f), Each->MaxAheadOfSlot));
			}
			break;
		}
		case EPass::OnCritical:
			Check(State, PaceShare == 0.f, FString::Printf(TEXT("[%s] the critically wounded (crawling) never sets the pace (%.0f %% of the frames)"), *Name, 100.f * PaceShare));
			Check(State, FMath::Abs(LeaderMedian - FitPace) <= 0.08f * FitPace, FString::Printf(TEXT("[%s] the leader at the fit patrol pace %.0f cm/s (%.0f)"), *Name, LeaderMedian, FitPace));
			Check(State, W->GetStance() == EOperativeStance::Prone && S.OverAllowed <= FMath::Max(2, S.Samples / 50),
				FString::Printf(TEXT("[%s] he crawls after his slot at his own crawl speed (prone %d, max %.0f, allowed %.0f)"), *Name, W->GetStance() == EOperativeStance::Prone ? 1 : 0,
					S.MaxSpeed, S.Allowed));
			break;
		default:
			break;
		}
	}

	bool Step(UWorld* World, FState& State, float DeltaTime)
	{
		State.Time += DeltaTime;
		State.StageTime += DeltaTime;
		if (State.Time > Timeout)
		{
			Check(State, false, FString::Printf(TEXT("timeout in stage %d (pass %s)"), State.Stage, PassName(State.Pass)));
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
			Member->bTacticalCeaseFire = true;
		}
		AOperativeCharacter* A = State.A.Get();
		if (A)
		{
			SetHealthPct(A, LeaderHealth(State.Pass));
			SetHealthPct(State.W.Get(), FollowerHealth(State.Pass));
			SetHealthPct(State.Other.Get(), 100.f);
		}

		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < NavWarmupSeconds)
			{
				return true;
			}
			TArray<AOperativeCharacter*> Members = Squad->GetMembers();
			Members.Sort([](const AOperativeCharacter& L, const AOperativeCharacter& R) { return L.SquadIndex < R.SquadIndex; });
			State.A = A = Members[0];
			State.Other = Members[1];
			State.W = Members[2];
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				It->Destroy();
			}
			Squad->SetAutonomousSquadCombat(false);
			Squad->SetSquadPosture(ESquadFirePosture::Passive);
			if (Squad->IsSoloMode())
			{
				Squad->ExitSoloMode(false, 0.f);
			}
			Squad->SetLeader(A);
			for (AOperativeCharacter* Member : Members)
			{
				if (Member->GetMesh())
				{
					Member->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones; // headless: the anim's clips are read
				}
			}
			Check(State, Squad->IsFormationActive() && Squad->GetFormationLeader() == A, FString::Printf(TEXT("formation of 3, %s leads, %s is the wounded follower"),
				*A->DisplayName.ToString(), *State.W->DisplayName.ToString()));
			TArray<const AActor*> Ignore;
			for (AOperativeCharacter* Member : Members)
			{
				Ignore.Add(Member);
			}
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, A->GetActorLocation(), A->GetActorForwardVector().GetSafeNormal2D(),
				FBox2D(FVector2D(-600.f, -800.f), FVector2D(WalkCm + 400.f, 800.f)), Ignore);
			State.Start = Arena.bFound ? Arena.Origin + FVector(0.f, 0.f, A->GetSimpleCollisionHalfHeight() + 5.f) : A->GetActorLocation();
			State.Forward = Arena.bFound ? Arena.Forward.GetSafeNormal2D() : A->GetActorForwardVector().GetSafeNormal2D();
			State.StartRotation = FRotator(0.f, State.Forward.Rotation().Yaw, 0.f);
			State.End = SmokeUtils::ClearPoint(World, State.Start, State.Start + State.Forward * WalkCm);
			Check(State, Arena.bFound, FString::Printf(TEXT("a clear %.0f m stretch (at %s, walk %.1f m)"), WalkCm / 100.f, *State.Start.ToCompactString(),
				FVector::Dist2D(State.Start, State.End) / 100.f));
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		case 1: // the pass: the rule, the HP, the squad at the start
		{
			if (State.StageTime < 0.1f)
			{
				return true;
			}
			if (State.StageTime < 0.2f)
			{
				SetWoundPace(State.Pass == EPass::OffLight ? 0 : 1);
				if (State.Pass != EPass::OnCritical && State.W->IsCriticallyWounded())
				{
					State.W->HealthComponent->RestoreHealth(FollowerHealth(State.Pass), 100.f);
				}
				ResetSquad(World, State);
				return true;
			}
			if (State.StageTime < 3.f) // settled (a critical man goes down by himself)
			{
				return true;
			}
			State.LeaderSpeed.Reset();
			State.WS = FFollowerSamples();
			State.OS = FFollowerSamples();
			State.PaceFrames = State.Frames = 0;
			State.CapSum = 0.f;
			State.PaceBy = TEXT("-");
			State.bShotTaken = false;
			State.SprintEndedAt = -1.f;
			const bool bSprint = State.Pass == EPass::OnSprint;
			A->OrderMoveTo(State.End, bSprint);
			State.bSprintSeenAtOrder = A->IsSprinting();
			State.Stage = 2;
			State.StageTime = 0.f;
			return true;
		}
		case 2: // the walk: the samples
		{
			if (State.Pass == EPass::OnSprint && State.SprintEndedAt < 0.f && !A->IsSprinting())
			{
				State.SprintEndedAt = State.StageTime;
			}
			const bool bArrived = FVector::Dist2D(A->GetActorLocation(), State.End) < 250.f;
			const float Walked = static_cast<float>(FVector::Dist2D(A->GetActorLocation(), State.Start));
			if (State.StageTime >= SampleAfterSeconds && !bArrived && A->GetVelocity().Size2D() > 20.f)
			{
				++State.Frames;
				float Cap = 0.f;
				AOperativeCharacter* By = nullptr;
				if (Squad->GetFormationWoundPace(Cap, By))
				{
					++State.PaceFrames;
					State.CapSum += Cap;
					State.PaceBy = By ? By->DisplayName.ToString() : TEXT("?");
				}
				State.LeaderSpeed.Add(static_cast<float>(A->GetVelocity().Size2D()));
				Sample(*Squad, State, *State.W, State.WS);
				Sample(*Squad, State, *State.Other, State.OS);
				if (Walked > 0.45f * WalkCm)
				{
					Shot(World, State);
				}
			}
			const bool bOver = bArrived || (State.Pass == EPass::OnSprint && State.StageTime > 12.f);
			if (!bOver && State.StageTime < 60.f)
			{
				return true;
			}
			Judge(*Squad, State);
			A->StopOperative();
			if (State.Pass == EPass::OnCritical)
			{
				State.W->HealthComponent->RestoreHealth(100.f, 100.f);
				return Finish(State, true);
			}
			State.Pass = static_cast<EPass>(static_cast<uint8>(State.Pass) + 1);
			State.Stage = 1;
			State.StageTime = 0.f;
			return true;
		}
		default:
			return false;
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShots = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("shots"), ESearchCase::IgnoreCase); });
		State->Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots/WoundedFollower"));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			UWorld* W = WeakWorld.Get();
			return W && !State->bDone && Step(W, *State, W->IsPaused() ? 0.f : W->GetDeltaSeconds());
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.WoundedFollowerSmoke"),
		TEXT("Dev check (the game designer's decision 2026-11-11, L_MovementTest): a formation of 3 walks 25 m with a wounded follower - rule off (the old pace) vs on at 40 % / 25 % HP ")
		TEXT("(the squad at 0.8 x his allowed walk, he keeps his slot and faces his formation look, never above his walk), the wounded leader (the followers do not overrun him), ")
		TEXT("a sprint order (ends), the critical wound (never sets the pace). Logs PASS/FAIL, then exits. Arg \"shots\": rendered game-camera frames to Saved/Screenshots/WoundedFollower."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
