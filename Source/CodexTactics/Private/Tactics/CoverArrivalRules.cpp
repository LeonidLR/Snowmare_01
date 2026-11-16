#include "Tactics/CoverArrivalRules.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace CoverArrivalRules
{
	bool LoadConfigFromJson(const FString& JsonString, FCoverArrivalConfig& OutConfig)
	{
		TSharedPtr<FJsonObject> Json;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonString), Json) || !Json.IsValid())
		{
			return false;
		}
		FCoverArrivalConfig Config;
		const TSharedPtr<FJsonObject>* Params = nullptr;
		double Value = 0.0;
		if (Json->TryGetObjectField(TEXT("params"), Params) && Params && (*Params)->TryGetNumberField(TEXT("pass_squadmate_min_gap_cm"), Value))
		{
			Config.PassSquadmateMinGapCm = FMath::Clamp(static_cast<float>(Value), 0.f, 100.f);
		}
		OutConfig = Config;
		return true;
	}

	const FCoverArrivalConfig& GetConfig()
	{
		static const FCoverArrivalConfig Config = []
		{
			FCoverArrivalConfig Loaded;
			FString Text;
			if (FFileHelper::LoadFileToString(Text, *(FPaths::ProjectContentDir() / TEXT("Data/Tactics/cover_arrival.json"))))
			{
				LoadConfigFromJson(Text, Loaded);
			}
			return Loaded;
		}();
		return Config;
	}

	bool ShouldPassSquadmate(bool bSweepBlocked, bool bBlockerIsSquadmate, bool bBlockerInCover, float GapCm, const FCoverArrivalConfig& Config)
	{
		return bSweepBlocked && bBlockerIsSquadmate && !bBlockerInCover && GapCm > Config.PassSquadmateMinGapCm;
	}
}
