#pragma once

#include "CoreMinimal.h"
#include "Combat/FirefightRules.h"
#include "Subsystems/WorldSubsystem.h"
#include "FirefightSubsystem.generated.h"

class AActor;
enum class EAmbushTrigger : uint8;

/**
 * Horde #12 (game designer decisions 2026-11-02 / 2026-11-03 / 2026-11-08; UE-only, no Godot reference): counts the squad's FIREFIGHTS IN A
 * ROW on the level and asks UHordeSubsystem for the horde at the level's threshold ("horde_after_firefights", default 6). Definitions in
 * FirefightRules.h. Input: every hostile contact (ULevelEncounterSubsystem::OnHostileContact - an enemy spotted / attacked the squad, the
 * squad hit / attacked an enemy) with one of the level's enemies (ULevelEncounterSubsystem::IsLevelEnemy: placed patrols, the pressure's
 * patrols / ambushers; hostile PMC fighters) - never a scripted wave, never the horde itself (bKnowsSquadPosition). One encounter at a
 * time: a contact during a running encounter belongs to it. The slip-past rule follows the level's enemy groups at 4 Hz (the pressure's
 * group id, else the escort leader). Logs every step as "[Firefight] ..."; HQ lines: the hint before the threshold, the reset by a
 * slip-past (only when a streak was running); the horde warning itself is UHordeSubsystem's. Not saved: a load starts a fresh streak.
 */
UCLASS()
class CODEXTACTICS_API UFirefightSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	/** Firefights in a row now (since the last slip-past / horde). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Firefight")
	int32 GetStreak() const { return Streak.Count; }

	/** Firefights on this level. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Firefight")
	int32 GetFirefightCount() const { return Streak.Total; }

	/** The threshold in force (the level's horde_after_firefights / Codex.Horde.AfterFirefights). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Firefight")
	int32 GetThreshold() const;

	const FFirefightStreak& GetStreakState() const { return Streak; }
	/** An encounter is running (contact) / it already counts as a firefight. */
	bool IsEncounterActive() const { return Encounter.bActive; }
	bool IsFirefightActive() const { return Encounter.bActive && Encounter.bCounted; }
	int32 GetEncounterCount() const { return EncounterCount; }
	EFirefightEnd GetLastEnd() const { return LastEnd; }
	/** Groups slipped past (every verdict, also with a zero streak or the reset off). */
	int32 GetSlipPastCount() const { return SlipPastCount; }
	/** Level enemy groups followed by the slip-past rule now. */
	int32 GetTrackedGroupCount() const { return Groups.Num(); }

	/** Smokes: back to a fresh level (no encounter, streak 0, no groups). */
	void ResetForTest();

private:
	struct FTrackedGroup
	{
		FSlipGroupState State;
		/** Seen in the last group pass. */
		bool bSeen = false;
	};
	struct FSourceInfo
	{
		int32 Group = 0;
		bool bAmbusher = false;
	};

	void HandleHostileContact(EAmbushTrigger Trigger, AActor* Source);
	/** A level enemy / hostile PMC fighter, not a horde member. */
	bool IsTrackedSource(const AActor* Source) const;
	FSourceInfo DescribeSource(const AActor* Source);
	void UpdateEncounter(double Now);
	void UpdateGroups();
	void CountFirefight(const TCHAR* Why);
	void EndEncounter(EFirefightEnd Why, double Now);
	const FFirefightConfig& GetConfig() const;
	void PostHQ(const FString& Line) const;

	FFirefightEncounter Encounter;
	FFirefightStreak Streak;
	int32 EncounterCount = 0;
	EFirefightEnd LastEnd = EFirefightEnd::None;
	/** Who engaged in the running encounter. */
	TArray<TWeakObjectPtr<AActor>> Engaged;
	/** Slip-past state per level enemy group. */
	TMap<int32, FTrackedGroup> Groups;
	/** Groups that had a hostile contact since the last group pass (latched as engaged there). */
	TSet<int32> ContactGroups;
	/** Cached source descriptions (group key, ambusher). */
	TMap<TWeakObjectPtr<const AActor>, FSourceInfo> SourceInfo;
	float GroupTimer = 0.f;
	int32 SlipPastCount = 0;
	FDelegateHandle ContactHandle;
};
