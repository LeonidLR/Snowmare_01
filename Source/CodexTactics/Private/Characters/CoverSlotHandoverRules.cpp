#include "Characters/CoverSlotHandoverRules.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace CoverSlotHandoverRules
{
	const FConfig& GetConfig()
	{
		static const FConfig Config = []
		{
			FConfig Loaded;
			FString Text;
			TSharedPtr<FJsonObject> Json;
			const TSharedPtr<FJsonObject>* Params = nullptr;
			if (FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Data/Tactics/cover_arrival.json")))
				&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid()
				&& Json->TryGetObjectField(TEXT("params"), Params) && Params && Params->IsValid())
			{
				double Value = 0.0;
				if ((*Params)->TryGetNumberField(TEXT("handover_retry_seconds"), Value) && FMath::IsFinite(Value) && Value > 0.0)
				{
					Loaded.RetrySeconds = static_cast<float>(Value);
				}
				if ((*Params)->TryGetNumberField(TEXT("handover_timeout_seconds"), Value) && FMath::IsFinite(Value) && Value > 0.0)
				{
					Loaded.TimeoutSeconds = static_cast<float>(Value);
				}
			}
			return Loaded;
		}();
		return Config;
	}

	bool IsVacating(bool bOwnMove, float GoalDistanceCm, float CombinedRadiusCm)
	{
		return bOwnMove && GoalDistanceCm >= CombinedRadiusCm;
	}

	EWaitResult DecideWait(bool bOccupied, double Now, double Deadline)
	{
		if (!bOccupied)
		{
			return EWaitResult::Enter;
		}
		return Now < Deadline ? EWaitResult::Wait : EWaitResult::Timeout;
	}
}
