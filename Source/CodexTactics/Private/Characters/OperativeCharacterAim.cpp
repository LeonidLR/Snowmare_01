// AOperativeCharacter: Muzzle, aim point, aim cone residual, combat facing, instant aimed turns.
// Split out of OperativeCharacter.cpp verbatim (cleanup day 2026-10-14; map: docs/port/REFACTOR_OPERATIVE_2026-10.md).
// Godot reference: Scenes/movements/player.gd (combat_facing_direction, _align_rotation_with_leader). Aim cone: UE-only
// (2026-10-07).

#include "Characters/OperativeCharacter.h"

#include "Characters/AimOffsetRules.h"
#include "Characters/FacingRules.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacterInternal.h"
#include "Combat/HealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/TurnBasedCombatSubsystem.h"

FVector AOperativeCharacter::GetMuzzleLocation() const
{
	const float Height = Stance == EOperativeStance::Prone ? 25.f : (Stance == EOperativeStance::Crouching ? 85.f : 140.f);
	return GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight() - Height);
}

void AOperativeCharacter::NoteShotAim(AActor* Target, const FVector& Point, bool bBlind)
{
	LastShotTarget = Target;
	LastShotPoint = Point;
	bLastShotBlind = bBlind;
	LastShotTime = GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0e9;
}

bool AOperativeCharacter::GetAimTargetPoint(FVector& OutPoint) const
{
	const UWorld* World = GetWorld();
	if (World && World->GetTimeSeconds() - LastShotTime <= AimTargetHoldSeconds)
	{
		const AActor* Shot = LastShotTarget.Get();
		if (bLastShotBlind || !Shot)
		{
			OutPoint = LastShotPoint;
			return true;
		}
		if (IsValid(Shot))
		{
			OutPoint = Shot->GetActorLocation(); // follows a running target (the tracer's end)
			return true;
		}
	}
	const AActor* Combat = CurrentCombatTarget.Get();
	if (Combat && OperativeCharacterInternal::IsLiveEnemy(Combat))
	{
		OutPoint = Combat->GetActorLocation();
		return true;
	}
	return false;
}

FVector AOperativeCharacter::GetWeaponMuzzleLocation() const
{
	if (WeaponMesh && WeaponMesh->GetStaticMesh() && WeaponMesh->IsVisible())
	{
		static const FName MuzzleSocket(TEXT("Muzzle"));
		const FVector Offset = CurrentWeapon && !CurrentWeapon->HandMesh.IsNull() && !CurrentWeapon->HandMeshMuzzleOffset.IsNearlyZero()
			? CurrentWeapon->HandMeshMuzzleOffset : MuzzleOffset;
		return WeaponMesh->DoesSocketExist(MuzzleSocket) ? WeaponMesh->GetSocketLocation(MuzzleSocket)
			: WeaponMesh->GetComponentTransform().TransformPosition(Offset);
	}
	return GetMuzzleLocation();
}

