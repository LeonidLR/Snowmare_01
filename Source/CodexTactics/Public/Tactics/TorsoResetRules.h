#pragma once

#include "CoreMinimal.h"

/**
 * Torso drift (G025). A look, a cover lean and a climb leave yaw on the mesh, the barrel and the aim offset.
 * Every exit (neutral look, mantle end, cover exit, a player's move order) puts that yaw back on the move.
 * The formation scan pitch is not part of the drift: a move that still has a formation look keeps it.
 * Tests: CodexTactics.Tactics.Torso.Reset.
 */
namespace TorsoResetRules
{
	/** What an exit writes. Mesh and barrel yaw go to 0. The controller stops owning the rotation. */
	struct FRelease
	{
		float MeshYawDeg = 0.f;
		float BarrelYawDeg = 0.f;
		bool bControllerDesiredRotation = false;
		/** False while a formation look is still set: that pitch is the scan, not a leftover aim. */
		bool bClearAimPitch = true;
	};

	/** The pose after an exit. bKeepAimPitch leaves the formation scan pitch alone. */
	CODEXTACTICS_API FRelease AfterExit(bool bKeepAimPitch);

	/**
	 * True when an exponential ease of ErrorDeg at InterpSpeed (1/s) is within LimitDeg after Seconds.
	 * The aim-offset interp and the barrel ease both use this shape. A snapped offset (error 0) is settled.
	 */
	CODEXTACTICS_API bool Settled(float ErrorDeg, float InterpSpeed, float Seconds, float LimitDeg);
}
