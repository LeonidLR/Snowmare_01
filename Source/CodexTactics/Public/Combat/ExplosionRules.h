#pragma once

#include "CoreMinimal.h"

class FJsonObject;
struct FWeaponExplosionStats;

/**
 * The shared explosion model's pure part (the weapons framework, 2026-11-09; docs/design/WEAPONS_FRAMEWORK.md 16): one falloff family for every
 * blast (the grenade's half-at-the-edge curve, the placed charges' linear curve, a weapon block's full core + edge multiplier), the cover rule
 * (a wall between cuts the blast; a plunging round ignores walls and only a roof protects), the RPG-7 back-blast cone, and the model-wide
 * settings from weapons_tuning.json "explosions". The world part is Explosions::Detonate (Combat/Explosion.h). UE-only.
 * Tested in CodexTactics.Weapons.Explosion.*.
 */
enum class EBlastFalloff : uint8
{
	/** Godot grenade.gd: 1 - d / R x 0.5, clamped 0.5..1 (half damage at the edge). */
	HalfAtEdge,
	/** Godot deployables (mines, tripwires, barrels, trapped objects): 1 - d / R x Factor. */
	Linear,
	/** A weapon block (FWeaponExplosionStats): full inside FullRadiusCm, then a straight line to EdgeMultiplier at the radius. */
	FullCore
};

/** How a blast's damage falls off with the distance. */
struct CODEXTACTICS_API FBlastFalloff
{
	EBlastFalloff Model = EBlastFalloff::Linear;
	/** Linear: the share lost at the edge (0.4 = 60 % left at the radius). */
	float Factor = 0.4f;
	/** FullCore: the full-damage radius, cm. */
	float FullRadiusCm = 0.f;
	/** FullCore: the multiplier at the radius. */
	float EdgeMultiplier = 1.f;
};

/** The cover rule's multipliers. */
struct CODEXTACTICS_API FBlastOcclusionConfig
{
	/** A wall between the blast and the body (not for plunging rounds). */
	float WallMultiplier = 0.25f;
	/** A roof / ceiling above the body against a plunging round (high-angle fire). */
	float RoofMultiplier = 0.f;
};

/** weapons_tuning.json "explosions". */
struct CODEXTACTICS_API FExplosionConfig
{
	FBlastOcclusionConfig Occlusion;
	/** Camera trauma added by a blast at the camera's focus (falls off over 4 radii); 0 = no shake (as before 2026-11-09). */
	float CameraTrauma = 0.f;
	/** Per blast kind (GameplayEvents::ExplosionKinds names + "projectile"): is the cover rule applied? Missing = off (the legacy kinds). */
	TMap<FName, bool> OcclusionByKind;

	bool UsesOcclusion(FName Kind) const
	{
		const bool* Found = OcclusionByKind.Find(Kind);
		return Found && *Found;
	}
};

namespace ExplosionRules
{
	/** The blast kind every projectile explosion uses ("projectile"; occlusion on by default). */
	CODEXTACTICS_API FName ProjectileKind();

	/** The falloff multiplier at DistanceCm; 0 outside the radius (the edge itself counts). */
	CODEXTACTICS_API float Falloff(const FBlastFalloff& Curve, float DistanceCm, float RadiusCm);

	/** Base x falloff x Scale x CoverMultiplier (0 outside the radius). */
	CODEXTACTICS_API float VictimDamage(float Base, const FBlastFalloff& Curve, float DistanceCm, float RadiusCm, float Scale = 1.f, float CoverMultiplier = 1.f);

	/**
	 * The cover rule: a normal blast is cut by a wall between (WallMultiplier) - a roof above does not matter; a plunging round (high-angle) is
	 * not stopped by walls or side cover - only a roof above protects (RoofMultiplier). 1 = no cover.
	 */
	CODEXTACTICS_API float CoverMultiplier(bool bPlunging, bool bWallBetween, bool bRoofAbove, const FBlastOcclusionConfig& Config);

	/** A weapon block's curve (FullCore; radius 0 = no blast). */
	CODEXTACTICS_API FBlastFalloff FromWeapon(const FWeaponExplosionStats& Stats);

	/**
	 * The RPG-7's back-blast danger zone: 1 at the launcher, falling to 0 at LengthCm along the cone behind the shooter (the axis = -AimDir,
	 * HalfAngleDeg wide); 0 outside the cone or with no back-blast (length 0).
	 */
	CODEXTACTICS_API float BackblastMultiplier(const FVector& Launcher, const FVector& AimDir, const FVector& Point, float LengthCm, float HalfAngleDeg);

	/** The camera trauma a blast adds at DistanceCm from the camera's focus (linear to 0 at 4 radii). */
	CODEXTACTICS_API float CameraTrauma(float BaseTrauma, float DistanceCm, float RadiusCm);

	/** Reads "explosions" over the defaults (occlusion on for projectiles); bad values keep the default with a line in OutErrors. */
	CODEXTACTICS_API FExplosionConfig Parse(const FJsonObject& Block, TArray<FString>* OutErrors = nullptr);
	CODEXTACTICS_API FExplosionConfig Defaults();
	CODEXTACTICS_API void Set(const FExplosionConfig& Config);
	CODEXTACTICS_API const FExplosionConfig& Get();
}
