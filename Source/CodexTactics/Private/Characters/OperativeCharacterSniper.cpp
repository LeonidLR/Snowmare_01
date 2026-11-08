// Sniper rifle handling and per-role body of AOperativeCharacter (user request 2026-10-09; UE-only, no Godot reference).
// Pure rules: Combat/SniperRules. Animation: UOperativeAnimInstance sniper layer (OperativeAnimInstanceSniper.cpp).

#include "Characters/OperativeCharacter.h"

#include "CodexTactics.h"
#include "Characters/OperativeAnimInstance.h"
#include "Combat/SniperRules.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Data/WeaponRoster.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "UI/FloatingTextSubsystem.h"

namespace
{
	UOperativeAnimInstance* SniperAnimOf(const AOperativeCharacter& Operative)
	{
		const USkeletalMeshComponent* Body = Operative.GetMesh();
		return Body ? Cast<UOperativeAnimInstance>(Body->GetAnimInstance()) : nullptr;
	}
}

bool AOperativeCharacter::IsSniperWeaponEquipped() const
{
	return CurrentWeapon && CurrentWeapon->IsSniperRifle();
}

ESniperFireStep AOperativeCharacter::PrepareSniperShot(bool bDirectOrder)
{
	if (!IsSniperWeaponEquipped())
	{
		return ESniperFireStep::Ready;
	}
	const UOperativeAnimInstance* Anim = SniperAnimOf(*this);
	FSniperFireContext Context;
	Context.Stance = Stance;
	Context.bMoving = IsMoving();
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	// In cover the kneel is the cover layer's cross-blend into the crouched fire stance (no stance clip): it settles too.
	Context.bStanceTransitionPlaying = (Anim && Anim->IsPlayingStanceTransition()) || Now - SniperKneelTime < SniperKneelSettleSeconds;
	Context.bDirectOrder = bDirectOrder;
	const ESniperFireStep Step = SniperRules::NextStep(Context);
	const bool bLine = Now - SniperLastPrepLineTime > 2.0;
	switch (Step)
	{
	case ESniperFireStep::Stop:
		StopOperative();
		++SniperStops;
		SniperHoldUntil = Now + SniperOrderHoldSeconds;
		UE_LOG(LogCodexTactics, Display, TEXT("[Sniper] %s stops to take the shot"), *DisplayName.ToString());
		if (bLine)
		{
			SniperLastPrepLineTime = Now;
			UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("ðŸŽ¯ STOPPING TO AIM"), FLinearColor(0.6f, 0.9f, 1.f));
		}
		break;
	case ESniperFireStep::Kneel:
	{
		const EOperativeStance Old = Stance;
		SetStance(SniperRules::FiringStance(Stance));
		if (Stance != Old)
		{
			SniperKneelTime = Now;
			++SniperKneels;
			UE_LOG(LogCodexTactics, Display, TEXT("[Sniper] %s kneels for the shot (%s)"), *DisplayName.ToString(),
				bDirectOrder ? TEXT("order") : TEXT("fire posture"));
			if (bLine)
			{
				SniperLastPrepLineTime = Now;
				UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("ðŸŽ¯ KNEELING TO FIRE"), FLinearColor(0.6f, 0.9f, 1.f));
			}
		}
		if (bDirectOrder)
		{
			SniperHoldUntil = Now + SniperOrderHoldSeconds;
		}
		break;
	}
	default:
		if (bDirectOrder)
		{
			SniperHoldUntil = Now + SniperOrderHoldSeconds;
		}
		break;
	}
	return Step;
}

bool AOperativeCharacter::SniperShotAllowed()
{
	if (!IsSniperWeaponEquipped())
	{
		return true;
	}
	if (!SniperRules::CanFireNow(Stance, IsMoving()))
	{
		++SniperRefusedShots;
		UE_LOG(LogCodexTactics, Display, TEXT("[Sniper] %s: shot refused (%s, moving %d)"), *DisplayName.ToString(),
			*GetStanceDisplayName(Stance).ToString(), IsMoving() ? 1 : 0);
		return false;
	}
	++SniperShots;
	return true;
}

bool AOperativeCharacter::IsHoldingForSniperShot() const
{
	const UWorld* World = GetWorld();
	return IsSniperWeaponEquipped() && World && World->GetTimeSeconds() < SniperHoldUntil;
}

void AOperativeCharacter::UpdateSniperPendingShot(float DeltaTime)
{
	AActor* Target = SniperPendingObject.Get();
	if (!Target)
	{
		SniperPendingObject.Reset();
		return;
	}
	SniperPendingTimer -= DeltaTime;
	if (!IsSniperWeaponEquipped() || SniperPendingTimer <= 0.f || ClassifyShotTarget(Target) == ETargetedShotKind::None)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Sniper] %s: pending shot at %s dropped"), *DisplayName.ToString(), *Target->GetName());
		SniperPendingObject.Reset();
		return;
	}
	if (ShootAtObject(Target))
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Sniper] %s: pending shot at %s fired"), *DisplayName.ToString(), *Target->GetName());
		SniperPendingObject.Reset();
		// The same target may also be planned (tactical pause): it is done now.
		for (auto It = PlannedShots.CreateIterator(); It; ++It)
		{
			if (It.Value().Get() == Target)
			{
				It.RemoveCurrent();
			}
		}
	}
}

void AOperativeCharacter::PlaySniperGridShot()
{
	if (UOperativeAnimInstance* Anim = SniperAnimOf(*this); Anim && IsSniperWeaponEquipped())
	{
		++SniperShots;
		Anim->PlaySniperShot(/*bCycleBolt*/ true);
	}
}

