// AOperativeCharacter: Stances, stance clips' move hold, the prone dive on the move (root motion), the placeholder
// body.
// Split out of OperativeCharacter.cpp verbatim (cleanup day 2026-10-14; map: docs/port/REFACTOR_OPERATIVE_2026-10.md).
// Godot reference: Scenes/movements/player.gd (set_stance; capsule 2.0 / 1.3 / 0.7 m). Prone dive: UE-only (rev 5e /
// 5f).

#include "Characters/OperativeCharacter.h"

#include "CodexTactics.h"
#include "Animation/AnimSequence.h"
#include "Characters/OperativeAnimInstance.h"
#include "Combat/HealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "Survival/ColdSurvivalComponent.h"
#include "Vehicles/VehicleSubsystem.h"

float AOperativeCharacter::GetStanceChangeDelay(EOperativeStance From, EOperativeStance To) const
{
	const UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	if (!Anim || From == To)
	{
		return 0.f;
	}
	const UAnimSequenceBase* Clip = From == EOperativeStance::Prone
		? (To == EOperativeStance::Crouching ? Anim->ProneToCrouchAnimation.Get() : Anim->ProneToStandAnimation.Get())
		: To == EOperativeStance::Prone ? (From == EOperativeStance::Crouching ? Anim->CrouchToProneAnimation.Get() : Anim->StandToProneAnimation.Get())
		: To == EOperativeStance::Crouching ? Anim->StandToCrouchAnimation.Get() : Anim->CrouchToStandAnimation.Get();
	return Clip ? FMath::Max(0.f, Clip->GetPlayLength() / FMath::Max(Anim->StanceTransitionPlayRate, 0.1f) - Anim->StanceTransitionBlendTime) : 0.f;
}

void AOperativeCharacter::SetStance(EOperativeStance NewStance)
{
	if (Stance == NewStance || IsKnockedDown())
	{
		return; // knocked down: he gets up standing (the get-up clip)
	}
	// Godot: a frostbitten operative physically cannot get up from the snow.
	if (ColdSurvival && ColdSurvival->IsFrostbitten() && NewStance != EOperativeStance::Prone)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("%s: frostbitten, cannot leave prone"), *DisplayName.ToString());
		return;
	}
	// User decision 2026-10-01: getting up from prone needs a full stop — a crawling operative stops, then rises in place.
	if (Stance == EOperativeStance::Prone && NewStance != EOperativeStance::Prone
		&& (bHasMoveOrder || GetVelocity().SizeSquared2D() > 100.f))
	{
		StopOperative();
	}
	const EOperativeStance OldStance = Stance;
	// The stance clip starts on the next anim update: hold moves for its length from now (not at the wall — the cover clips
	// carry their own timing through the anim instance).
	StanceSettleUntil = GetWorld() && !bInCover ? GetWorld()->GetTimeSeconds() + GetStanceChangeDelay(OldStance, NewStance) : 0.f;
	Stance = NewStance;
	if (Stance != EOperativeStance::Standing)
	{
		bSprinting = false;
	}

	if (HealthComponent)
	{
		float DefMult = 1.0f;
		if (Stance == EOperativeStance::Crouching)
		{
			DefMult = 0.75f;
		}
		else if (Stance == EOperativeStance::Prone)
		{
			DefMult = 0.50f;
		}
		HealthComponent->SetDefenseMultiplier(DefMult);
	}

	ApplyStanceCapsule();
	ApplyMovementParams();
	UE_LOG(LogCodexTactics, Display, TEXT("%s: stance %s -> %s"), *DisplayName.ToString(),
		*GetStanceDisplayName(OldStance).ToString(), *GetStanceDisplayName(Stance).ToString());
	OnStanceChanged.Broadcast(this, OldStance, Stance);
	ReceiveStanceChanged(OldStance, Stance);
}

