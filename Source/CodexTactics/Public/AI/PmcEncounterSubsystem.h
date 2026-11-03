#pragma once

#include "CoreMinimal.h"
#include "AI/PmcEncounterRules.h"
#include "Subsystems/WorldSubsystem.h"
#include "PmcEncounterSubsystem.generated.h"

/** One encounter squad that came into the level (smokes, the log). */
struct FPmcEncounterSpawn
{
	int32 TeamId = INDEX_NONE;
	FPmcEncounterSpec Spec;
	FVector Spot = FVector::ZeroVector;
	int32 Level = 0;
	int32 Seed = 0;
	double Time = 0.0;
};

/**
 * PMC squads as a normal encounter (user request 2026-11-03: "I want to play against the PMC squads myself";
 * docs/systems/pmc-squad-ai.md 7; UE-only). Reads the level JSON "pmc_squads" (FLevelCombatConfig::PmcSquads) at the level start
 * and brings each squad in after its delay_s: a seeded navmesh spot min..max from the player's squad on any level (ground, roofs,
 * walkways), out of the squad's sight (UCombatPressureSubsystem::IsVisibleToSquad), with a full navmesh path to the squad; the
 * fighters (UPmcSquadSubsystem::SpawnFighter, a new hostile team each) get the entry's commander weights and posture
 * (UPmcSquadSubsystem::SetTeamPosture). Also the console command CodexTactics.PmcSpawn (PIE testing, a normal session).
 */
UCLASS()
class CODEXTACTICS_API UPmcEncounterSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	static UPmcEncounterSubsystem* Get(const UWorld* World);

	/**
	 * Brings one squad in now (the delay is ignored). Seed 0 = a running seed. Returns the new team id, INDEX_NONE (OutWhy) when no
	 * spot fits / nothing could spawn.
	 */
	int32 SpawnEncounter(const FPmcEncounterSpec& Spec, int32 Seed = 0, FString* OutWhy = nullptr);

	/** The squads brought in so far. */
	const TArray<FPmcEncounterSpawn>& GetSpawned() const { return Spawned; }
	/** The level's squads still waiting for their delay. */
	int32 GetPendingCount() const { return Pending.Num(); }
	/** The delay of the I-th waiting squad (seconds on GetClock), -1 when there is none. */
	float GetPendingDueAt(int32 Index) const { return Pending.IsValidIndex(Index) ? Pending[Index].DueAt : -1.f; }
	/** Drops the level's squads still waiting (smokes: one encounter at a time). */
	void CancelPending() { Pending.Reset(); }
	/** Seconds since the level data was read (the delays' clock). */
	float GetClock() const { return Clock; }

	/** Planar spot candidates tried per spawn, and the navmesh height levels probed (ground + 3 m steps). */
	static constexpr int32 MaxCandidates = 48;
	static constexpr int32 MaxLevels = 3;
	/** A squad with no fitting spot tries again this much later (at most RetryLimit times). */
	static constexpr float RetrySeconds = 10.f;
	static constexpr int32 RetryLimit = 3;

private:
	struct FPending
	{
		FPmcEncounterSpec Spec;
		int32 Index = 0;
		float DueAt = 0.f;
		int32 Retries = 0;
	};

	void ReadConfig();

	bool bConfigRead = false;
	float Clock = 0.f;
	int32 SeedCounter = 0;
	TArray<FPending> Pending;
	TArray<FPmcEncounterSpawn> Spawned;
};
