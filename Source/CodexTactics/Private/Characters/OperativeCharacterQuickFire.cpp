// AOperativeCharacter: quick fire (game designer request 2026-10-19; Ctrl + click on a point with nothing to aim at, user decision
// 2026-10-19) - turn to the clicked point and fire a short burst at it now (area fire; the normal aimed shot at an Enemy passed
// in). From cover: the corner shot when the point is round the corner, else the open shot (as a Ctrl + click targeted shot).
// Pure rules: Combat/QuickFireRules. UE-only, no Godot reference. Cluster map: docs/port/REFACTOR_OPERATIVE_2026-10.md.

#include "Characters/OperativeCharacter.h"

#include "CodexTactics.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacterInternal.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/QuickFireRules.h"
#include "Combat/SuppressionSubsystem.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Survival/ColdSurvivalComponent.h"
#include "UI/FloatingTextSubsystem.h"
#include "Vehicles/VehicleSubsystem.h"

bool AOperativeCharacter::CanTakeQuickFire(bool bExplain)
{
	if (!bRecruited || !HealthComponent || !HealthComponent->IsAlive() || IsKnockedDown() || VehicleQueries::IsMounted(this))
	{
		return false;
	}
	if (IsRaging())
	{
		if (bExplain)
		{
			UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("⚠️ ENRAGED! IGNORING ORDERS!"), FLinearColor(1.f, 0.4f, 0.1f));
		}
		return false;
	}
	if (IsPanicking())
	{
		if (bExplain)
		{
			UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("⚠️ PANICKING! NOT FIRING!"), FLinearColor(1.f, 0.3f, 0.3f));
		}
		return false;
	}
	if (!UsesAmmo())
	{
		if (bExplain)
		{
			UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("🔪 KNIFE IN HANDS - NO FIRE"), FLinearColor(0.85f, 0.85f, 0.85f));
		}
		return false;
	}
	if (ColdSurvival && ColdSurvival->IsWeaponFrozen())
	{
		if (bExplain)
		{
			NotifyWeaponFrozen();
		}
		return false;
	}
	if (bCarrying)
	{
		if (bExplain)
		{
			UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("✋ HANDS BUSY"), FLinearColor(0.85f, 0.85f, 0.85f));
		}
		return false;
	}
	return true;
}

bool AOperativeCharacter::OrderQuickFire(const FVector& Point, AActor* Enemy)
{
	if (!CanTakeQuickFire(true))
	{
		return false;
	}
	const FQuickFireConfig& Config = QuickFireRules::GetConfig();
	const int32 Rounds = QuickFireRules::GetBurstRounds(UsesAmmo(), IsSniperWeaponEquipped(), CurrentWeapon ? CurrentWeapon->MaxClipSize : 30, Config);
	if (Rounds <= 0)
	{
		return false;
	}
	bQuickFireActive = true;
	bQuickFirePlanned = false;
	QuickFirePoint = Point;
	QuickFireTarget = Enemy;
	QuickFireRoundsLeft = Rounds;
	QuickFireLastProgressTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (!bInCover)
	{
		// He stops where he is and turns (RunQuickFire); in cover the open shot steps him off the wall first.
		SetSprinting(false);
		StopOperative();
	}
	ClearIdleFacing();
	NoteShotAim(Enemy, Enemy ? Enemy->GetActorLocation() : Point, Enemy == nullptr);
	UE_LOG(LogCodexTactics, Display, TEXT("[QuickFire] %s: %d round(s) at (%.0f, %.0f, %.0f)%s%s"), *DisplayName.ToString(), Rounds,
		Point.X, Point.Y, Point.Z, Enemy ? TEXT(", target ") : TEXT(" (area fire)"), Enemy ? *Enemy->GetName() : TEXT(""));
	return true;
}

bool AOperativeCharacter::PlanQuickFire(const FVector& Point, AActor* Enemy)
{
	if (!CanTakeQuickFire(true))
	{
		return false;
	}
	bQuickFirePlanned = true;
	PlannedQuickFirePoint = Point;
	PlannedQuickFireTarget = Enemy;
	return true;
}

