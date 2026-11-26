// Per-operative stealth, designer task 003. UE-only; no Godot reference.
// Reuses the existing formation ghost material at runtime; source materials and assets are never changed.
#include "Characters/OperativeCharacter.h"
#include "Characters/OperativeCharacterInternal.h"
#include "AI/PmcSquadSubsystem.h"
#include "Characters/StealthModeRules.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Tactics/LookHoldRules.h"
#include "Vehicles/VehicleSubsystem.h"

bool AOperativeCharacter::CanEnterStealthWithoutFormation() const
{
	return StealthModeRules::GetConfig().bEnabled && HealthComponent && HealthComponent->IsAlive() && !IsCriticallyWounded()
		&& !IsKnockedDown() && !IsPanicking() && !IsRaging() && !bCarrying && !bVaulting && !bMantling && !IsClimbingLadder() && !VehicleQueries::IsMounted(this);
}

bool AOperativeCharacter::CanEnterStealthMode() const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	if (Squad && Squad->IsStealthBlockedByFormation(*this))
	{
		return false;
	}
	return CanEnterStealthWithoutFormation();
}

bool AOperativeCharacter::SetStealthMode(bool bEnabled)
{
	if (bEnabled == bStealthMode)
	{
		return true;
	}
	const FStealthModeConfig& Config = StealthModeRules::GetConfig();
	if (bEnabled)
	{
		if (!CanEnterStealthMode())
		{
			return false;
		}
		if (!StealthMaterial)
		{
			UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/UI/Materials/M_FormationGhost.M_FormationGhost"));
			if (!Parent)
			{
				return false;
			}
			StealthMaterial = UMaterialInstanceDynamic::Create(Parent, this);
		}
		StealthMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(Config.VisualGray, Config.VisualGray, Config.VisualGray, Config.VisualOpacity));
		StealthMaterial->SetScalarParameterValue(TEXT("Fill"), 1.f);
		StealthMaterial->SetScalarParameterValue(TEXT("RimBoost"), 0.f);
		StealthMaterial->SetScalarParameterValue(TEXT("ScanBoost"), 0.f);
		StealthMaterial->SetScalarParameterValue(TEXT("FlickerAmount"), 0.f);
		StealthMaterial->SetScalarParameterValue(TEXT("OpacityFill"), Config.VisualOpacity);
		StealthMaterial->SetScalarParameterValue(TEXT("RimOpacity"), 0.f);
		StealthMaterial->SetScalarParameterValue(TEXT("ScanOpacity"), 0.f);
		StealthMaterial->SetScalarParameterValue(TEXT("EmissiveIntensity"), 1.f);
		// Restore any late feedback overlay from a previous use before taking a fresh material snapshot.
		RefreshStealthVisuals();
		StealthMeshes.Reset();
		StealthMaterialCounts.Reset();
		StealthOriginalMaterials.Reset();
		SetSprinting(false);
		bStealthFast = false;
		ClearStealthManualLookTarget();
		ClearStealthAutoLookTarget();
		ClearIdleFacing();
		SetFormationLookYaw(TOptional<float>());
		SmoothedVelocity = FVector::ZeroVector;
		bStealthMode = true;
		if (Stance != EOperativeStance::Prone)
		{
			SetStance(EOperativeStance::Crouching);
		}
	}
	else
	{
		bStealthFast = false;
		ClearStealthManualLookTarget();
		ClearStealthAutoLookTarget();
		bStealthMode = false;
		ApplyMovementParams();
	}
	RefreshStealthVisuals();
	return true;
}

bool AOperativeCharacter::CanStealthFast() const
{
	return bStealthMode && !bCarrying && !IsWounded() && !IsCriticallyWounded() && Stance == EOperativeStance::Crouching;
}

void AOperativeCharacter::SetStealthFast(bool bFast)
{
	const bool bAllowed = bFast && CanStealthFast();
	if (bStealthFast != bAllowed)
	{
		bStealthFast = bAllowed;
		ApplyMovementParams();
	}
}

