#pragma once

#include "CoreMinimal.h"

/**
 * One line of Saved/Telemetry/raw_runs/runs.jsonl — the run record the Wave Editor's analytics read (Godot archive
 * Scenes/movements/main.gd _record_telemetry / player.gd get_weapon_telemetry_summary; data/schemas/telemetry_run.schema.json
 * is older than the records, the editor reads the record below). Every run is recorded: tester_profile "HUMAN" for a
 * player, the bot's profile (CASUAL / NORMAL / VETERAN) for the playtest bot.
 */
struct CODEXTACTICS_API FWeaponWaveStat
{
	int32 Shots = 0;
	float Damage = 0.f;
};

/** Per-operative numbers of a run. */
struct CODEXTACTICS_API FMemberRunStats
{
	FString Name;
	/** Wave index -> weapon id (m16 / pistol / knife / ...) -> hits and damage (Godot weapon_stats_by_wave). */
	TMap<int32, TMap<FString, FWeaponWaveStat>> ByWave;
	int32 FirstPistolWave = -1;
	float FinalColdPct = 0.f;
	float ColdDamageTaken = 0.f;
	float ExtremeColdTimeSec = 0.f;
	int32 Medkits = 0;
	int32 CannedFood = 0;
	int32 Chocolate = 0;
	int32 Matches = 0;
	int32 AmmoM16 = 0;
	int32 AmmoPistol = 0;
};

/** Turret / barricade / mine results (Godot deployables_combat_stats + the survivors at the end). */
struct CODEXTACTICS_API FDeployableRunStats
{
	int32 Kills = 0;
	float Damage = 0.f;
	int32 Survived = 0;
	float AvgHpPct = 0.f;
};

struct CODEXTACTICS_API FRunRecord
{
	FString SessionId;
	FString TimestampUtc;
	FString TesterProfile = TEXT("HUMAN");
	FString LevelId;
	/** VICTORY or DEFEAT. */
	FString Result;
	int32 WavesCleared = 0;
	int32 TotalWaves = 0;
	float RunDurationSec = 0.f;
	bool bStandardLootFound = true;
	bool bPuzzleSecretFound = false;
	TArray<FMemberRunStats> Members;
	FDeployableRunStats Turrets;
	FDeployableRunStats Barricades;
	FDeployableRunStats Mines;
	/** Defeat only: the wave lost and HP_DEPLETED / FREEZING_FATIGUE. */
	int32 FailedWave = 0;
	FString DeathCause;
};

namespace RunTelemetryRules
{
	/** Godot _record_weapon_shot: a hit with Damage in Wave (the first pistol wave is remembered). */
	CODEXTACTICS_API void RecordShot(FMemberRunStats& Stats, int32 Wave, const FString& WeaponId, float Damage);

	/** Godot get_weapon_telemetry_summary (totals per weapon, dominant weapon, by_wave). */
	CODEXTACTICS_API TSharedRef<class FJsonObject> BuildWeaponSummary(const FMemberRunStats& Stats);

	/** The whole record as one JSON line (no newline). */
	CODEXTACTICS_API FString BuildRunJson(const FRunRecord& Record);

	/** Godot: waves_cleared = the wave reached on a victory, the one before the lost wave on a defeat. */
	CODEXTACTICS_API int32 WavesCleared(bool bVictory, int32 CurrentWave);
}
