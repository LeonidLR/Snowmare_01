#include "Formation/FormationLookFilterRules.h"

namespace FormationLookFilterRules
{
	TOptional<float> Step(FFormationLookFilter& Filter, const TOptional<float>& Raw, float DeltaSeconds, float JumpDeg, float HoldSeconds,
		float BigJumpDeg, float BigHoldSeconds)
	{
		if (!Raw.IsSet())
		{
			Filter = FFormationLookFilter();
			return TOptional<float>();
		}
		const float New = FRotator::NormalizeAxis(Raw.GetValue());
		Filter.SinceBigJumpSeconds += FMath::Max(0.f, DeltaSeconds);
		if (!Filter.Applied.IsSet() || FMath::Abs(FMath::FindDeltaAngleDegrees(Filter.Applied.GetValue(), New)) <= JumpDeg)
		{
			Filter.Applied = New;
			Filter.Pending.Reset();
			Filter.PendingSeconds = 0.f;
			return Filter.Applied;
		}
		const bool bBig = FMath::Abs(FMath::FindDeltaAngleDegrees(Filter.Applied.GetValue(), New)) >= BigJumpDeg;
		// Hysteresis: right after a big jump, a jump straight back to the look before it is rejected (a limit cycle between two sides).
		if (Filter.Previous.IsSet() && Filter.SinceBigJumpSeconds < BigHoldSeconds
			&& FMath::Abs(FMath::FindDeltaAngleDegrees(Filter.Previous.GetValue(), New)) <= JumpDeg)
		{
			Filter.Pending.Reset();
			Filter.PendingSeconds = 0.f;
			return Filter.Applied;
		}
		// A far jump: it must hold (stay within JumpDeg of itself) for HoldSeconds; a flip back and forth never does.
		if (Filter.Pending.IsSet() && FMath::Abs(FMath::FindDeltaAngleDegrees(Filter.Pending.GetValue(), New)) <= JumpDeg)
		{
			Filter.PendingSeconds += FMath::Max(0.f, DeltaSeconds);
		}
		else
		{
			Filter.PendingSeconds = FMath::Max(0.f, DeltaSeconds);
		}
		Filter.Pending = New;
		if (Filter.PendingSeconds >= (bBig ? FMath::Max(HoldSeconds, BigHoldSeconds) : HoldSeconds))
		{
			if (bBig)
			{
				Filter.Previous = Filter.Applied;
				Filter.SinceBigJumpSeconds = 0.f;
			}
			Filter.Applied = New;
			Filter.Pending.Reset();
			Filter.PendingSeconds = 0.f;
		}
		return Filter.Applied;
	}
}