void AOperativeCharacter::SetManualLookPoint(const FVector& WorldPoint)
{
	const LookHoldRules::FLookDirection Dir = LookHoldRules::DirectionFromClick(GetActorLocation(), WorldPoint);
	ManualLookPoint = WorldPoint;
	SetManualLookDirection(Dir.YawDeg, Dir.PitchDeg);
}

void AOperativeCharacter::SetManualLookDirection(float YawDeg, float PitchDeg)
{
	bHasManualLookPoint = true;
	ManualLookYawDeg = FRotator::NormalizeAxis(YawDeg);
	ManualLookPitchDeg = FMath::Clamp(PitchDeg, -89.f, 89.f);
	ClearIdleFacing();
	bExplicitFacing = false;
	// A look set while the plain corner peek is already up switches to the CoverIndoor pose. A look before that peek
	// stays a look hold only (the peek order and the cover entry clear it).
	if (IsPeekingRoundCorner())
	{
		bCornerLookPose = true;
	}
}

void AOperativeCharacter::ClearManualLookPoint()
{
	const bool bHadLook = bHasManualLookPoint;
	bHasManualLookPoint = false;
	ManualLookPoint = FVector::ZeroVector;
	ManualLookYawDeg = 0.f;
	ManualLookPitchDeg = 0.f;
	bCornerLookPose = false;
	// Neutral look (Shift-click on himself) and every other clear. Cover entry calls this after bInCover is set,
	// so the release no-ops there and the entry yaw blend keeps the mesh.
	if (bHadLook)
	{
		ReleaseTorsoToMovement(FormationLookYaw.IsSet());
	}
}

void AOperativeCharacter::SetStealthManualLookTarget(const FVector& WorldPoint)
{
	if (!bStealthMode)
	{
		ClearManualLookPoint();
		return;
	}
	const LookHoldRules::FLookDirection Dir = LookHoldRules::DirectionFromClick(GetActorLocation(), WorldPoint);
	bHasManualLookPoint = true;
	ManualLookPoint = WorldPoint;
	ManualLookYawDeg = Dir.YawDeg;
	ManualLookPitchDeg = Dir.PitchDeg;
}

void AOperativeCharacter::ClearStealthManualLookTarget()
{
	ClearManualLookPoint();
}

void AOperativeCharacter::ClearStealthAutoLookTarget()
{
	StealthAutoLookActor.Reset();
	StealthAutoLookTargetId = 0;
	StealthAutoLookLocation = FVector::ZeroVector;
	StealthAutoLookLostSeconds = 0.f;
}

