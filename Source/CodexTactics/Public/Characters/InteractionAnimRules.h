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
	/** Opens a loot crate (Anim_Regular_chest_open); the loot window opens when it ends. */
	ChestOpen,
	/** Rummages in the crate after Take / Take All in the loot window (Anim_Regular_chest_search), cosmetic. */
	ChestSearch,
	/** Picks the disarmed charge up from the ground after a successful mine / tripwire disarm (Anim_Regular_pickup_ground). */
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

/** Pure rules of the interaction clips (tests: CodexTactics.Characters.InteractionAnim.*). */
namespace InteractionAnimRules
{
	/** Standing -> FullBody, crouched -> UpperBody, prone -> None. */
	CODEXTACTICS_API EInteractionAnimLayer ChooseLayer(EOperativeStance Stance);

	/**
	 * Seconds from the click on a crate to the loot window: the chest-open clip plays out first (it ends ClipSeconds after it
	 * started; the window opens when it starts to blend out, BlendOutSeconds before its end); without a clip (prone, none assigned)
	 * the crate's own FallbackSeconds; in the tactical pause (time stands still) at once - the window is planning, not acting.
	 */
	CODEXTACTICS_API float GetLootWindowDelay(float ClipSeconds, float BlendOutSeconds, float FallbackSeconds, bool bTimeFrozen);

	/** Interaction clips play only while time runs (not in the tactical pause: a frozen clip would hold the body until the release). */
	CODEXTACTICS_API bool ShouldPlayClip(bool bTimeFrozen, EInteractionAnimLayer Layer);
}
