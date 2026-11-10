#pragma once

#include "CoreMinimal.h"
#include "Characters/CriticalWoundRules.h"

/**
 * Which labels the canvas HUD (ACodexTacticsHUD) draws (the game designer 2026-11-10, feat/injured-tiers-fullbody: "one switch per label kind
 * the HUD draws today" - the Training Ground gets a panel for it). UE-only, no Godot reference (Godot's Label3D plates were per-scene).
 * Data: Content/Data/UI/hud_labels.json (schema codex.hud_labels/1): "labels": { "<key>": { "show": bool, "label": "<Russian name for the
 * panel>", "doc": "<English: what it is, where it is drawn>" } }. Every default = the HUD as it was (all on); a key missing from the file keeps
 * its default, an unknown key is logged. Read at level start (ACodexTacticsHUD::BeginPlay -> ReloadConfig) and, in dev builds, re-read when
 * the file's timestamp changes (PollConfigFile, at most once a second). Console override: Codex.HUD.WoundTags (-1 data, 0 off, 1 on) for the
 * wound / critical tags (ApplyWoundTagOverride).
 * Tests CodexTactics.UI.HudLabels.*.
 */
namespace HudLabelRules
{
	/** One label kind the HUD draws (the order = the data file's order; keep GetKeys in step). */
	enum class EHudLabel : uint8
	{
		OperativeName,
		OperativeStance,
		FrostbiteTag,
		WoundTag,
		CriticalTag,
		PanicBadge,
		RageBadge,
		PostureLetters,
		KnockdownBadges,
		SuppressionBadges,
		RecruitPrompt,
		EnemyNames,
		EnemyStatus,
		EnemyHealth,
		EnemyArmorMarker,
		EnemyAlertMarkers,
		InteractableLabels,
		GateLabels,
		VehicleHullLabels,
		FloatingTexts,
		CoverTexts,
		DefenseMarkers,
		HitChance,
		HordeWarning,
		CombatModeBadge,
		SquadStatusPanel,
		Count
	};

	constexpr int32 LabelCount = static_cast<int32>(EHudLabel::Count);

	/** The data schema id ("schema" in hud_labels.json). */
	inline constexpr const TCHAR* SchemaId = TEXT("codex.hud_labels/1");

	/** A label kind's data key and its default (today's behaviour). */
	struct FLabelKey
	{
		EHudLabel Label;
		const TCHAR* Key;
		bool bDefaultShow;
	};

	/** Every label kind, in EHudLabel order. */
	CODEXTACTICS_API const TArray<FLabelKey>& GetKeys();

	/** The data key of a label kind ("operative_name", ...). */
	CODEXTACTICS_API const TCHAR* GetKey(EHudLabel Label);

	/** The switches in force. */
	struct CODEXTACTICS_API FHudLabelConfig
	{
		FHudLabelConfig();
		/** One per EHudLabel. */
		TStaticArray<bool, LabelCount> Shown;

		bool IsShown(EHudLabel Label) const { return Shown[static_cast<int32>(Label)]; }
		void SetShown(EHudLabel Label, bool bShow) { Shown[static_cast<int32>(Label)] = bShow; }
		/** All the enemy plate's parts at their defaults (the plate's text needs no recomposing). */
		bool IsEnemyPlateDefault() const;
	};

	/**
	 * Reads hud_labels.json text: false when it is no JSON object or its "schema" is not SchemaId (Out untouched). Each "labels" entry's
	 * "show" (a bool) sets its switch; missing keys keep Out's value; unknown keys are added to OutUnknown (if given).
	 */
	CODEXTACTICS_API bool ParseConfig(const FString& JsonText, FHudLabelConfig& Out, TArray<FString>* OutUnknown = nullptr);

	/** Codex.HUD.WoundTags: < 0 = the data's wound / critical tags, 0 = both off, > 0 = both on. */
	CODEXTACTICS_API FHudLabelConfig ApplyWoundTagOverride(const FHudLabelConfig& Config, int32 Override);

	/** The wound tag text of a tier (" · CRITICAL" / " · BADLY WOUNDED" / " · WOUNDED", or empty) under the switches. */
	CODEXTACTICS_API FString WoundTagText(const FHudLabelConfig& Config, CriticalWoundRules::EWoundTier Tier);

	/**
	 * A squad member's name plate as the HUD draws it: "<name> · <stance>[ · FROSTBITTEN][ · <wound tag>]", each part under its switch,
	 * the parts joined by " · "; empty when every part is off (no plate is drawn).
	 */
	CODEXTACTICS_API FString ComposeOperativePlate(const FHudLabelConfig& Config, const FString& Name, const FString& Stance, bool bFrostbitten,
		CriticalWoundRules::EWoundTier Tier);

	/**
	 * An enemy's plate text: line 1 "<name><status>" (Status as the enemy writes it: " BURNING ICED ..."), line 2 "<health>/<max>"; each
	 * under its switch; a line left empty is dropped; empty when nothing is shown.
	 */
	CODEXTACTICS_API FString ComposeEnemyLabel(const FHudLabelConfig& Config, const FString& Name, const FString& Status, int32 Health, int32 MaxHealth);

	/** A floating text is the cover entry's ("IN COVER (-35% damage)": the cover_texts switch). */
	CODEXTACTICS_API bool IsCoverText(const FString& Text);

	/** Content/Data/UI/hud_labels.json. */
	CODEXTACTICS_API FString GetConfigPath();

	/** The switches in force (the file read on first use; the code defaults when it is missing / unreadable). */
	CODEXTACTICS_API const FHudLabelConfig& GetConfig();

	/** Re-reads the file now (level start); returns true when it was read. */
	CODEXTACTICS_API bool ReloadConfig();

	/**
	 * Dev builds: at most once per PollSeconds (real time NowSeconds) compares the file's timestamp with the last read and re-reads it when it
	 * changed; returns true when it re-read. Shipping: never polls.
	 */
	CODEXTACTICS_API bool PollConfigFile(double NowSeconds, double PollSeconds = 1.0);

	/** Replaces the switches in force (tests / smokes). */
	CODEXTACTICS_API void SetConfig(const FHudLabelConfig& Config);
}
