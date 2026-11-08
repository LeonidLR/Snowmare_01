#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationPeekCarryRules.h"
#include "Tactics/CoverTypes.h"

/**
 * Run-and-peek (#55, user request 2026-11-07: "the peek works only after hugging the wall first - that badly hurts the pace of the fight; I
 * want the peek at once: the operative is running, gets the order to peek round a corner in front of him, runs straight to that spot and
 * peeks immediately"). A click on the END of a peekable wall (the manual corner peek's end region: its last ~75 cm, its end face or its top
 * there) by an operative who is NOT in cover at that wall - standing, walking, running in the open or in cover at another wall - sends him
 * to the corner's cover slot on the face of that wall that faces him and turns the cover entry straight into the corner peek
 * (AOperativeCharacter::OrderRunCornerPeek). Real time and - since the game designer's answers 2026-11-08 (Q3) - planned in the tactical
 * pause (run there and peek once the pause ends); turn-based keeps its rules. In formation the corner man peeks once the LEADER is at the
 * wall (Q2), not after the whole squad settled. UE-only (Godot reference: none). Pure; tests CodexTactics.Tactics.Cover.RunCornerPeek.*. The runtime (face traces, slots, orders) is
 * ACodexTacticsPlayerController::TryHandleRunCornerPeekClick / FindRunCornerPeekForHit (CodexTacticsPlayerControllerRunPeek.cpp).
 */
namespace RunCornerPeekRules
{
	/** A face narrower than this between its two exposed ends (an end face, a thin post) is no run-and-peek face, cm. */
	inline constexpr float MinFaceWidthCm = 100.f;
	/** Two probed faces whose normals agree this much (dot) are one face. */
	inline constexpr float SameFaceDot = 0.95f;
	/** A cover entry within this of the ordered corner slot (on the same wall) is the run-and-peek's arrival, cm (the arrival tolerance of a cover walk). */
	inline constexpr float ArrivalToleranceCm = 150.f;
	/**
	 * His floor only (the LadderQueueSmoke regression 2026-11-08: a box selection's click on a ROOF beyond a wall ladder's head was taken for
	 * the end of a peekable wall - nobody climbed): a wall's side / end face counts up to about head height above his feet ...
	 */
	inline constexpr float MaxFaceHitAboveFeetCm = 200.f;
	/** ... and no further than this below them; the corner slot's floor within this of his feet (another floor - a roof, the far end of a
	 *  ladder / stairs - is never a run-and-peek), cm. */
	inline constexpr float MaxFloorGapCm = 100.f;

	/** What a click is and who gets it, for the run-and-peek decision. */
	struct FClickFacts
	{
		/** Codex.Cover.RunCornerPeek is on. */
		bool bEnabled = true;
		/** The turn-based grid is active (its own cover rules). */
		bool bTurnBased = false;
		/** The tactical pause plans the orders (they run on the release): since 2026-11-08 (Q3) the run-and-peek is planned there (DecideTiming). */
		bool bPausePlanning = false;
		/** The squad-wide modifier (Alt) is held: Alt keeps its "take cover at that object" meaning. */
		bool bSquadWide = false;
		/** The tutorial has not taught the corner peek yet (PlayerCommands::CoverCornerPeek locked). */
		bool bTutorialLocked = false;
		/** The click hit a body (an operative, an enemy, any pawn). */
		bool bHitPawn = false;
		/** The click hit an object with its own click meaning (barricade / barrel / crate menus, the APC hull, a silhouette, an enemy). */
		bool bHitObject = false;
		/** He is in cover at the clicked object already: the in-cover corner peek / shimmy / wrap of that object decide. */
		bool bInCoverAtClickedObject = false;
		/**
		 * He stands at a wall edge and the clicked cover is a wall-edge cover switch target (CoverSwitchRules: in range, Cover.Switch taught): the
		 * switch keeps its preview + double-click dash there (2026-10-20, approved) - no run-and-peek.
		 */
		bool bCoverSwitchTarget = false;
	};

