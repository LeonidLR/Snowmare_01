#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "Core/PlayerCommands.h"
#include "GameFlow/GameFlowTypes.h"
#include "Survival/ColdRules.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tutorial/TutorialRules.h"
#include "Tutorial/TutorialStateMachine.h"
#include "TutorialSubsystem.generated.h"

class AEnemyCharacter;
class AOperativeCharacter;
class ATutorialMarkerActor;
class ATutorialTriggerVolume;
class FJsonObject;
class UHealthComponent;

/** The hint card as the overlay shows it. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FTutorialCardView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	bool bVisible = false;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FName StepId;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FText Title;

	/** Body text with {key:<CommandId>} tokens resolved to key caps. */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FText Body;

	/** "[B]  [Alt+V]". */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FText Keys;

	/** "2 / 5" among the cards of its beat (empty for a single card). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FText Progress;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	bool bSkippable = true;

	/** The confirm moment (the step was just completed; the card hides after a short flash). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	bool bDone = false;

	/** The operative the card is about (step "focus": "coldest_operative"), null when none. */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	TObjectPtr<AOperativeCharacter> FocusOperative = nullptr;

	/** His display name (also substituted for the {focus} token of the text). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FText FocusName;
};

/** One objective tracker line. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FTutorialObjectiveView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FName StepId;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FText Text;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	bool bDone = false;
};

/** The radio / walk-and-talk subtitle strip. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FTutorialSubtitleView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	bool bVisible = false;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FName StepId;

	/** Dialogue speaker name ("Commander", "Engineer", "Medic-Sapper", "HQ"). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FText Speaker;

	/** Role tag of the dialogue speaker card (DialogueRules::GetSpeakerStyle). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FText Role;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FText Line;

	/** Linear colour of the speaker's name (dialogue card colour). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FLinearColor SpeakerColor = FLinearColor::White;

	/** Seconds left on this line. */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	float SecondsLeft = 0.f;

	/** 0-based index of the shown line and the step's line count ("2 / 5"). */
	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	int32 LineIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	int32 LineCount = 0;
};

/** The "Retry from checkpoint" prompt after a lesson down. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FTutorialRetryView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	bool bVisible = false;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	FText Text;

	UPROPERTY(BlueprintReadOnly, Category = "CodexTactics|Tutorial")
	bool bHasCheckpoint = false;
};

/**
 * The current navigation target as the HUD shows it (2026-10-20, UTutorialSubsystem::GetGuideView): exactly one at a time - the edge
 * arrow (with the distance) when it is off-screen and the highlighted minimap icon point at it. Resolved from FTutorialStateMachine::GetGuide:
 * a step's markers -> the nearest shown one; a volume -> the runtime guide beacon on the ground at its box.
 */
struct CODEXTACTICS_API FTutorialGuideView
{
	bool bValid = false;
	FTutorialGuide Guide;
	/** Where the arrow / icon points (the marker's or the beacon's location). */
	FVector Location = FVector::ZeroVector;
	/** The marker actor shown there (a level marker, or the runtime guide beacon). */
	TWeakObjectPtr<ATutorialMarkerActor> Marker;
	/** The marker wants the off-screen edge arrow (ATutorialMarkerActor::bScreenEdgePointer). */
	bool bEdgePointer = true;
};

/**
 * Demo 01 tutorial framework (producer plan C8, UX brief 03 §2): loads Content/Data/Tutorial/<file>.json (the level JSON
 * "demo" object names it), runs the per-step state machine (FTutorialStateMachine) on player commands
 * (UCodexEventBus::OnPlayerCommand, the PlayerCommands::Report choke point), gameplay events, tutorial volumes and time;
 * feeds the non-blocking overlay (UTutorialOverlayWidget: hint card, objective tracker, radio / walk-and-talk strip that
 * never pauses the game) and the world markers (ATutorialMarkerActor); writes Saved/Tutorial/tutorial.jsonl (testers'
 * telemetry); saves the completed steps in the save's "tutorial" section; per-beat checkpoint autosave (slot
 * "checkpoint") and, in lesson beats, a lethal hit downs the operative instead of killing him ("Retry from checkpoint").
 * Tutorial ON / OFF: SetTutorialEnabled / console Codex.Tutorial.Enabled (a player setting kept in
 * Saved/Tutorial/profile.json). Texts: FNarrativeManifest::GetTutorialText ("[EN missing: <id>]" until the Tutorial tab
 * exists, TANDEM request #17). UE-only.
 */
