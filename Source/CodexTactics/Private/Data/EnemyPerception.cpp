#include "Data/EnemyPerception.h"

#include "CodexTactics.h"
#include "Data/LevelJsonRules.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace EnemyPerception
{
	// Jev AI coach knobs (Scripts/Tools/jev_ai_coach.py --stealth, ai_tuning.json / -dpcvars=); the defaults keep the data.
	static TAutoConsoleVariable<float> CVarSightScale(TEXT("Codex.Perception.SightRangeScale"), 1.f, TEXT("Multiplier of every enemy's sight range"));
	static TAutoConsoleVariable<float> CVarFovScale(TEXT("Codex.Perception.FovScale"), 1.f, TEXT("Multiplier of every enemy's field-of-view half-angle"));
	static TAutoConsoleVariable<float> CVarProneScale(TEXT("Codex.Perception.ProneVisibilityScale"), 1.f,
		TEXT("Multiplier of the prone visibility (how far a prone operative is seen)"));
	static TAutoConsoleVariable<float> CVarHearingScale(TEXT("Codex.Perception.HearingScale"), 1.f,
		TEXT("Multiplier of every hearing radius (footsteps, gunshots, grenades)"));
	static TAutoConsoleVariable<float> CVarSmellScale(TEXT("Codex.Perception.SmellScale"), 1.f, TEXT("Multiplier of the hounds' smell radius"));
	static TAutoConsoleVariable<float> CVarDetectScale(TEXT("Codex.Perception.TimeToDetectScale"), 1.f, TEXT("Multiplier of the sight time-to-detect"));
	static TAutoConsoleVariable<float> CVarSearchSeconds(TEXT("Codex.Patrol.SearchSeconds"), -1.f,
		TEXT("Patrol search time after a trap, s (-1: level JSON / enemy_perception.json)"));
	static TAutoConsoleVariable<float> CVarSearchRadius(TEXT("Codex.Patrol.SearchRadius"), -1.f, TEXT("Patrol search sweep radius, cm (-1: data)"));
	static TAutoConsoleVariable<float> CVarSearchSpeed(TEXT("Codex.Patrol.SearchSpeedScale"), 1.f, TEXT("Multiplier of the search pace multiplier"));

	static TMap<EEnemyArchetype, FEnemyPerceptionParams> Overrides;
	static FPatrolSearchParams CurrentSearch;

	static void ReadMeters(const FJsonObject& Json, const TCHAR* Key, float& OutCm)
	{
		double Value = 0.0;
		if (Json.TryGetNumberField(Key, Value))
		{
			OutCm = FMath::Max(static_cast<float>(Value) * 100.f, 0.f);
		}
	}

	static void ReadNumber(const FJsonObject& Json, const TCHAR* Key, float& Out)
	{
		double Value = 0.0;
		if (Json.TryGetNumberField(Key, Value))
		{
			Out = FMath::Max(static_cast<float>(Value), 0.f);
		}
	}
}

FString EnemyPerception::GetDefaultPath()
{
	return FPaths::ProjectContentDir() / TEXT("Data/AI/enemy_perception.json");
}

FEnemyPerceptionParams EnemyPerception::Get(EEnemyArchetype Archetype)
{
	const FEnemyPerceptionParams* Found = Overrides.Find(Archetype);
	return PerceptionRules::Sanitize(Archetype, ApplyTuning(Found ? *Found : PerceptionRules::GetArchetypeDefaults(Archetype), GetTuning()));
}

FPerceptionTuning EnemyPerception::GetTuning()
{
	FPerceptionTuning Tuning;
	Tuning.SightRangeScale = CVarSightScale.GetValueOnGameThread();
	Tuning.FovScale = CVarFovScale.GetValueOnGameThread();
	Tuning.ProneVisibilityScale = CVarProneScale.GetValueOnGameThread();
	Tuning.HearingScale = CVarHearingScale.GetValueOnGameThread();
	Tuning.SmellScale = CVarSmellScale.GetValueOnGameThread();
	Tuning.TimeToDetectScale = CVarDetectScale.GetValueOnGameThread();
	Tuning.SearchSeconds = CVarSearchSeconds.GetValueOnGameThread();
	Tuning.SearchRadiusCm = CVarSearchRadius.GetValueOnGameThread();
	Tuning.SearchSpeedScale = CVarSearchSpeed.GetValueOnGameThread();
	return Tuning;
}

