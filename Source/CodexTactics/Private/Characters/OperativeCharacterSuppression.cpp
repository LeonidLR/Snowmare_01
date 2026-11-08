// AOperativeCharacter: suppressive fire (game designer decisions 2026-10-26) - the SUPPRESS order (sustained full-auto bursts at an
// enemy / his cover or at a spot, lots of ammo) and his own suppression tier (blind fire when suppressed, none when pinned).
// Pure rules: Combat/SuppressionRules; runtime: Combat/SuppressionSubsystem. UE-only, no Godot reference. Spec: docs/design/SUPPRESSION.md.
// Cluster map: docs/port/REFACTOR_OPERATIVE_2026-10.md.

#include "Characters/OperativeCharacter.h"

#include "AI/PmcSquadSubsystem.h"
#include "CodexTactics.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacterInternal.h"
#include "Combat/WeaponBallisticsRules.h"
#include "Combat/HealthComponent.h"
#include "Combat/SuppressionSubsystem.h"
#include "Combat/TacticalSightSubsystem.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
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
	Round.WeightScale = CurrentWeapon ? CurrentWeapon->Stats.Fire.SuppressionWeight : 1.f;
	USuppressionSubsystem::Report(this, Round);
}

namespace OperativeSuppressionLocal
{
	static TAutoConsoleVariable<int32> CVarHonestFire(TEXT("Codex.Combat.HonestFire"), 1,
		TEXT("fix/pmc-fire-through-wall (designer 2026-11-08: a prone PMC fighter kept firing into the wall the squad stood behind): 1 (default) a PMC team sees a target only through a clear line (a heard shot / a flash without one makes it known, not seen), fire at a point needs a line of fire that reaches near it, a running suppress follows the target only while his side sees it; 0 = the old rules (A/B)."));
}

bool AOperativeCharacter::IsHonestFireOn()
{
	return OperativeSuppressionLocal::CVarHonestFire.GetValueOnGameThread() != 0;
}

bool AOperativeCharacter::IsSeenByOwnSide(const AActor* Target) const
{
	UWorld* World = GetWorld();
	if (!Target || !World)
	{
		return false;
	}
	if (IsPlayerTeam())
	{
		const UTacticalSightSubsystem* Sight = World->GetSubsystem<UTacticalSightSubsystem>();
		return !Sight || Sight->IsVisibleToSquad(Target);
	}
	const UPmcSquadSubsystem* Pmc = UPmcSquadSubsystem::Get(World);
	return !Pmc || Pmc->IsVisibleToTeam(TeamId, Target);
}

bool AOperativeCharacter::HasUsefulFireLine(const FVector& Point, float* OutBlockedAtCm) const
{
	return HasUsefulFireLineFrom(bInCover ? GetCoverFireOrigin() : GetMuzzleLocation(), Point, OutBlockedAtCm);
}

