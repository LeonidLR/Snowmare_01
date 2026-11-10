#include "Characters/OperativeAnimInstance.h"
#include "Ladders/LadderPoseIK.h"

#include "Combat/KnockdownComponent.h"
#include "Characters/HitReactionRules.h"
#include "Misc/ScopeExit.h"
#include "Characters/LeftHandIKRules.h"
#include "Characters/AimOffsetRules.h"
#include "Combat/SniperRules.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarDebugLeftHandIK(TEXT("Codex.Debug.LeftHandIK"), 0,
		TEXT("1: draw the left-hand IK spheres (red socket, green target, yellow hand_l, blue shoulder) in PIE / game."));
}
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "CodexTactics.h"
#include "HAL/IConsoleManager.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNodeBase.h"
#include "Engine/World.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "TwoBoneIK.h"
#include "Characters/ColdAnimationRules.h"
#include "Characters/OperativeCharacter.h"
#include "Falls/LedgeFallComponent.h"
#include "Combat/HealthComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Survival/ColdSurvivalComponent.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/RunCornerPeekRules.h"
#include "Tactics/TurnBasedCombatSubsystem.h"

namespace
{
	/** Playback rate range for speed-matched clips. */
	constexpr float MinPlayRate = 0.4f;
	constexpr float MaxPlayRate = 2.2f;
	/** Clips below this weight are not sampled. */
	constexpr float MinSampleWeight = 0.005f;

	/** Dev trace of what the AnimBP gets every frame (CodexTactics.AnimTrace 1). */
	TAutoConsoleVariable<int32> CVarAnimTrace(TEXT("CodexTactics.AnimTrace"), 0,
		TEXT("1 = log the operative / enemy anim inputs every frame (speed, direction, blend axes, play rates)."));

	int32 Slot(EOperativeClip Clip)
	{
		return static_cast<int32>(Clip);
	}
}

FAnimInstanceProxy* UOperativeAnimInstance::CreateAnimInstanceProxy()
{
	return new FOperativeAnimInstanceProxy(this);
}

void UOperativeAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	// The run-and-peek's peek stance: what the graph's layers add to the standing fire idles' drawn facing, as measured (learnt on from there).
	// (The rifle slots 0 / 1 only: a shotgun's left hand is on the pump - no left-hand IK, no bias.)
	PeekStanceBodyBias[CoverFacingRules::ClipIndex(ECoverFacing::Left)] = RunCornerPeekRules::StandDrawnBiasSeedDeg;
	PeekStanceBodyBias[CoverFacingRules::ClipIndex(ECoverFacing::Right)] = RunCornerPeekRules::StandDrawnBiasSeedDeg;
	LoadShotgunAnimSet(); // the D-1 set (weapon_anim_sets.json), once per instance
	if (AOperativeCharacter* Operative = Cast<AOperativeCharacter>(TryGetPawnOwner()))
	{
		BoundOperative = Operative;
		FiredHandle = Operative->OnWeaponFiredNative.AddUObject(this, &UOperativeAnimInstance::HandleWeaponFired);
		GrenadeHandle = Operative->OnGrenadeThrowNative.AddUObject(this, &UOperativeAnimInstance::HandleGrenadeThrow);
		if (UHealthComponent* Health = Operative->HealthComponent)
		{
			Health->OnHealthChanged.AddUniqueDynamic(this, &UOperativeAnimInstance::HandleHealthChanged);
			Health->OnDied.AddUniqueDynamic(this, &UOperativeAnimInstance::HandleDied);
		}
	}
	// Prone dives (rev 5e item 6, rev 5f): the root-motion copies of the crawl pack's transitions (Claude-owned, made by
	// Scripts/Editor/create_prone_dive_root_motion_clips.py; same skeleton as the prone clips the ABP already plays). The pack's
	// originals have no root motion: the dive would fall back to "stop, then lie down" (AOperativeCharacter::DropProne).
	if (!WalkToProneAnimation)
	{
		WalkToProneAnimation = LoadObject<UAnimSequenceBase>(nullptr,
			TEXT("/Game/Characters/Operatives/Anims/ProneDive/AS_Crawl_from_Walk_RM.AS_Crawl_from_Walk_RM"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	if (!RunToProneAnimation)
	{
		RunToProneAnimation = LoadObject<UAnimSequenceBase>(nullptr,
			TEXT("/Game/Characters/Operatives/Anims/ProneDive/AS_Crawl_from_Run_RM.AS_Crawl_from_Run_RM"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	// The dive's root motion comes from a montage: an AnimBP set to ignore root motion would leave the capsule behind the body.
	if (RootMotionMode == ERootMotionMode::NoRootMotionExtraction || RootMotionMode == ERootMotionMode::IgnoreRootMotion)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("%s: root motion mode %d -> from montages only (the prone dive is root motion)"), *GetName(),
			static_cast<int32>(RootMotionMode.GetValue()));
		RootMotionMode = ERootMotionMode::RootMotionFromMontagesOnly;
	}
	// Interaction clips + the cold idle variant (game designer 2026-10-15): Claude-owned montages / the pack clip when unset.
	LoadDefaultInteractionClips();
	// Blizzard walk (game designer 2026-10-22): the retargeted storm upper-body clips when the class defaults are empty.
	LoadDefaultBlizzardWalkClips();
	// Corner-ready pose (user rule 2026-10-06): until setup_operative_cover_animation.py assigns it, take the M4 pack's
	// look-at idles when the pack is in the project and the other cover clips come from it.
	if (bUseNativeCoverClips && (CoverStandIdle.ContainsByPredicate([](const TObjectPtr<UAnimSequenceBase>& Clip) { return Clip != nullptr; })))
	{
		auto Fill = [](TArray<TObjectPtr<UAnimSequenceBase>>& Clips, const TCHAR* Left, const TCHAR* Right)
		{
			Clips.SetNum(2);
			const TCHAR* Paths[2] = { Left, Right };
			for (int32 Index = 0; Index < 2; ++Index)
			{
				if (!Clips[Index])
				{
					Clips[Index] = LoadObject<UAnimSequenceBase>(nullptr, Paths[Index], nullptr, LOAD_NoWarn | LOAD_Quiet);
				}
			}
		};
		Fill(CoverStandCorner, TEXT("/Game/M4_Cover_Pack/Animations/stand/anim_M4_cvr_std_look_at_idle_L.anim_M4_cvr_std_look_at_idle_L"),
			TEXT("/Game/M4_Cover_Pack/Animations/stand/anim_M4_cvr_std_look_at_idle_R.anim_M4_cvr_std_look_at_idle_R"));
		Fill(CoverCrouchCorner, TEXT("/Game/M4_Cover_Pack/Animations/crouch/anim_M4_cvr_crch_look_at_idle_L.anim_M4_cvr_crch_look_at_idle_L"),
			TEXT("/Game/M4_Cover_Pack/Animations/crouch/anim_M4_cvr_crch_look_at_idle_R.anim_M4_cvr_crch_look_at_idle_R"));
		// Fire-ready corner stance + its transitions (user rule 2026-10-06, see UpdateCoverLayer).
		auto Stand = [](const TCHAR* Name) { return FString::Printf(TEXT("/Game/M4_Cover_Pack/Animations/stand/anim_M4_%s.anim_M4_%s"), Name, Name); };
		auto Crouch = [](const TCHAR* Name) { return FString::Printf(TEXT("/Game/M4_Cover_Pack/Animations/crouch/anim_M4_%s.anim_M4_%s"), Name, Name); };
		Fill(CoverStandFireIdle, *Stand(TEXT("cvr_std_fire_idle_L")), *Stand(TEXT("cvr_std_fire_idle_R")));
		Fill(CoverCrouchFireIdle, *Crouch(TEXT("cvr_crch_fire_idle_L")), *Crouch(TEXT("cvr_crch_fire_idle_R")));
		Fill(CoverStandFireEnter, *Stand(TEXT("cvr_std_idle_L_to_fire")), *Stand(TEXT("cvr_std_idle_R_to_fire")));
		Fill(CoverCrouchFireEnter, *Crouch(TEXT("cvr_crch_idle_to_fire_L")), *Crouch(TEXT("cvr_crch_idle_to_fire_R")));
		Fill(CoverStandFireExit, *Stand(TEXT("cvr_std_fire_to_std_idle_L")), *Stand(TEXT("cvr_std_fire_to_std_idle_R")));
		Fill(CoverCrouchFireExit, *Crouch(TEXT("cvr_crch_fire_to_idle_L")), *Crouch(TEXT("cvr_crch_fire_to_idle_R")));
		// Crouched set + stance switches at the wall (user request 2026-10-07; [_L, _R], pack _L = his own right).
		Fill(CoverCrouchIdle, *Crouch(TEXT("cvr_crch_idle_L")), *Crouch(TEXT("cvr_crch_idle_R")));
		Fill(CoverCrouchFire, *Crouch(TEXT("cvr_crch_fire_L")), *Crouch(TEXT("cvr_crch_fire_R")));
		Fill(CoverCrouchMoveForward, *Crouch(TEXT("cvr_crch_walk_fwd_loop_L")), *Crouch(TEXT("cvr_crch_walk_fwd_loop_R")));
		Fill(CoverCrouchMoveBackward, *Crouch(TEXT("cvr_crch_walk_bwd_loop_L")), *Crouch(TEXT("cvr_crch_walk_bwd_loop_R")));
		Fill(CoverCrouchEnter, *Crouch(TEXT("crch_idle_fwd_to_cvr_crch_idle_L")), *Crouch(TEXT("crch_idle_fwd_to_cvr_crch_idle_R")));
		Fill(CoverCrouchToStand, *Crouch(TEXT("cvr_crch_idle_L_to_cvr_stand_idle_L")), *Crouch(TEXT("cvr_crch_idle_R_to_cvr_stand_idle_R")));
		Fill(CoverStandToCrouch, *Stand(TEXT("cvr_stand_idle_L_to_cvr_crch_idle_L")), *Stand(TEXT("cvr_stand_idle_R_to_cvr_crch_idle_R")));
		// Wall-edge cover switch (game designer 2026-10-20): the run-in transitions, index = approach * 2 + end side
		// (CoverSwitchRules::ClipArrayIndex; the names come from CoverSwitchRules::ClipName).
		for (int32 SetIndex = 0; SetIndex < 4; ++SetIndex)
		{
			// 0 / 1: the jog run-ins standing / crouched, 2 / 3: the sprint ones (2026-10-21).
			const int32 CrouchIndex = SetIndex % 2;
			const bool bSprintSet = SetIndex >= 2;
			TArray<TObjectPtr<UAnimSequenceBase>>& Clips = bSprintSet ? (CrouchIndex == 1 ? CoverCrouchSwitchSprintEnter : CoverStandSwitchSprintEnter)
				: (CrouchIndex == 1 ? CoverCrouchSwitchEnter : CoverStandSwitchEnter);
			Clips.SetNum(CoverSwitchRules::ClipCount);
			for (const ECoverSwitchApproach Approach : { ECoverSwitchApproach::Front, ECoverSwitchApproach::FromPackL, ECoverSwitchApproach::FromPackR })
			{
				for (const ECoverFacing End : { ECoverFacing::Right, ECoverFacing::Left })
				{
					const int32 Index = CoverSwitchRules::ClipArrayIndex(Approach, End);
					if (!Clips[Index])
					{
						const FString Name = CoverSwitchRules::ClipName(Approach, End, CrouchIndex == 1, bSprintSet);
						const FString Path = FString::Printf(TEXT("/Game/M4_Cover_Pack/Animations/%s/%s.%s"), CrouchIndex == 1 ? TEXT("crouch") : TEXT("stand"), *Name, *Name);
						Clips[Index] = LoadObject<UAnimSequenceBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
					}
				}
			}
		}
	}
}

void UOperativeAnimInstance::NativeUninitializeAnimation()
{
	if (AOperativeCharacter* Operative = BoundOperative.Get())
	{
		Operative->OnWeaponFiredNative.Remove(FiredHandle);
		Operative->OnGrenadeThrowNative.Remove(GrenadeHandle);
		if (UHealthComponent* Health = Operative->HealthComponent)
		{
			Health->OnHealthChanged.RemoveDynamic(this, &UOperativeAnimInstance::HandleHealthChanged);
			Health->OnDied.RemoveDynamic(this, &UOperativeAnimInstance::HandleDied);
		}
	}
	SetWarmWeaponHidden(false);
	BoundOperative.Reset();
	Super::NativeUninitializeAnimation();
}

void UOperativeAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	StateDeltaSeconds = DeltaSeconds;
	LastAnimUpdateFrame = GFrameCounter;
	SettlePeekStanceYawLock();
	if (PeekStanceYawLock.bActive && PeekStanceYawLockSeconds() > PeekStanceYawLock.HoldSeconds + RunCornerPeekRules::LockFadeSeconds)
	{
		PeekStanceYawLock.bActive = false; // let go (the proxy saw its fade to 0)
	}
	UpdateState();
	UpdateStanceTransition();
	UpdateUpperBody(DeltaSeconds);
	UpdateColdLayer(DeltaSeconds);
	UpdateNativeBlend(DeltaSeconds);
	UpdateShotgunLayer(DeltaSeconds); // the D-1 shotgun set over the ABP (OperativeAnimInstanceShotgun.cpp)
	UpdateInjuredLayer(DeltaSeconds); // the injured walk / slow walk on the legs; decides the injured crawl set (OperativeAnimInstanceInjured.cpp)
	UpdateProneCrawlLayer(DeltaSeconds); // the 8-direction prone crawl over the ABP's prone pose (OperativeAnimInstanceProneCrawl.cpp)
	if (bUseRifle2Locomotion)
	{
		const AOperativeCharacter* Rifle2Owner = Cast<AOperativeCharacter>(TryGetPawnOwner());
		UpdateRifle2Locomotion(DeltaSeconds, Rifle2Owner && (bIsClimbing ? Speed > 1.f : Rifle2Owner->IsMoving()), Speed, Direction,
			Rifle2Owner ? Rifle2Owner->GetActorRotation().Yaw : 0.f);
	}
	if (CVarAnimTrace.GetValueOnGameThread() > 0 && (Speed > 0.f || TryGetPawnOwner() && TryGetPawnOwner()->GetVelocity().SizeSquared2D() > 0.f))
	{
		if (const APawn* Pawn = TryGetPawnOwner())
		{
			UE_LOG(LogCodexTactics, Display, TEXT("AnimTrace %s t=%.3f dt=%.3f vel=%.0f speed=%.0f dir=%.0f stand=%.0f/%.2f slow=%.0f/%.2f moving=%d aim=%d crouch=%d prone=%d slots=%.2f/%.2f"),
				*Pawn->GetName(), Pawn->GetWorld()->GetTimeSeconds(), DeltaSeconds, Pawn->GetVelocity().Size2D(), Speed, Direction,
				StandBlendSpeed, StandPlayRate, SlowBlendSpeed, SlowPlayRate, bIsMoving ? 1 : 0, bIsAiming ? 1 : 0, bIsCrouching ? 1 : 0,
				bIsProne ? 1 : 0, GetSlotMontageGlobalWeight(UpperBodySlot), GetSlotMontageGlobalWeight(FullBodySlot));
		}
	}
}

void UOperativeAnimInstance::EnterRifleLocoState(ERifleLocoState NewState, UAnimSequence* Clip)
{
	LocoState = NewState;
	LocoStateTime = 0.f;
	LocoClipLength = Clip ? Clip->GetPlayLength() : 0.f;
	bLocoIdle = NewState == ERifleLocoState::Idle;
	bLocoIdleBreak = NewState == ERifleLocoState::IdleBreak;
	bLocoTurn = NewState == ERifleLocoState::Turn;
	bLocoStart = NewState == ERifleLocoState::Start;
	bLocoWalk = NewState == ERifleLocoState::Walk;
	bLocoStop = NewState == ERifleLocoState::Stop;
	switch (NewState)
	{
	case ERifleLocoState::IdleBreak: LocoBreakClip = Clip; break;
	case ERifleLocoState::Turn: LocoTurnClip = Clip; LocoTurnEased = 0.f; break;
	case ERifleLocoState::Start: LocoStartClip = Clip; break;
	case ERifleLocoState::Stop: LocoStopClip = Clip; break;
	case ERifleLocoState::Walk: LocoWalkSeconds = 0.f; break;
	default: LocoIdleSeconds = 0.f; break;
	}
}

void UOperativeAnimInstance::UpdateRifle2Locomotion(float DeltaSeconds, bool bMoveIntent, float InSpeed, float InDirection, float ActorYaw)
{
	using namespace RifleLocomotionRules;
	LocoIdleClip = Rifle2IdleLoop;
	Rifle2BlendSpeed = FMath::Min(InSpeed, Rifle2WalkClipSpeed);
	Rifle2PlayRate = FMath::Max(1.f, InSpeed / FMath::Max(1.f, Rifle2WalkClipSpeed));
	LocoStateTime += DeltaSeconds;

	// The root yaw offset: standing still the body keeps its world facing while the actor turns; moving it catches up.
	const float ActorDelta = bLocoHasYaw ? DeltaYaw(LocoLastActorYaw, ActorYaw) : 0.f;
	LocoLastActorYaw = ActorYaw;
	bLocoHasYaw = true;
	const bool bStill = LocoState == ERifleLocoState::Idle || LocoState == ERifleLocoState::IdleBreak || LocoState == ERifleLocoState::Turn;
	if (bStill)
	{
		RootYawOffset = FMath::UnwindDegrees(RootYawOffset - ActorDelta);
	}
	else
	{
		RootYawOffset = FMath::FInterpTo(RootYawOffset, 0.f, DeltaSeconds, 10.f);
	}

	auto PickClip = [](const TArray<TObjectPtr<UAnimSequence>>& Clips, int32 Index) -> UAnimSequence*
	{
		if (Clips.IsValidIndex(Index) && Clips[Index])
		{
			return Clips[Index];
		}
		// The other foot of the same sector, else nothing (the state is skipped).
		const int32 Other = Index ^ 1;
		return Clips.IsValidIndex(Other) ? Clips[Other].Get() : nullptr;
	};

	switch (LocoState)
	{
	case ERifleLocoState::Idle:
	case ERifleLocoState::IdleBreak:
	{
		if (ShouldStart(InSpeed, bMoveIntent))
		{
			const int32 Sector = DirectionSector(InDirection);
			UAnimSequence* Clip = PickClip(Rifle2Starts, ClipIndex(Sector, StartFoot(Sector)));
			EnterRifleLocoState(Clip ? ERifleLocoState::Start : ERifleLocoState::Walk, Clip);
			break;
		}
		if (ShouldTurnInPlace(RootYawOffset, InSpeed, bMoveIntent))
		{
			bool bRight = false;
			const int32 Bucket = TurnBucket(RootYawOffset, bRight);
			const TArray<TObjectPtr<UAnimSequence>>& Turns = bRight ? Rifle2TurnRight : Rifle2TurnLeft;
			if (UAnimSequence* Clip = Turns.IsValidIndex(Bucket) ? Turns[Bucket].Get() : nullptr)
			{
				LocoTurnDegrees = BucketDegrees(Bucket);
				bLocoTurnRight = bRight;
				EnterRifleLocoState(ERifleLocoState::Turn, Clip);
				break;
			}
		}
		if (LocoState == ERifleLocoState::IdleBreak)
		{
			if (LocoStateTime >= LocoClipLength - 0.25f)
			{
				EnterRifleLocoState(ERifleLocoState::Idle, nullptr);
				LocoNextBreak = FMath::FRandRange(Rifle2IdleBreakMinSeconds, Rifle2IdleBreakMaxSeconds);
			}
			break;
		}
		// A small leftover offset (under the turn trigger) settles slowly; now and then an idle break.
		RootYawOffset = FMath::FInterpTo(RootYawOffset, 0.f, DeltaSeconds, 1.5f);
		LocoIdleSeconds += DeltaSeconds;
		if (LocoIdleSeconds >= LocoNextBreak && Rifle2IdleBreaks.Num() > 0)
		{
			if (UAnimSequence* Break = Rifle2IdleBreaks[FMath::RandRange(0, Rifle2IdleBreaks.Num() - 1)])
			{
				EnterRifleLocoState(ERifleLocoState::IdleBreak, Break);
			}
		}
		break;
	}
	case ERifleLocoState::Turn:
	{
		// The clip turns the body: the offset closes by the bucket's degrees over the clip (eased).
		const float Progress = LocoClipLength > 0.f ? FMath::Clamp(LocoStateTime / LocoClipLength, 0.f, 1.f) : 1.f;
		const float Eased = FMath::InterpEaseInOut(0.f, 1.f, Progress, 2.f);
		RootYawOffset = FMath::UnwindDegrees(RootYawOffset + (bLocoTurnRight ? 1.f : -1.f) * LocoTurnDegrees * (Eased - LocoTurnEased));
		LocoTurnEased = Eased;
		if (ShouldStart(InSpeed, bMoveIntent))
		{
			const int32 Sector = DirectionSector(InDirection);
			UAnimSequence* Clip = PickClip(Rifle2Starts, ClipIndex(Sector, StartFoot(Sector)));
			EnterRifleLocoState(Clip ? ERifleLocoState::Start : ERifleLocoState::Walk, Clip);
		}
		else if (Progress >= 1.f)
		{
			EnterRifleLocoState(ERifleLocoState::Idle, nullptr);
		}
		break;
	}
	case ERifleLocoState::Start:
		if (ShouldStop(bMoveIntent))
		{
			const int32 Sector = DirectionSector(InDirection);
			UAnimSequence* Clip = bInCover ? nullptr : PickClip(Rifle2Stops, ClipIndex(Sector, StartFoot(Sector))); // into cover: the enter clip, no stop
			EnterRifleLocoState(Clip ? ERifleLocoState::Stop : ERifleLocoState::Idle, Clip);
		}
		else if (LocoStateTime >= FMath::Max(0.f, LocoClipLength - StartBlendOut))
		{
			EnterRifleLocoState(ERifleLocoState::Walk, nullptr);
		}
		break;
	case ERifleLocoState::Walk:
		LocoWalkSeconds += DeltaSeconds;
		if (ShouldStop(bMoveIntent))
		{
			const int32 Sector = DirectionSector(InDirection);
			UAnimSequence* Clip = bInCover ? nullptr : PickClip(Rifle2Stops, ClipIndex(Sector, StopFoot(LocoWalkSeconds, Rifle2WalkCycleSeconds)));
			EnterRifleLocoState(Clip ? ERifleLocoState::Stop : ERifleLocoState::Idle, Clip);
		}
		break;
	case ERifleLocoState::Stop:
		if (ShouldStart(InSpeed, bMoveIntent))
		{
			const int32 Sector = DirectionSector(InDirection);
			UAnimSequence* Clip = PickClip(Rifle2Starts, ClipIndex(Sector, StartFoot(Sector)));
			EnterRifleLocoState(Clip ? ERifleLocoState::Start : ERifleLocoState::Walk, Clip);
		}
		else if (LocoStateTime >= LocoClipLength)
		{
			EnterRifleLocoState(ERifleLocoState::Idle, nullptr);
		}
		break;
	}
}

void UOperativeAnimInstance::UpdateVaultClip(const AOperativeCharacter& Operative)
{
	const bool bVaulting = Operative.IsVaulting();
	if (bVaulting && !bWasVaulting && Operative.GetVaultDuration() >= VaultMinClipDuration)
	{
		// Left / right foot in turn (a missing one falls back to the other).
		UAnimSequenceBase* Clip = bVaultLeftNext ? VaultLeftFootAnimation : VaultRightFootAnimation;
		float Landing = bVaultLeftNext ? VaultLeftFootLandingTime : VaultRightFootLandingTime;
		if (!Clip)
		{
			Clip = bVaultLeftNext ? VaultRightFootAnimation : VaultLeftFootAnimation;
			Landing = bVaultLeftNext ? VaultRightFootLandingTime : VaultLeftFootLandingTime;
		}
		bVaultLeftNext = !bVaultLeftNext;
		if (Clip && !bIsDead)
		{
			// The clip's landing frame on the arc's end: faster for a running vault, slower for a careful one.
			const float PlayRate = FMath::Clamp(Landing / FMath::Max(Operative.GetVaultDuration(), 0.1f), 0.5f, 2.5f);
			VaultMontage = PlaySlotAnimationAsDynamicMontage(Clip, FullBodySlot, 0.1f, 0.25f, PlayRate);
			VaultClipsPlayed += VaultMontage.IsValid() ? 1 : 0;
			UE_LOG(LogCodexTactics, Display, TEXT("[Vault] %s plays %s at x%.2f (vault %.2f s)"), *Operative.DisplayName.ToString(), *Clip->GetName(),
				PlayRate, Operative.GetVaultDuration());
		}
	}
	else if (!bVaulting && bWasVaulting)
	{
		// Landed: walking on -> the recovery blends out into the locomotion; standing -> it plays to the end.
		UAnimMontage* Montage = VaultMontage.Get();
		if (Montage && Montage_IsPlaying(Montage) && Operative.IsMoving())
		{
			Montage_Stop(0.25f, Montage);
		}
	}
	bWasVaulting = bVaulting;
}

void UOperativeAnimInstance::UpdateColdLayer(float DeltaSeconds)
{
	// Godot cold_animation_controller.gd: presentation only. Eligible = standing locomotion, not aiming / sprinting /
	// reloading, alive (locomotion_controller.gd cold_eligible).
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(TryGetPawnOwner());
	ColdVisualTier = ColdAnimationRules::SelectTier(ColdVisualTier, Operative ? Operative->ColdLevel : 0.f, ColdThresholds, ColdHysteresis);
	auto Resolve = [this](const TArray<TObjectPtr<UAnimSequenceBase>>& Clips) -> UAnimSequenceBase*
	{
		TArray<bool> Set;
		for (const TObjectPtr<UAnimSequenceBase>& Clip : Clips)
		{
			Set.Add(Clip != nullptr);
		}
		const int32 Index = ColdAnimationRules::ResolveClipIndex(Set, ColdVisualTier);
		return Clips.IsValidIndex(Index) ? Clips[Index].Get() : nullptr;
	};
	if (ColdVisualTier > 0)
	{
		ColdIdleAnimation = Resolve(ColdIdleClips);
		ColdWalkAnimation = Resolve(ColdWalkClips);
		ColdMoveBlend = FMath::Clamp(Speed / ColdWalkFullSpeed, 0.f, 1.f);
	}
	const bool bHasPose = ColdIdleAnimation && ColdWalkAnimation;
	const bool bEligible = bHasPose && Stance == EOperativeStance::Standing && !bIsAiming && !bIsSprinting && !bIsReloading && !bIsDead && !bIsClimbing;
	ColdVisualWeight = ColdAnimationRules::StepWeight(ColdVisualWeight, ColdVisualTier, bEligible, DeltaSeconds);
}

void UOperativeAnimInstance::HandleWeaponFired(AOperativeCharacter* Shooter, AActor* Target, bool bHit)
{
	if (Shooter && (Shooter->IsKnockedDown() || Shooter->IsClimbingLadder()))
	{
		return; // Sprint 14: no fire clip over the knockdown (nor over the climb)
	}
	AimTimer = AimHoldAfterShot;
	DemandInjuredRaise(); // wounded: the weapon stays up for lower_after_s after the shot (feat/injured-tiers-fullbody)
	// Sniper rifle (user request 2026-10-09): the stance's shot + the bolt while rounds remain, full body.
	if (bSniperWeapon && bSniperPose && Shooter)
	{
		PlaySniperShot(SniperRules::ShouldCycleBolt(Shooter->CurrentClip));
		return;
	}
	// Sprint 12: a shot from cover plays the corner fire (or blind fire) clip full body; the loop resumes after it.
	if (bInCover && bUseNativeCoverClips && !bIsProne && !bIsReloading)
	{
		UAnimSequenceBase* Clip = Shooter && Shooter->bIsBlindFiring ? PickCoverClip(CoverBlindFire) : nullptr;
		if (!Clip)
		{
			Clip = PickCoverClip(bIsCrouching ? CoverCrouchFire : CoverStandFire);
		}
		if (Clip)
		{
			// Shot from the corner (user rule 2026-10-06): from the plain cover idle the idle -> fire transition plays first, then
			// the shot, then the fire-ready idle again; already in the fire-ready pose the shot plays at once.
			const bool bCornerShot = !(Shooter && Shooter->bIsBlindFiring)
				&& (bCoverAtCorner || CoverHeight == ECoverHeight::LowCover || (Shooter && Shooter->IsCornerAimActive())); // sustained aim: from fire_idle, no cycle per shot; a low cover is fired over anywhere
			if (bCornerShot && PickCoverClip(bIsCrouching ? CoverCrouchFireIdle : CoverStandFireIdle))
			{
				if (CoverPendingFireClip.IsValid())
				{
					return; // the transition is playing and a shot is queued behind it
				}
				UAnimSequenceBase* Enter = bCoverInFirePose ? nullptr : PickCoverClip(bIsCrouching ? CoverCrouchFireEnter : CoverStandFireEnter);
				bCoverInFirePose = true;
				if (Enter && PlayCoverOneShot(Enter, 0.1f, 0.05f))
				{
					CoverPendingFireClip = Clip;
					return;
				}
			}
			PlayCoverOneShot(Clip, 0.1f, 0.05f);
			return;
		}
	}
	if (bIsProne)
	{
		// Godot ProneFire: the prone body shoots full body; the standing fire montage is not layered over it.
		if (FireProneAnimation && !bIsReloading && !IsPlayingStanceTransition())
		{
			PlaySlotAnimationAsDynamicMontage(FireProneAnimation, FullBodySlot, 0.05f, 0.15f);
		}
		return;
	}
	// #51 (designer video 2026-11-06, measured by CodexTactics.RecoilProbe 2026-11-08): a fire montage started while a stance transition
	// plays (prone -> crouch / stand: the full-body Crawl_to_* clip) stopped that transition at once - montages of one group replace
	// each other - and the body jumped from the floor to the crouch in ONE frame (40-80 cm), for the squad and the PMC alike: a fighter
	// rising to shoot looked thrown up / back with every first shot. The shot goes out; the rise keeps playing and the next shot's clip
	// plays over the finished stance (the prone fire above already waited for it).
	if (IsPlayingStanceTransition())
	{
		++FireClipsSkippedForStance;
		return;
	}
	if (PlayShotgunFire())
	{
		return; // the D-1: the pack's aim / hip shot on the upper body (OperativeAnimInstanceShotgun.cpp)
	}
	UAnimMontage* Montage = bIsAiming && FireAimMontage ? FireAimMontage : FireMontage;
	if (Montage && !bIsReloading)
	{
		Montage_Play(Montage);
	}
}

UAnimSequenceBase* UOperativeAnimInstance::GetProneDiveClip(EProneDive Dive) const
{
	switch (Dive)
	{
	case EProneDive::FromWalk: return WalkToProneAnimation.Get();
	case EProneDive::FromRun: return RunToProneAnimation.Get();
	default: return nullptr;
	}
}

float UOperativeAnimInstance::GetStanceTransitionPosition(const UAnimSequenceBase* Clip) const
{
	const UAnimMontage* Montage = StanceTransitionMontage.Get();
	if (!Clip || !Montage || !Montage_IsPlaying(Montage) || !IsPlayingSlotAnimation(Clip, FullBodySlot))
	{
		return -1.f;
	}
	return Montage_GetPosition(Montage);
}

bool UOperativeAnimInstance::IsPlayingStanceTransition() const
{
	const UAnimMontage* Montage = StanceTransitionMontage.Get();
	return Montage && Montage_IsPlaying(Montage);
}

float UOperativeAnimInstance::GetStanceTransitionTimeLeft() const
{
	const UAnimMontage* Montage = StanceTransitionMontage.Get();
	if (!Montage || !Montage_IsPlaying(Montage))
	{
		return 0.f;
	}
	return FMath::Max(0.f, (Montage->GetPlayLength() - Montage_GetPosition(Montage)) / FMath::Max(StanceTransitionPlayRate, 0.1f)
		- StanceTransitionBlendTime);
}

void UOperativeAnimInstance::UpdateStanceTransition()
{
	// Godot locomotion_controller.gd: ProneStart when the stance becomes prone, ProneEnd when it leaves it (auto-advance
	// at the clip's end); here the clip plays on the full-body slot over the already switched locomotion.
	if (!BoundOperative.IsValid())
	{
		return;
	}
	const EOperativeStance From = PreviousStance.Get(Stance);
	PreviousStance = Stance;
	if (From == Stance || bIsDead || bKnockedDown)
	{
		return; // knocked down: the get-up clip ends standing, no stance clip on top
	}
	if (IsInteractionClipActive() && InteractionLayer == EInteractionAnimLayer::FullBody)
	{
		return; // the full-body pickup after a crouched disarm carries the stand-up itself (it starts low and rises)
	}
	if (bInCover && bUseNativeCoverClips && bCoverEnteredThisFrame)
	{
		return; // the cover enter clip carries the stance change (stand -> crouch at a low cover)
	}
	// Stand <-> crouch at the wall (user request 2026-10-07): the pack's cover transitions on the facing side
	// (cvr_crch_idle_L_to_cvr_stand_idle_L ... measured: they keep the side and end in the other stance's cover idle).
	if (bInCover && bUseNativeCoverClips && !bShimmying && From != EOperativeStance::Prone && Stance != EOperativeStance::Prone)
	{
		// In the corner fire stance (user PIE 2026-10-07: the switch clips go idle -> idle, so the body tucked back behind the
		// corner and stepped out again): straight from one stance's fire_idle to the other's, the rifle stays up.
		UAnimSequenceBase* OtherFireIdle = PickCoverClip(Stance == EOperativeStance::Crouching ? CoverCrouchFireIdle : CoverStandFireIdle);
		if (bCoverInFirePose && OtherFireIdle)
		{
			// The cover layer (it runs first in the frame, with the new stance) may already have started it: keep that one.
			if (UAnimMontage* Loop = CoverLoopMontage.Get(); Loop && Montage_IsPlaying(Loop) && CoverLoopClip.Get() == OtherFireIdle)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[CoverAnim] stance switch in the corner fire stance: %s already blending in"), *OtherFireIdle->GetName());
				return;
			}
			CoverOneShotMontage.Reset(); // the old stance's loop / shot / idle -> fire transition fade out in PlayCoverMontage
			CoverPendingFireClip.Reset();
			bCoverEnterPlaying = false;
			CoverLoopClip = OtherFireIdle;
			CoverLoopMontage = PlayCoverMontage(OtherFireIdle, FAlphaBlendArgs(CoverFireStanceSwitchBlendSeconds), 0.2f, 1.f, /*LoopCount*/ 1000);
			// The loop is the stance clip here: the cover layer waits for it like for a switch clip, then keeps it.
			StanceTransitionMontage.Reset();
			if (CoverLoopMontage.IsValid())
			{
				++CoverClipsPlayed;
				CoverClipLog.Add(OtherFireIdle->GetName());
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[CoverAnim] stance switch in the corner fire stance: blend to %s (%.2f s)"),
				*OtherFireIdle->GetName(), CoverFireStanceSwitchBlendSeconds);
			return;
		}
		if (UAnimSequenceBase* CoverSwitch = PickCoverClip(Stance == EOperativeStance::Crouching ? CoverStandToCrouch : CoverCrouchToStand))
		{
			CoverLoopMontage.Reset();
			CoverLoopClip.Reset();
			CoverPendingFireClip.Reset();
			bCoverInFirePose = false; // the switch starts from the cover idle; the fire stance is re-entered after it
			StanceTransitionMontage = PlayCoverMontage(CoverSwitch, FAlphaBlendArgs(0.15f), 0.2f);
			if (StanceTransitionMontage.IsValid())
			{
				++CoverClipsPlayed;
				CoverClipLog.Add(CoverSwitch->GetName());
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[CoverAnim] stance switch at the wall: %s (%s)"), *CoverSwitch->GetName(),
				StanceTransitionMontage.IsValid() ? TEXT("playing") : TEXT("not played"));
			return;
		}
	}
	if (bInCover)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[CoverAnim] stance change in cover without a cover switch (native %d, shimmy %d)"),
			bUseNativeCoverClips ? 1 : 0, bShimmying ? 1 : 0);
	}
	// Prone on the move (rev 5e item 6): the walk / run dive replaces the lie-down clip (the character carries the capsule along).
	if (Stance == EOperativeStance::Prone && ProneDive != EProneDive::None)
	{
		if (UAnimSequenceBase* Dive = GetProneDiveClip(ProneDive))
		{
			StopSlotAnimation(StanceTransitionBlendTime, UpperBodySlot);
			// Rev 5f: a short blend-in - the root motion is weighted by the montage, a long one would brake the run for a moment.
			StanceTransitionMontage = PlaySlotAnimationAsDynamicMontage(Dive, FullBodySlot, ProneDiveBlendInSeconds, StanceTransitionBlendTime,
				StanceTransitionPlayRate);
			UE_LOG(LogCodexTactics, Display, TEXT("[StanceAnim] prone dive: %s (root motion %d, blend-in %.2f s)"), *Dive->GetName(),
				Dive->HasRootMotion() ? 1 : 0, ProneDiveBlendInSeconds);
			return;
		}
	}
	// Sniper rifle in hands, standing still (user request 2026-10-09): the pack's kneel / prone / rise clips.
	if (UAnimSequenceBase* SniperClip = PickSniperTransition(From, Stance))
	{
		StopSlotAnimation(StanceTransitionBlendTime, UpperBodySlot);
		SniperLoopMontage.Reset();
		SniperLoopClip.Reset();
		SniperOneShotMontage.Reset();
		bSniperBoltPlaying = false;
		StanceTransitionMontage = PlayCoverMontage(SniperClip, FAlphaBlendArgs(StanceTransitionBlendTime), StanceTransitionBlendTime);
		bSniperTransitionPlaying = StanceTransitionMontage.IsValid();
		SniperClipLog.Add(SniperClip->GetName());
		UE_LOG(LogCodexTactics, Display, TEXT("[SniperAnim] stance %d -> %d: %s"), static_cast<int32>(From), static_cast<int32>(Stance), *SniperClip->GetName());
		return;
	}
	UAnimSequenceBase* Clip = nullptr;
	switch (Stance)
	{
	case EOperativeStance::Prone:
		Clip = From == EOperativeStance::Crouching ? CrouchToProneAnimation.Get() : StandToProneAnimation.Get();
		break;
	case EOperativeStance::Crouching:
		Clip = From == EOperativeStance::Prone ? ProneToCrouchAnimation.Get() : StandToCrouchAnimation.Get();
		break;
	default:
		Clip = From == EOperativeStance::Prone ? ProneToStandAnimation.Get() : CrouchToStandAnimation.Get();
		break;
	}
	if (!Clip)
	{
		// No clip: the graph's crossfade; drop a transition still playing so it does not hold the old stance.
		if (IsPlayingStanceTransition())
		{
			StopSlotAnimation(StanceTransitionBlendTime, FullBodySlot);
		}
		return;
	}
	StopSlotAnimation(StanceTransitionBlendTime, UpperBodySlot);
	StanceTransitionMontage = PlaySlotAnimationAsDynamicMontage(Clip, FullBodySlot, StanceTransitionBlendTime,
		StanceTransitionBlendTime, StanceTransitionPlayRate);
}