EProneEntry AOperativeCharacter::DropProne()
{
	const float Speed = static_cast<float>(GetVelocity().Size2D());
	const bool bBlocked = bInCover || bHasPendingCover || bCarrying || bVaulting || IsKnockedDown() || VehicleQueries::IsMounted(this);
	EProneEntry Entry = OperativeMovementRules::DecideProneEntry(MovementConfig, Stance, Speed, bSprinting, bHasMoveOrder, bBlocked);
	const UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	const EProneDive Dive = Entry == EProneEntry::DiveFromRun ? EProneDive::FromRun : Entry == EProneEntry::DiveFromWalk ? EProneDive::FromWalk : EProneDive::None;
	UAnimSequenceBase* Clip = Anim ? Anim->GetProneDiveClip(Dive) : nullptr;
	if (Dive != EProneDive::None && !Clip)
	{
		Entry = EProneEntry::StopThenProne; // no dive clip (the pack is missing): the old way, stopped first
	}
	else if (Dive != EProneDive::None && !Clip->HasRootMotion())
	{
		// Rev 5f: the dive moves the capsule by the clip's root motion; a clip without it would dive in place and snap back.
		UE_LOG(LogCodexTactics, Warning, TEXT("%s: dive clip %s has no root motion (enable Root Motion on it, or use the RM copies of "
			"Scripts/Editor/create_prone_dive_root_motion_clips.py): stop, then prone"), *DisplayName.ToString(), *Clip->GetName());
		Entry = EProneEntry::StopThenProne;
	}
	switch (Entry)
	{
	case EProneEntry::Refused:
		return Entry;
	case EProneEntry::StopThenProne:
		StopOperative();
		SetStance(EOperativeStance::Prone);
		break;
	case EProneEntry::InPlace:
		SetStance(EOperativeStance::Prone);
		break;
	default:
	{
		// Rev 5f: the dive is ROOT MOTION (the clip carries the capsule through the movement component; the body never leaves it).
		// He faces his run; the anim instance sees ProneDive with the new stance and plays the clip on the full-body slot.
		const FVector Direction = GetVelocity().GetSafeNormal2D().IsNearlyZero() ? GetActorForwardVector().GetSafeNormal2D() : GetVelocity().GetSafeNormal2D();
		const float Rate = FMath::Max(Anim->StanceTransitionPlayRate, 0.1f);
		ProneDive = Dive;
		ProneDiveClip = Clip;
		ProneDiveLength = Clip->GetPlayLength() / Rate;
		ProneDiveStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		bProneDiveHadOrder = bHasMoveOrder;
		ProneDiveGoal = LastMoveDestination;
		SetActorRotation(FRotator(0.f, Direction.Rotation().Yaw, 0.f));
		SetStance(EOperativeStance::Prone);
		StanceSettleUntil = 0.f; // the dive itself moves him: no stand-still hold
		ProneDiveStart = GetActorLocation();
		// How far the clip carries him (its root track through the mesh transform, as the movement component applies it) and how
		// fast it enters; stretched by his momentum, shortened by the navmesh ahead (an obstacle / a ledge: a shorter slide).
		const FTransform MeshTransform = GetMesh()->GetComponentTransform();
		const float ClipTravel = static_cast<float>(MeshTransform.TransformVector(GetProneDiveRoot(Clip->GetPlayLength())).Size2D());
		constexpr float EntryWindow = 0.2f;
		const float ClipEntrySpeed = static_cast<float>(MeshTransform.TransformVector(GetProneDiveRoot(EntryWindow)).Size2D()) / EntryWindow * Rate;
		const float WantedTravel = ClipTravel * FMath::Max(1.f, MovementConfig.ProneDiveMaxMomentumScale);
		float AllowedTravel = WantedTravel;
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		FNavLocation StartOnNav;
		if (Nav && Nav->ProjectPointToNavigation(ProneDiveStart, StartOnNav, FVector(60.f, 60.f, 250.f)))
		{
			FVector Hit;
			const FVector Wanted = StartOnNav.Location + Direction * WantedTravel;
			if (Nav->NavigationRaycast(GetWorld(), StartOnNav.Location, Wanted, Hit))
			{
				AllowedTravel = static_cast<float>(FVector::Dist2D(StartOnNav.Location, Hit)) - GetCapsuleComponent()->GetScaledCapsuleRadius();
			}
		}
		const float Scale = OperativeMovementRules::ComputeProneDiveTranslationScale(Speed, ClipEntrySpeed, MovementConfig.ProneDiveMaxMomentumScale,
			ClipTravel, AllowedTravel);
		ProneDiveSavedRootMotionScale = GetAnimRootMotionTranslationScale();
		SetAnimRootMotionTranslationScale(Scale);
		bProneDiveSavedWalkOffLedges = GetCharacterMovement()->bCanWalkOffLedges;
		GetCharacterMovement()->bCanWalkOffLedges = false;
		UE_LOG(LogCodexTactics, Display, TEXT("%s: prone dive from %s at %.0f cm/s (%s, %.2f s; clip %.0f cm entering at %.0f cm/s, x%.2f -> %.0f cm, "
			"allowed %.0f cm; %s)"), *DisplayName.ToString(), Dive == EProneDive::FromRun ? TEXT("a run") : TEXT("a walk"), Speed, *Clip->GetName(),
			ProneDiveLength, ClipTravel, ClipEntrySpeed, Scale, ClipTravel * Scale, AllowedTravel,
			bProneDiveHadOrder ? TEXT("then crawls on if the goal is ahead") : TEXT("then stays"));
		break;
	}
	}
	return Entry;
}

