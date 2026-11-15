#include "Tactics/LookHoldRules.h"

namespace LookHoldRules
{
	int32 GetDirectionSector(float RelativeMoveDeg)
	{
		const float Wrapped = FMath::Fmod(FRotator::ClampAxis(RelativeMoveDeg) + 22.5f, 360.f);
		return FMath::Clamp(FMath::FloorToInt(Wrapped / 45.f), 0, 7);
	}

	int32 GetGaitStartSector(int32 LeadSector, bool bBodyHeldByLook)
	{
		return bBodyHeldByLook ? ((LeadSector % 8) + 8) % 8 : 0;
	}

	bool IsOffGaitStartSector(int32 StartSector, float LegDirectionDeg, float ToleranceDeg)
	{
		const float StartDeg = static_cast<float>(((StartSector % 8) + 8) % 8) * 45.f;
		return FMath::Abs(FMath::FindDeltaAngleDegrees(StartDeg, LegDirectionDeg)) > FMath::Max(0.f, ToleranceDeg);
	}

	bool IsValidLookPoint(const FVector& LookPoint, const FVector& OperativeLocation, float MinDistanceCm)
	{
		return FVector::DistSquared2D(LookPoint, OperativeLocation) > FMath::Square(FMath::Max(0.f, MinDistanceCm));
	}

	bool ShouldClearOnIncapacitated(bool bDead, bool bKnockedDown, bool bPanicking, bool bClimbingLadder, bool bVaulting, bool bCarrying)
	{
		return bDead || bKnockedDown || bPanicking || bClimbingLadder || bVaulting || bCarrying;
	}

	bool IsLookHoldEligible(bool bInStealthMode, bool bSoloMode, bool bIsLeader, bool bGuarding)
	{
		if (bInStealthMode)
		{
			return true;
		}
		return bSoloMode || bIsLeader || bGuarding;
	}

	float ClampResidualTwist(float TwistDeg, float LimitDeg)
	{
		const float Limit = FMath::Max(0.f, LimitDeg);
		return FMath::Clamp(FRotator::NormalizeAxis(TwistDeg), -Limit, Limit);
	}
}
