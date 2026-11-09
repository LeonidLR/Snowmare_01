// AOperativeCharacter - the RPG-7 (2026-11-09; docs/systems/weapons.md "The RPG-7"; new - no Godot reference): the launcher's shot rules
// (stand / kneel, never prone; no wall right behind, not from a closed room; not closer than the arming distance), the rocket through the shared
// projectile layer (UProjectileSubsystem -> Explosions::Detonate) and the back-blast cone behind him (friendly fire too).

#include "Characters/OperativeCharacter.h"

#include "Characters/EnemyCharacter.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/LauncherRules.h"
#include "Combat/ProjectileSubsystem.h"
#include "Data/WeaponDataAsset.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UI/FloatingTextSubsystem.h"

namespace OperativeLauncherInternal
{
	/** Where the rocket is aimed on Target (a body's centre). */
	FVector AimPointOn(const AActor* Target)
	{
		return Target ? Target->GetActorLocation() : FVector::ZeroVector;
	}

	FCollisionObjectQueryParams Geometry()
	{
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		return Objects;
	}
}

bool AOperativeCharacter::IsLauncherEquipped() const
{
	return CurrentWeapon && CurrentWeapon->Stats.Handling == EWeaponHandling::Launcher && CurrentWeapon->Stats.Kind == EWeaponKind::Projectile;
}

ELauncherBackblast AOperativeCharacter::CheckLauncherBackblast(const FVector& AimDir) const
{
	const UWorld* World = GetWorld();
	if (!World || !CurrentWeapon)
	{
		return ELauncherBackblast::Clear;
	}
	const FWeaponExplosionStats& Blast = CurrentWeapon->Stats.Explosion;
	if (Blast.BackBlastLengthCm <= 0.f)
	{
		return ELauncherBackblast::Clear;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LauncherBackblast), false, this);
	const FVector Shoulder = GetActorLocation() + FVector(0.f, 0.f, 50.f);
	const FVector Back = -AimDir.GetSafeNormal();
	FHitResult Hit;
	const float WallBehind = World->LineTraceSingleByObjectType(Hit, Shoulder, Shoulder + Back * Blast.BackBlastLengthCm,
		OperativeLauncherInternal::Geometry(), Params) ? static_cast<float>(Hit.Distance) : -1.f;
	FHitResult Roof;
	const bool bRoof = World->LineTraceSingleByObjectType(Roof, Shoulder, Shoulder + FVector(0.f, 0.f, 600.f), OperativeLauncherInternal::Geometry(), Params);
	return LauncherRules::ClassifyBackblast(WallBehind, bRoof, Blast);
}

bool AOperativeCharacter::LauncherShotAllowed(AActor* Target)
{
	if (!IsLauncherEquipped() || !Target)
	{
		return true;
	}
	const FVector AimDir = (OperativeLauncherInternal::AimPointOn(Target) - GetWeaponMuzzleLocation()).GetSafeNormal();
	LastLauncherBackblast = CheckLauncherBackblast(AimDir);
	LastLauncherRefusal = LauncherRules::Refusal(CurrentClip > 0, Stance, static_cast<float>(FVector::Dist(GetActorLocation(), Target->GetActorLocation())),
		LastLauncherBackblast, CurrentWeapon->Stats);
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const bool bWarn = LastLauncherRefusal != ELauncherRefusal::None || LastLauncherBackblast != ELauncherBackblast::Clear;
	if (bWarn && Now - LauncherLastWarningTime > 2.0)
	{
		LauncherLastWarningTime = Now;
		const FString Text = LastLauncherRefusal != ELauncherRefusal::None ? LauncherRules::RefusalText(LastLauncherRefusal, LastLauncherBackblast)
			: LauncherRules::RefusalText(ELauncherRefusal::Backblast, LastLauncherBackblast) + TEXT(" (fired anyway)");
		UFloatingTextSubsystem::SpawnAboveOperative(this, Text, FLinearColor(1.f, 0.55f, 0.15f));
	}
	if (LastLauncherRefusal != ELauncherRefusal::None)
	{
		++LauncherRefusals;
		UE_LOG(LogCodexTactics, Display, TEXT("[RPG] %s: shot refused (%s)"), *DisplayName.ToString(),
			*LauncherRules::RefusalText(LastLauncherRefusal, LastLauncherBackblast));
		return false;
	}
	return true;
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
	Launch.DirectDamage = Stats.Explosion.DirectHitDamage;
	Launch.BlastDamage = Stats.Damage.BaseDamage;
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
	ApplyLauncherBackblast(AimDir);
	// One rocket in the tube: the next one goes in at once (the weapon's reload_time).
	if (UsesAmmo() && CurrentClip <= 0 && ReserveAmmo > 0)
	{
		StartReload();
	}
	return true;
}

void AOperativeCharacter::ApplyLauncherBackblast(const FVector& AimDir)
{
	LastBackblastVictims = 0;
	LastBackblastDamage = 0.f;
	UWorld* World = GetWorld();
	if (!World || !CurrentWeapon)
	{
		return;
	}
	const FWeaponExplosionStats& Blast = CurrentWeapon->Stats.Explosion;
	if (Blast.BackBlastLengthCm <= 0.f || Blast.BackBlastDamage <= 0.f)
	{
		return;
	}
	const FVector Launcher = GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LauncherBackblastVictim), false, this);
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		ACharacter* Body = *It;
		if (Body == this || FVector::DistSquared(Body->GetActorLocation(), Launcher) > FMath::Square(Blast.BackBlastLengthCm + 50.f))
		{
			continue;
		}
		const float Damage = LauncherRules::BackblastDamageAt(Blast, Launcher, AimDir, Body->GetActorLocation());
		UHealthComponent* Health = Body->FindComponentByClass<UHealthComponent>();
		if (Damage <= 0.f || !Health || !Health->IsAlive())
		{
			continue;
		}
		Params.ClearIgnoredActors();
		Params.AddIgnoredActor(this);
		Params.AddIgnoredActor(Body);
		FHitResult Wall;
		if (World->LineTraceSingleByObjectType(Wall, Launcher, Body->GetActorLocation(), OperativeLauncherInternal::Geometry(), Params))
		{
			continue; // a wall between him and the body takes the blast
		}
		const FString Source = TEXT("RPG back-blast");
		if (AOperativeCharacter* Mate = Cast<AOperativeCharacter>(Body))
		{
			Mate->TakeHit(Damage, Source, false, true, this);
		}
		else
		{
			FDamageSpec Hit;
			Hit.Amount = Damage;
			Hit.DamageType = EDamageType::Explosive;
			Hit.AttackerSource = Source;
			Health->TakeDamage(Hit);
		}
		++LastBackblastVictims;
		LastBackblastDamage += Damage;
		UE_LOG(LogCodexTactics, Display, TEXT("[RPG] %s: back-blast hits %s for %.1f"), *DisplayName.ToString(), *Body->GetName(), Damage);
	}
}
