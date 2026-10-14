// Dev-only console command for headless stance checks on L_MovementTest:
//   Scripts/smoke.ps1 -Command CodexTactics.StanceSmoke
// The squad must be spawned from BP_Operative. The leader goes crouch -> prone -> standing: the capsule takes the
// stance height (Godot 1.3 / 0.7 / 2.0 m ratios) while the feet stay on the ground. With prone clips set in
// ABP_Operative, crouch -> prone and prone -> standing play their transition clips and a prone shot plays the
// full-body prone fire clip. Then (user decision 2026-10-01): a crawling operative told to crouch stops first and
// rises in place; a sprint order (double click) to a prone operative stands him up in place, then he runs.
// Rev 5e item 6 (user request 2026-10-14): prone on the move - walking (>= 30 cm/s) the prone order dives with the walk clip
// (Crawl_from_Walk) without stopping, the capsule keeps going and he crawls on to the goal; sprinting (>= 75 cm/s) the run
// clip (Crawl_from_Run); walking slower than 30 cm/s the old way: he stops, then lies down in place (Crawl_from_Act).
// Rev 5f (game designer 2026-10-14: "he stops dead, dives in place, then the mesh snaps back"): the dive is ROOT MOTION. Every frame of
// the sprint dive is sampled (capsule and pelvis along the run direction, the mesh's offset on the capsule): the capsule carries
// >= 2.5 m, neither the capsule nor the body ever jumps back > 10 cm, the mesh never leaves its place on the capsule and the body
// lies where the dive ended (no snap when the clip hands over to the prone idle). The same sprint dive with the Female Soldier
// body (BP_Operative_FemaleSoldier's mesh and mesh transform, another skeleton) on the leader.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Debug/SmokeUtils.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"
#include "AIController.h"

namespace StanceSmoke
{
	constexpr float NavWarmupSeconds = 3.f;
	constexpr float FeetTolerance = 3.f;

	struct FState
	{
		int32 Second = 0;
		float StartFeetZ = 0.f;
		bool bOk = true;
		FVector CrawlStart = FVector::ZeroVector;
		bool bRiseOk = false;
		/** Pelvis height range while prone and firing / aiming (a jump to a standing pose shows as > ProneMaxPelvis). */
		float FireMinPelvis = TNumericLimits<float>::Max();
		float FireMaxPelvis = 0.f;
		FString FireSamples;
		/** Rev 5e dive passes. */
		FVector DiveStart = FVector::ZeroVector;
		float DiveSpeedAtOrder = 0.f;
		EProneEntry DiveEntry = EProneEntry::InPlace;
		float MinSpeedDuringDive = TNumericLimits<float>::Max();
		/** Rev 5f: the operative wearing the Female Soldier body for the second sprint dive, and the body he had before. */
		TWeakObjectPtr<AOperativeCharacter> Female;
		TWeakObjectPtr<USkeletalMesh> MaleMesh;
		FTransform MaleMeshTransform;
	};

	/** Rev 5f: one operative's dive sampled every frame (capsule / pelvis along the run direction, the mesh offset on the capsule). */
	struct FDiveTrace
	{
		TWeakObjectPtr<AOperativeCharacter> Who;
		FVector Start = FVector::ZeroVector;
		FVector Direction = FVector::ForwardVector;
		FVector MeshBase = FVector::ZeroVector;
		double StartTime = 0.0;
		/** X = seconds, Y = capsule along, Z = pelvis along (cm from the start), W = the mesh's XY offset from its base. */
		TArray<FVector4> Samples;
		FTSTicker::FDelegateHandle Handle;
	};

	float Along(const FDiveTrace& Trace, const FVector& Point)
	{
		return static_cast<float>(FVector::DotProduct(FVector(Point.X - Trace.Start.X, Point.Y - Trace.Start.Y, 0.f), Trace.Direction));
	}

