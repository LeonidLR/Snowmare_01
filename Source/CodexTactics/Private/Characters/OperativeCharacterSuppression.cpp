// AOperativeCharacter: suppressive fire (game designer decisions 2026-10-26) - the SUPPRESS order (sustained full-auto bursts at an
// enemy / his cover or at a spot, lots of ammo) and his own suppression tier (blind fire when suppressed, none when pinned).
// Pure rules: Combat/SuppressionRules; runtime: Combat/SuppressionSubsystem. UE-only, no Godot reference. Spec: docs/design/SUPPRESSION.md.
// Cluster map: docs/port/REFACTOR_OPERATIVE_2026-10.md.

#include "Characters/OperativeCharacter.h"

#include "CodexTactics.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacterInternal.h"
#include "Combat/HealthComponent.h"
#include "Combat/SuppressionSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "UI/FloatingTextSubsystem.h"

ESuppressionTier AOperativeCharacter::GetSuppressionTier() const
{
	return USuppressionSubsystem::GetTierOf(this);
}

ESuppressionWeapon AOperativeCharacter::GetOwnRoundClass(bool bFullAuto) const
{
	if (bFullAuto)
	{
		return ESuppressionWeapon::FullAuto;
	}
	return IsSniperWeaponEquipped() ? ESuppressionWeapon::Sniper : ESuppressionWeapon::SemiAuto;
}

void AOperativeCharacter::ReportOwnRound(const FVector& Start, const FVector& End, const AActor* Target, bool bHit, bool bStruckCover, bool bFullAuto) const
{
	if (!UsesAmmo())
	{
		return; // the knife: no rounds
	}
	FSuppressionRound Round;
	Round.Shooter = this;
	Round.bAtEnemies = true;
	Round.Start = Start;
	Round.End = End;
	Round.Target = Target;
	Round.bHitTarget = bHit;
	Round.bStruckCover = bStruckCover;
	Round.Weapon = GetOwnRoundClass(bFullAuto);
	Round.WeightScale = CurrentWeapon ? CurrentWeapon->SuppressionWeight : 1.f;
	USuppressionSubsystem::Report(this, Round);
}

bool AOperativeCharacter::CanTakeSuppress(bool bExplain)
{
	if (!USuppressionSubsystem::IsEnabled())
	{
		if (bExplain)
		{
			UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("SUPPRESSIVE FIRE IS OFF"), FLinearColor(0.85f, 0.85f, 0.85f));
		}
		return false;
	}
	if (!CanTakeQuickFire(bExplain)) // dead, knocked down, aboard, raging, panicking, the knife, a frozen weapon, hands busy
	{
		return false;
	}
	if (SuppressionRules::OrderAmmoBudget(SuppressionRules::GetConfig(), CurrentClip, ReserveAmmo) <= 0)
	{
		if (bExplain)
		{
			UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("LOW AMMO - NO SUPPRESSIVE FIRE"), FLinearColor(1.f, 0.7f, 0.2f));
		}
		return false;
	}
	return true;
}

bool AOperativeCharacter::OrderSuppress(AActor* Target, const FVector& Point)
{
	if (!CanTakeSuppress(true))
	{
		return false;
	}
	CancelQuickFire(); // one fire order at a time
	bSuppressActive = true;
	bSuppressPlanned = false;
	bSuppressAtTarget = Target != nullptr;
	SuppressTarget = Target;
	// The caller's point: a seen enemy's position or a hidden one's silhouette (never the hidden enemy's real spot).
	SuppressPoint = Point;
	SuppressBurstCount = 0;
	LastSuppressStop = ESuppressOrderStop::None;
	SuppressLastProgressTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (!bInCover)
	{
		// He stops where he is and turns (RunSuppressFire); in cover he fires from the cover pose.
		SetSprinting(false);
		StopOperative();
	}
	ClearIdleFacing();
	NoteShotAim(Target, SuppressPoint, Target == nullptr);
	UE_LOG(LogCodexTactics, Display, TEXT("[Suppress] %s: suppressive fire at %s (%.0f, %.0f, %.0f), %s, ammo %d + %d"), *DisplayName.ToString(),
		Target ? *Target->GetName() : TEXT("a spot"), SuppressPoint.X, SuppressPoint.Y, SuppressPoint.Z,
		CurrentWeapon && CurrentWeapon->HasFullAuto() ? TEXT("full-auto") : TEXT("the weapon's cadence"), CurrentClip, ReserveAmmo);
	return true;
}

