#include "Data/WeaponDataAsset.h"

FWeaponStats UWeaponDataAsset::MakeBaseStats() const
{
	FWeaponStats Out;
	Out.Kind = bUsesAmmo ? EWeaponKind::Hitscan : EWeaponKind::Melee;
	Out.Handling = Handling;
	Out.Damage.BaseDamage = BaseDamage;
	Out.Damage.DamageType = DamageType;
	Out.Damage.ArmorPenetration = ArmorPenetration;
	Out.Range.MaxRangeCm = AttackRangeCm;
	Out.Range.EffectiveRangeCm = EffectiveRangeCm;
	Out.Fire.ShotIntervalSeconds = FireRate;
	Out.Fire.FullAutoIntervalSeconds = FullAutoInterval;
	Out.Fire.SuppressionWeight = SuppressionWeight;
	Out.Ammo.bUsesAmmo = bUsesAmmo;
	Out.Ammo.MagazineSize = MaxClipSize;
	Out.Ammo.ReserveAmmo = DefaultReserveAmmo;
	Out.Ammo.ReloadSeconds = ReloadTime;
	Out.Accuracy.HitChanceByCell = BaseHitChances;
	Out.Accuracy.DamageMultiplierByCell = DistanceDamageMultipliers;
	Out.Status.Effect = StatusEffect;
	Out.Status.DurationSeconds = StatusDuration;
	Out.Status.TickDamage = StatusTickDamage;
	Out.Thermal.SelfColdPerShot = SelfColdGeneration;
	Out.Thermal.SelfWarmthPerShot = SelfWarmthGeneration;
	Out.TurnBased.AttackShape = AttackShape;
	Out.TurnBased.MaxRangeCells = MaxRangeCells;
	return Out;
}

void UWeaponDataAsset::RebuildStatsFromAsset()
{
	Stats = MakeBaseStats();
}

void UWeaponDataAsset::PostInitProperties()
{
	Super::PostInitProperties();
	RebuildStatsFromAsset(); // the CDO and NewObject: the defaults
}

void UWeaponDataAsset::PostLoad()
{
	Super::PostLoad();
	RebuildStatsFromAsset(); // a loaded DA_Weapon_*: its saved base fields
}

void UWeaponDataAsset::PostDuplicate(bool bDuplicateForPIE)
{
	Super::PostDuplicate(bDuplicateForPIE);
	RebuildStatsFromAsset(); // Stats is transient: a duplicate starts from its base fields
}

#if WITH_EDITOR
void UWeaponDataAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RebuildStatsFromAsset(); // a Details edit of a base field shows at once (the JSON applies again at the next StartPlay)
}
#endif
