// Dev-only headless check of the wall-edge COVER SWITCH (game designer 2026-10-20: "standing at the end of a wall in cover, double-click
// another cover across from me - he runs over into it with anim_M4_std_jog_fwd_L_to_cvr_std_idle_L / _R_to_cvr_std_idle_R"):
//   Scripts/smoke_parallel.ps1 -Commands CoverSwitchSmoke
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
//     -ExecCmds="CodexTactics.CoverSwitchSmoke Shot"   -> Saved/Screenshots/cover_switch_preview.png (ghost + dash line), _dash.png,
//     _arrival.png (the run-in clip mid-turn), _settled.png, _low.png (the crouched run-in); add -UseFixedTimeStep -FPS=60 for steady frames
// A runtime arena on L_MovementTest (SmokeUtils::FindClearArena, nothing saved), exploring, Solo: wall A (3 m high, 6 m long, R -300..300)
// 3 m ahead, wall B (3 m high, 3 m, R 550..850) and the low wall C (1 m, 3 m, R -850..-550) 4.5 m ahead - the gaps 2.5 m wide. Every
// order goes through the controller's cover confirm (ShowCoverPreview + ConfirmCoverPreview(bDoubleClick)):
//  1 the leader in cover at A's end towards B; the hover over B shows the switch ghost (not over his own wall); the click ghost's
//    dash line; a DOUBLE-click on B = the switch: he leaves cover (exposed on the dash), sprints, arrives through
//    anim_M4_std_jog_fwd_L_to_cvr_std_idle_L (started mid-clip), ends in cover at B's corner slot facing back along the wall, the mesh
//    back on the capsule (no run-in offset left);
//  2 back from B's end to A: anim_M4_std_jog_fwd_R_to_cvr_std_idle_R;
//  3 a slow (single) confirm from A's end: the ordinary cover order (no run-in clip);
//  4 in the middle of A (not at an edge) a double-click on B: the ordinary running cover order, no switch;
//  5 from A's end with the range tuned down to 3 m: out of range - the ordinary order, no dash line;
//  6 from A's other end a double-click on the LOW wall C: the crouched run-in (std_jog_fwd_R_to_cvr_crch_idle_R), crouched at the end;
//  7 group mode: the leader switches (run-in clip), the two others join at B by the group cover rules;
//  8 a wave fight in the tactical pause: the switch is only planned (pending switch, nobody moves); the release -> the run-in arrival;
//  9 corner to corner (game designer 2026-10-20, second pass): from A's end (facing left, towards B) hovering B's FAR end shows the ghost at
//    B's NEAR corner on its front face in the mirrored pose (facing right) + the dash path; a double-click on B's SIDE face (the face
//    towards him) still dashes diagonally to that front-face corner and he arrives facing right (mirrored), at the corner.
//    Shot mode adds cover_corner_switch_preview.png / _dash / _settled;
// 10 the committed target is the hovered ghost (user bug 2026-10-20: "the ghost is right, I click it again - he ends round the corner"),
//    through the controller's click path (HandleWorldHit, the real double-click timing): from A's end, hover B's near edge - its END face
//    (towards A) and its front face near the edge - then click: the first click starts no order and its ghost stays where the hover ghost
//    stood (same corner, same pose); a double-click / a slow second click commits exactly that slot and side (switch / ordinary run).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeAnimInstance.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/HUD.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/CoverGhostActor.h"
#include "Tactics/CoverSwitchRules.h"
#include "Tactics/CoverTraceRules.h"
#include "UnrealClient.h"

namespace CoverSwitchSmoke
{
	constexpr float StepSeconds = 0.05f;
	constexpr float NearFaceA = 300.f;
	constexpr float NearFaceBC = 450.f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bShot = false;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		float GroundZ = 0.f;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		TArray<TWeakObjectPtr<AStaticMeshActor>> Walls;
		/** The clicked slots: A at its end towards B / at its other end / its middle, B near its end, C near its end. */
		FCoverSlot SlotAEnd;
		FCoverSlot SlotAOtherEnd;
		FCoverSlot SlotAMiddle;
		FCoverSlot SlotB;
		FCoverSlot SlotC;
		/** The current dash: what is expected, what was seen. */
		FCoverSlot Target;
		FString ExpectedClip;
		bool bExpectSwitch = false;
		bool bSawSprint = false;
		bool bSawOutOfCover = false;
		bool bSawSwitchEntry = false;
		int32 SwitchCountBefore = 0;
		float StartDistance = 0.f;
		int32 ShotsTaken = 0;
		/** Shot mode: when the run-in clip was first seen on this dash, stage time. */
		float SwitchSeenAt = -1.f;
		/** 10: the click case (0 end face double, 1 front face double, 2 front face slow), its hit, the hovered ghost. */
		int32 ClickCase = 0;
		FHitResult ClickHit;
		FVector HoverAt = FVector::ZeroVector;
		ECoverFacing HoverFacing = ECoverFacing::Right;
		/** 9: his facing at A's end before the corner-to-corner dash; the dash's start. */
		ECoverFacing FacingBefore = ECoverFacing::Right;
		FVector DashStart = FVector::ZeroVector;
		FString LastPlayback;
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
		for (const TWeakObjectPtr<AStaticMeshActor>& Wall : State.Walls)
		{
			if (Wall.IsValid())
			{
				Wall->Destroy();
			}
		}
		FPlatformMisc::RequestExit(false, TEXT("CoverSwitchSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
	}

	AOperativeCharacter* Member(const FState& State, int32 Index)
	{
		return State.Members.IsValidIndex(Index) ? State.Members[Index].Get() : nullptr;
	}

	UOperativeAnimInstance* AnimOf(const AOperativeCharacter* Op)
	{
		return Op && Op->GetMesh() ? Cast<UOperativeAnimInstance>(Op->GetMesh()->GetAnimInstance()) : nullptr;
	}

	/** A box wall (40 cm thick) whose near face lies NearX ahead of the arena origin, from MinR to MaxR along R, HeightCm high. */
	void SpawnWall(UWorld* World, FState& State, float NearX, float MinR, float MaxR, float HeightCm)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		const FVector Centre = State.P + State.F * (NearX + 20.f) + State.R * ((MinR + MaxR) * 0.5f);
		const FTransform Transform(State.F.Rotation(), FVector(Centre.X, Centre.Y, State.GroundZ + HeightCm * 0.5f),
			FVector(0.4f, (MaxR - MinR) / 100.f, HeightCm / 100.f));
		AStaticMeshActor* Block = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
		if (Block)
		{
			Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
			Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
			Block->FinishSpawning(Transform);
		}
		State.Walls.Add(Block);
	}