UCLASS()
class CODEXTACTICS_API UTutorialSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Checkpoint save slot of the beats. */
	static const TCHAR* CheckpointSlot;

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return false; }

	// --- Setup ------------------------------------------------------------------------------------------------------

	/** Loads a steps file from Content/Data/Tutorial (the level's demo flags do this on their own) and starts it. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	bool LoadStepsFile(const FString& FileName);

	/** Loads already-parsed steps (smokes, tests) and starts them. */
	void LoadSteps(const FTutorialStepsData& Data);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	bool HasSteps() const { return Machine.IsLoaded(); }

	/** Steps loaded, the player's tutorial setting on and the tutorial not skipped for this run. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	bool IsTutorialActive() const;

	/**
	 * Rev 5i (game designer 2026-10-15): SKIP TUTORIAL - the tutorial is off for THIS run (the player's ON / OFF setting is not
	 * touched): every lock and confinement is released, cards / objectives / radio / walk-and-talk lines disappear (the story
	 * dialogue windows of the level go on), no more checkpoints, normal death rules; the quest / wave flow is not affected. Saved
	 * ("skipped" in the tutorial section): a load keeps it skipped. RESET TUTORIAL starts it again. False without steps / already
	 * skipped. Callers confirm with the player first (the overlay button and the pause menu entry ask Yes / No).
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	bool SkipTutorial();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	bool IsTutorialSkipped() const { return bSkipped; }

	/** The SKIP TUTORIAL button is offered on screen: the tutorial runs and is still in its start beat (Demo 01: A0). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	bool IsSkipOfferShown() const;

	/** The player's tutorial setting (console Codex.Tutorial.Enabled; saved to Saved/Tutorial/profile.json). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	static bool IsTutorialEnabled();

	/** Tutorial ON / OFF (the pause-menu entry later); OFF hides everything and stops counting. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	void SetTutorialEnabled(bool bEnabled);

	// --- Inputs (also from Blueprints: level scripts may raise their own events) --------------------------------------

	/** A player command (normally through UCodexEventBus::OnPlayerCommand). */
	void HandlePlayerCommand(FName CommandId, const FPlayerCommandContext& Context);

	/** A gameplay event (normally through UCodexEventBus::OnGameplayEvent); SubjectId = the operative's id (distinct counts). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	void NotifyEvent(FName EventName, FName SubjectId = NAME_None);

	/** Ids of the tutorial volumes (ATutorialTriggerVolume::GetEffectiveVolumeId) whose box holds Location ("in_volume" completions). */
	TArray<FName> GetTutorialVolumesAt(const FVector& Location) const;

	/** A squad member entered a tutorial volume; false when it did not count (tutorial off, required beat not reached). */
	bool NotifyVolumeEntered(ATutorialTriggerVolume* Volume, AOperativeCharacter* Operative);

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	bool StartBeat(FName BeatId);

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	bool TriggerStep(FName StepId);

	/** Skips the visible hint card ("Skip" on the card). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	bool SkipActiveCard();

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	bool SkipStep(FName StepId);

	// --- Dialogue skip (user request 2026-10-13: radio / walk-and-talk, every level) -----------------------------------

	/** A radio / walk-and-talk line is on screen now (the Enter key and the strip's SKIP act on it). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	bool IsSpeechActive() const;

	/**
	 * Enter (tap): the next line at once; after the last one the step ends exactly as if it had played out. False when no
	 * speech is shown. The lines are already in the log (all of them are logged when the speech starts).
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	bool AdvanceSpeechLine();

	/**
	 * Enter (hold) / the strip's SKIP: the whole conversation ends now, exactly as if it had played out (COMPLETED, not
	 * skipped: its after_step followers fire and its lock / unlock lists apply, e.g. the walk card after the HQ radio).
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	bool SkipSpeech();

	/** Replays a finished step (pause menu "replay" later). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	bool ReplayStep(FName StepId);

	/** Forgets every step's progress and starts the tutorial again from its start beat. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	void ResetTutorial();

	/** "Retry from checkpoint": loads the "checkpoint" slot in place. False without one. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	bool RetryFromCheckpoint();

	/** "Carry on" on the retry prompt: hides it (the downed operative keeps 1 HP). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	void DismissRetryPrompt() { bRetryPrompt = false; }

	/**
	 * Saves the "checkpoint" slot now (a beat with "checkpoint": true does it when it starts: the previous stage is done). Rev 5f:
	 * the ONLY save the game writes on its own (no autosaves); never while a save is being loaded (IsLoadPending), never in the
	 * fight (CanSaveNow), not in headless checks unless USaveGameSubsystem::bCheckpointsEnabled.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Tutorial")
	bool SaveCheckpoint();

	/** A save waits to be applied to this world (F9 / Continue / LOAD reopened the map; UMissionSessionSubsystem::HasPendingLoad). */
	bool IsLoadPending() const;

	// --- Queries ----------------------------------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	ETutorialStepState GetStepState(FName StepId) const { return Machine.GetState(StepId); }
	/** How far a running step got (commands / events counted towards its completion count). */
	int32 GetStepProgress(FName StepId) const { return Machine.GetProgress(StepId); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	FName GetCurrentBeat() const { return Machine.GetCurrentBeat(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	bool IsLessonBeat() const { return Machine.IsLessonBeat(); }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	FTutorialCardView GetCardView() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	TArray<FTutorialObjectiveView> GetObjectiveViews() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	FTutorialSubtitleView GetSubtitleView() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	FTutorialRetryView GetRetryView() const;

	/** The English text of a tutorial text id (narrative manifest), "[EN missing: <id>]" until it exists. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	static FText GetTutorialText(FName TextId);

	/** Markers shown now (the overlay draws their labels). */
	TArray<ATutorialMarkerActor*> GetVisibleMarkers() const;
	/**
	 * THE current navigation target (2026-10-20): what FTutorialStateMachine::GetGuide names, resolved in the world (refreshed every
	 * tick). Invalid with the tutorial inactive / nothing to show. The overlay draws its edge arrow, the minimap highlights it.
	 */
	const FTutorialGuideView& GetGuideView() const { return GuideView; }
	/** The runtime guide beacon (shown at a volume target; smokes), null before the first volume target. */
	ATutorialMarkerActor* GetGuideBeacon() const { return GuideBeacon.Get(); }
	/**
	 * Pacing (2026-10-20, telemetry for play-tests and smokes): real seconds with NO actionable instruction on screen (no card, no objective,
	 * no current target) since the steps loaded, and the longest wait from a completed card to the next card / objective
	 * (telemetry events "no_instruction" / "next_instruction", active_s = the seconds).
	 */
	float GetNoInstructionSeconds() const { return NoInstructionTotal; }
	float GetMaxNextInstructionSeconds() const { return MaxNextInstructionSeconds; }
	/** "markers:<step>" / "volume:<id>" / "" - the guide in one string (logs, telemetry, the save's informational "guide"). */
	static FString DescribeGuide(const FTutorialGuide& Guide);

	const FTutorialStateMachine& GetMachine() const { return Machine; }

	/** Operatives downed by the lesson rule since the last checkpoint load (smokes). */
	int32 GetLessonDownCount() const { return LessonDownCount; }

	/** Log file of this run (smokes). */
	FString GetTelemetryPath() const;

	// --- Command locks, conditions, focus (2026-10-12, Demo 01 A0-A1 redesign) ------------------------------------------

	/** True when the tutorial is active and a beat / step lock covers CommandId (e.g. "Order.Sprint" until warmed up in A1). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Tutorial")
	bool IsCommandLocked(FName CommandId) const;

	/**
	 * THE lock choke point for order handlers: true when CommandId is locked now; the caller then refuses / degrades the order
	 * and this raises "Tutorial.Locked" + "Tutorial.Locked.<CommandId>" (a "feed" / card step can react once). When no step
	 * reacts to that event, ONE short feed line per command family ("Stance", "Time", ...) says it is not taught yet (Demo 01
	 * progressive unlock, user request 2026-10-13). False without a tutorial (tutorial OFF = no locks).
	 */
	static bool CheckCommandLocked(const UObject* WorldContext, FName CommandId);

	/**
	 * The command an order handler should carry out (TutorialRules::ResolveLockedCommand): CommandId when free, its fallback
	 * when locked ("Order.Sprint" -> "Order.Move"), NAME_None when locked without one. A locked command raises the
	 * "Tutorial.Locked" events (CheckCommandLocked).
	 */
	static FName FilterCommand(const UObject* WorldContext, FName CommandId);

	/** Locked commands refused since the steps were loaded (smokes). */
	int32 GetLockedRefusalCount() const { return LockedRefusals; }

	/** Hint cards completed from the command history without being shown ("precredit", smokes). */
	int32 GetPrecreditedCount() const { return PrecreditedCount; }

	/**
	 * A step condition stat now (TutorialRules::IsKnownStat; meanings in docs/systems/tutorial-engine.md): "squad_cold_max" /
	 * "squad_cold_min" (cold % of the living squad members), "leader_cold", "squad_heat_packs", "squad_min_heat_packs",
	 * "heat_packs_given" (USquadTransferSubsystem; rev 5d), "squad_solo", "squad_injury_max", "squad_prone", "enemies_unseen",
	 * "traps_spotted.<tag>", "squad_not_low_in.<volume>". 0 without a squad.
	 */
	float GetConditionStat(FName Stat) const;

	/** The operative a shown step focuses on ("focus"), null when none. */
	AOperativeCharacter* GetFocusOperative(FName StepId) const;

	/**
	 * 2026-10-21 ("enemies" bindings, the A4 ambush split): the level's enemies (alive or dying) that belong to the steps file's binding
	 * BindingId - their patrol route or they themselves match one of its selectors (label, object name, tag). Empty for an unknown id.
	 */
	TArray<AEnemyCharacter*> FindBoundEnemies(FName BindingId) const;

	/** Enemy belongs to the binding BindingId of the loaded steps (FindBoundEnemies). */
	bool IsEnemyBound(const AEnemyCharacter& Enemy, FName BindingId) const;

	/**
	 * 2026-10-22 (stat "all_dead.<binding>"): every enemy of the binding is dead - the ones it finds are all dying / dead, or it found one
	 * earlier in this level (at the steps load, a stat poll, or before the save this game was loaded from: "enemy_bindings_seen") and none
	 * is left. False when the binding never matched an enemy here (a map without it) or is unknown.
	 */
	bool AreBoundEnemiesDead(FName BindingId) const;

	/**
	 * The "enemy" filter of an event now: the subject (an enemy of the world by object name) is bound, a living operative stands within
	 * "within_m" and it stands at least "off_route_m" from its patrol route (TutorialRules::PassesEnemyFilter). The state machine asks it.
	 */
	bool PassesEnemyFilterNow(const FTutorialEnemyFilter& Filter, FName SubjectId) const;

	/** Re-resolves the current navigation target now (normally every tick; smokes that just placed what a spot needs). */
	void RefreshGuideNow() { RefreshGuide(); }

	/**
	 * "role_spots" (2026-10-19, the A3 cover lesson): Operative stands at his role's spot of StepId - within the ring (+ 80 cm) of the
	 * step's marker whose RoleId is his role. True when the level has no role marker for that step (an older map: any place counts).
	 */
	bool IsAtRoleSpot(FName StepId, const AOperativeCharacter& Operative) const;

	/** A role spot of StepId is taken: an operative of RoleId counted for the step (his marker hides). */
	bool IsRoleSpotDone(FName StepId, FName RoleId) const;

	/** The role id of a role-spot marker / an operative: the EOperativeRole name ("Commander", "Engineer", "MedicSapper"). */
	static FName GetRoleId(const AOperativeCharacter& Operative);

	// --- Confine (rev 5i, game designer 2026-10-15: hold the squad in an area until the lesson is done) ----------------

	/** The tutorial is active and a step's "confine" holds the squad now (FTutorialConfine). */
	bool IsConfining() const;

	/**
	 * Judges a move of Operative to Destination under the confinement in force (Allowed when none): the area test (the open volumes
	 * of the confinement, or their bounding box with "span"; a 50 cm margin) + TutorialRules::JudgeConfinedMove.
	 */
	ETutorialConfineVerdict JudgeMove(const AOperativeCharacter& Operative, const FVector& Destination);

	/** A move order was refused: the verdict's feed line ("Hold here - follow the training.", at most one per verdict per second), "Tutorial.Confined". */
	void ReportConfinedRefusal(ETutorialConfineVerdict Verdict, const AOperativeCharacter* Operative);

	/**
	 * THE confine choke point for move orders of the player / interactions / autonomy (AOperativeCharacter::OrderMoveTo,
	 * OrderTakeCover, RequestMove, the ground click): true when the order may go ahead; false = refused (the line is posted).
	 * True without a tutorial / confinement.
	 */
	static bool CheckConfinedOrder(const UObject* WorldContext, const AOperativeCharacter* Operative, const FVector& Destination);

	/** Point lies in the allowed area of the confinement in force (true when none). */
	bool IsInsideConfinement(const FVector& Point) const;

	/** A formation move: pulls Destination into the holding volume (its box shrunk by 60 cm). True when it was moved. */
	bool ClampIntoConfinement(FVector& InOutDestination) const;

	/** The stance every move needs under the confinement in force ("crouch" / "prone"), NAME_None when none. */
	FName GetConfineStance() const;

	/** "stand" / "crouch" / "prone" (TutorialRules::StanceSatisfies). */
	static FName GetStanceName(EOperativeStance Stance);

	/** Move orders refused by a confinement since the steps were loaded (smokes). */
	int32 GetConfinedRefusalCount() const { return ConfinedRefusals; }

	// --- Markers / save ---------------------------------------------------------------------------------------------

	void RegisterMarker(ATutorialMarkerActor* Marker);
	void UnregisterMarker(ATutorialMarkerActor* Marker);
	/** The volumes of the world (ATutorialTriggerVolume BeginPlay / EndPlay): the guide's lookups without an actor iterator per tick. */
	void RegisterVolume(ATutorialTriggerVolume* Volume);
	void UnregisterVolume(ATutorialTriggerVolume* Volume);

	/** The save's "tutorial" object (null when there is nothing to save). */
	TSharedPtr<FJsonObject> CaptureSaveState() const;
	void RestoreSaveState(const FJsonObject& Object);

private:
	struct FSpokenLine
	{
		FString Speaker;
		FString Text;
		float Duration = 3.f;
	};

	void InitFromLevel();
	void StartTutorial();
	/** Hides what the overlay shows on its own (speech strip, confirm flashes, the retry prompt): new steps, skip, reset, a load. */
	void ClearOverlayState();
	/** One "TRAINING" line in the message feed. */
	void PostTrainingLine(const FText& Line) const;
	void BindWorldEvents();
	void BindSquadMembers();
	void ProcessTransitions();
	void RefreshMarkers();
	void StartSpeech(FName StepId);
	void TickSpeech(float DeltaSeconds);
	/** The speech step's lines are over (played out, advanced past the last one or skipped): it completes unless it waits for more. */
	void EndSpeech();
	bool HandleLethalHit(UHealthComponent* Health, const FString& Source);
	void WriteTelemetry(const FString& Event, FName StepId, FName BeatId, float ActiveSeconds) const;
	TArray<FSpokenLine> ResolveLines(const FTutorialStep& Step) const;
	/** A step was shown: focus, world actions, feed lines. */
	void HandleStepShown(const FTutorialStep& Step);
	void RunActions(const FTutorialStep& Step);
	/** The step completes in a volume: the squad members standing in it already count (FTutorialStateMachine::HandleVolumePresence). */
	void CountVolumePresence(const FTutorialStep& Step);
	/** "role_spots" steps running: every squad member in cover at his role's spot counts (FTutorialStateMachine::HandleRoleSpotPresence). */
	void CountRoleSpotPresence();
	/** {focus} -> the focused operative's name. */
	FString ResolveFocusToken(const FString& Text, FName StepId) const;
	AOperativeCharacter* FindColdestMember() const;
	/** Rev 2026-10-19: the living member with the lowest health share ("focus": "wounded_operative"). */
	AOperativeCharacter* FindMostWoundedMember() const;
	/** Picks the step's focus operative now (by its "focus" kind) and remembers it. */
	AOperativeCharacter* ChooseFocus(const FTutorialStep& Step);
	FTutorialCardView BuildCardView(FName StepId, bool bDone) const;
	FText ObjectiveText(FName StepId) const;

	UFUNCTION()
	void HandleBusCommand(FName CommandId, const FPlayerCommandContext& Context);
	UFUNCTION()
	void HandleBusEvent(FName EventName, AActor* Subject);
	UFUNCTION()
	void HandleMineSpotted(AActor* Mine, AOperativeCharacter* Spotter);
	UFUNCTION()
	void HandleItemUsed(AOperativeCharacter* Soldier, const FString& ItemId);
	UFUNCTION()
	void HandleSoldierDowned(AOperativeCharacter* Soldier);
	UFUNCTION()
	void HandleDialogueFinished();
	UFUNCTION()
	void HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode);
	UFUNCTION()
	void HandleColdTierChanged(AOperativeCharacter* Operative, EColdTier Tier);

	FTutorialStateMachine Machine;
	FString StepsFile;
	bool bInitDone = false;
	bool bBusBound = false;
	ECodexGamePhase LastPhase = ECodexGamePhase::Exploration;

	/** Speech playback of the active radio / walk-and-talk step. */
	FName SpeechStep;
	TArray<FSpokenLine> SpeechLines;
	int32 SpeechIndex = 0;
	float SpeechLineLeft = 0.f;

	/** The confirm flash of the card / objectives that just completed. */
	FName FlashCard;
	float FlashCardLeft = 0.f;
	TMap<FName, float> FlashObjectives;

	bool bRetryPrompt = false;
	int32 LessonDownCount = 0;
	int32 LockedRefusals = 0;
	/** Command families ("Stance", "Time", ...) that already got their "not taught yet" feed line. */
	TSet<FString> LockHintFamilies;
	/** Posts the "not taught yet" line for CommandId's family once, unless a step of the file reacts to its locked event. */
	void PostLockHint(FName CommandId);
	/** Drops every lock (a fight started / the tutorial was switched off) and logs why. */
	void UnlockAllCommands(const TCHAR* Reason);
	int32 PrecreditedCount = 0;
	/** Operative each shown "focus" step is about (resolved when it shows). */
	TMap<FName, TWeakObjectPtr<AOperativeCharacter>> FocusOperatives;
	float SquadBindTimer = 0.f;
	TArray<TWeakObjectPtr<AOperativeCharacter>> BoundMembers;
	TArray<TWeakObjectPtr<ATutorialMarkerActor>> Markers;
	FString SessionId;
	/** Progress loaded before the steps (a save applied before the level's steps file was read): applied by LoadSteps. */
	TOptional<FTutorialProgress> DeferredProgress;

	/** Rev 5i: SKIP TUTORIAL for this run (saved). */
	bool bSkipped = false;
	/** Rev 5i: the confinement serial last seen (FTutorialStateMachine::GetConfineSerial) and the refusals so far. */
	int32 SeenConfineSerial = 0;
	int32 ConfinedRefusals = 0;
	/** Real time of the last feed line per verdict (one line per verdict per second). */
	TMap<uint8, double> ConfineLineTimes;
	/** A confinement started / ended: a start stops the walks that would leave the area (or the whole squad with "stop"). */
	void HandleConfineChanged();
	/** 2026-10-20: the current navigation target (RefreshGuide, every RefreshMarkers) and the beacon shown at a volume target. */
	FTutorialGuideView GuideView;
	TWeakObjectPtr<ATutorialMarkerActor> GuideBeacon;
	TArray<TWeakObjectPtr<ATutorialTriggerVolume>> GuideVolumes;
	bool bRefreshingGuide = false;
	/** Pacing instrumentation (real seconds): the clock, the last card done, the current / total no-instruction time, the longest wait. */
	double PacingClock = 0.0;
	double LastCardDoneAt = -1.0;
	float NoInstructionRun = 0.f;
	float NoInstructionTotal = 0.f;
	float MaxNextInstructionSeconds = 0.f;
	/** Accumulates the no-instruction time (Tick). */
	void TickPacing(float RealDelta);
	/**
	 * "@patrol_route" (2026-10-20): the patrol routes' point nearest to the step's focus operative; false when the level has none.
	 * 2026-10-21: "@patrol_route.<enemy>" (only the bound enemies' routes), "@lure_spot.<enemy>" (TutorialRules::LureSpotDistanceCm short
	 * of that route towards the focus operative, on the ground) and "@enemy.<enemy>" (the nearest living bound enemy; false when none lives).
	 */
	bool ResolveGuideSpot(FName Spot, FName StepId, FVector& OutLocation) const;
	/** Logs, once per steps load, which level enemies each "enemies" binding found (none: a warning - those steps end by timeout only). */
	void LogEnemyBindings() const;
	/** 2026-10-21, action "tactical_pause": real seconds left to wait for the fight before the free pause opens (0 = none pending). */
	float LessonPauseWait = 0.f;
	/** 2026-10-22: "enemies" bindings that found an enemy in this level at least once (AreBoundEnemiesDead). */
	mutable TSet<FName> EnemyBindingsSeen;
	/** 2026-10-22: {pause_charges} / {pause_seconds} / {pause_cooldown} of a card text from the fight's config. */
	FString ResolvePauseTokens(const FString& Text) const;
	/** Opens the requested lesson pause once the fight runs in real time (UGameFlowSubsystem::EnterLessonPause). */
	void TickLessonPause(float RealDelta);
	/** Resolves Machine.GetGuide() in the world (nearest marker / the beacon at the volume), logs + telemetry when it changes. */
	void RefreshGuide();
	/** The registered volume with this id nearest to Near, null when the level has none. */
	const ATutorialTriggerVolume* FindGuideVolume(FName VolumeId, const FVector& Near) const;
	/** Where the beacon of a volume target stands: the box's ground point (a trace down from its top, else its bottom). */
	FVector GetVolumeGroundPoint(const ATutorialTriggerVolume& Volume) const;
	/** The tutorial volumes of the world with this id. */
	TArray<const ATutorialTriggerVolume*> FindVolumes(FName VolumeId) const;
	/** "span": the bounding box of every volume of these areas (invalid when none is in the world). */
	FBox GetSpanBox(const TArray<FName>& Areas) const;
};
