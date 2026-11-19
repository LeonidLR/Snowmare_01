#pragma once

#include "CoreMinimal.h"

namespace CoverSlotHandoverRules
{
	struct CODEXTACTICS_API FConfig
	{
		float RetrySeconds = 0.1f;
		float TimeoutSeconds = 3.f;
	};

	enum class EWaitResult : uint8
	{
		Enter,
		Wait,
		Timeout
	};

	/** Cover-slot handover timing from cover_arrival.json. */
	CODEXTACTICS_API const FConfig& GetConfig();

	/** Whether a pawn overlapping the endpoint is under orders to clear it. */
	CODEXTACTICS_API bool IsVacating(bool bOwnMove, float GoalDistanceCm, float CombinedRadiusCm);

	/** Resolve a pending arrival, giving a clear endpoint priority over the deadline. */
	CODEXTACTICS_API EWaitResult DecideWait(bool bOccupied, double Now, double Deadline);
}
