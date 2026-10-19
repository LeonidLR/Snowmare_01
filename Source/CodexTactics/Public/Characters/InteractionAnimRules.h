#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "InteractionAnimRules.generated.h"

/**
 * Interaction clips of an operative (game designer 2026-10-15; UE-only, no Godot reference). Clips: the user's Polygonmaker pack
 * /Game/Polygonmaker/Animations/Interactions/UE4/, montages made by Scripts/Editor/setup_operative_interaction_animation.py.
 */
UENUM(BlueprintType)
enum class EOperativeInteractionAnim : uint8
{
	None,
	/** Opens a loot crate (Anim_Regular_chest_open); the loot window opens at its effect moment (the lid is open). */
	ChestOpen,
	/** Rummages in the crate after Take / Take All in the loot window (Anim_Regular_chest_search); the items move at its grab moment. */
	ChestSearch,
	/** Picks the disarmed charge up after a successful mine / tripwire disarm (Anim_Regular_pickup_ground); the trap goes at its grab moment. */
	PickupGround
};

/** Where an interaction clip plays on the body. */
UENUM(BlueprintType)
enum class EInteractionAnimLayer : uint8
{
	/** No clip (prone: a prone clip will be chosen later). */
	None,
	/** Standing: the whole body (FullBody slot). */
	FullBody,
	/** Crouched: the upper-body layer (layered blend from spine_01), the legs keep the crouch pose. */
	UpperBody
};

/** How an interaction runs now (InteractionAnimRules::GetDispatch). */
UENUM(BlueprintType)
enum class EInteractionClipDispatch : uint8
{
	/** The clip plays; the effect lands at its effect moment. */
	PlayClip,
	/** No clip for the stance (prone): the effect lands without a clip (a crate waits its short fallback). */
	NoClip,
	/** Tactical pause: queued - the clip plays when the pause is released, the effect after it. */
	WaitForRelease
};

/** Pure rules of the interaction clips (tests: CodexTactics.Characters.InteractionAnim.*). */
namespace InteractionAnimRules
{
	/** Standing -> FullBody, crouched -> UpperBody, prone -> None. */
	CODEXTACTICS_API EInteractionAnimLayer ChooseLayer(EOperativeStance Stance);

	/**
	 * Seconds from the clip's (re)start to its effect moment (rev 2026-10-19, game designer: the animation FIRST, then the effect):
	 * EffectFraction of the whole clip (0..1: the lid is open / the hand grabs / the charge is lifted), counted from where the clip
	 * started (StartSeconds into it), at PlayRate. A clip started past that moment fires at once (0).
	 */
	CODEXTACTICS_API float GetEffectDelay(float ClipLengthSeconds, float StartSeconds, float PlayRate, float EffectFraction);

	/**
	 * Seconds from the start of the chest-open clip to the loot window: the clip's effect moment (GetEffectDelay); no clip played
	 * (prone, none assigned) -> the crate's own FallbackSeconds (Godot 1.8 s).
	 */
	CODEXTACTICS_API float GetLootWindowDelay(float EffectDelaySeconds, bool bClipPlayed, float FallbackSeconds);

	/**
	 * How an interaction (crate open / search, the pickup after a disarm) runs now: in the tactical pause it waits for the release
	 * (time stands still - the clip plays and the effect lands once time runs again); without a clip for the stance (prone) the effect
	 * lands without a clip; else the clip plays and the effect lands at its effect moment.
	 */
	CODEXTACTICS_API EInteractionClipDispatch GetDispatch(bool bTimeFrozen, EInteractionAnimLayer Layer);

	/**
	 * The search in a crate reached its grab moment: the items move only when the collector is still there to take them (alive, not
	 * knocked down, within ReachCm of the crate); otherwise the search was interrupted and nothing moves.
	 */
	CODEXTACTICS_API bool ShouldCompleteSearch(bool bCollectorAlive, bool bKnockedDown, float DistanceToCrateCm, float ReachCm);

	/** Interaction clips play only while time runs (not in the tactical pause: a frozen clip would hold the body until the release). */
	CODEXTACTICS_API bool ShouldPlayClip(bool bTimeFrozen, EInteractionAnimLayer Layer);
}
