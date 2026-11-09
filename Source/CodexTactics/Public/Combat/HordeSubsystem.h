#pragma once

#include "CoreMinimal.h"
#include "Combat/HordeRules.h"
#include "GameFlow/GameFlowTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "HordeSubsystem.generated.h"

class AEnemyCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnHordeReleased, int32, HordeIndex, int32, EnemyCount, FVector, Location);

/**
 * The horde (UE-only; no Godot counterpart). Horde #12 (game designer 2026-11-02 / 11-03 / 11-08; the ONLY trigger since 2026-11-09): it
 * comes after "horde_after_firefights" (default 10) firefights IN A ROW on the level - UFirefightSubsystem counts them and calls
 * RequestFirefightHorde: the HQ warning at once, the horde Firefight.WarningLeadSeconds later (running game time: the tactical pause /
 * turn-based / a dialogue stop the countdown, HordeRules::IsCountdownRunning; in exploration the fight starts with it), then the horde intro
 * (UHordeIntroSubsystem::BeginExternalHorde: the «HORDE INCOMING» alert, the flyover once per level). Game designer play-test 2026-11-09:
 * the group appears FAR away - a reachable navmesh point out of the squad's sight ~300 m from its centre (firefight_spawn_distance_m 300 +-
 * firefight_spawn_spread_m 40), at a random bearing at least firefight_bearing_change_deg (90) off the previous horde's; a map with no point
 * that far gives the farthest ones (logged: "[Horde] ... no point 260-340 m: the farthest"); clustered within 4 m. WHO comes: on a level
 * whose waves are not played as scripted waves (an ambush-start level such as the Depot, HordeRules::ShouldWavesFeedHorde) the level's
 * waves in order (horde 1 = wave 1, ..., then the last repeats; max_simultaneous_enemies = the most alive at once, the rest come at the
 * horde's spawn point as they fall; wave_modifiers + custom_stats apply; a SPITTER entry comes as the MILITARY_ZOMBIE), else the "horde"
 * block / horde.json (count + composition, 8: frost hounds + frostbitten by default). They know where the squad is
 * (AEnemyCharacter::bKnowsSquadPosition: no patrol, no perception, no fall-back) and rush it. They join the fight
 * (UWaveSubsystem::SpawnEnemy; the held-back ones through UWaveSubsystem::SetExternalPendingSpawns), so it ends only when all are dead.
 * The dev command spawns 25-45 m away (min_distance_m / max_distance_m).
 * HUD: "HORDE!" banner + an arrow towards it (ACodexTacticsHUD::DrawHordeWarning), the radio line, OnHordeReleased (hook for audio / VFX)
 * and the optional "warning_sound". Data: Content/Data/AI/horde.json + the level JSON "horde_enabled" / "horde_after_firefights" / "horde"
 * (HordeRules::ResolveForLevel); console Codex.Horde.Enabled / AfterFirefights / FirefightWarningSeconds, command CodexTactics.Horde.Release
 * (a direct debug release, no countdown). "horde_enabled": false = never.
 * Removed 2026-11-09 (game designer: only the firefight streak summons the horde): the 240 s real-time-fight clock (FHordeTimer,
 * "trigger_seconds" / "repeats", Codex.Horde.TriggerSeconds / RepeatSeconds). Scripted stage waves are not the horde and are unchanged.
 */
