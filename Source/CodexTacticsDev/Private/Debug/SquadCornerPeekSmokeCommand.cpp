// Dev-only headless check of the squad's corner peek ANIMATION on the formation lab (the game designer's play-test 2026-11-01, video
// Saved/BugReport/1.11.26_bug_01.avi: "when peeking round a corner the animation plays TWICE" - the peeker spun twice at the corner):
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands SquadCornerPeekSmoke
//   (map /Game/Maps/L_FormationLab, "-LevelJson=L_FormationLab.json" - Scripts/test_map.json smokeOptions)
// The squad of three in group mode in front of the lab wall W_B_3 (1500..4000 at y -2700, 3 m high, its south face): the player's two clicks
// on the wall (the group cover order = the formation wall layout, crouched), then the hover on the wall's END (UpdateCornerPeekHover: the peek
// ghost of the member nearest it) and the click there (HandleWorldHit -> USquadSubsystem::OrderFormationCornerPeek). Every frame of the
// peeker is watched (one tick per frame, his mesh's pose ticked headless): every montage start on him (clip, kind), every cover entry, every
// CoverFacing change while in cover, the manual peek orders, the body's yaw (the shoulder line) - each logged with the frame. Checks per peek
// order: one cover entry, one peek order on him, the arrival along the wall turns in WITHOUT the wall-facing enter clip
// (CoverFacingRules::DecideCoverEntryTurn), ONE corner step-out montage start (cvr_*_idle_*_to_fire), no exit / stance / second entry
// one-shot, no facing flip while in cover, the held loop = the fire idle of the peek side, and ONE body turn from his walk into the peek (the
// bug: the enter clip's first frame faces the wall - a spin to the wall, a spin back out, then the step-out), the peek by the man at the
// ORDERED end round THAT corner. Four orders: 1a the west end, 1b straight on the east end of the same wall (the squad still stacked at the
// west end - the designer's 2nd bug 2026-11-01: that peek never happened, the old stack was taken for the new one), then a fresh layout,
// 2a the east end, 2b the west end. The end click runs inside the world tick (OnWorldPreActorTick), where the player's click lands.
// A/B: smoke_parallel -PreCmds "Codex.Cover.CornerPeekEntryTurn 0" = the old entry.
// "Shot" (rendered, fixed step: UnrealEditor.exe <uproject> /Game/Maps/L_FormationLab -game -RenderOffscreen -ResX=1280 -ResY=720
// -UseFixedTimeStep -FPS=60 "-LevelJson=L_FormationLab.json" "-ExecCmds=CodexTactics.SquadCornerPeekSmoke Shot") saves a frame strip of the
// peek to Saved/Screenshots/CornerPeekFix/peek_<fixed|old>_order<1a..2b>_<west|east>_<i>.png (a camera beside the corner, every 0.25 s from his last 1.5 m).
// Godot reference: none (UE-only; docs/systems/squad-and-orders.md "The squad's corner peek").

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
#include "Engine/World.h"
#include "Formation/FormationRuntimeRules.h"
#include "GameFramework/HUD.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/CoverGhostActor.h"
#include "UnrealClient.h"

namespace SquadCornerPeekSmoke
{
	/** The lab wall W_B_3 (Content/Data/FormationLab/formation_lab_01.json): x 1500..4000 at y -2700, 40 cm thick; the squad on its south face. */
	constexpr float WallWestX = 1500.f;
	constexpr float WallEastX = 4000.f;
	constexpr float WallFaceY = -2720.f;
	constexpr float StartY = -3300.f;
	/** The peek is judged once held this long, s; the whole peek within PeekMaxSeconds. */
	constexpr float HoldSeconds = 3.f;
	constexpr float PeekMaxSeconds = 45.f;
	/** A body turn: at least this much yaw, deg; it ends after this many still frames (< TurnStillDeg a frame) or on a reversal. */
	constexpr float TurnMinDeg = 30.f;
	constexpr int32 TurnPauseFrames = 6;
	constexpr float TurnStillDeg = 0.4f;
	/** A turn segment below this is noise (the clips' own sway), deg. */
	constexpr float TurnNoiseDeg = 8.f;
	/** The yaw trace covers this many frames after the entry. */
	constexpr int32 YawTraceFrames = 120;
	/** The frame strip: a still every this many s from the click, this many stills. */
	constexpr float StripEverySeconds = 0.25f;
	constexpr int32 StripStills = 16;

	/** What one montage on the peeker plays (by the operative's AnimInstance cover clip lists). */
	enum class EClipKind : uint8
	{
		CoverEnter,
		FireEnter,
		FireExit,
		FireIdle,
		CornerLoop,
		CoverIdle,
		Shimmy,
		StanceSwitch,
		Other,
	};