bool AOperativeCharacter::ExecutePlannedQuickFire()
{
	if (!bQuickFirePlanned)
	{
		return false;
	}
	bQuickFirePlanned = false;
	AActor* Enemy = PlannedQuickFireTarget.Get();
	PlannedQuickFireTarget.Reset();
	return OrderQuickFire(Enemy && OperativeCharacterInternal::IsLiveEnemy(Enemy) ? Enemy->GetActorLocation() : PlannedQuickFirePoint,
		Enemy && OperativeCharacterInternal::IsLiveEnemy(Enemy) ? Enemy : nullptr);
}

void AOperativeCharacter::CancelQuickFire()
{
	bQuickFireActive = false;
	QuickFireRoundsLeft = 0;
	QuickFireTarget.Reset();
}

const TCHAR* AOperativeCharacter::RunQuickFire(float DeltaTime, AActor*& OutTarget)
{
	UWorld* World = GetWorld();
	const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased)
	{
		CancelQuickFire(); // the grid owns every shot (its attack, its AP)
		return TEXT("quick fire: dropped (turn-based)");
	}
	if (Flow && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause)
	{
		QuickFireLastProgressTime = Now; // the pause holds the order; it goes on when the pause is released
		return TEXT("quick fire: waits (tactical pause)");
	}
	if (IsRaging() || IsPanicking())
	{
		CancelQuickFire();
		return TEXT("quick fire: dropped (rage / panic)");
	}

	// The clicked enemy while it lives and the squad sees it; then its last spot (area fire).
	AActor* Enemy = QuickFireTarget.Get();
	if (Enemy && !OperativeCharacterInternal::IsLiveEnemy(Enemy))
	{
		QuickFireTarget.Reset();
		Enemy = nullptr;
	}
	if (Enemy)
	{
		QuickFirePoint = Enemy->GetActorLocation();
	}
	const FVector AimAt = QuickFirePoint;
	OutTarget = Enemy;
	NoteShotAim(Enemy, AimAt, Enemy == nullptr); // the aim offset holds the point / target while he turns

	// Ammo: an empty clip reloads first (the order waits), an empty weapon switches like the automatic fire does.
	if (UsesAmmo() && CurrentClip <= 0)
	{
		if (ReserveAmmo > 0)
		{
			StartReload();
			return TEXT("quick fire: reloading first");
		}
		AutoSwitchOnEmpty();
		if (!UsesAmmo())
		{
			CancelQuickFire();
			return TEXT("quick fire: dropped (no ammo left)");
		}
		return TEXT("quick fire: weapon switched");
	}

	// Cover (as a Ctrl + click targeted shot, ShootAtObject): a point round the corner / over a low wall is fired at from the
	// cover pose (lean out / pop up, or blind fire by the cover fire mode); a point in front of the cover: he steps off the
	// wall (the open shot, user decision 2026-10-06) and walks back after it.
	if (bInCover && !IsCornerShotTarget(AimAt))
	{
		BeginOpenShotFromCover(Enemy); // false while shimmying: he stays, the cover shot below
	}
	const bool bFromCover = bInCover;
	if (bFromCover && !CanFireFromCover())
	{
		CancelQuickFire();
		return TEXT("quick fire: dropped (no line out of this cover)");
	}

	// Sniper rifle: a direct order stops her and she kneels by herself (SniperRules), the round waits for the stance.
	if (IsSniperWeaponEquipped() && PrepareSniperShot(/*bDirectOrder*/ true) != ESniperFireStep::Ready)
	{
		return TEXT("quick fire: sniper gets ready");
	}

	// The arms throw a grenade: no round until the throw clip is done (as the automatic fire).
	if (const UOperativeAnimInstance* ArmsAnim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
		ArmsAnim && ArmsAnim->IsThrowingGrenade())
	{
		return TEXT("quick fire: throwing a grenade");
	}

	float Residual = 0.f;
	if (bFromCover)
	{
		// The back stays to the wall: the point is the threat side (GatherCoverThreat), the cover pose takes the shot.
		CoverShotTarget = Enemy;
		UpdateCoverFacing(/*bAllowSnap*/ false);
		if (ShootTimer > 0.f || MisfireCooldownTimer > 0.f)
		{
			return TEXT("quick fire: cooldown (cover)");
		}
		BeginCoverShot();
	}
	else
	{
		// Turn the body to the point (eased, never slower than AimTurnRateDegPerSec); no round where he does not aim.
		const FRotator LookRot(0.f, (AimAt - GetActorLocation()).Rotation().Yaw - BarrelYawOffset, 0.f);
		const FRotator Eased = FMath::RInterpTo(GetActorRotation(), LookRot, DeltaTime, 12.0f);
		const FRotator Constant = FMath::RInterpConstantTo(GetActorRotation(), LookRot, DeltaTime, AimTurnRateDegPerSec);
		const float EasedLeft = FMath::Abs(FRotator::NormalizeAxis(LookRot.Yaw - Eased.Yaw));
		const float ConstantLeft = FMath::Abs(FRotator::NormalizeAxis(LookRot.Yaw - Constant.Yaw));
		SetActorRotation(EasedLeft <= ConstantLeft ? Eased : Constant);
		Residual = GetShotAimResidualDeg(AimAt);
		if (Residual > AimConeDeg)
		{
			return TEXT("quick fire: turning");
		}
		if (ShootTimer > 0.f || MisfireCooldownTimer > 0.f)
		{
			return TEXT("quick fire: cooldown");
		}
	}

	LastQuickFireAimErrorDeg = Residual;
	const float MisfireBefore = MisfireCooldownTimer;
	bool bAimed = false;
	if (Enemy)
	{
		// The normal aimed shot (hit chance, cover, crit) while it is in range and in the line of fire.
		FShootCandidate Line;
		bool bBarricadeBlocked = false;
		if (EvaluateShotLine(Enemy, /*bKeepTarget*/ true, Line, bBarricadeBlocked) && Line.Enemy == Enemy && !Line.bBlind)
		{
			TGuardValue<bool> CoverBlind(bCoverBlindShot, bFromCover && CoverFireMode == ECoverFireMode::BlindFire);
			ShootAtTarget(Enemy, Line.Cover);
			bAimed = true;
		}
	}
	if (!bAimed)
	{
		FireQuickFireRound(AimAt);
	}
	const bool bMisfired = MisfireCooldownTimer > MisfireBefore && MisfireCooldownTimer > 0.f;
	if (!bMisfired)
	{
		++QuickFireRoundsFired;
	}
	--QuickFireRoundsLeft;
	QuickFireLastProgressTime = Now;
	if (QuickFireRoundsLeft <= 0)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[QuickFire] %s: burst done (%d round(s) fired in all)"), *DisplayName.ToString(), QuickFireRoundsFired);
		CancelQuickFire();
	}
	return bMisfired ? TEXT("quick fire: misfire") : TEXT("fired");
}

