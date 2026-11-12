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
#include "Vehicles/VehicleSubsystem.h"

bool AOperativeCharacter::CanEnterStealthWithoutFormation() const
{
	return StealthModeRules::GetConfig().bEnabled && HealthComponent && HealthComponent->IsAlive() && !IsCriticallyWounded()
		&& !IsKnockedDown() && !IsPanicking() && !IsRaging() && !bCarrying && !bVaulting && !IsClimbingLadder() && !VehicleQueries::IsMounted(this);
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

void AOperativeCharacter::SetStealthManualLookTarget(const FVector& WorldPoint)
{
	if (!bStealthMode)
	{
		ClearStealthManualLookTarget();
		return;
	}
	bHasStealthManualLook = true;
	StealthManualLookTarget = WorldPoint;
}

void AOperativeCharacter::ClearStealthManualLookTarget()
{
	bHasStealthManualLook = false;
	StealthManualLookTarget = FVector::ZeroVector;
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
	if (!bStealthMode || !GetWorld() || !HealthComponent || !HealthComponent->IsAlive() || IsKilledInAction())
	{
		ClearStealthAutoLookTarget();
		return;
	}
	const UTacticalSightSubsystem* Sight = GetWorld()->GetSubsystem<UTacticalSightSubsystem>();
	const UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(GetWorld());
	TArray<StealthModeRules::FStealthLookLockCandidate> Candidates;
	TMap<uint32, AActor*> CandidateActors;
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
		const bool bVisible = !Target->IsHidden()
			&& (!Sight || (OtherOp ? !Sight->IsHostileOperativeConcealed(OtherOp) : Sight->IsVisibleToSquad(Target)))
			&& (!OtherOp || !Pmc || Pmc->IsVisibleToTeam(TeamId, OtherOp));
		const uint32 Id = Target->GetUniqueID();
		Candidates.Add({ Id, Target->GetActorLocation(), bVisible });
		CandidateActors.Add(Id, Target);
	}
	if (StealthAutoLookTargetId != 0 && (!StealthAutoLookActor.IsValid() || !CandidateActors.Contains(StealthAutoLookTargetId)))
	{
		ClearStealthAutoLookTarget();
	}
	StealthModeRules::FStealthLookLockState LockState;
	LockState.TargetId = StealthAutoLookTargetId;
	LockState.LastKnownLocation = StealthAutoLookLocation;
	LockState.TimeSinceSeenSeconds = StealthAutoLookLostSeconds;
	const bool bMoving = GetVelocity().SizeSquared2D() > 10.f * 10.f;
	const float HeadingYaw = bMoving ? static_cast<float>(GetVelocity().Rotation().Yaw) : static_cast<float>(GetActorRotation().Yaw);
	StealthModeRules::UpdateLookLock(LockState, GetActorLocation(), HeadingYaw, Candidates, DeltaSeconds, StealthModeRules::GetConfig(), 50.f);
	StealthAutoLookTargetId = LockState.TargetId;
	StealthAutoLookLocation = LockState.LastKnownLocation;
	StealthAutoLookLostSeconds = LockState.TimeSinceSeenSeconds;
	StealthAutoLookActor = StealthAutoLookTargetId != 0 ? CandidateActors.FindRef(StealthAutoLookTargetId) : nullptr;
}

bool AOperativeCharacter::GetStealthLookPoint(FVector& OutPoint) const
{
	constexpr float LookPointDeadRadiusSq = 50.f * 50.f;
	if (HasStealthManualLookTarget() && FVector::DistSquared2D(GetActorLocation(), StealthManualLookTarget) > LookPointDeadRadiusSq)
	{
		OutPoint = StealthManualLookTarget;
		return true;
	}
	if (HasStealthAutoLookTarget() && FVector::DistSquared2D(GetActorLocation(), StealthAutoLookLocation) > LookPointDeadRadiusSq)
	{
		OutPoint = StealthAutoLookLocation;
		return true;
	}
	return false;
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
	if (Shooter == this && StealthModeRules::ShouldBreak(bStealthMode, true, false, StealthModeRules::GetConfig()))
	{
		SetStealthMode(false);
	}
}

void AOperativeCharacter::UpdateStealthMode()
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	const bool bFormationBlocked = Squad && Squad->IsStealthBlockedByFormation(*this);
	if (bStealthMode && (bFormationBlocked || !HealthComponent || !HealthComponent->IsAlive() || IsCriticallyWounded() || IsKnockedDown()
		|| IsPanicking() || IsRaging() || bCarrying || bVaulting || IsClimbingLadder() || VehicleQueries::IsMounted(this)
		|| StealthModeRules::ShouldBreak(true, false, bSprinting, StealthModeRules::GetConfig())))
	{
		SetStealthMode(false);
	}
	// Feedback owns its own overlay while it flashes. Reclaim only an empty slot; clean up a late restore after stealth ended.
	RefreshStealthVisuals();
}
