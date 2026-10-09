#include "Tactics/TurnAttackTimeline.h"

namespace TurnAttackTimeline
{
	ETurnShotStep NextStep(const FTurnShotReadiness& Readiness)
	{
		if (Readiness.bAttackerLost)
		{
			return ETurnShotStep::Drop;
		}
		if (Readiness.Elapsed >= MaxWaitSeconds)
		{
			return ETurnShotStep::Fire;
		}
		const bool bOnTarget = FMath::Abs(Readiness.BodyYawErrorDeg) <= AimToleranceDeg;
		return bOnTarget && !Readiness.bStanceSettling ? ETurnShotStep::Fire : ETurnShotStep::Wait;
	}
}
