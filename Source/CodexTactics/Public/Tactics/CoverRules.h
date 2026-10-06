#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "Survival/ColdRules.h"
#include "Tactics/CoverTypes.h"

/**
 * Gameplay modifiers of cover (Sprint 12-E; UE-only, no Godot reference — Gemini Sprint 12 spec, user decisions
 * 2026-10-06): damage absorption from the wall's frontal arc, cover blind fire (distinct from the Sprint 08 «ghost»
 * blind fire at a silhouette), headshot immunity while the head stays down, sight occlusion behind high cover, wind
 * chill shelter. Pure rules, tested in CodexTactics.Tactics.Cover.*.
 */
struct CODEXTACTICS_API FCoverCombatConfig
{
	/**
	 * High cover absorbs this share of a hit whose source lies inside the wall's frontal arc (user decision 2026-10-06:
	 * 90 %, not the directive's 100 %); flanking hits pass fully.
	 */
	float HighCoverFrontalAbsorb = 0.9f;
	/**
	 * Low cover (60 cm barricade and the like), crouched: the Godot barricade cover «В УКРЫТИИ (-35% урона)» — kept
	 * consistent with the existing barricade rules. Standing in low cover absorbs nothing (the body shows above it).
	 */
	float LowCoverCrouchedAbsorb = 0.35f;
	/** Prone behind low cover: fully hidden by the Sprint 08 60 cm rule, so it shelters like high cover. */
	float LowCoverProneAbsorb = 0.9f;
	/** Full width of the frontal arc centred on the wall (into it), degrees. */
	float FrontalArcDeg = 160.f;
	/** Cover blind fire (no head exposure): hit chance x this (-40 %). Sprint 08 ghost blind fire is x0.2 on top. */
	float CoverBlindFireAccuracyMultiplier = 0.6f;
	/** Blind shots (cover, ghost, both) never fall below this hit chance after the multipliers. */
	float MinBlindFireHitChance = 0.05f;
	/** Headshot (crit) chance of hits on an operative whose head stays behind the cover. */
	float CoverHeadshotChance = 0.f;
	/** Leaning out of the corner lasts this long after a shot before he ducks back, s. */
	float LeanHoldSeconds = 0.9f;
	/** Blind-firing pose lasts this long after a shot, s. */
	float BlindFireHoldSeconds = 0.6f;
	/** The muzzle moves this far round the corner for a lean / blind shot, cm. */
	float CornerPeekOffsetCm = 60.f;
};

namespace CoverRules
{
	/** The config in force (console tunables Codex.Cover.* override the defaults). */
	CODEXTACTICS_API const FCoverCombatConfig& GetConfig();

	/** Planar angle between the direction into the wall (-normal) and the direction from the slot to Source, degrees. */
	CODEXTACTICS_API float AngleFromWallDeg(const FVector& WallNormal, const FVector& SlotLocation, const FVector& SourceLocation);

	/** Source lies inside the wall's frontal arc (behind the wall from the operative's point of view). */
	CODEXTACTICS_API bool IsInFrontalArc(const FVector& WallNormal, const FVector& SlotLocation, const FVector& SourceLocation,
		float ArcDeg = FCoverCombatConfig().FrontalArcDeg);

	/**
	 * Share of a hit the cover absorbs: 0 outside the arc or while leaning out (the body shows); high cover 0.9; low
	 * cover 0.35 crouched, 0.9 prone, 0 standing. Blind firing keeps the body behind the cover.
	 */
	CODEXTACTICS_API float AbsorbFraction(const FCoverCombatConfig& Config, ECoverHeight Height, EOperativeStance Stance, bool bLeaning, bool bInArc);

	/** Damage left after the cover: Amount x (1 - fraction). */
	CODEXTACTICS_API float ApplyAbsorb(float Amount, float Fraction);

	/** Hit chance of a cover blind shot (-40 %), floored at the minimum. */
	CODEXTACTICS_API float CoverBlindFireHitChance(const FCoverCombatConfig& Config, float BaseHitChance);

	/**
	 * Hit chance with both blind mechanics (user decision 2026-10-06): cover blind fire x0.6 and ghost blind fire
	 * (Sprint 08, GhostMultiplier 0.2) multiply, the result is floored at MinBlindFireHitChance when any applies.
	 */
	CODEXTACTICS_API float CombinedBlindFireHitChance(const FCoverCombatConfig& Config, float BaseHitChance, bool bCoverBlind, bool bGhostBlind,
		float GhostMultiplier);

	/** No headshots (crits) on an operative in cover whose head stays down: in cover, not leaning (blind fire included). */
	CODEXTACTICS_API bool IsHeadshotImmune(ECoverHeight Height, bool bLeaning);

	/** Crit chance of a hit on him: CoverHeadshotChance (0) while immune, else the attacker's. */
	CODEXTACTICS_API float HeadshotChance(const FCoverCombatConfig& Config, float AttackerCritChance, bool bImmune);

	/**
	 * An operative in high cover, not leaning out, is invisible to an observer inside the wall's frontal arc (the wall is
	 * between them whatever the trace says); firing demasks him through the Sprint 08 rule, which the caller checks first.
	 */
	CODEXTACTICS_API bool HiddenFromObserver(ECoverHeight Height, bool bLeaning, bool bObserverInArc);

	/** Wind chill multiplier: FColdConfig::CoverWindChillMultiplier (0.5, the operative cold balance) in cover, 1 otherwise. */
	CODEXTACTICS_API float WindChillMultiplier(const FColdConfig& Config, bool bInCover);

	/** Default stance on entering a cover: high -> standing (the wall covers him), low -> crouched. */
	CODEXTACTICS_API EOperativeStance DefaultStanceFor(ECoverHeight Height);

	/** Muzzle for a corner shot: the slot's muzzle moved CornerPeekOffsetCm round the facing's edge. */
	CODEXTACTICS_API FVector CornerMuzzle(const FCoverSlot& Slot, ECoverFacing Facing, const FVector& Muzzle, float OffsetCm);
}