void UOperativeAnimInstance::HandleHealthChanged(float NewHealth, float MaxHealth, float Delta)
{
	// Godot play_hit_reaction: per stance, the pistol one with the pistol in hands; not over a throw or while dead.
	// User request 2026-10-08: real time and turn-based (grid hits go through TakeHit too), throttled, not in cover.
	const AOperativeCharacter* Operative = BoundOperative.Get();
	if (Delta >= 0.f || NewHealth <= 0.f || !Operative || bIsDead)
	{
		return;
	}
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	HitReactionRules::FHitReactionSettings Settings;
	Settings.MinIntervalSeconds = HitReactionMinIntervalSeconds;
	Settings.MinDamage = HitReactionMinDamage;
	Settings.bAllowInCover = bHitReactionInCover;
	HitReactionRules::FHitReactionContext Context;
	Context.Damage = -Delta;
	Context.SecondsSinceLast = static_cast<float>(Now - LastHitReactionTime);
	Context.bInCover = Operative->bInCover;
	// Knocked down: the knockdown clip owns the body; a throw / reload / stance change keeps its clip.
	Context.bBusy = Operative->IsKnockedDown() || IsThrowingGrenade() || Operative->bIsReloading || IsPlayingStanceTransition() || Operative->IsVaulting()
		|| Operative->IsClimbingLadder(); // on a ladder: damage applies, no hit clip (LADDERS.md 7.7)
	if (!HitReactionRules::ShouldReact(Settings, Context))
	{
		return;
	}
	if (bSniperPose && PlaySniperHitReaction(Operative->GetStance()))
	{
		LastHitReactionTime = Now;
		++HitReactionsPlayed;
		LastHitReactionSlot = FullBodySlot;
		return;
	}
	FVector Source;
	const bool bFromBehind = Operative->GetLastHitSource(Source)
		&& HitReactionRules::IsFromBehind(Operative->GetActorForwardVector(), Operative->GetActorLocation(), Source);
	UAnimSequenceBase* Clip = HitStandAnimation;
	FName Slot = UpperBodySlot;
	if (Operative->GetStance() == EOperativeStance::Prone)
	{
		// The prone body reacts as a whole (Godot hit_prone).
		Clip = HitProneAnimation;
		Slot = FullBodySlot;
	}
	else if (UAnimSequenceBase* LauncherHit = PickLauncherHitClip())
	{
		Clip = LauncherHit; // the RPG-7 in his hands: the launcher pack's Get_Hit (upper body, the tube stays shouldered)
	}
	else if (bFromBehind && HitBackAnimation)
	{
		Clip = HitBackAnimation;
	}
	else if (Operative->CurrentWeapon && Operative->CurrentWeapon->WeaponId == TEXT("pistol") && PistolHitAnimation)
	{
		Clip = PistolHitAnimation;
	}
	else if (Operative->GetStance() == EOperativeStance::Crouching && HitCrouchAnimation)
	{
		Clip = HitCrouchAnimation;
	}
	if (!Clip)
	{
		return;
	}
	// Upper body ("Fire" slot, the graph's layered blend from spine_01): the legs keep the locomotion.
	PlaySlotAnimationAsDynamicMontage(Clip, Slot, HitReactionBlendInSeconds, HitReactionBlendOutSeconds);
	LeftHandIKBlockSeconds = FMath::Max(LeftHandIKBlockSeconds, Clip->GetPlayLength()); // the arms react: no IK / aim offset
	LastHitReactionTime = Now;
	++HitReactionsPlayed;
	LastHitReactionClip = Clip;
	LastHitReactionSlot = Slot;
	UE_LOG(LogCodexTactics, Verbose, TEXT("[HitReaction] %s: %s on %s (-%.0f)"), *Operative->DisplayName.ToString(), *Clip->GetName(), *Slot.ToString(), -Delta);
}

void UOperativeAnimInstance::HandleGrenadeThrow()
{
	// Godot play_grenade_throw: prone / crouch / run / walk by the stance and speed. Godot released the grenade at 70 % of
	// the clip (get_grenade_throw_duration); the measured per-clip release time replaces that (GrenadeThrow*ReleaseSeconds).
	AOperativeCharacter* Operative = BoundOperative.Get();
	if (!Operative || Operative->IsKnockedDown() || Operative->IsClimbingLadder())
	{
		return;
	}
	const EOperativeStance ThrowStance = Operative->GetStance();
	const bool bRunning = ThrowStance == EOperativeStance::Standing && (Operative->IsSprinting() || Operative->GetVelocity().Size2D() > 300.f);
	UAnimSequenceBase* Clip = ThrowStance == EOperativeStance::Prone ? GrenadeThrowProneAnimation
		: ThrowStance == EOperativeStance::Crouching ? GrenadeThrowCrouchAnimation
		: bRunning ? GrenadeThrowRunAnimation : GrenadeThrowWalkAnimation;
	const float ReleaseSeconds = ThrowStance == EOperativeStance::Prone ? GrenadeThrowProneReleaseSeconds
		: ThrowStance == EOperativeStance::Crouching ? GrenadeThrowCrouchReleaseSeconds
		: bRunning ? GrenadeThrowRunReleaseSeconds : GrenadeThrowWalkReleaseSeconds;
	if (!Clip)
	{
		return;
	}
	// The grenade leaves the hand at the clip's measured release frame (GrenadeSubsystem reads both fields right after
	// this event); the montage plays on the upper-body slot only, so the legs keep the locomotion.
	Operative->GrenadeThrowDuration = Clip->GetPlayLength();
	Operative->GrenadeReleaseSeconds = ReleaseSeconds;
	PlaySlotAnimationAsDynamicMontage(Clip, UpperBodySlot, GrenadeThrowBlendInSeconds, GrenadeThrowBlendOutSeconds);
	LeftHandIKBlockSeconds = FMath::Max(LeftHandIKBlockSeconds, Clip->GetPlayLength()); // the left hand throws
	GrenadeThrowSecondsLeft = Clip->GetPlayLength() - GrenadeThrowBlendOutSeconds;
}

void UOperativeAnimInstance::HandleDied(AActor* Victim, const FString& AttackerSource)
{
	// Godot play_death: a random standing variation, or the crouched / prone death; full body, held at the end.
	const AOperativeCharacter* Operative = BoundOperative.Get();
	// fix/corpse-falls-to-ground (game designer PIE 2026-11-08: a fighter killed on a ladder lay in his death pose in mid-air): killed off the
	// ground he falls first - the death clip starts once he has landed (UpdateDeathPose's caller below).
	if (!bDeathPlayed && !bDeathLanded && Operative && (Operative->IsCorpseFalling() || Operative->ShouldCorpseFallNow()))
	{
		bDeathDeferred = true;
		return;
	}
	// Sprint 14: killed while knocked down -> Death_Back / Death_Front from the ground (UKnockdownComponent), held.
	if (UKnockdownComponent* Knockdown = Operative ? Operative->KnockdownComponent.Get() : nullptr; !bDeathPlayed && Knockdown && Knockdown->HandleDeath())
	{
		bDeathPlayed = true;
		DeathPoseClip = Knockdown->GetPlayingClip(); // the death from the ground is the corpse pose (UpdateDeathPose)
		StopSlotAnimation(0.1f, UpperBodySlot);
		return;
	}
	if (bDeathPlayed || !Operative)
	{
		return;
	}
	bDeathPlayed = true;
	UAnimSequenceBase* Clip = PickDeathClip(*Operative);
	DeathPoseClip = Clip; // null: the knockdown fall below (UpdateDeathPose adopts it)
	StopSlotAnimation(0.1f, UpperBodySlot);
	if (!Clip)
	{
		// User bug 2026-10-08 (Ivan Susanin kept standing after his death): ABP_Operative has no standing / crouched death
		// clip (DeathStandAnimations empty, DeathCrouchAnimation unset — the pack's MM_Death_* clips stop half-way down,
		// pelvis 83-88 cm, no corpse pose). He falls with the knockdown fall of the side the killing blow came from and
		// lies there (held).
		FVector Source = Operative->GetActorLocation() + Operative->GetActorForwardVector() * 100.f;
		Operative->GetLastHitSource(Source);
		if (UKnockdownComponent* Knockdown = Operative->KnockdownComponent.Get())
		{
			Knockdown->PlayDeathFall(Source);
		}
		return;
	}
	const bool bLauncherDeath = LauncherSetData.Deaths.Contains(Clip); // the launcher's falls play from their first frame
	if (UAnimMontage* Montage = PlaySlotAnimationAsDynamicMontage(Clip, FullBodySlot, 0.15f, 0.f, 1.f, 1, -1.f,
		Stance == EOperativeStance::Standing && !bLauncherDeath ? FMath::Min(DeathStartOffset, Clip->GetPlayLength() * 0.9f) : 0.f))
	{
		// Held on the last frame (the running instance's flag; the montage asset's is copied only at the start).
		if (FAnimMontageInstance* Instance = GetActiveInstanceForMontage(Montage))
		{
			Instance->bEnableAutoBlendOut = false;
		}
	}
}

bool UOperativeAnimInstance::IsDeathPoseSettled() const
{
	if (!bDeathPlayed || bDeathDeferred)
	{
		return false;
	}
	for (const FAnimMontageInstance* Instance : MontageInstances)
	{
		// The held death clip / fall stops advancing at its end (no auto blend-out); anything still playing or blending is not settled.
		if (Instance && Instance->IsValid() && (Instance->IsPlaying() || !Instance->GetBlend().IsComplete()))
		{
			return false;
		}
	}
	return true;
}

UAnimSequenceBase* UOperativeAnimInstance::PickDeathClip(const AOperativeCharacter& Operative) const
{
	if (UAnimSequenceBase* LauncherDeath = PickLauncherDeathClip(Operative))
	{
		return LauncherDeath; // the RPG-7 standing: the launcher pack's Dead_01 / Dead_02 (they end lying, unlike the rifle pack's)
	}
	TArray<UAnimSequenceBase*> StandClips;
	for (const TObjectPtr<UAnimSequenceBase>& Stand : DeathStandAnimations)
	{
		if (Stand)
		{
			StandClips.Add(Stand.Get());
		}
	}
	return Operative.GetStance() == EOperativeStance::Crouching ? DeathCrouchAnimation.Get()
		: Operative.GetStance() == EOperativeStance::Prone ? DeathProneAnimation.Get()
		: StandClips.IsEmpty() ? nullptr : StandClips[FMath::RandRange(0, StandClips.Num() - 1)];
}

