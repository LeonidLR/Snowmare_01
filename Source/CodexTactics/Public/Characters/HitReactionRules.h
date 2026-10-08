#pragma once

#include "CoreMinimal.h"

/**
 * Pure rules of the operative hit reaction (user request 2026-10-08; Godot locomotion_controller.gd play_hit_reaction
 * played one on every hit). Used by UOperativeAnimInstance::HandleHealthChanged in real time and turn-based (the grid
 * hits go through AOperativeCharacter::TakeHit too). Tested in CodexTactics.Anim.HitReaction.*.
 */
namespace HitReactionRules
{
	/** What the operative is doing when the hit lands. */
	struct FHitReactionContext
	{
		/** Health lost by this hit. */
		float Damage = 0.f;
		/** Game seconds since the last reaction started (large when none). */
		float SecondsSinceLast = 1000.f;
		bool bInCover = false;
		/** Knocked down, dead, throwing a grenade, reloading or mid-stance-change: the body is busy. */
		bool bBusy = false;
	};

	struct FHitReactionSettings
	{
		/** At most one reaction per this many game seconds (a burst / shotgun pellets play one). */
		float MinIntervalSeconds = 0.8f;
		/** Hits below this damage (chip damage, cold ticks) play nothing. */
		float MinDamage = 1.f;
		/** In cover the M4 cover clips own the body: no clip unless allowed (user rule: skip, cover keeps its pose). */
		bool bAllowInCover = false;
	};

	/** A reaction clip should start now. */
	CODEXTACTICS_API bool ShouldReact(const FHitReactionSettings& Settings, const FHitReactionContext& Context);

	/** The hit came from behind him (the planar angle between his forward and the source is over 90 degrees). */
	CODEXTACTICS_API bool IsFromBehind(const FVector& ActorForward, const FVector& ActorLocation, const FVector& SourceLocation);
}