	/**
	 * The click lies at his own level (LadderQueueSmoke regression, 2026-11-08): never a TOP surface (a wall's top, a building's roof - a
	 * click there is a move / climb order to that level), and a side / end face hit no higher than MaxFaceAboveCm above his feet nor lower
	 * than MaxBelowCm below them. HitAboveFeetCm = the hit point's height over his feet.
	 */
	CODEXTACTICS_API bool IsClickAtHisLevel(bool bTopSurface, float HitAboveFeetCm, float MaxFaceAboveCm = MaxFaceHitAboveFeetCm, float MaxBelowCm = MaxFloorGapCm);

	/** The corner slot stands on his floor: its floor within MaxGapCm of his feet (CornerFloorAboveFeetCm = the slot's floor over his feet). */
	CODEXTACTICS_API bool IsCornerOnHisFloor(float CornerFloorAboveFeetCm, float MaxGapCm = MaxFloorGapCm);

	/** The click may be a run-and-peek (the wall end itself is checked by the traces); the tactical pause plans it (DecideTiming). */
	CODEXTACTICS_API bool MayRunToPeek(const FClickFacts& Facts);

	/**
	 * A face's slot spans at least MinWidthCm between its exposed ends (a closed side counts as wide: the wall goes on past the corner probes).
	 * LeftCm / RightCm = the slot's distance to each exposed end.
	 */
	CODEXTACTICS_API bool IsFaceWideEnough(bool bLeftExposed, float LeftCm, bool bRightExposed, float RightCm, float MinWidthCm = MinFaceWidthCm);

	/** Normal is a face not in Known yet (no known normal within SameFaceDot of it). Planar unit normals. */
	CODEXTACTICS_API bool IsNewFace(const TArray<FVector2D>& Known, const FVector2D& Normal);

	/**
	 * Only cover tall enough to peek round its side is a run-and-peek (the game designer's PIE 2026-11-08, L_Gen_Depot_1's low cubes: the
	 * corner man ran to a low cube's corner, the two others "stacked behind" a cube with no room and stood in the open): high cover (the
	 * corner peek) yes; low cover (fire over it) never - a click there is a plain cover click (a box selection: the group cover over nearby
	 * covers). The face rule then applies to high cover only.
	 */
	CODEXTACTICS_API bool IsRunPeekCover(ECoverHeight Height);

	/** A stack man's place behind the corner man is no cover when it is not on the face (no slot there) or closer than this to the corner slot, cm. */
	inline constexpr float StackMinFromCornerCm = 50.f;

	/**
	 * A stack place behind the corner man is really covered by that object: its slot was found on the same object's face (bSlotOnFace) and
	 * stands at least MinFromCornerCm from the corner man's slot (a block too short for the stack has no such place - that man takes a nearby
	 * cover by the group cover rule instead of standing in the open).
	 */
	CODEXTACTICS_API bool IsStackPlaceCovered(bool bSlotOnFace, float FromCornerSlotCm, float MinFromCornerCm = StackMinFromCornerCm);

	/** One peekable face of the clicked wall at the click (the probe found its slot, the click lies at its end, it is wide enough). */
	struct FFace
	{
		/** The face's outward planar unit normal. */
		FVector2D Normal = FVector2D(1.0, 0.0);
		/** A point on the face (its slot's wall point). */
		FVector2D Point = FVector2D::ZeroVector;
	};

	/**
	 * Design decision 3 (#55): the face of the wall that faces the operative - the one whose normal points most towards him from its point
	 * (dot of the normal with the unit direction to him); a tie (within 0.01) goes to the nearer face point. INDEX_NONE without faces.
	 */
	CODEXTACTICS_API int32 ChooseNearFace(const TArray<FFace>& Faces, const FVector2D& Operative);

	// --- The face under the cursor (the game designer's PIE 2026-11-08: "I want to press to the face I point at; the ghost appears only on
	//     ONE side ... the other sides don't work") ---

	/** The clicked face: a face whose normal agrees at least this much (dot) with the hit's normal ... */
	inline constexpr float ClickedFaceMinDot = 0.5f;
	/** ... and two faces within this of each other's agreement (a hit on the edge between them) go to the one facing the camera more. */
	inline constexpr float ClickedFaceTieDot = 0.05f;

