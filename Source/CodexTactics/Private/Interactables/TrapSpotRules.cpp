#include "Interactables/TrapSpotRules.h"

namespace TrapSpotRules
{
	ETrapSpotResponse DecideSquadResponse(bool bFormationActive, bool bAutonomy, int32 LivingMembers)
	{
		return bFormationActive && !bAutonomy && LivingMembers > 1 ? ETrapSpotResponse::HoldAndSolo : ETrapSpotResponse::HoldOnly;
	}

	bool IsInPeekSightCone(const FVector& Origin, const FVector& Direction, const FVector& Target, float RangeCm, float HalfAngleDeg)
	{
		const FVector ToTarget(Target.X - Origin.X, Target.Y - Origin.Y, 0.f);
		const float Distance = ToTarget.Size();
		const FVector Forward = Direction.GetSafeNormal2D();
		if (Distance <= KINDA_SMALL_NUMBER || Distance > RangeCm || Forward.IsNearlyZero())
		{
			return false;
		}
		const float Cos = FVector::DotProduct(Forward, ToTarget / Distance);
		return Cos >= FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(HalfAngleDeg, 0.f, 180.f))) - KINDA_SMALL_NUMBER;
	}

	FName GroupEventName(FName SpotEventTag)
	{
		return SpotEventTag.IsNone() ? NAME_None : FName(*FString::Printf(TEXT("TrapSpotted.%s"), *SpotEventTag.ToString()));
	}

	FName ParseSpottedStatTag(FName Stat)
	{
		static const FString Prefix(TEXT("traps_spotted."));
		const FString Name = Stat.ToString();
		return Name.StartsWith(Prefix) && Name.Len() > Prefix.Len() ? FName(*Name.Mid(Prefix.Len())) : NAME_None;
	}
}