void UOperativeAnimInstance::UpdateDeathPose(const AOperativeCharacter& Operative, float DeltaSeconds)
{
	if (!bIsDead)
	{
		DeathPoseClip.Reset();
		DeathPoseSeconds = 0.f;
		return;
	}
	if (bDeathDeferred)
	{
		return; // still falling (HandleDied waits for the landing)
	}
	DeathPoseSeconds += DeltaSeconds;
	// The clip on the FullBody slot now: the death clip, the knockdown's held fall - or something that replaced it.
	const FAnimMontageInstance* Holding = nullptr;
	for (const FAnimMontageInstance* Instance : MontageInstances)
	{
		if (Instance && Instance->Montage && Instance->IsActive() && !Instance->IsStopped() && Instance->Montage->IsValidSlot(FullBodySlot))
		{
			Holding = Instance;
		}
	}
	const UAnimMontage* HoldingMontage = Holding ? Holding->Montage.Get() : nullptr;
	UAnimSequenceBase* HoldingClip = !HoldingMontage || HoldingMontage->SlotAnimTracks.IsEmpty() || HoldingMontage->SlotAnimTracks[0].AnimTrack.AnimSegments.IsEmpty()
		? nullptr : HoldingMontage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference().Get();
	// The corpse pose: the clip HandleDied played (DeathPoseClip), else the knockdown's held fall (a save restored him dead).
	UKnockdownComponent* Knockdown = Operative.KnockdownComponent.Get();
	if (!DeathPoseClip.IsValid() && Knockdown && Knockdown->PlayedDeathFall() && Knockdown->GetPlayingClip())
	{
		DeathPoseClip = Knockdown->GetPlayingClip();
	}
	UAnimSequenceBase* Clip = DeathPoseClip.Get();
	if (!Clip)
	{
		if (DeathPoseSeconds < 1.f)
		{
			return; // HandleDied has not run yet (or the fallback below was tried)
		}
		Clip = PickDeathClip(Operative); // no death clip ever started: fall now
		if (!Clip)
		{
			if (Knockdown && !Knockdown->PlayedDeathFall())
			{
				FVector Source = Operative.GetActorLocation() + Operative.GetActorForwardVector() * 100.f;
				Operative.GetLastHitSource(Source);
				Knockdown->PlayDeathFall(Source);
			}
			DeathPoseSeconds = -1000.f; // tried once
			return;
		}
	}
	else if (HoldingClip == Clip || DeathPoseSeconds < 0.3f)
	{
		return; // still lying there (or the death clip still blending in)
	}
	// Lost (a cover / stance / interaction clip took the slot): the death clip again from its last frame, held.
	if (++DeathPoseRestores <= 3)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[Death] %s: the corpse pose was lost (%s on the FullBody slot) - %s held again"), *Operative.DisplayName.ToString(),
			HoldingClip ? *HoldingClip->GetName() : TEXT("nothing"), *Clip->GetName());
	}
	DeathPoseClip = Clip;
	if (UAnimMontage* Montage = PlaySlotAnimationAsDynamicMontage(Clip, FullBodySlot, 0.2f, 0.f, 1.f, 1, -1.f, FMath::Max(0.f, Clip->GetPlayLength() - 0.05f)))
	{
		if (FAnimMontageInstance* Instance = GetActiveInstanceForMontage(Montage))
		{
			Instance->bEnableAutoBlendOut = false;
		}
	}
}

void UOperativeAnimInstance::PlayWorkingDevice(float Seconds)
{
	if (WorkingDeviceAnimation && !bIsDead)
	{
		PlaySlotAnimationAsDynamicMontage(WorkingDeviceAnimation, UpperBodySlot, 0.15f, 0.2f,
			WorkingDeviceAnimation->GetPlayLength() / FMath::Max(Seconds, 0.1f));
	}
}

void UOperativeAnimInstance::UpdateUpperBody(float DeltaSeconds)
{
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(TryGetPawnOwner());
	AimTimer = FMath::Max(0.f, AimTimer - DeltaSeconds);
	bool bAttackMode = false;
	if (Operative && Operative->GetWorld())
	{
		const UTurnBasedCombatSubsystem* TurnBased = Operative->GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>();
		bAttackMode = TurnBased && TurnBased->IsActive() && TurnBased->IsAttackMode() && TurnBased->GetActiveUnit() == Operative;
	}
	// Godot: a sprinting operative does not shoot (and runs with the rifle down) — no aim pose while sprinting.
	if (bIsSprinting)
	{
		AimTimer = 0.f;
	}
	bIsAiming = !bIsDead && !bKnockedDown && !bIsSprinting && (bAttackMode || AimTimer > 0.f);

	// Blend-space axes (user report 2026-10-01: legs and arms trembled while slowing down). The speed is smoothed and
	// has a dead zone with hysteresis (formation followers stop / restart and creep at 10-25 cm/s, and the raw speed
	// flicked the blend space between the idle and the walk); slower than the walk samples the pose stays the walk and
	// the clip slows down instead of mixing in the idle; above the samples' range the clip speeds up (no foot sliding).
	LocomotionSpeed = FMath::FInterpTo(LocomotionSpeed, Speed, DeltaSeconds, 8.f);
	bLocomotionMoving = bLocomotionMoving ? LocomotionSpeed > LocomotionStopSpeed : LocomotionSpeed > LocomotionStartSpeed;
	auto Axis = [this](float WalkSampleSpeed, float MaxSpeed, float& OutBlend, float& OutRate)
	{
		if (!bLocomotionMoving)
		{
			OutBlend = 0.f;
			OutRate = 1.f;
		}
		else if (LocomotionSpeed < WalkSampleSpeed)
		{
			OutBlend = WalkSampleSpeed;
			OutRate = FMath::Max(MinWalkPlayRate, LocomotionSpeed / WalkSampleSpeed);
		}
		else
		{
			OutBlend = FMath::Min(LocomotionSpeed, MaxSpeed);
			OutRate = FMath::Max(1.f, LocomotionSpeed / MaxSpeed);
		}
	};
	Axis(FMath::Min(WalkSampleSpeed, StandBlendSpaceMaxSpeed), StandBlendSpaceMaxSpeed, StandBlendSpeed, StandPlayRate);
	Axis(FMath::Min(WalkSampleSpeed, SlowBlendSpaceMaxSpeed), SlowBlendSpaceMaxSpeed, SlowBlendSpeed, SlowPlayRate);
	Axis(ProneBlendSpaceMaxSpeed, ProneBlendSpaceMaxSpeed, ProneBlendSpeed, PronePlayRate);

	// Reload: one clip per reload, stretched to the reload time left.
	// Prone: its own full-body clip (Godot ProneReload) or nothing.
	UAnimSequenceBase* Reload = bIsProne ? ReloadProneAnimation.Get() : ReloadAnimation.Get();
	const FName ReloadSlot = bIsProne ? FullBodySlot : UpperBodySlot;
	// Sniper pose: the stance's magazine reload on the full body (it runs out with the reload time on its own).
	if (bIsReloading && !bWasReloading && Operative && bSniperPose && StartSniperReload(*Operative))
	{
		bWasReloading = true;
		Reload = nullptr;
	}
	else if (bSniperPose)
	{
		Reload = nullptr; // never stop the sniper clips at the reload's end
	}
	if (UpdateLauncherAction(BoundOperative.Get(), DeltaSeconds))
	{
		bWasReloading = bIsReloading; // the RPG-7: the pack's reload natively on the upper body, fitted to the reload time (OperativeAnimInstanceShotgun.cpp)
		return;
	}
	if (UpdateShotgunReload(Operative, DeltaSeconds))
	{
		bWasReloading = bIsReloading; // the D-1: intro, a shell-insertion loop per shell, outro (OperativeAnimInstanceShotgun.cpp)
		return;
	}
	if (bIsReloading && !bWasReloading && Reload && Operative && !IsPlayingStanceTransition() && !bKnockedDown && !bIsClimbing)
	{
		const float Duration = FMath::Max(0.1f, Operative->ReloadTimer);
		PlaySlotAnimationAsDynamicMontage(Reload, ReloadSlot, 0.2f, 0.2f, Reload->GetPlayLength() / Duration);
	}
	else if (!bIsReloading && bWasReloading && Reload)
	{
		StopSlotAnimation(0.2f, ReloadSlot);
	}
	bWasReloading = bIsReloading;
}

FName UOperativeAnimInstance::GetFallStateName() const
{
	const int32 Machine = GetStateMachineIndex(TEXT("FallStates"));
	return Machine == INDEX_NONE ? NAME_None : GetCurrentStateName(Machine);
}

void UOperativeAnimInstance::UpdateState()
{
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(TryGetPawnOwner());
	if (!Operative)
	{
		return;
	}
	const FVector Velocity = Operative->GetVelocity();
	Speed = Velocity.Size2D();
	// The blend-space direction follows the movement smoothly and is held below 30 cm/s: braking at the goal made the
	// raw velocity direction flip and the legs jump between the forward / side / back clips.
	if (Speed > 30.f)
	{
		const float RawDirection = FRotator::NormalizeAxis(Velocity.Rotation().Yaw - Operative->GetActorRotation().Yaw);
		const float Alpha = 1.f - FMath::Exp(-12.f * StateDeltaSeconds);
		Direction = FRotator::NormalizeAxis(Direction + FRotator::NormalizeAxis(RawDirection - Direction) * Alpha);
	}
	// Turn-based steps move the actor directly (no velocity): walk forward over the whole path.
	const UTurnBasedCombatSubsystem* TurnBased = Operative->GetWorld() ? Operative->GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	if (const float TacticalSpeed = TurnBased ? TurnBased->GetTacticalMoveSpeed(Operative) : -1.f; TacticalSpeed >= 0.f)
	{
		Speed = TacticalSpeed;
		Direction = 0.f;
	}
	// The fall states (2026-11-03): ULedgeFallComponent decides the phase.
	const FallRules::EFallAnimPhase FallPhase = Operative->LedgeFall ? Operative->LedgeFall->GetAnimPhase() : FallRules::EFallAnimPhase::Grounded;
	bFallAirborne = FallPhase == FallRules::EFallAnimPhase::Falling;
	bFallLanding = FallPhase == FallRules::EFallAnimPhase::Landing;
	bFallGrounded = FallPhase == FallRules::EFallAnimPhase::Grounded;
	// A vault moves the actor directly too (Godot "Vault" state; the graph may play its own clip on bIsVaulting).
	bIsVaulting = Operative->IsVaulting();
	if (bIsVaulting)
	{
		Speed = Operative->GetVaultSpeed();
		Direction = 0.f;
	}
	UpdateVaultClip(*Operative);
	// On a ladder the climb moves the actor directly and owns the full body (LADDERS.md 6.1).
	bIsClimbing = Operative->IsClimbingLadder();
	LadderIKFrame = bIsClimbing && Operative->LadderClimb ? Operative->LadderClimb->GetIKFrame() : FLadderIKFrame();
	if (bIsClimbing)
	{
		// Standing / turning on the ladder head (the turn-in-place clips follow the actor's yaw), or walking up to it.
		Speed = Operative->LadderClimb ? Operative->LadderClimb->GetAlignWalkSpeed() : 0.f;
		Direction = 0.f;
	}
	// Sprint 12 cover state (the shimmy moves sideways: the blend-space direction already strafes).
	bInCover = Operative->bInCover;
	CoverHeight = Operative->CurrentCoverHeight;
	CoverFacing = Operative->CoverFacing;
	bShimmying = Operative->bShimmying;
	ShimmyDirection = Operative->ShimmyDirection;
	bLeaning = Operative->bIsCornerLeaning;
	bBlindFiring = Operative->bIsBlindFiring;
	bCoverShimmyForward = bShimmying && Operative->IsShimmyForward();
	bCoverAtCorner = bInCover && Operative->bAtCoverCorner;
	bCoverFireReady = bInCover && Operative->IsCoverFireReady();
	bCoverCornerAim = bInCover && Operative->IsCornerAimActive();
	// The stance first (user request 2026-10-07): entering a crouched cover from a standing run picked the STANDING
	// enter clip because the cover layer still saw last frame's stance.
	Stance = Operative->GetStance();
	ProneDive = Operative->GetProneDive();
	bIsCrouching = Stance == EOperativeStance::Crouching;
	bIsProne = Stance == EOperativeStance::Prone;
	bIsReloading = Operative->bIsReloading;
	bKnockedDown = Operative->IsKnockedDown();
	// Bug #44 (2026-11-07, "a killed PMC keeps standing in the aim pose"): the death is known BEFORE the cover layer runs. It used to read
	// last frame's bIsDead: killed in the corner fire stance, HandleDied left the cover, the next update saw "left the cover, alive, in the
	// fire pose" and played the fire -> idle exit clip (PlayCoverMontage fades out every FullBody montage - the death clip too), so the
	// corpse blended back to the standing aim pose. UpdateDeathPose holds the corpse pose against anything else that stops it.
	bIsDead = Operative->HealthComponent && !Operative->HealthComponent->IsAlive();
	if (bDeathDeferred && bIsDead && Operative->IsKilledInAction() && !Operative->IsCorpseFalling())
	{
		bDeathDeferred = false; // landed: the death clip now
		TGuardValue<bool> Landed(bDeathLanded, true);
		HandleDied(nullptr, FString());
	}
	UpdateCoverLayer(*Operative);
	UpdateDeathPose(*Operative, StateDeltaSeconds);
	bIsMoving = Speed > 5.f;
	bIsSprinting = Operative->IsSprinting();
	if (const UColdSurvivalComponent* Cold = Operative->ColdSurvival)
	{
		ColdTier = Cold->GetTier();
		bIsFrostbitten = Cold->IsFrostbitten();
		bIsWeaponFrozen = Cold->IsWeaponFrozen();
	}
	UpdateInteractionLayer(*Operative); // before the weather / sniper layers: an interaction clip keeps both off
	UpdateBlizzardWalkLayer(*Operative); // before the weather idle: it applies the storm pose's weapon hiding too
	UpdateWarmIdleLayer(*Operative); // before the sniper layer: a weather pose keeps the sniper idle off
	UpdateSniperLayer(*Operative);
	UpdateLeftHandIK(*Operative, StateDeltaSeconds);
	UpdateAimOffset(*Operative, StateDeltaSeconds);
}

