#include "Tactics/CoverRules.h"

#include "HAL/IConsoleManager.h"

namespace CoverTunables
{
	// -1 = the default of FCoverCombatConfig.
	static TAutoConsoleVariable<float> CVarHighAbsorb(TEXT("Codex.Cover.HighFrontalAbsorb"), -1.f,
		TEXT("Share of a frontal hit a high cover absorbs (default 0.9, user decision 2026-10-06); -1 = default."));
	static TAutoConsoleVariable<float> CVarLowAbsorb(TEXT("Codex.Cover.LowCrouchedAbsorb"), -1.f,
		TEXT("Share of a frontal hit a low cover absorbs for a crouched operative (default 0.35); -1 = default."));
	static TAutoConsoleVariable<float> CVarArc(TEXT("Codex.Cover.FrontalArcDeg"), -1.f,
		TEXT("Full width of the wall's frontal arc, degrees (default 160); -1 = default."));
	static TAutoConsoleVariable<float> CVarBlindAccuracy(TEXT("Codex.Cover.BlindFireAccuracy"), -1.f,
		TEXT("Hit chance multiplier of cover blind fire (default 0.6 = -40 %); -1 = default."));
	static TAutoConsoleVariable<float> CVarMinBlind(TEXT("Codex.Cover.MinBlindHitChance"), -1.f,
		TEXT("Floor of the hit chance of blind shots (default 0.05); -1 = default."));
}

namespace CoverRules
{
	const FCoverCombatConfig& GetConfig()
	{
		static FCoverCombatConfig Config;
		const FCoverCombatConfig Defaults;
		auto Pick = [](float Override, float Default) { return Override >= 0.f ? Override : Default; };
		Config.HighCoverFrontalAbsorb = Pick(CoverTunables::CVarHighAbsorb.GetValueOnGameThread(), Defaults.HighCoverFrontalAbsorb);
		Config.LowCoverCrouchedAbsorb = Pick(CoverTunables::CVarLowAbsorb.GetValueOnGameThread(), Defaults.LowCoverCrouchedAbsorb);
		Config.FrontalArcDeg = Pick(CoverTunables::CVarArc.GetValueOnGameThread(), Defaults.FrontalArcDeg);
		Config.CoverBlindFireAccuracyMultiplier = Pick(CoverTunables::CVarBlindAccuracy.GetValueOnGameThread(), Defaults.CoverBlindFireAccuracyMultiplier);
		Config.MinBlindFireHitChance = Pick(CoverTunables::CVarMinBlind.GetValueOnGameThread(), Defaults.MinBlindFireHitChance);
		return Config;
	}

	float AngleFromWallDeg(const FVector& WallNormal, const FVector& SlotLocation, const FVector& SourceLocation)
	{
		const FVector IntoWall = -FVector(WallNormal.X, WallNormal.Y, 0.f).GetSafeNormal();
		const FVector ToSource = FVector(SourceLocation.X - SlotLocation.X, SourceLocation.Y - SlotLocation.Y, 0.f).GetSafeNormal();
		if (IntoWall.IsNearlyZero() || ToSource.IsNearlyZero())
		{
			return 180.f;
		}
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(IntoWall, ToSource), -1.f, 1.f)));
	}

	bool IsInFrontalArc(const FVector& WallNormal, const FVector& SlotLocation, const FVector& SourceLocation, float ArcDeg)
	{
		return AngleFromWallDeg(WallNormal, SlotLocation, SourceLocation) <= ArcDeg * 0.5f;
	}

	float AbsorbFraction(const FCoverCombatConfig& Config, ECoverHeight Height, EOperativeStance Stance, bool bLeaning, bool bInArc)
	{
		if (!bInArc || bLeaning || Height == ECoverHeight::None)
		{
			return 0.f;
		}
		if (Height == ECoverHeight::HighCover)
		{
			return Config.HighCoverFrontalAbsorb;
		}
		switch (Stance)
		{
		case EOperativeStance::Crouching:
			return Config.LowCoverCrouchedAbsorb;
		case EOperativeStance::Prone:
			return Config.LowCoverProneAbsorb;
		default:
			return 0.f;
		}
	}

	float ApplyAbsorb(float Amount, float Fraction)
	{
		return Amount * (1.f - FMath::Clamp(Fraction, 0.f, 1.f));
	}

	float CoverBlindFireHitChance(const FCoverCombatConfig& Config, float BaseHitChance)
	{
		return FMath::Max(Config.MinBlindFireHitChance, BaseHitChance * Config.CoverBlindFireAccuracyMultiplier);
	}

	float CombinedBlindFireHitChance(const FCoverCombatConfig& Config, float BaseHitChance, bool bCoverBlind, bool bGhostBlind, float GhostMultiplier)
	{
		if (!bCoverBlind && !bGhostBlind)
		{
			return BaseHitChance;
		}
		float Chance = BaseHitChance;
		if (bCoverBlind)
		{
			Chance *= Config.CoverBlindFireAccuracyMultiplier;
		}
		if (bGhostBlind)
		{
			Chance *= GhostMultiplier;
		}
		return FMath::Max(Config.MinBlindFireHitChance, Chance);
	}

	bool IsHeadshotImmune(ECoverHeight Height, bool bLeaning)
	{
		return Height != ECoverHeight::None && !bLeaning;
	}

	float HeadshotChance(const FCoverCombatConfig& Config, float AttackerCritChance, bool bImmune)
	{
		return bImmune ? Config.CoverHeadshotChance : AttackerCritChance;
	}

	bool HiddenFromObserver(ECoverHeight Height, bool bLeaning, bool bObserverInArc)
	{
		return Height == ECoverHeight::HighCover && !bLeaning && bObserverInArc;
	}

	float WindChillMultiplier(const FColdConfig& Config, bool bInCover)
	{
		return bInCover ? FMath::Clamp(Config.CoverWindChillMultiplier, 0.f, 1.f) : 1.f;
	}

	EOperativeStance DefaultStanceFor(ECoverHeight Height)
	{
		return Height == ECoverHeight::HighCover ? EOperativeStance::Standing : EOperativeStance::Crouching;
	}

	FVector CornerMuzzle(const FCoverSlot& Slot, ECoverFacing Facing, const FVector& Muzzle, float OffsetCm)
	{
		const FVector Side = Facing == ECoverFacing::Right ? Slot.RightTangent() : -Slot.RightTangent();
		return Muzzle + Side * OffsetCm;
	}
}
