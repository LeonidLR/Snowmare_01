#include "Characters/StealthModeRules.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace StealthModeRules
{
	bool EnableForSelection(int32 SelectedCount, int32 StealthedCount)
	{
		return SelectedCount > 0 && StealthedCount < SelectedCount;
	}

	bool ShouldBreak(bool bActive, bool bFired, bool bSprinting, const FStealthModeConfig& Config)
	{
		return bActive && (bSprinting || (bFired && Config.bBreakOnFire));
	}

	float DetectionRangeScale(bool bActive, const FStealthModeConfig& Config)
	{
		return bActive && Config.bEnabled ? FMath::Clamp(Config.DetectionRangeMultiplier, 0.f, 1.f) : 1.f;
	}

	float DetectionBuildupScale(bool bActive, const FStealthModeConfig& Config)
	{
		return bActive && Config.bEnabled ? FMath::Clamp(Config.DetectionBuildupMultiplier, 0.f, 1.f) : 1.f;
	}

	float FootstepNoiseScale(bool bActive, const FStealthModeConfig& Config)
	{
		return bActive && Config.bEnabled ? FMath::Clamp(Config.FootstepNoiseMultiplier, 0.f, 1.f) : 1.f;
	}

	FStealthModeConfig FromJson(const FJsonObject& Json, const FStealthModeConfig& Base)
	{
		FStealthModeConfig Config = Base;
		Json.TryGetBoolField(TEXT("enabled"), Config.bEnabled);
		Json.TryGetBoolField(TEXT("stealth_break_on_fire"), Config.bBreakOnFire);
		const auto ReadScale = [&Json](const TCHAR* Key, float& Value)
		{
			double Read = 0.0;
			if (Json.TryGetNumberField(Key, Read) && FMath::IsFinite(Read))
			{
				Value = FMath::Clamp(static_cast<float>(Read), 0.f, 1.f);
			}
		};
		ReadScale(TEXT("detection_range_multiplier"), Config.DetectionRangeMultiplier);
		ReadScale(TEXT("detection_buildup_multiplier"), Config.DetectionBuildupMultiplier);
		ReadScale(TEXT("footstep_noise_multiplier"), Config.FootstepNoiseMultiplier);
		ReadScale(TEXT("visual_opacity"), Config.VisualOpacity);
		ReadScale(TEXT("visual_gray"), Config.VisualGray);
		return Config;
	}

	FString GetDefaultPath()
	{
		return FPaths::ProjectContentDir() / TEXT("Data/AI/stealth_mode.json");
	}

	const FStealthModeConfig& GetConfig()
	{
		static const FStealthModeConfig Config = []
		{
			FString Text;
			TSharedPtr<FJsonObject> Json;
			return FFileHelper::LoadFileToString(Text, *GetDefaultPath())
				&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) && Json.IsValid()
				? FromJson(*Json) : FStealthModeConfig();
		}();
		return Config;
	}
}
