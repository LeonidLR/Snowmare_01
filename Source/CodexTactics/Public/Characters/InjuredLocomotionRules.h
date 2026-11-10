#pragma once

#include "CoreMinimal.h"

/**
 * The injured locomotion (the game designer 2026-11-10: "operatives sometimes walk very slowly and I cannot see why - give a wounded operative a
 * visible INJURED walk"; UE-only, Godot had none). A native layer of UOperativeAnimInstance (OperativeAnimInstanceInjured.cpp) plays the
 * designer's /Game/CON_Injured clips (weapon_anim_sets.json "injured"; IK-retargeted copies /Game/Characters/Animations/Injured/{Male,Female} by
 * Scripts/Editor/setup_injured_animation.py) on the LOWER body (root, pelvis, legs) over ABP_Operative - the torso stays the ABP's / the weapon
 * layer's (the weapon pose, the aim, the blizzard shield, fire / reload clips).
 *
 * - WOUNDED (AOperativeCharacter::IsWounded: HP < MovementConfig.WoundedHealthThreshold, the speed x0.65 and no sprint): standing, the 8-direction
 *   injured walk (Walk_Injured_BS clips, blended by the move direction relative to the body, phase-matched) and the injured idle; prone, the
 *   prone crawl layer plays the injured crawl forward (Crawl_Injured + its idle; the other directions keep the mocap crawl).
 * - COLD (the designer 2026-11-10): Freezing ("cold_from" 0.7) while WALKING plays the 8-direction injured walk (~0.6x at the freezing speed);
 *   Frostbite ("cold_slow_from" 0.9) the slow injured walk (Walk_Injured_Slow, a forward clip: by a forward cone, the injured walk outside it,
 *   ~1.4x); standing still the layer is off - the existing shiver idle (the warm-idle sets' Cold_1) plays. It replaces the empty ColdWalkClips
 *   slots of the ABP's cold layer for the 70 / 90 tiers.
 * - Wounded takes priority over cold.
 * Tests CodexTactics.Animation.Injured.*; smoke CodexTactics.InjuredLocomotionSmoke. Console A/B: Codex.Anim.InjuredLayer (0 = off).
 */
namespace InjuredLocomotionRules
{
	/** The data triggers ("triggers" in weapon_anim_sets.json "injured"). */
	struct FInjuredTriggers
	{
		/** "wounded": IsWounded turns the injured walk on. */
		bool bWounded = true;
		/** "cold_from": the cold share (0..1) from which a walking body plays the injured walk (Freezing); <= 0 or absent = off. */
		float ColdFrom = 0.f;
		/** "cold_slow_from": the cold share from which it plays the slow injured walk instead (Frostbite); <= 0 or absent = off. */
		float ColdSlowFrom = 0.f;
	};

	/** The gait the layer plays. */
	enum class EInjuredGait : uint8
	{
		/** Not hurt: the layer is off (the ABP alone). */
		None,
		/** Wounded: the 8-direction injured walk + the injured idle (prone: the injured crawl). */
		Wounded,
		/** Freezing: the 8-direction injured walk while walking; idle = the ABP's shiver. */
		Cold,
		/** Frostbite: the slow injured walk while walking forward (the injured walk outside the cone); idle = the ABP's shiver. */
		ColdSlow
	};

	/** A "cold_from" value as read (a share 0..1; a percent 1..100 is taken as a percent); anything else = off (0). */
	CODEXTACTICS_API float SanitizeColdFrom(double Value);

	/** The gait: wounded (trigger on) first, then frostbite (cold_slow_from on and ColdShare >= it), then freezing (cold_from), else none. */
	CODEXTACTICS_API EInjuredGait ChooseGait(const FInjuredTriggers& Triggers, bool bWounded, float ColdShare);

	/**
	 * The slow walk's share by the move direction relative to the body (deg, 0 = straight ahead): 1 within FullDeg, 0 from ZeroDeg on, linear
	 * between (the clip walks forward only).
	 */
	CODEXTACTICS_API float ForwardConeShare(float RelativeDeg, float FullDeg, float ZeroDeg);

	/**
	 * The standing layer's weight target: off when not eligible / no gait; wounded = 1 (idle and walk are the layer's); cold (freezing /
	 * frostbite) = the move share (standing still stays the ABP's: the shiver idle).
	 */
	CODEXTACTICS_API float LayerTarget(EInjuredGait Gait, bool bEligible, float MoveShare);

	/** The slow walk's share of the layer's walk: frostbite = the forward cone share (the injured walk outside it), else 0. */
	CODEXTACTICS_API float SlowWalkShare(EInjuredGait Gait, float ConeShare);

	/**
	 * A direction blend's alpha with a clip band (deg, "direction_clip_band_deg"): within BandDeg of either clip's direction (GapDeg apart) the
	 * blend is that clip alone, the rest is remapped linearly - a walk a few degrees off a clip (path following wobbles +-3..7 deg) plays that
	 * clip pure instead of mixing in the neighbour's different stance (the strafes' pivoting feet slid ~10 points more when mixed).
	 */
	CODEXTACTICS_API float BandedAlpha(float Alpha, float BandDeg, float GapDeg = 45.f);

	/** The prone crawl plays the injured crawl (wounded, prone; the crawl set has the injured clips). */
	CODEXTACTICS_API bool UsesInjuredCrawl(EInjuredGait Gait, bool bProne, bool bHasInjuredCrawl);

	/**
	 * How much of the injured pelvis's rotation the torso rides (0 = the torso keeps the ABP's mesh-space rotation exactly - the weapon pose and
	 * the aim; "torso_pelvis_share" = the limp shows in the shoulders): 0 while aiming, the share clamped to [0, 1] otherwise.
	 */
	CODEXTACTICS_API float TorsoPelvisShare(float Share, bool bAiming);
}
