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
//   D. the leader NORTH of the wall, the click on the south face's west end -> the corner on the NORTH face (the face looking at him);
//   E. all three box-selected out in the open -> the man nearest the west end takes the corner and peeks, the others in cover behind him;
//   F. (the game designer's answer Q2, 2026-11-08) the squad in FORMATION in the open, the followers far behind the leader: the click on the
//      west end = the squad's wall order there, its corner peek fires as soon as the LEADER is at the wall - while a follower is still on his
//      way - and the corner man peeks round the west end;
//   G. (answer Q3) solo, a fight, the TACTICAL PAUSE: the hover over the west end shows the run-and-peek ghost (no cover ghost), the click
//      plans "run there and peek" (nobody moves in the pause); the pause's end -> he runs there and peeks on arrival.
// A/B: -PreCmds "Codex.Cover.RunCornerPeek 0" = the old rule (A, D and E fail: the click is a cover ghost).
// Godot reference: none (UE-only; docs/systems/cover-and-peek.md "Run-and-peek").

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Characters/EnemyCharacter.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Tactics/CoverGhostActor.h"

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
		if (!FaceHit(World, State, State.ClickX, Hit))
		{
			Check(State, false, FString::Printf(TEXT("the cursor hit on W_B_3's south face at x %.0f"), State.ClickX));
			return;
		}
		State.bLastGhost = State.bHoverToo && PC->UpdateCornerPeekHover(Leader, &Hit);
		State.LastGhostOf = State.bLastGhost && PC->GetCornerPeekGhost() ? PC->GetCornerPeekGhost()->GetOperative() : nullptr;
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
		return true;
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
			if (!WatchRunPeek(State, *Leader, DeltaTime, TEXT("A (running, solo)"), false))
			{
				return true;
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
			// D: north of the wall (the camera / the click on the south face): the corner on the face looking at HIM.
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
			if (!State.bClicked || !WatchRunPeek(State, *Leader, DeltaTime, TEXT("D (from the north)"), true))
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