FVector AOperativeCharacter::GetProneDiveRoot(float Time) const
{
	const UAnimSequence* Sequence = Cast<UAnimSequence>(ProneDiveClip.Get());
	if (!Sequence)
	{
		return FVector::ZeroVector;
	}
	const FAnimExtractContext Start(0.0);
	const FAnimExtractContext Now(static_cast<double>(FMath::Clamp(Time, 0.f, Sequence->GetPlayLength())));
	return Sequence->ExtractRootTrackTransform(Now, nullptr).GetTranslation() - Sequence->ExtractRootTrackTransform(Start, nullptr).GetTranslation();
}

void AOperativeCharacter::UpdateProneDive(float DeltaSeconds)
{
	if (ProneDive == EProneDive::None)
	{
		return;
	}
	const UWorld* World = GetWorld();
	const UOperativeAnimInstance* Anim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	const float Elapsed = World ? static_cast<float>(World->GetTimeSeconds() - ProneDiveStartTime) : ProneDiveLength;
	// The clip starts on the next anim update; once it has stopped playing (or its time is up) the root motion is over.
	const bool bClipPlaying = Anim && Anim->GetStanceTransitionPosition(ProneDiveClip.Get()) >= 0.f;
	if (Stance != EOperativeStance::Prone || IsKnockedDown() || bInCover || !ProneDiveClip.IsValid() || Elapsed >= ProneDiveLength + 0.1f
		|| (Elapsed > 0.3f && !bClipPlaying))
	{
		EndProneDive();
	}
}

void AOperativeCharacter::EndProneDive()
{
	if (ProneDive == EProneDive::None)
	{
		return;
	}
	ProneDive = EProneDive::None;
	ProneDiveClip.Reset();
	SetAnimRootMotionTranslationScale(ProneDiveSavedRootMotionScale);
	GetCharacterMovement()->bCanWalkOffLedges = bProneDiveSavedWalkOffLedges;
	ApplyMovementParams(); // the crawl speed for the rest of the move
	const float Travel = static_cast<float>(FVector::Dist2D(ProneDiveStart, GetActorLocation()));
	const bool bCrawlOn = bHasMoveOrder && OperativeMovementRules::ShouldCrawlOnAfterDive(ProneDiveStart, GetActorLocation(), ProneDiveGoal,
		MovementConfig.AcceptanceRadius);
	if (bCrawlOn)
	{
		RequestMove(ProneDiveGoal); // a fresh path from where the dive ended (it may have slid past a corner of the old one)
	}
	else if (bHasMoveOrder)
	{
		StopOperative(); // the dive carried him to / past the goal
	}
	UE_LOG(LogCodexTactics, Display, TEXT("%s: prone dive done after %.0f cm (%s)"), *DisplayName.ToString(), Travel,
		bCrawlOn ? TEXT("crawling on to the goal") : TEXT("lying where the dive ended"));
}

