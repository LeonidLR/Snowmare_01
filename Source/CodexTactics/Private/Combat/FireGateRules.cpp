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
		/** JSON object holding the key ("params" or a G007 section). */
		const TCHAR* Section;
		const TCHAR* Key;
		float& (*Field)(FFireGateConfig&);
	};

#define FIRE_GATE_KEY(SectionName, KeyName, Path) { TEXT(SectionName), TEXT(KeyName), [](FFireGateConfig& C) -> float& { return C.Path; } }
	static const TCHAR* const ParamsSection = TEXT("params");

	/** fire_gate.json key -> FFireGateConfig member (the struct comments name the same keys). */
	static const FFireGateTuningKey FireGateTuningKeys[] = {
		FIRE_GATE_KEY("params", "enabled", Enabled),
		FIRE_GATE_KEY("params", "raise_seconds", RaiseSeconds),
		FIRE_GATE_KEY("params", "cover_raise_seconds", CoverRaiseSeconds),
		FIRE_GATE_KEY("params", "settle_seconds", SettleSeconds),
		FIRE_GATE_KEY("params", "eval_gap_seconds", EvalGapSeconds),
		FIRE_GATE_KEY("params", "anim_stale_seconds", AnimStaleSeconds),
		FIRE_GATE_KEY("params", "cover_reach_seconds", CoverReachSeconds),
		FIRE_GATE_KEY("params", "object_shot_timeout_seconds", ObjectShotTimeoutSeconds),
		FIRE_GATE_KEY("params", "stealth_shot_hold_seconds", StealthShotHoldSeconds),
		FIRE_GATE_KEY("params", "crouch_corner_aim_outward_deg", CrouchCornerAimOutwardDeg),
		FIRE_GATE_KEY("dead_zone", "chest_offset_cm", DeadZone.ChestOffsetCm),
		FIRE_GATE_KEY("dead_zone", "min_delta_z_cm", DeadZone.MinDeltaZCm),
		FIRE_GATE_KEY("dead_zone", "max_horizontal_cm", DeadZone.MaxHorizontalCm),
		FIRE_GATE_KEY("dead_zone", "min_angle_deg", DeadZone.MinAngleDeg),
		FIRE_GATE_KEY("dead_zone", "edge_drop_cm", DeadZone.EdgeDropCm),
		FIRE_GATE_KEY("ground_probe", "spacing_cm", GroundProbe.SpacingCm),
		FIRE_GATE_KEY("ground_probe", "lateral_cm", GroundProbe.LateralCm),
		FIRE_GATE_KEY("ground_probe", "up_cm", GroundProbe.UpCm),
		FIRE_GATE_KEY("ground_probe", "down_cm", GroundProbe.DownCm),
		FIRE_GATE_KEY("ground_probe", "max_probes", GroundProbe.MaxProbes),
		FIRE_GATE_KEY("melee_reach", "body_offset_cm", MeleeReach.BodyOffsetCm),
		FIRE_GATE_KEY("melee_reach", "height_cap_cm", MeleeReach.HeightCapCm),
		FIRE_GATE_KEY("melee_reach", "step_max_cm", MeleeReach.StepMaxCm),
		FIRE_GATE_KEY("melee_reach", "slope_full_deg", MeleeReach.SlopeFullDeg),
	};
#undef FIRE_GATE_KEY

	/** The sections besides "params" (each optional in the file). */
	static const TCHAR* const ExtraSections[] = { TEXT("dead_zone"), TEXT("ground_probe"), TEXT("melee_reach") };

	static FString FullKey(const FFireGateTuningKey& Entry)
	{
		return FCString::Strcmp(Entry.Section, ParamsSection) == 0 ? FString(Entry.Key) : FString::Printf(TEXT("%s.%s"), Entry.Section, Entry.Key);
	}

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
			Keys.Add(FullKey(Entry));
		}
		return Keys;
	}

	bool GetValue(const FFireGateConfig& Config, const FString& Key, float& OutValue)
	{
		for (const FFireGateTuningKey& Entry : FireGateTuningKeys)
		{
			if (Key.Equals(FullKey(Entry), ESearchCase::CaseSensitive))
			{
				OutValue = Entry.Field(const_cast<FFireGateConfig&>(Config));
				return true;
			}
		}
		return false;
	}

	/** One JSON object's numbers over InOut (Section's keys only). */
	static void ApplySection(const FJsonObject& Object, const TCHAR* Section, FFireGateConfig& InOut, TArray<FString>* OutUnknown)
	{
		const bool bParams = FCString::Strcmp(Section, ParamsSection) == 0;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object.Values)
		{
			if (!bParams && Pair.Key == TEXT("comment"))
			{
				continue;
			}
			double Number = 0.0;
			const FFireGateTuningKey* Entry = nullptr;
			for (const FFireGateTuningKey& Each : FireGateTuningKeys)
			{
				Entry = FCString::Strcmp(Each.Section, Section) == 0 && Pair.Key.Equals(Each.Key, ESearchCase::CaseSensitive) ? &Each : Entry;
			}
			if (!Entry || !Pair.Value.IsValid() || !Pair.Value->TryGetNumber(Number))
			{
				if (OutUnknown)
				{
					OutUnknown->Add(bParams ? Pair.Key : FString::Printf(TEXT("%s.%s"), Section, *Pair.Key));
				}
				continue;
			}
			Entry->Field(InOut) = static_cast<float>(Number);
		}
	}

	bool ApplyJson(const FString& JsonText, FFireGateConfig& InOut, TArray<FString>* OutUnknown)
	{
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonText), Root) || !Root.IsValid())
		{
			return false;
		}
		const TSharedPtr<FJsonObject>* Params = nullptr;
		if (!Root->TryGetObjectField(ParamsSection, Params) || !Params || !Params->IsValid())
		{
			return false;
		}
		ApplySection(**Params, ParamsSection, InOut, OutUnknown);
		for (const TCHAR* Section : ExtraSections)
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (Root->TryGetObjectField(Section, Object) && Object && Object->IsValid())
			{
				ApplySection(**Object, Section, InOut, OutUnknown);
			}
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