void UOperativeAnimInstance::UpdateLeftHandIK(const AOperativeCharacter& Operative, float DeltaSeconds)
{
	// The grip in hand_r bone space: the socket on the weapon mesh, composed with the weapon's transform relative to its
	// attach parent (hand_r). Recomputed every update: a weapon switch / a new mesh / offset shows at once.
	const UStaticMeshComponent* Weapon = Operative.WeaponMesh;
	const UStaticMesh* WeaponAsset = Weapon ? Weapon->GetStaticMesh() : nullptr;
	const UStaticMeshSocket* Grip = WeaponAsset ? WeaponAsset->FindSocket(LeftHandGripSocket) : nullptr;
	bLeftHandIKGripValid = Grip && Weapon->GetAttachSocketName() == Operative.WeaponSocket;
	const USkeletalMeshComponent* Body = GetSkelMeshComponent();
	FTransform HandR = FTransform::Identity;
	FVector Shoulder = FVector::ZeroVector;
	LeftHandIKSlideCm = 0.f;
	LeftHandIKExcessCm = 0.f;
	if (bLeftHandIKGripValid)
	{
		const FTransform SocketInWeapon(Grip->RelativeRotation, Grip->RelativeLocation, Grip->RelativeScale);
		const FTransform InHand = LeftHandIKRules::GripInHandSpace(Weapon->GetRelativeTransform(), SocketInWeapon);
		LeftHandIKOffset = InHand.GetLocation();
		LeftHandIKRotation = InHand.Rotator();
		// The arm's reach (upper + lower arm of the reference skeleton) and the shoulder in hand_r space from the last
		// pose: a grip beyond reach slides back along the barrel to the furthest reachable point (user report
		// 2026-10-07: the m16 handguard is 72-74 cm from the shoulder on the pack's fire stances, the arm reaches 57).
		if (Body && Body->GetSkeletalMeshAsset())
		{
			if (LeftHandArmReach <= 0.f || LeftHandIKChainRoot != LeftHandArmReachRoot)
			{
				// The chain's length from LeftHandIKChainRoot down to hand_l (Two Bone IK: upperarm_l; a FABRIK from
				// clavicle_l reaches further).
				LeftHandArmReachRoot = LeftHandIKChainRoot;
				LeftHandArmReach = 0.f;
				const FReferenceSkeleton& Ref = Body->GetSkeletalMeshAsset()->GetRefSkeleton();
				const int32 Root = Ref.FindBoneIndex(LeftHandIKChainRoot);
				for (int32 Bone = Ref.FindBoneIndex(TEXT("hand_l")); Bone != INDEX_NONE && Bone != Root && Root != INDEX_NONE; Bone = Ref.GetParentIndex(Bone))
				{
					LeftHandArmReach += static_cast<float>(Ref.GetRefBonePose()[Bone].GetLocation().Size());
				}
			}
			HandR = Body->GetSocketTransform(TEXT("hand_r"), RTS_Component);
			// Robust to how the weapon is attached (a skeletal socket with its own offset, a weapon bone...): the grip's
			// world location taken into the hand_r BONE space the IK node uses (Effector Target = hand_r, Bone Space).
			const FTransform GripWorld = Weapon->GetSocketTransform(LeftHandGripSocket, RTS_World);
			const FTransform GripInHand = GripWorld.GetRelativeTransform(Body->GetComponentTransform()).GetRelativeTransform(HandR);
			LeftHandIKOffset = GripInHand.GetLocation();
			LeftHandIKRotation = GripInHand.Rotator();
			Shoulder = HandR.InverseTransformPosition(Body->GetSocketTransform(LeftHandIKChainRoot, RTS_Component).GetLocation());
			if (LeftHandArmReach > 0.f)
			{
				const FVector Axis = Weapon->GetRelativeTransform().TransformVectorNoScale(Operative.MuzzleOffset.GetSafeNormal());
				const FVector Reachable = LeftHandIKRules::SlideIntoReach(LeftHandIKOffset, Axis, Shoulder,
					LeftHandArmReach * LeftHandIKReachFraction, LeftHandIKMaxSlideCm);
				LeftHandIKSlideCm = static_cast<float>(FVector::Dist(Reachable, LeftHandIKOffset));
				LeftHandIKOffset = Reachable;
				LeftHandIKExcessCm = FMath::Max(0.f, static_cast<float>(FVector::Dist(Reachable, Shoulder)) - LeftHandArmReach * LeftHandIKReachFraction);
			}
		}
	}
	LeftHandIKBlockSeconds = FMath::Max(0.f, LeftHandIKBlockSeconds - DeltaSeconds);
	GrenadeThrowSecondsLeft = FMath::Max(0.f, GrenadeThrowSecondsLeft - DeltaSeconds);
	FLeftHandIKState State;
	State.bEnabled = bLeftHandIK;
	State.bHasGrip = bLeftHandIKGripValid && Operative.UsesAmmo(); // a melee weapon has no handguard
	State.bWeaponVisible = Weapon && Weapon->IsVisible();
	State.bReloading = bIsReloading || bSniperBoltPlaying; // the bolt hand leaves the rifle
	// A grenade throw / hit reaction plays on the upper body: it frees the left hand only while it actually shows — under a
	// full-body cover clip it is invisible (user PIE 2026-10-07: every hound bite blocked the IK for the hit clip's length
	// while the cover fire stance kept the rifle up, so the left hand let go of it).
	// The blizzard walk's storm pose holds both hands in front of the face (game designer 2026-10-22).
	State.bUpperBodyAction = (LeftHandIKBlockSeconds > 0.f || bBlizzardWalkActive) && GetSlotMontageGlobalWeight(FullBodySlot) < 0.5f;
	State.bVaulting = bIsVaulting || bIsClimbing; // a ladder frees both hands (LADDERS.md 6.1)
	State.bDead = bIsDead || bKnockedDown; // Sprint 14: no IK / aim offset while knocked down
	State.bProne = bIsProne && !bSniperPose; // the prone sniper aim holds the rifle with both hands (crawl clips do not)
	// Every pose where the right hand holds the rifle keeps the IK (cover idle / look-around / shimmy / enter / switches
	// included, 2026-10-07); only clips listed in LeftHandIKFreeClips free the left hand (none measured so far).
	{
		FString Playing;
		float MontageWeight = 0.f;
		float SlotWeight = 0.f;
		GetCoverPlayback(Playing, MontageWeight, SlotWeight);
		State.bTwoHandedPose = !bWarmIdleActive && !(MontageWeight > 0.5f && LeftHandIKFreeClips.ContainsByPredicate([&Playing](const FString& Part) { return !Part.IsEmpty() && Playing.Contains(Part); }));
	}
	LeftHandIKLinear = LeftHandIKRules::StepAlpha(LeftHandIKLinear, LeftHandIKRules::WantsIK(State), DeltaSeconds, LeftHandIKBlendSeconds);
	// Still out of reach after the slide (a rifle hanging low): fade by the excess instead of a straight arm.
	LeftHandIKAlpha = FMath::SmoothStep(0.f, 1.f, LeftHandIKLinear) * LeftHandIKRules::ReachFade(LeftHandIKExcessCm, LeftHandIKFadeCm);
	if (LeftHandIKAlphaOverrideForTesting >= 0.f)
	{
		LeftHandIKAlpha = FMath::Clamp(LeftHandIKAlphaOverrideForTesting, 0.f, 1.f);
	}
	// Codex.Debug.LeftHandIK 1: red = the LeftHandGrip socket, green = the IK target (after the reach slide), yellow = the
	// left hand now, blue = the shoulder (upperarm_l) with the reach as a circle radius in the log line.
	if (CVarDebugLeftHandIK.GetValueOnGameThread() > 0 && Body && Weapon && Operative.GetWorld())
	{
		UWorld* World = Operative.GetWorld();
		const FTransform BodyXf = Body->GetComponentTransform();
		const FVector TargetWorld = BodyXf.TransformPosition(HandR.TransformPosition(LeftHandIKOffset));
		if (bLeftHandIKGripValid)
		{
			DrawDebugSphere(World, Weapon->GetSocketLocation(LeftHandGripSocket), 3.f, 8, FColor::Red, false, -1.f, SDPG_Foreground);
			DrawDebugSphere(World, TargetWorld, 3.5f, 8, FColor::Green, false, -1.f, SDPG_Foreground);
		}
		DrawDebugSphere(World, Body->GetBoneLocation(TEXT("hand_l")), 2.5f, 8, FColor::Yellow, false, -1.f, SDPG_Foreground);
		DrawDebugSphere(World, Body->GetBoneLocation(LeftHandIKChainRoot), 2.5f, 8, FColor::Blue, false, -1.f, SDPG_Foreground);
		DrawDebugString(World, TargetWorld + FVector(0.f, 0.f, 12.f), FString::Printf(TEXT("IK a=%.2f slide %.0f reach %.0f"),
			LeftHandIKAlpha, LeftHandIKSlideCm, LeftHandArmReach), nullptr, FColor::Green, 0.f, true, 1.f);
	}
}

void UOperativeAnimInstance::UpdateAimOffset(const AOperativeCharacter& Operative, float DeltaSeconds)
{
	// The pitch from the stance's muzzle height (stable: it does not move with the pose the offset itself bends) to the
	// point the tracer flies to (the target's capsule centre).
	FVector AimPoint = FVector::ZeroVector;
	const UTurnBasedCombatSubsystem* TurnBasedMode = Operative.GetWorld() ? Operative.GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	bAimOffsetTurnBasedOff = !bAimOffsetInTurnBased && TurnBasedMode && TurnBasedMode->IsActive();
	bHasAimTarget = bAimOffset && !bAimOffsetTurnBasedOff && Operative.GetAimTargetPoint(AimPoint);
	AimPitchTarget = bHasAimTarget ? AimOffsetRules::PitchToTarget(Operative.GetMuzzleLocation(), AimPoint, AimPitchClampDegrees) : 0.f;
	AimPitch = FMath::Clamp(FMath::FInterpTo(AimPitch, AimPitchTarget, DeltaSeconds, AimPitchInterpSpeed), -AimPitchClampDegrees, AimPitchClampDegrees);
	if (!bHasAimTarget && FMath::Abs(AimPitch) < 0.05f)
	{
		AimPitch = 0.f;
	}
	// The yaw twist: only in an aiming pose (out of cover, or the corner fire stance) - the plain cover idle does not twist.
	const bool bYawPose = !bInCover || Operative.IsCornerAimActive();
	// No twist while walking a turn-based grid path (the body turns cell by cell along it; user PIE video
	// AnimOffset_Bug_01 2026-10-08) - it fades back in when he stands. The reference is the pose's own aim direction, the
	// AO excluded (AOperativeCharacter::GetStanceAimBaseDirection), so the twist never feeds back into its own input.
	const bool bGridWalk = TurnBasedMode && TurnBasedMode->IsActive() && Operative.GetVelocity().SizeSquared2D() > 20.f * 20.f;
	const float RawYaw = bHasAimTarget && bYawPose && !bGridWalk
		? AimOffsetRules::YawToTarget(Operative.GetStanceAimOrigin(), Operative.GetStanceAimBaseDirection(), AimPoint, 180.f) : 0.f;
	AimYawTarget = bHasAimTarget && bYawPose && !bGridWalk
		? AimOffsetRules::YawTargetWithinReach(RawYaw, AimYawClampDegrees, AimYawClampDegrees + AimYawReachMarginDegrees, 15.f, bAimYawOutOfReach) : 0.f;
	AimYaw = FMath::Clamp(FMath::FInterpTo(AimYaw, AimYawTarget, DeltaSeconds, AimYawInterpSpeed), -AimYawClampDegrees, AimYawClampDegrees);
	if (AimYawTarget == 0.f && FMath::Abs(AimYaw) < 0.05f)
	{
		AimYaw = 0.f;
	}
	FAimOffsetState State;
	// Sniper poses: only with the sniper AO wired into the graph (ActiveAimOffset); the rifle AO would bend them wrongly.
	State.bEnabled = bAimOffset && (bAimOffsetInCover || !bInCover) && (!bSniperPose || bSniperAimOffsetWired);
	State.bRangedWeapon = Operative.UsesAmmo();
	State.bWeaponVisible = Operative.WeaponMesh && Operative.WeaponMesh->IsVisible();
	State.bReloading = bIsReloading;
	// A grenade throw / hit reaction plays on the upper body: it frees the left hand only while it actually shows — under a
	// full-body cover clip it is invisible (user PIE 2026-10-07: every hound bite blocked the IK for the hit clip's length
	// while the cover fire stance kept the rifle up, so the left hand let go of it).
	State.bUpperBodyAction = (LeftHandIKBlockSeconds > 0.f || bBlizzardWalkActive) && GetSlotMontageGlobalWeight(FullBodySlot) < 0.5f; // grenade throw / hit reaction windows (shared with the left-hand IK), the storm pose
	State.bVaulting = bIsVaulting || bIsClimbing;
	State.bSprinting = bIsSprinting;
	State.bProne = bIsProne;
	State.bTurnBasedSuppressed = bAimOffsetTurnBasedOff;
	State.bDead = bIsDead || bKnockedDown; // Sprint 14: no IK / aim offset while knocked down
	AimOffsetLinear = AimOffsetRules::StepAlpha(AimOffsetLinear, AimOffsetRules::WantsAimOffset(State), DeltaSeconds, AimOffsetBlendSeconds);
	AimOffsetAlpha = FMath::SmoothStep(0.f, 1.f, AimOffsetLinear);
	bAimOffsetActive = AimOffsetAlpha > 0.01f;
}

UAnimSequence* UOperativeAnimInstance::GetClip(EOperativeClip Clip) const
{
	switch (Clip)
	{
	case EOperativeClip::Idle: return Animations.Idle;
	case EOperativeClip::Walk: return Animations.Walk;
	case EOperativeClip::Run: return Animations.Run;
	case EOperativeClip::CrouchIdle: return Animations.CrouchIdle;
	case EOperativeClip::CrouchWalk: return Animations.CrouchWalk;
	case EOperativeClip::ProneIdle: return Animations.ProneIdle;
	case EOperativeClip::ProneCrawl: return Animations.ProneCrawl;
	default: return nullptr;
	}
}

void UOperativeAnimInstance::UpdateNativeBlend(float DeltaSeconds)
{
	const AOperativeCharacter* Operative = Cast<AOperativeCharacter>(TryGetPawnOwner());
	const FOperativeMovementConfig Movement = Operative ? Operative->MovementConfig : FOperativeMovementConfig();

	// Stance crossfade.
	const FVector3f TargetStance(Stance == EOperativeStance::Standing ? 1.f : 0.f, bIsCrouching ? 1.f : 0.f, bIsProne ? 1.f : 0.f);
	const float StanceStep = DeltaSeconds / StanceBlendTime;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		StanceWeights[Axis] = FMath::FInterpConstantTo(StanceWeights[Axis], TargetStance[Axis], 1.f, StanceStep);
	}

	// Idle -> walk -> run by speed, using the gameplay speeds as blend anchors (Godot BlendSpace1D points).
	const float ProneSpeed = Movement.WalkSpeed * Movement.ProneSpeedMultiplier;
	const FVector3f TargetMove(
		Speed <= Movement.WalkSpeed
			? Speed / Movement.WalkSpeed
			: 1.f + FMath::Clamp((Speed - Movement.WalkSpeed) / FMath::Max(Movement.RunSpeed - Movement.WalkSpeed, 1.f), 0.f, 1.f),
		FMath::Clamp(Speed / Movement.CrouchSpeed, 0.f, 1.f),
		FMath::Clamp(Speed / FMath::Max(ProneSpeed, 1.f), 0.f, 1.f));
	const float MoveAlpha = FMath::Clamp(LocomotionBlendSpeed * DeltaSeconds, 0.f, 1.f);
	MoveBlend = FMath::Lerp(MoveBlend, TargetMove, MoveAlpha);

	const float StandWalk = MoveBlend.X <= 1.f ? MoveBlend.X : 2.f - MoveBlend.X;
	const float StandRun = FMath::Max(MoveBlend.X - 1.f, 0.f);
	ClipWeights[Slot(EOperativeClip::Idle)] = StanceWeights.X * FMath::Max(1.f - MoveBlend.X, 0.f);
	ClipWeights[Slot(EOperativeClip::Walk)] = StanceWeights.X * StandWalk;
	ClipWeights[Slot(EOperativeClip::Run)] = StanceWeights.X * StandRun;
	ClipWeights[Slot(EOperativeClip::CrouchIdle)] = StanceWeights.Y * (1.f - MoveBlend.Y);
	ClipWeights[Slot(EOperativeClip::CrouchWalk)] = StanceWeights.Y * MoveBlend.Y;
	ClipWeights[Slot(EOperativeClip::ProneIdle)] = StanceWeights.Z * (1.f - MoveBlend.Z);
	ClipWeights[Slot(EOperativeClip::ProneCrawl)] = StanceWeights.Z * MoveBlend.Z;

	// Advance clip times; moving clips are speed-matched to limit foot sliding.
	auto Rate = [this](float ClipSpeed) { return Speed > 1.f ? FMath::Clamp(Speed / ClipSpeed, MinPlayRate, MaxPlayRate) : 1.f; };
	const float Rates[] = { 1.f, Rate(Animations.WalkClipSpeed), Rate(Animations.RunClipSpeed), 1.f,
		Rate(Animations.CrouchWalkClipSpeed), 1.f, Rate(Animations.CrawlClipSpeed) };
	for (int32 Index = 0; Index < Slot(EOperativeClip::Count); ++Index)
	{
		if (const UAnimSequence* Sequence = GetClip(static_cast<EOperativeClip>(Index)))
		{
			const float Length = Sequence->GetPlayLength();
			ClipTimes[Index] = Length > 0.f ? FMath::Fmod(ClipTimes[Index] + DeltaSeconds * Rates[Index], Length) : 0.f;
		}
	}
}

void FOperativeAnimInstanceProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	const UOperativeAnimInstance* Instance = CastChecked<UOperativeAnimInstance>(InAnimInstance);
	bUseNative = Instance->bUseNativeLocomotion;
	LadderIK = Instance->LadderIKFrame;
	// The peek stance yaw lock for this evaluation (time-based: begun earlier in this frame it is at its start).
	PeekLockAlpha = 0.f;
	if (const UOperativeAnimInstance::FPeekStanceYawLockState& Lock = Instance->PeekStanceYawLock; Lock.bActive)
	{
		const float Seconds = static_cast<float>(Instance->PeekStanceYawLockSeconds());
		PeekLockAlpha = RunCornerPeekRules::LockAlpha(Seconds, Lock.HoldSeconds);
		PeekLockShoulderWeight = Instance->PeekStanceBodyShoulderWeight;
		if (Lock.bWorld)
		{
			// The approach: a world yaw, into the mesh's component space as it stands for this evaluation.
			const USkeletalMeshComponent* Mesh = Instance->GetSkelMeshComponent();
			const float World = RunCornerPeekRules::ApproachLockTargetYaw(Lock.EntryYawCs, Lock.FinalYawCs, Seconds, Lock.PathSeconds);
			PeekLockTargetCs = static_cast<float>(FRotator::NormalizeAxis(World - (Mesh ? Mesh->GetComponentRotation().Yaw : 0.0)));
			PeekLockMaxCorrectionDeg = RunCornerPeekRules::ApproachLockMaxCorrectionDeg;
		}
		else
		{
			PeekLockTargetCs = RunCornerPeekRules::LockTargetYaw(Lock.EntryYawCs, Lock.FinalYawCs, Seconds, Lock.PathSeconds);
			PeekLockMaxCorrectionDeg = 360.f;
		}
	}
	if (PeekLockAlpha <= 0.f)
	{
		PeekLockCorrectionDeg = 0.f;
	}
	WeaponClips = Instance->ShotgunClips;
	CrawlClips = Instance->CrawlClips;
	CrawlWeight = Instance->CrawlWeight;
	InjuredClips = Instance->InjuredClips;
	InjuredWeight = Instance->InjuredWeight;
	InjuredTorsoShare = Instance->InjuredTorsoShare;
	InjuredLimpAngles = Instance->InjuredLimpAngles;
	InjuredUpperShare = Instance->InjuredUpperShare;
	WeaponUpperWeight = Instance->ShotgunUpperWeight;
	WeaponLowerWeight = Instance->ShotgunLowerWeight;
	bWeaponFullBody = Instance->IsWeaponLayerFullBody();
	bWeaponRootReference = bWeaponFullBody && Instance->ActiveWeaponSet().bRootReference;
	WeaponLegsAbpWeight = bWeaponFullBody ? Instance->ShotgunLegsAbpWeight : 0.f;
	bWeaponTorsoOnCapsule = !bWeaponFullBody && Instance->bShotgunWeapon && Instance->ActiveWeaponSet().bFullBody && Instance->ActiveWeaponSet().bCrouchUpper;
	WeaponTorsoRef = Instance->LayerTorsoAlpha > MinSampleWeight ? Instance->LayerTorsoRef : nullptr;
	WeaponTorsoRefTime = Instance->LayerTorsoRefTime;
	WeaponTorsoAlpha = Instance->LayerTorsoAlpha;
	bWeaponProbe = Instance->bWeaponLayerProbe;
	WeaponReloadClip = Instance->ShotgunReloadWeight > MinSampleWeight ? Instance->ActiveWeaponSet().Reload : nullptr;
	WeaponReloadTime = Instance->ShotgunReload.Time;
	WeaponReloadWeight = Instance->ShotgunReloadWeight;
	WeaponFireAdditive = Instance->WeaponFireAdditiveTime >= 0.f ? Instance->WeaponFireAdditiveClip : nullptr;
	WeaponFireAdditiveTime = FMath::Max(0.f, Instance->WeaponFireAdditiveTime);
	// The launcher set: its action clip and the aim poses (OperativeAnimInstanceShotgun.cpp).
	const bool bLauncherLayer = Instance->LayerHandling == EWeaponHandling::Launcher;
	WeaponActionClip = bLauncherLayer && Instance->LauncherActionWeight > MinSampleWeight ? Instance->LauncherActionClip : nullptr;
	WeaponActionTime = FMath::Max(0.f, Instance->LauncherActionTime);
	WeaponActionWeight = Instance->LauncherActionWeight;
	// Any set's pose aim offset (the launcher's, the shotgun's).
	const bool bAimLayer = Instance->bShotgunWeapon;
	WeaponAimBase = bAimLayer ? Instance->ActiveWeaponSet().AimPoseBase : nullptr;
	for (int32 Index = 0; Index < 9; ++Index)
	{
		WeaponAimPoses[Index] = bAimLayer ? Instance->ActiveWeaponSet().AimPoses[Index] : nullptr;
		WeaponAimWeights[Index] = bAimLayer ? Instance->LauncherAimWeights[Index] : 0.f;
	}
	for (int32 Index = 0; Index < Slot(EOperativeClip::Count); ++Index)
	{
		Clips[Index].Sequence = Instance->GetClip(static_cast<EOperativeClip>(Index));
		Clips[Index].Time = Instance->ClipTimes[Index];
		Clips[Index].Weight = Instance->ClipWeights[Index];
	}
}

bool FOperativeAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	if (!bUseNative && HasRootNode())
	{
		return false; // the AnimBP graph drives the pose
	}

	constexpr int32 MaxClips = static_cast<int32>(EOperativeClip::Count);
	TArray<FCompactPose, TInlineAllocator<MaxClips>> Poses;
	TArray<FBlendedCurve, TInlineAllocator<MaxClips>> Curves;
	TArray<UE::Anim::FStackAttributeContainer, TInlineAllocator<MaxClips>> Attributes;
	TArray<float, TInlineAllocator<MaxClips>> Weights;
	Poses.Reserve(MaxClips);
	Curves.Reserve(MaxClips);
	Attributes.Reserve(MaxClips);

	float TotalWeight = 0.f;
	for (const FOperativeClipState& Clip : Clips)
	{
		if (!Clip.Sequence || Clip.Weight < MinSampleWeight)
		{
			continue;
		}
		FCompactPose& Pose = Poses.AddDefaulted_GetRef();
		Pose.SetBoneContainer(&Output.Pose.GetBoneContainer());
		FBlendedCurve& Curve = Curves.AddDefaulted_GetRef();
		Curve.InitFrom(Output.Curve);
		UE::Anim::FStackAttributeContainer& Attribute = Attributes.AddDefaulted_GetRef();
		FAnimationPoseData PoseData(Pose, Curve, Attribute);
		Clip.Sequence->GetAnimationPose(PoseData, FAnimExtractContext(static_cast<double>(Clip.Time), false));
		Weights.Add(Clip.Weight);
		TotalWeight += Clip.Weight;
	}

	if (Poses.IsEmpty() || TotalWeight <= KINDA_SMALL_NUMBER)
	{
		Output.ResetToRefPose();
		return true;
	}
	for (float& Weight : Weights)
	{
		Weight /= TotalWeight;
	}
	FAnimationPoseData OutputData(Output);
	FAnimationRuntime::BlendPosesTogether(Poses, Curves, Attributes, Weights, OutputData);
	if (PeekLockAlpha > 0.f)
	{
		ApplyPeekStanceYawLock(Output);
	}
	return true;
}

// --- Ladder rung IK (docs/design/LADDERS.md 6.2): applied after the whole graph, no ABP node ---------------------------------

FVector UOperativeAnimInstance::GetLadderKneeIKOffset(int32 Side) const
{
	return GetProxyOnGameThread<FOperativeAnimInstanceProxy>().GetKneeIKOffset(Side);
}

bool FOperativeAnimInstanceProxy::Evaluate_WithRoot(FPoseContext& Output, FAnimNode_Base* InRootNode)
{
	if (bUseNative || !HasRootNode())
	{
		return Evaluate(Output);
	}
	const bool bPeekLock = PeekLockAlpha > 0.f;
	const bool bWeaponLayer = WeaponUpperWeight > MinSampleWeight || WeaponLowerWeight > MinSampleWeight || (WeaponReloadClip && WeaponReloadWeight > MinSampleWeight)
		|| WeaponFireAdditive != nullptr;
	if (bWeaponProbe && !bWeaponLayer)
	{
		WeaponProbeLower = WeaponProbeUpper = 0.f; // the layer is off: it changes no bone (the probe reads 0, not the last layer frame)
	}
	const bool bCrawlLayer = CrawlWeight > MinSampleWeight;
	const bool bInjuredLayer = InjuredWeight > MinSampleWeight || !InjuredLimpAngles.IsNearlyZero(); // the cue alone crouched / in cover
	if ((!LadderIK.bActive && !bPeekLock && !bWeaponLayer && !bCrawlLayer && !bInjuredLayer) || InRootNode != GetRootNode())
	{
		KneeIKOffset[0] = KneeIKOffset[1] = FVector::ZeroVector;
		return false; // the graph alone
	}
	EvaluateAnimationNode_WithRoot(Output, InRootNode);
	if (bCrawlLayer)
	{
		BlendProneCrawlLayer(Output); // the directional prone crawl over the graph's prone pose (ProneCrawlRules)
	}
	if (bWeaponLayer)
	{
		FCompactPose GraphPose;
		if (bWeaponProbe)
		{
			GraphPose = Output.Pose;
		}
		BlendWeaponLayer(Output); // the shotgun set over the graph (WeaponAnimLayerRules)
		if (bWeaponProbe)
		{
			RecordWeaponProbe(GraphPose, Output.Pose);
		}
	}
	if (bInjuredLayer)
	{
		BlendInjuredLayer(Output); // the injured body; the torso rises to the graph's / the weapon layer's when firing (InjuredLocomotionRules)
	}
	if (LadderIK.bActive)
	{
		ApplyLadderIK(Output);
	}
	else
	{
		KneeIKOffset[0] = KneeIKOffset[1] = FVector::ZeroVector;
	}
	if (bPeekLock)
	{
		ApplyPeekStanceYawLock(Output);
	}
	return true;
}

void FOperativeAnimInstanceProxy::ApplyLadderIK(FPoseContext& Output) const
{
	LadderPoseIK::Apply(Output, GetComponentTransform(), LadderIK, KneeIKOffset);
}

// --- Run-and-peek: the peek stance yaw lock (#55, the game designer's video Bug_2 2026-11-08) ---------------------------------------------

namespace OperativePeekStanceLock
{
	/** The drawn body's lines: the shoulders (upperarm_l -> upperarm_r) and the hips (thigh_l -> thigh_r), UE mannequin names. */
	const TCHAR* const BodyLineBones[4] = { TEXT("upperarm_l"), TEXT("upperarm_r"), TEXT("thigh_l"), TEXT("thigh_r") };

	/** RunCornerPeekRules::MixBodyYaw of the four bone points (component space). */
	float BodyYaw(const FVector (&Points)[4], float ShoulderWeight)
	{
		const float Shoulders = RunCornerPeekRules::BodyLineYaw(FVector2D(Points[0]), FVector2D(Points[1]));
		const float Hips = RunCornerPeekRules::BodyLineYaw(FVector2D(Points[2]), FVector2D(Points[3]));
		return RunCornerPeekRules::MixBodyYaw(Shoulders, Hips, ShoulderWeight);
	}
}

void FOperativeAnimInstanceProxy::ApplyPeekStanceYawLock(FPoseContext& Output) const
{
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	FCompactPoseBoneIndex Indices[4] = { FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE), FCompactPoseBoneIndex(INDEX_NONE),
		FCompactPoseBoneIndex(INDEX_NONE) };
	for (int32 Bone = 0; Bone < 4; ++Bone)
	{
		const int32 MeshIndex = Bones.GetPoseBoneIndexForBoneName(FName(OperativePeekStanceLock::BodyLineBones[Bone]));
		Indices[Bone] = MeshIndex == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshIndex));
		if (Indices[Bone] == INDEX_NONE)
		{
			PeekLockCorrectionDeg = 0.f;
			bPeekLockNaturalValid = false;
			return; // a LOD without the bones: no lock
		}
	}
	FCSPose<FCompactPose> Pose;
	Pose.InitPose(Output.Pose);
	FVector Points[4];
	for (int32 Bone = 0; Bone < 4; ++Bone)
	{
		Points[Bone] = Pose.GetComponentSpaceTransform(Indices[Bone]).GetLocation();
	}
	const float Natural = OperativePeekStanceLock::BodyYaw(Points, PeekLockShoulderWeight);
	PeekLockNaturalCs = Natural; // the pose's own drawn yaw (UOperativeAnimInstance::SettlePeekStanceYawLock)
	bPeekLockNaturalValid = true;
	const float Correction = FMath::Clamp(RunCornerPeekRules::LockCorrectionDeg(PeekLockTargetCs, Natural, PeekLockAlpha), -PeekLockMaxCorrectionDeg,
		PeekLockMaxCorrectionDeg);
	// The root bone's local transform is its component-space one: turning it about the vertical turns the whole pose round the capsule's axis
	// (the mesh origin), as the held yaw on the mesh did.
	FTransform& Root = Output.Pose[FCompactPoseBoneIndex(0)];
	Root = Root * FTransform(FQuat(FVector::UpVector, FMath::DegreesToRadians(Correction)));
	PeekLockCorrectionDeg = Correction;
}

int32 UOperativeAnimInstance::PeekStanceBiasSlot(ECoverFacing Side, bool bCrouch) const
{
	return (bShotgunWeapon ? 4 : 0) + (bCrouch ? 2 : 0) + CoverFacingRules::ClipIndex(Side);
}

void UOperativeAnimInstance::SettlePeekStanceYawLock()
{
	FPeekStanceYawLockState& Lock = PeekStanceYawLock;
	if (!Lock.bActive || Lock.bSettled || PeekStanceYawLockSeconds() < Lock.PathSeconds)
	{
		return;
	}
	Lock.bSettled = true;
	// The fire idle is fully in now: the pose's own drawn yaw is the real one (the graph's layers on top of the clip - the left-hand IK pulling
	// the left shoulder - turn it a few degrees from the clip's: measured +6 deg on both standing pack clips 2026-11-08). Learnt for the next
	// arrival (the pre-turn then aims right at it) and eased to now over RunCornerPeekRules::SettleSeconds instead of the short fade.
	float Natural = 0.f;
	if (!GetProxyOnGameThread<FOperativeAnimInstanceProxy>().GetPeekLockNaturalCs(Natural))
	{
		return;
	}
	const float Off = static_cast<float>(FRotator::NormalizeAxis(Natural - Lock.FinalYawCs));
	const bool bLearn = FMath::Abs(Off) <= RunCornerPeekRules::MaxDrawnBiasDeg;
	if (bLearn)
	{
		PeekStanceBodyBias[Lock.BiasSlot] = FMath::Clamp(PeekStanceBodyBias[Lock.BiasSlot] + Off, -RunCornerPeekRules::MaxDrawnBiasDeg,
			RunCornerPeekRules::MaxDrawnBiasDeg);
	}
	const bool bEase = FMath::Abs(Off) > RunCornerPeekRules::SettleMinDeg;
	if (bEase)
	{
		Lock.EntryYawCs = Lock.FinalYawCs;
		Lock.FinalYawCs = Natural;
		Lock.PathSeconds = RunCornerPeekRules::SettleSeconds;
		Lock.HoldSeconds = Lock.PathSeconds;
		Lock.BeginTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[CoverAnim] %s: peek stance yaw lock - the fire idle draws the body %.1f deg off the predicted facing (learnt bias %.1f%s)%s"),
		*GetNameSafe(GetOwningActor()), Off, PeekStanceBodyBias[Lock.BiasSlot], bLearn ? TEXT("") : TEXT(", too far to learn"),
		bEase ? TEXT(", eased there") : TEXT(""));
}

double UOperativeAnimInstance::PeekStanceYawLockSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? FMath::Max(0.0, World->GetTimeSeconds() - PeekStanceYawLock.BeginTime) : 0.0;
}

void UOperativeAnimInstance::BeginPeekStanceYawLock(float EntryYawCs, float FinalYawCs, float PathSeconds, ECoverFacing Side, bool bCrouch)
{
	PeekStanceYawLock.bActive = true;
	PeekStanceYawLock.bWorld = false;
	PeekStanceYawLock.bSettled = false;
	PeekStanceYawLock.BiasSlot = PeekStanceBiasSlot(Side, bCrouch);
	PeekStanceYawLock.EntryYawCs = EntryYawCs;
	PeekStanceYawLock.FinalYawCs = FinalYawCs;
	PeekStanceYawLock.PathSeconds = FMath::Max(PathSeconds, 0.f);
	PeekStanceYawLock.HoldSeconds = PeekStanceYawLock.PathSeconds;
	PeekStanceYawLock.BeginTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	++PeekStanceYawLocks;
}

void UOperativeAnimInstance::BeginPeekApproachYawLock(float StartWorldYaw, float FinalWorldYaw, float PathSeconds)
{
	PeekStanceYawLock.bActive = true;
	PeekStanceYawLock.bWorld = true;
	PeekStanceYawLock.bSettled = true; // nothing to learn on the run
	PeekStanceYawLock.EntryYawCs = StartWorldYaw;
	PeekStanceYawLock.FinalYawCs = FinalWorldYaw;
	PeekStanceYawLock.PathSeconds = FMath::Max(PathSeconds, 0.f);
	PeekStanceYawLock.HoldSeconds = TNumericLimits<float>::Max() * 0.5f; // held until the entry's lock replaces it / released
	PeekStanceYawLock.BeginTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}

void UOperativeAnimInstance::ReleasePeekApproachYawLock()
{
	if (PeekStanceYawLock.bActive && PeekStanceYawLock.bWorld)
	{
		ReleasePeekStanceYawLock();
	}
}

bool UOperativeAnimInstance::GetNaturalBodyYawCs(float& OutYawCs) const
{
	if (PeekStanceYawLock.bActive && GetProxyOnGameThread<FOperativeAnimInstanceProxy>().GetPeekLockNaturalCs(OutYawCs))
	{
		return true;
	}
	return GetMeshBodyYawCs(OutYawCs);
}

