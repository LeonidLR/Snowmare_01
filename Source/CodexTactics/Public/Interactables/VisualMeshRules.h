#pragma once

#include "CoreMinimal.h"

/**
 * 2026-10-21 (Demo 01 lake quest): where the visual mesh of a box-shaped gameplay object sits. Interactables and the blast gate's
 * leaves draw the engine cube (100 cm) stretched to fill their collision box; an art mesh (the burnt BTR-82, a door leaf model)
 * keeps its own proportions at an authored transform relative to the box instead. Pure, unit-tested (CodexTactics.Interactables.VisualMesh).
 * UE-only (Godot drew primitives sized in the scene).
 */
namespace VisualMeshRules
{
	/** Size of the engine cube /Engine/BasicShapes/Cube, cm. */
	constexpr float PlaceholderCubeCm = 100.f;

	/**
	 * Relative transform of the visual mesh inside a box of BoxSizeCm (full size, not the half extent): the placeholder cube is
	 * centred and scaled to fill the box; an art mesh (bArtMesh) uses ArtTransform unchanged (never stretched).
	 */
	CODEXTACTICS_API FTransform ResolveTransform(bool bArtMesh, const FVector& BoxSizeCm, const FTransform& ArtTransform);

	/** The right blast-gate leaf's art transform: the left one's mirrored across the gate's centre line (local Y). */
	CODEXTACTICS_API FTransform MirrorAcrossY(const FTransform& LeftTransform);
}
