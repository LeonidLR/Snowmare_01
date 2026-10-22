#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "Environment/EnvironmentTypes.h"

/**
 * Blizzard walk (game designer 2026-10-22; UE-only, no Godot reference): outdoors in a blizzard a walking operative shields
 * himself against the wind - the storm clips of /Game/90+_Weather_Animations (NEW_version/Storm, retargeted by
 * Scripts/Editor/setup_operative_storm_animation.py) play on the upper body (the ABP's UpperBodySlot, layered from spine_01),
 * the legs keep the locomotion. UOperativeAnimInstance (OperativeAnimInstanceBlizzardWalk.cpp) feeds these pure rules every frame.
 * Tests: CodexTactics.Characters.BlizzardWalk.*
 */

/** Why the blizzard walk layer is off (the first reason that applies; None = it plays). */
enum class EBlizzardWalkBlock : uint8
{
	None,
	/** Not a blizzard (or the blizzard amount is below the enter / exit threshold). */
	NoBlizzard,
	/** Indoors: a Closed / Shelter camera zone or a heat source's warm zone. */
	Sheltered,
	/** Dead, knocked down. */
	Down,
	/** Crouched or prone (the clips are a standing walk). */
	Stance,
	/** Standing still / creeping below the walk threshold. */
	NotWalking,
	/** Sprinting / running faster than the jog limit. */
	TooFast,
	/** Aiming, a shot just fired, an aim target held (combat aim) or reloading. */
	Aiming,
	/** In cover, vaulting, carrying a body. */
	Busy,
	/** Another clip owns the upper body (hit reaction, grenade throw, interaction, working device) or a full-body clip plays
	 *  (stance transition, weather idle, sniper pose). */
	OtherClip
};

/** Everything the gate reads (one frame of one operative). */
struct FBlizzardWalkInput
{
	/** 0..1: how much of the current weather is a blizzard (GetBlizzardAmount; the 45 s transitions blend it). */
	float BlizzardAmount = 0.f;
	bool bSheltered = false;
	/** Ground speed, cm/s. */
	float Speed = 0.f;
	EOperativeStance Stance = EOperativeStance::Standing;
	bool bSprinting = false;
	bool bAiming = false;
	bool bHasAimTarget = false;
	bool bReloading = false;
	bool bInCover = false;
	bool bVaulting = false;
	bool bCarrying = false;
	bool bDead = false;
	bool bKnockedDown = false;
	/** Another upper-body montage (fire, reload, hit, grenade, interaction, working device) is playing. */
	bool bOtherUpperBodyClip = false;
	/** A full-body clip covers the body (weather idle, stance transition, sniper pose, vault, knockdown...). */
	bool bFullBodyClip = false;
};

/** The thresholds (UOperativeAnimInstance class defaults). */
struct FBlizzardWalkTuning
{
	/** Starts walking-in-the-storm above this speed, cm/s ... */
	float StartSpeed = 40.f;
	/** ... and keeps it down to this one (hysteresis: formation followers creep at 10-25 cm/s). */
	float StopSpeed = 20.f;
	/** Above this ground speed (a run; the standing walk is 220, the sprint 725) the layer is off. */
	float MaxSpeed = 450.f;
	/** The layer starts once the blizzard amount reaches this ... */
	float EnterAmount = 0.5f;
	/** ... and ends below this one (a weather transition does not flicker it). */
	float ExitAmount = 0.35f;
};

namespace BlizzardWalkRules
{
	/**
	 * How much of the weather is a blizzard: the current weather fading out over the transition, the next one fading in
	 * (Weather == Blizzard ? 1 - Blend : 0) + (Next == Blizzard ? Blend : 0), clamped to 0..1.
	 */
	CODEXTACTICS_API float GetBlizzardAmount(EWeatherState Weather, EWeatherState NextWeather, float TransitionBlend);

	/** The first reason the layer is off; bActive = it plays now (the speed / amount hysteresis keeps it). */
	CODEXTACTICS_API EBlizzardWalkBlock GetBlock(const FBlizzardWalkInput& Input, const FBlizzardWalkTuning& Tuning, bool bActive);

	/** Target weight of the storm upper body: the blizzard amount when nothing blocks it, else 0. */
	CODEXTACTICS_API float GetTargetWeight(const FBlizzardWalkInput& Input, const FBlizzardWalkTuning& Tuning, bool bActive);

	/** The block's name for logs / smokes. */
	CODEXTACTICS_API const TCHAR* BlockName(EBlizzardWalkBlock Block);
}
