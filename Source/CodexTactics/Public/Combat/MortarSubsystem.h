#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MortarSubsystem.generated.h"

class AMortarNestActor;
class AOperativeCharacter;
struct FMortarNestPlacement;

/**
 * The mortar nests of a world (2026-11-09; docs/systems/weapons.md "Mortar nests"): spawns them from the level JSON "mortar_nests" (no map
 * edits; a PMC nest with "crew" gets its own commanded PMC team at its places), finds the spotter for a point (the spotter rule, the same for
 * the squad and the PMC: a living man of the nest's side who sees it now - UTacticalSightSubsystem::CanOperativeSee), and carries the player's
 * console orders: CodexTactics.MortarList / MortarMan [nest] [squad indices] / MortarFire [nest] <x y [z] | cursor> / MortarCease [nest]; dev:
 * CodexTactics.MortarCrewPmc [nest] (a PMC crew at a nest).
 */
UCLASS()
class CODEXTACTICS_API UMortarSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	static UMortarSubsystem* Get(const UWorld* World);

	void RegisterNest(AMortarNestActor* Nest);
	void UnregisterNest(AMortarNestActor* Nest);

	/** Every nest (TeamId < 0) or one side's. */
	TArray<AMortarNestActor*> GetNests(int32 TeamId = -1) const;

	/** The nest Man crews (null = none). */
	AMortarNestActor* FindNestOf(const AOperativeCharacter* Man) const;
	static bool IsCrewing(const UWorld* World, const AOperativeCharacter* Man);

	/** A living man of TeamId's side who sees Point (+ the data's spot height) now; null = nobody (no spotter, no fire). */
	AOperativeCharacter* FindSpotter(int32 TeamId, const FVector& Point) const;

	/**
	 * Spawns Count PMC fighters at Nest's places as its crew - a new commanded PMC team (TeamId < 0: a free id) using pmc_commander.json; the
	 * nest takes their side. Returns how many took a place.
	 */
	int32 SpawnPmcCrew(AMortarNestActor* Nest, int32 Count, int32 TeamId = -1);

	/** A PMC team id no squad uses yet (>= 1). */
	int32 FreePmcTeamId() const;

	/** Spawns one nest (+ its PMC crew); the level JSON's path. */
	AMortarNestActor* SpawnNest(const FMortarNestPlacement& Placement);

	/** Spawns the nests of the level JSON file in use (the game mode's resolution); returns how many. */
	int32 SpawnFromLevelJson();

private:
	TArray<TWeakObjectPtr<AMortarNestActor>> Nests;
	bool bSpawnedFromLevel = false;
};
