// AOperativeCharacter: Selection ring, see-through silhouette, vision cone.
// Split out of OperativeCharacter.cpp verbatim (cleanup day 2026-10-14; map: docs/port/REFACTOR_OPERATIVE_2026-10.md).
// Godot reference: Scenes/movements/player.gd (set_group_selected, enable_silhouette / _check_silhouette_occlusion,
// _setup_vision_cone / _update_vision_cone / _is_any_enemy_spotted).

#include "Characters/OperativeCharacter.h"

#include "Camera/PlayerCameraManager.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/SightRules.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Combat/VisionConeComponent.h"
#include "Combat/VisionConeRules.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/RadiusRingSubsystem.h"
#include "Materials/MaterialInstanceDynamic.h"

float AOperativeCharacter::GetVisionConeRange() const
{
	if (!bVisionConeRangeFromWeapon || !CurrentWeapon)
	{
		return VisionConeRangeCm;
	}
	// What FindBestCombatTarget can reach on flat ground (Godot: the cone range was the M16 range, 14 m).
	return CurrentWeapon->AttackRangeCm * SquadFireRules::GetPostureRangeMultiplier(Stance);
}

bool AOperativeCharacter::IsEnemySpottedInCone() const
{
	if (GetCurrentCombatTarget() || GetManualPriorityTarget())
	{
		return true;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const float Range = GetVisionConeRange();
	const FVector Origin = GetActorLocation();
	const FVector Forward = GetActorForwardVector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VisionConeSpot), false, this);
	if (const USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
	{
		for (AOperativeCharacter* Member : Squad->GetMembers())
		{
			Params.AddIgnoredActor(Member);
		}
	}
	for (AEnemyCharacter* Registered : UEnemyRegistrySubsystem::GetEnemyCharacters(World))
	{
		const AEnemyCharacter* Enemy = Registered;
		if (Enemy->IsDying() || !Enemy->GetHealthComponent() || !Enemy->GetHealthComponent()->IsAlive()
			|| !VisionConeRules::IsInsideCone(Origin, Forward, Enemy->GetActorLocation(), Range, VisionConeHalfAngleDeg))
		{
			continue;
		}
		// Godot: a ray from the eyes to the enemy's body; a barricade still counts as seen unless prone.
		FHitResult Hit;
		const FVector Eye = UTacticalSightSubsystem::EyePoint(*this);
		const FVector Body = Enemy->GetActorLocation() + FVector(0.f, 0.f, 60.f);
		if (!World->LineTraceSingleByChannel(Hit, Eye, Body, ECC_Visibility, Params) || Hit.GetActor() == Enemy
			|| Cast<APawn>(Hit.GetActor()) || (Hit.GetActor() && Hit.GetActor()->IsA<ABarricadeActor>() && Stance != EOperativeStance::Prone))
		{
			return true;
		}
	}
	return false;
}

void AOperativeCharacter::UpdateVisionCone(float DeltaTime)
{
	if (!VisionCone)
	{
		return;
	}
	const bool bAlive = HealthComponent && HealthComponent->IsAlive();
	const bool bVisible = VisionConeRules::ResolveVisible(UVisionConeComponent::GetGlobalMode(), bShowVisionCone, bAlive);
	if (bVisible)
	{
		VisionConeCheckTimer -= DeltaTime;
		if (VisionConeCheckTimer <= 0.f)
		{
			VisionConeCheckTimer = 0.2f;
			bVisionConeSpotted = IsEnemySpottedInCone();
		}
	}
	const FLinearColor Color = VisionConeRules::GetStateColor(bVisionConeSpotted ? EVisionConeState::Spotted : EVisionConeState::Clear);
	// Occluded cone (game designer 2026-10-19): the squad's sight line (UTacticalSightSubsystem) from his eyes (stance height) to
	// a man-sized enemy's profile (150 cm); one level, one tone.
	FVisionConeSightSpec Sight;
	Sight.EyeHeightCm = SightRules::EyeHeight(Stance);
	Sight.Levels.Add({ SightRules::ProfileHeight(EOperativeStance::Standing), GetVisionConeRange() });
	VisionCone->UpdateCone(bVisible, GetVisionConeRange(), VisionConeHalfAngleDeg, Color, DeltaTime, &Sight);
}

void AOperativeCharacter::SetGroupSelected(bool bSelected, bool bInMultiSelectionIn)
{
	bGroupSelected = bSelected;
	bInMultiSelection = bSelected && bInMultiSelectionIn;
	UpdateSelectionRing();
}

bool AOperativeCharacter::IsSelectionRingShown() const
{
	return SelectionRing && !SelectionRing->IsHidden();
}

