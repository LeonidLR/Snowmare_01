#include "Combat/UnderFireRules.h"

#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UnderFireRules
{
	struct FUnderFireTuningKey
	{
		const TCHAR* Key;
		float FUnderFireConfig::* Member;
	};

	/** under_fire.json "params" key -> FUnderFireConfig member (the struct comments name the same keys). */
	static const FUnderFireTuningKey UnderFireTuningKeys[] = {
		{ TEXT("memory_seconds"), &FUnderFireConfig::MemorySeconds },
		{ TEXT("share_radius_cm"), &FUnderFireConfig::ShareRadiusCm },
		{ TEXT("manoeuvre.enabled"), &FUnderFireConfig::ManoeuvreEnabled },
		{ TEXT("manoeuvre.decision_seconds"), &FUnderFireConfig::DecisionSeconds },
		{ TEXT("manoeuvre.reaction_seconds"), &FUnderFireConfig::ReactionSeconds },
		{ TEXT("manoeuvre.hold_seconds"), &FUnderFireConfig::HoldSeconds },
		{ TEXT("cover.radius_cm"), &FUnderFireConfig::CoverSeekRadiusCm },
		{ TEXT("cover.min_quality"), &FUnderFireConfig::CoverMinQuality },
		{ TEXT("cover.retry_seconds"), &FUnderFireConfig::CoverRetrySeconds },
		{ TEXT("sidestep.max_cm"), &FUnderFireConfig::SidestepMaxCm },
		{ TEXT("sidestep.step_cm"), &FUnderFireConfig::SidestepStepCm },
		{ TEXT("sidestep.mate_block_radius_cm"), &FUnderFireConfig::MateBlockRadiusCm },
		{ TEXT("sidestep.cooldown_seconds"), &FUnderFireConfig::SidestepCooldownSeconds },
		{ TEXT("prone.when_no_cover"), &FUnderFireConfig::ProneWhenNoCover },
	};

	static FUnderFireConfig& UnderFireConfigInForce()
	{
		static FUnderFireConfig Config;
		return Config;
	}

	static bool bUnderFireConfigLoaded = false;

	FString GetDefaultPath()
	{
		return FPaths::ProjectContentDir() / TEXT("Data/Combat/under_fire.json");
	}

	TArray<FString> KnownKeys()
	{
		TArray<FString> Keys;
		for (const FUnderFireTuningKey& Entry : UnderFireTuningKeys)
		{
			Keys.Add(Entry.Key);
		}
		return Keys;
	}

	bool GetValue(const FUnderFireConfig& Config, const FString& Key, float& OutValue)
	{
		for (const FUnderFireTuningKey& Entry : UnderFireTuningKeys)
		{
			if (Key.Equals(Entry.Key, ESearchCase::CaseSensitive))
			{
				OutValue = Config.*(Entry.Member);
				return true;
			}
		}
		return false;
	}

	bool ApplyJson(const FString& JsonText, FUnderFireConfig& InOut, TArray<FString>* OutUnknown)
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
			const FUnderFireTuningKey* Entry = nullptr;
			for (const FUnderFireTuningKey& Each : UnderFireTuningKeys)
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

	const FUnderFireConfig& GetConfig()
	{
		if (!bUnderFireConfigLoaded)
		{
			ReloadConfig();
		}
		return UnderFireConfigInForce();
	}

	void SetConfig(const FUnderFireConfig& Config)
	{
		bUnderFireConfigLoaded = true;
		UnderFireConfigInForce() = Config;
	}

	bool ReloadConfig()
	{
		bUnderFireConfigLoaded = true;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *GetDefaultPath()))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Under-fire tuning: %s missing - the code defaults are used"), *GetDefaultPath());
			return false;
		}
		FUnderFireConfig Loaded;
		TArray<FString> Unknown;
		if (!ApplyJson(Text, Loaded, &Unknown))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Under-fire tuning: %s unreadable (no \"params\" object) - values kept"), *GetDefaultPath());
			return false;
		}
		for (const FString& Key : Unknown)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Under-fire tuning: unknown / non-numeric key \"%s\" skipped"), *Key);
		}
		UnderFireConfigInForce() = Loaded;
		return true;
	}

	bool IsThreatActive(double Now, double LastShotAtTime, float MemorySeconds)
	{
		return Now - LastShotAtTime <= MemorySeconds && Now >= LastShotAtTime;
	}

	bool SharesThreat(bool bIsTarget, float DistanceToTargetCm, float ShareRadiusCm)
	{
		return bIsTarget || DistanceToTargetCm <= ShareRadiusCm;
	}

	bool ShouldFaceThreat(const FFaceFacts& Facts)
	{
		return Facts.bRealTimeFight && Facts.bAlive && Facts.bThreatActive && !Facts.bHasCombatTarget && !Facts.bInCover && !Facts.bSprinting
			&& !Facts.bCarrying && !Facts.bPanicking && !Facts.bFireOrder && !Facts.bExplicitFacing && !Facts.bGridOwnsFacing;
	}

	bool ShouldReturnFire(bool bThreatActive, bool bHasDirectTarget, bool bMayAutoFire, bool bLineToFlash, bool bInReach)
	{
		return bThreatActive && !bHasDirectTarget && bMayAutoFire && bLineToFlash && bInReach;
	}

	bool MayManoeuvre(const FUnderFireConfig& Config, const FManoeuvreFacts& Facts)
	{
		if (Config.ManoeuvreEnabled <= 0.f || !Facts.bRealTimeFight || !Facts.bAlive || !Facts.bThreatActive || Facts.bGridOwnsFacing)
		{
			return false;
		}
		// In cover already, or an order of his own (the player's explicit move / fire / facing order, the commander's, a use task): left alone.
		if (Facts.bInCover || Facts.bCoverPending || Facts.bOwnOrder || Facts.bFireOrFacingOrder || Facts.bCarrying || Facts.bPanicking || Facts.bAutonomyOwnsCover
			|| Facts.bSoloHold)
		{
			return false;
		}
		// The formation's slot logic first: nothing while it walks, a moment's grace after the first round while it halts.
		return !Facts.bFormationFollower || (!Facts.bFormationWalking && Facts.SecondsUnderFire >= Config.ReactionSeconds);
	}

	EUnderFireManoeuvre ChooseManoeuvre(const FUnderFireConfig& Config, const FManoeuvreOptions& Options)
	{
		if (Options.bCoverFound)
		{
			return EUnderFireManoeuvre::TakeCover;
		}
		if (Options.bWantsLine && Options.bSidestepFound && !Options.bSidestepCoolingDown)
		{
			return EUnderFireManoeuvre::Sidestep;
		}
		if (!Options.bProne && Config.ProneWhenNoCover > 0.f)
		{
			return EUnderFireManoeuvre::GoProne;
		}
		return EUnderFireManoeuvre::None;
	}

	TArray<float> SidestepOffsets(const FUnderFireConfig& Config)
	{
		TArray<float> Out;
		const float Step = FMath::Max(Config.SidestepStepCm, 10.f);
		for (float Offset = Step; Offset <= Config.SidestepMaxCm + 0.5f && Out.Num() < 64; Offset += Step)
		{
			Out.Add(Offset);
			Out.Add(-Offset);
		}
		return Out;
	}

	FVector SidestepPoint(const FVector& Feet, const FVector& Flash, float OffsetCm)
	{
		const FVector Ahead = (Flash - Feet).GetSafeNormal2D();
		const FVector Right = FVector::CrossProduct(FVector::UpVector, Ahead);
		return Feet + Right * OffsetCm;
	}

	bool IsMateInLine(const FVector& From, const FVector& To, const FVector& Mate, float RadiusCm)
	{
		const FVector2D A(From);
		const FVector2D B(To);
		const FVector2D P(Mate);
		const FVector2D AB = B - A;
		const float LengthSq = static_cast<float>(AB.SizeSquared());
		if (LengthSq < 1.f)
		{
			return false;
		}
		const float T = static_cast<float>(FVector2D::DotProduct(P - A, AB)) / LengthSq;
		// Between the ends: a mate right at his muzzle (half the block radius) or beyond the flash is not in the way.
		if (T * FMath::Sqrt(LengthSq) < RadiusCm * 0.5f || T > 1.f)
		{
			return false;
		}
		return FVector2D::Distance(P, A + AB * T) <= RadiusCm;
	}

	bool IsHoldingManoeuvreSpot(const FUnderFireConfig& Config, bool bManoeuvred, double Now, double LastShotAtTime, bool bLeaderOrderedSince)
	{
		return bManoeuvred && !bLeaderOrderedSince && IsThreatActive(Now, LastShotAtTime, Config.MemorySeconds + FMath::Max(Config.HoldSeconds, 0.f));
	}

	bool ShouldRiseAfterThreat(bool bWentProneUnderFire, bool bStillProne, bool bThreatActive)
	{
		return bWentProneUnderFire && bStillProne && !bThreatActive;
	}
}