bool AOperativeCharacter::PlanSuppress(AActor* Target, const FVector& Point)
{
	if (!CanTakeSuppress(true))
	{
		return false;
	}
	bSuppressPlanned = true;
	PlannedSuppressTarget = Target;
	PlannedSuppressPoint = Point;
	return true;
}

bool AOperativeCharacter::ExecutePlannedSuppress()
{
	if (!bSuppressPlanned)
	{
		return false;
	}
	bSuppressPlanned = false;
	AActor* Target = PlannedSuppressTarget.Get();
	PlannedSuppressTarget.Reset();
	const UHealthComponent* Health = Target ? Target->FindComponentByClass<UHealthComponent>() : nullptr;
	const bool bAlive = IsValid(Target) && (!Health || Health->IsAlive());
	return OrderSuppress(bAlive ? Target : nullptr, bAlive && !Target->IsHidden() ? Target->GetActorLocation() : PlannedSuppressPoint);
}

void AOperativeCharacter::CancelSuppressFire(const TCHAR* Why)
{
	if (bSuppressActive)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[Suppress] %s: suppressive fire ends (%s, %d round(s) fired in all)"), *DisplayName.ToString(), Why,
			SuppressRoundsFired);
	}
	bSuppressActive = false;
	bSuppressAtTarget = false;
	SuppressTarget.Reset();
	SuppressBurstCount = 0;
}