void AOperativeCharacter::UpdateStealthLookLock(float DeltaSeconds)
{
	if (!bStealthMode || !GetWorld() || !HealthComponent || !HealthComponent->IsAlive() || IsKilledInAction() || bInCover)
	{
		ClearStealthAutoLookTarget();
		return;
	}
	const UTacticalSightSubsystem* Sight = GetWorld()->GetSubsystem<UTacticalSightSubsystem>();
	const UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(GetWorld());
	TArray<StealthModeRules::FStealthLookLockCandidate, TInlineAllocator<16>> Candidates;
	TMap<uint32, AActor*, TInlineSetAllocator<16>> CandidateActors;
	const FVector Eye = UTacticalSightSubsystem::EyePoint(*this);
	for (AActor* Target : OperativeCharacterInternal::GatherTargetsFor(*this))
	{
		if (!IsValid(Target))
		{
			continue;
		}
		const UHealthComponent* TargetHealth = Target->FindComponentByClass<UHealthComponent>();
		const AOperativeCharacter* OtherOp = Cast<AOperativeCharacter>(Target);
		const bool bLive = (!TargetHealth || TargetHealth->IsAlive())
			&& (!OtherOp || (!OtherOp->IsKilledInAction() && OtherOp->TeamId != TeamId));
		if (!bLive)
		{
			continue;
		}
		const bool bVisibleToSquad = !Target->IsHidden()
			&& (!Sight || (OtherOp ? !Sight->IsHostileOperativeConcealed(OtherOp) : Sight->IsVisibleToSquad(Target)))
			&& (!OtherOp || !Pmc || Pmc->IsVisibleToTeam(TeamId, OtherOp));
		const bool bHasClearLine = !Sight || Sight->HasClearSight(Eye, Target->GetActorLocation());
		const bool bVisible = bVisibleToSquad && bHasClearLine;
		const uint32 Id = Target->GetUniqueID();
		Candidates.Add({ Id, Target->GetActorLocation(), bVisible });
		CandidateActors.Add(Id, Target);
	}
	if (StealthAutoLookTargetId != 0)
	{
		AActor* CurrentActor = CandidateActors.FindRef(StealthAutoLookTargetId);
		if (!CurrentActor || StealthAutoLookActor.Get() != CurrentActor)
		{
			ClearStealthAutoLookTarget();
		}
	}
	StealthModeRules::FStealthLookLockState LockState;
	LockState.TargetId = StealthAutoLookTargetId;
	LockState.LastKnownLocation = StealthAutoLookLocation;
	LockState.TimeSinceSeenSeconds = StealthAutoLookLostSeconds;
	const float HeadingYaw = static_cast<float>(GetSightFacing().Rotation().Yaw);
	StealthModeRules::UpdateLookLock(LockState, GetActorLocation(), HeadingYaw, Candidates, DeltaSeconds, StealthModeRules::GetConfig(), 50.f);
	StealthAutoLookTargetId = LockState.TargetId;
	StealthAutoLookLocation = LockState.LastKnownLocation;
	StealthAutoLookLostSeconds = LockState.TimeSinceSeenSeconds;
	StealthAutoLookActor = StealthAutoLookTargetId != 0 ? CandidateActors.FindRef(StealthAutoLookTargetId) : nullptr;
}

bool AOperativeCharacter::GetManualLookPoint(FVector& OutPoint) const
{
	if (HasManualLookPoint())
	{
		OutPoint = LookHoldRules::ProjectLookPoint(GetActorLocation(), ManualLookYawDeg, ManualLookPitchDeg, 1000.f);
		return true;
	}
	return false;
}

bool AOperativeCharacter::IsLookHoldEligible() const
{
	if (bVaulting || IsClimbingLadder() || bCarrying || IsPanicking())
	{
		return false;
	}
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	const bool bSolo = Squad && Squad->IsSoloMode();
	const bool bLeader = Squad ? Squad->GetLeader() == this : true;
	return LookHoldRules::IsLookHoldEligible(IsInStealthMode(), bSolo, bLeader);
}

bool AOperativeCharacter::GetActiveLookPoint(FVector& OutPoint) const
{
	if (IsLookHoldEligible() && GetManualLookPoint(OutPoint))
	{
		return true;
	}
	if (IsInStealthMode() && HasStealthAutoLookTarget() && LookHoldRules::IsValidLookPoint(StealthAutoLookLocation, GetActorLocation(), LookHoldRules::GetConfig().MinPlacementDistanceCm))
	{
		OutPoint = StealthAutoLookLocation;
		return true;
	}
	return false;
}

bool AOperativeCharacter::GetStealthLookPoint(FVector& OutPoint) const
{
	if (!IsInStealthMode())
	{
		return false;
	}
	return GetActiveLookPoint(OutPoint);
}