	/** The slot a click on a wall's near face at (NearX, AlongR), ClickHeight above the ground gives. */
	bool FindSlot(UWorld* World, const FState& State, float NearX, float AlongR, float ClickHeight, FCoverSlot& OutSlot)
	{
		const FVector Face = State.P + State.F * NearX + State.R * AlongR;
		return CoverTraceRules::FindCoverSlotAt(World, FVector(Face.X, Face.Y, State.GroundZ + ClickHeight), State.F, OutSlot);
	}

	float AlongR(const FState& State, const FVector& Point)
	{
		return static_cast<float>(FVector::DotProduct(Point - State.P, State.R));
	}

	float AlongF(const FState& State, const FVector& Point)
	{
		return static_cast<float>(FVector::DotProduct(Point - State.P, State.F));
	}

	/** Everyone out of cover, standing; the leader at Start (arena frame), the others parked behind him. */
	void ResetSquad(USquadSubsystem* Squad, FState& State, const FVector2D& Start)
	{
		for (int32 Index = 0; Index < State.Members.Num(); ++Index)
		{
			AOperativeCharacter* Each = Member(State, Index);
			if (!Each)
			{
				continue;
			}
			Each->LeaveCover(TEXT("smoke reset"));
			Each->ClearPendingCover();
			Each->StopOperative();
			Each->SetStance(EOperativeStance::Standing);
			const FVector2D Local = Index == 0 ? Start : FVector2D(-300.f, Index == 1 ? -150.f : 150.f);
			Each->TeleportTo(State.P + State.F * Local.X + State.R * Local.Y + FVector(0.f, 0.f, Each->GetSimpleCollisionHalfHeight() + 5.f),
				State.F.Rotation(), false, true);
		}
		Squad->SetLeader(Member(State, 0));
	}

	/** Settled in cover: in it, not moving / side-stepping, the run-in offset gone. */
	bool Settled(const AOperativeCharacter* Op)
	{
		return Op && Op->bInCover && !Op->bShimmying && !Op->IsMoving() && Op->GetVelocity().Size2D() < 5.f && Op->GetCoverEntryMeshOffset().Size() < 1.f;
	}

	/**
	 * The still camera (Shot mode): Eye / LookAt in the arena frame (F, R, up from the ground). bHud: the game HUD stays on (the switch
	 * preview's dash line is drawn by it), else it is hidden for a clean view of the body.
	 */
	void PlaceCamera(UWorld* World, FState& State, const FVector& EyeLocal, const FVector& LookLocal, bool bHud)
	{
		auto ToWorld = [&State](const FVector& Local) { return State.P + State.F * Local.X + State.R * Local.Y + FVector(0.f, 0.f, Local.Z); };
		const FVector Eye = ToWorld(EyeLocal);
		const FRotator Look = (ToWorld(LookLocal) - Eye).Rotation();
		if (!State.Camera.IsValid())
		{
			State.Camera = World->SpawnActor<ACameraActor>(Eye, Look);
			if (ACameraActor* Created = State.Camera.Get())
			{
				Created->GetCameraComponent()->SetFieldOfView(55.f);
				Created->GetCameraComponent()->bConstrainAspectRatio = false;
			}
		}
		if (ACameraActor* Cam = State.Camera.Get())
		{
			Cam->SetActorLocationAndRotation(Eye, Look);
			if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
			{
				PC->SetViewTarget(Cam);
				if (AHUD* Hud = PC->GetHUD())
				{
					Hud->bShowHUD = bHud;
				}
			}
		}
	}