UCLASS()
class CODEXTACTICS_API UHordeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** The config in force (defaults + level + console overrides). */
	const FHordeConfig& GetConfig() const { return Config; }

	/** Hordes released on this level (the firefight hordes and the dev command's). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Horde")
	int32 GetHordesReleased() const { return HordesReleased; }

	/** The firefight horde's countdown ran on the last tick (false while the pause / turn-based / a dialogue holds it). */
	bool IsCountdownRunning() const { return bCountdownRunning; }

	/**
	 * Releases a horde now with no warning (the dev command CodexTactics.Horde.Release, smokes): finds the spawn point and spawns the group
	 * (it joins the running fight; no fight is started). Returns how many enemies appeared (0: no spawn point / no squad).
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Horde")
	int32 ReleaseHorde();

	/** The HUD warning while it lasts: where the horde appeared, how many, seconds left. */
	bool GetActiveWarning(FVector& OutLocation, int32& OutCount, float& OutSecondsLeft) const;

	/** Members of the last horde (smokes). */
	const TArray<TWeakObjectPtr<AEnemyCharacter>>& GetLastHorde() const { return LastHorde; }

	/** Where the last horde appeared and the squad's centre then (smokes / logs). */
	FVector GetLastSpawnPoint() const { return LastSpawnPoint; }
	FVector GetLastSquadCentre() const { return LastSquadCentre; }
	bool WasLastSpawnHidden() const { return bLastSpawnHidden; }

	/** The plan of the last horde (its members, cap, source wave) and how many of it have come so far / are still held back. */
	const FHordePlan& GetActivePlan() const { return ActivePlan; }
	int32 GetActiveSpawned() const { return ActiveSpawned; }
	int32 GetActiveQueued() const { return FMath::Max(0, ActivePlan.Members.Num() - ActiveSpawned); }

	/**
	 * The plan of the level's next firefight horde (FirefightHordesReleased + 1): the level wave it maps to when the waves feed the horde
	 * (HordeRules::ShouldWavesFeedHorde), else the "horde" block / horde.json with the current wave's modifiers (apply_wave_modifiers).
	 */
	FHordePlan PlanNextHorde() const;

	/** How the last firefight horde's spawn point was chosen, its bearing from the squad, the search's cost. */
	EHordeFarPick GetLastSpawnPick() const { return LastSpawnPick; }
	/** The last firefight horde came near the previous one's bearing: no reachable out-of-sight point off it existed. */
	bool WasLastSpawnBearingRelaxed() const { return bLastSpawnBearingRelaxed; }
	float GetLastSpawnBearingDeg() const { return LastSpawnBearingDeg; }
	double GetLastSpawnSearchMs() const { return LastSpawnSearchMs; }

	/** A horde member came within ArrivalRangeCm of the squad's centre (first time): its label, seconds after its spawn, spawn distance. */
	struct FArrival
	{
		FString Label;
		float Seconds = 0.f;
		float SpawnDistanceCm = 0.f;
		int32 Horde = 0;
	};
	const TArray<FArrival>& GetArrivals() const { return Arrivals; }
	/** "Arrived" = within this of the squad's centre (log + smokes). */
	static constexpr float ArrivalRangeCm = 1500.f;

	/** Re-reads the config for the current level (start of every fight, a firefight horde request; smokes after changing the console values). */
	void RefreshConfig();

	/** Smokes: horde.json keys applied over the level's config at every RefreshConfig (empty = none); refreshes now. */
	void SetDevOverrideJson(const FString& Json) { DevOverrideJson = Json; RefreshConfig(); }

	/**
	 * Horde #12: the firefight streak reached the level's threshold (UFirefightSubsystem). Posts the HQ warning now and releases the horde
	 * Firefight.WarningLeadSeconds of running game time later (the tactical pause / turn-based / a dialogue stop the countdown; in
	 * exploration the fight starts with it). False when the horde is off on this level or one is already pending.
	 */
	bool RequestFirefightHorde();

	/** A firefight horde is announced and not yet out. */
	bool IsFirefightHordePending() const { return FirefightHordeCountdown >= 0.f; }
	/** Seconds of the countdown left (-1: none pending). */
	float GetFirefightHordeSecondsLeft() const { return FirefightHordeCountdown; }
	/** Firefight hordes announced / released on this level. */
	int32 GetFirefightHordesAnnounced() const { return FirefightHordesAnnounced; }
	int32 GetFirefightHordesReleased() const { return FirefightHordesReleased; }

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Horde")
	FOnHordeReleased OnHordeReleased;

