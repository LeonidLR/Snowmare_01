// Pure rules of falls, running drops and knockback falls (game designer items 6 / 10 / 11a, 2026-11-03). UE-only, no Godot reference.

#include "Falls/FallRules.h"

#include "CodexTactics.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Ladders/LadderRules.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace FallRules
{
	struct FFallTuningKey
	{
		const TCHAR* Key;
		float FFallConfig::* Member;
	};

	/** falls.json "params" key -> FFallConfig member. */
	static const FFallTuningKey FallTuningKeys[] = {
		{ TEXT("damage.safe_height_cm"), &FFallConfig::SafeHeightCm },
		{ TEXT("damage.per_cm"), &FFallConfig::DamagePerCm },
		{ TEXT("damage.max"), &FFallConfig::MaxDamage },
		{ TEXT("damage.heavy_height_cm"), &FFallConfig::HeavyHeightCm },
		{ TEXT("damage.heavy_min"), &FFallConfig::HeavyDamageMin },
		{ TEXT("damage.heavy_max"), &FFallConfig::HeavyDamageMax },
		{ TEXT("damage.heavy_max_health_fraction"), &FFallConfig::HeavyMaxHealthFraction },
		{ TEXT("damage.lethal_height_cm"), &FFallConfig::LethalHeightCm },
		{ TEXT("track.min_fall_cm"), &FFallConfig::MinTrackedFallCm },
		{ TEXT("link.min_drop_cm"), &FFallConfig::LinkMinDropCm },
		{ TEXT("link.max_drop_cm"), &FFallConfig::LinkMaxDropCm },
		{ TEXT("link.max_gap_cm"), &FFallConfig::LinkMaxGapCm },
		{ TEXT("link.spacing_cm"), &FFallConfig::LinkSpacingCm },
		{ TEXT("link.landing_margin_cm"), &FFallConfig::LandingMarginCm },
		{ TEXT("link.reference_speed_cms"), &FFallConfig::ReferenceSpeedCms },
		{ TEXT("link.min_lower_depth_cm"), &FFallConfig::MinLowerDepthCm },
		{ TEXT("link.probe_step_cm"), &FFallConfig::ProbeStepCm },
		{ TEXT("link.step_tolerance_cm"), &FFallConfig::StepToleranceCm },
		{ TEXT("link.edge_search_cm"), &FFallConfig::EdgeSearchCm },
		{ TEXT("link.max_links"), &FFallConfig::MaxLinks },
		{ TEXT("takeoff.min_speed_cms"), &FFallConfig::TakeoffMinSpeedCms },
		{ TEXT("takeoff.speed_margin"), &FFallConfig::TakeoffSpeedMargin },
		{ TEXT("takeoff.max_speed_cms"), &FFallConfig::TakeoffMaxSpeedCms },
		{ TEXT("takeoff.runup_timeout_s"), &FFallConfig::RunUpTimeoutSeconds },
		{ TEXT("air.gravity_scale"), &FFallConfig::AirGravityScale },
		{ TEXT("air.timeout_s"), &FFallConfig::AirTimeoutSeconds },
		{ TEXT("land.settle_s"), &FFallConfig::LandSettleSeconds },
		{ TEXT("lease.radius_cm"), &FFallConfig::LeaseRadiusCm },
		{ TEXT("lease.hold_after_landing_s"), &FFallConfig::LeaseHoldAfterLandingSeconds },
		{ TEXT("knockback.reach_cm"), &FFallConfig::KnockbackReachCm },
		{ TEXT("knockback.speed_cms"), &FFallConfig::KnockbackSpeedCms },
		{ TEXT("knockback.up_cms"), &FFallConfig::KnockbackUpCms },
		{ TEXT("knockback.parapet_height_cm"), &FFallConfig::ParapetHeightCm },
		{ TEXT("knockback.min_fall_cm"), &FFallConfig::KnockbackMinFallCm },
		{ TEXT("anim.fall_min_s"), &FFallConfig::AnimFallMinSeconds },
		{ TEXT("anim.land_s"), &FFallConfig::AnimLandSeconds },
		{ TEXT("link.commit_radius_cm"), &FFallConfig::LinkCommitRadiusCm },
		{ TEXT("rescue.after_s"), &FFallConfig::RescueAfterSeconds },
		{ TEXT("rescue.watch_s"), &FFallConfig::RescueWatchSeconds },
		{ TEXT("rescue.max_cm"), &FFallConfig::RescueMaxCm },
		{ TEXT("rescue.level_tolerance_cm"), &FFallConfig::RescueLevelToleranceCm },
		{ TEXT("rescue.cooldown_s"), &FFallConfig::RescueCooldownSeconds },
		{ TEXT("enemy_drops.base"), &FFallConfig::EnemyDropsBase },
		{ TEXT("enemy_drops.frost_hound"), &FFallConfig::EnemyDropsFrostHound },
		{ TEXT("enemy_drops.spitter"), &FFallConfig::EnemyDropsSpitter },
		{ TEXT("enemy_drops.brute"), &FFallConfig::EnemyDropsBrute },
		{ TEXT("enemy_drops.frostbitten"), &FFallConfig::EnemyDropsFrostbitten },
		{ TEXT("enemy_drops.cutter"), &FFallConfig::EnemyDropsCutter },
		{ TEXT("enemy_drops.cryo_drone"), &FFallConfig::EnemyDropsCryoDrone },
		{ TEXT("enemy_drops.marksman"), &FFallConfig::EnemyDropsMarksman },
		{ TEXT("enemy_drops.hurt_entering_cm"), &FFallConfig::EnemyHurtDropEnteringCm },
		{ TEXT("enemy_drops.jumper_max_speed_cms"), &FFallConfig::JumperTakeoffMaxSpeedCms },
	};

	/** enemy_drops.<archetype> in EEnemyArchetype order (Base, FrostHound, Spitter, Brute, Frostbitten, Cutter, CryoDrone, Marksman). */
	static float FFallConfig::* const FallEnemyDropMembers[] = {
		&FFallConfig::EnemyDropsBase, &FFallConfig::EnemyDropsFrostHound, &FFallConfig::EnemyDropsSpitter, &FFallConfig::EnemyDropsBrute,
		&FFallConfig::EnemyDropsFrostbitten, &FFallConfig::EnemyDropsCutter, &FFallConfig::EnemyDropsCryoDrone, &FFallConfig::EnemyDropsMarksman,
	};

	static FFallConfig& FallConfigInForce()
	{
		static FFallConfig Config;
		return Config;
	}

	static bool bFallConfigLoaded = false;

	FFallVerdict JudgeFall(float HeightCm, const FFallConfig& Config)
	{
		FFallVerdict Verdict;
		if (Config.LethalHeightCm > 0.f && HeightCm >= Config.LethalHeightCm)
		{
			Verdict.Damage = LethalDamage;
			Verdict.Outcome = EFallOutcome::Lethal;
			return Verdict;
		}
		if (Config.HeavyHeightCm > 0.f && HeightCm >= Config.HeavyHeightCm)
		{
			const float Span = Config.LethalHeightCm > Config.HeavyHeightCm ? Config.LethalHeightCm - Config.HeavyHeightCm : 0.f;
			const float Alpha = Span > 0.f ? FMath::Clamp((HeightCm - Config.HeavyHeightCm) / Span, 0.f, 1.f) : 0.f;
			Verdict.Damage = FMath::Max(0.f, FMath::Lerp(Config.HeavyDamageMin, Config.HeavyDamageMax, Alpha));
			Verdict.Outcome = EFallOutcome::HeavyWound;
			Verdict.bKnockdown = true;
			return Verdict;
		}
		Verdict.Damage = LadderRules::FallDamage(HeightCm, LadderRules::FFallDamage{ Config.SafeHeightCm, Config.DamagePerCm, Config.MaxDamage });
		Verdict.Outcome = Verdict.Damage > 0.f ? EFallOutcome::Hurt : EFallOutcome::Unhurt;
		return Verdict;
	}

	FFallVerdict JudgeFallForBody(float HeightCm, float MaxHealth, const FFallConfig& Config)
	{
		FFallVerdict Verdict = JudgeFall(HeightCm, Config);
		if (Verdict.Outcome == EFallOutcome::HeavyWound && MaxHealth > 0.f && Config.HeavyMaxHealthFraction > 0.f)
		{
			Verdict.Damage = FMath::Min(Verdict.Damage, MaxHealth * Config.HeavyMaxHealthFraction);
		}
		return Verdict;
	}

	float FallDamage(float HeightCm, const FFallConfig& Config)
	{
		return JudgeFall(HeightCm, Config).Damage;
	}

	EEnemyDropUse EnemyDropUse(int32 ArchetypeIndex, const FFallConfig& Config)
	{
		if (ArchetypeIndex < 0 || ArchetypeIndex >= static_cast<int32>(UE_ARRAY_COUNT(FallEnemyDropMembers)))
		{
			return EEnemyDropUse::None;
		}
		const int32 Value = FMath::RoundToInt(Config.*(FallEnemyDropMembers[ArchetypeIndex]));
		return Value >= 2 ? EEnemyDropUse::Jumper : Value == 1 ? EEnemyDropUse::Free : EEnemyDropUse::None;
	}

	EFallAnimPhase FallAnimPhase(bool bDropAirborne, float FallingSeconds, float SecondsSinceFallEnded, const FFallConfig& Config)
	{
		if (bDropAirborne || (FallingSeconds > 0.f && FallingSeconds >= Config.AnimFallMinSeconds))
		{
			return EFallAnimPhase::Falling;
		}
		return SecondsSinceFallEnded >= 0.f && SecondsSinceFallEnded < Config.AnimLandSeconds ? EFallAnimPhase::Landing : EFallAnimPhase::Grounded;
	}

	bool IsCommittedToDrop(const FVector& Feet, const FVector& PathGoal, const FVector& LinkA, const FVector& LinkB, const FFallConfig& Config)
	{
		const FVector& Start = LinkA.Z >= LinkB.Z ? LinkA : LinkB;
		const FVector& End = LinkA.Z >= LinkB.Z ? LinkB : LinkA;
		constexpr float LevelToleranceCm = 80.f;
		if (Start.Z - End.Z < Config.LinkMinDropCm || FMath::Abs(Feet.Z - Start.Z) > LevelToleranceCm || PathGoal.Z > Start.Z - Config.LinkMinDropCm * 0.5f)
		{
			return false;
		}
		const double ToStart = FVector::Dist2D(Feet, Start);
		const double StartToEnd = FVector::Dist2D(Start, End);
		// At its start, or past it on the way to its end (between them and nearer the end than the start is).
		return ToStart <= Config.LinkCommitRadiusCm || (FVector::Dist2D(Feet, End) < StartToEnd && ToStart <= StartToEnd + Config.LinkCommitRadiusCm);
	}

	bool ShouldRescue(ENavStanding Standing, float BadSeconds, float SinceLanding, const FFallConfig& Config)
	{
		return Standing != ENavStanding::Fine && SinceLanding <= Config.RescueWatchSeconds && BadSeconds >= Config.RescueAfterSeconds;
	}

	int32 PickRescuePoint(const FVector& Feet, const TArray<FRescueCandidate>& Candidates, const FFallConfig& Config)
	{
		int32 BestLevel = INDEX_NONE;
		double BestLevelDist = TNumericLimits<double>::Max();
		const double MaxSq = FMath::Square(static_cast<double>(Config.RescueMaxCm));
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			const FRescueCandidate& Candidate = Candidates[Index];
			const double DistSq = FVector::DistSquared(Candidate.Point, Feet);
			if (!Candidate.bReaches || DistSq > MaxSq)
			{
				continue;
			}
			if (FMath::Abs(Candidate.Point.Z - Feet.Z) <= Config.RescueLevelToleranceCm)
			{
				const double Flat = FVector::DistSquared2D(Candidate.Point, Feet);
				if (Flat < BestLevelDist)
				{
					BestLevelDist = Flat;
					BestLevel = Index;
				}
			}
		}
		return BestLevel;
	}

	bool IsHurtingDrop(float DropCm, const FFallConfig& Config)
	{
		return FallDamage(DropCm, Config) > 0.f;
	}

	float AirGravity(float WorldGravityCmS2, const FFallConfig& Config)
	{
		return FMath::Abs(WorldGravityCmS2) * FMath::Max(0.1f, Config.AirGravityScale);
	}

	float KnockbackLaunchScale(const FFallConfig& Config)
	{
		return FMath::Sqrt(FMath::Max(0.1f, Config.AirGravityScale));
	}

	float FallSeconds(float DropCm, float GravityCmS2)
	{
		return DropCm > 0.f && GravityCmS2 > 0.f ? FMath::Sqrt(2.f * DropCm / GravityCmS2) : 0.f;
	}

	FTakeoffPlan PlanTakeoff(float ApproachSpeed, float CapSpeed, float EdgeCm, float NearCm, float FarCm, float DropCm, float GravityCmS2, const FFallConfig& Config)
	{
		FTakeoffPlan Plan;
		Plan.Seconds = FallSeconds(DropCm, GravityCmS2);
		if (Plan.Seconds <= 0.f)
		{
			return Plan;
		}
		const float Margin = Config.LandingMarginCm;
		const float Needed = FMath::Max(0.f, (NearCm + Margin - EdgeCm) / Plan.Seconds);
		const float MinSpeed = FMath::Max(Config.TakeoffMinSpeedCms, Needed * Config.TakeoffSpeedMargin);
		const float MaxSpeed = FarCm > 0.f ? FMath::Max(MinSpeed, (FarCm - Margin - EdgeCm) / Plan.Seconds) : TNumericLimits<float>::Max();
		Plan.SpeedCms = FMath::Min(FMath::Clamp(ApproachSpeed, MinSpeed, MaxSpeed), FMath::Max(0.f, CapSpeed));
		Plan.LandingCm = EdgeCm + Plan.SpeedCms * Plan.Seconds;
		Plan.bTooSlow = Plan.LandingCm < NearCm + Margin * 0.5f;
		Plan.bReaches = !Plan.bTooSlow && (FarCm <= 0.f || Plan.LandingCm <= FarCm - Margin * 0.5f);
		return Plan;
	}

	bool IsHigherLanding(const FProfileSample& Sample, const FDropProfile& Current, const FFallConfig& Config)
	{
		return Sample.bHit && Sample.DropCm >= Config.LinkMinDropCm && Sample.DropCm < Current.DropCm - Config.StepToleranceCm
			&& Sample.DistCm - Current.EdgeCm <= Config.LinkMaxGapCm + Config.ProbeStepCm * 0.5f;
	}

	FDropProfile AnalyseProfile(const TArray<FProfileSample>& Samples, const FFallConfig& Config)
	{
		enum class EPhase : uint8 { Upper, Gap, Lower };
		FDropProfile Out;
		EPhase Phase = EPhase::Upper;
		const float Tol = Config.StepToleranceCm;
		for (const FProfileSample& Sample : Samples)
		{
			if (Phase == EPhase::Upper)
			{
				if (Sample.DistCm > Config.EdgeSearchCm)
				{
					Out.Verdict = EProfileVerdict::NoEdge;
					return Out;
				}
				if (Sample.bHit && FMath::Abs(Sample.DropCm) <= Tol)
				{
					continue; // still the start floor
				}
				if (Sample.bHit && Sample.DropCm < -Tol)
				{
					Out.Verdict = EProfileVerdict::Blocked;
					return Out;
				}
				Out.EdgeCm = Sample.DistCm;
				Phase = EPhase::Gap;
			}
			if (Phase == EPhase::Gap)
			{
				if (Sample.DistCm - Out.EdgeCm > Config.LinkMaxGapCm + Config.ProbeStepCm * 0.5f)
				{
					Out.Verdict = EProfileVerdict::GapTooWide;
					return Out;
				}
				if (!Sample.bHit || Sample.DropCm > Config.LinkMaxDropCm)
				{
					continue; // the gap (nothing below within the band)
				}
				if (Sample.DropCm < -Tol)
				{
					Out.Verdict = EProfileVerdict::Blocked;
					return Out;
				}
				if (Sample.DropCm < Config.LinkMinDropCm)
				{
					Out.Verdict = EProfileVerdict::LowStep;
					return Out;
				}
				Out.NearCm = Sample.DistCm;
				Out.DropCm = Sample.DropCm;
				Out.FarCm = 0.f;
				Phase = EPhase::Lower;
				continue;
			}
			// The lower surface.
			if (Sample.bHit && FMath::Abs(Sample.DropCm - Out.DropCm) <= Tol)
			{
				continue;
			}
			if (IsHigherLanding(Sample, Out, Config))
			{
				// A higher surface still within the gap (the lower roof beyond the ground of the gap): that one is the landing.
				Out.NearCm = Sample.DistCm;
				Out.DropCm = Sample.DropCm;
				continue;
			}
			Out.FarCm = Sample.DistCm;
			break;
		}
		if (Phase == EPhase::Upper)
		{
			Out.Verdict = EProfileVerdict::NoEdge;
			return Out;
		}
		if (Phase == EPhase::Gap)
		{
			Out.Verdict = EProfileVerdict::NoLowerSurface;
			return Out;
		}
		Out.Verdict = Out.FarCm > 0.f && Out.FarCm - Out.NearCm < Config.MinLowerDepthCm ? EProfileVerdict::TooShallow : EProfileVerdict::Ok;
		return Out;
	}

	TArray<float> EdgeSampleFractions(float LengthCm, float SpacingCm)
	{
		TArray<float> Fractions;
		if (LengthCm <= 0.f)
		{
			return Fractions;
		}
		const int32 Count = FMath::Max(1, FMath::RoundToInt(LengthCm / FMath::Max(1.f, SpacingCm)));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Fractions.Add((Index + 0.5f) / Count);
		}
		return Fractions;
	}

	bool IsDuplicateStart(const FVector& Start, float LandZ, const TArray<FVector>& KeptStarts, const TArray<float>& KeptLandZ, float SpacingCm)
	{
		const float Limit = FMath::Square(0.8f * SpacingCm);
		for (int32 Index = 0; Index < KeptStarts.Num(); ++Index)
		{
			if (FMath::Abs(KeptStarts[Index].Z - Start.Z) < 100.f && KeptLandZ.IsValidIndex(Index) && FMath::Abs(KeptLandZ[Index] - LandZ) < 100.f
				&& FVector::DistSquared2D(KeptStarts[Index], Start) < Limit)
			{
				return true;
			}
		}
		return false;
	}

	bool LandingsConflict(const FVector& LandingA, const FVector& LandingB, float RadiusCm)
	{
		return FMath::Abs(LandingA.Z - LandingB.Z) < 150.f && FVector::DistSquared2D(LandingA, LandingB) < FMath::Square(RadiusCm);
	}

	FString DropMarkerLabel(float DropCm, float Damage, bool bFallsShort)
	{
		FString Label = FString::Printf(TEXT("DROP %.1f m"), FMath::Max(0.f, DropCm) / 100.f);
		if (bFallsShort)
		{
			Label += TEXT("  TOO FAR");
		}
		else if (Damage >= LethalDamage)
		{
			Label += TEXT("  LETHAL");
		}
		else if (Damage > 0.f)
		{
			Label += FString::Printf(TEXT("  -%d HP"), FMath::RoundToInt(Damage));
		}
		return Label;
	}

	FVector KnockbackDirection(EKnockdownDirection Side, const FVector& Forward, bool bHasSource, const FVector& VictimLocation, const FVector& SourceLocation)
	{
		if (bHasSource)
		{
			const FVector Away = (VictimLocation - SourceLocation).GetSafeNormal2D();
			if (!Away.IsNearlyZero())
			{
				return Away;
			}
		}
		const FVector Flat = Forward.GetSafeNormal2D();
		return Side == EKnockdownDirection::Front ? Flat : -Flat;
	}

	float KnockbackFlightCm(float SpeedCms, float UpCms, float GravityCmS2)
	{
		return GravityCmS2 > 0.f ? FMath::Max(0.f, SpeedCms) * 2.f * FMath::Max(0.f, UpCms) / GravityCmS2 : 0.f;
	}

	bool KnockbackGoesOver(float EdgeCm, bool bObstacle, float DropCm, float CapsuleRadiusCm, float GravityCmS2, const FFallConfig& Config)
	{
		if (bObstacle || EdgeCm < 0.f || EdgeCm > Config.KnockbackReachCm || DropCm < Config.KnockbackMinFallCm)
		{
			return false;
		}
		return EdgeCm + CapsuleRadiusCm * 0.5f <= KnockbackFlightCm(Config.KnockbackSpeedCms, Config.KnockbackUpCms, GravityCmS2);
	}

	FString GetDefaultPath()
	{
		return FPaths::ProjectContentDir() / TEXT("Data/Movement/falls.json");
	}

	TArray<FString> KnownKeys()
	{
		TArray<FString> Keys;
		for (const FFallTuningKey& Entry : FallTuningKeys)
		{
			Keys.Add(Entry.Key);
		}
		return Keys;
	}

	bool GetValue(const FFallConfig& Config, const FString& Key, float& OutValue)
	{
		for (const FFallTuningKey& Entry : FallTuningKeys)
		{
			if (Key.Equals(Entry.Key, ESearchCase::CaseSensitive))
			{
				OutValue = Config.*(Entry.Member);
				return true;
			}
		}
		return false;
	}

	bool ApplyJson(const FString& JsonText, FFallConfig& InOut, TArray<FString>* OutUnknown)
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
			const FFallTuningKey* Entry = nullptr;
			for (const FFallTuningKey& Each : FallTuningKeys)
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

	const FFallConfig& GetConfig()
	{
		if (!bFallConfigLoaded)
		{
			ReloadConfig();
		}
		return FallConfigInForce();
	}

	void SetConfig(const FFallConfig& Config)
	{
		bFallConfigLoaded = true;
		FallConfigInForce() = Config;
	}

	bool ReloadConfig()
	{
		bFallConfigLoaded = true;
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *GetDefaultPath()))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Fall tuning: %s missing - the code defaults are used"), *GetDefaultPath());
			return false;
		}
		FFallConfig Loaded;
		TArray<FString> Unknown;
		if (!ApplyJson(Text, Loaded, &Unknown))
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Fall tuning: %s unreadable (no \"params\" object) - values kept"), *GetDefaultPath());
			return false;
		}
		for (const FString& Key : Unknown)
		{
			UE_LOG(LogCodexTactics, Warning, TEXT("Fall tuning: unknown / non-numeric key \"%s\" skipped"), *Key);
		}
		FallConfigInForce() = Loaded;
		return true;
	}
}

bool FallRules::ShouldCorpseFall(bool bOnLadder, bool bVaulting, bool bFloorFound, float GapCm)
{
	return bOnLadder || bVaulting || (bFloorFound && GapCm > CorpseFloorToleranceCm);
}

bool FallRules::HasCorpseLanded(bool bMovingOnGround, bool bFloorFound, float GapCm, float FallSeconds)
{
	return bMovingOnGround || (bFloorFound && GapCm <= CorpseLandedGapCm) || FallSeconds >= CorpseFallTimeoutSeconds;
}
