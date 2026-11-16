// AOperativeCharacter: the fire gate - turn -> raise the weapon -> only then the round, on every operative fire path (bugs 058c / 061, the game
// designer 2026-11-15: "he faces the wall and his rounds fly at the enemy"; "they fire first and turn after"). UE-only, no Godot reference.
// Rules / tunables: Combat/FireGateRules (Content/Data/Combat/fire_gate.json); tests CodexTactics.Combat.FireGate.*; smoke FireGateSmoke.
// Callers: RunCombatShooting (auto fire open / cover / corner, the squad and the PMC fighters), RunQuickFire, RunSuppressFire,
// RunPendingObjectShot (ShootAtObject).

#include "Characters/OperativeCharacter.h"

#include "Characters/AimOffsetRules.h"
#include "Characters/OperativeAnimInstance.h"
#include "Combat/FireGateRules.h"
#include "Combat/SuppressionSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Tactics/CoverFacingRules.h"

namespace OperativeFireGateLocal
{
	static TAutoConsoleVariable<int32> CVarFireGate(TEXT("Codex.Fire.Gate"), 1,
		TEXT("Bugs 058c / 061 (2026-11-15): 1 (default) = every operative round waits for the fire gate - the barrel / body on the target, the weapon ")
		TEXT("raised (Content/Data/Combat/fire_gate.json); 0 = the per-path checks of 2026-11-15 only (A/B)."));
}

bool AOperativeCharacter::IsFireGateOn()
{
	return OperativeFireGateLocal::CVarFireGate.GetValueOnGameThread() != 0 && FireGateRules::IsEnabled(FireGateRules::GetConfig());
}

bool AOperativeCharacter::IsCoverShotBlind() const
{
	return bInCover && (CoverFireMode == ECoverFireMode::BlindFire || !USuppressionSubsystem::MayExposeNow(this));
}

float AOperativeCharacter::GetFireGateResidualDeg(const FVector& AimAt) const
{
	if (!bInCover)
	{
		return GetShotAimResidualDeg(AimAt); // the barrel / the stance (+ the twist); prone the body too (bug #33)
	}
	if (IsCoverShotBlind())
	{
		return 0.f; // head down: the round is not aimed (spread / hit malus); only his back to the wall counts
	}
	if (bCoverEntryAwaitClip || CoverEntryYawTime >= 0.f)
	{
		return 180.f; // the body is still turning onto the wall: not aimed yet
	}
	// The real barrel while the pose is evaluated (rendered / always ticked): the lean / pop-up clip aims where it aims.
	const USkeletalMeshComponent* Body = GetMesh();
	const bool bPoseLive = Body && (Body->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones
		|| Body->WasRecentlyRendered(0.25f));
	const bool bHighOrPeek = CurrentCoverHeight == ECoverHeight::HighCover || (bCornerAimActive && IsManualCornerPeekSpot());
	if (bHighOrPeek || (bPoseLive && WeaponMesh && WeaponMesh->GetStaticMesh() && WeaponMesh->IsVisible() && UsesAmmo()))
	{
		return GetPoseAimResidualDeg(AimAt); // the high corner stance's own rule (CornerHoldSmoke) or the measured barrel
	}
	// A low cover's fire stance seen nowhere (off screen, headless): the crouched fire idle aims like the corner stance (it leans out and
	// points CornerAimOutwardDeg past the along-wall line - LeanPoseSmoke), its sideways root step cancelled over the top.
	const FVector Aim = CoverFacingRules::CornerAimDirection(CoverSlot, CoverFacing, CornerAimOutwardDeg);
	return FMath::Abs(AimOffsetRules::YawToTarget(GetCoverFireOrigin(), Aim, AimAt, 180.f));
}

