// Dev-only headless check of the RUN-AND-PEEK (#55, user request 2026-11-07: "the peek works only after hugging the wall first - I want it at
// once: the operative is running, gets the order to peek round a corner in front of him, runs straight to that spot and peeks immediately"):
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands RunCornerPeekSmoke
//   (map /Game/Maps/L_FormationLab, "-LevelJson=L_FormationLab.json" - Scripts/test_map.json smokeOptions; the lab's walls are laid out by
//   the level JSON at known places, nothing is spawned or saved)
// The lab wall W_B_3 (x 1500..4000 at y -2700, 40 cm thick: its south face y -2720, its north face y -2680), solo mode:
//   A. the leader RUNNING in the open ~12 m south of it; the hover + ONE click on its west end -> the peek ghost is his, he runs on to the
//      south face's west corner slot and is in the corner-peek pose within RunEntryToPoseMaxSeconds of the cover entry (no second click);
//   B. from the open, a click on the face's MIDDLE = the cover ghost (take cover; no run-and-peek), its confirm = cover there, no peek;
//   C. from that cover, a click on the EAST end = the old in-cover peek (he side-steps along the wall in cover, no run-and-peek);
//   D. the leader NORTH of the wall, the click on the SOUTH face's west end -> since the face rule (the game designer 2026-11-08: "I want to
//      press to the face I point at") the corner on the clicked SOUTH face - he runs round the wall's end to it (before: the north face);
//   E. all three box-selected out in the open -> the man nearest the west end takes the corner and peeks, the others in cover behind him;
//   F. (the game designer's answer Q2, 2026-11-08) the squad in FORMATION in the open, the followers far behind the leader: the click on the
//      west end = the squad's wall order there, its corner peek fires as soon as the LEADER is at the wall - while a follower is still on his
//      way - and the corner man peeks round the west end;
//   G. (answer Q3) solo, a fight, the TACTICAL PAUSE: the hover over the west end shows the run-and-peek ghost (no cover ghost), the click
//      plans "run there and peek" (nobody moves in the pause); the pause's end -> he runs there and peeks on arrival.
//   H. (the face under the cursor, the game designer's PIE 2026-11-08) a free-standing 1.5 x 1.5 m block south of W_B_3, the leader in cover
//      at W_B_3's middle: the real hover + click on each of its four faces in turn (near one end) -> the ghost and the arrival are on the
//      CLICKED face's corner (its slot's wall normal = the face's), the far south face included (he runs round the block).
//   I. (low cover, the game designer's PIE 2026-11-08 on L_Gen_Depot_1's low cubes) three LOW cubes (90 cm) in a row, all three
//      box-selected north of them: the real click on the middle cube's end is NO run-and-peek (no peek ghost, nobody armed) - a plain
//      cover click (the cover ghost), its confirm = the group cover: every man ends in cover (or prone as the fallback), nobody stands in
//      the open.
// No wall hug (the game designer's video 2026-11-08 "08.11.26_InstantLookUp_Bug_1": "he runs to the edge of the wall, presses his back to it,
// and only then turns and peeks - like a stage play"): in A, D, E, F and G the peeker's arrival goes STRAIGHT into the peek stance - no
// cover enter clip, no cover idle / look-around loop, no idle -> fire step-out on him, no frame in cover without the peek pose, the peek pose and
// its fire idle within ArrivalToPeekMaxSeconds of the entry, and he arrives facing within ArrivalFacingMaxDeg of the peek facing (the corner
// stance's aim) - the run's last metres turned him there.
// Arrives already turned (the game designer's video Bug_2 2026-11-08: "when he runs up and peeks out from the wall there seems to be another
// small extra turn" - the first fix still turned the drawn body 20-25 deg after the arrival): the drawn body (the shoulder line, and the hip
// line) turns less than ArrivalBodyTurnMaxDeg in all from his last frame of the run to 1 s in the peek, and over his last
// ApproachMonotonicCm to the slot it turns one way only (back-and-forth within ApproachBacktrackMaxDeg).
// -PreCmds "Codex.Cover.RunPeekYawLock 0" = the first fix's arrival (those Bug_2 checks fail: the residual turn).
// A/B: -PreCmds "Codex.Cover.RunCornerPeek 0" = the old rule (A, D and E fail: the click is a cover ghost);
// -PreCmds "Codex.Cover.RunPeekStance 0" = the 2026-11-07 arrival (the no-hug checks fail: the enter clip, then the step-out).
// "Shot" (rendered, fixed step: UnrealEditor.exe <uproject> /Game/Maps/L_FormationLab -game -RenderOffscreen -ResX=1280 -ResY=720
// -UseFixedTimeStep -FPS=60 "-LevelJson=L_FormationLab.json" "-ExecCmds=CodexTactics.RunCornerPeekSmoke Shot") saves a frame strip of A's
// arrival to Saved/Screenshots/RunPeekNoHug2/runpeek_<lock|nolock|old>_side_<i>.png (a camera beside the west corner, every 0.1 s from
// his last 2.5 m); "ShotTop" the same from straight above (runpeek_<..>_top_<i>.png).
// Godot reference: none (UE-only; docs/systems/cover-and-peek.md "Run-and-peek").

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Animation/AnimMontage.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Characters/EnemyCharacter.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Tactics/CoverTraceRules.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/HUD.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Tactics/CoverGhostActor.h"
#include "UnrealClient.h"

namespace RunCornerPeekSmoke
{
	/** W_B_3 (Content/Data/FormationLab/formation_lab_01.json): x 1500..4000, 40 cm thick round y -2700. */
	constexpr float WallWestX = 1500.f;
	constexpr float WallEastX = 4000.f;
	constexpr float SouthFaceY = -2720.f;
	constexpr float NorthFaceY = -2680.f;
	/** A: the leader's start in the open and his run (north-west, towards the wall's west part). */
	const FVector2D RunStart(2500.f, -3750.f);
	const FVector2D RunGoal(1300.f, -3200.f);
	/** A / D / E: the click lands this many seconds into his run. */
	constexpr float ClickIntoRunSeconds = 0.6f;
	/** The click on an end: this far in from it along the face, cm. */
	constexpr float EndInsetCm = 20.f;
	/** A-D: the two others wait here in the open (within the 25 m solo leash of every stage, out of the way). */
	const FVector2D ParkA(3300.f, -3400.f);
	const FVector2D ParkB(3500.f, -3400.f);
	/** B: the face's middle (away from both ends). */
	constexpr float MiddleX = 2750.f;
	/** D: north of the wall. */
	const FVector2D NorthStart(2300.f, -1900.f);
	const FVector2D NorthGoal(1800.f, -2300.f);
	/** E: the box selection's starts in the open (the leader in the middle). */
	constexpr float BoxStartY = -3800.f;
	const float BoxStartX[3] = { 2600.f, 2150.f, 3050.f };
	/** The run-and-peek click lands 8..15 m ahead of the runner (the user's case). */
	constexpr float ClickMinCm = 800.f;
	constexpr float ClickMaxCm = 1500.f;
	/** "No idle pause on arrival": the corner-peek pose within this of the cover entry, s. */
	constexpr float RunEntryToPoseMaxSeconds = 1.0f;
	/** He holds the pose this long before a stage passes, s. */
	constexpr float HoldSeconds = 1.f;
	/** A stage's give-up time, s. */
	constexpr float GiveUpSeconds = 25.f;
	/** At the corner: the body within this of the wall's end along it, cm. */
	constexpr float AtEndCm = 130.f;
	/** F: the leader's start in the open ~6 m from the west end, the followers ~10 m behind him (still walking when he arrives). */
	const FVector2D FormationLeaderStart(2100.f, -3250.f);
	const FVector2D FormationFollowerStart[2] = { FVector2D(2300.f, -4300.f), FVector2D(2550.f, -4400.f) };
	/** F: a follower counts as still on his way above this planar speed, cm/s. */
	constexpr float StillWalkingSpeed = 30.f;
	/** G: the leader in the open within the pause's order radius of the west end. */
	const FVector2D PauseStart(2000.f, -3300.f);
	/** No wall hug: the peek pose and its fire idle within this of the cover entry, s. */
	constexpr float ArrivalToPeekMaxSeconds = 0.3f;
	/**
	 * He arrives facing roughly the peek's way (the corner stance's aim), deg. Since Bug_2 the run turns his DRAWN body to the fire idle's
	 * drawn facing, so the actor's own yaw stands 20-45 deg off the corner aim by the run pose (a sprint's torso twists more): a sanity bound.
	 */
	constexpr float ArrivalFacingMaxDeg = 60.f;
	/**
	 * No residual turn (Bug_2 2026-11-08; the first fix measured 20-25, a spin through the blend ~340): the drawn body - the shoulder line -
	 * turns less than this in all (the sum of its frame-to-frame turns) from his last frame of the run to 1 s in the peek, deg ...
	 */
	constexpr float ArrivalBodyTurnMaxDeg = 10.f;
	/**
	 * ... and the hips (the hip line) less than this: they also carry the legs' stop out of the stride and the run's hip twist, which no turn
	 * of the whole body can take out (measured 8-35 deg on the arrival - a sprint's stop dips 15 deg and back; a spin reads ~340), deg.
	 */
	constexpr float ArrivalHipTurnMaxDeg = 60.f;
	/** Over his last this-many cm to the slot the drawn body (shoulder line) turns one way only ... */
	constexpr float ApproachMonotonicCm = 150.f;
	/** ... its back-and-forth (the turn against the net direction) at most this much, deg. */
	constexpr float ApproachBacktrackMaxDeg = 2.f;
	/** "Shot": the strip's stills, every this many s from his last StripFromCm to the corner. */
	constexpr float StripEverySeconds = 0.1f;
	constexpr int32 StripStills = 18;
	constexpr float StripFromCm = 250.f;
	/** H: the free-standing block (the face rule): its centre, half size, height; the click this far from a face's centre towards one end. */
	const FVector2D BlockCentre(2750.f, -3500.f);
	constexpr float BlockHalfCm = 75.f;
	constexpr float BlockHeightCm = 250.f;
	constexpr float BlockClickAlongCm = 40.f;
	/** H: the leader's cover at W_B_3's middle (south face) before each click. */
	constexpr float HomeCoverX = 2750.f;
	/** I: three low cubes (fire-over cover) in a row along x at LowCubeY, LowCubeHalfCm half size, LowCubeHeightCm high; the selection north. */
	const float LowCubeX[3] = { 3400.f, 3700.f, 4000.f };
	constexpr float LowCubeY = -4600.f;
	constexpr float LowCubeHalfCm = 60.f;
	constexpr float LowCubeHeightCm = 90.f;
	const FVector2D LowCubeStart[3] = { FVector2D(3700.f, -4000.f), FVector2D(3550.f, -4050.f), FVector2D(3850.f, -4050.f) };