	const TCHAR* KindName(EClipKind Kind)
	{
		switch (Kind)
		{
		case EClipKind::CoverEnter: return TEXT("cover enter");
		case EClipKind::FireEnter: return TEXT("PEEK step-out (idle->fire)");
		case EClipKind::FireExit: return TEXT("fire->idle exit");
		case EClipKind::FireIdle: return TEXT("fire idle loop");
		case EClipKind::CornerLoop: return TEXT("corner look loop");
		case EClipKind::CoverIdle: return TEXT("cover idle loop");
		case EClipKind::Shimmy: return TEXT("shimmy loop");
		case EClipKind::StanceSwitch: return TEXT("stand<->crouch at the wall");
		default: return TEXT("other");
		}
	}

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		/** Variant 0: the west end, then the east end of the same wall; 1: the east end, then the west end. Pair = which of the two. */
		int32 Variant = 0;
		int32 Pair = 0;
		bool bShot = false;
		float GroundZ = 0.f;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		/** The peek under watch. */
		TWeakObjectPtr<AOperativeCharacter> Peeker;
		ECoverFacing PeekSide = ECoverFacing::Right;
		uint64 ClickFrame = 0;
		int32 PeeksBefore = 0;
		int32 ManualPeeksBefore = 0;
		int32 ArrivalEntriesBefore = 0;
		bool bWasInCover = false;
		bool bFacingValid = false;
		ECoverFacing LastFacing = ECoverFacing::Right;
		bool bLastManual = false;
		bool bLastAim = false;
		bool bLastShimmy = false;
		EOperativeStance LastStance = EOperativeStance::Standing;
		TSet<int32> SeenMontages;
		int32 CoverEntries = 0;
		int32 FacingFlips = 0;
		int32 FireEnterStarts = 0;
		int32 CoverEnterStarts = 0;
		int32 OneShotsAfterEntry = 0;
		TArray<FString> ClipsInCover;
		float LastBodyYaw = 0.f;
		bool bBodyYawValid = false;
		/** The body's turns in cover: one turn = frames turning the same way (a pause of TurnPauseFrames or a reversal ends it). */
		float TurnSum = 0.f;
		int32 TurnSign = 0;
		int32 TurnStillFrames = 0;
		int32 Turns = 0;
		TArray<float> TurnSegments;
		TArray<FString> TurnLog;
		/** Frames in cover, and the yaw trace after the entry. */
		int32 FramesInCover = 0;
		FString YawTrace;
		float HoldTime = 0.f;
		TWeakObjectPtr<ACameraActor> Camera;
		int32 StripIndex = 0;
		float NextStripTime = 0.f;
		bool bStripStarted = false;
		/**
		 * The end click is due inside the next world tick, before the actors and the squad tick (FWorldDelegates::OnWorldPreActorTick) - where
		 * the player's click lands in the game (the controller's PlayerTick): the squad then updates in the SAME frame as the order, before the
		 * leader's stand-up lets his move become the slide. (From the core ticker the squad saw the slide first - the 2nd bug never showed.)
		 */
		bool bClickInWorldTick = false;
		FDelegateHandle PreTickHandle;
		/** Codex.Cover.CornerPeekEntryTurn at the start (0 = the old entry: the turn checks are reported, not failed - the A/B). */
		bool bEntryTurnOn = true;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		FWorldDelegates::OnWorldPreActorTick.Remove(State.PreTickHandle);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("SquadCornerPeekSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	bool IsWestEnd(const FState& State)
	{
		return (State.Pair == 0) == (State.Variant == 0);
	}

	float EndX(const FState& State)
	{
		return IsWestEnd(State) ? WallWestX : WallEastX;
	}

	/** Along the wall from its end inwards (+X at the west end). */
	float Inward(const FState& State)
	{
		return IsWestEnd(State) ? 1.f : -1.f;
	}

	/** The peek order's name in the log: "order 1b (east end, the 2nd on the same wall)". */
	FString PeekLabel(const FState& State)
	{
		return FString::Printf(TEXT("order %d%s (%s end%s)"), State.Variant + 1, State.Pair == 0 ? TEXT("a") : TEXT("b"), IsWestEnd(State) ? TEXT("west") : TEXT("east"),
			State.Pair == 0 ? TEXT(", the first") : TEXT(", the 2nd on the same wall - the other end"));
	}

	/** The player's cursor hit on the wall's south face at X (a real trace: the wall's own component, as the cursor gives it). */
	bool WallHit(UWorld* World, const FState& State, float X, FHitResult& OutHit)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SquadCornerPeekSmokeWall), false);
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			Params.AddIgnoredActor(Weak.Get());
		}
		const float Z = State.GroundZ + 120.f;
		return World->LineTraceSingleByChannel(OutHit, FVector(X, WallFaceY - 300.f, Z), FVector(X, WallFaceY + 100.f, Z), ECC_Visibility, Params)
			&& FMath::Abs(OutHit.ImpactPoint.Y - WallFaceY) <= 15.f;
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
			return EClipKind::FireEnter;
		}
		if (In(Anim.CoverStandFireExit) || In(Anim.CoverCrouchFireExit))
		{
			return EClipKind::FireExit;
		}
		if (In(Anim.CoverStandFireIdle) || In(Anim.CoverCrouchFireIdle))
		{
			return EClipKind::FireIdle;
		}
		if (In(Anim.CoverStandCorner) || In(Anim.CoverCrouchCorner))
		{
			return EClipKind::CornerLoop;
		}
		if (In(Anim.CoverStandIdle) || In(Anim.CoverCrouchIdle))
		{
			return EClipKind::CoverIdle;
		}
		if (In(Anim.CoverStandMoveForward) || In(Anim.CoverStandMoveBackward) || In(Anim.CoverCrouchMoveForward) || In(Anim.CoverCrouchMoveBackward))
		{
			return EClipKind::Shimmy;
		}
		if (In(Anim.CoverStandToCrouch) || In(Anim.CoverCrouchToStand))
		{
			return EClipKind::StanceSwitch;
		}
		return EClipKind::Other;
	}

	/** The side of a cover clip list a clip is (0 = the pack's _L = his right, 1 = _R = his left), -1 if none. */
	int32 ClipSide(const UOperativeAnimInstance& Anim, const UAnimSequenceBase* Clip)
	{
		for (const TArray<TObjectPtr<UAnimSequenceBase>>* List : { &Anim.CoverStandFireIdle, &Anim.CoverCrouchFireIdle, &Anim.CoverStandFireEnter,
			&Anim.CoverCrouchFireEnter, &Anim.CoverStandEnter, &Anim.CoverCrouchEnter, &Anim.CoverStandCorner, &Anim.CoverCrouchCorner, &Anim.CoverStandIdle,
			&Anim.CoverCrouchIdle })
		{
			const int32 Index = List->IndexOfByKey(Clip);
			if (Index != INDEX_NONE && (*List)[0] != (*List)[1])
			{
				return Index;
			}
		}
		return -1;
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

	/** The body's yaw drawn on screen: the shoulder line's normal (the mesh + its animation), deg; false without the bones. */
	bool BodyYaw(const AOperativeCharacter& Op, float& OutYaw)
	{
		const USkeletalMeshComponent* Mesh = Op.GetMesh();
		if (!Mesh || Mesh->GetBoneIndex(TEXT("upperarm_l")) == INDEX_NONE || Mesh->GetBoneIndex(TEXT("upperarm_r")) == INDEX_NONE)
		{
			return false;
		}
		const FVector Line = Mesh->GetSocketLocation(TEXT("upperarm_r")) - Mesh->GetSocketLocation(TEXT("upperarm_l"));
		if (Line.Size2D() < 5.f)
		{
			return false;
		}
		OutYaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(-Line.X, Line.Y)));
		return true;
	}

	FString Brief(const AOperativeCharacter& Op)
	{
		return FString::Printf(TEXT("cover %d facing %s shimmy %d aim %d manual %d (%s) atCorner %d leaning %d stance %d actorYaw %.0f"), Op.bInCover ? 1 : 0,
			Op.CoverFacing == ECoverFacing::Left ? TEXT("L") : TEXT("R"), Op.bShimmying ? 1 : 0, Op.IsCornerAimActive() ? 1 : 0, Op.IsManualCornerPeek() ? 1 : 0,
			Op.GetManualCornerPeekSide() == ECoverFacing::Left ? TEXT("L") : TEXT("R"), Op.bAtCoverCorner ? 1 : 0, Op.bIsCornerLeaning ? 1 : 0,
			static_cast<int32>(Op.GetStance()), Op.GetActorRotation().Yaw);
	}

	/** One frame of the watch on the peeker: every change logged with the frame since the click. */
	void Watch(FState& State, AOperativeCharacter& Op, float DeltaTime)
	{
		const int64 Frame = static_cast<int64>(GFrameCounter - State.ClickFrame);
		UOperativeAnimInstance* Anim = Op.GetMesh() ? Cast<UOperativeAnimInstance>(Op.GetMesh()->GetAnimInstance()) : nullptr;
		if (Op.bInCover && !State.bWasInCover)
		{
			++State.CoverEntries;
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke watch f%lld: COVER ENTRY #%d (%s)"), Frame, State.CoverEntries, *Brief(Op));
			State.bFacingValid = false;
		}
		else if (!Op.bInCover && State.bWasInCover)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke watch f%lld: left the cover (%s)"), Frame, *Brief(Op));
		}
		State.bWasInCover = Op.bInCover;
		if (Op.bInCover)
		{
			if (State.bFacingValid && Op.CoverFacing != State.LastFacing)
			{
				++State.FacingFlips;
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke watch f%lld: FACING FLIP #%d %s -> %s (%s)"), Frame, State.FacingFlips,
					State.LastFacing == ECoverFacing::Left ? TEXT("L") : TEXT("R"), Op.CoverFacing == ECoverFacing::Left ? TEXT("L") : TEXT("R"), *Brief(Op));
			}
			State.LastFacing = Op.CoverFacing;
			State.bFacingValid = true;
		}
		if (Op.IsManualCornerPeek() != State.bLastManual || Op.IsCornerAimActive() != State.bLastAim || Op.bShimmying != State.bLastShimmy
			|| Op.GetStance() != State.LastStance)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke watch f%lld: state %s"), Frame, *Brief(Op));
			State.bLastManual = Op.IsManualCornerPeek();
			State.bLastAim = Op.IsCornerAimActive();
			State.bLastShimmy = Op.bShimmying;
			State.LastStance = Op.GetStance();
		}
		if (Anim)
		{
			for (const FAnimMontageInstance* Instance : Anim->MontageInstances)
			{
				if (!Instance || !Instance->Montage || State.SeenMontages.Contains(Instance->GetInstanceID()))
				{
					continue;
				}
				State.SeenMontages.Add(Instance->GetInstanceID());
				const UAnimSequenceBase* Clip = MontageClip(Instance->Montage);
				const EClipKind Kind = Classify(*Anim, Clip);
				const int32 Side = ClipSide(*Anim, Clip);
				if (Op.bInCover)
				{
					State.ClipsInCover.Add(FString::Printf(TEXT("f%lld %s"), Frame, Clip ? *Clip->GetName() : TEXT("-")));
					State.FireEnterStarts += Kind == EClipKind::FireEnter ? 1 : 0;
					State.CoverEnterStarts += Kind == EClipKind::CoverEnter ? 1 : 0;
					State.OneShotsAfterEntry += (Kind == EClipKind::FireExit || Kind == EClipKind::StanceSwitch || (Kind == EClipKind::CoverEnter && State.CoverEnterStarts > 1)) ? 1 : 0;
				}
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke watch f%lld: MONTAGE START %s [%s, side %s] (%s)"), Frame, Clip ? *Clip->GetName() : TEXT("-"),
					KindName(Kind), Side == 0 ? TEXT("_L = his right") : Side == 1 ? TEXT("_R = his left") : TEXT("-"), *Brief(Op));
			}
		}
		float Yaw = 0.f;
		if (!Op.bInCover || !BodyYaw(Op, Yaw))
		{
			State.bBodyYawValid = false;
			return;
		}
		const float NormalYaw = static_cast<float>(Op.GetCoverSlot().WallNormal.Rotation().Yaw);
		const float OffOut = FRotator::NormalizeAxis(Yaw - NormalYaw); // 0 = back to the wall, facing out; +-180 = facing the wall
		++State.FramesInCover;
		if (State.FramesInCover <= YawTraceFrames && State.FramesInCover % 3 == 1)
		{
			State.YawTrace += FString::Printf(TEXT(" %.0f"), OffOut);
		}
		const auto CloseTurn = [&State, Frame, &Op]()
		{
			if (FMath::Abs(State.TurnSum) >= TurnNoiseDeg)
			{
				State.TurnSegments.Add(State.TurnSum);
				State.TurnLog.Add(FString::Printf(TEXT("%+.0f"), State.TurnSum));
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke watch f%lld: body turn segment of %+.0f deg in cover (%s)"), Frame, State.TurnSum, *Brief(Op));
			}
			State.TurnSum = 0.f;
			State.TurnSign = 0;
		};
		if (State.bBodyYawValid)
		{
			const float Turn = FRotator::NormalizeAxis(Yaw - State.LastBodyYaw);
			if (FMath::Abs(Turn) < TurnStillDeg)
			{
				if (++State.TurnStillFrames >= TurnPauseFrames)
				{
					CloseTurn();
				}
			}
			else
			{
				const int32 Sign = Turn > 0.f ? 1 : -1;
				if (State.TurnSign != 0 && Sign != State.TurnSign)
				{
					CloseTurn();
				}
				State.TurnSign = Sign;
				State.TurnSum += Turn;
				State.TurnStillFrames = 0;
			}
		}
		State.LastBodyYaw = Yaw;
		State.bBodyYawValid = true;
	}

	/**
	 * The body's turns from its turn segments: segments under TurnMinDeg are wobbles (dropped), then neighbours turning the same way are one
	 * turn. The bug: [+84 (to the wall) +180 ... -119] = 2; one clean turn into the peek = 1.
	 */
	int32 CountTurns(const TArray<float>& Segments)
	{
		TArray<float> Merged;
		for (const float Segment : Segments)
		{
			if (!Merged.IsEmpty() && FMath::Sign(Merged.Last()) == FMath::Sign(Segment))
			{
				Merged.Last() += Segment;
			}
			else
			{
				Merged.Add(Segment);
			}
		}
		int32 Count = 0;
		float Last = 0.f;
		for (const float Turn : Merged)
		{
			if (FMath::Abs(Turn) >= TurnMinDeg && FMath::Sign(Turn) != FMath::Sign(Last))
			{
				++Count;
				Last = Turn;
			}
		}
		return Count;
	}

	/** The still camera beside the corner (on the squad's side, looking at the end). */
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

	void TakeStripStill(UWorld* World, FState& State, const AOperativeCharacter& Op)
	{
		// From his last steps to the corner (1.5 m off its end, or in cover) on.
		if (State.bShot && !State.bStripStarted && (Op.bInCover || FMath::Abs(Op.GetActorLocation().X - EndX(State)) <= 150.f))
		{
			State.bStripStarted = true;
			State.NextStripTime = State.StageTime;
		}
		if (!State.bShot || !State.bStripStarted || State.StripIndex >= StripStills || State.StageTime < State.NextStripTime)
		{
			return;
		}
		const float In = Inward(State);
		const FVector Corner(EndX(State), WallFaceY, State.GroundZ + 100.f);
		PlaceCamera(World, State, Corner + FVector(In * 250.f, -650.f, 450.f), Corner + FVector(In * 120.f, -80.f, 0.f));
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("CornerPeekFix") / FString::Printf(TEXT("peek_%s_order%d%s_%s_%02d.png"),
			State.bEntryTurnOn ? TEXT("fixed") : TEXT("old"), State.Variant + 1, State.Pair == 0 ? TEXT("a") : TEXT("b"), IsWestEnd(State) ? TEXT("west") : TEXT("east"),
			State.StripIndex);
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogCodexTactics, Display, TEXT("SquadCornerPeekSmoke: still %s at %.2f s after the click"), *Path, State.StageTime);
		++State.StripIndex;
		State.NextStripTime += StripEverySeconds;
	}

	AOperativeCharacter* Member(const FState& State, int32 Index)
	{
		return State.Members.IsValidIndex(Index) ? State.Members[Index].Get() : nullptr;
	}

	/** The squad of three standing 6 m south of the wall's middle, facing it, in group mode (the Commander leading). */
	bool PlaceSquad(UWorld* World, FState& State, USquadSubsystem* Squad)
	{
		Squad->ExitSoloMode(false, 0.f);
		AOperativeCharacter* Lead = Member(State, 0);
		if (!Lead)
		{
			return false;
		}
		Squad->SetLeader(Lead);
		for (int32 Index = 0; Index < State.Members.Num(); ++Index)
		{
			AOperativeCharacter* Each = State.Members[Index].Get();
			if (!Each)
			{
				return false;
			}
			Each->LeaveCover(TEXT("smoke reset"));
			Each->ClearPendingCover();
			Each->StopOperative();
			Each->SetFormationWallPose(false);
			Each->SetStance(EOperativeStance::Standing);
			const FVector At(2300.f + (Index == 0 ? 0.f : (Index == 1 ? -150.f : 150.f)), StartY - (Index == 0 ? 0.f : 200.f),
				State.GroundZ + Each->GetSimpleCollisionHalfHeight() + 5.f);
			Each->TeleportTo(At, FRotator(0.f, 90.f, 0.f), false, true);
		}
		Squad->ResetFormationState(FVector(0.f, 1.f, 0.f));
		return true;
	}

	/** The player's hover on the wall's end (the peek ghost of the member nearest it), then the click there; the watch restarts. */
	bool HoverAndClickEnd(UWorld* World, FState& State, USquadSubsystem* Squad, ACodexTacticsPlayerController* PC, AOperativeCharacter* Leader)
	{
		// The hover on the wall's end (20 cm in from it): the peek ghost of the member nearest it.
		FHitResult EndHit;
		if (!WallHit(World, State, EndX(State) + Inward(State) * 20.f, EndHit))
		{
			Check(State, false, TEXT("the cursor hit on the wall's end"));
			return Finish(State, false);
		}
		const bool bGhost = PC->UpdateCornerPeekHover(Leader, &EndHit);
		const ACoverGhostActor* Ghost = PC->GetCornerPeekGhost();
		AOperativeCharacter* GhostOf = Ghost ? const_cast<AOperativeCharacter*>(Ghost->GetOperative()) : nullptr;
		Check(State, bGhost && Ghost && Ghost->IsCornerPeekPose() && GhostOf, FString::Printf(TEXT("%s: the hover on the wall's end shows the squad's peek ghost (of %s)"),
			*PeekLabel(State), GhostOf ? *GhostOf->DisplayName.ToString() : TEXT("-")));
		// The click there (the player's click path).
		State.Peeker = GhostOf;
		State.PeeksBefore = Squad->GetFormationPeekCount();
		State.ManualPeeksBefore = GhostOf ? GhostOf->GetManualCornerPeekCount() : 0;
		State.ArrivalEntriesBefore = GhostOf ? GhostOf->GetCornerPeekArrivalEntryCount() : 0;
		State.ClickFrame = GFrameCounter;
		State.bWasInCover = GhostOf && GhostOf->bInCover;
		State.bFacingValid = false;
		State.SeenMontages.Reset();
		if (GhostOf)
		{
			if (const UOperativeAnimInstance* Anim = GhostOf->GetMesh() ? Cast<UOperativeAnimInstance>(GhostOf->GetMesh()->GetAnimInstance()) : nullptr)
			{
				for (const FAnimMontageInstance* Instance : Anim->MontageInstances)
				{
					if (Instance)
					{
						State.SeenMontages.Add(Instance->GetInstanceID()); // only the montages the peek starts count
					}
				}
			}
		}
		State.CoverEntries = State.FacingFlips = State.FireEnterStarts = State.CoverEnterStarts = State.OneShotsAfterEntry = 0;
		State.Turns = State.TurnSign = State.TurnStillFrames = State.FramesInCover = 0;
		State.TurnSum = 0.f;
		State.TurnLog.Reset();
		State.TurnSegments.Reset();
		State.YawTrace.Reset();
		State.bBodyYawValid = false;
		State.ClipsInCover.Reset();
		State.HoldTime = 0.f;
		State.StripIndex = 0;
		State.NextStripTime = 0.f;
		State.bStripStarted = false;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s: the click on the wall's end at (%.0f, %.0f), frame %llu"), *PeekLabel(State), EndHit.ImpactPoint.X,
			EndHit.ImpactPoint.Y, static_cast<unsigned long long>(GFrameCounter));
		PC->HandleWorldHit(EndHit);
		Check(State, GhostOf && Squad->GetFormationPeeker() == GhostOf, FString::Printf(TEXT("%s: the click orders the squad's corner peek for the ghost's member (%s)"),
			*PeekLabel(State), Squad->GetFormationPeeker() ? *Squad->GetFormationPeeker()->DisplayName.ToString() : TEXT("-")));
		Next(State, 4);
		return true;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State, float DeltaTime)
	{
		State.Time += DeltaTime;
		State.StageTime += DeltaTime;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 500.f)
		{
			Check(State, false, FString::Printf(TEXT("timeout (stage %d)"), State.Stage));
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		if (State.Stage == 0)
		{
			if (State.StageTime < 3.f || !Squad || !PC || Squad->GetMembers().Num() < 3)
			{
				return true;
			}
			if (!FormationRuntime::IsEnabled())
			{
				Check(State, false, TEXT("Codex.Formation.Sectors is 1 (the squad corner peek is the formation's)"));
				return Finish(State, false);
			}
			TArray<AOperativeCharacter*> Roster = Squad->GetMembers();
			Roster.Sort([](const AOperativeCharacter& X, const AOperativeCharacter& Y) { return X.SquadIndex < Y.SquadIndex; });
			for (int32 Index = 0; Index < 3; ++Index)
			{
				State.Members.Add(Roster[Index]);
				if (USkeletalMeshComponent* Mesh = Roster[Index]->GetMesh())
				{
					Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones; // the pose (the body's yaw) headless too
				}
			}
			// The ground under the start (the lab's ground_z, measured).
			FHitResult Ground;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(SquadCornerPeekSmokeGround), false);
			for (AOperativeCharacter* Each : Roster)
			{
				Params.AddIgnoredActor(Each);
			}
			State.GroundZ = World->LineTraceSingleByChannel(Ground, FVector(2300.f, StartY, 2000.f), FVector(2300.f, StartY, -2000.f), ECC_Visibility, Params)
				? static_cast<float>(Ground.ImpactPoint.Z) : 0.f;
			FHitResult Probe;
			const bool bWall = WallHit(World, State, 2300.f, Probe) && WallHit(World, State, WallWestX + 20.f, Probe) && WallHit(World, State, WallEastX - 20.f, Probe);
			Check(State, bWall, FString::Printf(TEXT("the lab wall W_B_3 stands where the layout puts it (south face y %.0f, x %.0f..%.0f; ground z %.0f)"), WallFaceY,
				WallWestX, WallEastX, State.GroundZ));
			if (!bWall || !PlaceSquad(World, State, Squad))
			{
				return Finish(State, false);
			}
			Next(State, 1);
			return true;
		}
		if (!Squad || !PC || !Squad->GetLeader())
		{
			return Finish(State, false);
		}
		AOperativeCharacter* Leader = Squad->GetLeader();
		switch (State.Stage)
		{
		case 1:
		{
			if (State.StageTime < 3.f)
			{
				return true; // settle after the teleport
			}
			// The player's two clicks on the wall's middle: the cover ghost, then its confirm (the group cover order = the formation wall layout).
			FHitResult Hit;
			if (WallHit(World, State, 2300.f, Hit))
			{
				PC->HandleWorldHit(Hit);
			}
			Next(State, 2);
			return true;
		}
		case 2:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			FHitResult Hit;
			if (WallHit(World, State, 2300.f, Hit))
			{
				PC->HandleWorldHit(Hit);
			}
			Check(State, Squad->HasFormationWallOrder(), FString::Printf(TEXT("%s: the clicks on the wall - the group cover order as the formation wall layout"), *PeekLabel(State)));
			Next(State, 3);
			return true;
		}
		case 3:
		{
			bool bSettled = Squad->GetFormationKind() == EFormationKind::Wall && !Squad->IsFormationSliding();
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
			{
				bSettled &= Weak.IsValid() && Weak->IsInFormationWallPose() && Weak->GetVelocity().Size2D() < 5.f;
			}
			if ((!bSettled || State.StageTime < 2.f) && State.StageTime < 25.f)
			{
				return true;
			}
			Check(State, bSettled, FString::Printf(TEXT("%s: the squad hugs the wall in the wall layout, crouched (%.1f s)"), *PeekLabel(State), State.StageTime));
			State.bClickInWorldTick = true; // the hover + click in the next world tick (OnWorldPreActorTick -> HoverAndClickEnd -> stage 4)
			Next(State, 6);
			return true;
		}
		case 5:
		{
			// The 2nd order on the same wall: the squad still stacked at the other end, the first peeker still in his corner.
			if (State.StageTime < 1.f)
			{
				return true;
			}
			State.bClickInWorldTick = true;
			Next(State, 6);
			return true;
		}
		case 6:
		{
			if (State.StageTime > 5.f)
			{
				Check(State, false, TEXT("the end click in the world tick never ran"));
				return Finish(State, false);
			}
			return true; // HoverAndClickEnd moves on to stage 4
		}
		case 4:
		{
			AOperativeCharacter* Op = State.Peeker.Get();
			if (!Op)
			{
				Check(State, false, TEXT("the peeker is gone"));
				return Finish(State, false);
			}
			Watch(State, *Op, DeltaTime);
			TakeStripStill(World, State, *Op);
			const bool bPose = Op->bInCover && Op->IsManualCornerPeek() && Op->IsCornerAimActive() && Op->bAtCoverCorner && !Op->bShimmying;
			State.HoldTime = bPose ? State.HoldTime + DeltaTime : 0.f;
			const bool bStripDone = !State.bShot || State.StripIndex >= StripStills;
			if (!(State.HoldTime >= HoldSeconds && bStripDone) && State.StageTime < PeekMaxSeconds)
			{
				return true;
			}
			const UOperativeAnimInstance* Anim = Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
			FString Loop = TEXT("-");
			int32 LoopSide = -1;
			EClipKind LoopKind = EClipKind::Other;
			if (Anim)
			{
				float Weight = 0.f;
				float SlotWeight = 0.f;
				Anim->GetCoverPlayback(Loop, Weight, SlotWeight);
				for (const FAnimMontageInstance* Instance : Anim->MontageInstances)
				{
					if (Instance && Instance->Montage && Instance->IsActive() && !Instance->IsStopped())
					{
						const UAnimSequenceBase* Clip = MontageClip(Instance->Montage);
						LoopKind = Classify(*Anim, Clip);
						LoopSide = ClipSide(*Anim, Clip);
					}
				}
			}
			// The peek's side: the end along his wall's right tangent (CoverSlot.RightTangent) or against it.
			const FCoverSlot& Slot = Op->GetCoverSlot();
			State.PeekSide = FVector::DotProduct(FVector(EndX(State), WallFaceY, Slot.WallPoint.Z) - Slot.WallPoint, Slot.RightTangent()) >= 0.f ? ECoverFacing::Right
				: ECoverFacing::Left;
			const int32 WantSide = CoverFacingRules::ClipIndex(State.PeekSide);
			if (FMath::Abs(State.TurnSum) >= TurnNoiseDeg)
			{
				State.TurnSegments.Add(State.TurnSum);
				State.TurnLog.Add(FString::Printf(TEXT("%+.0f"), State.TurnSum));
			}
			State.Turns = CountTurns(State.TurnSegments);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s summary: entries %d, facing flips %d, peek step-outs %d, cover enter clips %d, extra one-shots %d, body turns in cover %d (segments [%s]), %d frames in cover; clips in cover: %s"),
				*PeekLabel(State), State.CoverEntries, State.FacingFlips, State.FireEnterStarts, State.CoverEnterStarts, State.OneShotsAfterEntry, State.Turns,
				*FString::Join(State.TurnLog, TEXT(" ")), State.FramesInCover, *FString::Join(State.ClipsInCover, TEXT(", ")));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: %s body yaw off the wall normal after the entry (every 3rd frame, 0 = back to the wall, 180 = facing it):%s"),
				*PeekLabel(State), *State.YawTrace);
			Check(State, State.HoldTime >= HoldSeconds, FString::Printf(TEXT("%s: %s leans out round the corner in the peek pose and holds it (%.1f s held, %.1f s after the click; %s)"),
				*PeekLabel(State), *Op->DisplayName.ToString(), State.HoldTime, State.StageTime, *Brief(*Op)));
			Check(State, Squad->GetFormationPeekCount() == State.PeeksBefore + 1 && Op->GetManualCornerPeekCount() == State.ManualPeeksBefore + 1,
				FString::Printf(TEXT("%s: one peek order, one peek reached (squad %d -> %d, his %d -> %d)"), *PeekLabel(State), State.PeeksBefore, Squad->GetFormationPeekCount(),
					State.ManualPeeksBefore, Op->GetManualCornerPeekCount()));
			Check(State, State.CoverEntries == 1, FString::Printf(TEXT("%s: one cover entry (%d)"), *PeekLabel(State), State.CoverEntries));
			Check(State, Op->GetCornerPeekArrivalEntryCount() == State.ArrivalEntriesBefore + 1 && State.CoverEnterStarts == 0,
				FString::Printf(TEXT("%s: he walked in along the wall - the entry turns straight into the peek, no wall-facing enter clip (eased entries %d -> %d, enter clips %d)"),
					*PeekLabel(State), State.ArrivalEntriesBefore, Op->GetCornerPeekArrivalEntryCount(), State.CoverEnterStarts));
			Check(State, State.FireEnterStarts == 1, FString::Printf(TEXT("%s: exactly one peek step-out montage start (%d)"), *PeekLabel(State), State.FireEnterStarts));
			Check(State, State.CoverEnterStarts <= 1 && State.OneShotsAfterEntry == 0, FString::Printf(TEXT("%s: no second entry / exit / stance one-shot at the corner (enter clips %d, extra %d)"),
				*PeekLabel(State), State.CoverEnterStarts, State.OneShotsAfterEntry));
			Check(State, State.FacingFlips == 0, FString::Printf(TEXT("%s: no facing-side flip while in cover (%d)"), *PeekLabel(State), State.FacingFlips));
			Check(State, Op->CoverFacing == State.PeekSide && LoopKind == EClipKind::FireIdle && (LoopSide == WantSide || LoopSide < 0),
				FString::Printf(TEXT("%s: the held pose is the fire idle of the peek side (%s; facing %s, loop %s [%s], clip side %d, want %d)"), *PeekLabel(State),
					State.PeekSide == ECoverFacing::Left ? TEXT("his left") : TEXT("his right"), Op->CoverFacing == ECoverFacing::Left ? TEXT("L") : TEXT("R"), *Loop,
					KindName(LoopKind), LoopSide, WantSide));
			Check(State, FMath::Abs(Op->GetActorLocation().X - EndX(State)) <= 130.f && Op->IsManualCornerPeek() && Op->GetManualCornerPeekSide() == State.PeekSide,
				FString::Printf(TEXT("%s: the peek is by the man at the ORDERED end, round THAT corner (%s %.0f cm from it, peek side %s, want %s)"), *PeekLabel(State),
					*Op->DisplayName.ToString(), FMath::Abs(Op->GetActorLocation().X - EndX(State)), Op->GetManualCornerPeekSide() == ECoverFacing::Left ? TEXT("L") : TEXT("R"),
					State.PeekSide == ECoverFacing::Left ? TEXT("L") : TEXT("R")));
			Check(State, State.Turns == 1, FString::Printf(TEXT("%s: ONE body turn from his walk into the peek, no swing back (turns %d, segments [%s]; the video: two spins)"),
				*PeekLabel(State), State.Turns, *FString::Join(State.TurnLog, TEXT(" "))));
			if (State.bShot)
			{
				if (APlayerController* Player = UGameplayStatics::GetPlayerController(World, 0))
				{
					Player->SetViewTarget(Player->GetPawn());
				}
			}
			if (State.Pair == 0)
			{
				State.Pair = 1; // the other end of the same wall, straight from the stack (the game designer's second bug, 2026-11-01)
				Next(State, 5);
				return true;
			}
			if (State.Variant == 0)
			{
				++State.Variant;
				State.Pair = 0;
				if (!PlaceSquad(World, State, Squad))
				{
					return Finish(State, false);
				}
				Next(State, 1);
				return true;
			}
			return Finish(State, true);
		}
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
		State->bShot = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("Shot"), ESearchCase::IgnoreCase); });
		const IConsoleVariable* EntryTurn = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Cover.CornerPeekEntryTurn"));
		State->bEntryTurnOn = !EntryTurn || EntryTurn->GetInt() != 0;
		UE_LOG(LogCodexTactics, Display, TEXT("SquadCornerPeekSmoke: Codex.Cover.CornerPeekEntryTurn %d%s"), State->bEntryTurnOn ? 1 : 0,
			State->bEntryTurnOn ? TEXT("") : TEXT(" (the old entry: its turn checks are expected to fail - the A/B)"));
		TWeakObjectPtr<UWorld> WeakWorld(World);
		State->PreTickHandle = FWorldDelegates::OnWorldPreActorTick.AddLambda([WeakWorld, State](UWorld* Ticking, ELevelTick, float)
		{
			if (!State->bClickInWorldTick || Ticking != WeakWorld.Get())
			{
				return;
			}
			State->bClickInWorldTick = false;
			USquadSubsystem* Squad = Ticking->GetSubsystem<USquadSubsystem>();
			ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(Ticking, 0));
			if (Squad && PC && Squad->GetLeader())
			{
				HoverAndClickEnd(Ticking, *State, Squad, PC, Squad->GetLeader());
			}
		});
		// One step per frame: the watch logs every frame of the peek.
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float DeltaTime)
		{
			const UWorld* Live = WeakWorld.Get();
			return Step(WeakWorld, *State, Live ? Live->GetDeltaSeconds() : DeltaTime);
		}), 0.f);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.SquadCornerPeekSmoke"),
		TEXT("Dev check (the game designer's play-test 2026-11-01: the squad corner peek animation played twice): on L_FormationLab the squad hugs W_B_3, the hover + click on its end -> one cover entry, one peek step-out, no facing flip, the fire idle of the peek side; both ends; PASS/FAIL, exits. 'Shot' = a frame strip in Saved/Screenshots/CornerPeekFix/."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Run(Args, World); }));
}

#endif // !UE_BUILD_SHIPPING
