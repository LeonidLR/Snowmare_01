#include "Vehicles/VehicleHullRules.h"

namespace VehicleHullRules
{
	float GetSourceFactor(EVehicleDamageSource Source, const FVehicleHullConfig& Config)
	{
		switch (Source)
		{
		case EVehicleDamageSource::SmallMelee:
			return Config.SmallMeleeFactor;
		case EVehicleDamageSource::HeavyMelee:
			return Config.HeavyMeleeFactor;
		case EVehicleDamageSource::Acid:
			return Config.AcidFactor;
		case EVehicleDamageSource::Rifle:
			return Config.RifleFactor;
		case EVehicleDamageSource::Explosion:
			return Config.ExplosionFactor;
		case EVehicleDamageSource::Mine:
		case EVehicleDamageSource::Ram:
			return 1.f;
		default:
			return Config.OtherFactor;
		}
	}

	float GetHullDamage(float Amount, EVehicleDamageSource Source, const FVehicleHullConfig& Config)
	{
		if (Source == EVehicleDamageSource::Mine)
		{
			return FMath::Max(0.f, Config.MineDamage);
		}
		return FMath::Max(0.f, Amount) * FMath::Max(0.f, GetSourceFactor(Source, Config));
	}

	EVehicleDamageSource ClassifyEnemyAttack(EEnemyArchetype Archetype)
	{
		switch (Archetype)
		{
		case EEnemyArchetype::Brute:
			return EVehicleDamageSource::HeavyMelee;
		case EEnemyArchetype::Spitter:
		case EEnemyArchetype::CryoDrone:
			return EVehicleDamageSource::Acid;
		case EEnemyArchetype::Marksman:
			return EVehicleDamageSource::Rifle;
		default:
			return EVehicleDamageSource::SmallMelee;
		}
	}

	FVehicleHullState MakeFullHull(const FVehicleHullConfig& Config)
	{
		FVehicleHullState State;
		State.Health = FMath::Max(1.f, Config.MaxHealth);
		State.bDisabled = false;
		return State;
	}

	EVehicleHullChange ApplyDamage(FVehicleHullState& State, float HullDamage, const FVehicleHullConfig& Config, float& OutDealt)
	{
		OutDealt = 0.f;
		if (State.bDisabled || HullDamage <= 0.f || State.Health <= 0.f)
		{
			return EVehicleHullChange::None;
		}
		const float Max = FMath::Max(1.f, Config.MaxHealth);
		const float Before = State.Health;
		State.Health = FMath::Max(0.f, Before - HullDamage);
		OutDealt = Before - State.Health;
		if (State.Health <= 0.f)
		{
			State.bDisabled = true;
			return EVehicleHullChange::Disabled;
		}
		const float Low = Max * Config.LowHealthFraction;
		return Before > Low && State.Health <= Low ? EVehicleHullChange::DamagedLow : EVehicleHullChange::Damaged;
	}

	float GetRepairSeconds(EOperativeRole Role, const FVehicleHullConfig& Config)
	{
		return Role == EOperativeRole::Engineer ? Config.RepairSeconds : Config.OtherRepairSeconds;
	}

	void Repair(FVehicleHullState& State, const FVehicleHullConfig& Config)
	{
		const float Max = FMath::Max(1.f, Config.MaxHealth);
		State.Health = FMath::Clamp(FMath::Max(State.Health, Max * Config.RepairFraction), 1.f, Max);
		State.bDisabled = false;
	}

	FVehicleHullState RestoreHull(float SavedHealth, bool bSavedDisabled, const FVehicleHullConfig& Config)
	{
		FVehicleHullState State;
		State.Health = FMath::Clamp(SavedHealth, 0.f, FMath::Max(1.f, Config.MaxHealth));
		State.bDisabled = bSavedDisabled || State.Health <= 0.f;
		if (State.bDisabled)
		{
			State.Health = 0.f;
		}
		return State;
	}

	bool IsEnemyTarget(int32 CrewCount, bool bDisabled, bool bHordeKnowsSquad, float HullDistanceCm, const FVehicleHullConfig& Config)
	{
		return CrewCount > 0 && !bDisabled && (bHordeKnowsSquad || HullDistanceCm <= Config.EnemyAwarenessCm);
	}

	float GetEnemyTargetWeight(bool bSmallEnemy, const FVehicleHullConfig& Config)
	{
		return bSmallEnemy ? Config.EnemyTargetWeightSmall : Config.EnemyTargetWeightLarge;
	}

	float GetBlastHullDamage(float BlastDamageAtHull, bool bTriggeredByThisVehicle, const FVehicleHullConfig& Config, EVehicleDamageSource& OutSource)
	{
		OutSource = bTriggeredByThisVehicle ? EVehicleDamageSource::Mine : EVehicleDamageSource::Explosion;
		return GetHullDamage(BlastDamageAtHull, OutSource, Config);
	}

	FString GetHudLine(const FVehicleHullState& State, const FVehicleHullConfig& Config, float RepairProgress)
	{
		if (State.bDisabled)
		{
			return RepairProgress > 0.f ? FString::Printf(TEXT("HULL REPAIRING %d%%"), FMath::Clamp(FMath::RoundToInt(RepairProgress * 100.f), 0, 100))
				: FString(TEXT("HULL DISABLED - Engineer [Y] repairs"));
		}
		return FString::Printf(TEXT("HULL %d/%d"), FMath::CeilToInt(State.Health), FMath::RoundToInt(FMath::Max(1.f, Config.MaxHealth)));
	}

	FString GetSourceName(EVehicleDamageSource Source)
	{
		const UEnum* Enum = StaticEnum<EVehicleDamageSource>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Source)) : FString();
	}

	EVehicleObjectShot CheckObjectShot(const FObjectShotCheck& Check)
	{
		if (!Check.bShootable)
		{
			return EVehicleObjectShot::NotATarget;
		}
		if (Check.bDisabled)
		{
			return EVehicleObjectShot::Disabled;
		}
		if (!Check.bHasGunner)
		{
			return EVehicleObjectShot::NoGunner;
		}
		if (Check.bHeld)
		{
			return EVehicleObjectShot::Held;
		}
		if (Check.ShellsLeft <= 0)
		{
			return EVehicleObjectShot::NoShells;
		}
		if (Check.DistanceCm > Check.CannonRangeCm)
		{
			return EVehicleObjectShot::OutOfRange;
		}
		if (Check.HullDistanceCm < Check.DeadZoneCm)
		{
			return EVehicleObjectShot::DeadZone;
		}
		return Check.bLineOfFire ? EVehicleObjectShot::None : EVehicleObjectShot::NoLineOfFire;
	}
}