void AOperativeCharacter::UpdateCombatFacing(float DeltaTime)
{
	// Godot player.gd: combat_facing_direction (_process_combat_shooting) wins over the movement heading
	// (_face_movement_target) while not sprinting, so a single-click move walks sideways / backs off facing the enemy;
	// a sprint turns to the movement and stops the fire.
	bFacingCombatTarget = false;
	if (bInCover)
	{
		// User design rule 2026-10-06: the back stays against the wall (actor yaw = wall normal); the threat side only picks the _L/_R clip
		// (CoverFacingRules); no turn to a target or to the movement (a shimmy away from the threat walks backwards).
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, GetCoverFacingYaw(), 0.f), DeltaTime, 10.f));
		BarrelYawOffset = FMath::FInterpTo(BarrelYawOffset, 0.f, DeltaTime, 6.f);
		return;
	}
	const UGameFlowSubsystem* Flow = GetWorld() ? GetWorld()->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	const bool bRealTimeFight = !Flow || (Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetCombatMode() == ECodexCombatMode::RealTime);
	AActor* Target = CurrentCombatTarget.Get();
	if (bRealTimeFight && !bSprinting && !bCarrying && !IsPanicking() && Target && OperativeCharacterInternal::IsLiveEnemy(Target) && HealthComponent && HealthComponent->IsAlive())
	{
		bFacingCombatTarget = true;
		const float Yaw = (Target->GetActorLocation() - GetActorLocation()).Rotation().Yaw - BarrelYawOffset;
		SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.f, Yaw, 0.f), DeltaTime, 12.f));
	}
	// Godot _face_movement_target / _safe_look_at: otherwise the body turns to the (smoothed) movement above 0.35 m/s
	// with lerp_angle(turn_speed of the stance) and holds still below it — no trembling while braking at the goal.
	SmoothedVelocity = FacingRules::SmoothVelocity(SmoothedVelocity, GetVelocity(), DeltaTime);
	// Turn-based: the grid owns the facing (a shot's FaceAimAt, a step's direction). The parked-follower idle facing from real
	// time used to pull the body back to the leader's old yaw right after every grid shot (user report 2026-10-09).
	const UTurnBasedCombatSubsystem* FacingTurnBased = GetWorld() ? GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>() : nullptr;
	const bool bGridOwnsFacing = FacingTurnBased && FacingTurnBased->IsActive();
	if (bGridOwnsFacing)
	{
		ClearIdleFacing();
	}
	bool bFacingWall = false;
	// Running into cover (user report 2026-10-09: two pops on entry): the last metres turn the body to face the wall, the
	// pose the enter clip starts in (UpdateCoverEntryBlend takes out what is left of the turn).
	if (!bFacingCombatTarget && bHasPendingCover && !bInCover && SmoothedVelocity.SizeSquared2D() > 35.f * 35.f
		&& FVector::Dist2D(GetActorLocation(), PendingCoverSlot.WorldLocation) < CoverApproachFaceWallCm
		&& FVector::DotProduct(SmoothedVelocity.GetSafeNormal2D(), -PendingCoverSlot.WallNormal.GetSafeNormal2D()) >= 0.5f)
	{
		const float WallYaw = (-PendingCoverSlot.WallNormal).Rotation().Yaw;
		SetActorRotation(FRotator(0.f, FacingRules::StepYaw(GetActorRotation().Yaw, WallYaw, 2.f * OperativeMovementRules::GetTurnRate(MovementConfig, Stance) / 57.2958f, DeltaTime), 0.f));
		bFacingWall = true; // the movement facing below stays out of it this frame
	}
	const float TurnSpeed = OperativeMovementRules::GetTurnRate(MovementConfig, Stance) / 57.2958f; // stored as deg/s, Godot rad/s
	if (!bFacingCombatTarget && !bFacingWall && SmoothedVelocity.SizeSquared2D() > 35.f * 35.f)
	{
		SetActorRotation(FRotator(0.f, FacingRules::StepYaw(GetActorRotation().Yaw, SmoothedVelocity.Rotation().Yaw, TurnSpeed, DeltaTime), 0.f));
	}
	else if (!bFacingCombatTarget && bHasIdleFacing && GetVelocity().SizeSquared2D() < 10.f * 10.f && !bGridOwnsFacing)
	{
		// Every frame (it used to step only with the formation repath, every 0.2 s, and looked jerky).
		SetActorRotation(FRotator(0.f, FacingRules::StepYaw(GetActorRotation().Yaw, IdleFacingYaw, TurnSpeed, DeltaTime), 0.f));
	}

	// Barrel vs body yaw in the current aim pose (the rifle is held across the chest); 0 when not aiming.
	float Offset = 0.f;
	const UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	// The sniper clips aim straight along the body (measured: the pack's Offset_F holds the rifle on the mesh forward axis).
	if (bAlignBarrelWithTarget && !IsSniperWeaponEquipped() && Anim && Anim->bIsAiming && WeaponMesh && WeaponMesh->GetStaticMesh() && WeaponMesh->IsVisible())
	{
		const FVector Barrel = WeaponMesh->GetComponentTransform().TransformVectorNoScale(MuzzleOffset.GetSafeNormal());
		if (FMath::Abs(Barrel.Z) < 0.7f) // roughly level, i.e. really aimed
		{
			// The pose's own barrel yaw: the 2D aim offset's twist (in these bones since the last evaluation, with the AimYaw
			// still in the anim instance) is taken out - else the facing and AimYaw chase each other (AnimOffset_Bug_01).
			Offset = FMath::Clamp(FRotator::NormalizeAxis(Barrel.Rotation().Yaw - GetActorRotation().Yaw - GetAppliedAimYaw()), -45.f, 45.f);
		}
	}
	BarrelYawOffset = FMath::FInterpTo(BarrelYawOffset, Offset, DeltaTime, 6.f);
}

void AOperativeCharacter::StopAndFace(const FVector& Location)
{
	SetSprinting(false);
	StopOperative();
	const FVector Direction = (Location - GetActorLocation()).GetSafeNormal2D();
	if (!Direction.IsNearlyZero())
	{
		SetActorRotation(FRotator(0.f, Direction.Rotation().Yaw, 0.f));
	}
}