FEnemyPerceptionParams EnemyPerception::ApplyTuning(const FEnemyPerceptionParams& Params, const FPerceptionTuning& Tuning)
{
	FEnemyPerceptionParams Out = Params;
	Out.SightRangeCm *= FMath::Max(Tuning.SightRangeScale, 0.f);
	Out.SightHalfAngleDeg = FMath::Clamp(Out.SightHalfAngleDeg * FMath::Max(Tuning.FovScale, 0.f), 0.f, 180.f);
	Out.ProneVisibility *= FMath::Max(Tuning.ProneVisibilityScale, 0.f);
	const float Hearing = FMath::Max(Tuning.HearingScale, 0.f);
	Out.HearWalkCm *= Hearing;
	Out.HearRunCm *= Hearing;
	Out.HearCrouchWalkCm *= Hearing;
	Out.HearCrawlCm *= Hearing;
	Out.HearGunshotCm *= Hearing;
	Out.HearExplosionCm *= Hearing;
	Out.SmellRadiusCm *= FMath::Max(Tuning.SmellScale, 0.f);
	Out.TimeToDetectSeconds *= FMath::Max(Tuning.TimeToDetectScale, 0.f);
	return Out;
}

FPatrolSearchParams EnemyPerception::ApplyTuning(const FPatrolSearchParams& Search, const FPerceptionTuning& Tuning)
{
	FPatrolSearchParams Out = Search;
	if (Tuning.SearchSeconds >= 0.f)
	{
		Out.DurationSeconds = Tuning.SearchSeconds;
	}
	if (Tuning.SearchRadiusCm >= 0.f)
	{
		Out.SweepRadiusCm = Tuning.SearchRadiusCm;
	}
	Out.SpeedMultiplier *= FMath::Max(Tuning.SearchSpeedScale, 0.f);
	return Out;
}

void EnemyPerception::Set(EEnemyArchetype Archetype, const FEnemyPerceptionParams& Params)
{
	Overrides.Add(Archetype, PerceptionRules::Sanitize(Archetype, Params));
}

FPatrolSearchParams EnemyPerception::GetSearch()
{
	return ApplyTuning(CurrentSearch, GetTuning());
}

void EnemyPerception::SetSearch(const FPatrolSearchParams& Search)
{
	CurrentSearch = Search;
}

void EnemyPerception::ResetToDefaults()
{
	Overrides.Reset();
	CurrentSearch = FPatrolSearchParams();
}

FEnemyPerceptionParams EnemyPerception::ParamsFromJson(const FJsonObject& Json, const FEnemyPerceptionParams& Base)
{
	FEnemyPerceptionParams Out = Base;
	ReadMeters(Json, TEXT("sight_range_m"), Out.SightRangeCm);
	ReadNumber(Json, TEXT("sight_half_angle_deg"), Out.SightHalfAngleDeg);
	ReadNumber(Json, TEXT("visibility_standing"), Out.StandingVisibility);
	ReadNumber(Json, TEXT("visibility_crouching"), Out.CrouchingVisibility);
	ReadNumber(Json, TEXT("visibility_prone"), Out.ProneVisibility);
	ReadMeters(Json, TEXT("proximity_m"), Out.ProximityCm);
	ReadNumber(Json, TEXT("time_to_detect_seconds"), Out.TimeToDetectSeconds);
	ReadNumber(Json, TEXT("suspicion_decay_per_second"), Out.SuspicionDecayPerSecond);
	ReadMeters(Json, TEXT("hear_walk_m"), Out.HearWalkCm);
	ReadMeters(Json, TEXT("hear_run_m"), Out.HearRunCm);
	ReadMeters(Json, TEXT("hear_crouch_walk_m"), Out.HearCrouchWalkCm);
	ReadMeters(Json, TEXT("hear_crawl_m"), Out.HearCrawlCm);
	ReadMeters(Json, TEXT("hear_gunshot_m"), Out.HearGunshotCm);
	ReadMeters(Json, TEXT("hear_explosion_m"), Out.HearExplosionCm);
	ReadMeters(Json, TEXT("smell_radius_m"), Out.SmellRadiusCm);
	Out.SightHalfAngleDeg = FMath::Clamp(Out.SightHalfAngleDeg, 0.f, 180.f);
	return Out;
}

