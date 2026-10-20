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
#include "Tactics/CoverFacingRules.h"
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

float AOperativeCharacter::GetVisionConeHalfAngle() const
{
	const UTacticalSightSubsystem* Sight = GetWorld() ? GetWorld()->GetSubsystem<UTacticalSightSubsystem>() : nullptr;
	return Sight && Sight->IsFovRuleActive() ? Sight->GetFovDeg(Stance) * 0.5f : VisionConeHalfAngleDeg;
}

FVector AOperativeCharacter::GetSightFacing() const
{
	if (bInCover && CurrentCoverHeight != ECoverHeight::None && !bShimmying)
	{
		if (bCornerAimActive || IsCoverFireReady())
		{
			return CoverFacingRules::CornerAimDirection(CoverSlot, CoverFacing, CornerAimOutwardDeg); // over the cover, the way the stance aims
		}
		return CoverFacingRules::AlongWallDirection(CoverSlot, CoverFacing).GetSafeNormal2D(); // the look-around pose: along the wall
	}
	return GetActorForwardVector().GetSafeNormal2D();
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
	float Range = GetVisionConeRange();
	float HalfAngle = GetVisionConeHalfAngle();
	FVector Origin = GetActorLocation();
	FVector Forward = GetSightFacing();
	FVector Eye = UTacticalSightSubsystem::EyePoint(*this);
	// Peek sight (2026-10-19): peeking, the cone is the peek cone from the leaned-out eyes.
	FVector PeekEye;
	FVector PeekDirection;
	if (GetPeekSight(PeekEye, PeekDirection))
	{
		Range = PeekSightConfig.RangeCm;
		HalfAngle = PeekSightConfig.HalfAngleDeg;
		Origin = PeekEye;
		Forward = PeekDirection;
		Eye = PeekEye;
	}
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
			|| !VisionConeRules::IsInsideCone(Origin, Forward, Enemy->GetActorLocation(), Range, HalfAngle))
		{
			continue;
		}
		// Godot: a ray from the eyes to the enemy's body; a barricade still counts as seen unless prone.
		FHitResult Hit;
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
	// Shown by the BP / actor setting, or by the player's squad-cone toggle (Shift+O, 2026-10-20); the debug CVar 0 / 1 overrides both.
	const bool bVisible = VisionConeRules::ResolveVisible(UVisionConeComponent::GetGlobalMode(), bShowVisionCone, bAlive,
		UVisionConeComponent::IsPlayerSquadConesOn());
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
	float Range = GetVisionConeRange();
	float HalfAngle = GetVisionConeHalfAngle();
	// Peek sight (game designer 2026-10-19): leaned out round a corner / over a low wall the cone is the limited peek cone, drawn from
	// the leaned-out eye point in the pose's aim (what UTacticalSightSubsystem::CanOperativeSee reveals).
	FVector PeekEye;
	FVector PeekDirection;
	if (bVisible && GetPeekSight(PeekEye, PeekDirection))
	{
		const FVector Feet = GetActorLocation() - FVector(0.f, 0.f, GetSimpleCollisionHalfHeight());
		VisionCone->SetViewOverride(PeekEye - Feet, static_cast<float>(PeekDirection.Rotation().Yaw));
		Sight.EyeHeightCm = static_cast<float>(PeekEye.Z - Feet.Z);
		Range = PeekSightConfig.RangeCm;
		HalfAngle = PeekSightConfig.HalfAngleDeg;
	}
	else if (bVisible && bInCover && !GetSightFacing().Equals(GetActorForwardVector().GetSafeNormal2D(), 0.01f))
	{
		// In cover he looks along the wall / over it, not the way his back-to-the-wall body faces: the cone follows his sight (origin on the capsule).
		VisionCone->SetViewOverride(FVector::ZeroVector, static_cast<float>(GetSightFacing().Rotation().Yaw));
	}
	else
	{
		VisionCone->ClearViewOverride();
	}
	Sight.Levels.Add({ SightRules::ProfileHeight(EOperativeStance::Standing), Range });
	VisionCone->UpdateCone(bVisible, Range, HalfAngle, Color, DeltaTime, &Sight);
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