const TCHAR* AOperativeCharacter::RunSuppressFire(float DeltaTime, AActor*& OutTarget)
{
	UWorld* World = GetWorld();
	const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const FSuppressionConfig& Config = SuppressionRules::GetConfig();
	if (!USuppressionSubsystem::IsEnabled())
	{
		CancelSuppressFire(TEXT("Codex.Combat.Suppression 0"));
		return TEXT("suppress: dropped (suppression off)");
	}
	if (Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased)
	{
		CancelSuppressFire(TEXT("turn-based combat")); // decision 5: no suppression on the grid
		return TEXT("suppress: dropped (turn-based)");
	}
	if (Flow && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause)
	{
		SuppressLastProgressTime = Now; // the pause holds the order; it goes on when the pause is released
		return TEXT("suppress: waits (tactical pause)");
	}
	if (IsRaging() || IsPanicking())
	{
		CancelSuppressFire(TEXT("rage / panic"));
		return TEXT("suppress: dropped (rage / panic)");
	}

	// The enemy while he lives (seen: his spot follows him; unseen: his last spot - the rounds keep hammering his cover).
	AActor* Enemy = SuppressTarget.Get();
	const UHealthComponent* EnemyHealth = Enemy ? Enemy->FindComponentByClass<UHealthComponent>() : nullptr;
	const bool bEnemyAlive = IsValid(Enemy) && Enemy->ActorHasTag(CombatQueries::EnemyTag) && (!EnemyHealth || EnemyHealth->IsAlive());
	const ESuppressOrderStop Stop = SuppressionRules::CheckOrder(Config, bSuppressAtTarget, bEnemyAlive, UsesAmmo(), CurrentClip, ReserveAmmo);
	if (Stop != ESuppressOrderStop::None)
	{
		LastSuppressStop = Stop;
		if (Stop == ESuppressOrderStop::AmmoLow)
		{
			UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("LOW AMMO - SUPPRESSION ENDS"), FLinearColor(1.f, 0.7f, 0.2f));
		}
		CancelSuppressFire(SuppressionRules::StopName(Stop));
		return TEXT("suppress: done");
	}
	if (bEnemyAlive && !Enemy->IsHidden())
	{
		SuppressPoint = Enemy->GetActorLocation();
	}
	FVector AimAt = SuppressPoint + FVector(0.f, 0.f, Config.OrderAimHeightCm);
	if (!bSuppressAtTarget && Config.OrderSpotScatterCm > 0.f)
	{
		// Fire at a spot walks the rounds over it.
		const FVector2D Scatter = FMath::RandPointInCircle(Config.OrderSpotScatterCm);
		AimAt += FVector(Scatter.X, Scatter.Y, 0.f);
	}
	OutTarget = bEnemyAlive ? Enemy : nullptr;
	NoteShotAim(OutTarget, AimAt, OutTarget == nullptr); // the aim offset holds the point / target while he turns

	// Ammo: an empty magazine reloads first (the order waits; the budget above keeps the last rounds).
	if (UsesAmmo() && CurrentClip <= 0)
	{
		if (ReserveAmmo > 0)
		{
			StartReload();
			SuppressLastProgressTime = Now;
			return TEXT("suppress: reloading");
		}
		LastSuppressStop = ESuppressOrderStop::AmmoLow;
		CancelSuppressFire(TEXT("magazine and reserve empty"));
		return TEXT("suppress: done (no ammo)");
	}

	// Cover: a point round the corner / over a low wall is fired at from the cover pose; a point in front of the cover: he steps off the
	// wall (the open shot) - not while suppressed himself (he keeps his head down and fires blind from the cover pose).
	if (bInCover && !IsCornerShotTarget(AimAt) && USuppressionSubsystem::MayExposeNow(this))
	{
		BeginOpenShotFromCover(Enemy);
	}
	const bool bFromCover = bInCover;
	if (bFromCover && !CanFireFromCover() && IsCornerShotTarget(AimAt))
	{
		CancelSuppressFire(TEXT("no line out of this cover"));
		return TEXT("suppress: dropped (no line out of this cover)");
	}
	if (IsSniperWeaponEquipped() && PrepareSniperShot(/*bDirectOrder*/ true) != ESniperFireStep::Ready)
	{
		return TEXT("suppress: sniper gets ready");
	}
	if (const UOperativeAnimInstance* ArmsAnim = GetMesh() ? Cast<UOperativeAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
		ArmsAnim && ArmsAnim->IsThrowingGrenade())
	{
		return TEXT("suppress: throwing a grenade");
	}
	if (bFromCover)
	{
		CoverShotTarget = Enemy;
		UpdateCoverFacing(/*bAllowSnap*/ false);
		if (ShootTimer > 0.f || MisfireCooldownTimer > 0.f)
		{
			return TEXT("cooldown");
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
		if (GetShotAimResidualDeg(AimAt) > AimConeDeg)
		{
			return TEXT("suppress: turning");
		}
		if (ShootTimer > 0.f || MisfireCooldownTimer > 0.f)
		{
			return TEXT("cooldown");
		}
	}

	// The round: full-auto inside bursts (assault rifles), else the weapon's cadence; a wide spray, hits by the order's factor.
	const bool bFullAuto = CurrentWeapon && CurrentWeapon->HasFullAuto();
	const int32 StanceIndex = Stance == EOperativeStance::Prone ? 2 : (Stance == EOperativeStance::Crouching ? 1 : 0);
	FOperativeAreaRound Spec;
	Spec.SpreadDeg = SuppressionRules::GetOrderSpreadDeg(Config, StanceIndex);
	Spec.HitMultiplier = Config.OrderHitMultiplier;
	Spec.NextRoundDelay = SuppressionRules::NextOrderRoundDelay(Config, CurrentWeapon ? CurrentWeapon->FireRate : 0.65f,
		CurrentWeapon ? CurrentWeapon->FullAutoInterval : 0.f, SuppressBurstCount);
	Spec.bFullAuto = bFullAuto;
	Spec.LogTag = TEXT("Suppress");
	const float MisfireBefore = MisfireCooldownTimer;
	FireAreaRound(AimAt, Spec);
	const bool bMisfired = MisfireCooldownTimer > MisfireBefore && MisfireCooldownTimer > 0.f;
	if (!bMisfired)
	{
		++SuppressRoundsFired;
		FullAutoRoundsFired += bFullAuto ? 1 : 0;
	}
	SuppressLastProgressTime = Now;
	return bMisfired ? TEXT("suppress: misfire") : TEXT("fired");
}
