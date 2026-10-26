// Pure rules of suppressive fire (game designer decisions 2026-10-26; UE-only, no Godot reference). Spec: docs/design/SUPPRESSION.md.

#include "Combat/SuppressionRules.h"

#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace SuppressionRules
{
	struct FSuppressionTuningKey
	{
		const TCHAR* Key;
		float FSuppressionConfig::* Member;
	};

	/** suppression.json "params" key -> FSuppressionConfig member (the struct comments name the same keys). */
	static const FSuppressionTuningKey SuppressionTuningKeys[] = {
		{ TEXT("tier.suppressed_enter"), &FSuppressionConfig::SuppressedEnter },
		{ TEXT("tier.suppressed_exit"), &FSuppressionConfig::SuppressedExit },
		{ TEXT("tier.pinned_enter"), &FSuppressionConfig::PinnedEnter },
		{ TEXT("tier.pinned_exit"), &FSuppressionConfig::PinnedExit },
		{ TEXT("decay.per_second"), &FSuppressionConfig::DecayPerSecond },
		{ TEXT("decay.quiet_seconds"), &FSuppressionConfig::QuietSeconds },
		{ TEXT("decay.quiet_per_second"), &FSuppressionConfig::QuietDecayPerSecond },
		{ TEXT("round.near_miss_radius_cm"), &FSuppressionConfig::NearMissRadiusCm },
		{ TEXT("round.near_miss_edge_factor"), &FSuppressionConfig::NearMissEdgeFactor },
		{ TEXT("round.cover_strike_radius_cm"), &FSuppressionConfig::CoverStrikeRadiusCm },
		{ TEXT("round.cover_strike_factor"), &FSuppressionConfig::CoverStrikeFactor },
		{ TEXT("round.hit_factor"), &FSuppressionConfig::HitFactor },
		{ TEXT("round.explosive_radius_cm"), &FSuppressionConfig::ExplosiveRadiusCm },
		{ TEXT("weight.semi_auto"), &FSuppressionConfig::WeightSemiAuto },
		{ TEXT("weight.full_auto"), &FSuppressionConfig::WeightFullAuto },
		{ TEXT("weight.machine_gun"), &FSuppressionConfig::WeightMachineGun },
		{ TEXT("weight.sniper"), &FSuppressionConfig::WeightSniper },
		{ TEXT("weight.acid"), &FSuppressionConfig::WeightAcid },
		{ TEXT("weight.cannon"), &FSuppressionConfig::WeightCannon },
		{ TEXT("weight.deliberate_multiplier"), &FSuppressionConfig::DeliberateMultiplier },
		{ TEXT("effect.suppressed_hit_multiplier"), &FSuppressionConfig::SuppressedHitMultiplier },
		{ TEXT("effect.suppressed_spread_multiplier"), &FSuppressionConfig::SuppressedSpreadMultiplier },
		{ TEXT("order.burst_rounds"), &FSuppressionConfig::OrderBurstRounds },
		{ TEXT("order.burst_pause_seconds"), &FSuppressionConfig::OrderBurstPauseSeconds },
		{ TEXT("order.hit_multiplier"), &FSuppressionConfig::OrderHitMultiplier },
		{ TEXT("order.spread_standing_deg"), &FSuppressionConfig::OrderSpreadStandingDeg },
		{ TEXT("order.spread_crouching_deg"), &FSuppressionConfig::OrderSpreadCrouchingDeg },
		{ TEXT("order.spread_prone_deg"), &FSuppressionConfig::OrderSpreadProneDeg },
		{ TEXT("order.stop_at_rounds"), &FSuppressionConfig::OrderStopAtRounds },
		{ TEXT("order.timeout_seconds"), &FSuppressionConfig::OrderTimeoutSeconds },
		{ TEXT("order.aim_height_cm"), &FSuppressionConfig::OrderAimHeightCm },
		{ TEXT("order.spot_scatter_cm"), &FSuppressionConfig::OrderSpotScatterCm },
		{ TEXT("order.click_radius_cm"), &FSuppressionConfig::OrderClickRadiusCm },
		{ TEXT("order.marker_radius_cm"), &FSuppressionConfig::OrderMarkerRadiusCm },
		{ TEXT("explosion.pressure"), &FSuppressionConfig::ExplosionPressure },
		{ TEXT("explosion.edge_factor"), &FSuppressionConfig::ExplosionEdgeFactor },
		{ TEXT("explosion.radius_factor"), &FSuppressionConfig::ExplosionRadiusFactor },
		{ TEXT("explosion.min_radius_cm"), &FSuppressionConfig::ExplosionMinRadiusCm },
		{ TEXT("explosion.max_burst"), &FSuppressionConfig::ExplosionMaxBurst },
		{ TEXT("explosion.decay_per_second"), &FSuppressionConfig::ExplosionDecayPerSecond },
		{ TEXT("zone.cell_cm"), &FSuppressionConfig::ZoneCellCm },
		{ TEXT("zone.density_threshold"), &FSuppressionConfig::ZoneDensityThreshold },
		{ TEXT("zone.density_decay_per_second"), &FSuppressionConfig::ZoneDensityDecayPerSecond },
		{ TEXT("zone.radius_cm"), &FSuppressionConfig::ZoneRadiusCm },
		{ TEXT("zone.duration_seconds"), &FSuppressionConfig::ZoneDurationSeconds },
		{ TEXT("zone.min_squad_distance_cm"), &FSuppressionConfig::ZoneMinSquadDistanceCm },
	};

	/** The values in force (GetConfig / SetConfig); loaded from the file on the first GetConfig. */
	static FSuppressionConfig& SuppressionConfigInForce()
	{
		static FSuppressionConfig Config;
		return Config;
	}

	static bool bSuppressionConfigLoaded = false;

	float GetWeaponWeight(const FSuppressionConfig& Config, ESuppressionWeapon Weapon)
	{
		switch (Weapon)
		{
		case ESuppressionWeapon::FullAuto: return Config.WeightFullAuto;
		case ESuppressionWeapon::MachineGun: return Config.WeightMachineGun;
		case ESuppressionWeapon::Sniper: return Config.WeightSniper;
		case ESuppressionWeapon::Acid: return Config.WeightAcid;
		case ESuppressionWeapon::Cannon: return Config.WeightCannon;
		default: return Config.WeightSemiAuto;
		}
	}

	float GetNearMissRadius(const FSuppressionConfig& Config, ESuppressionWeapon Weapon)
	{
		return Weapon == ESuppressionWeapon::Cannon ? FMath::Max(Config.NearMissRadiusCm, Config.ExplosiveRadiusCm) : Config.NearMissRadiusCm;
	}

	ESuppressionImpact ClassifyImpact(const FSuppressionConfig& Config, ESuppressionWeapon Weapon, const FVector& Start, const FVector& End,
		bool bStruckCover, bool bHitThisUnit, const FVector& UnitLocation, float& OutDistanceCm)
	{
		OutDistanceCm = 0.f;
		if (bHitThisUnit)
		{
			return ESuppressionImpact::Hit;
		}
		const float PassCm = static_cast<float>(FMath::PointDistToSegment(UnitLocation, Start, End));
		const float NearRadius = GetNearMissRadius(Config, Weapon);
		const bool bNear = PassCm <= NearRadius;
		const float CoverCm = static_cast<float>(FVector::Dist(UnitLocation, End));
		const bool bCover = bStruckCover && CoverCm <= Config.CoverStrikeRadiusCm;
		if (!bNear && !bCover)
		{
			OutDistanceCm = PassCm;
			return ESuppressionImpact::None;
		}
		// Both: the stronger rating wins.
		const float NearPressure = bNear ? ImpactPressure(Config, Weapon, ESuppressionImpact::NearMiss, PassCm) : -1.f;
		const float CoverPressure = bCover ? ImpactPressure(Config, Weapon, ESuppressionImpact::CoverStrike, CoverCm) : -1.f;
		if (CoverPressure > NearPressure)
		{
			OutDistanceCm = CoverCm;
			return ESuppressionImpact::CoverStrike;
		}
		OutDistanceCm = PassCm;
		return ESuppressionImpact::NearMiss;
	}

	float ImpactPressure(const FSuppressionConfig& Config, ESuppressionWeapon Weapon, ESuppressionImpact Impact, float DistanceCm, float WeightScale)
	{
		const float Weight = GetWeaponWeight(Config, Weapon) * FMath::Max(0.f, WeightScale);
		switch (Impact)
		{
		case ESuppressionImpact::Hit:
			return Weight * Config.HitFactor;
		case ESuppressionImpact::CoverStrike:
			return DistanceCm <= Config.CoverStrikeRadiusCm ? Weight * Config.CoverStrikeFactor : 0.f;
		case ESuppressionImpact::NearMiss:
		{
			const float Radius = GetNearMissRadius(Config, Weapon);
			if (Radius <= 0.f || DistanceCm > Radius)
			{
				return 0.f;
			}
			const float Alpha = FMath::Clamp(DistanceCm / Radius, 0.f, 1.f);
			return Weight * FMath::Lerp(1.f, Config.NearMissEdgeFactor, Alpha);
		}
		default:
			return 0.f;
		}
	}

	ESuppressionTier ResolveTier(const FSuppressionConfig& Config, float Level, ESuppressionTier Previous)
	{
		if (Level >= Config.PinnedEnter || (Previous == ESuppressionTier::Pinned && Level >= Config.PinnedExit))
		{
			return ESuppressionTier::Pinned;
		}
		if (Level >= Config.SuppressedEnter || (Previous != ESuppressionTier::None && Level >= Config.SuppressedExit))
		{
			return ESuppressionTier::Suppressed;
		}
		return ESuppressionTier::None;
	}

	void AddPressure(const FSuppressionConfig& Config, FSuppressionState& State, float Amount)
	{
		if (Amount <= 0.f)
		{
			return;
		}
		State.Level = FMath::Clamp(State.Level + Amount, 0.f, 1.f);
		State.QuietTime = 0.f;
		State.Tier = ResolveTier(Config, EffectiveLevel(State), State.Tier);
	}

	void Step(const FSuppressionConfig& Config, FSuppressionState& State, float DeltaSeconds)
	{
		if (DeltaSeconds <= 0.f)
		{
			return;
		}
		State.QuietTime += DeltaSeconds;
		const float Rate = FMath::Max(0.f, Config.DecayPerSecond) + (State.QuietTime >= Config.QuietSeconds ? FMath::Max(0.f, Config.QuietDecayPerSecond) : 0.f);
		State.Level = FMath::Max(0.f, State.Level - Rate * DeltaSeconds);
		State.Burst = FMath::Max(0.f, State.Burst - FMath::Max(0.f, Config.ExplosionDecayPerSecond) * DeltaSeconds);
		State.Tier = ResolveTier(Config, EffectiveLevel(State), State.Tier);
	}

	float EffectiveLevel(const FSuppressionState& State)
	{
		return FMath::Clamp(State.Level + State.Burst, 0.f, 1.f);
	}

	float ExplosionReachCm(const FSuppressionConfig& Config, float BlastRadiusCm)
	{
		return FMath::Max(Config.ExplosionMinRadiusCm, FMath::Max(0.f, BlastRadiusCm) * Config.ExplosionRadiusFactor);
	}

	float ExplosionPressure(const FSuppressionConfig& Config, float BlastRadiusCm, float DistanceCm)
	{
		const float Reach = ExplosionReachCm(Config, BlastRadiusCm);
		if (Reach <= 0.f || DistanceCm > Reach)
		{
			return 0.f;
		}
		return FMath::Max(0.f, Config.ExplosionPressure) * FMath::Lerp(1.f, Config.ExplosionEdgeFactor, FMath::Clamp(DistanceCm / Reach, 0.f, 1.f));
	}

	void AddBurst(const FSuppressionConfig& Config, FSuppressionState& State, float Amount)
	{
		if (Amount <= 0.f)
		{
			return;
		}
		// Two blasts close together do not stack beyond the cap; the stronger one counts.
		State.Burst = FMath::Clamp(FMath::Max(State.Burst, Amount), 0.f, FMath::Max(0.f, Config.ExplosionMaxBurst));
		State.Tier = ResolveTier(Config, EffectiveLevel(State), State.Tier);
	}

	int32 FindFireCell(const FSuppressionConfig& Config, const TArray<FSuppressionFireCell>& Cells, const FVector& End)
	{
		int32 Best = INDEX_NONE;
		float BestDistSq = FMath::Square(FMath::Max(50.f, Config.ZoneCellCm));
		for (int32 Index = 0; Index < Cells.Num(); ++Index)
		{
			const float DistSq = static_cast<float>(FVector::DistSquared2D(Cells[Index].Centre, End));
			if (DistSq <= BestDistSq)
			{
				BestDistSq = DistSq;
				Best = Index;
			}
		}
		return Best;
	}

	void AddFireToCell(FSuppressionFireCell& Cell, const FVector& End)
	{
		const float Weight = Cell.Density + 1.f;
		Cell.Centre = Cell.Density <= 0.f ? End : Cell.Centre + (End - Cell.Centre) / Weight;
		Cell.Density += 1.f;
	}

	bool StepFireCell(const FSuppressionConfig& Config, FSuppressionFireCell& Cell, float DeltaSeconds, bool bSquadTooClose)
	{
		// Capped at twice the threshold: a long burst does not keep the zone up for long after the fire stops.
		Cell.Density = FMath::Clamp(Cell.Density - FMath::Max(0.f, Config.ZoneDensityDecayPerSecond) * FMath::Max(0.f, DeltaSeconds), 0.f,
			2.f * FMath::Max(1.f, Config.ZoneDensityThreshold));
		if (Cell.Density >= Config.ZoneDensityThreshold && !bSquadTooClose)
		{
			Cell.ZoneLeft = FMath::Max(Cell.ZoneLeft, Config.ZoneDurationSeconds);
		}
		else
		{
			Cell.ZoneLeft = FMath::Max(0.f, Cell.ZoneLeft - FMath::Max(0.f, DeltaSeconds));
		}
		if (bSquadTooClose)
		{
			Cell.ZoneLeft = 0.f; // the squad moved up to it: no fear zone on top of the squad
		}
		return Cell.Density > 0.f || Cell.ZoneLeft > 0.f;
	}

	bool IsZoneOpen(const FSuppressionFireCell& Cell)
	{
		return Cell.ZoneLeft > 0.f;
	}

	int32 PickClickTarget(const FSuppressionConfig& Config, const FVector& Point, const TArray<FSuppressionClickCandidate>& Candidates)
	{
		int32 Best = INDEX_NONE;
		float BestDistSq = FMath::Square(FMath::Max(0.f, Config.OrderClickRadiusCm));
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			if (!Candidates[Index].bKnown)
			{
				continue;
			}
			const float DistSq = static_cast<float>(FVector::DistSquared2D(Point, Candidates[Index].Location));
			if (DistSq <= BestDistSq)
			{
				BestDistSq = DistSq;
				Best = Index;
			}
		}
		return Best;
	}

	bool IgnoresSuppression(bool bRaging)
	{
		return bRaging;
	}

	bool EnemyMayFire(ESuppressionTier Tier)
	{
		return Tier == ESuppressionTier::None;
	}

	FSuppressionFireEffect OperativeFire(const FSuppressionConfig& Config, ESuppressionTier Tier)
	{
		FSuppressionFireEffect Effect;
		if (Tier == ESuppressionTier::Pinned)
		{
			Effect.bMayFire = false;
			Effect.bBlind = true;
		}
		else if (Tier == ESuppressionTier::Suppressed)
		{
			Effect.bBlind = true;
			Effect.HitMultiplier = FMath::Clamp(Config.SuppressedHitMultiplier, 0.f, 1.f);
			Effect.SpreadMultiplier = FMath::Max(1.f, Config.SuppressedSpreadMultiplier);
		}
		return Effect;
	}

	bool MayExpose(ESuppressionTier Tier)
	{
		return Tier == ESuppressionTier::None;
	}

	bool IsFullAuto(float FullAutoIntervalSeconds)
	{
		return FullAutoIntervalSeconds > 0.f;
	}

	float NextOrderRoundDelay(const FSuppressionConfig& Config, float WeaponFireRate, float FullAutoIntervalSeconds, int32& InOutBurstCount)
	{
		if (!IsFullAuto(FullAutoIntervalSeconds))
		{
			InOutBurstCount = 0;
			return FMath::Max(0.05f, WeaponFireRate);
		}
		const int32 BurstRounds = FMath::Max(1, FMath::RoundToInt(Config.OrderBurstRounds));
		++InOutBurstCount;
		if (InOutBurstCount >= BurstRounds)
		{
			InOutBurstCount = 0;
			return FullAutoIntervalSeconds + FMath::Max(0.f, Config.OrderBurstPauseSeconds);
		}
		return FullAutoIntervalSeconds;
	}

	float GetOrderSpreadDeg(const FSuppressionConfig& Config, int32 StanceIndex)
	{
		return StanceIndex >= 2 ? Config.OrderSpreadProneDeg : (StanceIndex == 1 ? Config.OrderSpreadCrouchingDeg : Config.OrderSpreadStandingDeg);
	}

	int32 OrderAmmoBudget(const FSuppressionConfig& Config, int32 Clip, int32 Reserve)
	{
		return FMath::Max(0, FMath::Max(0, Clip) + FMath::Max(0, Reserve) - FMath::RoundToInt(Config.OrderStopAtRounds));
	}

	ESuppressOrderStop CheckOrder(const FSuppressionConfig& Config, bool bHasTarget, bool bTargetAlive, bool bUsesAmmo, int32 Clip, int32 Reserve)
	{
		if (!bUsesAmmo)
		{
			return ESuppressOrderStop::NoWeapon;
		}
		if (bHasTarget && !bTargetAlive)
		{
			return ESuppressOrderStop::TargetDown;
		}
		if (OrderAmmoBudget(Config, Clip, Reserve) <= 0)
		{
			return ESuppressOrderStop::AmmoLow;
		}
		return ESuppressOrderStop::None;
	}

	const TCHAR* TierName(ESuppressionTier Tier)
	{
		switch (Tier)
		{
		case ESuppressionTier::Suppressed: return TEXT("suppressed");
		case ESuppressionTier::Pinned: return TEXT("pinned");
		default: return TEXT("free");
		}
	}

	const TCHAR* TierBadge(ESuppressionTier Tier)
	{
		switch (Tier)
		{
		case ESuppressionTier::Suppressed: return TEXT("SUPPRESSED");
		case ESuppressionTier::Pinned: return TEXT("PINNED");
		default: return TEXT("");
		}
	}

	const TCHAR* StopName(ESuppressOrderStop Stop)
	{
		switch (Stop)
		{
		case ESuppressOrderStop::TargetDown: return TEXT("target down");
		case ESuppressOrderStop::AmmoLow: return TEXT("ammo low");
		case ESuppressOrderStop::NoWeapon: return TEXT("no ammo weapon");
		default: return TEXT("going on");
		}
	}

	FString GetDefaultPath()
	{
		return FPaths::ProjectContentDir() / TEXT("Data/Combat/suppression.json");
	}

	TArray<FString> KnownKeys()
	{
		TArray<FString> Keys;
		for (const FSuppressionTuningKey& Entry : SuppressionTuningKeys)
		{
			Keys.Add(Entry.Key);
		}
		return Keys;
	}

	bool GetValue(const FSuppressionConfig& Config, const FString& Key, float& OutValue)
	{
		for (const FSuppressionTuningKey& Entry : SuppressionTuningKeys)
		{
			if (Key.Equals(Entry.Key, ESearchCase::CaseSensitive))
			{
				OutValue = Config.*(Entry.Member);
				return true;
			}
		}
		return false;
	}

	bool ApplyJson(const FString& JsonText, FSuppressionConfig& InOut, TArray<FString>* OutUnknown)
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
			const FSuppressionTuningKey* Entry = nullptr;
			for (const FSuppressionTuningKey& Each : SuppressionTuningKeys)
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

	const FSuppressionConfig& GetConfig()
	{
		if (!bSuppressionConfigLoaded)
		{
			ReloadConfig();
		}
		return SuppressionConfigInForce();
	}

	void SetConfig(const FSuppressionConfig& Config)
	{
		bSuppressionConfigLoaded = true;
		SuppressionConfigInForce() = Config;
	}

	bool ReloadConfig()
	{
		bSuppressionConfigLoaded = true;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *GetDefaultPath()))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Suppression tuning: %s missing - the code defaults are used"), *GetDefaultPath());
			return false;
		}
		FSuppressionConfig Loaded;
		TArray<FString> Unknown;
		if (!ApplyJson(Text, Loaded, &Unknown))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Suppression tuning: %s unreadable (no \"params\" object) - values kept"), *GetDefaultPath());
			return false;
		}
		for (const FString& Key : Unknown)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Suppression tuning: unknown / non-numeric key \"%s\" skipped"), *Key);
		}
		SuppressionConfigInForce() = Loaded;
		return true;
	}
}
