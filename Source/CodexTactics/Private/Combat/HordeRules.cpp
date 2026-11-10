#include "Combat/HordeRules.h"
#include "Data/LevelJsonRules.h"

#include "CodexTactics.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace HordeRules
{
	bool IsCountdownRunning(ECodexGamePhase Phase, ECodexCombatMode Mode, bool bWorldAIPaused)
	{
		return !bWorldAIPaused && (Phase == ECodexGamePhase::Exploration || (Phase == ECodexGamePhase::WaveCombat && Mode == ECodexCombatMode::RealTime));
	}

	bool IsInDistanceBand(float DistanceCm, const FHordeConfig& Config)
	{
		return DistanceCm >= Config.MinDistanceCm && DistanceCm <= Config.MaxDistanceCm;
	}

	int32 PickSpawnPoint(const TArray<FHordeSpawnCandidate>& Candidates, const FHordeConfig& Config)
	{
		const float Middle = (Config.MinDistanceCm + Config.MaxDistanceCm) * 0.5f;
		int32 Best = INDEX_NONE;
		float BestScore = TNumericLimits<float>::Lowest();
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			const FHordeSpawnCandidate& Candidate = Candidates[Index];
			if (!Candidate.bReachable || !IsInDistanceBand(Candidate.DistanceCm, Config))
			{
				continue;
			}
			// Hidden beats visible (a big bonus), then the band's middle beats its edges.
			const float Score = (Config.bPreferOutOfSight && !Candidate.bVisibleToSquad ? 100000.f : 0.f)
				- FMath::Abs(Candidate.DistanceCm - Middle);
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = Index;
			}
		}
		return Best;
	}

	int32 GetHordeSize(const FHordeConfig& Config)
	{
		return FMath::Max(1, Config.Count);
	}

	TArray<EEnemyArchetype> BuildComposition(const FHordeConfig& Config, int32 Total)
	{
		TArray<EEnemyArchetype> Result;
		for (const FPressureEnemyType& Member : BuildMembers(Config, Total))
		{
			Result.Add(Member.Type);
		}
		return Result;
	}

	TArray<FPressureEnemyType> BuildMembers(const FHordeConfig& Config, int32 Total)
	{
		TArray<FPressureEnemyType> Result;
		float WeightSum = 0.f;
		for (const FHordeMixEntry& Entry : Config.Composition)
		{
			WeightSum += FMath::Max(Entry.Weight, 0.f);
		}
		if (Total <= 0)
		{
			return Result;
		}
		if (WeightSum <= 0.f)
		{
			Result.Init(FPressureEnemyType(EEnemyArchetype::FrostHound), Total);
			return Result;
		}
		// Largest remainder: floor of each share, the rest to the biggest fractions (ties: list order).
		TArray<int32> Counts;
		TArray<TPair<float, int32>> Remainders;
		int32 Assigned = 0;
		for (int32 Index = 0; Index < Config.Composition.Num(); ++Index)
		{
			const float Share = Total * FMath::Max(Config.Composition[Index].Weight, 0.f) / WeightSum;
			const int32 Floor = FMath::FloorToInt(Share);
			Counts.Add(Floor);
			Assigned += Floor;
			Remainders.Emplace(Share - Floor, Index);
		}
		Remainders.StableSort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key > B.Key; });
		for (int32 Index = 0; Assigned < Total && Remainders.Num() > 0; ++Index)
		{
			++Counts[Remainders[Index % Remainders.Num()].Value];
			++Assigned;
		}
		for (int32 Index = 0; Index < Config.Composition.Num(); ++Index)
		{
			for (int32 Count = 0; Count < Counts[Index]; ++Count)
			{
				Result.Add(FPressureEnemyType(Config.Composition[Index].Type, Config.Composition[Index].Variant));
			}
		}
		return Result;
	}

	bool IsAllowedHordeType(EEnemyArchetype Type)
	{
		return Type != EEnemyArchetype::Spitter && Type != EEnemyArchetype::Base;
	}

	bool ParseCompositionKey(const FString& Key, FHordeMixEntry& OutEntry)
	{
		FPressureEnemyType Variant;
		if (CombatPressureRules::ParseVariantKey(Key, Variant))
		{
			OutEntry.Type = Variant.Type;
			OutEntry.Variant = Variant.Variant;
			return true;
		}
		EEnemyArchetype Type = EEnemyArchetype::FrostHound;
		if (!LevelJsonRules::ParseEnemyType(Key, Type) || !IsAllowedHordeType(Type))
		{
			return false;
		}
		OutEntry.Type = Type;
		OutEntry.Variant = NAME_None;
		return true;
	}

	FVector ClusterOffset(int32 Index, int32 Count, float ClusterRadiusCm)
	{
		if (Index <= 0 || Count <= 1)
		{
			return FVector::ZeroVector;
		}
		// Sunflower (golden angle): even spacing inside the disc, the last member on its edge.
		constexpr float GoldenAngle = 137.50776f;
		const float Radius = ClusterRadiusCm * FMath::Sqrt(static_cast<float>(Index) / static_cast<float>(Count - 1));
		return FVector(Radius, 0.f, 0.f).RotateAngleAxis(GoldenAngle * Index, FVector::UpVector);
	}

	void ApplyJson(const FJsonObject& Json, FHordeConfig& Config)
	{
		bool Bool = false;
		double Number = 0.0;
		FString Text;
		if (Json.TryGetBoolField(TEXT("enabled"), Bool))
		{
			Config.bEnabled = Bool;
		}
		// Game designer 2026-11-09: no horde after a long real-time fight any more - "trigger_seconds" / "repeats" / "repeat_seconds" /
		// "count_increase_per_repeat" are not read (an old level override that still has them changes nothing).
		if (Json.TryGetNumberField(TEXT("count"), Number))
		{
			Config.Count = FMath::Max(1, FMath::RoundToInt(Number));
		}
		const TSharedPtr<FJsonObject>* Mix = nullptr;
		if (Json.TryGetObjectField(TEXT("composition"), Mix) && Mix && Mix->IsValid())
		{
			TArray<FHordeMixEntry> Parsed;
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Mix)->Values)
			{
				FHordeMixEntry MixEntry;
				double Weight = 0.0;
				if (!ParseCompositionKey(Entry.Key, MixEntry))
				{
					UE_LOG(LogCodexTactics, Display, TEXT("[Horde] composition: '%s' skipped (unknown, or the Spitter - the Military Zombie took its place)"),
						*Entry.Key);
					continue;
				}
				if (Entry.Value.IsValid() && Entry.Value->TryGetNumber(Weight) && Weight > 0.0)
				{
					MixEntry.Weight = static_cast<float>(Weight);
					Parsed.Add(MixEntry);
				}
			}
			if (!Parsed.IsEmpty())
			{
				Config.Composition = MoveTemp(Parsed);
			}
		}
		if (Json.TryGetNumberField(TEXT("min_distance_m"), Number))
		{
			Config.MinDistanceCm = FMath::Max(0.f, static_cast<float>(Number) * 100.f);
		}
		if (Json.TryGetNumberField(TEXT("max_distance_m"), Number))
		{
			Config.MaxDistanceCm = FMath::Max(0.f, static_cast<float>(Number) * 100.f);
		}
		if (Config.MaxDistanceCm < Config.MinDistanceCm)
		{
			Swap(Config.MinDistanceCm, Config.MaxDistanceCm);
		}
		if (Json.TryGetNumberField(TEXT("cluster_radius_m"), Number))
		{
			Config.ClusterRadiusCm = FMath::Max(0.f, static_cast<float>(Number) * 100.f);
		}
		if (Json.TryGetBoolField(TEXT("prefer_out_of_sight"), Bool))
		{
			Config.bPreferOutOfSight = Bool;
		}
		if (Json.TryGetNumberField(TEXT("spawn_samples"), Number))
		{
			Config.SpawnSamples = FMath::Clamp(FMath::RoundToInt(Number), 1, 256);
		}
		if (Json.TryGetBoolField(TEXT("apply_wave_modifiers"), Bool))
		{
			Config.bApplyWaveModifiers = Bool;
		}
		if (Json.TryGetNumberField(TEXT("warning_seconds"), Number))
		{
			Config.WarningSeconds = FMath::Max(0.f, static_cast<float>(Number));
		}
		if (Json.TryGetStringField(TEXT("warning_sound"), Text))
		{
			Config.WarningSound = Text;
		}
		// Horde #12: the firefight count (FirefightRules).
		FFirefightConfig& Fire = Config.Firefight;
		if (Json.TryGetNumberField(TEXT("firefight_lost_contact_seconds"), Number))
		{
			Fire.LostContactSeconds = FMath::Max(1.f, static_cast<float>(Number));
		}
		if (Json.TryGetNumberField(TEXT("firefight_contact_range_m"), Number))
		{
			Fire.ContactRangeCm = FMath::Max(0.f, static_cast<float>(Number) * 100.f);
		}
		if (Json.TryGetBoolField(TEXT("firefight_slip_past_resets"), Bool))
		{
			Fire.bSlipPastResets = Bool;
		}
		if (Json.TryGetNumberField(TEXT("firefight_slip_min_reach_m"), Number))
		{
			Fire.SlipMinReachCm = FMath::Max(0.f, static_cast<float>(Number) * 100.f);
		}
		if (Json.TryGetNumberField(TEXT("firefight_slip_max_reach_m"), Number))
		{
			Fire.SlipMaxReachCm = FMath::Max(0.f, static_cast<float>(Number) * 100.f);
		}
		Fire.SlipMaxReachCm = FMath::Max(Fire.SlipMaxReachCm, Fire.SlipMinReachCm);
		if (Json.TryGetNumberField(TEXT("firefight_slip_exit_factor"), Number))
		{
			Fire.SlipExitFactor = FMath::Max(1.f, static_cast<float>(Number));
		}
		if (Json.TryGetNumberField(TEXT("firefight_warning_seconds"), Number))
		{
			Fire.WarningLeadSeconds = FMath::Max(0.f, static_cast<float>(Number));
		}
		if (Json.TryGetNumberField(TEXT("firefight_hint_before"), Number))
		{
			Fire.HintFirefightsBefore = FMath::Max(0, FMath::RoundToInt(Number));
		}
		// Game designer play-test 2026-11-09: the firefight horde far away, somewhere new each time; the level's waves feed it.
		if (Json.TryGetNumberField(TEXT("firefight_spawn_distance_m"), Number))
		{
			Config.FirefightSpawnDistanceCm = FMath::Max(0.f, static_cast<float>(Number) * 100.f);
		}
		if (Json.TryGetNumberField(TEXT("firefight_spawn_spread_m"), Number))
		{
			Config.FirefightSpawnSpreadCm = FMath::Max(0.f, static_cast<float>(Number) * 100.f);
		}
		if (Json.TryGetNumberField(TEXT("firefight_bearing_change_deg"), Number))
		{
			Config.FirefightBearingChangeDeg = FMath::Clamp(static_cast<float>(Number), 0.f, 180.f);
		}
		if (Json.TryGetNumberField(TEXT("firefight_bearing_relax_share"), Number))
		{
			Config.FirefightBearingRelaxShare = FMath::Clamp(static_cast<float>(Number), 0.f, 1.f);
		}
		if (Json.TryGetNumberField(TEXT("firefight_spawn_bearings"), Number))
		{
			Config.FirefightSpawnBearings = FMath::Clamp(FMath::RoundToInt(Number), 4, 64);
		}
		if (Json.TryGetNumberField(TEXT("firefight_fallback_share"), Number))
		{
			Config.FirefightFallbackShare = FMath::Clamp(static_cast<float>(Number), 0.f, 1.f);
		}
		if (Json.TryGetBoolField(TEXT("waves_feed_horde"), Bool))
		{
			Config.bWavesFeedHorde = Bool;
		}
	}

	bool ApplyJsonText(const FString& Text, FHordeConfig& Config)
	{
		TSharedPtr<FJsonObject> Root;
		if (Text.TrimStartAndEnd().IsEmpty() || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			return false;
		}
		ApplyJson(*Root, Config);
		return true;
	}

	FString GetDefaultPath()
	{
		return FPaths::ProjectContentDir() / TEXT("Data/AI/horde.json");
	}

	FHordeConfig LoadDefaults()
	{
		FHordeConfig Config;
		FString Text;
		if (FFileHelper::LoadFileToString(Text, *GetDefaultPath()))
		{
			ApplyJsonText(Text, Config);
		}
		return Config;
	}

	FHordeConfig ResolveForLevel(const FHordeConfig& Defaults, bool bLevelHordeEnabled, const FString& LevelOverrideJson, int32 LevelAfterFirefights)
	{
		FHordeConfig Config = Defaults;
		Config.AfterFirefights = FMath::Max(1, LevelAfterFirefights);
		if (!LevelOverrideJson.IsEmpty())
		{
			ApplyJsonText(LevelOverrideJson, Config);
		}
		Config.bEnabled = Config.bEnabled && bLevelHordeEnabled;
		return Config;
	}

	bool ShouldWavesFeedHorde(const FHordeConfig& Config, int32 NumLevelWaves, bool bAmbushCombatStart, bool bScriptedWavesPlayed)
	{
		return Config.bWavesFeedHorde && NumLevelWaves > 0 && bAmbushCombatStart && !bScriptedWavesPlayed;
	}

	int32 WaveForHorde(int32 HordeOrdinal, int32 NumLevelWaves)
	{
		return NumLevelWaves > 0 ? FMath::Clamp(HordeOrdinal, 1, NumLevelWaves) - 1 : INDEX_NONE;
	}

	FHordePlan PlanFromWave(const FWaveDefinition& Wave, int32 WaveNumber)
	{
		FHordePlan Plan;
		Plan.SourceWave = WaveNumber;
		Plan.MaxAlive = FMath::Max(1, Wave.MaxSimultaneousEnemies);
		Plan.bHasModifiers = true;
		Plan.Modifiers = Wave.Modifiers;
		Plan.Entries = Wave.Spawns;
		FPressureEnemyType MilitaryZombie;
		CombatPressureRules::ParseVariantKey(TEXT("MILITARY_ZOMBIE"), MilitaryZombie);
		for (int32 EntryIndex = 0; EntryIndex < Wave.Spawns.Num(); ++EntryIndex)
		{
			const FEnemySpawnEntry& Entry = Wave.Spawns[EntryIndex];
			FPressureEnemyType Enemy(Entry.EnemyType, Entry.Variant);
			if (Entry.EnemyType == EEnemyArchetype::Spitter)
			{
				Enemy = MilitaryZombie; // game designer 2026-11-09: the Spitter is refused in the horde
				Plan.SpitterSwaps += FMath::Max(0, Entry.Count);
			}
			else if (!IsAllowedHordeType(Entry.EnemyType))
			{
				continue;
			}
			for (int32 Count = 0; Count < Entry.Count; ++Count)
			{
				Plan.Members.Add({ Enemy, EntryIndex });
			}
		}
		return Plan;
	}

	FHordePlan PlanFromConfig(const FHordeConfig& Config)
	{
		FHordePlan Plan;
		for (const FPressureEnemyType& Enemy : BuildMembers(Config, GetHordeSize(Config)))
		{
			Plan.Members.Add({ Enemy, INDEX_NONE });
		}
		return Plan;
	}

	int32 MembersToSpawnNow(const FHordePlan& Plan, int32 Spawned, int32 Alive)
	{
		const int32 Left = FMath::Max(0, Plan.Members.Num() - Spawned);
		return Plan.MaxAlive > 0 ? FMath::Clamp(Plan.MaxAlive - Alive, 0, Left) : Left;
	}

	float BearingDeg(const FVector& From, const FVector& To)
	{
		const FVector Delta = To - From;
		const float Deg = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
		return Deg < 0.f ? Deg + 360.f : Deg;
	}

	bool IsBearingFarEnough(float BearingA, float BearingB, float MinChangeDeg)
	{
		return FMath::Abs(FMath::FindDeltaAngleDegrees(BearingA, BearingB)) >= MinChangeDeg - KINDA_SMALL_NUMBER;
	}

	float FarBandMinCm(const FHordeConfig& Config)
	{
		return FMath::Max(Config.MinDistanceCm, Config.FirefightSpawnDistanceCm - Config.FirefightSpawnSpreadCm);
	}

	float FarBandMaxCm(const FHordeConfig& Config)
	{
		return FMath::Max(FarBandMinCm(Config), Config.FirefightSpawnDistanceCm + Config.FirefightSpawnSpreadCm);
	}

	TArray<float> FarProbeRadii(const FHordeConfig& Config)
	{
		TArray<float> Radii;
		const float Max = FarBandMaxCm(Config);
		const float Min = FarBandMinCm(Config);
		const float Floor = FMath::Max(Config.MinDistanceCm, 100.f);
		Radii.Add(Max);
		if (Config.FirefightSpawnDistanceCm < Max - 1.f && Config.FirefightSpawnDistanceCm > Min + 1.f)
		{
			Radii.Add(Config.FirefightSpawnDistanceCm);
		}
		if (Min < Max - 1.f)
		{
			Radii.Add(Min);
		}
		for (float Radius = Min * 0.8f; Radius >= Floor; Radius *= 0.8f)
		{
			Radii.Add(Radius);
		}
		if (Radii.Last() > Floor + 1.f)
		{
			Radii.Add(Floor);
		}
		return Radii;
	}

	FHordeFarPick PickFarSpawnPoint(const TArray<FHordeFarCandidate>& Candidates, const FHordeConfig& Config, bool bHasPreviousBearing,
		float PreviousBearingDeg, FRandomStream& Random)
	{
		FHordeFarPick Pick;
		TArray<int32> Usable;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			if (Candidates[Index].bReachable && Candidates[Index].DistanceCm >= Config.MinDistanceCm - 1.f)
			{
				Usable.Add(Index);
			}
		}
		if (Usable.IsEmpty())
		{
			return Pick;
		}
		auto Filter = [&Candidates](const TArray<int32>& From, TFunctionRef<bool(const FHordeFarCandidate&)> Keep)
		{
			TArray<int32> Out;
			for (const int32 Index : From)
			{
				if (Keep(Candidates[Index]))
				{
					Out.Add(Index);
				}
			}
			return Out;
		};
		auto Farthest = [&Candidates](const TArray<int32>& From)
		{
			float Best = 0.f;
			for (const int32 Index : From)
			{
				Best = FMath::Max(Best, Candidates[Index].DistanceCm);
			}
			return Best;
		};
		// 1. Out of the squad's sight first.
		TArray<int32> Pool = Usable;
		if (Config.bPreferOutOfSight)
		{
			TArray<int32> Hidden = Filter(Usable, [](const FHordeFarCandidate& Each) { return !Each.bVisibleToSquad; });
			if (!Hidden.IsEmpty())
			{
				Pool = MoveTemp(Hidden);
			}
		}
		// 2. Somewhere new: off the previous horde's bearing - unless that leaves only points under FirefightBearingRelaxShare as far.
		if (bHasPreviousBearing && Config.FirefightBearingChangeDeg > 0.f)
		{
			const TArray<int32> NewBearing = Filter(Pool, [&Config, PreviousBearingDeg](const FHordeFarCandidate& Each)
			{
				return IsBearingFarEnough(Each.BearingDeg, PreviousBearingDeg, Config.FirefightBearingChangeDeg);
			});
			if (!NewBearing.IsEmpty() && Farthest(NewBearing) >= Farthest(Pool) * Config.FirefightBearingRelaxShare - 1.f)
			{
				Pool = NewBearing;
			}
			else
			{
				Pick.bBearingRelaxed = true;
			}
		}
		// 3. In the band at random, else at random among the farthest.
		const float BandMin = FarBandMinCm(Config);
		const float BandMax = FarBandMaxCm(Config);
		TArray<int32> Chosen = Filter(Pool, [BandMin, BandMax](const FHordeFarCandidate& Each)
		{
			return Each.DistanceCm >= BandMin - 1.f && Each.DistanceCm <= BandMax + 1.f;
		});
		Pick.Kind = EHordeFarPick::InBand;
		if (Chosen.IsEmpty())
		{
			const float Floor = Farthest(Pool) * Config.FirefightFallbackShare;
			Chosen = Filter(Pool, [Floor](const FHordeFarCandidate& Each) { return Each.DistanceCm >= Floor - 1.f; });
			Pick.Kind = EHordeFarPick::Farthest;
		}
		Pick.Index = Chosen[Random.RandRange(0, Chosen.Num() - 1)];
		Pick.bVisible = Candidates[Pick.Index].bVisibleToSquad;
		return Pick;
	}
}

FString FHordePlan::Describe() const
{
	TArray<TPair<FString, int32>> Counts;
	for (const FHordeMember& Member : Members)
	{
		const FString Label = CombatPressureRules::EnemyLabel(Member.Enemy).Replace(TEXT("EEnemyArchetype::"), TEXT(""));
		TPair<FString, int32>* Found = Counts.FindByPredicate([&Label](const TPair<FString, int32>& Each) { return Each.Key == Label; });
		if (Found)
		{
			++Found->Value;
		}
		else
		{
			Counts.Emplace(Label, 1);
		}
	}
	TArray<FString> Parts;
	for (const TPair<FString, int32>& Each : Counts)
	{
		Parts.Add(FString::Printf(TEXT("%d %s"), Each.Value, *Each.Key));
	}
	return Parts.IsEmpty() ? TEXT("nobody") : FString::Join(Parts, TEXT(", "));
}