	/** Samples Who every frame for Seconds from now (the dive must start right after this call). */
	TSharedRef<FDiveTrace> StartDiveTrace(AOperativeCharacter* Who, float Seconds)
	{
		TSharedRef<FDiveTrace> Trace = MakeShared<FDiveTrace>();
		Trace->Who = Who;
		Trace->Start = Who->GetActorLocation();
		const FVector Velocity = Who->GetVelocity();
		Trace->Direction = Velocity.GetSafeNormal2D().IsNearlyZero() ? Who->GetActorForwardVector().GetSafeNormal2D() : Velocity.GetSafeNormal2D();
		Trace->MeshBase = Who->GetMesh()->GetRelativeLocation();
		Trace->StartTime = Who->GetWorld()->GetTimeSeconds();
		Trace->Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Trace, Seconds](float)
		{
			const AOperativeCharacter* W = Trace->Who.Get();
			if (!W || !W->GetWorld())
			{
				return false;
			}
			const float T = static_cast<float>(W->GetWorld()->GetTimeSeconds() - Trace->StartTime);
			if (Trace->Samples.IsEmpty() || T > Trace->Samples.Last().X + 0.001f)
			{
				const FVector Offset = W->GetMesh()->GetRelativeLocation() - Trace->MeshBase;
				Trace->Samples.Add(FVector4(T, Along(*Trace, W->GetActorLocation()), Along(*Trace, W->GetMesh()->GetBoneLocation(TEXT("pelvis"))),
					FVector(Offset.X, Offset.Y, 0.f).Size()));
			}
			return T < Seconds;
		}));
		return Trace;
	}

	/**
	 * The dive checks of rev 5f: the capsule carried >= MinTravel; neither the capsule nor the pelvis jumped back > 10 cm between two
	 * frames (the old dive slid the body ahead and snapped it back); the mesh stayed on the capsule; the pelvis ends where the body
	 * then lies (no snap at the hand-over to the prone idle: the pelvis-to-capsule offset at the dive's end vs 0.6 s later).
	 */
	void CheckDiveTrace(FState& State, const FDiveTrace& Trace, const TCHAR* What, float MinTravel, float DiveEndSeconds)
	{
		float MaxCapsuleBack = 0.f;
		float MaxPelvisBack = 0.f;
		float MaxMeshOffset = 0.f;
		FString Frames;
		float NextLog = 0.f;
		float PelvisOffsetAtEnd = 0.f;
		bool bHaveEnd = false;
		for (int32 Index = 0; Index < Trace.Samples.Num(); ++Index)
		{
			const FVector4& S = Trace.Samples[Index];
			if (Index > 0)
			{
				MaxCapsuleBack = FMath::Max(MaxCapsuleBack, static_cast<float>(Trace.Samples[Index - 1].Y - S.Y));
				MaxPelvisBack = FMath::Max(MaxPelvisBack, static_cast<float>(Trace.Samples[Index - 1].Z - S.Z));
			}
			MaxMeshOffset = FMath::Max(MaxMeshOffset, static_cast<float>(S.W));
			if (S.X >= NextLog)
			{
				Frames += FString::Printf(TEXT(" %.2fs:%.0f/%.0f"), S.X, S.Y, S.Z);
				NextLog += 0.1f;
			}
			if (!bHaveEnd && S.X >= DiveEndSeconds)
			{
				PelvisOffsetAtEnd = static_cast<float>(S.Z - S.Y);
				bHaveEnd = true;
			}
		}
		const FVector4 Last = Trace.Samples.IsEmpty() ? FVector4(0.f, 0.f, 0.f, 0.f) : Trace.Samples.Last();
		const float Travel = static_cast<float>(Last.Y);
		const float PelvisOffsetLying = static_cast<float>(Last.Z - Last.Y);
		const float Snap = bHaveEnd ? FMath::Abs(PelvisOffsetLying - PelvisOffsetAtEnd) : 999.f;
		const bool bOk = Travel >= MinTravel && MaxCapsuleBack <= 10.f && MaxPelvisBack <= 10.f && MaxMeshOffset <= 1.f && Snap <= 15.f;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s frames (t: capsule / pelvis cm along the run):%s"), What, *Frames);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %d frames, capsule travel %.0f cm (>= %.0f), largest step back capsule %.1f / pelvis %.1f cm (<= 10), "
			"mesh off its place on the capsule %.1f cm (0), pelvis-to-capsule %.0f cm at the dive's end -> %.0f cm lying (snap %.1f cm, <= 15) -> %s"),
			What, Trace.Samples.Num(), Travel, MinTravel, MaxCapsuleBack, MaxPelvisBack, MaxMeshOffset, PelvisOffsetAtEnd, PelvisOffsetLying, Snap,
			bOk ? TEXT("ok") : TEXT("BAD"));
		State.bOk &= bOk;
	}

	TSharedPtr<FDiveTrace> LeaderDive;
	TSharedPtr<FDiveTrace> FemaleDive;

	/** Pelvis above the feet stays below this while lying (lying ~15 cm, crouched ~50, standing ~90). */
	constexpr float ProneMaxPelvis = 40.f;

	float FeetZ(const AOperativeCharacter* Operative)
	{
		return Operative->GetActorLocation().Z - Operative->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	}

	/** Pelvis bone above the feet, cm: the pose the graph really outputs (prone ~15, crouched ~50, standing ~90). */
	float PelvisHeight(const AOperativeCharacter* Operative)
	{
		return Operative->GetMesh()->GetBoneLocation(TEXT("pelvis")).Z - FeetZ(Operative);
	}

	bool Check(FState& State, AOperativeCharacter* Leader, EOperativeStance Expected)
	{
		const float Half = Leader->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
		const float ExpectedHalf = FMath::Max(Leader->GetStanceShape(Expected).CapsuleHalfHeight,
			Leader->GetCapsuleComponent()->GetUnscaledCapsuleRadius());
		const bool bOk = Leader->GetStance() == Expected && FMath::IsNearlyEqual(Half, ExpectedHalf, 0.5f)
			&& FMath::IsNearlyEqual(FeetZ(Leader), State.StartFeetZ, FeetTolerance);
		FString Clips;
		if (const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(Leader->GetMesh()->GetAnimInstance()))
		{
			Clips = FString::Printf(TEXT(" clips idle=%.2f crouch=%.2f prone=%.2f pelvis=%.0fcm transition=%d"),
				Anim->GetClipWeight(EOperativeClip::Idle), Anim->GetClipWeight(EOperativeClip::CrouchIdle),
				Anim->GetClipWeight(EOperativeClip::ProneIdle), PelvisHeight(Leader), Anim->IsPlayingStanceTransition() ? 1 : 0);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke t=%ds stance=%s halfHeight=%.1f (expected %.1f) feetZ=%.1f (start %.1f)%s -> %s"),
			State.Second, *AOperativeCharacter::GetStanceDisplayName(Leader->GetStance()).ToString(), Half, ExpectedHalf,
			FeetZ(Leader), State.StartFeetZ, *Clips, bOk ? TEXT("ok") : TEXT("BAD"));
		State.bOk &= bOk;
		return bOk;
	}

	/** 0.2 s after a stance change / shot: Clip (when set in the AnimBP) must be playing on the full-body slot. */
	void CheckFullBodyClipSoon(UWorld* World, FState& State, AOperativeCharacter* Leader, const TCHAR* What,
		TFunction<UAnimSequenceBase*(const UOperativeAnimInstance&)> GetClip)
	{
		TWeakObjectPtr<AOperativeCharacter> WeakLeader(Leader);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([&State, WeakLeader, What, GetClip]()
		{
			const AOperativeCharacter* L = WeakLeader.Get();
			const UOperativeAnimInstance* Anim = L ? Cast<UOperativeAnimInstance>(L->GetMesh()->GetAnimInstance()) : nullptr;
			UAnimSequenceBase* Clip = Anim ? GetClip(*Anim) : nullptr;
			const bool bOk = !Clip || Anim->IsPlayingSlotAnimation(Clip, Anim->FullBodySlot);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s clip=%s full-body playing=%d -> %s"), What, *GetNameSafe(Clip),
				Clip && Anim->IsPlayingSlotAnimation(Clip, Anim->FullBodySlot) ? 1 : 0, bOk ? TEXT("ok") : TEXT("BAD"));
			State.bOk &= bOk;
		}), 0.2f, false);
	}

	/** Returns true when finished. */
	bool Step(UWorld* World, FState& State)
	{
		AOperativeCharacter* Leader = World->GetSubsystem<USquadSubsystem>()->GetLeader();
		++State.Second;
		switch (State.Second)
		{
		case 1:
			Check(State, Leader, EOperativeStance::Standing);
			Leader->SetStance(EOperativeStance::Crouching);
			return false;
		case 2:
			Check(State, Leader, EOperativeStance::Crouching);
			Leader->SetStance(EOperativeStance::Prone);
			CheckFullBodyClipSoon(World, State, Leader, TEXT("crouch->prone"),
				[](const UOperativeAnimInstance& Anim) { return Anim.CrouchToProneAnimation.Get(); });
			return false;
		case 3:
			Check(State, Leader, EOperativeStance::Prone);
			return false;
		case 4:
		{
			// After the lying-down clip: a burst of shots plays the prone fire clip, and the body stays down through the
			// burst, the aim hold after it and the return to the idle (sampled every 0.05 s).
			Check(State, Leader, EOperativeStance::Prone);
			Leader->OnWeaponFiredNative.Broadcast(Leader, nullptr, false);
			CheckFullBodyClipSoon(World, State, Leader, TEXT("prone fire"),
				[](const UOperativeAnimInstance& Anim) { return Anim.FireProneAnimation.Get(); });
			TWeakObjectPtr<AOperativeCharacter> WeakLeader(Leader);
			TSharedRef<int32> Ticks = MakeShared<int32>(0);
			TSharedRef<FTimerHandle> Sampler = MakeShared<FTimerHandle>();
			World->GetTimerManager().SetTimer(*Sampler, FTimerDelegate::CreateLambda([&State, WeakLeader, Ticks, Sampler, World]()
			{
				AOperativeCharacter* L = WeakLeader.Get();
				if (!L || ++*Ticks > 80)
				{
					World->GetTimerManager().ClearTimer(*Sampler);
					return;
				}
				if (*Ticks % 3 == 0 && *Ticks <= 36) // a shot every 0.15 s for 1.8 s
				{
					L->OnWeaponFiredNative.Broadcast(L, nullptr, false);
				}
				const float Pelvis = PelvisHeight(L);
				State.FireMinPelvis = FMath::Min(State.FireMinPelvis, Pelvis);
				State.FireMaxPelvis = FMath::Max(State.FireMaxPelvis, Pelvis);
				if (*Ticks % 4 == 0)
				{
					const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(L->GetMesh()->GetAnimInstance());
					State.FireSamples += FString::Printf(TEXT(" %.2fs:%.0f%s"), *Ticks * 0.05f, Pelvis, Anim && Anim->bIsAiming ? TEXT("a") : TEXT(""));
				}
			}), 0.05f, true);
			return false;
		}
		case 5:
		case 6:
		case 7:
			return false;
		case 8:
		{
			const bool bDown = State.FireMaxPelvis <= ProneMaxPelvis;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke prone burst pelvis %.0f..%.0f cm (a = aiming):%s -> %s"),
				State.FireMinPelvis, State.FireMaxPelvis, *State.FireSamples, bDown ? TEXT("ok") : TEXT("BAD"));
			State.bOk &= bDown;
			Check(State, Leader, EOperativeStance::Prone);
			Leader->SetStance(EOperativeStance::Standing);
			CheckFullBodyClipSoon(World, State, Leader, TEXT("prone->standing"),
				[](const UOperativeAnimInstance& Anim) { return Anim.ProneToStandAnimation.Get(); });
			return false;
		}
		case 9:
			return false;
		case 10:
			Check(State, Leader, EOperativeStance::Standing);
			Leader->SetStance(EOperativeStance::Prone);
			return false;
		case 12:
			// Crawl 6 m.
			State.CrawlStart = Leader->GetActorLocation();
			Leader->OrderMoveTo(Leader->GetActorLocation() + Leader->GetActorForwardVector() * 600.f, false);
			return false;
		case 13:
		{
			const bool bCrawling = Leader->GetStance() == EOperativeStance::Prone && FVector::Dist2D(Leader->GetActorLocation(), State.CrawlStart) > 20.f;
			Leader->SetStance(EOperativeStance::Crouching);
			const bool bStopped = Leader->GetVelocity().Size2D() < 1.f && !Leader->IsMoving();
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke crawl -> crouch: crawling %d, stopped at once %d -> %s"), bCrawling ? 1 : 0, bStopped ? 1 : 0,
				bCrawling && bStopped ? TEXT("ok") : TEXT("BAD"));
			State.bOk &= bCrawling && bStopped;
			return false;
		}
		case 14:
			Leader->SetStance(EOperativeStance::Prone);
			return false;
		case 16:
			// A sprint order from prone: up first (in place), then the run.
			State.CrawlStart = Leader->GetActorLocation();
			Leader->OrderMoveTo(SmokeUtils::ClearPoint(Leader->GetWorld(), Leader->GetActorLocation(), Leader->GetActorLocation() - Leader->GetActorForwardVector() * 900.f), true);
			State.bRiseOk = Leader->GetStance() == EOperativeStance::Standing && Leader->GetVelocity().Size2D() < 1.f;
			return false;
		case 17:
			// 1 s in: still getting up (the clip is ~1.85 s), not moving yet.
			State.bRiseOk &= FVector::Dist2D(Leader->GetActorLocation(), State.CrawlStart) < 30.f;
			return false;
		case 19:
		{
			const float Moved = FVector::Dist2D(Leader->GetActorLocation(), State.CrawlStart);
			const bool bRunning = Leader->IsSprinting() && Moved > 150.f;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke prone sprint order: stood up in place %d, then running %d (moved %.0f cm) -> %s"),
				State.bRiseOk ? 1 : 0, bRunning ? 1 : 0, Moved, State.bRiseOk && bRunning ? TEXT("ok") : TEXT("BAD"));
			State.bOk &= State.bRiseOk && bRunning;
			Leader->StopOperative();
			return false;
		}
		case 11:
		case 15:
		case 18:
			return false;
		// --- Rev 5e item 6: prone on the move ---
		case 20:
			Check(State, Leader, EOperativeStance::Standing);
			State.DiveStart = Leader->GetActorLocation();
			Leader->OrderMoveTo(SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Leader->GetActorLocation() + Leader->GetActorForwardVector() * 1500.f), false);
			return false;
		case 21:
		{
			// Walking (~220 cm/s): the prone order dives on the move.
			State.DiveSpeedAtOrder = static_cast<float>(Leader->GetVelocity().Size2D());
			State.DiveStart = Leader->GetActorLocation();
			State.DiveEntry = Leader->DropProne();
			State.MinSpeedDuringDive = TNumericLimits<float>::Max();
			CheckFullBodyClipSoon(World, State, Leader, TEXT("walk->prone dive"), [](const UOperativeAnimInstance& Anim) { return Anim.WalkToProneAnimation.Get(); });
			TWeakObjectPtr<AOperativeCharacter> WeakLeader(Leader);
			TSharedRef<FTimerHandle> Sampler = MakeShared<FTimerHandle>();
			TSharedRef<int32> Ticks = MakeShared<int32>(0);
			World->GetTimerManager().SetTimer(*Sampler, FTimerDelegate::CreateLambda([&State, WeakLeader, Sampler, Ticks, World]()
			{
				const AOperativeCharacter* L = WeakLeader.Get();
				if (!L || ++*Ticks > 20)
				{
					World->GetTimerManager().ClearTimer(*Sampler);
					return;
				}
				State.MinSpeedDuringDive = FMath::Min(State.MinSpeedDuringDive, static_cast<float>(L->GetVelocity().Size2D()));
			}), 0.05f, true);
			const bool bOk = State.DiveEntry == EProneEntry::DiveFromWalk && Leader->GetStance() == EOperativeStance::Prone
				&& Leader->GetProneDive() == EProneDive::FromWalk && State.DiveSpeedAtOrder >= Leader->MovementConfig.ProneDiveMinWalkSpeed;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke walk %.0f cm/s -> prone order: entry %d, dive %d, stance prone %d -> %s"), State.DiveSpeedAtOrder,
				static_cast<int32>(State.DiveEntry), static_cast<int32>(Leader->GetProneDive()), Leader->GetStance() == EOperativeStance::Prone ? 1 : 0,
				bOk ? TEXT("ok") : TEXT("BAD"));
			State.bOk &= bOk;
			return false;
		}
		case 23:
		{
			const float Moved = static_cast<float>(FVector::Dist2D(Leader->GetActorLocation(), State.DiveStart));
			const bool bOk = Leader->GetStance() == EOperativeStance::Prone && !Leader->IsProneDiving() && Moved > 120.f && State.MinSpeedDuringDive > 20.f
				&& FMath::IsNearlyEqual(FeetZ(Leader), State.StartFeetZ, 15.f);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke walk dive: no stop (min speed %.0f cm/s during the clip), carried %.0f cm in 2 s, dive over %d, still on his order (moving %d) -> %s"),
				State.MinSpeedDuringDive, Moved, Leader->IsProneDiving() ? 0 : 1, Leader->IsMoving() ? 1 : 0, bOk ? TEXT("ok") : TEXT("BAD"));
			State.bOk &= bOk;
			Leader->StopOperative();
			Leader->SetStance(EOperativeStance::Standing);
			return false;
		}
		case 26:
			Check(State, Leader, EOperativeStance::Standing);
			Leader->OrderMoveTo(SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Leader->GetActorLocation() - Leader->GetActorForwardVector() * 1800.f), true);
			return false;
		case 27:
		{
			// Sprinting: the run dive.
			State.DiveSpeedAtOrder = static_cast<float>(Leader->GetVelocity().Size2D());
			const bool bSprinting = Leader->IsSprinting();
			State.DiveStart = Leader->GetActorLocation();
			LeaderDive = StartDiveTrace(Leader, 2.6f);
			State.DiveEntry = Leader->DropProne();
			CheckFullBodyClipSoon(World, State, Leader, TEXT("run->prone dive"), [](const UOperativeAnimInstance& Anim) { return Anim.RunToProneAnimation.Get(); });
			const bool bOk = bSprinting && State.DiveEntry == EProneEntry::DiveFromRun && Leader->GetProneDive() == EProneDive::FromRun;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke sprint %.0f cm/s (sprinting %d) -> prone order: run dive %d -> %s"), State.DiveSpeedAtOrder, bSprinting ? 1 : 0,
				Leader->GetProneDive() == EProneDive::FromRun ? 1 : 0, bOk ? TEXT("ok") : TEXT("BAD"));
			State.bOk &= bOk;
			return false;
		}
		case 29:
		{
			const float Moved = static_cast<float>(FVector::Dist2D(Leader->GetActorLocation(), State.DiveStart));
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			FNavLocation OnNav;
			const bool bOnNav = Nav && Nav->ProjectPointToNavigation(Leader->GetActorLocation(), OnNav, FVector(50.f, 50.f, 200.f));
			const bool bOk = Leader->GetStance() == EOperativeStance::Prone && !Leader->IsProneDiving() && Moved > 250.f && bOnNav;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke run dive: prone, carried %.0f cm, on the navmesh %d -> %s"), Moved, bOnNav ? 1 : 0, bOk ? TEXT("ok") : TEXT("BAD"));
			State.bOk &= bOk;
			if (LeaderDive.IsValid())
			{
				const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(Leader->GetMesh()->GetAnimInstance());
				const float DiveSeconds = Anim && Anim->RunToProneAnimation ? Anim->RunToProneAnimation->GetPlayLength() / FMath::Max(Anim->StanceTransitionPlayRate, 0.1f) : 1.2f;
				CheckDiveTrace(State, *LeaderDive, *FString::Printf(TEXT("sprint dive (%s, %s)"), *Leader->DisplayName.ToString(),
					*GetNameSafe(Leader->GetMesh()->GetSkeletalMeshAsset())), 250.f, DiveSeconds);
			}
			Leader->StopOperative();
			Leader->SetStance(EOperativeStance::Standing);
			return false;
		}
		case 32:
			Check(State, Leader, EOperativeStance::Standing);
			Leader->OrderMoveTo(SmokeUtils::ClearPoint(World, Leader->GetActorLocation(), Leader->GetActorLocation() + Leader->GetActorForwardVector() * 600.f), false);
			Leader->GetCharacterMovement()->MaxWalkSpeed = 20.f; // a slow walk, below the 30 cm/s dive threshold
			return false;
		case 33:
		{
			Leader->GetCharacterMovement()->MaxWalkSpeed = 20.f;
			State.DiveSpeedAtOrder = static_cast<float>(Leader->GetVelocity().Size2D());
			State.DiveEntry = Leader->DropProne();
			CheckFullBodyClipSoon(World, State, Leader, TEXT("slow walk->stop->prone"), [](const UOperativeAnimInstance& Anim) { return Anim.StandToProneAnimation.Get(); });
			const bool bOk = State.DiveEntry == EProneEntry::StopThenProne && !Leader->IsProneDiving() && !Leader->IsMoving()
				&& Leader->GetStance() == EOperativeStance::Prone;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke slow walk %.0f cm/s -> prone order: stopped first, lies down in place (entry %d) -> %s"),
				State.DiveSpeedAtOrder, static_cast<int32>(State.DiveEntry), bOk ? TEXT("ok") : TEXT("BAD"));
			State.bOk &= bOk;
			return false;
		}
		case 35:
			Leader->SetStance(EOperativeStance::Standing);
			return false;
		// --- Rev 5f: the same sprint dive with the Female Soldier body (another skeleton) ---
		case 38:
		{
			// The Female Soldier body (BP_Operative_FemaleSoldier: SK_Female_Soldier, Female_Soldier_Skeleton) put on the leader exactly as
			// her Blueprint sets it (mesh asset + the mesh component's transform), so the dive runs through the same squad / controller path.
			UClass* FemaleClass = LoadClass<AOperativeCharacter>(nullptr, TEXT("/Game/Characters/Operatives/BP_Operative_FemaleSoldier.BP_Operative_FemaleSoldier_C"));
			const AOperativeCharacter* FemaleDefaults = FemaleClass ? FemaleClass->GetDefaultObject<AOperativeCharacter>() : nullptr;
			USkeletalMesh* FemaleMesh = FemaleDefaults && FemaleDefaults->GetMesh() ? FemaleDefaults->GetMesh()->GetSkeletalMeshAsset() : nullptr;
			State.MaleMesh = Leader->GetMesh()->GetSkeletalMeshAsset();
			State.MaleMeshTransform = Leader->GetMesh()->GetRelativeTransform();
			if (FemaleMesh)
			{
				Leader->GetMesh()->SetSkeletalMeshAsset(FemaleMesh);
				Leader->GetMesh()->SetRelativeTransform(FemaleDefaults->GetMesh()->GetRelativeTransform());
			}
			const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(Leader->GetMesh()->GetAnimInstance());
			const bool bOk = FemaleMesh && Anim && Anim->RunToProneAnimation;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke female body on %s: %s (skeleton %s), anim %s -> %s"), *Leader->DisplayName.ToString(), *GetNameSafe(FemaleMesh),
				FemaleMesh ? *GetNameSafe(FemaleMesh->GetSkeleton()) : TEXT("-"), *GetNameSafe(Anim ? Anim->GetClass() : nullptr), bOk ? TEXT("ok") : TEXT("BAD"));
			State.bOk &= bOk;
			State.Female = Leader;
			return false;
		}
		case 40:
			if (AOperativeCharacter* Female = State.Female.Get())
			{
				const FVector Goal = SmokeUtils::ClearPoint(World, Female->GetActorLocation(), Female->GetActorLocation() - Female->GetActorForwardVector() * 1800.f);
				const EOperativeOrderResult Order = Female->OrderMoveTo(Goal, true);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke female move order: result %d, goal %.0f cm away, controller %s, mode %d, knocked %d, stance %d"),
					static_cast<int32>(Order), FVector::Dist2D(Goal, Female->GetActorLocation()), *GetNameSafe(Female->GetController()),
					static_cast<int32>(Female->GetCharacterMovement()->MovementMode.GetValue()), Female->IsKnockedDown() ? 1 : 0, static_cast<int32>(Female->GetStance()));
				// The prone order once she runs at full sprint (a freshly spawned body first lands and accelerates; at most 3 s).
				TWeakObjectPtr<AOperativeCharacter> WeakFemale(Female);
				TSharedRef<FTimerHandle> Poll = MakeShared<FTimerHandle>();
				TSharedRef<float> Waited = MakeShared<float>(0.f);
				World->GetTimerManager().SetTimer(*Poll, FTimerDelegate::CreateLambda([&State, WeakFemale, Poll, Waited, World]()
				{
					AOperativeCharacter* F = WeakFemale.Get();
					*Waited += 0.05f;
					const float Speed = F ? static_cast<float>(F->GetVelocity().Size2D()) : 0.f;
					if (F && (Speed < F->GetCharacterMovement()->MaxWalkSpeed * 0.9f || !F->IsSprinting()) && *Waited < 3.f)
					{
						return;
					}
					World->GetTimerManager().ClearTimer(*Poll);
					if (!F)
					{
						return;
					}
					FemaleDive = StartDiveTrace(F, 2.6f);
					const EProneEntry Entry = F->DropProne();
					const bool bOk = Entry == EProneEntry::DiveFromRun && F->GetProneDive() == EProneDive::FromRun;
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke female sprint %.0f cm/s after %.2f s (sprinting %d) -> prone order: entry %d, run dive %d -> %s"), Speed,
						*Waited, F->IsSprinting() ? 1 : 0, static_cast<int32>(Entry), F->GetProneDive() == EProneDive::FromRun ? 1 : 0, bOk ? TEXT("ok") : TEXT("BAD"));
					State.bOk &= bOk;
				}), 0.05f, true);
			}
			return false;
		case 41:
			return false;
		case 45:
			if (AOperativeCharacter* Female = State.Female.Get())
			{
				const UOperativeAnimInstance* Anim = Cast<UOperativeAnimInstance>(Female->GetMesh()->GetAnimInstance());
				const float DiveSeconds = Anim && Anim->RunToProneAnimation ? Anim->RunToProneAnimation->GetPlayLength() / FMath::Max(Anim->StanceTransitionPlayRate, 0.1f) : 1.2f;
				if (FemaleDive.IsValid())
				{
					CheckDiveTrace(State, *FemaleDive, *FString::Printf(TEXT("female sprint dive (%s)"), *GetNameSafe(Female->GetMesh()->GetSkeletalMeshAsset())), 250.f, DiveSeconds);
				}
				if (State.MaleMesh.IsValid())
				{
					Female->StopOperative();
					Female->SetStance(EOperativeStance::Standing);
					Female->GetMesh()->SetSkeletalMeshAsset(State.MaleMesh.Get());
					Female->GetMesh()->SetRelativeTransform(State.MaleMeshTransform);
				}
			}
			return false;
		case 22:
		case 24:
		case 25:
		case 28:
		case 30:
		case 31:
		case 34:
		case 36:
		case 37:
		case 39:
		case 42:
		case 43:
		case 44:
		case 46:
			return false;
		default:
			Check(State, Leader, EOperativeStance::Standing);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.bOk ? TEXT("PASS") : TEXT("FAIL"));
			FPlatformMisc::RequestExit(false, TEXT("StanceSmoke"));
			return true;
		}
	}

	void Start(UWorld* World)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Leader)
		{
			UE_LOG(LogCodexTactics, Error, TEXT("Smoke: no squad leader"));
			FPlatformMisc::RequestExit(false, TEXT("StanceSmoke"));
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->StartFeetZ = FeetZ(Leader);
		const bool bBlueprint = Leader->GetClass()->ClassGeneratedBy != nullptr;
		const UAnimInstance* Anim = Leader->GetMesh()->GetAnimInstance();
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke operative class=%s blueprint=%d placeholder=%d mesh=%s anim=%s weapon=%s"),
			*Leader->GetClass()->GetName(), bBlueprint ? 1 : 0, Leader->UsesPlaceholderBody() ? 1 : 0,
			*GetNameSafe(Leader->GetMesh()->GetSkeletalMeshAsset()), *GetNameSafe(Anim ? Anim->GetClass() : nullptr),
			*GetNameSafe(Leader->WeaponMesh ? Leader->WeaponMesh->GetStaticMesh() : nullptr));
		State->bOk = bBlueprint;
		// Headless (-nullrhi) nothing is rendered: refresh the bones anyway so the pelvis height is the real pose.
		Leader->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateLambda([WeakWorld, State, Handle]()
		{
			UWorld* W = WeakWorld.Get();
			if (W && Step(W, *State))
			{
				W->GetTimerManager().ClearTimer(*Handle);
			}
		}), 1.f, true);
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				Start(W);
			}
		}), NavWarmupSeconds, false);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.StanceSmoke"),
		TEXT("Dev check: BP_Operative spawned; stance capsule heights with feet kept on the ground; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
