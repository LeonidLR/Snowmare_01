#include "Survival/TacticalHeatRules.h"
#include "Combat/GrenadeRules.h"

bool FHeatBurnState::Ignite(float Seconds)
{
	if (Seconds <= 0.f)
	{
		return false;
	}
	bBurning = true;
	bBurntOut = false;
	TimeLeft = Seconds;
	return true;
}

bool FHeatBurnState::Step(float DeltaSeconds)
{
	if (!bBurning || DeltaSeconds <= 0.f)
	{
		return false;
	}
	TimeLeft = FMath::Max(0.f, TimeLeft - DeltaSeconds);
	if (TimeLeft > 0.f)
	{
		return false;
	}
	bBurning = false;
	bBurntOut = true;
	return true;
}

bool FHeatBurnState::AdvanceRound(float SecondsPerRound)
{
	// The last round's remainder of a float division must not leave a sliver burning.
	return Step(SecondsPerRound + KINDA_SMALL_NUMBER);
}

void FHeatBurnState::Stop()
{
	bBurning = false;
}

namespace TacticalHeatRules
{
	bool CanUseHeatPack(float Cold, bool bFrostbitten)
	{
		return bFrostbitten || Cold > 0.f;
	}

	FHeatPackResult ApplyHeatPack(const FTacticalHeatConfig& Config, float Cold, bool bFrostbitten)
	{
		FHeatPackResult Result;
		Result.NewCold = FMath::Clamp(Cold - Config.HeatPackColdReduction, 0.f, 100.f);
		Result.bClearsFrostbite = bFrostbitten;
		Result.BufferSeconds = Config.HeatPackBufferSeconds;
		return Result;
	}

	float GetBufferAccumulationMultiplier(const FTacticalHeatConfig& Config, float BufferSecondsLeft)
	{
		return BufferSecondsLeft > 0.f ? FMath::Clamp(Config.HeatPackAccumulationMultiplier, 0.f, 1.f) : 1.f;
	}

	float GetCampfireBurnSeconds(const FTacticalHeatConfig& Config, bool bWindExposed)
	{
		return Config.CampfireBurnSeconds * (bWindExposed ? FMath::Clamp(Config.CampfireWindBurnMultiplier, 0.05f, 1.f) : 1.f);
	}

	bool IsWindExposed(const FTacticalHeatConfig& Config, float GroundZ)
	{
		return GroundZ >= Config.CampfireWindHeight;
	}

	float SecondsPerRound(float TotalSeconds, int32 Rounds)
	{
		return TotalSeconds / FMath::Max(1, Rounds);
	}

	bool ShouldAdvanceRealTime(ECodexCombatMode CombatMode, bool bWorldAIPaused)
	{
		return CombatMode != ECodexCombatMode::TacticalPause && CombatMode != ECodexCombatMode::TurnBased && !bWorldAIPaused;
	}

	bool IsInHeatRadius(float Distance, float Radius, float Tolerance)
	{
		return Distance <= Radius + Tolerance;
	}

	EFirePuddleZone ClassifyPuddleZone(const FTacticalHeatConfig& Config, float Distance)
	{
		if (Distance <= Config.PuddleCoreRadius)
		{
			return EFirePuddleZone::Core;
		}
		return Distance <= Config.PuddleHeatRadius ? EFirePuddleZone::Radiant : EFirePuddleZone::Outside;
	}

	float GetPuddleLifetime(const FTacticalHeatConfig& Config, float Roll01)
	{
		const float Min = FMath::Min(Config.PuddleLifetimeMinSeconds, Config.PuddleLifetimeMaxSeconds);
		const float Max = FMath::Max(Config.PuddleLifetimeMinSeconds, Config.PuddleLifetimeMaxSeconds);
		return FMath::Lerp(Min, Max, FMath::Clamp(Roll01, 0.f, 1.f));
	}

	bool IsRevealedByFlare(const FTacticalHeatConfig& Config, bool bFlareBurning, float Distance)
	{
		return bFlareBurning && Distance <= Config.FlareRevealRadius;
	}

	float GetFlareThrowRange(const FTacticalHeatConfig& Config, EOperativeStance Stance)
	{
		return GrenadeRules::EffectiveRange(Config.FlareThrowRange, Stance);
	}

	int32 GetCraftableCampfires(const FTacticalHeatConfig& Config, int32 Matches, int32 Firewood, int32 FlameFuel)
	{
		const int32 ByMatches = Config.CampfireMatchesCost > 0 ? Matches / Config.CampfireMatchesCost : MAX_int32;
		const int32 ByWood = Config.CampfireFirewoodCost > 0 ? Firewood / Config.CampfireFirewoodCost : MAX_int32;
		const int32 ByFuel = FlameFuel / FMath::Max(1, Config.CampfireFlameFuelCost);
		const int64 ByFire = FMath::Min<int64>(static_cast<int64>(ByWood) + ByFuel, MAX_int32);
		return FMath::Max(0, FMath::Min(ByMatches, static_cast<int32>(ByFire)));
	}

	bool ConsumeCampfireMaterials(const FTacticalHeatConfig& Config, int32& Matches, int32& Firewood, int32& FlameFuel)
	{
		if (GetCraftableCampfires(Config, Matches, Firewood, FlameFuel) <= 0)
		{
			return false;
		}
		Matches -= Config.CampfireMatchesCost;
		if (Config.CampfireFirewoodCost <= 0 || Firewood >= Config.CampfireFirewoodCost)
		{
			Firewood -= FMath::Max(0, Config.CampfireFirewoodCost);
		}
		else
		{
			FlameFuel -= FMath::Max(1, Config.CampfireFlameFuelCost);
		}
		return true;
	}

	FTacticalHeatLoadout GetLoadout(const FTacticalHeatConfig& Config, int32 RoleIndex)
	{
		return Config.StartingLoadout.IsValidIndex(RoleIndex) ? Config.StartingLoadout[RoleIndex] : FTacticalHeatLoadout();
	}
}
