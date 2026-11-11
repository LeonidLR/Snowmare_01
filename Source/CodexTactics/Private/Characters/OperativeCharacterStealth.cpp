// Per-operative stealth, designer task 003. UE-only; no Godot reference.
// Reuses the existing formation ghost material at runtime; source materials and assets are never changed.
#include "Characters/OperativeCharacter.h"

#include "Characters/StealthModeRules.h"
#include "Combat/HealthComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Vehicles/VehicleSubsystem.h"

bool AOperativeCharacter::CanEnterStealthMode() const
{
	return StealthModeRules::GetConfig().bEnabled && HealthComponent && HealthComponent->IsAlive() && !IsCriticallyWounded()
		&& !IsKnockedDown() && !IsPanicking() && !IsRaging() && !bCarrying && !bVaulting && !IsClimbingLadder() && !VehicleQueries::IsMounted(this);
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
		TArray<UMeshComponent*> Meshes;
		GetComponents(Meshes);
		for (UMeshComponent* BodyPart : Meshes)
		{
			const bool bBodyOrWeapon = BodyPart->IsA<USkeletalMeshComponent>() || BodyPart == BodyMesh.Get() || BodyPart == WeaponMesh.Get()
				|| WeaponMeshParts.ContainsByPredicate([BodyPart](const TObjectPtr<UStaticMeshComponent>& Part) { return Part.Get() == BodyPart; });
			if (!bBodyOrWeapon)
			{
				continue; // facing markers, rings and vision meshes keep their own visuals
			}
			StealthMeshes.Add(BodyPart);
			StealthMaterialCounts.Add(BodyPart->GetNumMaterials());
			for (int32 Slot = 0; Slot < BodyPart->GetNumMaterials(); ++Slot)
			{
				StealthOriginalMaterials.Add(BodyPart->GetMaterial(Slot));
			}
		}
		SetSprinting(false);
		bStealthMode = true;
		SetStance(EOperativeStance::Crouching);
	}
	else
	{
		bStealthMode = false;
	}
	RefreshStealthVisuals();
	return true;
}

void AOperativeCharacter::RefreshStealthVisuals()
{
	if (!StealthMaterial)
	{
		return;
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
					StealthOriginalMaterials[Saved] = Current; // a weapon/mesh swap keeps its latest original material
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
	if (bStealthMode && (!HealthComponent || !HealthComponent->IsAlive() || IsCriticallyWounded() || IsKnockedDown()
		|| IsPanicking() || IsRaging() || bCarrying || bVaulting || IsClimbingLadder() || VehicleQueries::IsMounted(this)
		|| Stance == EOperativeStance::Prone || StealthModeRules::ShouldBreak(true, false, bSprinting, StealthModeRules::GetConfig())))
	{
		SetStealthMode(false);
	}
	// Feedback owns its own overlay while it flashes. Reclaim only an empty slot; clean up a late restore after stealth ended.
	RefreshStealthVisuals();
}
