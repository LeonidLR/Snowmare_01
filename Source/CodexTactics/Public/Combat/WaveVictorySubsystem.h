#pragma once

#include "CoreMinimal.h"
#include "Combat/KillStatsRules.h"
#include "GameFlow/GameFlowTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "WaveVictorySubsystem.generated.h"

class AOperativeCharacter;

/**
 * What happens around a cleared wave: the squad kill statistics, the victory panel's «next wave» button, and the
 * post-combat sequence after the last wave (enemies removed, the squad back at its preparation spots, the commander
 * leads, surviving turrets / barricades / mines back into the supply with the engineer's feed line).
 * Godot reference: Scenes/movements/main.gd register_enemy_kill, _format_kill_stats_bbcode, _on_wave_cleared,
 * _on_next_wave_pressed, _start_post_combat_sequence, _auto_recover_all_deployables, _end_cutscene_and_start_pause
 * (initial_prep_station).
 */
UCLASS()
class CODEXTACTICS_API UWaveVictorySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** An enemy died; Source is its last attacker (Godot last_attacker_source). */
	void RegisterEnemyKill(EEnemyArchetype Type, const FString& Source);

	const FSquadKillStats& GetKillStats() const { return KillStats; }

	/** Victory panel statistics text. */
	FString GetKillStatsText() const { return KillStatsRules::Format(KillStats); }

	/** Victory panel title / subtitle / button for the cleared wave (last wave: full victory). */
	FString GetVictoryTitle() const;
	FString GetVictorySubtitle() const;
	FString GetNextButtonText() const;

	/** The victory panel's main button (Godot _on_next_wave_pressed): next wave's rest, or the post-combat sequence. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Victory")
	void ContinueAfterWave();

	/** Where Member stood when the preparation began (Godot initial_prep_station; its spawn spot before that). */
	FVector GetPrepStation(const AOperativeCharacter* Member) const;

private:
	UFUNCTION()
	void HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode);

	void RecordPrepStations(bool bOverwrite);
	void StartPostCombatSequence();
	void RecoverDeployables();

	FSquadKillStats KillStats;
	TMap<TWeakObjectPtr<AOperativeCharacter>, FVector> PrepStations;
	ECodexGamePhase LastPhase = ECodexGamePhase::Exploration;
};
