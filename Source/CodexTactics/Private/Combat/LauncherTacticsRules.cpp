#include "Combat/LauncherTacticsRules.h"

#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace LauncherTacticsRules
{
	struct FLauncherTacticsKey
	{
		const TCHAR* Key;
		float FLauncherTacticsConfig::* Member;
		/** File value x Scale = the member (the *_m keys: 100, metres -> cm). */
		float Scale;
	};

	/** launcher_tactics.json "params" key -> FLauncherTacticsConfig member (the struct comments name the same keys). */
	static const FLauncherTacticsKey LauncherTacticsKeys[] = {
		{ TEXT("enabled"), &FLauncherTacticsConfig::Enabled, 1.f },
		{ TEXT("threshold"), &FLauncherTacticsConfig::Threshold, 1.f },
		{ TEXT("ordered_threshold"), &FLauncherTacticsConfig::OrderedThreshold, 1.f },
		{ TEXT("cluster.radius_m"), &FLauncherTacticsConfig::ClusterRadiusCm, 100.f },
		{ TEXT("cluster.min"), &FLauncherTacticsConfig::ClusterMin, 1.f },
		{ TEXT("cluster.full"), &FLauncherTacticsConfig::ClusterFull, 1.f },
		{ TEXT("weight.cluster"), &FLauncherTacticsConfig::WeightCluster, 1.f },
		{ TEXT("weight.cover"), &FLauncherTacticsConfig::WeightCover, 1.f },
		{ TEXT("weight.heavy"), &FLauncherTacticsConfig::WeightHeavy, 1.f },
		{ TEXT("weight.suppress"), &FLauncherTacticsConfig::WeightSuppress, 1.f },
		{ TEXT("weight.single_exposed"), &FLauncherTacticsConfig::WeightSingleExposed, 1.f },
		{ TEXT("suppress.group_radius_m"), &FLauncherTacticsConfig::SuppressGroupRadiusCm, 100.f },
		{ TEXT("suppress.group_min"), &FLauncherTacticsConfig::SuppressGroupMin, 1.f },
		{ TEXT("friendly_clear_m"), &FLauncherTacticsConfig::FriendlyClearCm, 100.f },
		{ TEXT("keep_rockets_for_heavy"), &FLauncherTacticsConfig::KeepRocketsForHeavy, 1.f },
		{ TEXT("draw_s"), &FLauncherTacticsConfig::DrawSeconds, 1.f },
		{ TEXT("return_after_s"), &FLauncherTacticsConfig::ReturnAfterSeconds, 1.f },
		{ TEXT("redraw_cooldown_s"), &FLauncherTacticsConfig::RedrawCooldownSeconds, 1.f },
		{ TEXT("decision_interval_s"), &FLauncherTacticsConfig::DecisionIntervalSeconds, 1.f },
		{ TEXT("player_choice_hold_s"), &FLauncherTacticsConfig::PlayerChoiceHoldSeconds, 1.f },
		{ TEXT("strike_timeout_s"), &FLauncherTacticsConfig::StrikeTimeoutSeconds, 1.f },
	};

	static FLauncherTacticsConfig& LauncherTacticsInForce()
	{
		static FLauncherTacticsConfig Config;
		return Config;
	}

	static bool bLauncherTacticsLoaded = false;

	ELauncherVeto Veto(const FLauncherTacticsConfig& Config, const FLauncherTargetView& View)
	{
		if (View.RocketsLeft <= 0)
		{
			return ELauncherVeto::NoRockets;
		}
		if (View.DistanceCm < View.MinRangeCm)
		{
			return ELauncherVeto::TooClose;
		}
		if (View.MaxRangeCm > 0.f && View.DistanceCm > View.MaxRangeCm)
		{
			return ELauncherVeto::OutOfRange;
		}
		if (View.FriendliesNear > 0)
		{
			return ELauncherVeto::FriendlyNear;
		}
		if (View.bLineBlockedShort)
		{
			return ELauncherVeto::NoLine;
		}
		if (!View.bHeavy && View.RocketsLeft <= FMath::RoundToInt(Config.KeepRocketsForHeavy))
		{
			return ELauncherVeto::SavedForHeavy;
		}
		return ELauncherVeto::None;
	}

	float ClusterFactor(const FLauncherTacticsConfig& Config, int32 Count)
	{
		const float Min = FMath::Max(2.f, Config.ClusterMin);
		if (static_cast<float>(Count) < Min)
		{
			return 0.f;
		}
		const float Full = FMath::Max(Min, Config.ClusterFull);
		return Full <= 1.f ? 1.f : FMath::Clamp((static_cast<float>(Count) - 1.f) / (Full - 1.f), 0.f, 1.f);
	}

	FLauncherChoice Score(const FLauncherTacticsConfig& Config, const FLauncherTargetView& View)
	{
		FLauncherChoice Out;
		auto Offer = [&Out](ELauncherUse Reason, float Value)
		{
			if (Value > Out.Score)
			{
				Out.Score = Value;
				Out.Reason = Reason;
			}
		};
		Offer(ELauncherUse::Heavy, View.bHeavy ? FMath::Max(0.f, Config.WeightHeavy) : 0.f);
		Offer(ELauncherUse::Cluster, FMath::Max(0.f, Config.WeightCluster) * ClusterFactor(Config, View.ClusterCount));
		Offer(ELauncherUse::Cover, View.bInCover ? FMath::Max(0.f, Config.WeightCover) : 0.f);
		Offer(ELauncherUse::Suppress, View.bGroupThreat && static_cast<float>(View.SuppressGroup) >= FMath::Max(2.f, Config.SuppressGroupMin)
			? FMath::Max(0.f, Config.WeightSuppress) : 0.f);
		Offer(ELauncherUse::Single, FMath::Max(0.f, Config.WeightSingleExposed));
		return Out;
	}

	FLauncherChoice Evaluate(const FLauncherTacticsConfig& Config, const FLauncherTargetView& View, float Threshold)
	{
		FLauncherChoice Out = Score(Config, View);
		Out.Veto = Veto(Config, View);
		Out.bUse = Out.Veto == ELauncherVeto::None && Out.Reason != ELauncherUse::None && Out.Score > 0.f && Out.Score >= Threshold;
		return Out;
	}

	int32 PickBest(const TArray<FLauncherChoice>& Choices)
	{
		int32 Best = INDEX_NONE;
		for (int32 Index = 0; Index < Choices.Num(); ++Index)
		{
			if (Choices[Index].bUse && (Best == INDEX_NONE || Choices[Index].Score > Choices[Best].Score))
			{
				Best = Index;
			}
		}
		return Best;
	}

	int32 CountWithin(const TArray<FVector>& Points, const FVector& Centre, float RadiusCm)
	{
		const double RadiusSq = FMath::Square(static_cast<double>(FMath::Max(0.f, RadiusCm)));
		int32 Count = 0;
		for (const FVector& Point : Points)
		{
			Count += FVector::DistSquared(Point, Centre) <= RadiusSq ? 1 : 0;
		}
		return Count;
	}

	bool IsPlayerPickHeld(const FLauncherTacticsConfig& Config, float SecondsSincePick)
	{
		return SecondsSincePick >= 0.f && SecondsSincePick < Config.PlayerChoiceHoldSeconds;
	}

	const TCHAR* UseName(ELauncherUse Use)
	{
		switch (Use)
		{
		case ELauncherUse::Cluster: return TEXT("cluster");
		case ELauncherUse::Cover: return TEXT("in cover");
		case ELauncherUse::Heavy: return TEXT("heavy target");
		case ELauncherUse::Suppress: return TEXT("suppress the group");
		case ELauncherUse::Single: return TEXT("single exposed");
		default: return TEXT("none");
		}
	}

	const TCHAR* VetoName(ELauncherVeto Veto)
	{
		switch (Veto)
		{
		case ELauncherVeto::NoRockets: return TEXT("no rockets");
		case ELauncherVeto::TooClose: return TEXT("inside the minimum range");
		case ELauncherVeto::OutOfRange: return TEXT("out of range");
		case ELauncherVeto::FriendlyNear: return TEXT("a mate near the impact");
		case ELauncherVeto::NoLine: return TEXT("the line is blocked short of the target");
		case ELauncherVeto::SavedForHeavy: return TEXT("kept for a heavy target");
		default: return TEXT("none");
		}
	}

	FString GetDefaultPath()
	{
		return FPaths::ProjectContentDir() / TEXT("Data/Combat/launcher_tactics.json");
	}

	TArray<FString> KnownKeys()
	{
		TArray<FString> Keys;
		for (const FLauncherTacticsKey& Entry : LauncherTacticsKeys)
		{
			Keys.Add(Entry.Key);
		}
		return Keys;
	}

	bool GetValue(const FLauncherTacticsConfig& Config, const FString& Key, float& OutValue)
	{
		for (const FLauncherTacticsKey& Entry : LauncherTacticsKeys)
		{
			if (Key.Equals(Entry.Key, ESearchCase::CaseSensitive))
			{
				OutValue = Config.*(Entry.Member) / Entry.Scale;
				return true;
			}
		}
		return false;
	}

	bool ApplyJson(const FString& JsonText, FLauncherTacticsConfig& InOut, TArray<FString>* OutUnknown)
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
			const FLauncherTacticsKey* Entry = nullptr;
			for (const FLauncherTacticsKey& Each : LauncherTacticsKeys)
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
			InOut.*(Entry->Member) = static_cast<float>(Number) * Entry->Scale;
		}
		return true;
	}

	const FLauncherTacticsConfig& GetConfig()
	{
		if (!bLauncherTacticsLoaded)
		{
			ReloadConfig();
		}
		return LauncherTacticsInForce();
	}

	void SetConfig(const FLauncherTacticsConfig& Config)
	{
		bLauncherTacticsLoaded = true;
		LauncherTacticsInForce() = Config;
	}

	bool ReloadConfig()
	{
		bLauncherTacticsLoaded = true;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *GetDefaultPath()))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Launcher tactics: %s missing - the code defaults are used"), *GetDefaultPath());
			return false;
		}
		FLauncherTacticsConfig Loaded;
		TArray<FString> Unknown;
		if (!ApplyJson(Text, Loaded, &Unknown))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Launcher tactics: %s unreadable (no \"params\" object) - values kept"), *GetDefaultPath());
			return false;
		}
		for (const FString& Key : Unknown)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Launcher tactics: unknown / non-numeric key \"%s\" skipped"), *Key);
		}
		LauncherTacticsInForce() = Loaded;
		return true;
	}
}
