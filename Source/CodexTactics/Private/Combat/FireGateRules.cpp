#include "Combat/FireGateRules.h"

#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace FireGateRules
{
	struct FFireGateTuningKey
	{
		const TCHAR* Key;
		float FFireGateConfig::* Member;
	};

	/** fire_gate.json "params" key -> FFireGateConfig member (the struct comments name the same keys). */
	static const FFireGateTuningKey FireGateTuningKeys[] = {
		{ TEXT("enabled"), &FFireGateConfig::Enabled },
		{ TEXT("raise_seconds"), &FFireGateConfig::RaiseSeconds },
		{ TEXT("cover_raise_seconds"), &FFireGateConfig::CoverRaiseSeconds },
		{ TEXT("settle_seconds"), &FFireGateConfig::SettleSeconds },
		{ TEXT("eval_gap_seconds"), &FFireGateConfig::EvalGapSeconds },
		{ TEXT("anim_stale_seconds"), &FFireGateConfig::AnimStaleSeconds },
		{ TEXT("cover_reach_seconds"), &FFireGateConfig::CoverReachSeconds },
		{ TEXT("object_shot_timeout_seconds"), &FFireGateConfig::ObjectShotTimeoutSeconds },
		{ TEXT("stealth_shot_hold_seconds"), &FFireGateConfig::StealthShotHoldSeconds },
		{ TEXT("crouch_corner_aim_outward_deg"), &FFireGateConfig::CrouchCornerAimOutwardDeg },
	};

	static FFireGateConfig& FireGateConfigInForce()
	{
		static FFireGateConfig Config;
		return Config;
	}

	static bool bFireGateConfigLoaded = false;

	FString GetDefaultPath()
	{
		return FPaths::ProjectContentDir() / TEXT("Data/Combat/fire_gate.json");
	}

	TArray<FString> KnownKeys()
	{
		TArray<FString> Keys;
		for (const FFireGateTuningKey& Entry : FireGateTuningKeys)
		{
			Keys.Add(Entry.Key);
		}
		return Keys;
	}

	bool GetValue(const FFireGateConfig& Config, const FString& Key, float& OutValue)
	{
		for (const FFireGateTuningKey& Entry : FireGateTuningKeys)
		{
			if (Key.Equals(Entry.Key, ESearchCase::CaseSensitive))
			{
				OutValue = Config.*(Entry.Member);
				return true;
			}
		}
		return false;
	}

	bool ApplyJson(const FString& JsonText, FFireGateConfig& InOut, TArray<FString>* OutUnknown)
	{
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root) || !Root.IsValid())
		{
			return false;
		}
		const TSharedPtr<FJsonObject>* Params = nullptr;
		if (!Root->TryGetObjectField(TEXT("params"), Params) || !Params || !Params->IsValid())
		{
			return false;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Params)->Values)
		{
			double Number = 0.0;
			const FFireGateTuningKey* Entry = nullptr;
			for (const FFireGateTuningKey& Each : FireGateTuningKeys)
			{
				Entry = Pair.Key.Equals(Each.Key, ESearchCase::CaseSensitive) ? &Each : Entry;
			}
			if (!Entry || !Pair.Value.IsValid() || !Pair.Value->TryGetNumber(Number))
			{
				if (OutUnknown)
				{
					OutUnknown->Add(Pair.Key);
				}
				continue;
			}
			InOut.*(Entry->Member) = static_cast<float>(Number);
		}
		return true;
	}

	const FFireGateConfig& GetConfig()
	{
		if (!bFireGateConfigLoaded)
		{
			ReloadConfig();
		}
		return FireGateConfigInForce();
	}

	void SetConfig(const FFireGateConfig& Config)
	{
		bFireGateConfigLoaded = true;
		FireGateConfigInForce() = Config;
	}

	bool ReloadConfig()
	{
		bFireGateConfigLoaded = true;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *GetDefaultPath()))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Fire gate tuning: %s missing - the code defaults are used"), *GetDefaultPath());
			return false;
		}
		FFireGateConfig Loaded;
		TArray<FString> Unknown;
		if (!ApplyJson(Text, Loaded, &Unknown))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Fire gate tuning: %s unreadable (no \"params\" object) - values kept"), *GetDefaultPath());
			return false;
		}
		for (const FString& Key : Unknown)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Fire gate tuning: unknown / non-numeric key \"%s\" skipped"), *Key);
		}
		FireGateConfigInForce() = Loaded;
		return true;
	}

	bool IsEnabled(const FFireGateConfig& Config)
	{
		return Config.Enabled > 0.5f;
	}

	EFireGateBlock Evaluate(const FFireGateInput& Input, const FFireGateConfig& Config)
	{
		const bool bOnTarget = Input.bBodyReady && Input.ResidualDeg <= FMath::Max(Input.ConeDeg, 0.f);
		if (!bOnTarget)
		{
			// From cover: the stance is up and has had its time to bring the barrel round - it does not reach the point.
			const bool bStanceSettled = Input.bInCover && Input.bBodyReady && Input.SecondsRaised >= FMath::Max(Config.CoverReachSeconds, 0.f);
			return bStanceSettled ? EFireGateBlock::CoverPoseOutOfReach : EFireGateBlock::NotOnTarget;
		}
		if (Input.bAnimLive)
		{
			if (Input.bWoundedRaisePending)
			{
				return EFireGateBlock::WoundedRaise;
			}
			if (Input.bStanceClip)
			{
				return EFireGateBlock::StanceClip;
			}
			const float Raise = FMath::Max(Input.bInCover ? Config.CoverRaiseSeconds : Config.RaiseSeconds, 0.f);
			if (Input.SecondsRaised < Raise)
			{
				return EFireGateBlock::Raising;
			}
		}
		if (Input.SecondsOnTarget < FMath::Max(Config.SettleSeconds, 0.f))
		{
			return EFireGateBlock::Settling;
		}
		return EFireGateBlock::None;
	}

	bool IsBodyInArc(float BodyErrorDeg, float ArcDeg)
	{
		return ArcDeg >= 180.f || FMath::Abs(BodyErrorDeg) <= FMath::Max(ArcDeg, 0.f);
	}

	double StepSince(double Since, bool bCondition, double Now)
	{
		if (!bCondition)
		{
			return -1.0;
		}
		return Since < 0.0 ? Now : Since;
	}

	float SecondsSince(double Since, double Now)
	{
		return Since < 0.0 ? -1.f : static_cast<float>(FMath::Max(0.0, Now - Since));
	}

	const TCHAR* BlockName(EFireGateBlock Block)
	{
		switch (Block)
		{
		case EFireGateBlock::None: return TEXT("fire gate: open");
		case EFireGateBlock::NotOnTarget: return TEXT("fire gate: turning (barrel / body not on the target)");
		case EFireGateBlock::CoverPoseOutOfReach: return TEXT("fire gate: the cover pose does not reach the target");
		case EFireGateBlock::WoundedRaise: return TEXT("fire gate: wounded, raising the weapon");
		case EFireGateBlock::StanceClip: return TEXT("fire gate: stance clip playing");
		case EFireGateBlock::Raising: return TEXT("fire gate: raising the weapon");
		case EFireGateBlock::Settling: return TEXT("fire gate: settling on the target");
		default: return TEXT("fire gate: ?");
		}
	}
}
