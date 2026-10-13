#pragma once

#include "CoreMinimal.h"

/**
 * Vision cones on the ground (user request 2026-10-11: restore the Godot feature for operatives AND enemies, switchable
 * from the editor). Godot reference: Scenes/movements/player.gd _setup_vision_cone / _rebuild_vision_cone_mesh /
 * _update_vision_cone / _is_any_enemy_spotted (operatives only: a flat sector fan of 32 segments + an outline, 8 cm above
 * the floor, range vision_cone_range 14 m = the M16 range then, FULL angle vision_cone_angle 70 deg, unshaded alpha
 * material, green (0.12, 0.88, 0.35) clear / red (0.95, 0.18, 0.18) when an enemy is spotted, fill alpha 0.22, line alpha
 * 0.85, colour lerp delta x 14; toggled only by GameBalanceConfig.show_vision_cones, off in the shipped config).
 * Godot enemies had no perception and no cone; the port draws theirs from AEnemyCharacter's perception (sight range,
 * field-of-view half angle) with a state colour (idle / suspicious / alert).
 * Pure rules, tested in CodexTactics.Combat.VisionCone.*; UVisionConeComponent draws them.
 */

/** What a cone shows (its colour). */
enum class EVisionConeState : uint8
{
	/** Operative: no enemy in its cone (Godot green). */
	Clear,
	/** Operative: an enemy in its cone or a combat target (Godot red). */
	Spotted,
	/** Enemy on patrol / guard, nothing noticed. */
	Idle,
	/** Enemy whose suspicion meter is filling, or searching after a trap. */
	Suspicious,
	/** Enemy engaged with the squad (hunting / fighting). */
	Alert
};

/** Triangle geometry of a cone in the component's local space (+X forward, +Z up). */
struct CODEXTACTICS_API FVisionConeMesh
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
};

namespace VisionConeRules
{
	/** Godot vision_cone_range, cm (also the M16 range of that time). */
	inline constexpr float GodotRangeCm = 1400.f;
	/** Godot vision_cone_angle 70 deg is the FULL angle: +-35 deg. */
	inline constexpr float GodotHalfAngleDeg = 35.f;
	/** Godot: 32 segments. */
	inline constexpr int32 GodotSegments = 32;
	/** Godot: the cone root sits 0.92 m below the 1 m player origin = 8 cm above the floor. */
	inline constexpr float GodotHeightCm = 8.f;
	inline constexpr float GodotFillAlpha = 0.22f;
	inline constexpr float GodotLineAlpha = 0.85f;
	/** Godot colour lerp speed (lerp(target, delta * 14)). */
	inline constexpr float GodotColorLerpSpeed = 14.f;

	/** Segment count clamped to 3..128. */
	CODEXTACTICS_API int32 ClampSegments(int32 Segments);
	/** Half angle clamped to 1..180 deg (180 = all round). */
	CODEXTACTICS_API float ClampHalfAngle(float HalfAngleDeg);

	/** Point on the arc at AngleDeg from forward (+X), RangeCm out, HeightCm up. Positive angles turn right (+Y). */
	CODEXTACTICS_API FVector ArcPoint(float RangeCm, float AngleDeg, float HeightCm = 0.f);

	/**
	 * Filled sector: vertex 0 = the apex, then Segments + 1 arc points from -HalfAngle to +HalfAngle; Segments triangles,
	 * each emitted in both windings (visible from above and below with any material). Empty for RangeCm <= 0.
	 */
	CODEXTACTICS_API FVisionConeMesh BuildFill(float RangeCm, float HalfAngleDeg, int32 Segments, float HeightCm = 0.f);

	/**
	 * Outline (Godot line strip apex -> arc -> apex) as a flat band WidthCm wide, inside the sector edge: both side edges
	 * (unless the cone is all round) and the arc. Both windings. Empty for RangeCm <= 0.
	 */
	CODEXTACTICS_API FVisionConeMesh BuildOutline(float RangeCm, float HalfAngleDeg, int32 Segments, float WidthCm, float HeightCm = 0.f);

	/** Target inside the cone (planar): within RangeCm of Origin and within HalfAngleDeg of Forward. */
	CODEXTACTICS_API bool IsInsideCone(const FVector& Origin, const FVector& Forward, const FVector& Target, float RangeCm, float HalfAngleDeg);

	/**
	 * Whether the cone is drawn. GlobalMode = console Codex.Debug.VisionCones: 0 every cone off, 1 every cone on, anything
	 * else (-1, the default) the actor's own bShowVisionCone, or bPlayerToggle (the player's enemy-cone toggle, O / pause menu,
	 * user decision 2026-10-13: Commandos style; enemies pass it, operatives pass false). Never for a dead / dying character.
	 */
	CODEXTACTICS_API bool ResolveVisible(int32 GlobalMode, bool bActorSetting, bool bAlive, bool bPlayerToggle = false);

	/** Enemy cone state: engaged -> Alert; on patrol / guard: searching or suspicion > 0 -> Suspicious, else Idle. */
	CODEXTACTICS_API EVisionConeState GetEnemyState(bool bOnPatrol, bool bSearching, float Suspicion);

	/** Colour (RGB, alpha 1) of a state; Suspicious blends from the idle colour by Suspicion (0..1), searching = full. */
	CODEXTACTICS_API FLinearColor GetStateColor(EVisionConeState State, float Suspicion = 1.f);

	/** Colour step towards Target (Godot lerp(Target, clamp(Delta * Speed, 0, 1))). */
	CODEXTACTICS_API FLinearColor StepColor(const FLinearColor& Current, const FLinearColor& Target, float DeltaSeconds,
		float Speed = GodotColorLerpSpeed);
}