const FOperativeStanceShape& AOperativeCharacter::GetStanceShape(EOperativeStance InStance) const
{
	switch (InStance)
	{
	case EOperativeStance::Crouching: return CrouchingShape;
	case EOperativeStance::Prone: return ProneShape;
	default: return StandingShape;
	}
}

FText AOperativeCharacter::GetStanceDisplayName(EOperativeStance InStance)
{
	switch (InStance)
	{
	case EOperativeStance::Crouching: return NSLOCTEXT("CodexTactics", "StanceCrouching", "CROUCHED");
	case EOperativeStance::Prone: return NSLOCTEXT("CodexTactics", "StanceProne", "PRONE");
	default: return NSLOCTEXT("CodexTactics", "StanceStanding", "STANDING");
	}
}

bool AOperativeCharacter::UsesPlaceholderBody() const
{
	const USkeletalMeshComponent* SkeletalMesh = GetMesh();
	return !SkeletalMesh || !SkeletalMesh->GetSkeletalMeshAsset();
}

void AOperativeCharacter::UpdatePlaceholderVisibility()
{
	const bool bPlaceholder = UsesPlaceholderBody();
	if (BodyMesh)
	{
		BodyMesh->SetVisibility(bPlaceholder);
		BodyMesh->SetCollisionEnabled(bPlaceholder ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
	if (FacingMarker)
	{
		FacingMarker->SetVisibility(bPlaceholder);
	}
	if (WeaponMesh)
	{
		WeaponMesh->SetVisibility(!bPlaceholder);
	}
}

void AOperativeCharacter::ApplyStanceCapsule()
{
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	const float OldHalf = Capsule->GetUnscaledCapsuleHalfHeight();
	const float NewHalf = FMath::Max(GetStanceShape(Stance).CapsuleHalfHeight, Capsule->GetUnscaledCapsuleRadius());
	if (!FMath::IsNearlyEqual(OldHalf, NewHalf))
	{
		Capsule->SetCapsuleHalfHeight(NewHalf);
		// Keep the feet where they were.
		AddActorWorldOffset(FVector(0.f, 0.f, (NewHalf - OldHalf) * Capsule->GetShapeScale()), false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (bMeshBaseCaptured)
	{
		FVector MeshLocation = GetMesh()->GetRelativeLocation();
		MeshLocation.Z = MeshBaseZ + (StandingShape.CapsuleHalfHeight - NewHalf);
		GetMesh()->SetRelativeLocation(MeshLocation);
	}
}

void AOperativeCharacter::UpdatePlaceholderPose(float Alpha)
{
	if (!BodyMesh || !FacingMarker || !BodyMesh->IsVisible())
	{
		return;
	}
	const FOperativeStanceShape& Shape = GetStanceShape(Stance);
	const float FeetZ = -GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	const FVector BodyLocation(0.f, 0.f, FeetZ + Shape.PlaceholderCenterHeight);
	const FVector MarkerLocation = Shape.MarkerOffset + FVector(0.f, 0.f, FeetZ);
	const FQuat BodyRotation = FQuat::Slerp(BodyMesh->GetRelativeRotation().Quaternion(), Shape.PlaceholderRotation.Quaternion(), Alpha);
	BodyMesh->SetRelativeLocationAndRotation(FMath::Lerp(BodyMesh->GetRelativeLocation(), BodyLocation, Alpha), BodyRotation);
	BodyMesh->SetRelativeScale3D(FMath::Lerp(BodyMesh->GetRelativeScale3D(), Shape.PlaceholderScale, Alpha));
	FacingMarker->SetRelativeLocation(FMath::Lerp(FacingMarker->GetRelativeLocation(), MarkerLocation, Alpha));
}