void UOperativeAnimInstance::ReleasePeekStanceYawLock()
{
	if (!PeekStanceYawLock.bActive)
	{
		return;
	}
	const float Seconds = static_cast<float>(PeekStanceYawLockSeconds());
	if (Seconds < PeekStanceYawLock.PathSeconds)
	{
		// The path ends where it is now (the target holds there) and the fade lets the pose's own facing take over.
		const float Here = PeekStanceYawLock.bWorld
			? RunCornerPeekRules::ApproachLockTargetYaw(PeekStanceYawLock.EntryYawCs, PeekStanceYawLock.FinalYawCs, Seconds, PeekStanceYawLock.PathSeconds)
			: RunCornerPeekRules::LockTargetYaw(PeekStanceYawLock.EntryYawCs, PeekStanceYawLock.FinalYawCs, Seconds, PeekStanceYawLock.PathSeconds);
		PeekStanceYawLock.EntryYawCs = Here;
		PeekStanceYawLock.FinalYawCs = Here;
		PeekStanceYawLock.PathSeconds = Seconds;
	}
	PeekStanceYawLock.HoldSeconds = FMath::Min(PeekStanceYawLock.HoldSeconds, Seconds);
}

float UOperativeAnimInstance::GetPeekStanceYawLockCorrectionDeg() const
{
	return GetProxyOnGameThread<FOperativeAnimInstanceProxy>().GetPeekLockCorrectionDeg();
}

bool UOperativeAnimInstance::GetMeshBodyYawCs(float& OutYawCs) const
{
	OutYawCs = 0.f;
	const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	if (!Mesh)
	{
		return false;
	}
	const TArray<FTransform>& Pose = Mesh->GetComponentSpaceTransforms();
	FVector Points[4];
	for (int32 Bone = 0; Bone < 4; ++Bone)
	{
		const int32 Index = Mesh->GetBoneIndex(FName(OperativePeekStanceLock::BodyLineBones[Bone]));
		if (!Pose.IsValidIndex(Index))
		{
			return false;
		}
		Points[Bone] = Pose[Index].GetLocation();
	}
	OutYawCs = OperativePeekStanceLock::BodyYaw(Points, PeekStanceBodyShoulderWeight);
	return true;
}

bool UOperativeAnimInstance::GetPeekStanceBodyYawCs(ECoverFacing Side, bool bCrouch, float& OutYawCs, float ClipSeconds) const
{
	OutYawCs = 0.f;
	const TArray<TObjectPtr<UAnimSequenceBase>>& Clips = bCrouch ? CoverCrouchFireIdle : CoverStandFireIdle;
	const int32 ClipIndex = CoverFacingRules::ClipIndex(Side);
	const UAnimSequence* Sequence = Clips.IsValidIndex(ClipIndex) ? Cast<UAnimSequence>(Clips[ClipIndex].Get()) : nullptr;
	const USkeleton* Skeleton = Sequence ? Sequence->GetSkeleton() : nullptr;
	if (!Sequence || !Skeleton)
	{
		return false;
	}
	const FReferenceSkeleton& Reference = Skeleton->GetReferenceSkeleton();
	const FAnimExtractContext Context(static_cast<double>(FMath::Max(ClipSeconds, 0.f))); // the montage plays it from its start (0)
	FVector Points[4];
	for (int32 Bone = 0; Bone < 4; ++Bone)
	{
		const int32 ClipBone = Reference.FindBoneIndex(FName(OperativePeekStanceLock::BodyLineBones[Bone]));
		if (ClipBone == INDEX_NONE)
		{
			return false;
		}
		// The bone in component space: up its parent chain.
		FTransform Component = FTransform::Identity;
		for (int32 Chain = ClipBone; Chain != INDEX_NONE; Chain = Reference.GetParentIndex(Chain))
		{
			FTransform Local;
			Sequence->GetBoneTransform(Local, FSkeletonPoseBoneIndex(Chain), Context, false);
			Component = Component * Local;
		}
		Points[Bone] = Component.GetLocation();
	}
	// Plus what the graph's layers add on top of the clip, learnt from the arrivals so far (SettlePeekStanceYawLock).
	OutYawCs = static_cast<float>(FRotator::NormalizeAxis(OperativePeekStanceLock::BodyYaw(Points, PeekStanceBodyShoulderWeight)
		+ PeekStanceBodyBias[PeekStanceBiasSlot(Side, bCrouch)]));
	return true;
}

// --- Tactical cover baseline (Sprint 12) ------------------------------------------------------------------------

UAnimSequenceBase* UOperativeAnimInstance::PickCoverClip(const TArray<TObjectPtr<UAnimSequenceBase>>& Clips) const
{
	return PickCoverClipFor(Clips, CoverFacing);
}

bool UOperativeAnimInstance::PlayCoverFireExit(double Now)
{
	bCoverInFirePose = false;
	CoverFireLeftTime = Now;
	UAnimSequenceBase* Exit = PickCoverClipFor(bIsCrouching ? CoverCrouchFireExit : CoverStandFireExit, CoverFirePoseFacing);
	return Exit && PlayCoverOneShot(Exit, FMath::Max(CoverFireLeaveBlendSeconds, 0.05f), 0.35f); // out of it as softly: into the graph's pose (a crouched exit stands up)
}

UAnimSequenceBase* UOperativeAnimInstance::PickCoverClipFor(const TArray<TObjectPtr<UAnimSequenceBase>>& Clips, ECoverFacing Facing) const
{
	// The pack names its sides facing the wall: *_L works at his own RIGHT (back to the wall), *_R at his left
	// (CoverFacingRules::ClipIndex: Right -> 0 = _L, Left -> 1 = _R; measured 2026-10-06).
	const int32 Index = CoverFacingRules::ClipIndex(Facing);
	if (Clips.IsValidIndex(Index) && Clips[Index])
	{
		return Clips[Index];
	}
	// The other side's clip stands in (unmirrored: the user assigns / mirrors the missing side).
	return Clips.IsValidIndex(1 - Index) ? Clips[1 - Index].Get() : nullptr;
}

UAnimSequenceBase* UOperativeAnimInstance::PickShimmyClip(const TArray<TObjectPtr<UAnimSequenceBase>>& Clips, bool bForward) const
{
	// The walk clips' suffix is the movement direction along the wall in the pack's naming (_L = towards his own right):
	// forward = the facing side's clip, backing away = the other side's suffix (CoverFacingRules::ShimmyClipIndex).
	const int32 Index = CoverFacingRules::ShimmyClipIndex(CoverFacing, bForward);
	if (Clips.IsValidIndex(Index) && Clips[Index])
	{
		return Clips[Index];
	}
	return Clips.IsValidIndex(1 - Index) ? Clips[1 - Index].Get() : nullptr;
}

float UOperativeAnimInstance::TakeCoverHandoffBlend(float Default)
{
	if (CoverHandoffBlend < 0.f)
	{
		return Default;
	}
	const float Blend = CoverHandoffBlend;
	CoverHandoffBlend = -1.f;
	CoverHandoffMontage.Reset();
	return Blend;
}

void UOperativeAnimInstance::FadeOutCoverMontages(float BlendIn)
{
	const FAlphaBlend FadeOut(FMath::Max(BlendIn, 0.f) * 2.f + 0.05f);
	for (FAnimMontageInstance* Instance : MontageInstances)
	{
		if (Instance && Instance->Montage && !Instance->IsStopped() && Instance->Montage->IsValidSlot(FullBodySlot))
		{
			Instance->Stop(FadeOut);
		}
	}
}

UAnimMontage* UOperativeAnimInstance::PlayCoverMontage(UAnimSequenceBase* Clip, const FAlphaBlendArgs& BlendIn, float BlendOut, float PlayRate, int32 LoopCount,
	float StartSeconds)
{
	if (!Clip)
	{
		return nullptr;
	}
	FadeOutCoverMontages(BlendIn.BlendTime);
	const FMontageBlendSettings In(BlendIn);
	UAnimMontage* Montage = UAnimMontage::CreateSlotAnimationAsDynamicMontage_WithBlendSettings(Clip, FullBodySlot, In,
		FMontageBlendSettings(BlendOut), PlayRate, LoopCount);
	if (!Montage || Montage_PlayWithBlendSettings(Montage, In, PlayRate, EMontagePlayReturnType::MontageLength, FMath::Max(StartSeconds, 0.f),
		/*bStopAllMontages*/ false) <= 0.f)
	{
		return nullptr;
	}
	return Montage;
}

