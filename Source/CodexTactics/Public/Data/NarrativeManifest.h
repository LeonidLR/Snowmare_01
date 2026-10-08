#pragma once

#include "CoreMinimal.h"
#include "Data/DialogueSequenceAsset.h"

/** One English line of a narrative sequence (a row of Content/Data/Narrative/narrative_manifest.json). */
struct CODEXTACTICS_API FNarrativeLine
{
	FString Speaker;
	FString Text;
	float DelayAfter = 4.f;
};

/**
 * Runtime view of Content/Data/Narrative/narrative_manifest.json, the output of Scripts/Narrative/sync_narrative.py
 * (Google Sheet -> manifest). Dialogues show the English columns (speaker_en / text_en); a missing line shows the
 * placeholder "[EN missing: <sequence>#<n>]" and logs a warning, so no Russian ever reaches the screen.
 * Sequence ids equal the DA_Dialogue* asset names. Godot reference: none (new narrative pipeline, 2026-10-08).
 */
struct CODEXTACTICS_API FNarrativeManifest
{
	/** Sequence id -> its lines in order. */
	TMap<FString, TArray<FNarrativeLine>> Sequences;
	/** Glossary key -> name_en (weapons, items, enemies, objects). */
	TMap<FString, FString> GlossaryNames;

	/** Parses the manifest JSON; false (and OutError) when it is not a valid manifest. */
	static bool Parse(const FString& Json, FNarrativeManifest& Out, FString& OutError);

	/** Loads Content/Data/Narrative/narrative_manifest.json once (empty and a warning in the log when it is missing). */
	static const FNarrativeManifest& Get();

	/** Drops the cached manifest so the next Get() re-reads the file (tests, hot reload). */
	static void Reset();

	/** True when Text contains a Cyrillic letter (such text must never be shown). */
	static bool ContainsCyrillic(const FString& Text);

	/** Text itself, or Fallback when it is empty / contains Cyrillic. */
	static FString EnglishOr(const FString& Text, const FString& Fallback);

	/** "[EN missing: <SequenceId>#<LineNumber>]", LineNumber is 1-based. */
	static FString MakeMissingPlaceholder(const FString& SequenceId, int32 LineNumber);

	/** Glossary name_en for Key, or Fallback when the key is unknown. */
	FString GetGlossaryName(const FString& Key, const FString& Fallback = FString()) const;

	/**
	 * English lines of a sequence. When the manifest has it, its lines win (empty text / speaker -> placeholder).
	 * Otherwise every AssetLines entry is kept only if it is already English, else replaced by the placeholder
	 * (a sequence with no asset lines gives one placeholder). Each problem is appended to OutWarnings.
	 */
	TArray<FDialogueLine> BuildLines(const FString& SequenceId, const TArray<FDialogueLine>& AssetLines, TArray<FString>& OutWarnings) const;
};
