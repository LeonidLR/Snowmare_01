#include "Combat/HordeRules.h"
#include "Data/LevelJsonRules.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

float FHordeTimer::GetNextTriggerSeconds(const FHordeConfig& Config) const
{
	if (!Config.bEnabled)
	{
		return TNumericLimits<float>::Max();
	}
	if (HordesReleased == 0)
	{
		return Config.TriggerSeconds;
	}
	if (!Config.bRepeats)
	{
		return TNumericLimits<float>::Max();
	}
	return Config.TriggerSeconds + HordesReleased * FMath::Max(Config.RepeatSeconds, 1.f);
}

bool FHordeTimer::Advance(float DeltaSeconds, bool bCounting, const FHordeConfig& Config)
{
	if (!bCounting || !Config.bEnabled)
	{
		return false;
	}
	CombatSeconds += FMath::Max(DeltaSeconds, 0.f);
	if (CombatSeconds < GetNextTriggerSeconds(Config))
	{
		return false;
	}
	++HordesReleased;
	return true;
}

namespace HordeRules
{
	bool IsCountingTime(ECodexGamePhase Phase, ECodexCombatMode Mode, bool bWorldAIPaused)
	{
		return Phase == ECodexGamePhase::WaveCombat && Mode == ECodexCombatMode::RealTime && !bWorldAIPaused;
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

	int32 GetHordeSize(const FHordeConfig& Config, int32 HordeIndex)
	{
		return FMath::Max(1, Config.Count + Config.CountIncreasePerRepeat * FMath::Max(HordeIndex, 0));
	}

	TArray<EEnemyArchetype> BuildComposition(const FHordeConfig& Config, int32 Total)
	{
		TArray<EEnemyArchetype> Result;
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
			Result.Init(EEnemyArchetype::FrostHound, Total);
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
				Result.Add(Config.Composition[Index].Type);
			}
		}
		return Result;
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
		if (Json.TryGetNumberField(TEXT("trigger_seconds"), Number))
		{
			Config.TriggerSeconds = FMath::Max(1.f, static_cast<float>(Number));
		}
		if (Json.TryGetBoolField(TEXT("repeats"), Bool))
		{
			Config.bRepeats = Bool;
		}
		if (Json.TryGetNumberField(TEXT("repeat_seconds"), Number))
		{
			Config.RepeatSeconds = FMath::Max(1.f, static_cast<float>(Number));
		}
		if (Json.TryGetNumberField(TEXT("count"), Number))
		{
			Config.Count = FMath::Max(1, FMath::RoundToInt(Number));
		}
		if (Json.TryGetNumberField(TEXT("count_increase_per_repeat"), Number))
		{
			Config.CountIncreasePerRepeat = FMath::Max(0, FMath::RoundToInt(Number));
		}
		const TSharedPtr<FJsonObject>* Mix = nullptr;
		if (Json.TryGetObjectField(TEXT("composition"), Mix) && Mix && Mix->IsValid())
		{
			TArray<FHordeMixEntry> Parsed;
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Mix)->Values)
			{
				FHordeMixEntry MixEntry;
				double Weight = 0.0;
				if (LevelJsonRules::ParseEnemyType(Entry.Key, MixEntry.Type) && Entry.Value.IsValid() && Entry.Value->TryGetNumber(Weight) && Weight > 0.0)
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

	FHordeConfig ResolveForLevel(const FHordeConfig& Defaults, bool bLevelHordeEnabled, const FString& LevelOverrideJson)
	{
		FHordeConfig Config = Defaults;
		if (!LevelOverrideJson.IsEmpty())
		{
			ApplyJsonText(LevelOverrideJson, Config);
		}
		Config.bEnabled = Config.bEnabled && bLevelHordeEnabled;
		return Config;
	}
}
