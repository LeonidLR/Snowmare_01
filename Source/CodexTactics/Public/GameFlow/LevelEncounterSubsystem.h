#pragma once

#include "CoreMinimal.h"
#include "Data/WaveConfigTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "LevelEncounterSubsystem.generated.h"

class AActor;

/** What started an ambush fight (logs, smokes). */
UENUM(BlueprintType)
enum class EAmbushTrigger : uint8
{
	/** The player ordered an attack on an enemy (Ctrl + click). */
	AttackOrder,
	/** An enemy took damage from the squad. */
	EnemyDamaged,
	/** A patrol detected the squad (sight / hearing / smell) or was alerted by its partner. */
	PatrolDetection,
	/** An Aggressive operative opened fire on an enemy it saw (fire posture, user request 2026-10-06). */
	SquadAutoFire
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAmbushCombatStarted, EAmbushTrigger, Trigger);

/**
 * Per-level encounter settings (user request 2026-10-06, UE-only).
 * Combat start: on an ambush level (level JSON "combat_start": "ambush", or "auto" — the default — on a map that holds
 * patrols: an APatrolRouteActor or an enemy with a route / escort leader at load) the player never presses «Начать бой»
 * (the main menu hides it): the real-time fight (WaveCombat / RealTime of wave 1, no cutscene, no preparation; the
 * level's placed enemies are the wave) starts once — when the squad attacks / hurts an enemy or an enemy on patrol
 * detects the squad. Wave maps without patrols keep the classic flow. Overrides: -CombatStart=auto|ambush|button on the
 * command line, console CodexTactics.CombatStart. Also resolves the patrol search time (level "patrol_search_seconds",
 * else enemy_perception.json).
 */
UCLASS()
class CODEXTACTICS_API ULevelEncounterSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** The configured mode: console / command-line override, else the level JSON, else Auto. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Encounter")
	ECombatStartMode GetCombatStartMode() const;

	/** The fight of this level starts by ambush («Начать бой» hidden). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Encounter")
	bool IsAmbushCombatStart() const;

	/** The map held patrols when it loaded (Auto resolves to ambush). */
	bool LevelHasPatrols() const { return bLevelHasPatrols; }

	/**
	 * A hostile contact happened (see EAmbushTrigger): on an ambush level still in exploration the fight starts now
	 * (once). True when this call started it.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Encounter")
	bool NotifyHostileContact(EAmbushTrigger Trigger, AActor* Source);

	/** NotifyHostileContact on World's subsystem (no-op without one). */
	static bool NotifyHostileContactIn(UWorld* World, EAmbushTrigger Trigger, AActor* Source);

	/** Forces a mode for this world (console, smokes); Auto clears nothing — use ClearCombatStartOverride. */
	void SetCombatStartOverride(ECombatStartMode Mode);
	void ClearCombatStartOverride() { bHasOverride = false; }

	/** Seconds a patrol searches after a trap: the level's patrol_search_seconds, else enemy_perception.json. */
	float GetPatrolSearchSeconds() const;
	/** GetPatrolSearchSeconds of World (the global value without a subsystem). */
	static float GetPatrolSearchSecondsIn(const UWorld* World);
	/** Smokes: replaces the search time (<= 0 clears it). */
	void SetPatrolSearchSecondsOverride(float Seconds) { SearchSecondsOverride = Seconds; }

	/** How many ambush fights started in this world (smokes). */
	int32 GetAmbushStarts() const { return AmbushStarts; }
	EAmbushTrigger GetLastTrigger() const { return LastTrigger; }

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|Encounter")
	FOnAmbushCombatStarted OnAmbushCombatStarted;

private:
	const FLevelCombatConfig* GetLevelConfig() const;

	bool bLevelHasPatrols = false;
	bool bHasOverride = false;
	ECombatStartMode ModeOverride = ECombatStartMode::Auto;
	float SearchSecondsOverride = -1.f;
	int32 AmbushStarts = 0;
	EAmbushTrigger LastTrigger = EAmbushTrigger::AttackOrder;
};