void AOperativeCharacter::UpdateSelectionRing()
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	const bool bLeader = Squad && Squad->GetLeader() == this;
	// Sprint 06-A (user / architect 2026-10-04; Godot showed it only on group members): the active leader always has
	// his gold ring, every selected operative a cyan one — in exploration, preparation and the fight.
	const bool bShow = (bLeader || bGroupSelected) && bRecruited && HealthComponent && HealthComponent->IsAlive();
	if (!bShow)
	{
		if (SelectionRing)
		{
			SelectionRing->SetActorHiddenInGame(true);
		}
		return;
	}
	if (!SelectionRing)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SelectionRing = GetWorld()->SpawnActor<ARadiusRingActor>(Params);
	}
	if (SelectionRing)
	{
		// Godot torus 0.55..0.65 m, 5 cm above the feet; leader gold (1, 0.85, 0.2), others cyan (0.3, 0.9, 1).
		const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight() - 5.f);
		const FLinearColor Color = bLeader ? FLinearColor(1.f, 0.85f, 0.2f) : FLinearColor(0.3f, 0.9f, 1.f);
		SelectionRing->ShowRing(Feet, 60.f, Color, 10.f);
		SelectionRing->SetActorHiddenInGame(false);
	}
}

FLinearColor AOperativeCharacter::GetSilhouetteColor() const
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	auto Godot = [](float R, float G, float B, float A)
	{
		FLinearColor Color = FLinearColor::FromSRGBColor(FColor(FMath::RoundToInt(R * 255.f), FMath::RoundToInt(G * 255.f), FMath::RoundToInt(B * 255.f)));
		Color.A = A;
		return Color;
	};
	if (Squad && Squad->GetLeader() == this)
	{
		return Godot(0.2f, 0.85f, 1.f, 0.55f);
	}
	switch (SquadRole)
	{
	case EOperativeRole::Engineer: return Godot(1.f, 0.55f, 0.15f, 0.55f);
	case EOperativeRole::MedicSapper: return Godot(0.2f, 0.9f, 0.35f, 0.55f);
	case EOperativeRole::Recruit: return Godot(0.3f, 0.9f, 0.5f, 0.55f);
	default: return Godot(1.f, 0.45f, 0.18f, 0.5f);
	}
}

void AOperativeCharacter::UpdateSilhouette(float DeltaTime)
{
	if (!bEnableSilhouette || !HealthComponent || !HealthComponent->IsAlive())
	{
		SetSilhouetteVisible(false);
		return;
	}
	if (!bSilhouetteOcclusionOnly)
	{
		SetSilhouetteVisible(true);
		return;
	}
	SilhouetteCheckTimer -= DeltaTime;
	if (SilhouetteCheckTimer > 0.f)
	{
		return;
	}
	SilhouetteCheckTimer = 0.05f;
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}
	const FVector CameraLocation = PC->PlayerCameraManager->GetCameraLocation();
	// Godot global_position + 1 m: the operative's centre is 1 m above the feet.
	const FVector Centre = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight() - 100.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OperativeSilhouette), false, this);
	FHitResult Hit;
	const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, CameraLocation, Centre, ECC_Visibility, Params)
		&& FVector::Dist(CameraLocation, Hit.ImpactPoint) < FVector::Dist(CameraLocation, Centre) - 35.f;
	SetSilhouetteVisible(bBlocked);
	if (bBlocked && SilhouetteMID)
	{
		SilhouetteMID->SetVectorParameterValue(TEXT("Color"), GetSilhouetteColor()); // the leader may change
	}
}

void AOperativeCharacter::SetSilhouetteVisible(bool bVisible)
{
	if (bSilhouetteVisible == bVisible)
	{
		return;
	}
	if (bVisible && !SilhouetteMID)
	{
		UMaterialInterface* Base = SilhouetteMaterial ? SilhouetteMaterial.Get()
			: LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/VFX/Materials/M_Silhouette.M_Silhouette"));
		if (!Base)
		{
			return;
		}
		SilhouetteMID = UMaterialInstanceDynamic::Create(Base, this);
		SilhouetteMID->SetVectorParameterValue(TEXT("Color"), GetSilhouetteColor());
	}
	bSilhouetteVisible = bVisible;
	TInlineComponentArray<UMeshComponent*> Meshes(this);
	for (UMeshComponent* MeshComponent : Meshes)
	{
		if (MeshComponent && MeshComponent->IsVisible())
		{
			MeshComponent->SetOverlayMaterial(bVisible ? SilhouetteMID.Get() : nullptr);
		}
	}
}