void AOperativeCharacter::RefreshStealthVisuals()
{
	if (!StealthMaterial)
	{
		return;
	}
	if (bStealthMode)
	{
		TInlineComponentArray<UMeshComponent*> Meshes(this);
		for (UMeshComponent* BodyPart : Meshes)
		{
			const bool bBodyOrWeapon = BodyPart->IsA<USkeletalMeshComponent>() || BodyPart == BodyMesh.Get() || BodyPart == WeaponMesh.Get()
				|| BodyPart == WeaponRoundMesh.Get()
				|| WeaponMeshParts.ContainsByPredicate([BodyPart](const TObjectPtr<UStaticMeshComponent>& Part) { return Part.Get() == BodyPart; });
			if (!bBodyOrWeapon || StealthMeshes.Contains(BodyPart))
			{
				continue; // markers / vision meshes keep their visuals; existing snapshots stay stable
			}
			StealthMeshes.Add(BodyPart);
			StealthMaterialCounts.Add(BodyPart->GetNumMaterials());
			for (int32 Slot = 0; Slot < BodyPart->GetNumMaterials(); ++Slot)
			{
				// Preserve a null override as null: otherwise changing the mesh would keep the previous weapon's default material.
				StealthOriginalMaterials.Add(BodyPart->OverrideMaterials.IsValidIndex(Slot) ? BodyPart->OverrideMaterials[Slot].Get() : nullptr);
			}
		}
	}
	int32 Saved = 0;
	for (int32 Index = 0; Index < StealthMeshes.Num(); ++Index)
	{
		UMeshComponent* BodyPart = StealthMeshes[Index].Get();
		const int32 Count = StealthMaterialCounts[Index];
		for (int32 Slot = 0; Slot < Count; ++Slot, ++Saved)
		{
			if (!BodyPart || Slot >= BodyPart->GetNumMaterials())
			{
				continue;
			}
			UMaterialInterface* Current = BodyPart->GetMaterial(Slot);
			if (bStealthMode)
			{
				if (Current != StealthMaterial)
				{
					StealthOriginalMaterials[Saved] = BodyPart->OverrideMaterials.IsValidIndex(Slot) ? BodyPart->OverrideMaterials[Slot].Get() : nullptr;
					BodyPart->SetMaterial(Slot, StealthMaterial);
				}
			}
			else if (Current == StealthMaterial)
			{
				BodyPart->SetMaterial(Slot, StealthOriginalMaterials[Saved]);
			}
		}
		if (BodyPart)
		{
			if (bStealthMode && !BodyPart->GetOverlayMaterial())
			{
				BodyPart->SetOverlayMaterial(StealthMaterial);
			}
			else if (!bStealthMode && BodyPart->GetOverlayMaterial() == StealthMaterial)
			{
				BodyPart->SetOverlayMaterial(nullptr);
			}
		}
	}
}

void AOperativeCharacter::HandleStealthWeaponFired(AOperativeCharacter* Shooter, AActor*, bool)
{
	if (Shooter == this && StealthModeRules::ShouldBreak(bStealthMode, true, false, IsStealthFast(), StealthModeRules::GetConfig()))
	{
		SetStealthMode(false);
	}
}

void AOperativeCharacter::UpdateStealthMode()
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	const bool bFormationBlocked = Squad && Squad->IsStealthBlockedByFormation(*this);
	// R17-2 (designer 2026-11-17): a mantle / vault / fence climb keeps stealth - the climb clips play over the stealth layer and the stance
	// stays crouched (SetStance clamps Standing while stealthed); only a ladder still ends it (StealthModeRules::ShouldBreakOnClimb).
	const FStealthModeConfig& Config = StealthModeRules::GetConfig();
	if (bStealthMode && (bFormationBlocked || !HealthComponent || !HealthComponent->IsAlive() || IsCriticallyWounded() || IsKnockedDown()
		|| IsPanicking() || IsRaging() || bCarrying || VehicleQueries::IsMounted(this)
		|| StealthModeRules::ShouldBreakOnClimb(true, bVaulting || bMantling, IsClimbingLadder(), Config)
		|| StealthModeRules::ShouldBreak(true, false, bSprinting, IsStealthFast(), Config)))
	{
		SetStealthMode(false);
	}
	// Feedback owns its own overlay while it flashes. Reclaim only an empty slot; clean up a late restore after stealth ended.
	RefreshStealthVisuals();
}