bool AOperativeCharacter::CanFireAt(const FVector& AimAt, const TCHAR*& OutReason, EFireGateBlock* OutBlock)
{
	OutReason = FireGateRules::BlockName(EFireGateBlock::None);
	if (OutBlock)
	{
		*OutBlock = EFireGateBlock::None;
	}
	UWorld* World = GetWorld();
	if (!World || !IsFireGateOn())
	{
		return true;
	}
	const FFireGateConfig& Config = FireGateRules::GetConfig();
	const double Now = World->GetTimeSeconds();
	UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;

	// The raise is asked for on every attempt (also while he still turns): the weapon comes up in parallel with the turn.
	if (Anim)
	{
		Anim->DemandInjuredRaise(); // wounded: the torso / the hunch rises into the weapon pose
		if (!bInCover)
		{
			Anim->HoldAimPose(); // the aim pose (bIsAiming) - healthy operatives get it too, not only after the first round
		}
	}
	if (bInCover)
	{
		RaiseCoverFireStance(); // lean out / pop up / blind fire BEFORE the round, not with it
		TrackCoverFireStance();
	}

	// An attempt after a pause (he aimed elsewhere / nowhere): the barrel settles again - unless it is on the point already (no turn to settle).
	const bool bFreshAttempt = FireGateLastEvalTime < 0.0 || Now - FireGateLastEvalTime > FMath::Max(Config.EvalGapSeconds, 0.f);
	if (bFreshAttempt)
	{
		FireGateOnTargetSince = -1.0;
	}
	FireGateLastEvalTime = Now;

	FFireGateInput Input;
	Input.ResidualDeg = GetFireGateResidualDeg(AimAt);
	Input.ConeDeg = AimConeDeg;
	Input.bInCover = bInCover;
	if (bInCover)
	{
		// His back to the wall (the cover poses aim from there); the shot itself is judged by the pose's barrel above.
		Input.bBodyReady = CoverFacingRules::IsFacingAligned(GetActorRotation().Yaw, CoverSlot, CoverFacing, FCoverFacingConfig().FacingToleranceDeg);
	}
	else
	{
		// Still, standing / crouched: the body itself inside the arc (the twist alone never fires). Prone the residual holds the body already
		// (bug #33); the sniper's residual is the body's.
		const bool bBodyInResidual = Stance == EOperativeStance::Prone || IsSniperWeaponEquipped();
		Input.bBodyReady = bBodyInResidual || FireGateRules::IsBodyInArc(GetBodyAimErrorDeg(AimAt), GetBodyArcDeg());
	}
	const bool bOnTarget = Input.bBodyReady && Input.ResidualDeg <= Input.ConeDeg;
	FireGateOnTargetSince = bFreshAttempt && bOnTarget ? Now - FMath::Max(Config.SettleSeconds, 0.f)
		: FireGateRules::StepSince(FireGateOnTargetSince, bOnTarget, Now);
	Input.SecondsOnTarget = FireGateRules::SecondsSince(FireGateOnTargetSince, Now);
	Input.bAnimLive = Anim && Anim->IsUpperBodyUpdateFresh(Config.AnimStaleSeconds);
	Input.SecondsRaised = bInCover ? FireGateRules::SecondsSince(CoverStanceUpSince, Now) : (Anim ? Anim->GetAimPoseUpSeconds() : -1.f);
	Input.bStanceClip = Anim && Anim->IsPlayingStanceTransition();
	Input.bWoundedRaisePending = Anim && Anim->IsInjuredRaisePending();

	const EFireGateBlock Block = FireGateRules::Evaluate(Input, Config);
	LastFireGateBlock = Block;
	if (Block == EFireGateBlock::None)
	{
		return true;
	}
	++FireGateHolds[static_cast<int32>(Block)];
	if (Block == EFireGateBlock::WoundedRaise)
	{
		++InjuredRaiseHolds; // the wounded raise's own counters (smokes)
		LastInjuredRaiseHoldTime = Now;
	}
	OutReason = FireGateRules::BlockName(Block);
	if (OutBlock)
	{
		*OutBlock = Block;
	}
	return false;
}