	/**
	 * Supersedes decision 3 when the clicked face has a run-and-peek corner itself: the face UNDER THE CURSOR decides - the face whose normal
	 * agrees most with the hit's normal (HitNormal, planar), at least MinDot; a hit on the edge between two faces (their agreement within
	 * TieDot) takes the one whose normal points most towards the camera (TowardCamera, planar). Any face of the object, also the side away
	 * from him (he paths round to it). INDEX_NONE when no face agrees (the hit face is no run-and-peek face of its own - a wall's narrow end
	 * face: ChooseNearFace over the faces round it, as before).
	 */
	CODEXTACTICS_API int32 ChooseClickedFace(const TArray<FFace>& Faces, const FVector2D& HitNormal, const FVector2D& TowardCamera,
		float MinDot = ClickedFaceMinDot, float TieDot = ClickedFaceTieDot);

	/**
	 * Q1 (kept, narrowed with the face rule): from a wall EDGE, a click that is also a cover-switch target (bSwitchVerdict) stays the switch
	 * only where the switch goes itself - a face on his protected side (ClickedFaceNormal within 60 deg of OwnWallNormal: the switch's corner
	 * in the mirrored pose) at its end nearer to him (ToClickedEndCm <= ToOtherEndCm). Every other face / the far end = the run-and-peek
	 * to the face under the cursor.
	 */
	CODEXTACTICS_API bool KeepsCoverSwitch(bool bSwitchVerdict, const FVector2D& ClickedFaceNormal, const FVector2D& OwnWallNormal, float ToClickedEndCm,
		float ToOtherEndCm);

	/**
	 * In cover at the clicked object, only HIS OWN face is left to the in-cover rules (the corner peek / shimmy at that face); ANOTHER face of
	 * the same block at its end is the run-and-peek to that face's corner (the game designer 2026-11-08: at one end of a long block, the enemy
	 * beyond its far side - dash the short gap to the far face's corner and peek towards them along that face, not out from the near face with
	 * the flank open). bInCoverAtObject: he is in cover at the clicked component; bSameFace: the face found is his cover face.
	 */
	CODEXTACTICS_API bool IsOwnCoverFace(bool bInCoverAtObject, bool bSameFace);

	/**
	 * The click hit a face looking the other way than his cover face (HitNormal planar vs OwnWallNormal, dot <= -0.5; a wall face: |HitNormalZ|
	 * <= 0.3): the far side, never the in-cover peek of his own face (a block 1.5 m thick puts its far face's end right at his own end) - the
	 * run-and-peek to that face takes it. A top hit / his face / an end face (perpendicular) stay as they were.
	 */
	CODEXTACTICS_API bool IsBackFaceHit(const FVector2D& HitNormal, float HitNormalZ, const FVector2D& OwnWallNormal);

	/**
	 * The planar directions the clicked wall is probed from to find its faces round the click: Count directions evenly round the circle,
	 * the first = Primary (the clicked face's normal, or the camera's side for a top hit). Unit vectors; Count < 1 gives Primary alone.
	 */
	CODEXTACTICS_API TArray<FVector2D> ProbeDirections(const FVector2D& Primary, int32 Count = 8);

	/**
	 * A cover entry is the run-and-peek's arrival (the peek starts at once): the run-and-peek is still armed, the entered slot lies on the
	 * ordered corner's wall and within ToleranceCm of the ordered corner slot.
	 */
	CODEXTACTICS_API bool IsPeekArrival(bool bArmed, bool bSameWall, float DistanceFromOrderedCm, float ToleranceCm = ArrivalToleranceCm);

	/** Who carries a run-and-peek order out. */
	enum class EGroup : uint8
	{
		/** The ordered operative alone (solo mode - the primary play mode - or no group around him). */
		Single,
		/** A box selection he belongs to: the member nearest the corner takes it, the others stack behind him (PlanGroupCornerPeek). */
		BoxSelection,
		/** The squad following him in formation: the group wall order at that corner, the squad's corner peek there once it stands at it. */
		Formation,
	};

	/**
	 * The group a run-and-peek click orders: a box selection of 2+ that includes him -> BoxSelection (when the group corner peek is on, else
	 * Single); else in formation (not solo, followers that join a group cover order) -> Formation; else Single.
	 */
	CODEXTACTICS_API EGroup DecideGroup(bool bMultiSelection, bool bOperativeSelected, bool bGroupCornerPeekOn, bool bSoloMode, int32 GroupCoverOthers);

	// --- The game designer's answers 2026-11-08 (#55 Q1-Q6) ---

