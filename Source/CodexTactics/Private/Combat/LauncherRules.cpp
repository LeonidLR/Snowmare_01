#include "Combat/LauncherRules.h"

#include "Characters/OperativeMovementRules.h"
#include "Combat/ExplosionRules.h"

namespace LauncherRules
{
	bool StanceAllows(EOperativeStance Stance)
	{
		return Stance != EOperativeStance::Prone;
	}

	ELauncherBackblast ClassifyBackblast(float WallBehindCm, bool bRoofAbove, const FWeaponExplosionStats& Stats)
	{
		if (Stats.BackBlastLengthCm <= 0.f || WallBehindCm < 0.f)
		{
			return ELauncherBackblast::Clear;
		}
		if (WallBehindCm < Stats.BackBlastClearanceCm)
		{
			return ELauncherBackblast::WallBehind;
		}
		return bRoofAbove && WallBehindCm <= Stats.BackBlastLengthCm ? ELauncherBackblast::ClosedRoom : ELauncherBackblast::Clear;
	}

	float BackblastDamageAt(const FWeaponExplosionStats& Stats, const FVector& Launcher, const FVector& AimDir, const FVector& Point)
	{
		return Stats.BackBlastDamage * ExplosionRules::BackblastMultiplier(Launcher, AimDir, Point, Stats.BackBlastLengthCm, Stats.BackBlastHalfAngleDeg);
	}

	float MinimumRangeCm(const FWeaponStats& Stats)
	{
		return FMath::Max(Stats.Range.MinRangeCm, Stats.Projectile.ArmingDistanceCm);
	}

	ELauncherRefusal Refusal(bool bLoaded, EOperativeStance Stance, float TargetDistanceCm, ELauncherBackblast Backblast, const FWeaponStats& Stats)
	{
		if (!bLoaded)
		{
			return ELauncherRefusal::Empty;
		}
		if (!StanceAllows(Stance))
		{
			return ELauncherRefusal::Prone;
		}
		if (TargetDistanceCm < MinimumRangeCm(Stats))
		{
			return ELauncherRefusal::TooClose;
		}
		if (Backblast != ELauncherBackblast::Clear && Stats.Explosion.bBackBlastRefuses)
		{
			return ELauncherRefusal::Backblast;
		}
		return ELauncherRefusal::None;
	}

	FVector MissPoint(const FVector& AimPoint, const FVector& AimDir, float Roll01, float SideRoll)
	{
		const FVector Flat = FVector(AimDir.X, AimDir.Y, 0.f).GetSafeNormal();
		const FVector Forward = Flat.IsNearlyZero() ? FVector::ForwardVector : Flat;
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
		const float Scatter = FMath::Lerp(MinScatterCm, MaxScatterCm, FMath::Clamp(Roll01, 0.f, 1.f));
		const float Side = SideRoll < 0.5f ? -1.f : 1.f;
		// Mostly sideways, a little long or short (the same roll: deterministic for a seed).
		return AimPoint + Right * (Side * Scatter * 0.8f) + Forward * (Scatter * 0.6f * (Roll01 < 0.5f ? -1.f : 1.f));
	}

	FString RefusalText(ELauncherRefusal Refusal, ELauncherBackblast Backblast)
	{
		switch (Refusal)
		{
		case ELauncherRefusal::Prone: return TEXT("RPG: not prone - stand or kneel (the back-blast)");
		case ELauncherRefusal::TooClose: return TEXT("RPG: too close - the warhead would not arm");
		case ELauncherRefusal::Empty: return TEXT("RPG: no rocket loaded");
		case ELauncherRefusal::Backblast:
			return Backblast == ELauncherBackblast::ClosedRoom ? TEXT("RPG: not from a closed room - the back-blast") : TEXT("RPG: wall right behind - the back-blast");
		default:
			return FString();
		}
	}
}
