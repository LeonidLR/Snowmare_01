#pragma once

#include "CoreMinimal.h"
#include "LevelGen/MissionGoalsRules.h"
#include "Subsystems/WorldSubsystem.h"
#include "MissionGoalsSubsystem.generated.h"

class AMissionGoalMarkerActor;
class AOperativeCharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnMissionGoalReached, const FString&, GoalId, AOperativeCharacter*, Operative, int32, ReachedCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMissionGoalsComplete);

/**
 * The level generator's goals as in-game mission objectives (designer 2026-11-17, TASK_BOARD F-4 option «а»). At BeginPlay it reads
 * Content/Data/LevelGen/objectives/<Map>.objectives.json (MissionGoalsRules; written by the Training Ground «Принять» / «Собрать в
 * Unreal»); a map without that file has no goals and nothing here runs (the props pattern, UCodexLevelPropsSubsystem).
 * Each unreached goal has a world marker (AMissionGoalMarkerActor, the orange hologram) and therefore a minimap objective icon; the HUD
 * draws "GOALS: n/N" under the objective banner (GetHudLine). A goal is REACHED when a living squad operative's feet stand within its
 * radius (2D, same floor) - a room goal: inside its room (MissionGoalsRules::IsAtGoal); "free" order = any order, "sequence" = only
 * the next one. ZONE CLEAR (designer 2026-11-20): a goal does not count while a RESISTING hostile (an "Enemy"-tagged actor - monster or
 * hostile PMC fighter - alive, not dying / killed in action, seen or not) stands in its zone (the same zone test); the squad standing
 * in a contested zone is BLOCKED (HUD "CLEAR THE ZONE", a radio line, the zone drawn red) and the goal counts once the zone is clear.
 * The HUD and the minimap draw every unreached goal as its zone (MissionGoalsRules::BuildZoneOutline: the room or a RadiusCm circle). Each reached goal hides its marker, posts a radio line and raises GameplayEvents::MissionGoalReached on the event bus
 * (UCodexEventBus::OnGameplayEvent / Detailed) plus OnGoalReached; when the last is reached, GameplayEvents::MissionGoalsComplete,
 * OnGoalsComplete and the EXISTING level end: ULevelExitSubsystem::CompleteLevel (mission complete, fade, next level / menu).
 * Codex.MissionGoals.Enabled 0 switches it off. UE-only (the Godot archive had no generated maps).
 */
UCLASS()
class CODEXTACTICS_API UMissionGoalsSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** The map has goals (its file loaded with at least one goal). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|MissionGoals")
	bool IsActive() const { return bActive; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|MissionGoals")
	int32 GetGoalCount() const { return File.Goals.Num(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|MissionGoals")
	int32 GetReachedCount() const { return MissionGoalsRules::CountReached(Reached); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|MissionGoals")
	bool AreAllReached() const { return bActive && MissionGoalsRules::AreAllReached(Reached); }

	bool IsGoalReached(int32 Index) const { return Reached.IsValidIndex(Index) && Reached[Index]; }

	/** A resisting hostile stands in the goal's zone (as of the last check). Internal truth - the HUD shows only IsGoalBlocked. */
	bool IsGoalContested(int32 Index) const { return Contested.IsValidIndex(Index) && Contested[Index]; }

	/** The squad stands in the countable goal's zone but it does not count: the zone is contested (as of the last check). */
	bool IsGoalBlocked(int32 Index) const { return Blocked.IsValidIndex(Index) && Blocked[Index]; }

	/** Some goal is blocked now (the HUD's "CLEAR THE ZONE"). */
	bool IsAnyGoalBlocked() const { return Blocked.Contains(true); }

	/** The goal may count now (unreached; in a sequence the next one). */
	bool CanGoalCount(int32 Index) const { return MissionGoalsRules::CanCount(File.Order, Reached, Index); }

	const FMissionGoalsFile& GetFile() const { return File; }
	const TArray<FMissionGoal>& GetGoals() const { return File.Goals; }

	/** Restores saved IDs silently; unknown IDs are ignored and all-reached never replays level completion. */
	void RestoreReachedGoals(const TArray<FString>& GoalIds);

	/** The goal's world marker (null when it could not spawn). */
	AMissionGoalMarkerActor* GetMarker(int32 Index) const;

	/** "GOALS: 1/3"; empty when the map has no goals. */
	FString GetHudLine() const;

	/** The file this world loaded (empty when none). */
	const FString& GetLoadedPath() const { return LoadedPath; }

	/**
	 * Loads a goals file instead of the map's (smokes / tools): clears the current goals and markers first. False (and inactive) when the
	 * file is missing or invalid.
	 */
	bool LoadFromFile(const FString& Path);

	/** Removes the goals and their markers (inactive afterwards). */
	void ClearGoals();

	/** Checks the squad against the goals now (the tick does it every EvaluateIntervalSeconds). */
	void EvaluateNow();

	/** True if Codex.MissionGoals.Enabled != 0. */
	static bool IsEnabled();

	/** Seconds between two reach checks. */
	static constexpr float EvaluateIntervalSeconds = 0.2f;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|MissionGoals")
	FOnMissionGoalReached OnGoalReached;

	UPROPERTY(BlueprintAssignable, Category = "CodexTactics|MissionGoals")
	FOnMissionGoalsComplete OnGoalsComplete;

private:
	void SpawnMarkers();
	void HandleReached(int32 GoalIndex, AOperativeCharacter* Operative);
	void HandleAllReached(AOperativeCharacter* LastOperative);

	/** The hostiles of the world as the zone-clear rule sees them (the "Enemy"-tagged actors). */
	void GatherHostiles(TArray<FMissionGoalHostile>& OutHostiles) const;
	/** Stores the contested / blocked state of this check; posts the radio line when a goal becomes blocked. */
	void UpdateZoneState(TConstArrayView<FVector> ResistingFeet, TConstArrayView<int32> BlockedGoals);

	FMissionGoalsFile File;
	TArray<bool> Reached;
	TArray<bool> Contested;
	TArray<bool> Blocked;
	/** The radio line of this contested spell was posted (cleared when the zone stops being contested). */
	TArray<bool> BlockAnnounced;
	FString LoadedPath;
	bool bActive = false;
	bool bCompleted = false;
	float EvaluateIn = 0.f;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AMissionGoalMarkerActor>> Markers;
};
