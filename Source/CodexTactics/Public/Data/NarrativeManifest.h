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
 * One entry of the manifest's "tutorial" section (TANDEM request #17: the Tutorial tab of the narrative sheet,
 * columns StepID, Kind, Title_EN, Text_EN, Radio_EN; optional speaker_en for radio / walk-and-talk lines).
 */
struct CODEXTACTICS_API FNarrativeTutorialEntry
{
	FString Title;
	FString Text;
	FString Radio;
	FString Speaker;
};

/** What the tutorial shows for a text id (GetTutorialText). */
struct CODEXTACTICS_API FTutorialTextView
{
	FString Title;
	/** Card / objective body or the line itself; "[EN missing: <id>]" when the id is unknown. */
	FString Text;
	FString Radio;
	FString Speaker;
	bool bMissing = false;
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
	/** Tutorial text id -> English texts (manifest "tutorial" section, request #17; empty until Gemini adds the tab). */
	TMap<FString, FNarrativeTutorialEntry> TutorialTexts;
	/**
	 * DRAFT tutorial texts (Content/Data/Tutorial/demo_01_texts_draft.json), the fallback of GetTutorialText until the
	 * manifest "tutorial" section exists (request #17); the manifest always wins.
	 */
	TMap<FString, FNarrativeTutorialEntry> DraftTutorialTexts;

	/** Parses the manifest JSON; false (and OutError) when it is not a valid manifest. */
	static bool Parse(const FString& Json, FNarrativeManifest& Out, FString& OutError);

	/** Parses {"texts": {"<ID>": {"title": ..., "text": ...}}} into OutTexts (replacing it); false when it is not valid JSON. */
	static bool ParseDraftTexts(const FString& Json, TMap<FString, FNarrativeTutorialEntry>& OutTexts, FString& OutError);

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

	/**
	 * The tutorial text of TextId: the "tutorial" section first (title_en / text_en / radio_en / speaker_en), else a
	 * one-line "sequences" entry with that id (its first line's speaker / text), else DraftTutorialTexts (logged once per
	 * id in non-shipping builds), else the placeholder
	 * "[EN missing: <TextId>]" (bMissing). Cyrillic never passes (treated as missing).
	 */
	FTutorialTextView GetTutorialText(const FString& TextId) const;

	/** Glossary name_en for Key, or Fallback when the key is unknown. */
	FString GetGlossaryName(const FString& Key, const FString& Fallback = FString()) const;

	/**
	 * English lines of a sequence. When the manifest has it, its lines win (empty text / speaker -> placeholder).
	 * Otherwise every AssetLines entry is kept only if it is already English, else replaced by the placeholder
	 * (a sequence with no asset lines gives one placeholder). Each problem is appended to OutWarnings.
	 */
	TArray<FDialogueLine> BuildLines(const FString& SequenceId, const TArray<FDialogueLine>& AssetLines, TArray<FString>& OutWarnings) const;
};