	/** Q4: a second click on the same corner within this of the first is the double-click (the same order at a sprint), cm. */
	inline constexpr float DoubleClickEdgeCm = 60.f;
	/** Q2: the leader stands within this of his wall slot - he has arrived at the wall, cm. */
	inline constexpr float LeaderAtWallCm = 150.f;

	/**
	 * Q4 (kept): a run-and-peek runs at the normal order speed; only a double-click on the same corner (the second click within MaxSeconds and
	 * MaxPixels of the first, its corner within MaxEdgeCm) is the same order at a sprint. SecondsSinceFirst < 0 = no first click.
	 */
	CODEXTACTICS_API bool IsSprintClick(double SecondsSinceFirst, double MaxSeconds, float PixelsApart, float MaxPixels, float EdgeCmApart, float MaxEdgeCm = DoubleClickEdgeCm);

	/** When the run-and-peek order runs. */
	enum class ETiming : uint8
	{
		/** Real time: he sets off at once. */
		Now,
		/** Q3 (2026-11-08): the tactical pause plans it - the walk to the corner slot is planned (the run-and-peek ghost shows it) and the
		 *  peek on arrival armed; it runs when the pause ends, as the other planned pause orders. */
		OnPauseRelease,
	};

	/** Q3: the tactical pause plans the run-and-peek, real time runs it now (MayRunToPeek decides whether it is one at all). */
	CODEXTACTICS_API ETiming DecideTiming(const FClickFacts& Facts);

	/**
	 * Q2 (2026-11-08): the formation's run-and-peek - the squad's wall order at that corner with the squad's corner peek carried there - fires
	 * the peek as soon as the LEADER is at the wall (bLeaderAtWall: within LeaderAtWallCm of his wall slot, stood still
	 * FormationPeekCarry::LeaderStillSeconds after he set off), not once the whole squad has settled; the others keep forming up behind as
	 * before (the wall leadership stack). bRunPeek false (a group wall order carrying a peek in force, bug_04 A): FormationPeekCarry::Decide
	 * unchanged. The Drop rules (another order / leader, no wall order, too long) are FormationPeekCarry's.
	 */
	CODEXTACTICS_API FormationPeekCarry::EPeekCarryStep DecideFormationCarry(const FormationPeekCarry::FCarryProgressFacts& Facts, bool bRunPeek, bool bLeaderAtWall);

	// --- Straight into the peek stance, no wall hug (the game designer's video 2026-11-08 "08.11.26_InstantLookUp_Bug_1") ---

	/** Formation: the leader's wall place within this of the corner slot - his own run there is the run-and-peek, cm. */
	inline constexpr float LeaderPlaceAtCornerCm = 60.f;
	/** The last stretch of the run to the armed corner slot: within this of it the runner turns to the peek facing, cm. */
	inline constexpr float PreTurnCm = 150.f;
	/** The pre-turn only while he still runs in faster than this (standing at the slot the entry itself decides), cm/s. */
	inline constexpr float PreTurnMinSpeedCmS = 35.f;
	/** The peek stance's leftover turn on the mesh (his facing at the arrival vs the peek facing) eases out over MinSeconds for a small
	 *  leftover up to MaxSeconds for a half turn (YawBlendSeconds). */
	inline constexpr float PeekStanceYawBlendMinSeconds = 0.25f;
	inline constexpr float PeekStanceYawBlendMaxSeconds = 0.6f;

	/**
	 * The runner turns to the peek facing now (instead of facing the wall, the pose the hug's enter clip starts in): the run-and-peek is armed
	 * for the cover slot he walks to, he is within PreTurnDistanceCm of it and still moving faster than MinSpeedCmS. Wherever he comes from
	 * (head-on, along the wall, at an angle) he arrives already looking the way the peek looks.
	 */
	CODEXTACTICS_API bool ShouldPreTurn(bool bArmedForThisSlot, float DistanceToSlotCm, float SpeedCmS, float PreTurnDistanceCm = PreTurnCm,
		float MinSpeedCmS = PreTurnMinSpeedCmS);

