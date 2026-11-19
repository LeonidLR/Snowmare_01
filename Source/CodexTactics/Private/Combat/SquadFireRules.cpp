#include "Combat/SquadFireRules.h"
#include "Combat/FireGateRules.h"
#include "Data/GodotBalanceAsset.h"

namespace
{
	int32 StanceIndex(EOperativeStance Stance)
	{
		return Stance == EOperativeStance::Prone ? 2 : (Stance == EOperativeStance::Crouching ? 1 : 0);
	}
}

FSquadFireConfig SquadFireRules::ConfigFromBalance(const UGodotBalanceAsset* Balance)
{
	FSquadFireConfig Config;
	if (!Balance)
	{
		return Config;
	}
	const TCHAR* Names[3] = { TEXT("standing"), TEXT("crouching"), TEXT("prone") };
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Config.SwitchDelay[Index] = Balance->GetNumber(FName(FString(TEXT("stance_target_switch_delay_")) + Names[Index]), Config.SwitchDelay[Index]);
		Config.SwitchRatio[Index] = Balance->GetNumber(FName(FString(TEXT("stance_switch_distance_ratio_")) + Names[Index]), Config.SwitchRatio[Index]);
	}
	return Config;
}

float SquadFireRules::GetPostureRangeMultiplier(EOperativeStance Stance)
{
	return Stance == EOperativeStance::Prone ? 1.35f : (Stance == EOperativeStance::Crouching ? 1.15f : 1.f);
}

float SquadFireRules::GetStanceDamageMultiplier(EOperativeStance Stance)
{
	return Stance == EOperativeStance::Prone ? 1.6f : (Stance == EOperativeStance::Crouching ? 1.25f : 1.f);
}

FElevationAdvantage SquadFireRules::GetElevationAdvantage(float ShooterFeetZ, float TargetFeetZ)
{
	FElevationAdvantage Result;
	if (ShooterFeetZ + 100.f - TargetFeetZ >= 150.f)
	{
		Result.bElevated = true;
		Result.RangeMultiplier = 1.25f;
		Result.DamageMultiplier = 1.15f;
		Result.bBypassLowCover = true;
	}
	return Result;
}

const FDeadZoneConfig& SquadFireRules::GetDeadZoneConfig()
{
	return FireGateRules::GetConfig().DeadZone;
}

bool SquadFireRules::IsInDeadZone(const FVector& ShooterFeet, const FVector& TargetFeet)
{
	const FDeadZoneConfig& Config = GetDeadZoneConfig();
	const float DeltaZ = ShooterFeet.Z + Config.ChestOffsetCm - TargetFeet.Z;
	if (DeltaZ < Config.MinDeltaZCm)
	{
		return false;
	}
	const float Horizontal = FVector::Dist2D(ShooterFeet, TargetFeet);
	return Horizontal <= Config.MaxHorizontalCm && FMath::Atan2(DeltaZ, Horizontal) >= FMath::DegreesToRadians(Config.MinAngleDeg);
}

bool SquadFireRules::AppliesDeadZone(const FVector& ShooterFeet, const FVector& TargetFeet, bool bTraceBlocked, bool bLevelEdge)
{
	return IsInDeadZone(ShooterFeet, TargetFeet) && (bTraceBlocked || bLevelEdge);
}

bool SquadFireRules::IsLevelEdgeBetween(TConstArrayView<FFloorSample> Profile, float SpacingCm)
{
	return CombatGroundRules::HasLevelEdge(Profile, GetDeadZoneConfig().EdgeDropCm, SpacingCm);
}

FShotLineVerdict SquadFireRules::JudgeLine(EShotLineHit Hit, EOperativeStance Stance, const FElevationAdvantage& Elevation)
{
	FShotLineVerdict Verdict;
	switch (Hit)
	{
	case EShotLineHit::Clear:
		Verdict.bCanHit = true;
		break;
	case EShotLineHit::Barricade:
		if (Elevation.bElevated && Elevation.bBypassLowCover)
		{
			Verdict.bCanHit = true;
		}
		else if (Stance == EOperativeStance::Prone)
		{
			Verdict.bBarricadeBlocked = true;
		}
		else
		{
			Verdict.bCanHit = true;
			Verdict.Cover = Stance == EOperativeStance::Crouching ? 0.8f : 1.f;
		}
		break;
	default:
		break;
	}
	return Verdict;
}

bool SquadFireRules::MayClearRoofLip(const FRoofLipShot& Shot, const FRoofLipConfig& Lip)
{
	if (!(Lip.LipCm > 0.f) || !(Lip.PastCm > 0.f))
	{
		return false;
	}
	if (Shot.HitDistanceCm <= 1.f || Shot.HitDistanceCm > Lip.LipCm)
	{
		return false;
	}
	if (Shot.TargetDistanceCm <= Shot.HitDistanceCm + Lip.PastCm)
	{
		return false;
	}
	if (Shot.ShooterFeetZ - Shot.TargetFeetZ < Lip.MinDropCm)
	{
		return false;
	}
	return Shot.RimTopZ >= Shot.ShooterFeetZ - Lip.BandBelowCm && Shot.RimTopZ <= Shot.ShooterFeetZ + Lip.BandAboveCm;
}

FVector SquadFireRules::RoofLipRetryOrigin(const FVector& Muzzle, const FVector& Impact, float PastCm)
{
	const FVector Flat(Impact.X - Muzzle.X, Impact.Y - Muzzle.Y, 0.f);
	return FVector(Impact.X, Impact.Y, Muzzle.Z) + Flat.GetSafeNormal() * FMath::Max(PastCm, 0.f);
}

EShotAimPoint SquadFireRules::ChooseShotAimPoint(EShotLineHit BodyLine, bool bHasExposedPoint, EShotLineHit ExposedLine)
{
	// Only a wall in the body's way moves the aim; a barricade keeps JudgeLine's stance rules on the body.
	return BodyLine == EShotLineHit::Blocked && bHasExposedPoint && ExposedLine == EShotLineHit::Clear ? EShotAimPoint::Exposed : EShotAimPoint::Body;
}

bool SquadFireRules::IsSignificantlyCloser(float ClosestDistanceCm, float CurrentDistanceCm, float SwitchRatio)
{
	return ClosestDistanceCm < CurrentDistanceCm * SwitchRatio || ClosestDistanceCm < 350.f;
}

bool SquadFireRules::IsCrit(float Luck, float Roll01)
{
	return Roll01 * 100.f < Luck;
}

float SquadFireRules::ComputeShotDamage(float WeaponDamage, EOperativeStance Stance, float Cover, bool bCrit,
	const FElevationAdvantage& Elevation, float DistanceMultiplier)
{
	return WeaponDamage * GetStanceDamageMultiplier(Stance) * Cover * (bCrit ? 2.f : 1.f)
		* (Elevation.bElevated ? Elevation.DamageMultiplier : 1.f) * DistanceMultiplier;
}

int32 SquadFireRules::GetDistanceCells(float DistanceCm)
{
	return FMath::Max(1, FMath::RoundToInt(DistanceCm / 100.f / 1.5f));
}