bool AOperativeCharacter::HasUsefulFireLineFrom(const FVector& Muzzle, const FVector& Point, float* OutBlockedAtCm) const
{
	if (OutBlockedAtCm)
	{
		*OutBlockedAtCm = -1.f;
	}
	UWorld* World = GetWorld();
	if (!World || !IsHonestFireOn())
	{
		return true;
	}
	// The world only: bodies (his mates, the target, anybody) never make a line useless (FireAreaRound's round crosses them or not).
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FireLineUseful), false, this);
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	FHitResult Hit;
	const float Distance = static_cast<float>(FVector::Dist(Muzzle, Point));
	float BlockedAt = -1.f;
	if (World->LineTraceSingleByChannel(Hit, Muzzle, Point, ECC_Visibility, Params))
	{
		BlockedAt = Hit.bStartPenetrating ? 0.f : static_cast<float>(Hit.Distance);
	}
	if (OutBlockedAtCm)
	{
		*OutBlockedAtCm = BlockedAt;
	}
	return SuppressionRules::IsFireLineUseful(SuppressionRules::GetConfig(), Distance, BlockedAt);
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
	// Teams (2026-11-03): a hostile fighter's target is a man of another team, not an "Enemy"-tagged actor (IsLiveTargetOf).
	const bool bEnemyAlive = OperativeCharacterInternal::IsLiveTargetOf(*this, Enemy) && (!EnemyHealth || EnemyHealth->IsAlive());
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
	// Honest sight (fix/pmc-fire-through-wall 2026-11-08): the spot follows him only while this side sees him - the old check (not hidden)
	// let a hostile fighter's order track a squad man behind a wall (the player's men are never hidden actors).
	if (bEnemyAlive && !Enemy->IsHidden() && (!IsHonestFireOn() || IsSeenByOwnSide(Enemy)))
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
		// Turn the body to the point (eased, never slower than AimTurnRateDegPerSec; prone at the prone turn rate, bug #33); no round
		// where he does not aim.
		TurnAimTowards(AimAt, DeltaTime);
		if (GetShotAimResidualDeg(AimAt) > AimConeDeg)
		{
			return TEXT("suppress: turning");
		}
		if (ShootTimer > 0.f || MisfireCooldownTimer > 0.f)
		{
			return TEXT("cooldown");
		}
	}

	// No line of fire (fix/pmc-fire-through-wall 2026-11-08): a wall between his muzzle (his stance's: prone 25 cm) and the point, not just the
	// target's own cover near it - no round; the order ends (the commander moves him / flanks, the player re-orders).
	if (float BlockedAt = -1.f; !HasUsefulFireLine(SuppressPoint + FVector(0.f, 0.f, Config.OrderAimHeightCm), &BlockedAt))
	{
		++NoLineOfFireSkips;
		LastSuppressStop = ESuppressOrderStop::NoLineOfFire;
		if (IsPlayerTeam())
		{
			UFloatingTextSubsystem::SpawnAboveOperative(this, TEXT("NO LINE OF FIRE"), FLinearColor(1.f, 0.7f, 0.2f));
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[Suppress] %s: no line of fire to (%.0f, %.0f, %.0f): blocked %.0f cm out of %.0f"), *DisplayName.ToString(),
			SuppressPoint.X, SuppressPoint.Y, SuppressPoint.Z, BlockedAt,
			FVector::Dist(bInCover ? GetCoverFireOrigin() : GetMuzzleLocation(), SuppressPoint + FVector(0.f, 0.f, Config.OrderAimHeightCm)));
		CancelSuppressFire(TEXT("no line of fire"));
		return TEXT("suppress: dropped (no line of fire)");
	}

	// The round: full-auto inside bursts (assault rifles), else the weapon's cadence; a wide spray, hits by the order's factor.
	const bool bFullAuto = CurrentWeapon && CurrentWeapon->HasFullAuto();
	const int32 StanceIndex = Stance == EOperativeStance::Prone ? 2 : (Stance == EOperativeStance::Crouching ? 1 : 0);
	FOperativeAreaRound Spec;
	Spec.SpreadDeg = SuppressionRules::GetOrderSpreadDeg(Config, StanceIndex) * (CurrentWeapon ? CurrentWeapon->Stats.Accuracy.SpreadScale : 1.f);
	// Full-auto recoil (decision 5, 2026-11-08: the M16 +0.3 deg a round, settling in 0.5 s): the bloom widens the spray.
	const float RecoilPerRound = bFullAuto && CurrentWeapon ? CurrentWeapon->Stats.Accuracy.RecoilPerRoundDeg : 0.f;
	if (RecoilPerRound > 0.f)
	{
		RecoilBloomDeg = WeaponBallisticsRules::SettleRecoil(RecoilBloomDeg, RecoilLastRoundTime < 0.0 ? 1.0e6f : static_cast<float>(Now - RecoilLastRoundTime),
			RecoilPerRound, CurrentWeapon->Stats.Accuracy.RecoilRecoverySeconds);
		Spec.SpreadDeg += RecoilBloomDeg;
	}
	Spec.HitMultiplier = Config.OrderHitMultiplier;
	Spec.NextRoundDelay = SuppressionRules::NextOrderRoundDelay(Config, CurrentWeapon ? CurrentWeapon->Stats.Fire.ShotIntervalSeconds : 0.65f,
		CurrentWeapon ? CurrentWeapon->Stats.Fire.FullAutoIntervalSeconds : 0.f, SuppressBurstCount);
	Spec.bFullAuto = bFullAuto;
	Spec.LogTag = TEXT("Suppress");
	const float MisfireBefore = MisfireCooldownTimer;
	FireAreaRound(AimAt, Spec);
	const bool bMisfired = MisfireCooldownTimer > MisfireBefore && MisfireCooldownTimer > 0.f;
	if (!bMisfired)
	{
		++SuppressRoundsFired;
		FullAutoRoundsFired += bFullAuto ? 1 : 0;
		if (RecoilPerRound > 0.f)
		{
			RecoilBloomDeg += RecoilPerRound;
			RecoilLastRoundTime = Now;
		}
	}
	SuppressLastProgressTime = Now;
	return bMisfired ? TEXT("suppress: misfire") : TEXT("fired");
}