	/**
	 * How long the peek stance arrival eases out the leftover turn LeftoverDeg (his facing at the arrival minus the peek facing, any sign / wrap)
	 * on the mesh: MinSeconds up to 30 deg (the pre-turned run: a soft settle), growing linearly to MaxSeconds at 180 deg (no pre-turn - e.g.
	 * he faced a target on the run - still one continuous turn, never a snap).
	 */
	CODEXTACTICS_API float PeekStanceYawBlendSeconds(float LeftoverDeg, float MinSeconds = PeekStanceYawBlendMinSeconds, float MaxSeconds = PeekStanceYawBlendMaxSeconds);

	/** The pose blend counts as having shown its direction once it turned the body this far, deg (HeldTurnGoesTheOtherWay). */
	inline constexpr float BlendDirectionSeenDeg = 3.f;

	/**
	 * The peek stance's held turn (the mesh keeps the run's yaw while the actor already stands back to the wall, released with the fire idle's
	 * weight): HeldDeg and HeldDeg -/+ 360 draw the same first frame, but only the one turning the same way round as the pose blend
	 * (BlendTurnDeg: the shortest arc from the run pose to the fire idle, measured on the pelvis) keeps the drawn body still - the other spins
	 * it a full turn. Returns the equivalent of HeldDeg nearest to BlendTurnDeg.
	 */
	CODEXTACTICS_API float HeldTurnSameWayAsBlend(float HeldDeg, float BlendTurnDeg);

	/**
	 * The guard on that choice (the pack's fire idles stand ~180 deg from their root: a predicted blend turn near 180 can go either way): the
	 * blend has visibly turned the pose by ObservedTurnDeg (at least MinSeenDeg) the other way round than the held turn HeldDeg - flip the held
	 * turn by 360 (a one-frame step of 360 x the weight so far instead of a full spin).
	 */
	CODEXTACTICS_API bool HeldTurnGoesTheOtherWay(float HeldDeg, float ObservedTurnDeg, float MinSeenDeg = BlendDirectionSeenDeg);

	// --- Arrives already turned: no residual turn on the peek entry (#55, the game designer's video Bug_2 2026-11-08: "when he runs up and
	//     peeks out from the wall there seems to be another small extra turn") ---
	//
	// The residual turn was the gap between two DRAWN facings: the run's last frame (the run pose's shoulder line stands ~35-40 deg off the
	// actor's yaw) and the fire idle round the corner (its shoulder line ~35-60 deg off the corner aim, by clip). The 1.5 m pre-turn aimed the
	// ACTOR at the corner aim, so the drawn body still turned ~20 deg after the arrival, inside the 0.3 s blend - and unevenly: the pose blend
	// (per-bone nlerp over a ~160 deg pelvis turn) and the mesh's linearly released held yaw disagree by up to ~5 deg mid-blend. Now the run
	// turns the DRAWN body to the fire idle's drawn facing (BodyTargetActorYaw), earlier and at a planned even rate (PreTurnStartCm /
	// PreTurnStepYaw), and the anim holds the drawn body's yaw through the blend exactly (the peek stance yaw lock: LockTargetYaw / LockAlpha /
	// LockCorrectionDeg - a root yaw on the evaluated pose instead of the mesh's held yaw).

	/** The pre-turn starts at least this far before the entry (the stretch of the first fix), cm ... */
	inline constexpr float PreTurnMinCm = PreTurnCm;
	/** ... or this many seconds of his run before the entry, whichever is further (time-based: a faster run starts it sooner) ... */
	inline constexpr float PreTurnLeadSeconds = 0.6f;
	/** ... but never further than this, cm. */
	inline constexpr float PreTurnMaxCm = 400.f;
	/** The planned pre-turn ends this long before the entry (his last frames of the run already show the final facing), s. */
	inline constexpr float PreTurnFinishEarlySeconds = 0.1f;
	/** The drawn body's yaw: this share of the shoulder line, the rest the hip line (the torso is what the top-down camera reads). */
	inline constexpr float BodyShoulderWeight = 1.f;
	/** The peek stance yaw lock lets go over this long after its path (the fire idle's own facing from then on), s. */
	inline constexpr float LockFadeSeconds = 0.15f;
	/**
	 * The fire idle in the game draws the body a little off its clip (the graph's layers - the left-hand IK pulls the left shoulder: +6 deg
	 * measured on both standing pack clips 2026-11-08): the lock learns that bias at the end of its path (up to MaxDrawnBiasDeg) for the next
	 * arrival's pre-turn, and a gap over SettleMinDeg is eased out over SettleSeconds instead of the short fade.
	 */
	inline constexpr float MaxDrawnBiasDeg = 20.f;
	inline constexpr float SettleMinDeg = 1.f;
	inline constexpr float SettleSeconds = 0.4f;
	/** The bias the standing fire idles start with (measured 2026-11-08: _L +6.2, _R +5.7 deg), so the first arrival is already right. */
	inline constexpr float StandDrawnBiasSeedDeg = 6.f;