	void RequestShot(FState& State, const TCHAR* Name)
	{
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / FString::Printf(TEXT("%s.png"), Name);
		FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ false, /*bAddFilenameSuffix*/ false);
		++State.ShotsTaken;
		UE_LOG(LogCodexTactics, Display, TEXT("CoverSwitch: screenshot %s"), *Path);
	}

	/** Starts a confirm of Target for the leader (ShowCoverPreview = the first click, ConfirmCoverPreview = the second). */
	void Confirm(ACodexTacticsPlayerController* PC, AOperativeCharacter* Op, FState& State, const FCoverSlot& Target, bool bDoubleClick,
		bool bExpectSwitch, const FString& ExpectedClip)
	{
		State.Target = Target;
		State.bExpectSwitch = bExpectSwitch;
		State.ExpectedClip = ExpectedClip;
		State.bSawSprint = false;
		State.bSawOutOfCover = false;
		State.bSawSwitchEntry = false;
		State.SwitchCountBefore = Op->GetCoverSwitchCount();
		State.StartDistance = static_cast<float>(FVector::Dist2D(Op->GetActorLocation(), Target.WorldLocation));
		State.SwitchSeenAt = -1.f;
		PC->ShowCoverPreview(Op, Target);
		PC->ConfirmCoverPreview(bDoubleClick);
	}

	/** Tracks the dash every step; logs every change of the FullBody cover montage (the clip played on arrival). */
	void TrackDash(FState& State, const AOperativeCharacter* Op)
	{
		State.bSawSprint |= Op->IsSprinting();
		State.bSawOutOfCover |= !Op->bInCover && Op->GetVelocity().Size2D() > 100.f;
		State.bSawSwitchEntry |= Op->IsCoverSwitchEntry();
		if (const UOperativeAnimInstance* Anim = AnimOf(Op))
		{
			FString Clip;
			float Weight = 0.f;
			float SlotWeight = 0.f;
			Anim->GetCoverPlayback(Clip, Weight, SlotWeight);
			if (Clip != State.LastPlayback)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("CoverSwitch: t=%.2f montage %s (w %.2f, slot %.2f), in cover %d, %.0f cm to the slot, speed %.0f"), State.StageTime,
					*Clip, Weight, SlotWeight, Op->bInCover ? 1 : 0, FVector::Dist2D(Op->GetActorLocation(), State.Target.WorldLocation), Op->GetVelocity().Size2D());
				State.LastPlayback = Clip;
			}
		}
	}

	/** After a confirm: wait until he settles in cover, then check the arrival. True when done (or timed out). */
	bool CheckArrival(FState& State, AOperativeCharacter* Op, const TCHAR* Label, float CapSeconds = 15.f, float SlotToleranceCm = 30.f)
	{
		TrackDash(State, Op);
		const bool bDone = Settled(Op) && State.StageTime > 0.5f && Op->GetCoverSlot().WallActor.Get() == State.Target.WallActor.Get();
		if (!bDone && State.StageTime < CapSeconds)
		{
			return false;
		}
		const UOperativeAnimInstance* Anim = AnimOf(Op);
		const bool bSwitched = Op->GetCoverSwitchCount() > State.SwitchCountBefore;
		Check(State, bDone, FString::Printf(TEXT("%s: in cover at the other wall after %.1f s (%.0f cm dash)"), Label, State.StageTime, State.StartDistance));
		Check(State, State.bSawSprint, FString::Printf(TEXT("%s: he ran (sprint)"), Label));
		Check(State, State.bSawOutOfCover, FString::Printf(TEXT("%s: out of cover on the way (exposed)"), Label));
		if (State.bExpectSwitch)
		{
			const FString Played = Anim ? Anim->GetLastCoverSwitchClip() : FString();
			Check(State, bSwitched && State.bSawSwitchEntry, FString::Printf(TEXT("%s: arrived through the switch run-in (count %d -> %d)"), Label,
				State.SwitchCountBefore, Op->GetCoverSwitchCount()));
			Check(State, Played == State.ExpectedClip && Anim->GetCoverClipLog().Contains(Played),
				FString::Printf(TEXT("%s: the run-in clip %s (expected %s), started at %.2f s, the clip body %.0f cm off the runner"), Label, *Played,
					*State.ExpectedClip, Anim ? Anim->GetLastCoverSwitchStartSeconds() : -1.f, Op->GetLastCoverSwitchMismatchCm()));
			Check(State, Anim && Anim->GetLastCoverSwitchStartSeconds() > 0.2f, FString::Printf(TEXT("%s: only the last metres of the clip played"), Label));
		}
		else
		{
			Check(State, !bSwitched && !State.bSawSwitchEntry, FString::Printf(TEXT("%s: the ordinary cover order - no run-in clip"), Label));
		}
		const float OffSlot = static_cast<float>(FVector::Dist2D(Op->GetActorLocation(), Op->GetCoverSlot().WorldLocation));
		const float FacingErr = FMath::Abs(FMath::FindDeltaAngleDegrees(Op->GetActorRotation().Yaw, CoverFacingRules::FacingYaw(Op->GetCoverSlot(), Op->CoverFacing)));
		Check(State, OffSlot < SlotToleranceCm && FacingErr < 20.f, FString::Printf(TEXT("%s: on his slot (%.0f cm), back to the wall (%.0f deg), facing %s"), Label, OffSlot, FacingErr,
			Op->CoverFacing == ECoverFacing::Left ? TEXT("left") : TEXT("right")));
		Check(State, Op->GetCoverEntryMeshOffset().Size() < 1.f,
			FString::Printf(TEXT("%s: the run-in offset is gone - the mesh back on the capsule (run-in offset %.1f cm; base offset %.1f cm = the tracked wall push)"),
				Label, Op->GetCoverEntryMeshOffset().Size(), Op->GetMeshBaseOffsetCm()));
		return true;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 300.f)
		{
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		AOperativeCharacter* Op = Member(State, 0);
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 3.f || !Squad || !PC || Squad->GetMembers().Num() < 3)
			{
				return true;
			}
			TArray<AOperativeCharacter*> Roster = Squad->GetMembers();
			Roster.Sort([](const AOperativeCharacter& X, const AOperativeCharacter& Y) { return X.SquadIndex < Y.SquadIndex; });
			TArray<const AActor*> Ignore;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				State.Members.Add(Roster[Index]);
				Ignore.Add(Roster[Index]);
			}
			Ignore.Add(PC->GetPawn());
			const FVector Feet = Roster[0]->GetActorLocation() - FVector(0.f, 0.f, Roster[0]->GetSimpleCollisionHalfHeight());
			const SmokeUtils::FClearArena Arena = SmokeUtils::FindClearArena(World, Feet, Roster[0]->GetActorForwardVector(),
				FBox2D(FVector2D(-500.f, -950.f), FVector2D(800.f, 950.f)), Ignore);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: arena %s at %s facing %s (%d tried)"), Arena.bFound ? TEXT("found") : TEXT("NOT found"),
				*Arena.Origin.ToCompactString(), *Arena.Forward.ToCompactString(), Arena.Tried);
			if (!Arena.bFound)
			{
				Check(State, false, TEXT("an empty, flat, navigable 13 x 19 m spot for the smoke's own set-up"));
				return Finish(State, false);
			}
			State.P = Arena.Origin;
			State.F = Arena.Forward;
			State.R = Arena.Right;
			State.GroundZ = Arena.Origin.Z;
			Squad->EnterSoloMode();
			ResetSquad(Squad, State, FVector2D(0.f, 100.f));
			SpawnWall(World, State, NearFaceA, -300.f, 300.f, 300.f);  // A
			SpawnWall(World, State, NearFaceBC, 550.f, 850.f, 300.f);  // B
			SpawnWall(World, State, NearFaceBC, -850.f, -550.f, 100.f); // C (low)
			Next(State, 1);
			return true;
		}
		case 1:
		{
			if (State.StageTime < 2.5f)
			{
				return true; // the navmesh rebuilds round the walls
			}
			const bool bFound = FindSlot(World, State, NearFaceA, 250.f, 90.f, State.SlotAEnd) && FindSlot(World, State, NearFaceA, -250.f, 90.f, State.SlotAOtherEnd)
				&& FindSlot(World, State, NearFaceA, 0.f, 90.f, State.SlotAMiddle) && FindSlot(World, State, NearFaceBC, 650.f, 90.f, State.SlotB)
				&& FindSlot(World, State, NearFaceBC, -600.f, 50.f, State.SlotC);
			Check(State, bFound && State.SlotAEnd.Height == ECoverHeight::HighCover && State.SlotB.Height == ECoverHeight::HighCover
				&& State.SlotC.Height == ECoverHeight::LowCover, TEXT("set-up: slots at A (both ends, middle), B (high) and C (low)"));
			if (!bFound)
			{
				return Finish(State, false);
			}
			Op->OrderTakeCover(State.SlotAEnd, false);
			Next(State, 2);
			return true;
		}
		case 2:
		{
			if (!Settled(Op) && State.StageTime < 12.f)
			{
				return true;
			}
			if (State.StageTime < 1.5f)
			{
				return true;
			}
			const float EdgeLeft = CoverFacingRules::EdgeDistance(Op->GetCoverSlot(), ECoverFacing::Left);
			Check(State, Op->bInCover && EdgeLeft >= 0.f && EdgeLeft <= Op->CoverSwitchEdgeReachCm,
				FString::Printf(TEXT("1: in cover at A's end towards B (%.0f cm from the edge)"), EdgeLeft));
			Check(State, Op->EvaluateCoverSwitch(State.SlotB) == ECoverSwitchVerdict::Switch,
				FString::Printf(TEXT("1: B is a switch target from there (%s)"), CoverSwitchRules::VerdictName(Op->EvaluateCoverSwitch(State.SlotB))));
			// The hover: over B the switch ghost, over his own wall none.
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverSwitchSmokeHover), false);
			for (const TWeakObjectPtr<AOperativeCharacter>& Each : State.Members)
			{
				Params.AddIgnoredActor(Each.Get());
			}
			FHitResult HitB;
			const FVector EyeB = State.P + State.F * 150.f + State.R * 650.f + FVector(0.f, 0.f, State.GroundZ - State.P.Z + 150.f);
			World->LineTraceSingleByChannel(HitB, EyeB, EyeB + State.F * 500.f, ECC_Visibility, Params);
			Check(State, HitB.GetActor() == State.Walls[1].Get() && PC->UpdateCoverSwitchHover(Op, &HitB) && PC->IsCoverSwitchHoverShown(),
				TEXT("1: hovering B shows the switch ghost"));
			FVector From;
			FVector To;
			Check(State, PC->GetCoverSwitchPreviewLine(From, To) && FVector::Dist2D(From, Op->GetActorLocation()) < 5.f,
				FString::Printf(TEXT("1: ... and the dash line (%.0f cm long)"), FVector::Dist2D(From, To)));
			FHitResult HitA;
			const FVector EyeA = State.P + State.F * 100.f + FVector(0.f, 0.f, State.GroundZ - State.P.Z + 150.f);
			World->LineTraceSingleByChannel(HitA, EyeA, EyeA + State.F * 400.f, ECC_Visibility, Params);
			Check(State, HitA.GetActor() == State.Walls[0].Get() && !PC->UpdateCoverSwitchHover(Op, &HitA), TEXT("1: hovering his own wall: no switch ghost"));
			PC->UpdateCoverSwitchHover(nullptr, nullptr);
			if (State.bShot && State.ShotsTaken == 0)
			{
				// Shot mode: the click ghost of B with the dash line first (the HUD draws it), then the double-click (stage 30 -> back here).
				PC->ShowCoverPreview(Op, State.SlotB);
				PlaceCamera(World, State, FVector(-450.f, 250.f, 900.f), FVector(380.f, 420.f, 0.f), true);
				Next(State, 30);
				return true;
			}
			// The double-click on B.
			const FCoverSlot Corner = Op->ResolveCoverSwitchSlot(State.SlotB);
			Confirm(PC, Op, State, State.SlotB, true, true, TEXT("anim_M4_std_jog_fwd_L_to_cvr_std_idle_L"));
			Check(State, PC->WasLastCoverConfirmSwitch() && Op->HasPendingCoverSwitch() && !Op->bInCover,
				TEXT("1: the double-click is the switch: he leaves the cover at once, a switch pending"));
			const float CornerShift = static_cast<float>(FVector::Dist2D(Corner.WorldLocation, State.SlotB.WorldLocation));
			Check(State, CornerShift > 20.f && CornerShift < 70.f && FMath::Abs(AlongR(State, Corner.WorldLocation) - 607.f) < 25.f,
				FString::Printf(TEXT("1: the click 1 m from B's end -> his slot is B's corner stand-off (moved %.0f cm, at R %.0f)"), CornerShift,
					AlongR(State, Corner.WorldLocation)));
			if (State.bShot)
			{
				// A low three-quarter view of B's end from the open side, where he comes in.
				PlaceCamera(World, State, FVector(-120.f, 250.f, 260.f), FVector(400.f, 560.f, 90.f), false);
			}
			Next(State, 3);
			return true;
		}
		case 30: // Shot mode: the preview still, then on with the double-click (stage 2 again, the preview shot taken)
		{
			if (State.StageTime >= 0.4f && State.ShotsTaken == 0)
			{
				RequestShot(State, TEXT("cover_switch_preview"));
			}
			if (State.StageTime >= 0.8f)
			{
				PC->HideCoverPreview();
				Next(State, 2);
				State.StageTime = 5.f; // stage 2's settle wait is over
			}
			return true;
		}
		case 3:
		{
			if (State.bShot && State.ShotsTaken == 1 && !Op->bInCover && FVector::Dist2D(Op->GetActorLocation(), State.SlotB.WorldLocation) < State.StartDistance * 0.55f)
			{
				RequestShot(State, TEXT("cover_switch_dash"));
			}
			if (State.bShot && State.ShotsTaken == 2 && Op->IsCoverSwitchEntry() && State.StageTime > 0.f)
			{
				State.SwitchSeenAt = State.SwitchSeenAt < 0.f ? State.StageTime : State.SwitchSeenAt;
				if (State.StageTime - State.SwitchSeenAt >= 0.15f)
				{
					RequestShot(State, TEXT("cover_switch_arrival"));
				}
			}
			if (!CheckArrival(State, Op, TEXT("1 (A -> B)")))
			{
				return true;
			}
			Check(State, Op->CoverFacing == ECoverFacing::Right && Op->bAtCoverCorner,
				TEXT("1: at B's corner, facing back along the wall towards the gap he came over"));
			if (State.bShot)
			{
				RequestShot(State, TEXT("cover_switch_settled"));
			}
			Next(State, 4);
			return true;
		}
		case 4:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			// 2: back from B's end to A's end.
			Check(State, Op->EvaluateCoverSwitch(State.SlotAEnd) == ECoverSwitchVerdict::Switch, TEXT("2: A is a switch target from B's end"));
			Confirm(PC, Op, State, State.SlotAEnd, true, true, TEXT("anim_M4_std_jog_fwd_R_to_cvr_std_idle_R"));
			Next(State, 5);
			return true;
		}
		case 5:
		{
			if (!CheckArrival(State, Op, TEXT("2 (B -> A)")))
			{
				return true;
			}
			Next(State, 6);
			return true;
		}
		case 6:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			// 3: a slow second click from A's end: the ordinary cover order.
			Check(State, Op->EvaluateCoverSwitch(State.SlotB) == ECoverSwitchVerdict::Switch, TEXT("3: at A's end again"));
			Confirm(PC, Op, State, State.SlotB, false, false, FString());
			Check(State, !PC->WasLastCoverConfirmSwitch() && Op->HasPendingCover() && !Op->HasPendingCoverSwitch(),
				TEXT("3: a single (slow) confirm is the ordinary cover order"));
			Next(State, 7);
			return true;
		}
		case 7:
		{
			if (!CheckArrival(State, Op, TEXT("3 (single click)")))
			{
				return true;
			}
			// Back to the middle of A for 4.
			Op->OrderTakeCover(State.SlotAMiddle, true);
			Next(State, 8);
			return true;
		}
		case 8:
		{
			if ((!Settled(Op) || State.StageTime < 1.5f) && State.StageTime < 15.f)
			{
				return true;
			}
			const ECoverSwitchVerdict Verdict = Op->EvaluateCoverSwitch(State.SlotB);
			Check(State, Op->bInCover && Verdict == ECoverSwitchVerdict::NotAtEdge,
				FString::Printf(TEXT("4: in the middle of A: %s"), CoverSwitchRules::VerdictName(Verdict)));
			Confirm(PC, Op, State, State.SlotB, true, false, FString());
			Check(State, !PC->WasLastCoverConfirmSwitch() && PC->GetLastCoverSwitchVerdict() == ECoverSwitchVerdict::NotAtEdge && Op->HasPendingCover()
				&& !Op->HasPendingCoverSwitch(), TEXT("4: the double-click is the ordinary (running) cover order"));
			Next(State, 9);
			return true;
		}
		case 9:
		{
			if (!CheckArrival(State, Op, TEXT("4 (not at an edge)")))
			{
				return true;
			}
			Op->OrderTakeCover(State.SlotAEnd, true);
			Next(State, 10);
			return true;
		}
		case 10:
		{
			if ((!Settled(Op) || State.StageTime < 1.5f) && State.StageTime < 15.f)
			{
				return true;
			}
			// 5: range tuned down to 3 m - B (5 m) is out of range.
			Op->CoverSwitchMaxRangeCm = 300.f;
			PC->ShowCoverPreview(Op, State.SlotB);
			FVector From;
			FVector To;
			Check(State, !PC->GetCoverSwitchPreviewLine(From, To), TEXT("5: out of range: no dash line on the cover ghost"));
			PC->HideCoverPreview();
			Confirm(PC, Op, State, State.SlotB, true, false, FString());
			Check(State, !PC->WasLastCoverConfirmSwitch() && PC->GetLastCoverSwitchVerdict() == ECoverSwitchVerdict::OutOfRange,
				FString::Printf(TEXT("5: out of range (%s) - the ordinary cover order"), CoverSwitchRules::VerdictName(PC->GetLastCoverSwitchVerdict())));
			Op->CoverSwitchMaxRangeCm = 800.f;
			Next(State, 11);
			return true;
		}
		case 11:
		{
			if (!CheckArrival(State, Op, TEXT("5 (out of range)")))
			{
				return true;
			}
			Op->OrderTakeCover(State.SlotAOtherEnd, true);
			Next(State, 12);
			return true;
		}
		case 12:
		{
			if ((!Settled(Op) || State.StageTime < 1.5f) && State.StageTime < 15.f)
			{
				return true;
			}
			// 6: from A's other end to the low wall C.
			Check(State, Op->EvaluateCoverSwitch(State.SlotC) == ECoverSwitchVerdict::Switch,
				FString::Printf(TEXT("6: the low wall C is a switch target from A's other end (%s)"), CoverSwitchRules::VerdictName(Op->EvaluateCoverSwitch(State.SlotC))));
			Confirm(PC, Op, State, State.SlotC, true, true, TEXT("anim_M4_std_jog_fwd_R_to_cvr_crch_idle_R"));
			if (State.bShot)
			{
				PlaceCamera(World, State, FVector(-120.f, -250.f, 240.f), FVector(400.f, -560.f, 60.f), false);
			}
			Next(State, 13);
			return true;
		}
		case 13:
		{
			if (State.bShot && State.ShotsTaken == 4 && Op->IsCoverSwitchEntry() && State.StageTime > 0.f)
			{
				State.SwitchSeenAt = State.SwitchSeenAt < 0.f ? State.StageTime : State.SwitchSeenAt;
				if (State.StageTime - State.SwitchSeenAt >= 0.25f)
				{
					RequestShot(State, TEXT("cover_switch_low"));
				}
			}
			if (!CheckArrival(State, Op, TEXT("6 (low cover)")))
			{
				return true;
			}
			Check(State, Op->GetStance() == EOperativeStance::Crouching && Op->CurrentCoverHeight == ECoverHeight::LowCover,
				TEXT("6: crouched behind the low wall"));
			// 7: group mode - the leader switches, the others join.
			Squad->ExitSoloMode(false, 0.f);
			ResetSquad(Squad, State, FVector2D(0.f, 100.f));
			Op->OrderTakeCover(State.SlotAEnd, false);
			Next(State, 14);
			return true;
		}
		case 14:
		{
			if ((!Settled(Op) || State.StageTime < 3.f) && State.StageTime < 15.f)
			{
				return true;
			}
			const TArray<AOperativeCharacter*> Joining = Squad->GetGroupCoverMembers(Op);
			Check(State, Op->bInCover && Joining.Num() == 2, FString::Printf(TEXT("7: group mode, the leader at A's end, %d go along"), Joining.Num()));
			Confirm(PC, Op, State, State.SlotB, true, true, TEXT("anim_M4_std_jog_fwd_L_to_cvr_std_idle_L"));
			Check(State, PC->WasLastCoverConfirmSwitch() && PC->GetLastGroupCoverSlotCount() + PC->GetLastGroupCoverProneCount() == 3,
				FString::Printf(TEXT("7: the group order with the leader's switch (%d at the wall, %d prone)"), PC->GetLastGroupCoverSlotCount(),
					PC->GetLastGroupCoverProneCount()));
			Next(State, 15);
			return true;
		}
		case 15:
		{
			// The others run past him to their slots along the same wall: a shoulder may nudge his capsule a little (as with the ordinary
			// group order).
			if (!CheckArrival(State, Op, TEXT("7 (group, the leader)"), 20.f, 60.f))
			{
				return true;
			}
			Next(State, 16);
			return true;
		}
		case 16:
		{
			AOperativeCharacter* B = Member(State, 1);
			AOperativeCharacter* C = Member(State, 2);
			const bool bJoined = B && C && (B->bInCover || Squad->HasGroupCoverProne(B, true)) && (C->bInCover || Squad->HasGroupCoverProne(C, true));
			if (!bJoined && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, bJoined, TEXT("7: the two others joined at B (in cover or prone behind it)"));
			Check(State, B && C && B->GetCoverSwitchCount() == 0 && C->GetCoverSwitchCount() == 0, TEXT("7: only the leader dashed in through the run-in clip"));
			// 10 (before the fight: real-time fight clicks only select): the hovered ghost is the committed target.
			Squad->EnterSoloMode();
			ResetSquad(Squad, State, FVector2D(0.f, 100.f));
			Op->OrderTakeCover(State.SlotAEnd, false);
			State.ClickCase = 0;
			Next(State, 27);
			return true;
		}
		case 27:
		{
			if ((!Settled(Op) || State.StageTime < 1.5f) && State.StageTime < 15.f)
			{
				return true;
			}
			const TCHAR* CaseName = State.ClickCase == 0 ? TEXT("B's end face, double-click") : State.ClickCase == 1 ? TEXT("B's front face near the edge, double-click")
				: TEXT("B's front face near the edge, slow second click");
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverSwitchSmokeClick), false);
			for (const TWeakObjectPtr<AOperativeCharacter>& Each : State.Members)
			{
				Params.AddIgnoredActor(Each.Get());
			}
			const FVector Eye = State.ClickCase == 0 ? State.P + State.F * (NearFaceBC + 20.f) + State.R * 400.f : State.P + State.F * 150.f + State.R * 575.f;
			const FVector Along = State.ClickCase == 0 ? State.R : State.F;
			const FVector Start = Eye + FVector(0.f, 0.f, State.GroundZ - State.P.Z + 120.f);
			World->LineTraceSingleByChannel(State.ClickHit, Start, Start + Along * 500.f, ECC_Visibility, Params);
			// The hover: its ghost.
			const bool bHover = State.ClickHit.GetActor() == State.Walls[1].Get() && PC->UpdateCoverSwitchHover(Op, &State.ClickHit) && PC->IsCoverSwitchHoverShown();
			const ACoverGhostActor* HoverGhost = PC->GetCoverSwitchGhost();
			State.HoverAt = HoverGhost ? HoverGhost->GetSlot().WorldLocation : FVector::ZeroVector;
			State.HoverFacing = HoverGhost ? HoverGhost->GetShownFacing() : ECoverFacing::Left;
			Check(State, bHover && FMath::Abs(AlongR(State, State.HoverAt) - 607.f) < 25.f && State.HoverFacing == ECoverFacing::Right,
				FString::Printf(TEXT("10 (%s): the hover ghost at B's near corner on its front face (R %.0f), mirrored (facing right)"), CaseName, AlongR(State, State.HoverAt)));
			// The first click: a preview only - no order, and its ghost where the hover ghost stood.
			PC->HandleWorldHit(State.ClickHit);
			const ACoverGhostActor* ClickGhost = PC->GetCoverPreviewGhost();
			Check(State, PC->HasCoverPreview() && Op->bInCover && !Op->HasPendingCover() && !Op->IsMoving(),
				FString::Printf(TEXT("10 (%s): the first click starts no order (in cover %d, pending %d)"), CaseName, Op->bInCover ? 1 : 0, Op->HasPendingCover() ? 1 : 0));
			Check(State, ClickGhost && ClickGhost->IsShown() && FVector::Dist2D(ClickGhost->GetSlot().WorldLocation, State.HoverAt) < 5.f
				&& ClickGhost->GetShownFacing() == State.HoverFacing,
				FString::Printf(TEXT("10 (%s): ... its ghost stays where the hover ghost stood (%.0f cm off), same pose"), CaseName,
					ClickGhost ? FVector::Dist2D(ClickGhost->GetSlot().WorldLocation, State.HoverAt) : -1.f));
			if (State.ClickCase == 2)
			{
				Next(State, 28); // the slow second click after the double-click time
				return true;
			}
			State.Target = State.SlotB;
			State.bExpectSwitch = true;
			State.ExpectedClip = TEXT("anim_M4_std_jog_fwd_L_to_cvr_std_idle_L");
			State.bSawSprint = State.bSawOutOfCover = State.bSawSwitchEntry = false;
			State.SwitchCountBefore = Op->GetCoverSwitchCount();
			State.StartDistance = static_cast<float>(FVector::Dist2D(Op->GetActorLocation(), State.HoverAt));
			PC->HandleWorldHit(State.ClickHit); // the second click at once: a double-click
			Check(State, PC->WasLastCoverConfirmSwitch() && Op->HasPendingCoverSwitch() && FVector::Dist2D(Op->GetPendingCoverSlot().WorldLocation, State.HoverAt) < 5.f,
				FString::Printf(TEXT("10 (%s): the double-click commits the hovered slot (%.0f cm off it), as the switch"), CaseName,
					FVector::Dist2D(Op->GetPendingCoverSlot().WorldLocation, State.HoverAt)));
			Next(State, 29);
			return true;
		}
		case 28:
		{
			if (State.StageTime < 0.6f)
			{
				return true;
			}
			State.Target = State.SlotB;
			State.bExpectSwitch = false;
			State.bSawSprint = State.bSawOutOfCover = State.bSawSwitchEntry = false;
			State.SwitchCountBefore = Op->GetCoverSwitchCount();
			State.StartDistance = static_cast<float>(FVector::Dist2D(Op->GetActorLocation(), State.HoverAt));
			PC->HandleWorldHit(State.ClickHit);
			Check(State, !PC->WasLastCoverConfirmSwitch() && Op->HasPendingCover() && FVector::Dist2D(Op->GetPendingCoverSlot().WorldLocation, State.HoverAt) < 5.f,
				FString::Printf(TEXT("10 (slow second click): the ordinary cover order to the hovered slot (%.0f cm off it), no switch"),
					FVector::Dist2D(Op->GetPendingCoverSlot().WorldLocation, State.HoverAt)));
			Next(State, 29);
			return true;
		}
		case 29:
		{
			if (!CheckArrival(State, Op, TEXT("10 (hovered = committed)")))
			{
				return true;
			}
			const FCoverSlot& Arrived = Op->GetCoverSlot();
			Check(State, FVector::Dist2D(Arrived.WorldLocation, State.HoverAt) < 10.f && FVector::DotProduct(Arrived.WallNormal, -State.F) > 0.9f
				&& Op->CoverFacing == State.HoverFacing,
				FString::Printf(TEXT("10 case %d: in cover exactly where the ghost stood (%.0f cm off, front face %d), facing as it did"), State.ClickCase,
					FVector::Dist2D(Arrived.WorldLocation, State.HoverAt), FVector::DotProduct(Arrived.WallNormal, -State.F) > 0.9f ? 1 : 0));
			ResetSquad(Squad, State, FVector2D(0.f, 100.f));
			Op->OrderTakeCover(State.SlotAEnd, false);
			++State.ClickCase;
			Next(State, State.ClickCase <= 2 ? 27 : 17); // then 8: the pause in a fight
			return true;
		}
		case 17:
		{
			if ((!Settled(Op) || State.StageTime < 1.5f) && State.StageTime < 15.f)
			{
				return true;
			}
			SmokeUtils::UsePreparedWaveStart();
			Flow->TriggerCombatZone();
			Flow->FinishCutscene();
			Flow->FinishPreparation();
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
				It->SetActorTickEnabled(false);
			}
			Squad->SetLeader(Op);
			Check(State, Flow->ToggleTacticalPause() == EGameFlowResult::Ok && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause,
				TEXT("8: tactical pause in the fight"));
			Next(State, 18);
			return true;
		}
		case 18:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Check(State, Op->bInCover && Op->EvaluateCoverSwitch(State.SlotB) == ECoverSwitchVerdict::Switch, TEXT("8: still at A's end in cover"));
			Confirm(PC, Op, State, State.SlotB, true, true, TEXT("anim_M4_std_jog_fwd_L_to_cvr_std_idle_L"));
			Check(State, PC->WasLastCoverConfirmSwitch() && Op->HasPendingCoverSwitch() && Squad->GetPlannedOrderCount() == 1 && !Op->IsMoving(),
				FString::Printf(TEXT("8: only planned (pending switch %d, %d planned move, moving %d)"), Op->HasPendingCoverSwitch(), Squad->GetPlannedOrderCount(),
					Op->IsMoving()));
			Next(State, 19);
			return true;
		}
		case 19:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Check(State, Op->bInCover && !Op->IsMoving() && Op->HasPendingCoverSwitch(), TEXT("8: nobody moves during the pause"));
			Flow->ToggleTacticalPause();
			Next(State, 20);
			return true;
		}
		case 20:
		{
			if (!CheckArrival(State, Op, TEXT("8 (planned in the pause, released)")))
			{
				return true;
			}
			// 9: corner to corner - back to A's end first.
			ResetSquad(Squad, State, FVector2D(0.f, 100.f));
			Op->OrderTakeCover(State.SlotAEnd, false);
			Next(State, 22);
			return true;
		}
		case 22:
		{
			if ((!Settled(Op) || State.StageTime < 1.5f) && State.StageTime < 15.f)
			{
				return true;
			}
			State.FacingBefore = Op->CoverFacing;
			Check(State, Op->bInCover && Op->CoverFacing == ECoverFacing::Left, TEXT("9: at A's end, facing left (towards B)"));
			// The hover over B's FAR end: the ghost at B's NEAR corner, in the mirrored pose; the dash path.
			FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverSwitchSmokeCorner), false);
			for (const TWeakObjectPtr<AOperativeCharacter>& Each : State.Members)
			{
				Params.AddIgnoredActor(Each.Get());
			}
			FHitResult FarEnd;
			const FVector Eye = State.P + State.F * 150.f + State.R * 820.f + FVector(0.f, 0.f, State.GroundZ - State.P.Z + 150.f);
			World->LineTraceSingleByChannel(FarEnd, Eye, Eye + State.F * 500.f, ECC_Visibility, Params);
			const bool bHover = FarEnd.GetActor() == State.Walls[1].Get() && PC->UpdateCoverSwitchHover(Op, &FarEnd) && PC->IsCoverSwitchHoverShown();
			const ACoverGhostActor* Ghost = PC->GetCoverSwitchGhost();
			const FVector GhostAt = Ghost ? Ghost->GetSlot().WorldLocation : FVector::ZeroVector;
			Check(State, bHover && Ghost && FMath::Abs(AlongR(State, GhostAt) - 607.f) < 25.f && FMath::Abs(AlongF(State, GhostAt) - (NearFaceBC - 45.f)) < 25.f,
				FString::Printf(TEXT("9: hovering B's far end (R 820) - the ghost at B's near corner on its front face (R %.0f, F %.0f)"), AlongR(State, GhostAt),
					AlongF(State, GhostAt)));
			Check(State, Ghost && Ghost->GetShownFacing() == CoverSwitchRules::MirrorFacing(State.FacingBefore),
				FString::Printf(TEXT("9: ... in the mirrored pose (facing %s, he faces %s)"), Ghost && Ghost->GetShownFacing() == ECoverFacing::Left ? TEXT("left") : TEXT("right"),
					State.FacingBefore == ECoverFacing::Left ? TEXT("left") : TEXT("right")));
			TArray<FVector> PathPoints;
			const bool bPath = PC->GetCoverPreviewPath(PathPoints);
			Check(State, bPath && PathPoints.Num() >= 2 && FVector::Dist2D(PathPoints.Last(), GhostAt) < 30.f,
				FString::Printf(TEXT("9: ... and the dash path to it (%d points)"), PathPoints.Num()));
			if (State.bShot)
			{
				PC->bFreezeCoverHoverForTesting = true; // the hover stays up for the still
				PlaceCamera(World, State, FVector(-250.f, 330.f, 800.f), FVector(380.f, 450.f, 0.f), true);
				Next(State, 23);
				return true;
			}
			Next(State, 24);
			return true;
		}
		case 23: // Shot mode: the hover still (ghost + dash path drawn by the HUD)
		{
			if (State.StageTime >= 0.4f && State.StageTime < 0.45f)
			{
				RequestShot(State, TEXT("cover_corner_switch_preview"));
			}
			if (State.StageTime >= 0.8f)
			{
				Next(State, 24);
			}
			return true;
		}
		case 24:
		{
			PC->bFreezeCoverHoverForTesting = false;
			PC->UpdateCoverSwitchHover(nullptr, nullptr);
			// The double-click on B's SIDE face (the 40 cm face towards him, at R 550).
			FCoverSlot Side;
			const FVector SidePoint = State.P + State.F * (NearFaceBC + 20.f) + State.R * 550.f;
			const bool bSide = CoverTraceRules::FindCoverSlotAt(World, FVector(SidePoint.X, SidePoint.Y, State.GroundZ + 90.f), State.R, Side)
				&& Side.WallActor.Get() == State.Walls[1].Get() && FVector::DotProduct(Side.WallNormal, -State.R) > 0.9f;
			Check(State, bSide, TEXT("9: a slot on B's side face (facing A)"));
			if (!bSide)
			{
				Next(State, 26);
				return true;
			}
			State.DashStart = Op->GetActorLocation();
			Confirm(PC, Op, State, Side, true, true, TEXT("anim_M4_std_jog_fwd_L_to_cvr_std_idle_L"));
			Check(State, PC->WasLastCoverConfirmSwitch(), TEXT("9: the double-click on the side face is the switch"));
			if (State.bShot)
			{
				PlaceCamera(World, State, FVector(-120.f, 250.f, 260.f), FVector(400.f, 560.f, 90.f), false);
			}
			Next(State, 25);
			return true;
		}
		case 25:
		{
			if (State.bShot && !Op->bInCover && FVector::Dist2D(Op->GetActorLocation(), State.DashStart) > 150.f && State.SwitchSeenAt > -1.5f)
			{
				RequestShot(State, TEXT("cover_corner_switch_dash"));
				State.SwitchSeenAt = -2.f;
			}
			if (!CheckArrival(State, Op, TEXT("9 (corner to corner)")))
			{
				return true;
			}
			const FCoverSlot& Arrived = Op->GetCoverSlot();
			Check(State, FVector::DotProduct(Arrived.WallNormal, -State.F) > 0.9f && FMath::Abs(AlongR(State, Arrived.WorldLocation) - 607.f) < 25.f,
				FString::Printf(TEXT("9: on B's FRONT face at its near corner (R %.0f), not on the clicked side face"), AlongR(State, Arrived.WorldLocation)));
			Check(State, Op->CoverFacing == CoverSwitchRules::MirrorFacing(State.FacingBefore) && Op->bAtCoverCorner,
				FString::Printf(TEXT("9: arrived in the mirrored pose - facing %s (was %s at A), at the corner"), Op->CoverFacing == ECoverFacing::Left ? TEXT("left") : TEXT("right"),
					State.FacingBefore == ECoverFacing::Left ? TEXT("left") : TEXT("right")));
			const FVector Dash = Arrived.WorldLocation - State.DashStart;
			Check(State, FMath::Abs(FVector::DotProduct(Dash, State.F)) > 100.f && FMath::Abs(FVector::DotProduct(Dash, State.R)) > 200.f,
				FString::Printf(TEXT("9: a diagonal dash (%.0f cm forward, %.0f cm across)"), FVector::DotProduct(Dash, State.F), FVector::DotProduct(Dash, State.R)));
			if (State.bShot)
			{
				RequestShot(State, TEXT("cover_corner_switch_settled"));
			}
			Next(State, 26);
			return true;
		}
		case 26:
		{
			if (State.StageTime < 0.3f)
			{
				return true;
			}
			Op->LeaveCover(TEXT("smoke end"));
			Op->OrderMoveTo(State.P, false);
			Next(State, 21);
			return true;
		}
		case 21:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			Check(State, Op->GetMeshBaseOffsetCm() <= 1.f && Op->GetCoverEntryMeshOffset().Size() <= 1.f,
				FString::Printf(TEXT("out of cover the mesh is back at its default place (offset %.1f cm)"), Op->GetMeshBaseOffsetCm()));
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
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		State->bShot = Args.Contains(TEXT("Shot"));
		if (IConsoleVariable* Probe = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Debug.CoverSwitch")))
		{
			Probe->Set(1, ECVF_SetByCode); // every run-in probe in the log (distance, speed, heading, fit)
		}
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CoverSwitchSmoke"),
		TEXT("Dev check of the wall-edge cover switch (2026-10-20): double-click another cover from a wall edge -> dash + the pack's run-in clip; not at an edge / out of range / single click -> the ordinary order; low cover, group, pause. Arg 'Shot': rendered stills."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
