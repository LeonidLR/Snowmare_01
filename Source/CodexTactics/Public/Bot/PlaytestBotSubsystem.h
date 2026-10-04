#pragma once

#include "CoreMinimal.h"
#include "Bot/PlaytestBotRules.h"
#include "Bot/SpatialTelemetryRecorder.h"
#include "Data/CombatTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "PlaytestBotSubsystem.generated.h"

class AActor;
class AOperativeCharacter;

/**
 * Autonomous playtest bot (Godot archive tools/bot/bot_driver.gd + smart_tactical_bot.gd): plays the level through —
 * exploration (collects loot crates and loose deployables), the combat zone, the profile's defences in the first
 * preparation, every wave with the tactical decisions (grenade at clusters, leader fallback, cover, stances, the
 * profile's medkit / warming food) — and the run telemetry (URunTelemetrySubsystem, tester_profile = the profile) is
 * written at the end. Started by the command line
 *   -CodexBot [-BotProfile=CASUAL|NORMAL|VETERAN] [-BotTimeout=<real s, 600>] [-BotLoadout=COLLECT|UNIQUE|PRESET]
 * (Scripts/run_simulations.bat runs it fixed-step without rendering and quits at the end) or the console command
 * CodexTactics.Bot <profile> (no exit).
 */
UCLASS()
class CODEXTACTICS_API UPlaytestBotSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return bActive; }
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual bool IsTickableWhenPaused() const override { return true; }

	/** Starts playing (bQuitAtEnd: exit the game when the run is over / timed out). */
	void StartBot(EBotProfile InProfile, bool bQuitAtEnd, bool bCollectLoot = true);
	bool IsActive() const { return bActive; }
	EBotProfile GetProfile() const { return Profile; }

	/** The phase the bot is in (logs / smokes). */
	enum class EBotStage : uint8 { Explore, EnterCombat, Fight, Done };
	EBotStage GetStage() const { return Stage; }
	int32 GetGrenadesThrown() const { return GrenadesThrown; }
	int32 GetDeploysOrdered() const { return DeploysOrdered; }
	int32 GetItemsUsed() const { return ItemsUsed; }
	int32 GetStallHunts() const { return StallHunts; }
	int32 GetMarksmanReactions() const { return MarksmanReactions; }
	int32 GetGeneratorRepairs() const { return GeneratorRepairs; }

	/** Sprint 05-D: a marksman aiming at a squad member -> crouch, the leader into cover at once. True when it reacted. */
	bool ReactToMarksman();
	const FSpatialTelemetryRecorder& GetSpatialRecorder() const { return Spatial; }

private:
	void TickExplore(float DeltaTime);
	void TickFight(float DeltaTime);
	void DeployDefences();
	void CombatAssist();
	void SmartTactics(float DeltaTime);
	void UpdateStances();
	/** The best barricade cover for Leader against Threat within 20 m (false: none). */
	bool FindCover(const AOperativeCharacter* Leader, const FVector& Threat, FVector& OutStand, FString& OutId, float& OutScore) const;

	/** Point on the navigation mesh near Point (false: none within 2 m). */
	bool ProjectToNav(const FVector& Point, FVector& OutPoint) const;
	/**
	 * The broken diesel generator (its heat and the turrets' power): the engineer (else anyone) walks up and repairs it —
	 * in the preparation, or in a wave with no enemy within 12 m of it. True while the repair is under way.
	 */
	bool TickGeneratorRepair(float DeltaTime, bool bInWave);
	class AInteractableActor* FindBrokenGenerator() const;
	void Finish(const TCHAR* Result, int32 ExitCode);
	FVector GetFrontDirection() const;
	TArray<AActor*> LiveEnemies() const;
	AOperativeCharacter* Member(int32 Index) const;
	void HandleEnemySpawned(class AEnemyCharacter* Enemy, EEnemyArchetype Archetype);
	void HandleEnemyDied(AActor* Victim, const FString& Source);

	/** Godot spatial_telemetry_recorder: frames / events of the fight for the editor's replay player. */
	FSpatialTelemetryRecorder Spatial;
	bool bSpatialStarted = false;

	EBotProfile Profile = EBotProfile::Normal;
	FBotProfileConfig Config;
	EBotStage Stage = EBotStage::Explore;
	bool bActive = false;
	bool bQuit = false;
	bool bCollect = true;
	double StartRealTime = 0.0;
	float TimeoutSeconds = 600.f;
	float DecisionTimer = 0.f;
	float StatusTimer = 0.f;

	// Exploration
	TArray<TWeakObjectPtr<AActor>> ExploreTargets;
	TWeakObjectPtr<AActor> ExploreTarget;
	float ExploreTime = 0.f;
	float TargetTime = 0.f;

	// Preparation / fight
	bool bDeployed = false;
	/** The veteran's second barricade, ordered once the first one stands (one deploy task per worker at a time). */
	TOptional<FVector> PendingBarricade;
	float PendingYaw = 0.f;
	float DeployWait = 0.f;
	float GrenadeCooldown = 0.f;
	float MoveCooldown = 0.f;
	/** Wave stall: the few last enemies did not die for a while -> the squad goes after them. */
	int32 LastEnemyCount = 0;
	float StallTimer = 0.f;
	int32 StallHunts = 0;
	int32 GrenadesThrown = 0;
	int32 DeploysOrdered = 0;
	int32 ItemsUsed = 0;
	float WarmMoveCooldown = 0.f;
	int32 MarksmanReactions = 0;
	int32 GeneratorRepairs = 0;
	TWeakObjectPtr<class AInteractableActor> RepairTarget;
	TWeakObjectPtr<AOperativeCharacter> RepairWorker;
	float RepairTime = 0.f;
	float RepairCooldown = 0.f;
	/** Heat sources a warm-up trip did not help (cold not dropping): skipped until the world time. */
	TMap<TWeakObjectPtr<const UObject>, double> ColdHeatUntil;
	TWeakObjectPtr<const UObject> WarmTarget;
	float WarmStartCold = 0.f;
	int32 WarmMoves = 0;
};
