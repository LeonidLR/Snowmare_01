#pragma once

#include "CoreMinimal.h"

/**
 * Left-hand IK onto the rifle's handguard (user-approved plan 2026-10-07; UE-only, no Godot reference). The weapon is a
 * static mesh rigid on hand_r, its offset tuned for the RifleAnims locomotion; the M4 cover pack's clips hold their own
 * M4 differently, so their left hand ends 38-42 cm off our handguard. The weapon mesh's `LeftHandGrip` socket (added by
 * Scripts/Editor/add_weapon_left_hand_socket.py) marks where the left hand holds it; UOperativeAnimInstance turns it into
 * an effector in hand_r bone space for the ABP's Two Bone IK on hand_l. Pure rules, tested in
 * CodexTactics.Anim.LeftHandIK.*.
 */
struct CODEXTACTICS_API FLeftHandIKState
{
	bool bEnabled = true;
	/** The weapon has the grip socket and is attached to the right hand. */
	bool bHasGrip = false;
	bool bWeaponVisible = true;
	bool bReloading = false;
	/** A grenade throw / hit reaction / melee clip has the arms (upper body). */
	bool bUpperBodyAction = false;
	bool bVaulting = false;
	bool bDead = false;
	/** Prone: the crawl clips put the left hand on the ground (measured 49 cm off the grip); the prone aim already holds it (8-9 cm). */
	bool bProne = false;
};

namespace LeftHandIKRules
{
	/**
	 * The grip in the right hand's bone space: the socket's transform in weapon-mesh space composed with the weapon
	 * component's transform relative to hand_r (its attach parent).
	 */
	CODEXTACTICS_API FTransform GripInHandSpace(const FTransform& WeaponRelativeToHand, const FTransform& SocketInWeapon);

	/** The IK is wanted (alpha target 1) — else 0. */
	CODEXTACTICS_API bool WantsIK(const FLeftHandIKState& State);

	/** Alpha eased towards the target over BlendSeconds (linear step, smoothstep shaped by the caller's read if needed). */
	CODEXTACTICS_API float StepAlpha(float Current, bool bWanted, float DeltaSeconds, float BlendSeconds);
}
