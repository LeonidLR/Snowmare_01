// AOperativeCharacter: Ctrl + click targeted shots at objects and the planned (pause) shots.
// Split out of OperativeCharacter.cpp verbatim (cleanup day 2026-10-14; map: docs/port/REFACTOR_OPERATIVE_2026-10.md).
// Godot reference: Scenes/main.gd (Ctrl branch), Scenes/movements/player.gd (shoot_at_barrel / shoot_at_mine /
// shoot_at_crate, set_manual_priority_target).

#include "Characters/OperativeCharacter.h"
#include "Combat/EncounterQueries.h"

#include "Characters/EnemyCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Engine/World.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/LootCrateActor.h"
#include "Interactables/ProximityMineActor.h"
#include "UI/FloatingTextSubsystem.h"
#include "UI/GameMessageSubsystem.h"

#define LOCTEXT_NAMESPACE "OperativeTargetedShots"

namespace
{
	void OperativeShotLine(const AOperativeCharacter& Operative, const FText& Text)
	{
		UWorld* World = Operative.GetWorld();
		if (UGameMessageSubsystem* Messages = World ? World->GetSubsystem<UGameMessageSubsystem>() : nullptr)
		{
			Messages->PostMessage(Operative.DisplayName, Text);
		}
	}
}

ETargetedShotKind AOperativeCharacter::ClassifyShotTarget(const AActor* Target)
{
	if (!IsValid(Target))
	{
		return ETargetedShotKind::None;
	}
	if (Target->ActorHasTag(CombatQueries::EnemyTag))
	{
		const UHealthComponent* Health = Target->FindComponentByClass<UHealthComponent>();
		return Health && Health->IsAlive() ? ETargetedShotKind::Enemy : ETargetedShotKind::None;
	}
	if (Target->IsA<ABarrelActor>())
	{
		return ETargetedShotKind::Barrel;
	}
	if (Target->IsA<AProximityMineActor>())
	{
		return ETargetedShotKind::Mine;
	}
	if (Target->IsA<ALootCrateActor>())
	{
		return ETargetedShotKind::Crate;
	}
	const AInteractableActor* Interactable = Cast<AInteractableActor>(Target);
	return Interactable && Interactable->bTrapped ? ETargetedShotKind::TrappedObject : ETargetedShotKind::None;
}

bool AOperativeCharacter::DetonateShotTarget(AActor* Target, ETargetedShotKind Kind, const FText& ShooterName)
{
	if (!IsValid(Target))
	{
		return false;
	}
	switch (Kind)
	{
	case ETargetedShotKind::Barrel:
		return Cast<ABarrelActor>(Target) && Cast<ABarrelActor>(Target)->Explode(ShooterName);
	case ETargetedShotKind::Mine:
		if (AProximityMineActor* Mine = Cast<AProximityMineActor>(Target))
		{
			Mine->Detonate();
			return true;
		}
		return false;
	case ETargetedShotKind::Crate:
	case ETargetedShotKind::TrappedObject:
		if (AInteractableActor* Object = Cast<AInteractableActor>(Target); Object && Object->bTrapped)
		{
			Object->DetonateTrap(true, ShooterName);
			return true;
		}
		return false;
	default:
		return false;
	}
}