FVector AOperativeCharacter::GetStanceAimOrigin() const
{
	return bInCover && bCornerAimActive && (CurrentCoverHeight == ECoverHeight::HighCover || IsManualCornerPeekSpot()) ? GetCoverFireOrigin()
		: GetMuzzleLocation();
}

FVector AOperativeCharacter::GetStanceAimBaseDirection() const
{
	if (bInCover && ((CurrentCoverHeight == ECoverHeight::HighCover && (bCornerAimActive || IsCornerHoldSpot()))
		|| (bCornerAimActive && IsManualCornerPeekSpot())))
	{
		return CoverFacingRules::CornerAimDirection(CoverSlot, CoverFacing, CornerAimOutwardDeg);
	}
	// Out of cover (and any other pose): the pose's barrel yaw (the rifle is held across the chest). BarrelYawOffset is
	// measured on the weapon with the 2D aim offset's twist taken out (UpdateCombatFacing): a stable reference, the AO never
	// feeds back into its own input (AnimOffset_Bug_01, 2026-10-08).
	return FRotator(0.f, GetActorRotation().Yaw + BarrelYawOffset, 0.f).Vector();
}

float AOperativeCharacter::GetAppliedAimYaw() const
{
	const UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	return Anim && Anim->bAimOffsetYaw ? Anim->AimYaw * Anim->AimOffsetAlpha : 0.f;
}

float AOperativeCharacter::GetCornerHoldReachDeg() const
{
	// 5 deg inside the shot cone: a target the corner stance claims is one its barrel can be brought onto (the clip's
	// barrel varies a few degrees around CornerAimOutwardDeg) - no stand-off where he neither fires nor steps out.
	return FMath::Max(5.f, GetAimTwistLimitDeg() + AimConeDeg - 5.f);
}

float AOperativeCharacter::GetAimTwistLimitDeg() const
{
	const UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	return Anim && Anim->bAimOffsetYaw && Anim->bAimOffset && !Anim->bAimOffsetTurnBasedOff ? Anim->AimYawClampDegrees : 0.f;
}

float AOperativeCharacter::GetShotAimResidualDeg(const FVector& TargetLocation) const
{
	// The real barrel when the rifle is out and level (the pose as animated, the 2D aim offset's twist included): the shot
	// goes where the rifle points (user rule 2026-10-07). Else the stance rule (the pose's aim direction + the twist).
	if (bCoverEntryAwaitClip || CoverEntryYawTime >= 0.f)
	{
		return 180.f; // the body is still turning after an instant actor turn (the mesh holds the old yaw): not aimed yet
	}
	if (IsSniperWeaponEquipped() && !bInCover)
	{
		// The sniper clips aim along the body's forward (the M16 stand-in model is held at the pack rifle's angle only
		// roughly): the body's facing decides, not the stand-in's barrel.
		return FMath::Abs(AimOffsetRules::YawToTarget(GetMuzzleLocation(), GetActorForwardVector(), TargetLocation, 180.f));
	}
	const USkeletalMeshComponent* Body = GetMesh();
	const bool bPoseLive = Body && (Body->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones
		|| Body->WasRecentlyRendered(0.25f)); // headless (not rendered) the bones keep a stale pose
	if (bPoseLive && WeaponMesh && WeaponMesh->GetStaticMesh() && WeaponMesh->IsVisible() && UsesAmmo())
	{
		const FVector Barrel = WeaponMesh->GetComponentTransform().TransformVectorNoScale(MuzzleOffset.GetSafeNormal());
		if (Barrel.Size2D() < 0.3f)
		{
			return 180.f; // the rifle points up / down (a clip in between): not aimed at anything
		}
		return FMath::Abs(AimOffsetRules::YawToTarget(GetWeaponMuzzleLocation(), Barrel, TargetLocation, 180.f));
	}
	const float Error = AimOffsetRules::YawToTarget(GetStanceAimOrigin(), GetStanceAimBaseDirection(), TargetLocation, 180.f);
	return AimOffsetRules::ResidualAimError(Error, GetAppliedAimYaw());
}

void AOperativeCharacter::FaceAimAt(const FVector& TargetLocation)
{
	const FVector Delta = TargetLocation - GetActorLocation();
	ClearIdleFacing(); // an aimed facing is not undone by the parked-follower facing
	if (!Delta.IsNearlyZero())
	{
		SetActorRotation(FRotator(0.f, Delta.Rotation().Yaw - BarrelYawOffset, 0.f));
	}
}
