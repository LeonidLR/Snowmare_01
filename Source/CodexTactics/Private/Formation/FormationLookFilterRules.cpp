#include "Formation/FormationLookFilterRules.h"

namespace FormationLookFilterRules
{
	TOptional<float> Step(FFormationLookFilter& Filter, const TOptional<float>& Raw, float DeltaSeconds, float JumpDeg, float HoldSeconds)
	{
		if (!Raw.IsSet())
		{
			Filter = FFormationLookFilter();
			return TOptional<float>();
		}
		const float New = FRotator::NormalizeAxis(Raw.GetValue());
		if (!Filter.Applied.IsSet() || FMath::Abs(FMath::FindDeltaAngleDegrees(Filter.Applied.GetValue(), New)) <= JumpDeg)
		{
			Filter.Applied = New;
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
		if (Filter.PendingSeconds >= HoldSeconds)
		{
			Filter.Applied = New;
			Filter.Pending.Reset();
			Filter.PendingSeconds = 0.f;
		}
		return Filter.Applied;
	}
}