void AOperativeCharacter::FireQuickFireRound(const FVector& Aim)
{
	const FQuickFireConfig& Config = QuickFireRules::GetConfig();
	const int32 StanceIndex = Stance == EOperativeStance::Prone ? 2 : (Stance == EOperativeStance::Crouching ? 1 : 0);
	FOperativeAreaRound Spec;
	Spec.SpreadDeg = QuickFireRules::GetSpreadDeg(StanceIndex, Config);
	Spec.HitMultiplier = Config.AreaFireHitMultiplier;
	Spec.NextRoundDelay = FMath::Max(0.08f, CurrentWeapon ? CurrentWeapon->FireRate : 0.65f);
	Spec.LogTag = TEXT("QuickFire");
	FireAreaRound(Aim, Spec);
}

void AOperativeCharacter::FireAreaRound(const FVector& Aim, const FOperativeAreaRound& Spec)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const FQuickFireConfig& Config = QuickFireRules::GetConfig();
	if (UsesAmmo())
	{
		CurrentClip = FMath::Max(0, CurrentClip - 1);
	}
	// Cold misfire, as every rifle shot (ShootAtTarget).
	if (bForceMisfireForTesting || (ColdSurvival && FMath::FRand() < ColdSurvival->GetMisfireChance()))
	{
		MisfireCooldownTimer = ColdSurvival ? ColdSurvival->Config.MisfireDelay : 0.45f;
		UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("❄️ MISFIRE! (Bolt jammed)"), FLinearColor(0.4f, 0.85f, 1.f));
		OnWeaponMisfired.Broadcast(this);
		OnWeaponMisfiredNative.Broadcast(this);
		return;
	}
	ShootTimer = FMath::Max(0.05f, Spec.NextRoundDelay);
	// The muzzle flash gives him away, patrols hear the shot (the normal path: an alerted patrol starts the fight).
	if (UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>())
	{
		Sight->NotifyFired(this);
	}
	AEnemyCharacter::NotifySquadNoise(World, GetActorLocation(), ESquadNoise::Gunshot);
	// Suppressed (2026-10-26): blind, with the large spread; pinned he never gets here (RunCombatShooting holds the fire).
	const FSuppressionFireEffect Suppressed = USuppressionSubsystem::GetOperativeFireOf(this);
	ShotsWhilePinned += Suppressed.bMayFire ? 0 : 1;
	SuppressedBlindShots += Suppressed.bBlind ? 1 : 0;
	if (bInCover)
	{
		(CoverFireMode == ECoverFireMode::BlindFire || Suppressed.bBlind ? CoverBlindShots : CoverLeanShots) += 1;
	}
	else if (bOpenShotReturnPending)
	{
		++CoverOpenShots;
		OpenShotLastTime = World->GetTimeSeconds();
	}

	// The round: from the line-of-fire origin (round the corner from cover) towards the point with the stance's spread, at most
	// the weapon's range.
	const FVector Muzzle = bInCover ? GetCoverFireOrigin() : GetMuzzleLocation();
	FVector Direction = Aim - Muzzle;
	const float Length = static_cast<float>(Direction.Size());
	if (!bForceHitForTesting && Length > KINDA_SMALL_NUMBER)
	{
		const float SpreadRad = FMath::DegreesToRadians(FMath::Clamp(Spec.SpreadDeg * Suppressed.SpreadMultiplier, 0.f, 45.f));
		Direction = FMath::VRandCone(Direction / Length, SpreadRad) * Length;
	}
	const float Range = CurrentWeapon ? CurrentWeapon->AttackRangeCm : 1400.f;
	const FVector End = QuickFireRules::ComputeRoundEnd(Muzzle, Muzzle + Direction, Range, Config);

	// The world (walls, ground, barrels, mines) by the visibility trace; the squad never (no friendly fire, as every squad shot);
	// enemies - seen or not - by their capsules along the line up to the first blocking hit.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(QuickFireRound), false, this);
	OperativeCharacterInternal::AddShotLineAllies(World, Params);
	TArray<FQuickFireCapsule> Capsules;
	TArray<AActor*> CapsuleActors;
	for (AActor* Each : UEnemyRegistrySubsystem::GetTaggedEnemies(World))
	{
		const UHealthComponent* Health = IsValid(Each) ? Each->FindComponentByClass<UHealthComponent>() : nullptr;
		if (!IsValid(Each) || (Health && !Health->IsAlive()))
		{
			continue;
		}
		Params.AddIgnoredActor(Each);
		FQuickFireCapsule Capsule;
		if (const ACharacter* Character = Cast<ACharacter>(Each); Character && Character->GetCapsuleComponent())
		{
			Capsule.Center = Character->GetActorLocation();
			Capsule.Radius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
			Capsule.HalfHeight = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		}
		else
		{
			FVector Origin;
			FVector Extent;
			Each->GetActorBounds(true, Origin, Extent);
			Capsule.Center = Origin;
			Capsule.Radius = static_cast<float>(FMath::Max(Extent.X, Extent.Y));
			Capsule.HalfHeight = static_cast<float>(Extent.Z);
		}
		Capsules.Add(Capsule);
		CapsuleActors.Add(Each);
	}
	FHitResult Hit;
	const bool bBlocked = World->LineTraceSingleByChannel(Hit, Muzzle, End, ECC_Visibility, Params);
	const FVector Stop = bBlocked ? FVector(Hit.ImpactPoint) : End;
	float AlongCm = 0.f;
	const int32 Crossed = QuickFireRules::FindFirstCapsuleOnSegment(Muzzle, Stop, Capsules, Config.CapsuleToleranceCm, AlongCm);

	AActor* Struck = nullptr;
	bool bHit = false;
	FVector TracerEnd = Stop;
	if (Crossed != INDEX_NONE)
	{
		// An enemy in the line: the weapon's chance at that distance (cold hands too) times the area-fire factor (and the suppression's).
		AActor* EnemyHit = CapsuleActors[Crossed];
		Struck = EnemyHit;
		TracerEnd = Muzzle + (Stop - Muzzle).GetSafeNormal() * AlongCm;
		const float DistM = static_cast<float>(FVector::Dist2D(GetActorLocation(), EnemyHit->GetActorLocation())) / 100.f;
		const int32 DistCells = FMath::Max(1, FMath::RoundToInt(DistM / 1.5f));
		float Chance = CurrentWeapon ? CurrentWeapon->GetHitChanceForDistance(DistCells) : 0.85f;
		if (ColdSurvival && ColdLevel > 50.0f)
		{
			Chance = FMath::Clamp(Chance - ColdSurvival->GetAimPenalty(), 0.05f, 0.99f);
		}
		bHit = bForceHitForTesting || FMath::FRand() <= FMath::Clamp(Chance * Spec.HitMultiplier * Suppressed.HitMultiplier, 0.f, 1.f);
		if (bHit)
		{
			ApplyRangedHit(EnemyHit, 1.f, DistCells, IsRaging());
		}
	}
	else if (bBlocked && Hit.GetActor())
	{
		// A barrel explodes, a mine goes off, a trapped crate / object detonates (an untrapped crate is only pierced).
		const ETargetedShotKind Kind = ClassifyShotTarget(Hit.GetActor());
		if (Kind != ETargetedShotKind::None && Kind != ETargetedShotKind::Enemy)
		{
			Struck = Hit.GetActor();
			bHit = DetonateShotTarget(Struck, Kind, DisplayName);
		}
	}
	// Suppression (2026-10-26): the round suppresses the enemy it crossed and every one it passed near or whose cover it struck.
	ReportOwnRound(Muzzle, TracerEnd, Crossed != INDEX_NONE ? Struck : nullptr, bHit && Crossed != INDEX_NONE, bBlocked && Crossed == INDEX_NONE,
		Spec.bFullAuto);
	NoteShotAim(nullptr, Aim, true);
	OnWeaponFired.Broadcast(this, Struck, bHit);
	OnWeaponFiredNative.Broadcast(this, Struck, bHit);
	if (UCombatFeedbackSubsystem* Feedback = World->GetSubsystem<UCombatFeedbackSubsystem>())
	{
		Feedback->SpawnTracer(GetWeaponMuzzleLocation(), TracerEnd, CurrentWeapon ? CurrentWeapon->TracerColor : UCombatFeedbackSubsystem::DefaultTracerColor(),
			CurrentWeapon ? CurrentWeapon->DamageType : EDamageType::Kinetic);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[%s] %s: round to (%.0f, %.0f, %.0f) -> %s%s%s"), Spec.LogTag, *DisplayName.ToString(), TracerEnd.X,
		TracerEnd.Y, TracerEnd.Z, Struck ? *Struck->GetName() : (bBlocked && Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("nothing")),
		Struck ? (bHit ? TEXT(" (hit)") : TEXT(" (no hit)")) : TEXT(""), Suppressed.bBlind ? TEXT(" [suppressed: blind]") : TEXT(""));
	if (UsesAmmo() && CurrentClip <= 0)
	{
		StartReload();
	}
}
