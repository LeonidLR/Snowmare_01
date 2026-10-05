#pragma once

#include "CoreMinimal.h"
#include "Characters/SquadAutonomyRules.h"
#include "GameFlow/GameFlowTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "SquadAutonomySubsystem.generated.h"

class AEnemyCharacter;
class AOperativeCharacter;

/** What Commander Mode did so far (smokes, the summary log). */
struct CODEXTACTICS_API FSquadAutonomyStats
{
	int32 CoverMoves = 0;
	int32 StanceChanges = 0;
	int32 ProneForSniper = 0;
	int32 Reloads = 0;
	int32 SidearmSwitches = 0;
	int32 PrimarySwitches = 0;
	int32 FlankShifts = 0;
	int32 LeashReturns = 0;
	int32 AidMoves = 0;
	int32 AidGiven = 0;
	int32 AidRefusedUnsafe = 0;
	int32 TargetPicks = 0;
	int32 Freezes = 0;
};

/**
 * Commander Mode (Sprint 07, TANDEM «SPRINT 07 DIRECTIVE»; UE-only, no Godot reference): with
 * USquadSubsystem::IsAutonomousSquadCombat on, in a real-time wave fight, every squad member acts on its own every
 * 0.3 s inside the leash around its TacticalAnchor (the last player move order): barricade cover against the threat
 * (high ground preferred), stance by the ROE (prone / into cover under a marksman's aim), target by the ROE policy,
 * reload behind cover, pistol at point-blank range with an empty rifle, a cover-side change against a flanker, field
 * aid to a wounded mate when the Safe Aid Check passes. A tactical pause (Space) or the turn-based fight freezes it at
 * once: autonomous walks stop, its targets are dropped, the player's orders are untouched. Rules: SquadAutonomyRules;
 * ROE: SquadROE (squad_roe.json). Reuses the playtest bot's cover primitives (PlaytestBotRules).
 */
UCLASS()
class CODEXTACTICS_API USquadAutonomySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Runs the per-operative decisions now (smokes; the tick does it every DecisionInterval). */
	void RunDecisions();

	/** Stops the autonomous walks and drops the autonomy targets (pause, mode off). */
	void Freeze();

	const FSquadAutonomyStats& GetStats() const { return Stats; }

	/** Seconds between decisions. */
	static constexpr float DecisionInterval = 0.3f;

protected:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

private:
	enum class ETask : uint8
	{
		None,
		Cover,
		Aid,
		Return
	};

	struct FOperativeState
	{
		ETask Task = ETask::None;
		/** The anchor the autonomous walk belongs to: a new player order (anchor moved) cancels the task. */
		FVector TaskAnchor = FVector::ZeroVector;
		FVector TaskGoal = FVector::ZeroVector;
		float TaskTime = 0.f;
		float MoveCooldown = 0.f;
		TWeakObjectPtr<AOperativeCharacter> Patient;
		FString PrimaryWeaponId;
		/** The autonomy drew the sidearm (it puts it away again; a player's own pick is left alone). */
		bool bOnAutoSidearm = false;
	};

	struct FEnemyView
	{
		AEnemyCharacter* Enemy = nullptr;
		FVector Location = FVector::ZeroVector;
		/** A marksman aiming at this squad member (nullptr: not aiming). */
		const AOperativeCharacter* AimTarget = nullptr;
	};

	UFUNCTION()
	void HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode);

	void Decide(AOperativeCharacter& Operative, FOperativeState& State, const TArray<FEnemyView>& Enemies, const TArray<AOperativeCharacter*>& Squad,
		float Elapsed);
	bool UpdateTask(AOperativeCharacter& Operative, FOperativeState& State, const TArray<FEnemyView>& Enemies, float Elapsed);
	void UpdateWeapons(AOperativeCharacter& Operative, FOperativeState& State, float NearestEnemyCm);
	bool TryAid(AOperativeCharacter& Operative, FOperativeState& State, const TArray<FEnemyView>& Enemies, const TArray<AOperativeCharacter*>& Squad);
	void ChooseTarget(AOperativeCharacter& Operative, const TArray<FEnemyView>& Enemies, const AOperativeCharacter* Leader);
	/** Best barricade stand point against Threat whose stand lies inside the leash; false without one. */
	bool FindCoverInLeash(const AOperativeCharacter& Operative, const FVector& Threat, FVector& OutStand) const;
	void StartTask(AOperativeCharacter& Operative, FOperativeState& State, ETask Task, const FVector& Goal);
	bool ProjectToNav(const FVector& Point, FVector& OutPoint) const;

	TMap<TWeakObjectPtr<AOperativeCharacter>, FOperativeState> States;
	FSquadAutonomyStats Stats;
	float DecisionTimer = 0.f;
	float SummaryTimer = 0.f;
	bool bWasActive = false;
};
