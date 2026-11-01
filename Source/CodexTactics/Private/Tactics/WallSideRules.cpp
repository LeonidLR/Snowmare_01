// WallSideRules / ClickPickRules: the wall side a cursor click means and the tight operative pick at a wall (the game designer's play-test
// 2026-11-01, approved fixes a / b). Godot reference: none (UE-only).

#include "Tactics/WallSideRules.h"

namespace WallSideRules
{
	EWallClickSide SideOfTopHit(float HitAlong, float NearFaceAlong, float FarFaceAlong)
	{
		const float Thickness = FarFaceAlong - NearFaceAlong;
		if (Thickness <= 0.f || Thickness > MaxWallThicknessCm)
		{
			return EWallClickSide::None;
		}
		const float Middle = 0.5f * (NearFaceAlong + FarFaceAlong);
		return HitAlong > Middle ? EWallClickSide::Far : EWallClickSide::Near;
	}

	float FarGroundReachCm(float WallHeightCm, float CameraPitchDeg)
	{
		const float Down = FMath::Abs(CameraPitchDeg);
		if (Down < 5.f || WallHeightCm <= 0.f)
		{
			return WallHeightCm <= 0.f ? FarGroundReachMinCm : FarGroundReachMaxCm;
		}
		const float Hidden = WallHeightCm / FMath::Tan(FMath::DegreesToRadians(FMath::Min(Down, 89.f)));
		return FMath::Clamp(Hidden + FarGroundMarginCm, FarGroundReachMinCm, FarGroundReachMaxCm);
	}

	bool IsGroundBehindWall(float DistanceCm, float ReachCm, float FaceNormalAlongView, bool bHighWall, bool bLeaderOnOtherSide)
	{
		return DistanceCm >= 0.f && DistanceCm <= ReachCm && FaceNormalAlongView >= FaceAlongViewMin && bHighWall && bLeaderOnOtherSide;
	}
}

namespace ClickPickRules
{
	float PickRadius(bool bSquadAtWall, bool bGroundHit, float SelectRadius)
	{
		if (!bSquadAtWall)
		{
			return SelectRadius;
		}
		return bGroundHit ? FMath::Min(TightPickRadiusCm, SelectRadius) : 0.f;
	}
}