void UOperativeAnimInstance::UpdateCoverLayer(const AOperativeCharacter& Operative)
{
	CoverHandoffBlend = -1.f; // a hand-off is only valid in the frame (and the function) it was decided
	CoverHandoffMontage.Reset();
	ON_SCOPE_EXIT
	{
		CoverHandoffBlend = -1.f;
		CoverHandoffMontage.Reset();
	};
	const bool bEntered = bInCover && !bWasInCover;
	const bool bLeft = !bInCover && bWasInCover;
	bWasInCover = bInCover;
	bCoverEnteredThisFrame = bEntered;
	if (bLeft || bIsDead)
	{
		ReleasePeekStanceYawLock(); // off the wall (another order) / dead: the peek stance's held facing lets go
	}
	if (!bUseNativeCoverClips || bIsDead)
	{
		return;
	}
	const double LayerNow = Operative.GetWorld() ? Operative.GetWorld()->GetTimeSeconds() : 0.0;
	if (bLeft)
	{
		if (UAnimMontage* Loop = CoverLoopMontage.Get(); Loop && Montage_IsPlaying(Loop))
		{
			// Out of the fire pose (fix/corner-exit-jerk): its leaned-out body goes back slowly whatever follows (a walk off too).
			Montage_Stop(bCoverInFirePose ? FMath::Max(CoverFireLeaveBlendSeconds, 0.25f) : 0.25f, Loop);
		}
		CoverLoopMontage.Reset();
		CoverLoopClip.Reset();
		CoverPendingFireClip.Reset();
		bCoverEnterPlaying = false;
		bCoverPeekStanceEntry = false;
		CoverPeekStanceMontage.Reset();
		// A cover one-shot (enter / fire / transition) must not outlive the cover: it would hold the cover pose while the
		// capsule walks off (user-found bug 2026-10-07: grid walks slid across the floor in the cover pose).
		if (UAnimMontage* OneShot = CoverOneShotMontage.Get(); OneShot && Montage_IsPlaying(OneShot))
		{
			Montage_Stop(0.2f, OneShot);
		}
		CoverOneShotMontage.Reset();
		// Leaving the fire-ready pose: the exit transition plays when he stays put (walking on blends straight out; an
		// open shot at a target in front of the wall blends straight into the normal shooting pose; a move order / grid
		// walk leaves walking).
		if (bCoverInFirePose && !bKnockedDown && Speed < 20.f && !Operative.IsCoverOpenShotActive() && !Operative.IsMovingOffCover())
		{
			PlayCoverFireExit(LayerNow); // the fire pose's own side (the facing may have changed on the leave), the slow leave blend
		}
		if (bCoverInFirePose)
		{
			CoverFireLeftTime = LayerNow;
		}
		bCoverInFirePose = false;
		return;
	}
	if (!bInCover)
	{
		if (Speed > 20.f)
		{
			if (UAnimMontage* OneShot = CoverOneShotMontage.Get(); OneShot && Montage_IsPlaying(OneShot))
			{
				Montage_Stop(0.2f, OneShot); // the fire -> idle exit never plays over a walk
			}
			CoverOneShotMontage.Reset();
		}
		return;
	}
	if (bIsProne)
	{
		return;
	}
	// Cover_Enter once, then the loop. Back at the wall after an open shot (he only stepped off it): straight to the loop. The squad corner
	// peek's arrival along the wall (IsCoverEntryEasedTurn, game designer 2026-11-01): no wall-facing enter clip either - the eased turn and
	// the peek's step-out (the fire-ready transition below) are the one animation.
	// The run-and-peek's arrival (IsCoverEntryPeekStance, the game designer's video 2026-11-08): no enter clip (its wall-facing start and its
	// back-to-the-wall end were the "hug"), and the fire-ready block below skips the idle -> fire step-out (the "turn out").
	const double Now = Operative.GetWorld() ? Operative.GetWorld()->GetTimeSeconds() : 0.0;
	if (bEntered)
	{
		bCoverPeekStanceEntry = Operative.IsCoverEntryPeekStance();
		CoverPeekStanceEntryTime = Now;
	}
	else if (bCoverPeekStanceEntry && Now - CoverPeekStanceEntryTime > CoverPeekStanceWaitSeconds)
	{
		bCoverPeekStanceEntry = false; // the fire pose never came (suppressed / shimmy first): the ordinary loops and step-out from here
	}
	if (bEntered && (Operative.IsQuietCoverEntry() || Operative.IsCoverEntryEasedTurn() || Operative.IsCoverEntryPeekStance()))
	{
		bCoverEnterPlaying = false;
		CoverLoopMontage.Reset();
		CoverLoopClip.Reset();
	}
	else if (bEntered && Operative.IsCoverSwitchEntry()
		&& GetCoverSwitchClip(bIsCrouching, Operative.GetCoverSwitchApproach(), CoverFacing, Operative.IsCoverSwitchSprintClip()))
	{
		// Wall-edge cover switch (game designer 2026-10-20): the pack's run-in transition from where its body meets the runner
		// (the operative picked the clip - jog or sprint set - and its start for this facing when he entered; the mesh offset carries the body).
		UAnimSequenceBase* Switch = GetCoverSwitchClip(bIsCrouching, Operative.GetCoverSwitchApproach(), CoverFacing, Operative.IsCoverSwitchSprintClip());
		const float StartAt = FMath::Clamp(Operative.GetCoverSwitchStartSeconds(), 0.f, FMath::Max(Switch->GetPlayLength() - 0.1f, 0.f));
		FAlphaBlendArgs BlendIn(FMath::Max(CoverSwitchBlendSeconds, 0.f));
		BlendIn.BlendOption = EAlphaBlendOption::HermiteCubic;
		CoverOneShotMontage = PlayCoverMontage(Switch, BlendIn, 0.2f, 1.f, 1, StartAt);
		CoverClipsPlayed += CoverOneShotMontage.IsValid() ? 1 : 0;
		CoverOneShotBlendOut = 0.2f;
		bCoverEnterPlaying = CoverOneShotMontage.IsValid();
		if (bCoverEnterPlaying)
		{
			CoverClipLog.Add(Switch->GetName());
			LastCoverSwitchClip = Switch->GetName();
			LastCoverSwitchStart = StartAt;
			UE_LOG(LogCodexTactics, Display, TEXT("[CoverAnim] %s: cover switch run-in %s from %.2f s (of %.2f)"), *Operative.DisplayName.ToString(),
				*Switch->GetName(), StartAt, Switch->GetPlayLength());
		}
		CoverLoopMontage.Reset();
		CoverLoopClip.Reset();
	}
	else if (bEntered)
	{
		if (UAnimSequenceBase* Enter = PickCoverClip(bIsCrouching ? CoverCrouchEnter : CoverStandEnter))
		{
			if (Operative.IsCoverEnteringFromRun())
			{
				// User-found bug 2026-10-07 (run -> hard stop -> enter clip): no stop between them, an eased blend over the
				// last steps (the capsule is already at the slot; the mesh offset carries the body from where it ran).
				// The actor turned its back to the wall on entry (~180 deg): the run pose would show turned round under a slow
				// blend (user report 2026-10-09) - the clip's first frame is the arrival pose, so it comes in fast then.
				const bool bBigTurn = Operative.GetCoverEntryTurnDeg() > 90.f;
				FAlphaBlendArgs BlendIn(bBigTurn ? FMath::Min(CoverEnterFromRunBlendSeconds, CoverEnterTurnedBlendSeconds) : FMath::Max(CoverEnterFromRunBlendSeconds, 0.f));
				BlendIn.BlendOption = bBigTurn ? EAlphaBlendOption::Linear : EAlphaBlendOption::HermiteCubic;
				FAlphaBlendArgs BlendOut(0.2f);
				CoverOneShotMontage = PlayCoverMontage(Enter, BlendIn, BlendOut.BlendTime);
			}
			else
			{
				CoverOneShotMontage = PlayCoverMontage(Enter, FAlphaBlendArgs(Operative.GetCoverEntryTurnDeg() > 90.f ? FMath::Min(0.15f, CoverEnterTurnedBlendSeconds) : 0.15f), 0.2f);
			}
			CoverClipsPlayed += CoverOneShotMontage.IsValid() ? 1 : 0;
			CoverOneShotBlendOut = 0.2f;
			bCoverEnterPlaying = CoverOneShotMontage.IsValid();
			if (bCoverEnterPlaying)
			{
				CoverClipLog.Add(Enter->GetName());
			}
			CoverLoopMontage.Reset();
			CoverLoopClip.Reset();
		}
	}
	{
		UAnimMontage* OneShot = CoverOneShotMontage.Get();
		bool bOneShotPlaying = OneShot && Montage_IsPlaying(OneShot);
		if (bCoverEnterPlaying && (!bOneShotPlaying || bShimmying))
		{
			// The enter clip is done, or a shimmy ordered right after the entry cuts it short (never a walk blocked by it).
			if (bOneShotPlaying)
			{
				FadeOutCoverMontages(0.2f); // the shimmy loop fades in over 0.2 s this frame
				bOneShotPlaying = false;
			}
			bCoverEnterPlaying = false;
		}
		if (bShimmying && bOneShotPlaying)
		{
			// A shimmy cuts any cover one-shot (the fire -> idle exit, a transition): the side-step walk shows at once
			// instead of the body sliding along the wall in the exit pose (user request 2026-10-07, seen crouched). Right after the fire
			// pose the cut takes the leave blend (fix/corner-exit-jerk: the leaned-out body jumped back in 0.2 s).
			FadeOutCoverMontages(IsLeavingCoverFirePose(Now) ? FMath::Max(CoverFireLeaveBlendSeconds, 0.2f) : 0.2f);
			bOneShotPlaying = false;
			CoverPendingFireClip.Reset();
		}
		if (UAnimSequenceBase* Pending = CoverPendingFireClip.Get())
		{
			// The shot queued behind the idle -> fire transition starts as that ends.
			if (!bOneShotPlaying || OneShot->GetPlayLength() - Montage_GetPosition(OneShot) <= 0.12f)
			{
				CoverPendingFireClip.Reset();
				PlayCoverOneShot(Pending, 0.1f, 0.2f);
				return;
			}
		}
		if (bOneShotPlaying)
		{
			// Hand-off (user PIE 2026-10-07, the "tuck back" blip): the next clip starts before this one-shot's own blend-out,
			// and the two cross-fade over the same time - the FullBody slot never drops below full weight.
			const float Rate = FMath::Max(FMath::Abs(Montage_GetPlayRate(OneShot)), 0.01f);
			const float TimeLeft = (OneShot->GetPlayLength() - Montage_GetPosition(OneShot)) / Rate;
			if (TimeLeft > CoverOneShotBlendOut + CoverHandoffMarginSeconds)
			{
				return; // enter / transition / fire clip in progress
			}
			CoverHandoffBlend = FMath::Max(TimeLeft, 0.05f);
			CoverHandoffMontage = OneShot;
			bCoverEnterPlaying = false;
		}
		else if (OneShot)
		{
			// Ended on its own (its blend-out runs): the next clip blends in over what is left of it.
			CoverHandoffBlend = CoverOneShotBlendOut;
			CoverOneShotMontage.Reset();
		}
	}
	if (IsPlayingStanceTransition())
	{
		return; // stand <-> crouch at the wall: the stance clip plays, the loop follows
	}
	// Fire-ready pose (user rule 2026-10-06): a threat is known and he stands at the exposed edge on its side -> the
	// idle -> fire transition, then the fire-ready idle; the exit transition when the threat is gone for the hold time,
	// he shimmies off the edge or leaves the cover. Without the clips the look-around corner pose stands in.
	UAnimSequenceBase* FireIdle = PickCoverClip(bIsCrouching ? CoverCrouchFireIdle : CoverStandFireIdle);
	const bool bWantFirePose = bCoverFireReady && FireIdle;
	bool bPeekStanceBlend = false;
	if (bCoverInFirePose && !bShimmying)
	{
		CoverFirePoseFacing = CoverFacing; // the side the fire idle is drawn on (a shimmy may turn the facing before the pose ends)
	}
	if (bWantFirePose && !bCoverInFirePose && bCoverPeekStanceEntry && !bShimmying)
	{
		// The run-and-peek's peek stance: straight from the run into the fire idle (the loop below), no step-out.
		bCoverInFirePose = true;
		CoverFirePoseFacing = CoverFacing;
		bCoverPeekStanceEntry = false;
		bPeekStanceBlend = true;
	}
	else if (bWantFirePose && !bCoverInFirePose)
	{
		bCoverInFirePose = true;
		CoverFirePoseFacing = CoverFacing;
		if (UAnimSequenceBase* Enter = PickCoverClip(bIsCrouching ? CoverCrouchFireEnter : CoverStandFireEnter))
		{
			// Blend out 0.2 like the loops blend in (the fire_idle that follows takes over without a dip).
			if (PlayCoverOneShot(Enter, 0.2f, 0.2f))
			{
				return;
			}
		}
	}
	else if (!bWantFirePose && bCoverInFirePose)
	{
		// fix/corner-exit-jerk: the fire pose's own side (a shimmy turns the facing first: the other side's exit swung the body across), the
		// slow leave blend; a shimmy then cuts it with the same slow blend (above / the loop below).
		if (PlayCoverFireExit(Now))
		{
			return;
		}
	}
	// Reload behind the corner (user request 2026-10-07; the pack has no cover reload clip): after the fire -> idle exit
	// the cover loop steps aside so the normal reload (UpperBodySlot over the graph's idle) shows; the loop resumes after.
	if (bIsReloading && !bShimmying)
	{
		if (UAnimMontage* Loop = CoverLoopMontage.Get(); Loop && Montage_IsPlaying(Loop))
		{
			Montage_Stop(0.25f, Loop);
		}
		CoverLoopMontage.Reset();
		CoverLoopClip.Reset();
		return;
	}
	// The loop wanted now: shimmy forward (towards the threat side he faces) / backwards (away from it, still facing it),
	// the corner-ready pose at the exposed edge on that side, else the idle. Never the plain locomotion walk in cover.
	UAnimSequenceBase* Wanted = nullptr;
	float PlayRate = 1.f;
	if (bShimmying)
	{
		Wanted = PickShimmyClip(bIsCrouching ? (bCoverShimmyForward ? CoverCrouchMoveForward : CoverCrouchMoveBackward)
			: (bCoverShimmyForward ? CoverStandMoveForward : CoverStandMoveBackward), bCoverShimmyForward);
		PlayRate = FMath::Clamp(Speed / FMath::Max(CoverShimmyClipSpeed, 1.f), 0.5f, 2.f);
	}
	if (!Wanted && bCoverInFirePose && FireIdle)
	{
		Wanted = FireIdle; // threat known at the edge: ready to fire
	}
	if (!Wanted && bCoverAtCorner)
	{
		Wanted = PickCoverClip(bIsCrouching ? CoverCrouchCorner : CoverStandCorner); // no threat: looking round the corner
	}
	if (!Wanted)
	{
		Wanted = PickCoverClip(bIsCrouching ? CoverCrouchIdle : CoverStandIdle);
	}
	if (!Wanted)
	{
		return; // no clips assigned: the graph's locomotion shows (back to the wall by the actor rotation)
	}
	UAnimMontage* Loop = CoverLoopMontage.Get();
	if (CoverLoopClip.Get() == Wanted && Loop && Montage_IsPlaying(Loop))
	{
		Montage_SetPlayRate(Loop, PlayRate); // the shimmy pace follows the ground speed
		return;
	}
	// Stand <-> crouch in the fire stance: one fire_idle into the other (UpdateStanceTransition keeps it).
	const bool bFireStanceSwitch = Wanted == FireIdle && CoverLoopClip.IsValid()
		&& (CoverLoopClip.Get() == PickCoverClip(CoverStandFireIdle) || CoverLoopClip.Get() == PickCoverClip(CoverCrouchFireIdle));
	float LoopBlend = TakeCoverHandoffBlend(bPeekStanceBlend && Wanted == FireIdle ? CoverPeekStanceBlendSeconds
		: bFireStanceSwitch ? CoverFireStanceSwitchBlendSeconds : 0.2f);
	if (Wanted != FireIdle && IsLeavingCoverFirePose(Now))
	{
		LoopBlend = FMath::Max(LoopBlend, CoverFireLeaveBlendSeconds); // the loop after the fire pose (the shimmy, the idle): the slow leave blend
	}
	if (bPeekStanceBlend && Wanted == FireIdle)
	{
		++CoverPeekStanceBlends;
		UE_LOG(LogCodexTactics, Display, TEXT("[CoverAnim] %s: run-and-peek arrival - the run blends straight into the peek stance %s over %.2f s (no enter clip, no step-out)"),
			*Operative.DisplayName.ToString(), *Wanted->GetName(), LoopBlend);
	}

	CoverLoopClip = Wanted;
	CoverLoopMontage = PlayCoverMontage(Wanted, FAlphaBlendArgs(LoopBlend), 0.2f, PlayRate, /*LoopCount*/ 1000);
	// Any new loop ends the peek stance's blend from the run (GetCoverEnterBlendWeight follows only that one).
	CoverPeekStanceMontage = bPeekStanceBlend && Wanted == FireIdle ? CoverLoopMontage : nullptr;
	CoverClipsPlayed += CoverLoopMontage.IsValid() ? 1 : 0;
	if (CoverLoopMontage.IsValid())
	{
		CoverClipLog.Add(Wanted->GetName());
		if (CoverClipLog.Num() > 40)
		{
			CoverClipLog.RemoveAt(0, CoverClipLog.Num() - 40);
		}
	}
}

void UOperativeAnimInstance::GetCoverPlayback(FString& OutClip, float& OutMontageWeight, float& OutSlotNodeWeight) const
{
	OutClip = TEXT("none");
	OutMontageWeight = 0.f;
	OutSlotNodeWeight = GetSlotNodeGlobalWeight(FullBodySlot);
	if (const FAnimMontageInstance* Instance = GetActiveMontageInstance(); Instance && Instance->Montage)
	{
		OutMontageWeight = Instance->GetWeight();
		for (const FSlotAnimationTrack& Track : Instance->Montage->SlotAnimTracks)
		{
			if (Track.AnimTrack.AnimSegments.Num() > 0 && Track.AnimTrack.AnimSegments[0].GetAnimReference())
			{
				OutClip = Track.AnimTrack.AnimSegments[0].GetAnimReference()->GetName();
				break;
			}
		}
	}
}

float UOperativeAnimInstance::GetCoverEnterBlendWeight() const
{
	if (!bCoverEnterPlaying)
	{
		// The run-and-peek's peek stance (#55 fix 2026-11-08): its fire idle blending in from the run stands in for the enter clip - the
		// operative lets the held entry yaw go with its weight (EntryHeldYawFraction).
		if (const UAnimMontage* PeekStance = CoverPeekStanceMontage.Get())
		{
			const FAnimMontageInstance* StanceInstance = GetActiveInstanceForMontage(PeekStance);
			return StanceInstance ? StanceInstance->GetWeight() : -1.f;
		}
		return -1.f; // no enter clip playing (none assigned, done, or cut short)
	}
	const UAnimMontage* OneShot = CoverOneShotMontage.Get();
	const FAnimMontageInstance* Instance = OneShot ? GetActiveInstanceForMontage(OneShot) : nullptr;
	return Instance ? Instance->GetWeight() : -1.f;
}

bool UOperativeAnimInstance::GetPeekStanceTurnDeg(ECoverFacing Side, bool bCrouch, float& OutTurnDeg) const
{
	OutTurnDeg = 0.f;
	const TArray<TObjectPtr<UAnimSequenceBase>>& Clips = bCrouch ? CoverCrouchFireIdle : CoverStandFireIdle;
	const int32 Index = CoverFacingRules::ClipIndex(Side);
	const UAnimSequence* Sequence = Clips.IsValidIndex(Index) ? Cast<UAnimSequence>(Clips[Index].Get()) : nullptr;
	const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	const USkeleton* Skeleton = Sequence ? Sequence->GetSkeleton() : nullptr;
	if (!Sequence || !Mesh || !Skeleton)
	{
		return false;
	}
	static const FName PelvisName(TEXT("pelvis"));
	const FReferenceSkeleton& Reference = Skeleton->GetReferenceSkeleton();
	const int32 ClipPelvis = Reference.FindBoneIndex(PelvisName);
	const int32 MeshPelvis = Mesh->GetBoneIndex(PelvisName);
	const TArray<FTransform>& MeshPose = Mesh->GetComponentSpaceTransforms();
	if (ClipPelvis == INDEX_NONE || !MeshPose.IsValidIndex(MeshPelvis))
	{
		return false;
	}
	// The clip's pelvis in component space: up its parent chain at t = 0.
	const FAnimExtractContext Context(0.0);
	FTransform ClipComponent = FTransform::Identity;
	for (int32 Bone = ClipPelvis; Bone != INDEX_NONE; Bone = Reference.GetParentIndex(Bone))
	{
		FTransform Local;
		Sequence->GetBoneTransform(Local, FSkeletonPoseBoneIndex(Bone), Context, false);
		ClipComponent = ClipComponent * Local;
	}
	FQuat Turn = ClipComponent.GetRotation() * MeshPose[MeshPelvis].GetRotation().Inverse();
	if (Turn.W < 0.f)
	{
		Turn = -Turn; // the blend takes the shortest arc
	}
	OutTurnDeg = FMath::RadiansToDegrees(static_cast<float>(Turn.GetTwistAngle(FVector::UpVector)));
	return true;
}

bool UOperativeAnimInstance::HasCoverSwitchClips(bool bCrouch) const
{
	return (bCrouch ? CoverCrouchSwitchEnter : CoverStandSwitchEnter).ContainsByPredicate([](const TObjectPtr<UAnimSequenceBase>& Clip) { return Clip != nullptr; });
}

UAnimSequenceBase* UOperativeAnimInstance::GetCoverSwitchClip(bool bCrouch, ECoverSwitchApproach Approach, ECoverFacing ArrivalFacing, bool bSprint) const
{
	const TArray<TObjectPtr<UAnimSequenceBase>>& Clips = bSprint ? (bCrouch ? CoverCrouchSwitchSprintEnter : CoverStandSwitchSprintEnter)
		: (bCrouch ? CoverCrouchSwitchEnter : CoverStandSwitchEnter);
	const int32 Index = CoverSwitchRules::ClipArrayIndex(Approach, ArrivalFacing);
	return Clips.IsValidIndex(Index) ? Clips[Index].Get() : nullptr;
}

bool UOperativeAnimInstance::GetCoverSwitchTrack(bool bCrouch, ECoverSwitchApproach Approach, ECoverFacing ArrivalFacing, float MeshWorldYaw,
	FCoverSwitchTrack& OutTrack, bool bSprint) const
{
	OutTrack = FCoverSwitchTrack();
	const UAnimSequenceBase* Clip = GetCoverSwitchClip(bCrouch, Approach, ArrivalFacing, bSprint);
	if (!Clip || Clip->GetPlayLength() <= 0.f)
	{
		return false;
	}
	FCoverSwitchTrack* Cached = CoverSwitchTrackCache.Find(Clip);
	if (!Cached)
	{
		// The pack's clips carry their travel in the root bone with root motion off (measured 2026-10-20): the mesh draws it while the
		// capsule stands at the slot. Offsets from the last frame, component space.
		FCoverSwitchTrack Local;
		const float Length = Clip->GetPlayLength();
		const int32 Samples = FMath::Clamp(FMath::CeilToInt(Length * 30.f), 2, 300);
		const FVector End = Clip->ExtractRootTrackTransform(FAnimExtractContext(static_cast<double>(Length)), nullptr).GetLocation();
		for (int32 Index = 0; Index <= Samples; ++Index)
		{
			const float Time = Length * Index / Samples;
			const FVector Root = Clip->ExtractRootTrackTransform(FAnimExtractContext(static_cast<double>(Time)), nullptr).GetLocation();
			Local.Times.Add(Time);
			Local.Offsets.Add(FVector2D(Root.X - End.X, Root.Y - End.Y));
		}
		Cached = &CoverSwitchTrackCache.Add(Clip, MoveTemp(Local));
	}
	const FRotator ToWorld(0.f, MeshWorldYaw, 0.f);
	OutTrack.Times = Cached->Times;
	OutTrack.Offsets.Reserve(Cached->Offsets.Num());
	for (const FVector2D& Offset : Cached->Offsets)
	{
		OutTrack.Offsets.Add(FVector2D(ToWorld.RotateVector(FVector(Offset.X, Offset.Y, 0.f))));
	}
	return OutTrack.IsValid();
}

bool UOperativeAnimInstance::PlayCoverOneShot(UAnimSequenceBase* Clip, float BlendIn, float BlendOut)
{
	if (!Clip)
	{
		return false;
	}
	BlendIn = TakeCoverHandoffBlend(BlendIn);
	CoverLoopMontage.Reset();
	CoverLoopClip.Reset();
	bCoverEnterPlaying = false;
	CoverOneShotBlendOut = BlendOut;
	CoverOneShotMontage = PlayCoverMontage(Clip, FAlphaBlendArgs(BlendIn), BlendOut); // the loop and any one-shot fade out in it
	if (!CoverOneShotMontage.IsValid())
	{
		return false;
	}
	++CoverClipsPlayed;
	CoverClipLog.Add(Clip->GetName());
	if (CoverClipLog.Num() > 40)
	{
		CoverClipLog.RemoveAt(0, CoverClipLog.Num() - 40);
	}
	return true;
}