	/** The body yaw trace keeps this many frames before the entry (his last ~2.5 s of the run). */
	constexpr int32 TraceFramesBefore = 150;

	/** What one montage on an operative plays (by his AnimInstance's cover clip lists). */
	enum class EClipKind : uint8
	{
		CoverEnter,
		StepOut,
		FireIdle,
		CoverIdle,
		CornerLoop,
		Other,
	};

	const TCHAR* KindName(EClipKind Kind)
	{
		switch (Kind)
		{
		case EClipKind::CoverEnter: return TEXT("cover enter (the HUG)");
		case EClipKind::StepOut: return TEXT("idle->fire step-out (the TURN OUT)");
		case EClipKind::FireIdle: return TEXT("fire idle = the peek stance");
		case EClipKind::CoverIdle: return TEXT("cover idle (back to the wall)");
		case EClipKind::CornerLoop: return TEXT("corner look-around loop");
		default: return TEXT("other");
		}
	}

	EClipKind Classify(const UOperativeAnimInstance& Anim, const UAnimSequenceBase* Clip)
	{
		const auto In = [Clip](const TArray<TObjectPtr<UAnimSequenceBase>>& Clips) { return Clip && Clips.Contains(Clip); };
		if (In(Anim.CoverStandEnter) || In(Anim.CoverCrouchEnter) || In(Anim.CoverStandSwitchEnter) || In(Anim.CoverCrouchSwitchEnter))
		{
			return EClipKind::CoverEnter;
		}
		if (In(Anim.CoverStandFireEnter) || In(Anim.CoverCrouchFireEnter))
		{
			return EClipKind::StepOut;
		}
		if (In(Anim.CoverStandFireIdle) || In(Anim.CoverCrouchFireIdle))
		{
			return EClipKind::FireIdle;
		}
		if (In(Anim.CoverStandIdle) || In(Anim.CoverCrouchIdle))
		{
			return EClipKind::CoverIdle;
		}
		if (In(Anim.CoverStandCorner) || In(Anim.CoverCrouchCorner))
		{
			return EClipKind::CornerLoop;
		}
		return EClipKind::Other;
	}

	const UAnimSequenceBase* MontageClip(const UAnimMontage* Montage)
	{
		if (Montage)
		{
			for (const FSlotAnimationTrack& Track : Montage->SlotAnimTracks)
			{
				if (Track.AnimTrack.AnimSegments.Num() > 0)
				{
					return Track.AnimTrack.AnimSegments[0].GetAnimReference();
				}
			}
		}
		return nullptr;
	}

