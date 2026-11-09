// AOperativeCharacter - the RPG-7 (2026-11-09; the game designer's numbers the same day; docs/systems/weapons.md "The RPG-7"; new - no Godot
// reference): the launcher's one shot rule (no target inside its minimum range - the shooter would catch his own blast) and the rocket through the
// shared projectile layer (UProjectileSubsystem -> a direct hit / Explosions::Detonate). The designer removed the back-blast damage and the
// wall-behind / closed-room / prone refusals.

#include "Characters/OperativeCharacter.h"

#include "Characters/EnemyCharacter.h"
#include "CodexTactics.h"
#include "Combat/LauncherRules.h"
#include "Combat/ProjectileSubsystem.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/World.h"
#include "UI/FloatingTextSubsystem.h"

namespace OperativeLauncherInternal
{
	/** Where the rocket is aimed on Target (a body's centre). */
	FVector AimPointOn(const AActor* Target)
	{
		return Target ? Target->GetActorLocation() : FVector::ZeroVector;
	}
}

bool AOperativeCharacter::IsLauncherEquipped() const
{
	return CurrentWeapon && CurrentWeapon->Stats.Handling == EWeaponHandling::Launcher && CurrentWeapon->Stats.Kind == EWeaponKind::Projectile;
}

bool AOperativeCharacter::LauncherShotAllowed(AActor* Target)
{
	if (!IsLauncherEquipped() || !Target)
	{
		return true;
	}
	LastLauncherRefusal = LauncherRules::Refusal(CurrentClip > 0, static_cast<float>(FVector::Dist(GetActorLocation(), Target->GetActorLocation())),
		CurrentWeapon->Stats);
	if (LastLauncherRefusal == ELauncherRefusal::None)
	{
		return true;
	}
	++LauncherRefusals;
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	if (Now - LauncherLastWarningTime > 2.0)
	{
		LauncherLastWarningTime = Now;
		UFloatingTextSubsystem::SpawnAboveOperative(this, LauncherRules::RefusalText(LastLauncherRefusal), FLinearColor(1.f, 0.55f, 0.15f));
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[RPG] %s: shot refused (%s)"), *DisplayName.ToString(), *LauncherRules::RefusalText(LastLauncherRefusal));
	return false;
}

bool AOperativeCharacter::FireLauncherAt(AActor* Target, bool bRagingShot)
{
	UWorld* World = GetWorld();
	UProjectileSubsystem* Projectiles = World ? World->GetSubsystem<UProjectileSubsystem>() : nullptr;
	if (!Target || !Projectiles || !CurrentWeapon)
	{
		return false;
	}
	const FWeaponStats& Stats = CurrentWeapon->Stats;
	const FVector Muzzle = GetWeaponMuzzleLocation();
	const FVector AimPoint = OperativeLauncherInternal::AimPointOn(Target);
	const float Dist = static_cast<float>(FVector::Dist2D(GetActorLocation(), Target->GetActorLocation()));
	const int32 DistCells = FMath::Max(1, FMath::RoundToInt(Dist / 100.f / 1.5f));
	float HitChance = CurrentWeapon->GetHitChanceForDistance(DistCells) * GetRangeHitMultiplierAt(Target, Dist);
	if (bRagingShot)
	{
		HitChance = FMath::Min(1.f, HitChance + 0.1f);
	}
	LastShotHitChance = HitChance;
	const bool bHit = bForceHitForTesting || FMath::FRand() <= HitChance;
	const FVector AimDir = (AimPoint - Muzzle).GetSafeNormal();
	const FVector Point = bHit ? AimPoint : LauncherRules::MissPoint(AimPoint, AimDir, FMath::FRand(), FMath::FRand());

	FProjectileLaunch Launch;
	Launch.From = Muzzle;
	Launch.Projectile = Stats.Projectile;
	Launch.Explosion = Stats.Explosion;
	Launch.DirectDamage = Stats.Explosion.DirectHitDamage; // the actor struck: its whole share (the blast leaves it out)
	Launch.BlastDamage = Stats.Damage.BaseDamage;          // the splash on everyone else in the radius
	Launch.DamageType = Stats.Damage.DamageType;
	Launch.ArmorPenetration = Stats.Damage.ArmorPenetration;
	Launch.PenetrationClass = Stats.Damage.PenetrationClass;
	Launch.Instigator = this;
	Launch.SourceName = CurrentWeapon->WeaponName.IsEmpty() ? CurrentWeapon->WeaponId : CurrentWeapon->WeaponName.ToString();
	Launch.bSquadNoise = IsPlayerTeam();
	if (!Projectiles->FireAt(Launch, Point))
	{
		Launch.Velocity = (Point - Muzzle).GetSafeNormal() * Stats.Projectile.SpeedCmS;
		Projectiles->Fire(Launch);
	}
	++LauncherShots;
	UE_LOG(LogCodexTactics, Display, TEXT("[RPG] %s: rocket at %s (%.0f m, %s, chance %.2f)"), *DisplayName.ToString(), *Target->GetName(), Dist / 100.f,
		bHit ? TEXT("on target") : TEXT("off"), HitChance);
	// The launch is loud (patrols hear it; the blast's own noise comes at the impact).
	if (IsPlayerTeam())
	{
		AEnemyCharacter::NotifySquadNoise(World, GetActorLocation(), ESquadNoise::Gunshot);
	}
	NoteShotAim(Target, AimPoint, false);
	OnWeaponFired.Broadcast(this, Target, bHit);
	OnWeaponFiredNative.Broadcast(this, Target, bHit);
	// One rocket in the tube: the next one goes in at once (the weapon's reload_time).
	if (UsesAmmo() && CurrentClip <= 0 && ReserveAmmo > 0)
	{
		StartReload();
	}
	return true;
}
