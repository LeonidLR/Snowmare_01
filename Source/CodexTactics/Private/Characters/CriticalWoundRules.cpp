#include "Characters/CriticalWoundRules.h"

#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace CriticalWoundRules
{
	namespace
	{
		FCriticalWoundConfig& ConfigInForce()
		{
			static FCriticalWoundConfig Config;
			return Config;
		}
		bool bConfigLoaded = false;
	}

	float SanitizeShare(double Value)
	{
		if (!FMath::IsFinite(Value) || Value <= 0.0)
		{
			return 0.f;
		}
		if (Value <= 1.0)
		{
			return static_cast<float>(Value);
		}
		return Value <= 100.0 ? static_cast<float>(Value / 100.0) : 0.f;
	}

	bool IsCritical(const FCriticalWoundConfig& Config, float Health, float MaxHealth)
	{
		return Config.CriticalFrom > 0.f && MaxHealth > 0.f && Health > 0.f && Health / MaxHealth < Config.CriticalFrom;
	}

	bool AppliesTo(const FCriticalWoundConfig& Config, bool bPlayerSide)
	{
		return bPlayerSide || Config.bApplyToHostiles;
	}

	bool AllowsStance(bool bCritical, EOperativeStance Wanted)
	{
		return !bCritical || Wanted == EOperativeStance::Prone;
	}

	bool AllowsSprint(bool bCritical)
	{
		return !bCritical;
	}

	ECriticalWoundAction StepWound(bool bDown, bool bCritical, bool bProne, bool bBusy)
	{
		if (!bCritical)
		{
			return bDown ? ECriticalWoundAction::MayStand : ECriticalWoundAction::None;
		}
		if (bProne)
		{
			return bDown ? ECriticalWoundAction::None : ECriticalWoundAction::GoDown; // already lying: the lock + the line, no stance change
		}
		return bBusy ? ECriticalWoundAction::WaitToGoDown : ECriticalWoundAction::GoDown;
	}

	bool ParseConfig(const FString& JsonText, FCriticalWoundConfig& Out)
	{
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root) || !Root.IsValid())
		{
			return false;
		}
		FCriticalWoundConfig Parsed = Out;
		double Number = 0.0;
		if (Root->TryGetNumberField(TEXT("critical_from"), Number))
		{
			Parsed.CriticalFrom = SanitizeShare(Number);
		}
		bool bFlag = false;
		if (Root->TryGetBoolField(TEXT("apply_to_hostiles"), bFlag))
		{
			Parsed.bApplyToHostiles = bFlag;
		}
		Out = Parsed;
		return true;
	}

	FString GetWoundConfigPath()
	{
		return FPaths::ProjectContentDir() / TEXT("Data/Movement/critical_wound.json");
	}

	const FCriticalWoundConfig& GetWoundConfig()
	{
		if (!bConfigLoaded)
		{
			bConfigLoaded = true;
			FString Text;
			FCriticalWoundConfig Loaded;
			if (!FFileHelper::LoadFileToString(Text, *GetWoundConfigPath()))
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("Critical wound: %s missing - the code defaults are used (critical_from %.2f)"), *GetWoundConfigPath(), Loaded.CriticalFrom);
			}
			else if (!ParseConfig(Text, Loaded))
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("Critical wound: %s unreadable - the code defaults are used"), *GetWoundConfigPath());
			}
			else
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Critical wound: critical_from %.2f, hostiles %s (%s)"), Loaded.CriticalFrom,
					Loaded.bApplyToHostiles ? TEXT("too") : TEXT("no"), *GetWoundConfigPath());
			}
			ConfigInForce() = Loaded;
		}
		return ConfigInForce();
	}

	void SetWoundConfig(const FCriticalWoundConfig& Config)
	{
		bConfigLoaded = true;
		ConfigInForce() = Config;
	}
}
