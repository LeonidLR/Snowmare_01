#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Data/CombatTypes.h"
#include "GameFlow/GameFlowTypes.h"
#include "Telemetry/RunTelemetryRules.h"
#include "RunTelemetrySubsystem.generated.h"

class AEnemyCharacter;
class AOperativeCharacter;

/**
 * Records every run into Saved/Telemetry/raw_runs/runs.jsonl for the Wave Editor's analytics (Godot archive
 * Scenes/movements/main.gd _record_telemetry): DEFEAT when the game is over, VICTORY when the last wave's post-combat
 * starts. Collects the operatives' hits per wave and weapon (AOperativeCharacter::ShootAtTarget), turret / barricade /
 * mine damage and kills (from the enemies' health events by attacker source), the cold counters and the remaining
 * supplies. Off with -NoTelemetry (the smokes pass it).
 */
UCLASS()
class CODEXTACTICS_API URunTelemetrySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	/** A hit of an operative (Godot _record_weapon_shot). */
	void RecordWeaponHit(const AOperativeCharacter* Operative, const FString& WeaponId, float Damage);

	/** "HUMAN" for a player, the bot's profile for the playtest bot. */
	void SetTesterProfile(const FString& Profile) { TesterProfile = Profile; }
	const FString& GetTesterProfile() const { return TesterProfile; }

	/** Builds and appends the record now (the game-flow hooks call it; smokes too). Returns the JSON line. */
	FString RecordRun(bool bVictory);

	/** True once this run's record was written. */
	bool HasRecorded() const { return bRecorded; }
	const FString& GetLastRecordJson() const { return LastRecordJson; }

	/** Saved/Telemetry/raw_runs/runs.jsonl. */
	static FString GetRunsFilePath();

	bool IsEnabled() const { return bEnabled; }
	void SetEnabled(bool bInEnabled) { bEnabled = bInEnabled; }

private:
	UFUNCTION()
	void HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode);

	UFUNCTION()
	void HandleEnemyDamaged(const FDamageSpec& Spec, float FinalDamage);

	void HandleEnemySpawned(AEnemyCharacter* Enemy, EEnemyArchetype Archetype);
	void HandleEnemyDied(AActor* Victim, const FString& Source);
	FMemberRunStats& StatsFor(const AOperativeCharacter* Operative);
	FDeployableRunStats* DeployableFor(const FString& Source);

	TMap<FString, FMemberRunStats> MemberStats;
	FDeployableRunStats Turrets;
	FDeployableRunStats Barricades;
	FDeployableRunStats Mines;
	FString TesterProfile = TEXT("HUMAN");
	FString LastRecordJson;
	double StartAppTime = 0.0;
	bool bRecorded = false;
	bool bEnabled = true;
};
