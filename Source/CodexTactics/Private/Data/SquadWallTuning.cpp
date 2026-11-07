#include "Data/SquadWallTuning.h"

#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace SquadWallTuningLocal
{
	FSquadWallTuning WallTuningCurrent;
	bool bWallTuningLoaded = false;

	void ReadWallTuningNumber(const FJsonObject& Json, const TCHAR* Key, float& Out, float Min, float Max, float Scale = 1.f)
	{
		double Value = 0.0;
		if (Json.TryGetNumberField(Key, Value))
		{
			Out = FMath::Clamp(static_cast<float>(Value) * Scale, Min, Max);
		}
	}
}

FString SquadWallTuning::GetDefaultPath()
{
	return FPaths::ProjectContentDir() / TEXT("Data/AI/squad_walls.json");
}

FSquadWallTuning SquadWallTuning::FromJson(const FJsonObject& Json, const FSquadWallTuning& Base)
{
	using namespace SquadWallTuningLocal;
	FSquadWallTuning Tuning = Base;
	ReadWallTuningNumber(Json, TEXT("wall_corner_look_deg"), Tuning.WallCornerLookDeg, 90.f, 180.f);
	ReadWallTuningNumber(Json, TEXT("follower_cover_radius_m"), Tuning.FollowerCoverRadiusCm, 0.f, 2000.f, 100.f);
	ReadWallTuningNumber(Json, TEXT("follower_cover_side_deg"), Tuning.FollowerCoverSideDeg, 0.f, 180.f);
	ReadWallTuningNumber(Json, TEXT("reform_stand_m"), Tuning.ReformStandCm, 0.f, 5000.f, 100.f);
	ReadWallTuningNumber(Json, TEXT("wall_path_braking_cm"), Tuning.WallPathBrakingCm, 0.f, 500.f);
	ReadWallTuningNumber(Json, TEXT("reform_along_wall_off_m"), Tuning.ReformAlongWallOffCm, 0.f, 500.f, 100.f);
	ReadWallTuningNumber(Json, TEXT("corner_sweep_s"), Tuning.CornerSweepSeconds, 1.f, 60.f);
	return Tuning;
}

bool SquadWallTuning::Reload()
{
	using namespace SquadWallTuningLocal;
	bWallTuningLoaded = true;
	FString Text;
	TSharedPtr<FJsonObject> Json;
	if (!FFileHelper::LoadFileToString(Text, *GetDefaultPath()) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json.IsValid())
	{
		return false;
	}
	WallTuningCurrent = FromJson(*Json);
	UE_LOG(LogCodexTactics, Display, TEXT("Squad wall tuning from %s: corner look %.0f deg, follower cover radius %.0f cm (side %.0f deg)"), *GetDefaultPath(),
		WallTuningCurrent.WallCornerLookDeg, WallTuningCurrent.FollowerCoverRadiusCm, WallTuningCurrent.FollowerCoverSideDeg);
	return true;
}

const FSquadWallTuning& SquadWallTuning::Get()
{
	using namespace SquadWallTuningLocal;
	if (!bWallTuningLoaded)
	{
		Reload();
	}
	return WallTuningCurrent;
}