	/** Where the pre-turn starts: max(MinCm, Speed x LeadSeconds) up to MaxCm, plus EntryLeadCm (the run enters that far short of the slot), cm. */
	CODEXTACTICS_API float PreTurnStartCm(float SpeedCmS, float EntryLeadCm, float MinCm = PreTurnMinCm, float LeadSeconds = PreTurnLeadSeconds,
		float MaxCm = PreTurnMaxCm);

	/**
	 * One frame of the planned pre-turn: the yaw moves towards TargetYaw at the even rate that gets there FinishEarlySeconds before the entry
	 * (DistanceToEntryCm away at SpeedCmS) - never past it, no snap; once the planned end is reached it closes what is left. Returns the yaw.
	 */
	CODEXTACTICS_API float PreTurnStepYaw(float CurrentYaw, float TargetYaw, float DistanceToEntryCm, float SpeedCmS, float DeltaSeconds,
		float FinishEarlySeconds = PreTurnFinishEarlySeconds);

	/**
	 * The pre-turn turns one way only: InOutSign (0 = not yet) takes the way of its first real step (over 0.05 deg); a later step the other
	 * way is not taken (the yaw holds). Returns the yaw to set.
	 */
	CODEXTACTICS_API float OneWayStepYaw(float CurrentYaw, float SteppedYaw, int32& InOutSign);

	/**
	 * The actor yaw that draws the run's body where the fire idle will draw it: the cover facing he will get (FacingYaw) plus the fire idle's
	 * drawn body yaw (FinalBodyYawCs, the mesh's component space) minus the run pose's now (RunBodyYawCs). The mesh's own yaw on the capsule
	 * cancels out. Normalised, deg.
	 */
	CODEXTACTICS_API float BodyTargetActorYaw(float FacingYaw, float FinalBodyYawCs, float RunBodyYawCs);

	/** The yaw a body line (the left bone -> the right bone, X / Y) is drawn facing: its normal, forward +X, right +Y (shoulders / hips), deg. */
	CODEXTACTICS_API float BodyLineYaw(const FVector2D& Left, const FVector2D& Right);

	/** The drawn body's yaw from its shoulder and hip lines: HipYaw + ShoulderWeight x (ShoulderYaw - HipYaw), the short way, deg. */
	CODEXTACTICS_API float MixBodyYaw(float ShoulderYaw, float HipYaw, float ShoulderWeight = BodyShoulderWeight);

	/**
	 * The peek stance yaw lock's drawn body yaw Seconds after the entry: from EntryYaw (the run's last frame) to FinalYaw (the fire idle's own)
	 * eased over PathSeconds the short way (the pre-turned run: EntryYaw ~ FinalYaw - the body keeps its facing), deg.
	 */
	CODEXTACTICS_API float LockTargetYaw(float EntryYaw, float FinalYaw, float Seconds, float PathSeconds);

	/** The lock's strength: 1 over its path, then down to 0 over FadeSeconds (the pose's own facing - by then ~ the target - takes over). */
	CODEXTACTICS_API float LockAlpha(float Seconds, float PathSeconds, float FadeSeconds = LockFadeSeconds);

	/**
	 * The root yaw that draws the evaluated pose's body (PoseBodyYaw) at TargetYaw, times Alpha, deg. The pose blend turns the body ~160 deg
	 * from the run to the fire idle; this counter-turns exactly what it has turned so far (either way round: it is the same rotation), so the
	 * drawn yaw follows LockTargetYaw frame by frame - no nlerp wobble, no frame of lag.
	 */
	CODEXTACTICS_API float LockCorrectionDeg(float TargetYaw, float PoseBodyYaw, float Alpha);
}
