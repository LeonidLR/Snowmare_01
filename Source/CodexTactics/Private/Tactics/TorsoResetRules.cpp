#include "Tactics/TorsoResetRules.h"

namespace TorsoResetRules
{
	FRelease AfterExit(bool bKeepAimPitch)
	{
		FRelease Out;
		Out.bClearAimPitch = !bKeepAimPitch;
		return Out;
	}

	bool Settled(float ErrorDeg, float InterpSpeed, float Seconds, float LimitDeg)
	{
		const float Left = FMath::Abs(ErrorDeg) * FMath::Exp(-FMath::Max(InterpSpeed, 0.f) * FMath::Max(Seconds, 0.f));
		return Left <= LimitDeg;
	}
}
