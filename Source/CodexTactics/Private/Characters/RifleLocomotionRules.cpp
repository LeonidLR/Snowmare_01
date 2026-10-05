#include "Characters/RifleLocomotionRules.h"

int32 RifleLocomotionRules::DirectionSector(float DirectionDeg)
{
	const float Wrapped = FMath::UnwindDegrees(DirectionDeg); // -180..180
	const float Positive = Wrapped < 0.f ? Wrapped + 360.f : Wrapped; // 0..360, clockwise (right) positive
	return FMath::FloorToInt((Positive + 22.5f) / 45.f) % SectorCount;
}

const TCHAR* RifleLocomotionRules::SectorName(int32 Sector)
{
	static const TCHAR* Names[SectorCount] = { TEXT("F"), TEXT("FR"), TEXT("RR"), TEXT("BR"), TEXT("B"), TEXT("BL"), TEXT("LL"), TEXT("FL") };
	return Names[((Sector % SectorCount) + SectorCount) % SectorCount];
}

int32 RifleLocomotionRules::StartFoot(int32 Sector)
{
	return Sector >= 1 && Sector <= 3 ? 1 : 0;
}

int32 RifleLocomotionRules::StopFoot(float WalkSeconds, float CycleSeconds)
{
	if (CycleSeconds <= KINDA_SMALL_NUMBER)
	{
		return 0;
	}
	return FMath::FloorToInt(FMath::Max(0.f, WalkSeconds) / (CycleSeconds * 0.5f)) % 2;
}

int32 RifleLocomotionRules::ClipIndex(int32 Sector, int32 Foot)
{
	return (((Sector % SectorCount) + SectorCount) % SectorCount) * 2 + FMath::Clamp(Foot, 0, 1);
}

float RifleLocomotionRules::DeltaYaw(float FromDeg, float ToDeg)
{
	float Delta = FMath::UnwindDegrees(ToDeg - FromDeg);
	if (Delta <= -180.f)
	{
		Delta += 360.f;
	}
	return Delta;
}

bool RifleLocomotionRules::ShouldTurnInPlace(float RootYawOffsetDeg, float Speed, bool bMoveIntent)
{
	return !bMoveIntent && Speed < StartSpeed && FMath::Abs(RootYawOffsetDeg) >= TurnTriggerDeg;
}

int32 RifleLocomotionRules::TurnBucket(float RootYawOffsetDeg, bool& OutRight)
{
	// The actor turned right (yaw grew) -> the mesh lags behind -> offset negative -> turn right. A full flip turns right.
	OutRight = RootYawOffsetDeg <= 0.f || FMath::Abs(RootYawOffsetDeg) >= 180.f;
	const float Abs = FMath::Min(FMath::Abs(RootYawOffsetDeg), 180.f);
	return FMath::Clamp(FMath::RoundToInt(Abs / 45.f) - 1, 0, TurnBucketCount - 1);
}

float RifleLocomotionRules::BucketDegrees(int32 Bucket)
{
	return 45.f * (FMath::Clamp(Bucket, 0, TurnBucketCount - 1) + 1);
}

bool RifleLocomotionRules::ShouldStart(float Speed, bool bMoveIntent)
{
	return bMoveIntent && Speed > StartSpeed;
}

bool RifleLocomotionRules::ShouldStop(bool bMoveIntent)
{
	return !bMoveIntent;
}