void AOperativeCharacter::AddArsenalWeapon(UWeaponDataAsset* Weapon, int32 Reserve)
{
	if (!Weapon || AvailableWeapons.Contains(Weapon) || WeaponRoster::IsOutOfPlay(Weapon->WeaponId)) // the designer's roster (2026-11-08)
	{
		return;
	}
	// Right after the main rifle: X goes M16 -> this weapon -> pistol -> ... (the grenade aim stays further down the cycle).
	AvailableWeapons.Insert(Weapon, FMath::Min(1, AvailableWeapons.Num()));
	FWeaponAmmoState& Ammo = AmmoInventory.FindOrAdd(Weapon->WeaponId);
	Ammo.Clip = Weapon->Stats.Ammo.MagazineSize;
	Ammo.Reserve = Reserve < 0 ? FMath::Max(0, Weapon->Stats.Ammo.ReserveAmmo) : Reserve;
	if (const int32* Extra = ExtraAmmo.Find(FName(*Weapon->WeaponId)))
	{
		Ammo.Reserve += *Extra;
		ExtraAmmo.Remove(FName(*Weapon->WeaponId));
	}
	UE_LOG(LogCodexTactics, Display, TEXT("%s: arsenal + %s [%d / %d]"), *DisplayName.ToString(), *Weapon->WeaponId, Ammo.Clip, Ammo.Reserve);
}

void AOperativeCharacter::ApplyBodyMeshOverride(USkeletalMesh* BodyAsset)
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!BodyAsset || !Body || Body->GetSkeletalMeshAsset() == BodyAsset)
	{
		return;
	}
	Body->SetSkeletalMeshAsset(BodyAsset);
	Body->EmptyOverrideMaterials(); // the Blueprint's overrides are per slot index of the old mesh
	UpdatePlaceholderVisibility();
	UE_LOG(LogCodexTactics, Display, TEXT("%s: body mesh %s"), *DisplayName.ToString(), *BodyAsset->GetName());
}

void AOperativeCharacter::ApplyWeaponVisual()
{
	if (!WeaponMesh || !bDefaultWeaponMeshCaptured)
	{
		return;
	}
	UStaticMesh* HandAsset = DefaultWeaponMeshAsset;
	FTransform Transform = DefaultWeaponMeshTransform;
	USkeletalMesh* SkeletalAsset = CurrentWeapon && !CurrentWeapon->HandSkeletalMesh.IsNull() ? CurrentWeapon->HandSkeletalMesh.LoadSynchronous() : nullptr;
	if (SkeletalAsset)
	{
		HandAsset = nullptr; // the skeletal model (the D-1) rides on WeaponMesh: no static model under it
		if (CurrentWeapon->bOverrideHandMeshTransform)
		{
			Transform = CurrentWeapon->HandMeshAttachTransform;
		}
	}
	else if (CurrentWeapon && !CurrentWeapon->HandMesh.IsNull())
	{
		if (UStaticMesh* Own = CurrentWeapon->HandMesh.LoadSynchronous())
		{
			HandAsset = Own;
		}
		if (CurrentWeapon->bOverrideHandMeshTransform)
		{
			Transform = CurrentWeapon->HandMeshAttachTransform;
		}
	}
	if (WeaponSkeletalMesh && WeaponSkeletalMesh->GetSkeletalMeshAsset() != SkeletalAsset)
	{
		WeaponSkeletalMesh->SetSkeletalMeshAsset(SkeletalAsset);
		WeaponSkeletalMesh->SetRelativeTransform(FTransform::Identity);
	}
	if (WeaponMesh->GetStaticMesh() != HandAsset)
	{
		WeaponMesh->SetStaticMesh(HandAsset);
	}
	if (!WeaponMesh->GetRelativeTransform().Equals(Transform, 0.01f))
	{
		WeaponMesh->SetRelativeTransform(Transform);
	}
	// The extra parts (the pistol's magazine): one component each under WeaponMesh at identity, the spares emptied.
	const int32 PartCount = HandAsset && CurrentWeapon ? CurrentWeapon->HandMeshParts.Num() : 0;
	while (WeaponMeshParts.Num() < PartCount)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		Part->SetupAttachment(WeaponMesh);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(WeaponMesh->CastShadow);
		Part->RegisterComponent();
		Part->AttachToComponent(WeaponMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		Part->SetRelativeTransform(FTransform::Identity);
		WeaponMeshParts.Add(Part);
	}
	for (int32 Index = 0; Index < WeaponMeshParts.Num(); ++Index)
	{
		if (UStaticMeshComponent* Part = WeaponMeshParts[Index])
		{
			UStaticMesh* PartMesh = Index < PartCount ? CurrentWeapon->HandMeshParts[Index].LoadSynchronous() : nullptr;
			if (Part->GetStaticMesh() != PartMesh)
			{
				Part->SetStaticMesh(PartMesh);
			}
		}
	}
}

void AOperativeCharacter::PlayShotClip(AActor* Target, bool bHit)
{
	if (IsSniperWeaponEquipped())
	{
		++SniperShots;
	}
	if (UOperativeAnimInstance* Anim = SniperAnimOf(*this))
	{
		Anim->PlayShotClip(this, Target, bHit);
	}
}

float AOperativeCharacter::GetBodyYawLagDeg() const
{
	if (bCoverEntryAwaitClip)
	{
		return CoverEntryYawOffset;
	}
	if (CoverEntryYawTime >= 0.f)
	{
		// The blend this frame (UpdateCoverEntryBlend may not have run since the turn started).
		const float W = FMath::Clamp(CoverEntryYawTime / FMath::Max(CoverEntryYawBlendActive, 0.05f), 0.f, 1.f);
		return CoverEntryYawOffset * (1.f - FMath::SmoothStep(0.f, 1.f, W));
	}
	return CoverEntryAppliedYaw;
}
