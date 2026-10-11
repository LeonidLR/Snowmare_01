#pragma once

#include "CoreMinimal.h"
#include "Tutorial/TutorialTypes.h"

class FJsonObject;

/** One line of Saved/Tutorial/tutorial.jsonl (testers' telemetry). */
struct CODEXTACTICS_API FTutorialTelemetryEntry
{
	/** "session_start", "beat_started", "shown", "completed", "completed_silently", "skipped", "timed_out", "lesson_downed", "retry", "enabled", "disabled". */
	FString Event;
	FName Step;
	FName Beat;
	/** Seconds the step was on screen (0 when not shown). */
	float ActiveSeconds = 0.f;
	/** Seconds since the world started. */
	float GameSeconds = 0.f;
	FString Map;
	FString Session;
	/** ISO 8601 UTC. */
	FString Timestamp;
};

/**
 * Pure rules of the Demo 01 tutorial framework (UX brief 03 §2, producer plan C8): the steps JSON contract, matching,
 * line timing, the lesson "downed instead of killed" rule, save and telemetry formats. UE-only (Godot had no tutorial).
 */
namespace TutorialRules
{
	/** "hint_card" | "objective" | "radio" | "walk_talk". */
	CODEXTACTICS_API bool ParseKind(const FString& Text, ETutorialStepKind& OutKind);
	/** "manual" | "beat_start" | "volume" | "event" | "after_step" | "first_use" | "failure_retry". */
	CODEXTACTICS_API bool ParseTriggerType(const FString& Text, ETutorialTriggerType& OutType);
	CODEXTACTICS_API const TCHAR* KindName(ETutorialStepKind Kind);
	CODEXTACTICS_API const TCHAR* StateName(ETutorialStepState State);
	/** "completed" / "skipped" / "timed_out" -> the state; false for anything else. */
	CODEXTACTICS_API bool ParseFinishedState(const FString& Text, ETutorialStepState& OutState);

	/**
	 * Parses a steps file. Errors (invalid JSON, a step without id, a duplicate id, an unknown kind / trigger type, an
	 * after_step naming an unknown step) fail; soft problems (a beat not declared in "beats" is added, "blocking": true is
	 * forced off, an unknown key) go to OutWarnings.
	 */
	CODEXTACTICS_API bool ParseSteps(const FString& Json, FTutorialStepsData& OutData, TArray<FString>& OutWarnings, FString& OutError);

	/** Loads Content/Data/Tutorial/<FileName> (a bare name or a path relative to that folder). */
	CODEXTACTICS_API bool LoadStepsFile(const FString& FileName, FTutorialStepsData& OutData, TArray<FString>& OutWarnings, FString& OutError);
	CODEXTACTICS_API FString GetTutorialDirectory();

	CODEXTACTICS_API bool IsFinished(ETutorialStepState State);
	CODEXTACTICS_API bool IsRunning(ETutorialStepState State);

	/** Name pattern match for events (same syntax as PlayerCommands::Matches: exact, "*", "Prefix.*"). */
	CODEXTACTICS_API bool MatchesName(const FString& Pattern, FName Name);

	/** True when the step's completion counts this command (any pattern of "complete.commands"). */
	CODEXTACTICS_API bool CountsCommand(const FTutorialStep& Step, FName CommandId);

	/** Seconds a subtitle line stays: Override when > 0, else clamp(1.2 + 0.055 s per character, 2.5, 9). */
	CODEXTACTICS_API float LineDurationSeconds(const FString& Text, float Override = 0.f);

	/** "[EN missing: <TextId>]" (the dialogue placeholder style). */
	CODEXTACTICS_API FString MakeMissingText(FName TextId);

	/**
	 * Lesson beats (Demo 01 user decision 2026-10-11): while the tutorial is on and the current beat is a lesson, a lethal
	 * hit downs the operative instead of killing him (any operative, the commander too); outside lessons normal rules
	 * (permanent loss, commander-only defeat).
	 */
	CODEXTACTICS_API bool ShouldDownInsteadOfKill(bool bTutorialEnabled, bool bLessonBeatActive);

	/** Health a downed (not killed) operative keeps. */
	constexpr float LessonHealthFloor = 1.f;

	/** The progress as the save's "tutorial" object ({"beat", "finished": {id: state}, "pending": [ids]}). */
	CODEXTACTICS_API TSharedRef<FJsonObject> SaveProgress(const FTutorialProgress& Progress, bool bEnabled);
	/** Reads the save's "tutorial" object; false when it is not one. */
	CODEXTACTICS_API bool LoadProgress(const FJsonObject& Object, FTutorialProgress& OutProgress);

	/** One JSON object line (no newline) for tutorial.jsonl. */
	CODEXTACTICS_API FString BuildTelemetryLine(const FTutorialTelemetryEntry& Entry);

	/** Key caps of a step joined for the card: "[B]  [Alt+V]". */
	CODEXTACTICS_API FString FormatKeys(const TArray<FString>& Keys);
}