FPatrolSearchParams EnemyPerception::SearchFromJson(const FJsonObject& Json, const FPatrolSearchParams& Base)
{
	FPatrolSearchParams Out = Base;
	ReadNumber(Json, TEXT("duration_seconds"), Out.DurationSeconds);
	ReadMeters(Json, TEXT("sweep_radius_m"), Out.SweepRadiusCm);
	ReadNumber(Json, TEXT("speed_multiplier"), Out.SpeedMultiplier);
	ReadNumber(Json, TEXT("perception_multiplier"), Out.PerceptionMultiplier);
	ReadNumber(Json, TEXT("look_around_seconds"), Out.LookAroundSeconds);
	ReadMeters(Json, TEXT("arrive_m"), Out.ArriveCm);
	ReadNumber(Json, TEXT("leg_timeout_seconds"), Out.LegTimeoutSeconds);
	return Out;
}

bool EnemyPerception::ApplyJson(const FString& Text)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		return false;
	}
	Overrides.Reset();
	const TSharedPtr<FJsonObject>* Search = nullptr;
	CurrentSearch = Root->TryGetObjectField(TEXT("search"), Search) && Search && Search->IsValid()
		? SearchFromJson(**Search) : FPatrolSearchParams();
	const TSharedPtr<FJsonObject>* Archetypes = nullptr;
	if (Root->TryGetObjectField(TEXT("archetypes"), Archetypes) && Archetypes && Archetypes->IsValid())
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Archetypes)->Values)
		{
			EEnemyArchetype Archetype;
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!LevelJsonRules::ParseEnemyType(Entry.Key, Archetype) || !Entry.Value.IsValid() || !Entry.Value->TryGetObject(Object) || !Object)
			{
				UE_LOG(LogCodexTactics, Warning, TEXT("Enemy perception: archetype '%s' unknown, skipped"), *Entry.Key);
				continue;
			}
			Set(Archetype, ParamsFromJson(**Object, PerceptionRules::GetArchetypeDefaults(Archetype)));
		}
	}
	return true;
}

bool EnemyPerception::ApplyFile(const FString& Path)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !ApplyJson(Text))
	{
		return false;
	}
	UE_LOG(LogCodexTactics, Display, TEXT("Enemy perception from %s: %d archetypes, search %.0f s"), *Path, Overrides.Num(), CurrentSearch.DurationSeconds);
	return true;
}

namespace EnemyPerception
{
	static void DumpCommand(const TArray<FString>& Args)
	{
		for (const EEnemyArchetype Archetype : { EEnemyArchetype::FrostHound, EEnemyArchetype::Marksman, EEnemyArchetype::Spitter,
			EEnemyArchetype::Brute, EEnemyArchetype::Cutter, EEnemyArchetype::Frostbitten })
		{
			const FEnemyPerceptionParams P = Get(Archetype);
			UE_LOG(LogCodexTactics, Display,
				TEXT("Perception %s: sight %.0f m / %.0f deg (crouch x%.2f, prone x%.2f, detect %.1f s), hear walk %.0f run %.0f crouch %.0f crawl %.0f gunshot %.0f grenade %.0f m, smell %.0f m"),
				*UEnum::GetValueAsString(Archetype), P.SightRangeCm / 100.f, P.SightHalfAngleDeg, P.CrouchingVisibility, P.ProneVisibility,
				P.TimeToDetectSeconds, P.HearWalkCm / 100.f, P.HearRunCm / 100.f, P.HearCrouchWalkCm / 100.f, P.HearCrawlCm / 100.f,
				P.HearGunshotCm / 100.f, P.HearExplosionCm / 100.f, P.SmellRadiusCm / 100.f);
		}
		const FPatrolSearchParams& S = CurrentSearch;
		UE_LOG(LogCodexTactics, Display, TEXT("Patrol search: %.0f s, sweep %.0f m, speed x%.2f, perception x%.2f"), S.DurationSeconds,
			S.SweepRadiusCm / 100.f, S.SpeedMultiplier, S.PerceptionMultiplier);
	}

	static FAutoConsoleCommand DumpPerception(TEXT("CodexTactics.DumpPerception"),
		TEXT("Logs the enemy perception table and the patrol search parameters in force (enemy_perception.json)."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&DumpCommand));
}