private:
	UFUNCTION()
	void HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode);

	/** The living operatives' mean position (false without one). */
	bool GetSquadCentre(FVector& OutCentre) const;
	/** The dev command's near spawn: samples the 25-45 m band around Centre (navmesh, path, squad sight) and picks one. */
	bool FindSpawnPoint(const FVector& Centre, FVector& OutPoint, bool& bOutHidden) const;
	/**
	 * The firefight horde's far spawn (~300 m, HordeRules::PickFarSpawnPoint): FirefightSpawnBearings directions, along each the farthest
	 * reachable navmesh point of HordeRules::FarProbeRadii (an out-of-sight one preferred). bUsePreviousBearing: keep off the last horde's bearing.
	 */
	bool FindFarSpawnPoint(const FVector& Centre, bool bUsePreviousBearing, FVector& OutPoint, bool& bOutHidden, FHordeFarPick& OutPick, float& OutBearing);
	/** Some living operative sees Point (Visibility channel, pawns ignored; bWithinSightRange: and it lies within his sight range). */
	bool IsVisibleToSquad(const FVector& Point, bool bWithinSightRange = false) const;
	/** Spawns the first members of Plan at Point (up to its cap; the rest are held back); returns how many enemies appeared. */
	int32 SpawnHorde(const FHordePlan& Plan, const FVector& Point, const FVector& Centre, bool bHidden);
	/** Spawns ActivePlan's next member near Point; nullptr when it could not. */
	AEnemyCharacter* SpawnNextMember(const FVector& Point, const FRotator& Facing);
	/** The held-back members of the active horde come as the living fall (real-time fight only); arrivals are logged. */
	void TickActiveHorde(float DeltaTime);
	/** Drops the held-back members (the fight is over). */
	void DropQueue(const TCHAR* Why);
	/** Horde #12: counts the firefight horde's warning down (running game time); spawns it at 0. */
	void TickFirefightHorde(float DeltaTime);
	/** Horde #12: the firefight horde now (starts the fight from exploration); false = try again shortly. */
	bool ReleaseFirefightHorde();
	FString DevOverrideJson;
	float FirefightHordeCountdown = -1.f;
	float FirefightRetryCooldown = 0.f;
	int32 FirefightHordesAnnounced = 0;
	int32 FirefightHordesReleased = 0;

	FHordeConfig Config;
	int32 HordesReleased = 0;
	ECodexGamePhase LastPhase = ECodexGamePhase::Exploration;
	bool bCountdownRunning = false;
	double WarningUntil = -1.0;
	int32 WarningCount = 0;
	FVector LastSpawnPoint = FVector::ZeroVector;
	FVector LastSquadCentre = FVector::ZeroVector;
	bool bLastSpawnHidden = false;
	TArray<TWeakObjectPtr<AEnemyCharacter>> LastHorde;

	/** The active horde: its plan, members spawned so far, where they come from. */
	FHordePlan ActivePlan;
	int32 ActiveSpawned = 0;
	int32 ActiveHordeNumber = 0;
	FVector ActiveSpawnPoint = FVector::ZeroVector;
	float ReinforceCooldown = 0.f;
	float ArrivalCheckCooldown = 0.f;
	/** Spawn time (game seconds) and spawn distance of the members not yet arrived. */
	TMap<TWeakObjectPtr<AEnemyCharacter>, FVector2D> PendingArrivals;
	TArray<FArrival> Arrivals;
	/** The previous firefight horde's bearing from the squad (the next one keeps off it). */
	bool bHasLastFirefightBearing = false;
	float LastFirefightBearingDeg = 0.f;
	EHordeFarPick LastSpawnPick = EHordeFarPick::None;
	bool bLastSpawnBearingRelaxed = false;
	float LastSpawnBearingDeg = 0.f;
	double LastSpawnSearchMs = 0.0;
	FRandomStream SpawnRandom{ static_cast<int32>(FPlatformTime::Cycles()) };
};