bool AOperativeCharacter::ShootAtObject(AActor* Target)
{
	const ETargetedShotKind Kind = ClassifyShotTarget(Target);
	if (Kind == ETargetedShotKind::None || Kind == ETargetedShotKind::Enemy || !CanBeginWeaponShot())
	{
		return false;
	}
	if (IsSniperWeaponEquipped())
	{
		// Sniper rifle (user request 2026-10-09): she stops / kneels first; the shot waits for her (UpdateSniperPendingShot).
		if (PrepareSniperShot(/*bDirectOrder*/ true) != ESniperFireStep::Ready)
		{
			SniperPendingObject = Target;
			SniperPendingTimer = SniperPendingShotSeconds;
			FaceAimAt(Target->GetActorLocation());
			return false;
		}
		if (!SniperShotAllowed())
		{
			return false;
		}
		if (SniperPendingObject.Get() == Target)
		{
			SniperPendingObject.Reset();
		}
	}
	if (bInCover && !IsCornerShotTarget(Target->GetActorLocation()))
	{
		BeginOpenShotFromCover(Target); // out on the open side: a normal shot off the wall (user decision 2026-10-06)
	}
	if (bInCover)
	{
		// From cover the targeted shot goes round the corner / over the top like any cover shot (user rule 2026-10-06).
		if (!CanFireFromCover())
		{
			return false;
		}
		CoverShotTarget = Target;
		UpdateCoverFacing(/*bAllowSnap*/ false);
		BeginCoverShot();
	}
	else
	{
		StopAndFace(Target->GetActorLocation());
	}

	bool bHit = true;
	FMineShotChance MineShot;
	float DistanceM = 0.f;
	if (Kind == ETargetedShotKind::Mine)
	{
		DistanceM = FVector::Dist(GetActorLocation(), Target->GetActorLocation()) / 100.f;
		MineShot = TargetedShotRules::ComputeMineShotChance(Accuracy, ColdLevel, Stance, DistanceM);
		bHit = bForceHitForTesting || FMath::FRand() * 100.f <= MineShot.Chance;
	}
	NoteShotAim(Target, Target->GetActorLocation(), false);
	OnWeaponFired.Broadcast(this, Target, bHit);
	OnWeaponFiredNative.Broadcast(this, Target, bHit);
	// A targeted shot at a barrel / mine / crate is heard by patrols too (user request 2026-10-06).
	AEnemyCharacter::NotifySquadNoise(GetWorld(), GetActorLocation(), ESquadNoise::Gunshot);

	// Godot tracers: barrel = default green, explosive targets = orange, mine miss = grey into the snow nearby.
	if (UCombatFeedbackSubsystem* Feedback = GetWorld() ? GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>() : nullptr)
	{
		FVector End = Target->GetActorLocation();
		FLinearColor Color(1.f, 0.45f, 0.15f);
		EDamageType Style = EDamageType::Explosive;
		if (Kind == ETargetedShotKind::Barrel)
		{
			Color = UCombatFeedbackSubsystem::DefaultTracerColor();
			Style = EDamageType::Kinetic;
		}
		else if (Kind == ETargetedShotKind::Mine && bHit)
		{
			Color = FLinearColor(1.f, 0.5f, 0.15f);
		}
		else if (Kind == ETargetedShotKind::Mine)
		{
			const float Angle = FMath::FRandRange(0.f, 2.f * PI);
			End += FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * FMath::FRandRange(80.f, 180.f);
			Color = FLinearColor(0.7f, 0.7f, 0.8f, 0.65f);
			Style = EDamageType::Kinetic;
		}
		Feedback->SpawnTracer(GetWeaponMuzzleLocation(), End, Color, Style);
	}

	switch (Kind)
	{
	case ETargetedShotKind::Barrel:
		DetonateShotTarget(Target, Kind, DisplayName);
		break;
	case ETargetedShotKind::Mine:
	{
		const FText Chance = FText::AsNumber(FMath::TruncToInt(MineShot.Chance));
		const FText Distance = FText::FromString(FString::Printf(TEXT("%.1f"), DistanceM));
		if (bHit)
		{
			OperativeShotLine(*this, FText::Format(LOCTEXT("MineHit",
				"💥 {0}: \"Clean shot (Chance: {1}%, {2}, {3}m)! Mine neutralised remotely!\""),
				DisplayName, Chance, MineShot.StanceName, Distance));
			DetonateShotTarget(Target, Kind, DisplayName);
		}
		else
		{
			OperativeShotLine(*this, FText::Format(LOCTEXT("MineMiss",
				"💨 {0}: \"Missed! (Chance was {1}%: range {2}m, {3}). {4} - crouch or move closer!\""),
				DisplayName, Chance, Distance, MineShot.StanceName, TargetedShotRules::GetMineMissReason(Stance, DistanceM, ColdLevel)));
		}
		break;
	}
	case ETargetedShotKind::Crate:
	{
		ALootCrateActor* Crate = CastChecked<ALootCrateActor>(Target);
		if (Crate->bTrapped)
		{
			OperativeShotLine(*this, FText::Format(LOCTEXT("CrateTrapShot", "💥 {0}: \"Shot the crate trap! Remote detonation!\""), DisplayName));
			DetonateShotTarget(Crate, Kind, DisplayName);
		}
		else
		{
			// Only a trap blows up (user decision 2026-09-28: Godot also detonating an untrapped crate is a bug).
			OperativeShotLine(*this, LOCTEXT("CratePierced", "💥 The bullet pierced the supply crate."));
		}
		break;
	}
	case ETargetedShotKind::TrappedObject:
		OperativeShotLine(*this, FText::Format(LOCTEXT("TrappedShot", "💥 {0}: \"Shot the tripwire on the object! Remote detonation!\""), DisplayName));
		DetonateShotTarget(Target, Kind, DisplayName);
		break;
	default:
		break;
	}
	ConsumeAmmoAfterShot();
	return true;
}

void AOperativeCharacter::SetManualPriorityTarget(AActor* Enemy)
{
	if (IsRaging())
	{
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("⚠️ ENRAGED! IGNORING ORDERS!"), FLinearColor(1.f, 0.4f, 0.1f));
		return;
	}
	if (IsPanicking())
	{
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("⚠️ PANICKING! NOT FIRING!"), FLinearColor(1.f, 0.3f, 0.3f));
		return;
	}
	ManualPriorityTarget = Enemy;
	if (Enemy && !bInCover) // in cover the body stays along the wall (user report 2026-10-09: the order snapped her round, then back)
	{
		const FVector Direction = (Enemy->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		if (!Direction.IsNearlyZero())
		{
			SetActorRotation(FRotator(0.f, Direction.Rotation().Yaw, 0.f));
		}
	}
}

void AOperativeCharacter::PlanTargetedShot(AActor* Target)
{
	const ETargetedShotKind Kind = ClassifyShotTarget(Target);
	if (Kind != ETargetedShotKind::None)
	{
		PlannedShots.Add(Kind, Target);
	}
}

bool AOperativeCharacter::ExecutePlannedTargetedShots()
{
	TMap<ETargetedShotKind, TWeakObjectPtr<AActor>> Plans = MoveTemp(PlannedShots);
	PlannedShots.Reset();
	for (const ETargetedShotKind Kind : { ETargetedShotKind::Barrel, ETargetedShotKind::Mine, ETargetedShotKind::Crate, ETargetedShotKind::TrappedObject })
	{
		const TWeakObjectPtr<AActor>* Target = Plans.Find(Kind);
		if (Target && Target->IsValid() && !ShootAtObject(Target->Get()) && ClassifyShotTarget(Target->Get()) == Kind)
		{
			PlannedShots.Add(Kind, *Target); // could not fire yet: the squad retries it (USquadSubsystem::RetryPlannedShots)
		}
	}
	const TWeakObjectPtr<AActor>* Enemy = Plans.Find(ETargetedShotKind::Enemy);
	if (Enemy && Enemy->IsValid())
	{
		SetManualPriorityTarget(Enemy->Get());
	}
	return PlannedShots.IsEmpty();
}

#undef LOCTEXT_NAMESPACE