	/** The yaw a body line (Left -> Right bone) is drawn facing on screen: its normal (the mesh + its animation), deg; false without the bones. */
	bool LineYaw(const AOperativeCharacter& Op, const TCHAR* Left, const TCHAR* Right, float& OutYaw)
	{
		const USkeletalMeshComponent* Mesh = Op.GetMesh();
		if (!Mesh || Mesh->GetBoneIndex(Left) == INDEX_NONE || Mesh->GetBoneIndex(Right) == INDEX_NONE)
		{
			return false;
		}
		const FVector Line = Mesh->GetSocketLocation(Right) - Mesh->GetSocketLocation(Left);
		if (Line.Size2D() < 5.f)
		{
			return false;
		}
		OutYaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(-Line.X, Line.Y)));
		return true;
	}

	/** The body's yaw drawn on screen: the shoulder line's normal, deg. */
	bool BodyYaw(const AOperativeCharacter& Op, float& OutYaw)
	{
		return LineYaw(Op, TEXT("upperarm_l"), TEXT("upperarm_r"), OutYaw);
	}

	/** The hips' yaw drawn on screen: the hip line's normal (thigh_l -> thigh_r), deg. */
	bool HipYaw(const AOperativeCharacter& Op, float& OutYaw)
	{
		return LineYaw(Op, TEXT("thigh_l"), TEXT("thigh_r"), OutYaw);
	}

	/** The sum of the frame-to-frame turns of a yaw trace from From on, deg. */
	float TotalTurn(const TArray<float>& Trace, int32 From)
	{
		float Turn = 0.f;
		for (int32 Sample = FMath::Max(From, 1); Sample < Trace.Num(); ++Sample)
		{
			Turn += FMath::Abs(FRotator::NormalizeAxis(Trace[Sample] - Trace[Sample - 1]));
		}
		return Turn;
	}

	/** One operative's arrival as the stage watches it from the click on (the no-hug checks). */
	struct FArrivalWatch
	{
		bool bWasInCover = false;
		/** His actor yaw in the last frame out of cover (the facing he arrived with), deg. */
		float OpenYaw = 0.f;
		float ArrivalYaw = 0.f;
		/** Stage times (-1 = not yet): the cover entry, the peek pose, the fire idle montage start. */
		float EntryAt = -1.f;
		float PoseAt = -1.f;
		float FireIdleAt = -1.f;
		/** Hug clips started on him from the click on (cover enter / cover idle / look-around loop), step-outs. */
		int32 HugClips = 0;
		int32 StepOuts = 0;
		/** Frames in cover before the peek pose was first reached (the hug sub-state). */
		int32 HugFrames = 0;
		/** The peek facing (the corner stance's aim, GetStanceAimBaseDirection in the pose), deg; valid once PoseAt >= 0. */
		float PeekYaw = 0.f;
		/** The visual body (shoulder line) yaw trace from TraceFramesBefore frames before the entry to 1 s after it, deg; the hip line's; where. */
		TArray<float> BodyTrace;
		TArray<float> HipTrace;
		TArray<FVector2D> TraceAt;
		/** BodyTrace's first sample in cover (INDEX_NONE before the entry). */
		int32 EntryTraceIndex = INDEX_NONE;
		FString Clips;
		TSet<int32> SeenMontages;
	};

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		float GroundZ = 0.f;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		/** The click waiting for the world tick: the face point x and the face it is traced on. */
		bool bClickPending = false;
		float ClickX = 0.f;
		bool bHoverToo = false;
		/** The last click's results (the world tick fills them). */
		bool bLastGhost = false;
		TWeakObjectPtr<const AOperativeCharacter> LastGhostOf;
		bool bClicked = false;
		/** A run-and-peek stage: when he entered the cover / reached the pose (stage time, -1 = not yet), the hold. */
		float EntryAt = -1.f;
		float PoseAt = -1.f;
		float HoldTime = 0.f;
		int32 RunPeekCountBefore = 0;
		bool bLeftCover = false;
		TWeakObjectPtr<AOperativeCharacter> Expected;
		/** F: the carried peek's count before, when it fired, whether a follower was still on his way then, the leader's slot distance then. */
		int32 CarryCountBefore = 0;
		float CarryFiredAt = -1.f;
		bool bFollowerWalkingAtFire = false;
		FString AtFire;
		int32 PlannedBefore = 0;
		/** The no-hug watch of every member from the stage's click on. */
		TArray<FArrivalWatch> Watch;
		/** H: the block, the face being clicked (0 north, 1 east, 2 south, 3 west), its sub-step, the click traced from / to. */
		TWeakObjectPtr<AActor> Block;
		TArray<TWeakObjectPtr<AActor>> LowCubes;
		int32 FaceIndex = 0;
		int32 FaceStep = 0;
		bool bCustomClick = false;
		FVector ClickFrom = FVector::ZeroVector;
		FVector ClickTo = FVector::ZeroVector;
		FVector2D LastGhostNormal = FVector2D::ZeroVector;
		/** "Shot": the frame strip of A's arrival ("ShotTop": from straight above). */
		bool bShot = false;
		bool bShotTop = false;
		bool bStanceOn = true;
		bool bStripStarted = false;
		int32 StripIndex = 0;
		float NextStripTime = 0.f;
		TWeakObjectPtr<ACameraActor> Camera;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("RunCornerPeekSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		State.EntryAt = -1.f;
		State.PoseAt = -1.f;
		State.HoldTime = 0.f;
		State.bLeftCover = false;
		State.bClicked = false;
		State.Watch.Reset();
		State.Watch.SetNum(State.Members.Num());
	}

	/** Every frame after the stage's click: each member's arrival (the montages started on him, his facing, the hug frames). */
	void UpdateWatch(FState& State)
	{
		if (!State.bClicked)
		{
			return;
		}
		for (int32 Index = 0; Index < State.Members.Num() && Index < State.Watch.Num(); ++Index)
		{
			const AOperativeCharacter* Op = State.Members[Index].Get();
			if (!Op)
			{
				continue;
			}
			FArrivalWatch& W = State.Watch[Index];
			const bool bIn = Op->bInCover;
			if (bIn && !W.bWasInCover && W.EntryAt < 0.f)
			{
				W.EntryAt = State.StageTime;
				W.ArrivalYaw = W.OpenYaw;
				W.EntryTraceIndex = W.BodyTrace.Num();
			}
			if (!bIn)
			{
				W.OpenYaw = Op->GetActorRotation().Yaw;
			}
			W.bWasInCover = bIn;
			const bool bPose = Op->bInCover && Op->IsManualCornerPeek() && Op->IsCornerAimActive() && Op->bAtCoverCorner && !Op->bShimmying;
			if (W.EntryAt >= 0.f && W.PoseAt < 0.f && bIn && !bPose)
			{
				++W.HugFrames;
			}
			if (bPose && W.PoseAt < 0.f)
			{
				W.PoseAt = State.StageTime;
				W.PeekYaw = static_cast<float>(Op->GetStanceAimBaseDirection().Rotation().Yaw);
			}
			if (const UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr)
			{
				for (const FAnimMontageInstance* Instance : Anim->MontageInstances)
				{
					if (!Instance || !Instance->Montage || W.SeenMontages.Contains(Instance->GetInstanceID()))
					{
						continue;
					}
					W.SeenMontages.Add(Instance->GetInstanceID());
					const UAnimSequenceBase* Clip = MontageClip(Instance->Montage);
					const EClipKind Kind = Classify(*Anim, Clip);
					W.HugClips += Kind == EClipKind::CoverEnter || Kind == EClipKind::CoverIdle || Kind == EClipKind::CornerLoop ? 1 : 0;
					W.StepOuts += Kind == EClipKind::StepOut ? 1 : 0;
					if (Kind == EClipKind::FireIdle && W.FireIdleAt < 0.f)
					{
						W.FireIdleAt = State.StageTime;
					}
					W.Clips += FString::Printf(TEXT(" %.2fs %s [%s];"), State.StageTime, Clip ? *Clip->GetName() : TEXT("-"), KindName(Kind));
				}
			}
			float Body = 0.f;
			float Hips = 0.f;
			const bool bTraceWindow = W.EntryAt < 0.f || State.StageTime - W.EntryAt <= 1.f;
			if (bTraceWindow && BodyYaw(*Op, Body) && HipYaw(*Op, Hips))
			{
				W.BodyTrace.Add(Body);
				W.HipTrace.Add(Hips);
				W.TraceAt.Add(FVector2D(Op->GetActorLocation()));
				if (W.EntryTraceIndex == INDEX_NONE && W.BodyTrace.Num() > TraceFramesBefore)
				{
					W.BodyTrace.RemoveAt(0); // only the last frames before the entry
					W.HipTrace.RemoveAt(0);
					W.TraceAt.RemoveAt(0);
				}
			}
		}
	}

	/** The no-hug checks of the stage's peeker (the game designer's video 2026-11-08). */
	void CheckNoHug(FState& State, const AOperativeCharacter& Peeker, const TCHAR* Label)
	{
		const int32 Index = State.Members.IndexOfByPredicate([&Peeker](const TWeakObjectPtr<AOperativeCharacter>& Each) { return Each.Get() == &Peeker; });
		if (!State.Watch.IsValidIndex(Index))
		{
			Check(State, false, FString::Printf(TEXT("%s: %s watched"), Label, *Peeker.DisplayName.ToString()));
			return;
		}
		const FArrivalWatch& W = State.Watch[Index];
		const float ToPose = W.EntryAt >= 0.f && W.PoseAt >= 0.f ? W.PoseAt - W.EntryAt : -1.f;
		const float ToFireIdle = W.EntryAt >= 0.f && W.FireIdleAt >= 0.f ? W.FireIdleAt - W.EntryAt : -1.f;
		const float FacingOff = W.PoseAt >= 0.f ? FMath::Abs(FRotator::NormalizeAxis(W.ArrivalYaw - W.PeekYaw)) : -1.f;
		// The traces from his last ApproachMonotonicCm to the slot on (the slot: where he stands in the peek), relative to the peek facing.
		const FVector2D SlotAt = FVector2D(Peeker.GetActorLocation());
		int32 ApproachFrom = W.EntryTraceIndex;
		for (int32 Sample = 0; Sample < W.EntryTraceIndex && Sample < W.TraceAt.Num(); ++Sample)
		{
			if (FVector2D::Distance(W.TraceAt[Sample], SlotAt) <= ApproachMonotonicCm)
			{
				ApproachFrom = Sample;
				break;
			}
		}
		FString Trace;
		FString HipLine;
		float Lo = 1e9f;
		float Hi = -1e9f;
		for (int32 Sample = FMath::Max(ApproachFrom, 0); Sample < W.BodyTrace.Num(); ++Sample)
		{
			const float Rel = FRotator::NormalizeAxis(W.BodyTrace[Sample] - W.PeekYaw);
			Trace += FString::Printf(TEXT("%s%.0f"), Sample == W.EntryTraceIndex ? TEXT(" |") : TEXT(" "), Rel);
			HipLine += FString::Printf(TEXT("%s%.0f"), Sample == W.EntryTraceIndex ? TEXT(" |") : TEXT(" "), FRotator::NormalizeAxis(W.HipTrace[Sample] - W.PeekYaw));
			Lo = FMath::Min(Lo, Rel);
			Hi = FMath::Max(Hi, Rel);
		}
		const UOperativeAnimInstance* Anim = Peeker.GetMesh() ? Cast<UOperativeAnimInstance>(Peeker.GetMesh()->GetAnimInstance()) : nullptr;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke metric: %s no-hug: entry %.2f s, pose +%.2f s, fire idle +%.2f s, hug frames %d, hug clips %d, step-outs %d, arrival facing %.0f deg off the peek facing (yaw %.0f vs %.0f; the operative's own %.0f), peek stance entries %d, anim straight blends %d; clips:%s"),
			Label, W.EntryAt, ToPose, ToFireIdle, W.HugFrames, W.HugClips, W.StepOuts, FacingOff, W.ArrivalYaw, W.PeekYaw, Peeker.GetLastPeekStanceArrivalOffDeg(),
			Peeker.GetPeekStanceEntryCount(), Anim ? Anim->GetCoverPeekStanceBlends() : -1, *W.Clips);
		// The drawn body's turn in all from his last frame of the run on (a spin through the blend shows as ~340 deg, the first fix's residual
		// turn 20-25), the shoulder line and the hip line each.
		const bool bHasEntry = W.EntryTraceIndex > 0 && W.EntryTraceIndex < W.BodyTrace.Num();
		const float BodyTurn = bHasEntry ? TotalTurn(W.BodyTrace, W.EntryTraceIndex) : -1.f;
		const float HipTurn = bHasEntry ? TotalTurn(W.HipTrace, W.EntryTraceIndex) : -1.f;
		// His last ApproachMonotonicCm: one way only (the turn against the net direction = (all - net) / 2).
		float ApproachTurn = -1.f;
		float ApproachNet = 0.f;
		float Backtrack = -1.f;
		if (bHasEntry && ApproachFrom >= 0 && ApproachFrom < W.EntryTraceIndex)
		{
			ApproachTurn = 0.f;
			for (int32 Sample = ApproachFrom + 1; Sample < W.EntryTraceIndex; ++Sample)
			{
				const float Step = FRotator::NormalizeAxis(W.BodyTrace[Sample] - W.BodyTrace[Sample - 1]);
				ApproachTurn += FMath::Abs(Step);
				ApproachNet += Step;
			}
			Backtrack = 0.5f * (ApproachTurn - FMath::Abs(ApproachNet));
		}
		const UOperativeAnimInstance* LockAnim = Peeker.GetMesh() ? Cast<UOperativeAnimInstance>(Peeker.GetMesh()->GetAnimInstance()) : nullptr;
		// The fire idle's drawn body as the pre-turn / the lock predicted it from the clip vs the pose on the mesh now (1 s in, the lock gone).
		float PredictedCs = 0.f;
		float NowCs = 0.f;
		if (LockAnim && LockAnim->GetPeekStanceBodyYawCs(Peeker.CoverFacing, Peeker.GetStance() == EOperativeStance::Crouching, PredictedCs) && LockAnim->GetMeshBodyYawCs(NowCs))
		{
			float At05 = 0.f;
			float At10 = 0.f;
			LockAnim->GetPeekStanceBodyYawCs(Peeker.CoverFacing, Peeker.GetStance() == EOperativeStance::Crouching, At05, 0.5f);
			LockAnim->GetPeekStanceBodyYawCs(Peeker.CoverFacing, Peeker.GetStance() == EOperativeStance::Crouching, At10, 1.0f);
			FString RootInfo;
			const TArray<TObjectPtr<UAnimSequenceBase>>& Idles = Peeker.GetStance() == EOperativeStance::Crouching ? LockAnim->CoverCrouchFireIdle : LockAnim->CoverStandFireIdle;
			const int32 IdleIndex = Peeker.CoverFacing == ECoverFacing::Right ? 0 : 1;
			if (const UAnimSequence* Idle = Idles.IsValidIndex(IdleIndex) ? Cast<UAnimSequence>(Idles[IdleIndex].Get()) : nullptr)
			{
				FTransform Root;
				Idle->GetBoneTransform(Root, FSkeletonPoseBoneIndex(0), FAnimExtractContext(0.0), false);
				RootInfo = FString::Printf(TEXT("%s root yaw %.1f at (%.0f, %.0f), root motion %d lock %d"), *Idle->GetName(), Root.Rotator().Yaw, Root.GetLocation().X,
					Root.GetLocation().Y, Idle->bEnableRootMotion ? 1 : 0, static_cast<int32>(Idle->RootMotionRootLock.GetValue()));
			}
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke metric: %s the fire idle's drawn body (component space): predicted from the clip %.1f (at 0.5 s %.1f, 1 s %.1f), on the mesh now %.1f (lock on %d; aim pitch %.1f, aim yaw %.1f, left-hand IK %.2f; %s)"),
				Label, PredictedCs, At05, At10, NowCs, LockAnim->IsPeekStanceYawLockActive() ? 1 : 0, LockAnim->AimPitch, LockAnim->AimYaw, LockAnim->LeftHandIKAlpha, *RootInfo);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke metric: %s body (shoulder line) yaw vs the peek facing, per frame, his last %.0f cm (%d frames) | 1 s in cover: [%s ] (range %.0f..%.0f; the approach turned %.0f deg net, %.0f in all, back-and-forth %.1f; from the entry %.1f deg in all)"),
			Label, ApproachMonotonicCm, W.EntryTraceIndex - ApproachFrom, *Trace, Lo, Hi, ApproachNet, ApproachTurn, Backtrack, BodyTurn);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke metric: %s hips (hip line) yaw vs the peek facing: [%s ] (from the entry %.1f deg in all); yaw lock %d (locks %d, the gap it eased out %.0f deg)"),
			Label, *HipLine, HipTurn, AOperativeCharacter::IsRunPeekYawLockEnabled() ? 1 : 0, LockAnim ? LockAnim->GetPeekStanceYawLocks() : -1, Peeker.GetLastPeekStanceBodyGapDeg());
		Check(State, BodyTurn >= 0.f && BodyTurn < ArrivalBodyTurnMaxDeg, FString::Printf(TEXT("%s: no extra turn of the drawn body after reaching the slot (shoulders %.1f deg in all, < %.0f)"),
			Label, BodyTurn, ArrivalBodyTurnMaxDeg));
		Check(State, HipTurn >= 0.f && HipTurn < ArrivalHipTurnMaxDeg, FString::Printf(TEXT("%s: no spin of the hips after reaching the slot (%.1f deg in all, < %.0f)"),
			Label, HipTurn, ArrivalHipTurnMaxDeg));
		Check(State, Backtrack >= 0.f && Backtrack <= ApproachBacktrackMaxDeg, FString::Printf(TEXT("%s: over his last %.0f cm the drawn body turns one way (net %.0f deg, back-and-forth %.1f <= %.0f)"),
			Label, ApproachMonotonicCm, ApproachNet, Backtrack, ApproachBacktrackMaxDeg));
		Check(State, W.HugClips == 0 && W.StepOuts == 0, FString::Printf(TEXT("%s: no wall hug - no cover enter / back-to-the-wall idle / look-around clip and no idle->fire step-out on %s (hug clips %d, step-outs %d)"),
			Label, *Peeker.DisplayName.ToString(), W.HugClips, W.StepOuts));
		Check(State, W.EntryAt >= 0.f && W.HugFrames == 0, FString::Printf(TEXT("%s: no frame in cover without the peek pose (%d)"), Label, W.HugFrames));
		Check(State, ToPose >= 0.f && ToPose <= ArrivalToPeekMaxSeconds && ToFireIdle >= 0.f && ToFireIdle <= ArrivalToPeekMaxSeconds,
			FString::Printf(TEXT("%s: the peek pose (+%.2f s) and its fire idle (+%.2f s) within %.1f s of reaching the slot"), Label, ToPose, ToFireIdle, ArrivalToPeekMaxSeconds));
		Check(State, FacingOff >= 0.f && FacingOff <= ArrivalFacingMaxDeg, FString::Printf(TEXT("%s: he arrives facing the peek's way (%.0f deg off, <= %.0f)"), Label, FacingOff,
			ArrivalFacingMaxDeg));
	}

	void PlaceCamera(UWorld* World, FState& State, const FVector& Eye, const FVector& LookAt)
	{
		if (!State.Camera.IsValid())
		{
			State.Camera = World->SpawnActor<ACameraActor>(Eye, (LookAt - Eye).Rotation());
			if (ACameraActor* Created = State.Camera.Get())
			{
				Created->GetCameraComponent()->SetFieldOfView(55.f);
				Created->GetCameraComponent()->bConstrainAspectRatio = false;
			}
		}
		if (ACameraActor* Cam = State.Camera.Get())
		{
			Cam->SetActorLocationAndRotation(Eye, (LookAt - Eye).Rotation());
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
			{
				PC->SetViewTarget(Cam);
				if (AHUD* Hud = PC->GetHUD())
				{
					Hud->bShowHUD = false;
				}
			}
		}
	}

	/** "Shot": A's arrival as a frame strip from a camera beside the west corner (south-west of it, looking at the corner). */
	void TakeStripStill(UWorld* World, FState& State, const AOperativeCharacter& Op)
	{
		const FVector Corner(WallWestX, SouthFaceY, State.GroundZ + 100.f);
		if (!State.bShot || State.StripIndex >= StripStills)
		{
			return;
		}
		if (!State.bStripStarted && (Op.bInCover || FVector::Dist2D(Op.GetActorLocation(), Corner) <= StripFromCm))
		{
			State.bStripStarted = true;
			State.NextStripTime = State.StageTime;
		}
		if (!State.bStripStarted || State.StageTime < State.NextStripTime)
		{
			return;
		}
		if (State.bShotTop)
		{
			PlaceCamera(World, State, Corner + FVector(-90.f, -150.f, 720.f), Corner + FVector(-80.f, -120.f, -100.f)); // straight above the corner slot
		}
		else
		{
			PlaceCamera(World, State, Corner + FVector(-520.f, -560.f, 330.f), Corner + FVector(60.f, -90.f, -20.f));
		}
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("RunPeekNoHug2") / FString::Printf(TEXT("runpeek_%s_%s_%02d.png"),
			!State.bStanceOn ? TEXT("old") : AOperativeCharacter::IsRunPeekYawLockEnabled() ? TEXT("lock") : TEXT("nolock"), State.bShotTop ? TEXT("top") : TEXT("side"),
			State.StripIndex);
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogCodexTactics, Display, TEXT("RunCornerPeekSmoke: still %s at %.2f s after the click (in cover %d, peek pose %d)"), *Path, State.StageTime,
			Op.bInCover ? 1 : 0, Op.IsCornerAimActive() ? 1 : 0);
		++State.StripIndex;
		State.NextStripTime += StripEverySeconds;
	}

	FString NameOf(const AOperativeCharacter* Op)
	{
		return Op ? Op->DisplayName.ToString() : FString(TEXT("-"));
	}

	FString Where(const FState& State)
	{
		FString Line;
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			if (const AOperativeCharacter* Op = Weak.Get())
			{
				Line += FString::Printf(TEXT(" %s (%.0f, %.0f) v %.0f%s%s%s%s;"), *NameOf(Op), Op->GetActorLocation().X, Op->GetActorLocation().Y, Op->GetVelocity().Size2D(),
					Op->bInCover ? TEXT(" in cover") : TEXT(""), Op->IsManualCornerPeek() ? TEXT(" peeking") : TEXT(""), Op->IsCornerAimActive() ? TEXT(" aim") : TEXT(""),
					Op->IsRunCornerPeekPending() ? TEXT(" run-peek armed") : TEXT(""));
			}
		}
		return Line;
	}

	bool InPeekPose(const AOperativeCharacter& Op)
	{
		return Op.bInCover && Op.IsManualCornerPeek() && Op.IsCornerAimActive() && Op.bAtCoverCorner && !Op.bShimmying;
	}

	/** A cursor-like hit on W_B_3's south face at X (traced from the south at chest height). */
	bool FaceHit(UWorld* World, const FState& State, float X, FHitResult& OutHit)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RunCornerPeekSmokeFace), false);
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			Params.AddIgnoredActor(Weak.Get());
		}
		const float Z = State.GroundZ + 120.f;
		return World->LineTraceSingleByChannel(OutHit, FVector(X, SouthFaceY - 300.f, Z), FVector(X, SouthFaceY + 15.f, Z), ECC_Visibility, Params)
			&& FMath::Abs(OutHit.ImpactPoint.Y - SouthFaceY) <= 15.f;
	}

	/** The player's click inside the world tick (as the controller's PlayerTick lands it): the hover first (optional), then the click. */
	void WorldTickClick(UWorld* World, FState& State)
	{
		State.bClickPending = false;
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
		if (!Squad || !PC || !Leader)
		{
			return;
		}
		FHitResult Hit;
		if (State.bCustomClick)
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(RunCornerPeekSmokeBlock), false);
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
			{
				Params.AddIgnoredActor(Weak.Get());
			}
			if (!World->LineTraceSingleByChannel(Hit, State.ClickFrom, State.ClickTo, ECC_Visibility, Params) || Hit.GetActor() != State.Block.Get())
			{
				Check(State, false, FString::Printf(TEXT("H: the cursor hit on the block's face %d (hit %s)"), State.FaceIndex, *GetNameSafe(Hit.GetActor())));
				return;
			}
		}
		else if (!FaceHit(World, State, State.ClickX, Hit))
		{
			Check(State, false, FString::Printf(TEXT("the cursor hit on W_B_3's south face at x %.0f"), State.ClickX));
			return;
		}
		State.bLastGhost = State.bHoverToo && PC->UpdateCornerPeekHover(Leader, &Hit);
		State.LastGhostOf = State.bLastGhost && PC->GetCornerPeekGhost() ? PC->GetCornerPeekGhost()->GetOperative() : nullptr;
		State.LastGhostNormal = State.bLastGhost && PC->GetCornerPeekGhost() ? FVector2D(PC->GetCornerPeekGhost()->GetSlot().WallNormal).GetSafeNormal() : FVector2D::ZeroVector;
		PC->HandleWorldHit(Hit);
		State.bClicked = true;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: the click at (%.0f, %.0f) (peek ghost %d of %s) - leader %s, %d selected, solo %d;%s"), Hit.ImpactPoint.X, Hit.ImpactPoint.Y,
			State.bLastGhost ? 1 : 0, *NameOf(State.LastGhostOf.Get()), *NameOf(Leader), Squad->GetSelectedGroup().Num(), Squad->IsSoloMode() ? 1 : 0, *Where(State));
	}

	void Click(FState& State, float X, bool bHover)
	{
		State.bClickPending = true;
		State.ClickX = X;
		State.bHoverToo = bHover;
		State.bClicked = false;
	}

	void Place(FState& State, AOperativeCharacter* Op, const FVector2D& At, float Yaw)
	{
		Op->LeaveCover(TEXT("smoke reset"));
		Op->ClearPendingCover();
		Op->StopOperative();
		Op->SetFormationWallPose(false);
		Op->SetStance(EOperativeStance::Standing);
		Op->TeleportTo(FVector(At.X, At.Y, State.GroundZ + Op->GetSimpleCollisionHalfHeight() + 5.f), FRotator(0.f, Yaw, 0.f), false, true);
	}

	/** A run-and-peek stage after its click: entry -> pose timing, the corner, the hold. True when the stage is over (checked). */
	bool WatchRunPeek(FState& State, AOperativeCharacter& Peeker, float DeltaTime, const TCHAR* Label, bool bNorthFace)
	{
		if (State.EntryAt < 0.f && Peeker.bInCover)
		{
			State.EntryAt = State.StageTime;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s: %s entered the cover %.2f s after the click at (%.0f, %.0f)"), Label, *NameOf(&Peeker), State.StageTime,
				Peeker.GetActorLocation().X, Peeker.GetActorLocation().Y);
		}
		const bool bPose = InPeekPose(Peeker);
		if (State.PoseAt < 0.f && bPose)
		{
			State.PoseAt = State.StageTime;
		}
		State.HoldTime = bPose ? State.HoldTime + DeltaTime : 0.f;
		if (FMath::FloorToInt32(State.StageTime / 0.5f) != FMath::FloorToInt32((State.StageTime - DeltaTime) / 0.5f))
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke trace %.1f s (%s):%s"), State.StageTime, Label, *Where(State));
		}
		if (State.HoldTime < HoldSeconds && State.StageTime < GiveUpSeconds)
		{
			return false;
		}
		const float EntryToPose = State.EntryAt >= 0.f && State.PoseAt >= 0.f ? State.PoseAt - State.EntryAt : -1.f;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke metric: %s: cover entry %.2f s, the peek pose %.2f s after the click (entry -> pose %.2f s);%s"), Label, State.EntryAt,
			State.PoseAt, EntryToPose, *Where(State));
		Check(State, State.HoldTime >= HoldSeconds, FString::Printf(TEXT("%s: %s peeks round the corner in the peek pose (held %.1f s) - one click"), Label, *NameOf(&Peeker), State.HoldTime));
		Check(State, EntryToPose >= 0.f && EntryToPose <= RunEntryToPoseMaxSeconds, FString::Printf(TEXT("%s: straight from the run into the peek - the pose %.2f s after the cover entry (<= %.1f s)"),
			Label, EntryToPose, RunEntryToPoseMaxSeconds));
		Check(State, Peeker.GetRunCornerPeekCount() == State.RunPeekCountBefore + 1, FString::Printf(TEXT("%s: the peek came from the run-and-peek arrival (count %d -> %d)"), Label,
			State.RunPeekCountBefore, Peeker.GetRunCornerPeekCount()));
		const FVector At = Peeker.GetActorLocation();
		Check(State, FMath::Abs(At.X - WallWestX) <= AtEndCm, FString::Printf(TEXT("%s: at the WEST end (x %.0f, the end %.0f)"), Label, At.X, WallWestX));
		const bool bFace = bNorthFace ? At.Y > NorthFaceY && At.Y < NorthFaceY + 120.f : At.Y < SouthFaceY && At.Y > SouthFaceY - 120.f;
		Check(State, bFace, FString::Printf(TEXT("%s: on the %s face, the one looking at him (y %.0f)"), Label, bNorthFace ? TEXT("north") : TEXT("south"), At.Y));
		CheckNoHug(State, Peeker, Label);
		return true;
	}

	/** I: a low cube (2 x LowCubeHalfCm wide, LowCubeHeightCm high) at X on the LowCubeY row. */
	AActor* SpawnLowCube(UWorld* World, const FState& State, float X)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Cube)
		{
			return nullptr;
		}
		const FTransform Transform(FRotator::ZeroRotator, FVector(X, LowCubeY, State.GroundZ + LowCubeHeightCm * 0.5f),
			FVector(LowCubeHalfCm / 50.f, LowCubeHalfCm / 50.f, LowCubeHeightCm / 100.f));
		AStaticMeshActor* Block = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Block)
		{
			return nullptr;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
		Block->FinishSpawning(Transform);
		return Block;
	}

	/** I: the click on the middle low cube's north face near its east end (knee height: the cube is 90 cm). */
	void LowCubeClick(FState& State, bool bHover)
	{
		const float Z = State.GroundZ + 50.f;
		const float X = LowCubeX[1] + LowCubeHalfCm - 20.f;
		State.ClickFrom = FVector(X, LowCubeY + 300.f, Z);
		State.ClickTo = FVector(X, LowCubeY + LowCubeHalfCm - 20.f, Z);
		State.bCustomClick = true;
		State.bClickPending = true;
		State.bHoverToo = bHover;
		State.bClicked = false;
	}

	/** H: the block - a cube 1.5 x 1.5 m, 2.5 m high, on the ground (BlockAll; the dynamic navmesh cuts it out). */
	AActor* SpawnBlock(UWorld* World, const FState& State)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Cube)
		{
			return nullptr;
		}
		const FTransform Transform(FRotator::ZeroRotator, FVector(BlockCentre.X, BlockCentre.Y, State.GroundZ + BlockHeightCm * 0.5f),
			FVector(BlockHalfCm / 50.f, BlockHalfCm / 50.f, BlockHeightCm / 100.f));
		AStaticMeshActor* Block = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Block)
		{
			return nullptr;
		}
		Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
		Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
		Block->FinishSpawning(Transform);
		return Block;
	}

	/** H: face Index's outward normal (0 north +Y, 1 east +X, 2 south -Y, 3 west -X) and its tangent (towards the clicked end). */
	FVector2D BlockFaceNormal(int32 Index)
	{
		static const FVector2D Normals[4] = { FVector2D(0.f, 1.f), FVector2D(1.f, 0.f), FVector2D(0.f, -1.f), FVector2D(-1.f, 0.f) };
		return Normals[FMath::Clamp(Index, 0, 3)];
	}

	FVector2D BlockFaceTangent(int32 Index)
	{
		const FVector2D Normal = BlockFaceNormal(Index);
		return FVector2D(-Normal.Y, Normal.X);
	}

	const TCHAR* BlockFaceName(int32 Index)
	{
		static const TCHAR* Names[4] = { TEXT("north (towards him)"), TEXT("east"), TEXT("south (the far side)"), TEXT("west") };
		return Names[FMath::Clamp(Index, 0, 3)];
	}

	/** H: one face - back into cover at W_B_3, the hover + click near one end of the face, the arrival on that face's corner. True = go on. */
	bool StepBlockFace(UWorld* World, FState& State, AOperativeCharacter& Lead, ACodexTacticsPlayerController& PC, float DeltaTime)
	{
		const FVector2D Normal = BlockFaceNormal(State.FaceIndex);
		const FVector2D Tangent = BlockFaceTangent(State.FaceIndex);
		const FString Label = FString::Printf(TEXT("H (the block's %s face)"), BlockFaceName(State.FaceIndex));
		if (State.FaceStep == 0)
		{
			// In cover at W_B_3's middle (another wall): standing at its slot, the cover order enters at once.
			FCoverSlot Home;
			if (State.StageTime < 0.2f)
			{
				return true;
			}
			if (!CoverTraceRules::FindCoverSlotAt(World, FVector(HomeCoverX, SouthFaceY, State.GroundZ + 100.f), FVector(0.f, 1.f, 0.f), Home))
			{
				Check(State, false, FString::Printf(TEXT("%s: a cover slot at W_B_3's middle"), *Label));
				return Finish(State, false);
			}
			Place(State, &Lead, FVector2D(Home.WorldLocation), -90.f);
			Lead.OrderTakeCover(Home, false);
			State.FaceStep = 1;
			State.StageTime = 0.f;
			return true;
		}
		if (State.FaceStep == 1)
		{
			const bool bSettled = Lead.bInCover && !Lead.bShimmying && !Lead.HasPendingCover();
			if ((!bSettled || State.StageTime < 1.5f) && State.StageTime < 10.f)
			{
				return true;
			}
			Check(State, bSettled, FString::Printf(TEXT("%s: he is in cover at W_B_3 (another wall) before the click"), *Label));
			// The cursor: a ray at chest height straight at the face, BlockClickAlongCm from its centre towards one end.
			const FVector2D OnFace = BlockCentre + Normal * BlockHalfCm + Tangent * BlockClickAlongCm;
			const float Z = State.GroundZ + 120.f;
			State.ClickFrom = FVector(OnFace + Normal * 250.f, Z);
			State.ClickTo = FVector(OnFace - Normal * 20.f, Z);
			State.bCustomClick = true;
			State.RunPeekCountBefore = Lead.GetRunCornerPeekCount();
			State.bClickPending = true;
			State.bHoverToo = true;
			State.bClicked = false;
			State.FaceStep = 2;
			State.StageTime = 0.f;
			State.HoldTime = 0.f;
			return true;
		}
		if (State.FaceStep == 2)
		{
			if (!State.bClicked)
			{
				return true;
			}
			State.bCustomClick = false;
			const FVector2D Pending = FVector2D(Lead.GetPendingCoverSlot().WallNormal).GetSafeNormal();
			Check(State, State.bLastGhost && State.LastGhostOf.Get() == &Lead && FVector2D::DotProduct(State.LastGhostNormal, Normal) >= 0.9f,
				FString::Printf(TEXT("%s: the hover shows the run-and-peek ghost on the clicked face (ghost %d of %s, its wall normal (%.2f, %.2f))"), *Label,
					State.bLastGhost ? 1 : 0, *NameOf(State.LastGhostOf.Get()), State.LastGhostNormal.X, State.LastGhostNormal.Y));
			Check(State, Lead.IsRunCornerPeekPending() && Lead.HasPendingCover() && FVector2D::DotProduct(Pending, Normal) >= 0.9f,
				FString::Printf(TEXT("%s: the click = the run-and-peek to that face (armed %d, pending cover %d, its wall normal (%.2f, %.2f))"), *Label,
					Lead.IsRunCornerPeekPending() ? 1 : 0, Lead.HasPendingCover() ? 1 : 0, Pending.X, Pending.Y));
			State.FaceStep = 3;
			State.StageTime = 0.f;
			return true;
		}
		const bool bPose = InPeekPose(Lead);
		State.HoldTime = bPose ? State.HoldTime + DeltaTime : 0.f;
		if (FMath::FloorToInt32(State.StageTime / 0.5f) != FMath::FloorToInt32((State.StageTime - DeltaTime) / 0.5f))
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke trace %.1f s (%s):%s"), State.StageTime, *Label, *Where(State));
		}
		if (State.HoldTime < 0.5f && State.StageTime < GiveUpSeconds)
		{
			return true;
		}
		const FVector2D Slot = FVector2D(Lead.GetCoverSlot().WallNormal).GetSafeNormal();
		const FVector2D End = BlockCentre + Normal * BlockHalfCm + Tangent * BlockHalfCm;
		const float ToEnd = static_cast<float>(FVector2D::Distance(FVector2D(Lead.GetActorLocation()), End));
		Check(State, bPose && Lead.GetRunCornerPeekCount() == State.RunPeekCountBefore + 1 && FVector2D::DotProduct(Slot, Normal) >= 0.9f && ToEnd <= AtEndCm,
			FString::Printf(TEXT("%s: he arrives on the CLICKED face and peeks round its clicked end (pose %d, run-peeks %d -> %d, his wall normal (%.2f, %.2f), %.0f cm from that end)"),
				*Label, bPose ? 1 : 0, State.RunPeekCountBefore, Lead.GetRunCornerPeekCount(), Slot.X, Slot.Y, ToEnd));
		++State.FaceIndex;
		State.FaceStep = 0;
		State.StageTime = 0.f;
		State.bClicked = false;
		if (State.FaceIndex < 4)
		{
			return true;
		}
		// I: the low cubes, all three box-selected north of them in the open.
		for (int32 Index = 0; Index < 3; ++Index)
		{
			State.LowCubes.Add(SpawnLowCube(World, State, LowCubeX[Index]));
		}
		TArray<AOperativeCharacter*> All;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			if (AOperativeCharacter* Each = State.Members[Index].Get())
			{
				Place(State, Each, LowCubeStart[Index], -90.f);
				All.Add(Each);
			}
		}
		if (USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>())
		{
			Squad->SetSelectedGroup(All, false);
		}
		State.Block = State.LowCubes.IsValidIndex(1) ? State.LowCubes[1] : nullptr; // the click's target
		Next(State, 12);
		return true;
	}

	/** I: the low cubes - the end click is no run-and-peek, its confirm puts everybody in cover. True = go on. */
	bool StepLowCubes(UWorld* World, FState& State, ACodexTacticsPlayerController& PC, float DeltaTime)
	{
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		if (!Squad || !State.Block.IsValid())
		{
			return Finish(State, false);
		}
		if (State.FaceStep == 0)
		{
			if (State.StageTime < 1.5f)
			{
				return true; // the navmesh round the new cubes
			}
			Check(State, Squad->HasMultiSelection() && Squad->GetSelectedGroup().Num() == 3, TEXT("I: all three box-selected in the open north of the low cubes"));
			LowCubeClick(State, true);
			State.FaceStep = 1;
			return true;
		}
		if (State.FaceStep == 1)
		{
			if (!State.bClicked)
			{
				return true;
			}
			bool bArmed = false;
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
			{
				bArmed |= Weak.IsValid() && Weak->IsRunCornerPeekPending();
			}
			Check(State, !State.bLastGhost && !bArmed && PC.HasCoverPreview(), FString::Printf(TEXT("I: a click on a LOW cube's end is no run-and-peek - no peek ghost (%d), nobody armed (%d), the cover ghost (%d)"),
				State.bLastGhost ? 1 : 0, bArmed ? 1 : 0, PC.HasCoverPreview() ? 1 : 0));
			State.FaceStep = 2;
			State.StageTime = 0.f;
			return true;
		}
		if (State.FaceStep == 2)
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			LowCubeClick(State, false); // the confirm
			State.FaceStep = 3;
			State.StageTime = 0.f;
			State.HoldTime = 0.f;
			return true;
		}
		if (!State.bClicked)
		{
			return true;
		}
		State.bCustomClick = false;
		bool bSettled = true;
		bool bCovered = true;
		FString Line;
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			const AOperativeCharacter* Op = Weak.Get();
			if (!Op)
			{
				continue;
			}
			const bool bProne = Op->GetStance() == EOperativeStance::Prone;
			bSettled &= Op->GetVelocity().Size2D() < 5.f && !Op->HasPendingCover() && !Op->bShimmying;
			bCovered &= Op->bInCover || bProne;
			Line += FString::Printf(TEXT(" %s (%.0f, %.0f)%s%s;"), *NameOf(Op), Op->GetActorLocation().X, Op->GetActorLocation().Y, Op->bInCover ? TEXT(" in cover") : TEXT(""),
				bProne ? TEXT(" prone") : TEXT(""));
		}
		State.HoldTime = bSettled && bCovered ? State.HoldTime + DeltaTime : 0.f;
		if (FMath::FloorToInt32(State.StageTime / 0.5f) != FMath::FloorToInt32((State.StageTime - DeltaTime) / 0.5f))
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke trace %.1f s (I low cubes):%s"), State.StageTime, *Line);
		}
		if (State.HoldTime < 1.f && State.StageTime < GiveUpSeconds)
		{
			return true;
		}
		Check(State, bSettled && bCovered, FString::Printf(TEXT("I: its confirm = the group cover - every man in cover (prone only as the fallback), nobody standing in the open:%s"), *Line));
		return Finish(State, true);
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State, float DeltaTime)
	{
		State.Time += DeltaTime;
		State.StageTime += DeltaTime;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 300.f)
		{
			Check(State, false, FString::Printf(TEXT("timeout (stage %d)"), State.Stage));
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		UpdateWatch(State);
		if (State.Stage == 0)
		{
			if (State.StageTime < 3.f || !Squad || !PC || Squad->GetMembers().Num() < 3)
			{
				return true;
			}
			TArray<AOperativeCharacter*> Roster = Squad->GetMembers();
			Roster.Sort([](const AOperativeCharacter& X, const AOperativeCharacter& Y) { return X.SquadIndex < Y.SquadIndex; });
			FCollisionQueryParams Params(SCENE_QUERY_STAT(RunCornerPeekSmokeStart), false);
			for (int32 Index = 0; Index < 3; ++Index)
			{
				State.Members.Add(Roster[Index]);
				Params.AddIgnoredActor(Roster[Index]);
				if (USkeletalMeshComponent* Mesh = Roster[Index]->GetMesh())
				{
					Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
				}
			}
			FHitResult Ground;
			State.GroundZ = World->LineTraceSingleByChannel(Ground, FVector(RunStart.X, RunStart.Y, 2000.f), FVector(RunStart.X, RunStart.Y, -2000.f), ECC_Visibility, Params)
				? static_cast<float>(Ground.ImpactPoint.Z) : 0.f;
			FHitResult Probe;
			const bool bWall = FaceHit(World, State, MiddleX, Probe) && FaceHit(World, State, WallWestX + EndInsetCm, Probe) && FaceHit(World, State, WallEastX - EndInsetCm, Probe);
			Check(State, bWall, FString::Printf(TEXT("the lab wall W_B_3 stands where the layout puts it (south face y %.0f, x %.0f..%.0f)"), SouthFaceY, WallWestX, WallEastX));
			if (!bWall)
			{
				return Finish(State, false);
			}
			Check(State, ACodexTacticsPlayerController::IsRunCornerPeekEnabled(), TEXT("Codex.Cover.RunCornerPeek is on"));
			Squad->ExitSoloMode(false, 0.f);
			Squad->SetLeader(Roster[0]);
			// The leader in the open south of the wall; the others parked south-east in the open (solo: they hold; within the solo leash,
			// USquadSubsystem::SoloModeMaxDistance, of every stage).
			Place(State, Roster[0], RunStart, 90.f);
			Place(State, Roster[1], ParkA, 90.f);
			Place(State, Roster[2], ParkB, 90.f);
			Squad->ResetFormationState(FVector(0.f, 1.f, 0.f));
			Next(State, 1);
			return true;
		}
		if (!Squad || !PC || !Squad->GetLeader())
		{
			return Finish(State, false);
		}
		// A-D: the same operative throughout (solo mode keeps him the leader; checked at each click).
		AOperativeCharacter* Leader = State.Stage < 7 || State.Stage >= 8 ? State.Members[0].Get() : Squad->GetLeader();
		if (!Leader)
		{
			return Finish(State, false);
		}
		if (State.Stage >= 2 && State.Stage < 7 && State.bClickPending && (Squad->GetLeader() != Leader || !Squad->IsSoloMode()))
		{
			Check(State, false, FString::Printf(TEXT("stage %d: solo mode with %s the leader (leader %s, solo %d)"), State.Stage, *NameOf(Leader), *NameOf(Squad->GetLeader()),
				Squad->IsSoloMode() ? 1 : 0));
		}
		switch (State.Stage)
		{
		case 1:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Squad->EnterSoloMode();
			Check(State, Squad->IsSoloMode() && !Squad->HasMultiSelection(), FString::Printf(TEXT("solo mode, %s alone selected"), *NameOf(Leader)));
			Leader->OrderMoveTo(FVector(RunGoal.X, RunGoal.Y, Leader->GetActorLocation().Z), false);
			Next(State, 2);
			return true;
		}
		case 2:
		{
			// A: ONE click on the west end while he runs.
			if (!State.bClickPending && !State.bClicked && State.StageTime >= ClickIntoRunSeconds)
			{
				const float Ahead = static_cast<float>(FVector::Dist2D(Leader->GetActorLocation(), FVector(WallWestX, SouthFaceY, 0.f)));
				Check(State, Leader->GetVelocity().Size2D() > 150.f && Ahead >= ClickMinCm && Ahead <= ClickMaxCm,
					FString::Printf(TEXT("A: he runs (%.0f cm/s) in the open, the wall end %.1f m ahead (8..15 m)"), Leader->GetVelocity().Size2D(), Ahead / 100.f));
				State.RunPeekCountBefore = Leader->GetRunCornerPeekCount();
				Click(State, WallWestX + EndInsetCm, true);
				return true;
			}
			if (!State.bClicked)
			{
				return true;
			}
			if (State.StageTime < 0.05f + ClickIntoRunSeconds)
			{
				Check(State, State.bLastGhost && State.LastGhostOf.Get() == Leader, FString::Printf(TEXT("A: the hover over the wall end shows the peek ghost from the open (%s)"),
					*NameOf(State.LastGhostOf.Get())));
				Check(State, Leader->IsRunCornerPeekPending() && Leader->HasPendingCover() && !PC->HasCoverPreview(),
					TEXT("A: the click is the run-and-peek (his cover walk armed with the peek; no cover ghost to confirm)"));
			}
			TakeStripStill(World, State, *Leader); // (the strip ends before the 1 s hold does: ~0.7 s of the run + the hold)
			if (!WatchRunPeek(State, *Leader, DeltaTime, TEXT("A (running, solo)"), false))
			{
				return true;
			}
			if (State.bShot)
			{
				if (APlayerController* Player = UGameplayStatics::GetPlayerController(World, 0))
				{
					Player->SetViewTarget(Player->GetPawn());
				}
			}
			Place(State, Leader, FVector2D(2600.f, -3700.f), 90.f);
			Next(State, 3);
			return true;
		}
		case 3:
		{
			// B: from the open, a click on the face's middle = the cover ghost, not a run-and-peek.
			if (!State.bClickPending && !State.bClicked && State.StageTime >= 1.f)
			{
				Click(State, MiddleX, true);
				return true;
			}
			if (!State.bClicked)
			{
				return true;
			}
			Check(State, PC->HasCoverPreview() && !Leader->IsRunCornerPeekPending() && !Leader->HasPendingCover() && !State.bLastGhost,
				FString::Printf(TEXT("B: a click on the wall's middle = the cover ghost (preview %d, run-and-peek %d, peek ghost %d)"), PC->HasCoverPreview() ? 1 : 0,
					Leader->IsRunCornerPeekPending() ? 1 : 0, State.bLastGhost ? 1 : 0));
			Next(State, 4);
			return true;
		}
		case 4:
		{
			if (!State.bClickPending && !State.bClicked && State.StageTime >= 1.f)
			{
				Click(State, MiddleX, false); // the confirm
				return true;
			}
			if (!State.bClicked)
			{
				return true;
			}
			const bool bSettled = Leader->bInCover && !Leader->bShimmying && !Leader->HasPendingCover() && Leader->GetVelocity().Size2D() < 5.f;
			State.HoldTime = bSettled ? State.HoldTime + DeltaTime : 0.f;
			if (State.HoldTime < 1.5f && State.StageTime < GiveUpSeconds)
			{
				return true;
			}
			Check(State, bSettled && FMath::Abs(Leader->GetActorLocation().X - MiddleX) <= 150.f && !Leader->IsManualCornerPeek() && Leader->GetRunCornerPeekCount() == State.RunPeekCountBefore + 1,
				FString::Printf(TEXT("B: its confirm = take cover at the middle, no peek (x %.0f, peek %d);%s"), Leader->GetActorLocation().X, Leader->IsManualCornerPeek() ? 1 : 0,
					*Where(State)));
			Next(State, 5);
			return true;
		}
		case 5:
		{
			// C: from that cover, the old in-cover peek at the EAST end (a side-step in cover, no run-and-peek).
			if (!State.bClickPending && !State.bClicked && State.StageTime >= 0.5f)
			{
				Click(State, WallEastX - EndInsetCm, true);
				return true;
			}
			if (!State.bClicked)
			{
				return true;
			}
			if (State.HoldTime <= 0.f && State.PoseAt < 0.f && State.EntryAt < 0.f)
			{
				State.EntryAt = 0.f; // (the C stage measures nothing from an entry)
				Check(State, Leader->IsManualCornerPeek() && !Leader->IsRunCornerPeekPending() && Leader->bInCover,
					TEXT("C: in cover at that wall, its end = the old corner peek at once (no run-and-peek)"));
			}
			State.bLeftCover |= !Leader->bInCover;
			const bool bPose = InPeekPose(*Leader);
			State.HoldTime = bPose ? State.HoldTime + DeltaTime : 0.f;
			if (State.HoldTime < HoldSeconds && State.StageTime < GiveUpSeconds)
			{
				return true;
			}
			Check(State, bPose && FMath::Abs(Leader->GetActorLocation().X - WallEastX) <= AtEndCm && !State.bLeftCover,
				FString::Printf(TEXT("C: he side-stepped in cover to the east end and peeks (x %.0f, left the cover %d)"), Leader->GetActorLocation().X, State.bLeftCover ? 1 : 0));
			Place(State, Leader, NorthStart, -90.f);
			Next(State, 6);
			return true;
		}
		case 6:
		{
			// D: north of the wall, the click on the SOUTH face: the corner on the clicked face (the face rule) - he runs round the west end.
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			if (State.StageTime < 0.5f + DeltaTime * 1.5f)
			{
				Leader->OrderMoveTo(FVector(NorthGoal.X, NorthGoal.Y, Leader->GetActorLocation().Z), false);
				return true;
			}
			if (!State.bClickPending && !State.bClicked && State.StageTime >= 0.5f + ClickIntoRunSeconds)
			{
				State.RunPeekCountBefore = Leader->GetRunCornerPeekCount();
				Click(State, WallWestX + EndInsetCm, true);
				return true;
			}
			if (!State.bClicked || !WatchRunPeek(State, *Leader, DeltaTime, TEXT("D (from the north, the south face clicked)"), false))
			{
				return true;
			}
			// E: all three out in the open south of the wall, box-selected (the Commander in the middle).
			TArray<AOperativeCharacter*> All;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				AOperativeCharacter* Each = State.Members[Index].Get();
				if (Each)
				{
					Place(State, Each, FVector2D(BoxStartX[Index], BoxStartY - (Index == 0 ? 0.f : 150.f)), 90.f);
					All.Add(Each);
				}
			}
			Squad->SetSelectedGroup(All, false);
			State.Expected = State.Members[1];
			Next(State, 7);
			return true;
		}
		case 7:
		{
			if (!State.bClickPending && !State.bClicked && State.StageTime >= 1.f)
			{
				Check(State, Squad->HasMultiSelection() && Squad->GetSelectedGroup().Num() == 3, FString::Printf(TEXT("E: all three box-selected (leader %s)"), *NameOf(Leader)));
				AOperativeCharacter* Peeker = State.Expected.Get();
				State.RunPeekCountBefore = Peeker ? Peeker->GetRunCornerPeekCount() : 0;
				Click(State, WallWestX + EndInsetCm, true);
				return true;
			}
			if (!State.bClicked)
			{
				return true;
			}
			AOperativeCharacter* Peeker = State.Expected.Get();
			if (!Peeker)
			{
				return Finish(State, false);
			}
			if (State.StageTime < 1.05f)
			{
				Check(State, State.bLastGhost && State.LastGhostOf.Get() == Peeker, FString::Printf(TEXT("E: the peek ghost is the man nearest the west end (%s; ghost of %s)"),
					*NameOf(Peeker), *NameOf(State.LastGhostOf.Get())));
			}
			// The others settle in cover behind him too before the stage ends.
			bool bOthers = true;
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
			{
				const AOperativeCharacter* Op = Weak.Get();
				bOthers &= !Op || Op == Peeker || (Op->bInCover && !Op->bShimmying && !Op->HasPendingCover());
			}
			if ((!bOthers && State.StageTime < GiveUpSeconds) || !WatchRunPeek(State, *Peeker, DeltaTime, TEXT("E (box selection)"), false))
			{
				return true;
			}
			bool bBehind = true;
			bool bOtherPeeks = false;
			FString Line;
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
			{
				const AOperativeCharacter* Op = Weak.Get();
				if (!Op || Op == Peeker)
				{
					continue;
				}
				const float Behind = static_cast<float>(Op->GetActorLocation().X - Peeker->GetActorLocation().X);
				const bool bHere = Op->bInCover && Behind >= 60.f && Behind <= 400.f && Op->GetActorLocation().Y < SouthFaceY;
				bBehind &= bHere;
				bOtherPeeks |= Op->IsManualCornerPeek();
				Line += FString::Printf(TEXT(" %s %.0f cm behind%s;"), *NameOf(Op), Behind, bHere ? TEXT("") : TEXT(" OFF"));
			}
			Check(State, bBehind, FString::Printf(TEXT("E: the others ran into cover in a stack behind him on the south face:%s"), *Line));
			Check(State, !bOtherPeeks, TEXT("E: only he peeks"));
			// F: the squad in formation (no solo, no box selection), the followers far behind the leader in the open.
			AOperativeCharacter* Lead = State.Members[0].Get();
			if (!Lead)
			{
				return Finish(State, false);
			}
			Squad->SetSelectedGroup({ Lead }, false);
			Squad->SetLeader(Lead, true);
			Squad->ExitSoloMode(false, 0.f);
			Place(State, Lead, FormationLeaderStart, 90.f);
			for (int32 Index = 1; Index < 3; ++Index)
			{
				if (AOperativeCharacter* Each = State.Members[Index].Get())
				{
					Place(State, Each, FormationFollowerStart[Index - 1], 90.f);
				}
			}
			Squad->ResetFormationState(FVector(0.f, 1.f, 0.f));
			Next(State, 8);
			return true;
		}
		case 8:
		{
			// F (Q2): the click on the west end in formation - the peek fires once the LEADER is at the wall.
			AOperativeCharacter* Lead = State.Members[0].Get();
			if (!Lead)
			{
				return Finish(State, false);
			}
			if (!State.bClickPending && !State.bClicked && State.StageTime >= 0.5f)
			{
				Check(State, !Squad->IsSoloMode() && !Squad->HasMultiSelection() && Squad->GetLeader() == Lead, FString::Printf(TEXT("F: the squad in formation, %s leads (solo %d)"),
					*NameOf(Lead), Squad->IsSoloMode() ? 1 : 0));
				State.CarryCountBefore = PC->GetSquadPeekCarryCount();
				Click(State, WallWestX + EndInsetCm, true);
				return true;
			}
			if (!State.bClicked)
			{
				return true;
			}
			if (State.CarryFiredAt < 0.f && State.StageTime < 0.6f + DeltaTime * 1.5f)
			{
				Check(State, Squad->HasFormationWallOrder() && PC->IsSquadPeekCarryRunPeek(), FString::Printf(TEXT("F: the click = the squad's wall order there, its corner peek carried as a run-and-peek (wall order %d, run-peek carry %d)"),
					Squad->HasFormationWallOrder() ? 1 : 0, PC->IsSquadPeekCarryRunPeek() ? 1 : 0));
			}
			if (State.CarryFiredAt < 0.f && PC->GetSquadPeekCarryCount() > State.CarryCountBefore)
			{
				State.CarryFiredAt = State.StageTime;
				for (int32 Index = 1; Index < 3; ++Index)
				{
					const AOperativeCharacter* Each = State.Members[Index].Get();
					State.bFollowerWalkingAtFire |= Each && Each != Squad->GetFormationPeeker() && Each->GetVelocity().Size2D() > StillWalkingSpeed;
				}
				State.AtFire = Where(State);
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: F: the carried peek fired %.2f s after the click (a follower still walking %d);%s"), State.StageTime,
					State.bFollowerWalkingAtFire ? 1 : 0, *State.AtFire);
			}
			AOperativeCharacter* Peeker = Squad->GetFormationPeeker();
			const bool bPose = Peeker && InPeekPose(*Peeker) && FMath::Abs(Peeker->GetActorLocation().X - WallWestX) <= AtEndCm;
			State.HoldTime = bPose ? State.HoldTime + DeltaTime : 0.f;
			if (FMath::FloorToInt32(State.StageTime / 0.5f) != FMath::FloorToInt32((State.StageTime - DeltaTime) / 0.5f))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke trace %.1f s (F formation):%s"), State.StageTime, *Where(State));
			}
			if (State.HoldTime < HoldSeconds && State.StageTime < GiveUpSeconds)
			{
				return true;
			}
			Check(State, State.CarryFiredAt >= 0.f && State.bFollowerWalkingAtFire,
				FString::Printf(TEXT("F (Q2): the corner peek ordered as soon as the leader is at the wall - a follower still on his way (fired %.2f s after the click, follower walking %d);%s"),
					State.CarryFiredAt, State.bFollowerWalkingAtFire ? 1 : 0, *State.AtFire));
			Check(State, State.HoldTime >= HoldSeconds, FString::Printf(TEXT("F: the corner man %s peeks round the west end (held %.1f s);%s"), *NameOf(Peeker), State.HoldTime,
				*Where(State)));
			if (Peeker)
			{
				CheckNoHug(State, *Peeker, TEXT("F (formation, the corner man)"));
			}
			// G: solo, a fight in the tactical pause.
			UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			if (!Flow)
			{
				return Finish(State, false);
			}
			for (int32 Index = 0; Index < 3; ++Index)
			{
				if (AOperativeCharacter* Each = State.Members[Index].Get())
				{
					Place(State, Each, Index == 0 ? PauseStart : (Index == 1 ? ParkA : ParkB), 90.f);
				}
			}
			Squad->SetLeader(Lead, true);
			Squad->EnterSoloMode();
			Squad->ResetFormationState(FVector(0.f, 1.f, 0.f));
			SmokeUtils::UsePreparedWaveStart();
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
				It->SetActorTickEnabled(false);
			}
			Next(State, 9);
			return true;
		}
		case 9:
		{
			UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
			AOperativeCharacter* Lead = State.Members[0].Get();
			if (!Flow || !Lead)
			{
				return Finish(State, false);
			}
			if (State.StageTime < 1.f)
			{
				return true;
			}
			if (!State.bClickPending && !State.bClicked)
			{
				const bool bPause = Flow->ToggleTacticalPause() == EGameFlowResult::Ok && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause;
				Check(State, bPause && Squad->IsSoloMode() && Squad->GetLeader() == Lead, FString::Printf(TEXT("G: solo, %s leads, a fight in the tactical pause (phase %d, mode %d)"),
					*NameOf(Lead), static_cast<int32>(Flow->GetPhase()), static_cast<int32>(Flow->GetCombatMode())));
				if (!bPause)
				{
					return Finish(State, false);
				}
				State.PlannedBefore = PC->GetPlannedRunCornerPeekCount();
				State.RunPeekCountBefore = Lead->GetRunCornerPeekCount();
				Click(State, WallWestX + EndInsetCm, true);
				return true;
			}
			if (!State.bClicked)
			{
				return true;
			}
			if (State.StageTime < 1.6f)
			{
				return true;
			}
			Check(State, State.bLastGhost && State.LastGhostOf.Get() == Lead && !PC->HasCoverPreview(),
				FString::Printf(TEXT("G (Q3): in the pause the hover over the wall end shows the run-and-peek ghost (ghost of %s, cover ghost %d)"), *NameOf(State.LastGhostOf.Get()),
					PC->HasCoverPreview() ? 1 : 0));
			Check(State, Lead->IsRunCornerPeekPending() && Lead->HasPendingCover() && PC->GetPlannedRunCornerPeekCount() == State.PlannedBefore + 1 && Squad->GetPlannedOrderCount() >= 1
				&& Lead->GetVelocity().Size2D() < 5.f,
				FString::Printf(TEXT("G: the click plans the run-and-peek - armed %d, pending cover %d, planned %d -> %d, planned orders %d, nobody moves (v %.0f)"),
					Lead->IsRunCornerPeekPending() ? 1 : 0, Lead->HasPendingCover() ? 1 : 0, State.PlannedBefore, PC->GetPlannedRunCornerPeekCount(), Squad->GetPlannedOrderCount(),
					Lead->GetVelocity().Size2D()));
			Flow->ToggleTacticalPause(); // the pause's end: the planned orders run
			Next(State, 10);
			State.bClicked = true; // (the arrival watch runs from the release on)
			return true;
		}
		case 10:
		{
			AOperativeCharacter* Lead = State.Members[0].Get();
			if (!Lead)
			{
				return Finish(State, false);
			}
			if (!WatchRunPeek(State, *Lead, DeltaTime, TEXT("G (planned in the tactical pause, released)"), false))
			{
				return true;
			}
			// H: the free-standing block south of W_B_3 (the face rule).
			State.Block = SpawnBlock(World, State);
			Check(State, State.Block.IsValid(), TEXT("H: a free-standing 1.5 m block spawned south of W_B_3"));
			if (!State.Block.IsValid())
			{
				return Finish(State, false);
			}
			State.FaceIndex = 0;
			State.FaceStep = 0;
			Next(State, 11);
			return true;
		}
		case 11:
		{
			AOperativeCharacter* Lead = State.Members[0].Get();
			if (!Lead || !State.Block.IsValid())
			{
				return Finish(State, false);
			}
			return StepBlockFace(World, State, *Lead, *PC, DeltaTime);
		}
		case 12:
			return StepLowCubes(World, State, *PC, DeltaTime);
		default:
			return Finish(State, false);
		}
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShotTop = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("ShotTop"), ESearchCase::IgnoreCase); });
		State->bShot = State->bShotTop || Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("Shot"), ESearchCase::IgnoreCase); });
		State->bStanceOn = AOperativeCharacter::IsRunPeekStanceEnabled();
		UE_LOG(LogCodexTactics, Display, TEXT("RunCornerPeekSmoke: Codex.Cover.RunPeekStance %d, Codex.Cover.RunPeekYawLock %d%s"), State->bStanceOn ? 1 : 0,
			AOperativeCharacter::IsRunPeekYawLockEnabled() ? 1 : 0, State->bShot ? (State->bShotTop ? TEXT(", frame strip from above (ShotTop)") : TEXT(", frame strip (Shot)")) : TEXT(""));
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FWorldDelegates::OnWorldPreActorTick.AddLambda([WeakWorld, State](UWorld* Ticking, ELevelTick, float)
		{
			if (State->bClickPending && Ticking == WeakWorld.Get())
			{
				WorldTickClick(Ticking, *State);
			}
		});
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float DeltaTime)
		{
			const UWorld* Live = WeakWorld.Get();
			return Step(WeakWorld, *State, Live ? Live->GetDeltaSeconds() : DeltaTime);
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.RunCornerPeekSmoke"),
		TEXT("Dev check (#55, run-and-peek): on L_FormationLab's W_B_3 - one click on the end of a wall in front of a running operative sends him there and he peeks on arrival (no second click, no idle), from the north the north face; the wall's middle is still a cover click; the old in-cover peek still works; a box selection: the man nearest the end peeks, the others stack behind him; formation: the peek once the leader is at the wall (Q2 2026-11-08); the tactical pause plans it (Q3). PASS/FAIL, exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Run(Args, World); }));
}

#endif // !UE_BUILD_SHIPPING
