// Dev-only headless check of the wall-following movement ROUND a cover object (game designer 2026-10-20: "the character moves along the wall
// of any building / object without leaving it"; Tactics/CoverWrapRules, AOperativeCharacter::OrderCoverWrap):
//   Scripts/smoke_parallel.ps1 -Commands CoverWrapSmoke
//   UnrealEditor.exe CodexTactics.uproject /Game/Maps/L_MovementTest -game -RenderOffscreen -ResX=1280 -ResY=720 -nosound
//     -ExecCmds="CodexTactics.CoverWrapSmoke Shot"   -> Saved/Screenshots/cover_wrap_preview.png (ghost + perimeter path), _corner.png
//     (turning the corner), _settled.png, _low.png; add -UseFixedTimeStep -FPS=60 for steady frames
// A runtime arena on L_MovementTest (SmokeUtils::FindClearArena, nothing saved), exploring, Solo: box H (3 x 3 m, 3 m high) 2.5 m ahead,
// the low box L (2 x 2 m, 1 m high) to its left. Every order goes through the controller's click dispatcher (HandleWorldHit):
//  1 in cover on H's front face near its right end: the hover over the ground 60 cm off H's right face shows the ghost there + the
//    perimeter path; the ground click -> he goes round the corner hugging H (in cover all the way, 35..70 cm off its outline at every
//    step), turns, ends in cover on the right face at the goal, the mesh back on the capsule;
//  2 a click on H's LEFT face (the far side, two corners away): the shorter way round (via the front face), in cover all the way;
//  3 the low box L crouched: round its corner to its side face, crouched all the way;
//  4 an open-ground click 2 m away: leaves the cover and walks, as before;
//  5 group mode: the leader goes round, the two others take the destination face by the group cover rules;
//  6 a wave fight in the tactical pause: the wrap is only planned (nobody moves); the release runs it.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
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
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Tactics/CoverFacingRules.h"
#include "Tactics/CoverGhostActor.h"
#include "Tactics/CoverTraceRules.h"
#include "Tactics/CoverWrapRules.h"
#include "UnrealClient.h"

namespace CoverWrapSmoke
{
	constexpr float StepSeconds = 0.05f;

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
		/** 0 = the high box H, 1 = the low box L. */
		TArray<TWeakObjectPtr<AStaticMeshActor>> Boxes;
		/** The current wrap: its object's outline and what was seen on the way. */
		FCoverFootprint Outline;
		float MinOff = 0.f;
		float MaxOff = 0.f;
		int32 OutOfCoverSteps = 0;
		int32 StoodSteps = 0;
		int32 WrapsBefore = 0;
		int32 CornersBefore = 0;
		float YawStart = 0.f;
		float MaxYawTurn = 0.f;
		bool bSawShimmy = false;
		FCoverWrapPlan Plan;
		int32 ShotsTaken = 0;
		float CornerSeenAt = -1.f;
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
		for (const TWeakObjectPtr<AStaticMeshActor>& Box : State.Boxes)
		{
			if (Box.IsValid())
			{
				Box->Destroy();
			}
		}
		FPlatformMisc::RequestExit(false, TEXT("CoverWrapSmoke"));
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

	FVector Local(const FState& State, float AlongF, float AlongR, float Up = 0.f)
	{
		return State.P + State.F * AlongF + State.R * AlongR + FVector(0.f, 0.f, State.GroundZ - State.P.Z + Up);
	}

	float AlongR(const FState& State, const FVector& Point)
	{
		return static_cast<float>(FVector::DotProduct(Point - State.P, State.R));
	}

	float AlongF(const FState& State, const FVector& Point)
	{
		return static_cast<float>(FVector::DotProduct(Point - State.P, State.F));
	}

	/** A box centred (CentreF, CentreR) in the arena frame, SizeF x SizeR, HeightCm high. */
	void SpawnBox(UWorld* World, FState& State, float CentreF, float CentreR, float SizeF, float SizeR, float HeightCm)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		const FVector Centre = Local(State, CentreF, CentreR, HeightCm * 0.5f);
		const FTransform Transform(State.F.Rotation(), Centre, FVector(SizeF / 100.f, SizeR / 100.f, HeightCm / 100.f));
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
		State.Boxes.Add(Block);
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
			const FVector2D At = Index == 0 ? Start : FVector2D(-300.f, Index == 1 ? -150.f : 150.f);
			Each->TeleportTo(Local(State, At.X, At.Y, Each->GetSimpleCollisionHalfHeight() + 5.f), State.F.Rotation(), false, true);
		}
		Squad->SetLeader(Member(State, 0));
	}

	bool Settled(const AOperativeCharacter* Op)
	{
		return Op && Op->bInCover && !Op->bShimmying && !Op->IsCoverWrapping() && !Op->IsMoving() && Op->GetVelocity().Size2D() < 5.f
			&& Op->GetCoverEntryMeshOffset().Size() < 1.f;
	}

	/** A ground hit at (AlongF, AlongR) of the arena (a trace straight down, the squad and the ghosts not in the way). */
	FHitResult GroundHit(UWorld* World, const FState& State, float AtF, float AtR)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverWrapSmokeGround), false);
		for (const TWeakObjectPtr<AOperativeCharacter>& Each : State.Members)
		{
			Params.AddIgnoredActor(Each.Get());
		}
		for (TActorIterator<ACoverGhostActor> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		FHitResult Hit;
		const FVector Top = Local(State, AtF, AtR, 300.f);
		World->LineTraceSingleByChannel(Hit, Top, Top - FVector(0.f, 0.f, 600.f), ECC_Visibility, Params);
		return Hit;
	}

	/** A hit on a box face: a horizontal trace from (AtF, AtR) along Direction at chest height. */
	FHitResult FaceHit(UWorld* World, const FState& State, float AtF, float AtR, const FVector& Direction)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(CoverWrapSmokeFace), false);
		for (const TWeakObjectPtr<AOperativeCharacter>& Each : State.Members)
		{
			Params.AddIgnoredActor(Each.Get());
		}
		FHitResult Hit;
		const FVector From = Local(State, AtF, AtR, 120.f);
		World->LineTraceSingleByChannel(Hit, From, From + Direction * 400.f, ECC_Visibility, Params);
		return Hit;
	}

	void PlaceCamera(UWorld* World, FState& State, const FVector& EyeLocal, const FVector& LookLocal, bool bHud)
	{
		const FVector Eye = Local(State, EyeLocal.X, EyeLocal.Y, EyeLocal.Z);
		const FRotator Look = (Local(State, LookLocal.X, LookLocal.Y, LookLocal.Z) - Eye).Rotation();
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
		UE_LOG(LogCodexTactics, Display, TEXT("CoverWrap: screenshot %s"), *Path);
	}

	/** Takes the outline of Box for the tracking and resets what is seen on the way (before the order: a corner may be turned at once). */
	void BeginWrap(FState& State, const AOperativeCharacter* Op, int32 Box)
	{
		const AStaticMeshActor* Actor = State.Boxes.IsValidIndex(Box) ? State.Boxes[Box].Get() : nullptr;
		CoverWrapWorld::FootprintOf(Actor ? Actor->GetStaticMeshComponent() : nullptr, State.Outline);
		State.MinOff = TNumericLimits<float>::Max();
		State.MaxOff = -TNumericLimits<float>::Max();
		State.OutOfCoverSteps = 0;
		State.StoodSteps = 0;
		State.WrapsBefore = Op->GetCoverWrapCount();
		State.CornersBefore = Op->GetCoverWrapCornersTurned();
		State.YawStart = static_cast<float>(Op->GetActorRotation().Yaw);
		State.MaxYawTurn = 0.f;
		State.bSawShimmy = false;
		State.Plan = Op->GetCoverWrapPlan();
		State.CornerSeenAt = -1.f;
	}

	/** Every step of a wrap: in cover, the distance from the object's outline, the body's turn, the stance. */
	void TrackWrap(FState& State, const AOperativeCharacter* Op)
	{
		if (!Op->IsCoverWrapping() && !Op->bShimmying)
		{
			return;
		}
		State.OutOfCoverSteps += Op->bInCover ? 0 : 1;
		State.StoodSteps += Op->GetStance() == EOperativeStance::Standing ? 1 : 0;
		State.bSawShimmy |= Op->bShimmying;
		const float Off = CoverWrapRules::NearestOutlinePoint(State.Outline, FVector2D(Op->GetActorLocation())).SignedDistanceCm;
		if (Off > 65.f || Off < 38.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("CoverWrap: t=%.2f %.0f cm off the outline at (F %.0f, R %.0f), speed %.0f, wrapping %d"), State.StageTime, Off,
				AlongF(State, Op->GetActorLocation()), AlongR(State, Op->GetActorLocation()), Op->GetVelocity().Size2D(), Op->IsCoverWrapping() ? 1 : 0);
		}
		State.MinOff = FMath::Min(State.MinOff, Off);
		State.MaxOff = FMath::Max(State.MaxOff, Off);
		State.MaxYawTurn = FMath::Max(State.MaxYawTurn, FMath::Abs(FMath::FindDeltaAngleDegrees(State.YawStart, static_cast<float>(Op->GetActorRotation().Yaw))));
	}

	/** After a wrap order: wait for the arrival, then check the way and the end. True when done (or timed out). */
	bool CheckWrap(FState& State, AOperativeCharacter* Op, const TCHAR* Label, const FVector& FaceNormal, bool bExpectCrouch, float CapSeconds = 25.f)
	{
		TrackWrap(State, Op);
		const bool bDone = Settled(Op) && State.StageTime > 0.5f && Op->GetCoverWrapCount() > State.WrapsBefore;
		if (!bDone && State.StageTime < CapSeconds)
		{
			return false;
		}
		const FCoverSlot& Slot = Op->GetCoverSlot();
		Check(State, bDone, FString::Printf(TEXT("%s: round the object in %.1f s (%.0f cm, %d corner(s) planned, %d turned)"), Label, State.StageTime, State.Plan.Path.LengthCm,
			State.Plan.Path.CornerCount(), Op->GetCoverWrapCornersTurned() - State.CornersBefore));
		Check(State, State.OutOfCoverSteps == 0 && State.bSawShimmy, FString::Printf(TEXT("%s: in cover all the way, side-stepping (%d steps out of cover)"), Label,
			State.OutOfCoverSteps));
		Check(State, State.MinOff >= 35.f && State.MaxOff <= 70.f,
			FString::Printf(TEXT("%s: hugging the object - %.0f .. %.0f cm off its outline at every step (never across open ground)"), Label, State.MinOff, State.MaxOff));
		Check(State, State.MaxYawTurn >= 60.f, FString::Printf(TEXT("%s: the body turned the corner (%.0f deg)"), Label, State.MaxYawTurn));
		Check(State, Op->bInCover && FVector::DotProduct(Slot.WallNormal, FaceNormal) > 0.9f,
			FString::Printf(TEXT("%s: in cover on the target face at the end"), Label));
		const float OffGoal = static_cast<float>(FVector::Dist2D(Op->GetActorLocation(), State.Plan.GoalSlot.WorldLocation));
		const float FacingErr = FMath::Abs(FMath::FindDeltaAngleDegrees(static_cast<float>(Op->GetActorRotation().Yaw), CoverFacingRules::FacingYaw(Slot, Op->CoverFacing)));
		Check(State, OffGoal < 30.f && FacingErr < 20.f && Op->CoverFacing == State.Plan.GoalFacing,
			FString::Printf(TEXT("%s: at the goal (%.0f cm), back to the wall (%.0f deg), facing %s as planned"), Label, OffGoal, FacingErr,
				Op->CoverFacing == ECoverFacing::Left ? TEXT("left") : TEXT("right")));
		Check(State, Op->GetCoverEntryMeshOffset().Size() < 1.f, FString::Printf(TEXT("%s: the mesh on the capsule (entry offset %.1f cm, base offset %.1f cm = the wall push)"),
			Label, Op->GetCoverEntryMeshOffset().Size(), Op->GetMeshBaseOffsetCm()));
		Check(State, bExpectCrouch ? State.StoodSteps == 0 && Op->GetStance() == EOperativeStance::Crouching : Op->GetStance() == EOperativeStance::Standing,
			FString::Printf(TEXT("%s: %s all the way (%d standing steps)"), Label, bExpectCrouch ? TEXT("crouched") : TEXT("standing"), State.StoodSteps));
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
		auto FindSlot = [World, &State](float AtF, float AtR, float Height, const FVector& Direction, FCoverSlot& Out)
		{
			return CoverTraceRules::FindCoverSlotAt(World, Local(State, AtF, AtR, Height), Direction, Out);
		};
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
			SpawnBox(World, State, 400.f, 0.f, 300.f, 300.f, 300.f);   // H: faces at F 250 / 550, R -150 / 150
			SpawnBox(World, State, 400.f, -650.f, 200.f, 200.f, 100.f); // L: faces at F 300 / 500, R -750 / -550
			Next(State, 1);
			return true;
		}
		case 1:
		{
			if (State.StageTime < 2.5f)
			{
				return true; // the navmesh rebuilds round the boxes
			}
			FCoverSlot Front;
			const bool bFound = FindSlot(250.f, 100.f, 90.f, State.F, Front);
			FCoverFootprint Outline;
			Check(State, bFound && Front.Height == ECoverHeight::HighCover && Front.WallActor.Get() == State.Boxes[0].Get()
				&& CoverWrapWorld::FootprintOf(Front.WallComponent.Get(), Outline) && Outline.Num() == 4,
				TEXT("set-up: a high slot on H's front face, H's outline (4 corners)"));
			if (!bFound)
			{
				return Finish(State, false);
			}
			Op->OrderTakeCover(Front, false);
			Next(State, 2);
			return true;
		}
		case 2:
		{
			if ((!Settled(Op) || State.StageTime < 1.5f) && State.StageTime < 12.f)
			{
				return true;
			}
			Check(State, Op->bInCover && AlongR(State, Op->GetActorLocation()) > 60.f, FString::Printf(TEXT("1: in cover on H's front face near its right end (R %.0f)"),
				AlongR(State, Op->GetActorLocation())));
			// The hover over the ground 60 cm off H's right face: the ghost there + the perimeter path.
			const FHitResult Ground = GroundHit(World, State, 400.f, 210.f);
			const bool bHover = PC->UpdateCoverWrapHover(Op, &Ground) && PC->IsCoverWrapHoverShown();
			TArray<FVector> Path;
			const bool bPath = PC->GetCoverPreviewPath(Path);
			const ACoverGhostActor* Ghost = PC->GetCoverWrapGhost();
			const FVector GhostAt = Ghost ? Ghost->GetSlot().WorldLocation : FVector::ZeroVector;
			Check(State, bHover && Ghost && FMath::Abs(AlongR(State, GhostAt) - 195.f) < 25.f && FMath::Abs(AlongF(State, GhostAt) - 400.f) < 30.f,
				FString::Printf(TEXT("1: hovering the ground by H's right face: the ghost there (F %.0f, R %.0f)"), AlongF(State, GhostAt), AlongR(State, GhostAt)));
			Check(State, bPath && Path.Num() >= 5, FString::Printf(TEXT("1: ... and the perimeter path round the corner (%d points)"), Path.Num()));
			if (State.bShot && State.ShotsTaken == 0)
			{
				PC->bFreezeCoverHoverForTesting = true; // the hover stays up for the still
				PlaceCamera(World, State, FVector(-60.f, 520.f, 620.f), FVector(330.f, 150.f, 0.f), true);
				Next(State, 30);
				return true;
			}
			PC->UpdateCoverWrapHover(nullptr, nullptr);
			BeginWrap(State, Op, 0);
			PC->HandleWorldHit(Ground);
			State.Plan = Op->GetCoverWrapPlan();
			Check(State, Op->IsCoverWrapping() && PC->GetLastCoverWrapVerdict() == ECoverWrapVerdict::Wrap && Op->bInCover,
				FString::Printf(TEXT("1: the ground click is the wrap (%s), still in cover"), CoverWrapRules::VerdictName(PC->GetLastCoverWrapVerdict())));
			if (State.bShot)
			{
				PlaceCamera(World, State, FVector(-200.f, 450.f, 380.f), FVector(300.f, 120.f, 90.f), false);
			}
			Next(State, 3);
			return true;
		}
		case 30: // Shot mode: the preview still, then the click (stage 2 again)
		{
			if (State.StageTime >= 0.4f && State.ShotsTaken == 0)
			{
				RequestShot(State, TEXT("cover_wrap_preview"));
			}
			if (State.StageTime >= 0.8f)
			{
				PC->bFreezeCoverHoverForTesting = false;
				Next(State, 2);
				State.StageTime = 5.f;
			}
			return true;
		}
		case 3:
		{
			if (State.bShot && State.ShotsTaken == 1 && Op->GetCoverWrapCornersTurned() > State.CornersBefore)
			{
				State.CornerSeenAt = State.CornerSeenAt < 0.f ? State.StageTime : State.CornerSeenAt;
				if (State.StageTime - State.CornerSeenAt >= 0.3f)
				{
					RequestShot(State, TEXT("cover_wrap_corner"));
				}
			}
			if (!CheckWrap(State, Op, TEXT("1 (one corner)"), State.R, false))
			{
				return true;
			}
			if (State.bShot)
			{
				RequestShot(State, TEXT("cover_wrap_settled"));
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
			// 2: a click on H's LEFT face near its front end - two corners away; the front is the shorter way.
			const FHitResult Face = FaceHit(World, State, 330.f, -450.f, State.R);
			Check(State, Face.GetActor() == State.Boxes[0].Get(), TEXT("2: the click hits H's left face"));
			BeginWrap(State, Op, 0);
			PC->HandleWorldHit(Face);
			State.Plan = Op->GetCoverWrapPlan();
			const FCoverWrapPlan& Plan = Op->GetCoverWrapPlan();
			float MinF = TNumericLimits<float>::Max();
			for (const FVector& Point : Plan.Waypoints)
			{
				MinF = FMath::Min(MinF, AlongF(State, Point));
			}
			Check(State, Op->IsCoverWrapping() && Plan.Path.CornerCount() == 2 && MinF < 250.f,
				FString::Printf(TEXT("2: round two corners the shorter way, past the front face (%d corners, nearest F %.0f, %.0f cm)"), Plan.Path.CornerCount(), MinF,
					Plan.Path.LengthCm));
			Next(State, 5);
			return true;
		}
		case 5:
		{
			if (!CheckWrap(State, Op, TEXT("2 (two corners)"), -State.R, false))
			{
				return true;
			}
			Check(State, State.Plan.bGoalAtCorner && Op->bAtCoverCorner, TEXT("2: the click near the face's front end -> at that corner"));
			// 3: the low box, crouched.
			ResetSquad(Squad, State, FVector2D(0.f, -500.f));
			FCoverSlot LowFront;
			const bool bLow = FindSlot(300.f, -600.f, 50.f, State.F, LowFront) && LowFront.Height == ECoverHeight::LowCover;
			Check(State, bLow, TEXT("3: a low slot on L's front face"));
			Op->OrderTakeCover(LowFront, false);
			Next(State, 6);
			return true;
		}
		case 6:
		{
			if ((!Settled(Op) || State.StageTime < 1.5f) && State.StageTime < 12.f)
			{
				return true;
			}
			Check(State, Op->bInCover && Op->GetStance() == EOperativeStance::Crouching, TEXT("3: crouched behind L"));
			const FHitResult Ground = GroundHit(World, State, 400.f, -490.f);
			BeginWrap(State, Op, 1);
			PC->HandleWorldHit(Ground);
			State.Plan = Op->GetCoverWrapPlan();
			Check(State, Op->IsCoverWrapping(), FString::Printf(TEXT("3: the ground click by L's right face is the wrap (%s)"), CoverWrapRules::VerdictName(PC->GetLastCoverWrapVerdict())));
			if (State.bShot)
			{
				PlaceCamera(World, State, FVector(-150.f, -250.f, 330.f), FVector(380.f, -560.f, 40.f), false);
			}
			Next(State, 7);
			return true;
		}
		case 7:
		{
			if (State.bShot && Op->GetCoverWrapCornersTurned() > State.CornersBefore)
			{
				State.CornerSeenAt = State.CornerSeenAt < 0.f ? State.StageTime : State.CornerSeenAt;
				if (State.StageTime - State.CornerSeenAt >= 0.3f && State.CornerSeenAt > -0.5f)
				{
					RequestShot(State, TEXT("cover_wrap_low"));
					State.CornerSeenAt = -1000.f;
				}
			}
			if (!CheckWrap(State, Op, TEXT("3 (low box, crouched)"), State.R, true))
			{
				return true;
			}
			Check(State, Op->CurrentCoverHeight == ECoverHeight::LowCover, TEXT("3: in low cover at the end"));
			// 4: an open-ground click 2 m out: leaves the cover and walks, as before.
			PC->HandleWorldHit(GroundHit(World, State, 100.f, -400.f));
			Check(State, !Op->bInCover && !Op->IsCoverWrapping() && Op->IsMoving() && PC->GetLastCoverWrapVerdict() == ECoverWrapVerdict::TooFar,
				FString::Printf(TEXT("4: an open-ground click 2 m out leaves the cover and walks (%s)"), CoverWrapRules::VerdictName(PC->GetLastCoverWrapVerdict())));
			Next(State, 8);
			return true;
		}
		case 8:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			// 5: group mode - the leader in cover at H's front face, the others go along.
			Squad->ExitSoloMode(false, 0.f);
			ResetSquad(Squad, State, FVector2D(0.f, 100.f));
			FCoverSlot Front;
			FindSlot(250.f, 100.f, 90.f, State.F, Front);
			Op->OrderTakeCover(Front, false);
			Next(State, 9);
			return true;
		}
		case 9:
		{
			if ((!Settled(Op) || State.StageTime < 3.f) && State.StageTime < 15.f)
			{
				return true;
			}
			const TArray<AOperativeCharacter*> Joining = Squad->GetGroupCoverMembers(Op);
			Check(State, Op->bInCover && Joining.Num() == 2, FString::Printf(TEXT("5: group mode, the leader in cover at H, %d go along"), Joining.Num()));
			BeginWrap(State, Op, 0);
			PC->HandleWorldHit(GroundHit(World, State, 420.f, 210.f));
			State.Plan = Op->GetCoverWrapPlan();
			Check(State, Op->IsCoverWrapping() && PC->GetLastGroupCoverSlotCount() + PC->GetLastGroupCoverProneCount() == 3,
				FString::Printf(TEXT("5: the leader goes round, the group order for the others (%d at the wall, %d prone)"), PC->GetLastGroupCoverSlotCount(),
					PC->GetLastGroupCoverProneCount()));
			Next(State, 10);
			return true;
		}
		case 10:
		{
			if (!CheckWrap(State, Op, TEXT("5 (group, the leader)"), State.R, false, 25.f))
			{
				return true;
			}
			Next(State, 11);
			return true;
		}
		case 11:
		{
			AOperativeCharacter* B = Member(State, 1);
			AOperativeCharacter* C = Member(State, 2);
			auto AtRightFace = [&State, Squad](const AOperativeCharacter* Other)
			{
				return Other && ((Other->bInCover && FVector::DotProduct(Other->GetCoverSlot().WallNormal, State.R) > 0.9f) || Squad->HasGroupCoverProne(Other, true));
			};
			const bool bJoined = AtRightFace(B) && AtRightFace(C);
			if (!bJoined && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, bJoined, TEXT("5: the two others took H's right face (in cover there or prone behind it)"));
			// 6: the pause in a fight - Solo, only the leader.
			Squad->EnterSoloMode();
			ResetSquad(Squad, State, FVector2D(0.f, 100.f));
			FCoverSlot Front;
			FindSlot(250.f, 100.f, 90.f, State.F, Front);
			Op->OrderTakeCover(Front, false);
			Next(State, 12);
			return true;
		}
		case 12:
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
				TEXT("6: tactical pause in the fight"));
			Next(State, 13);
			return true;
		}
		case 13:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			PC->HandleWorldHit(GroundHit(World, State, 400.f, 210.f));
			Check(State, Op->HasPendingCoverWrap() && !Op->IsCoverWrapping() && Squad->GetPlannedOrderCount() == 1 && Op->bInCover,
				FString::Printf(TEXT("6: only planned (pending wrap %d, %d planned move, wrapping %d)"), Op->HasPendingCoverWrap(), Squad->GetPlannedOrderCount(),
					Op->IsCoverWrapping()));
			Next(State, 14);
			return true;
		}
		case 14:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			Check(State, Op->bInCover && !Op->IsMoving() && Op->HasPendingCoverWrap(), TEXT("6: nobody moves during the pause"));
			BeginWrap(State, Op, 0);
			Flow->ToggleTacticalPause();
			Next(State, 15);
			return true;
		}
		case 15:
		{
			if (State.StageTime < 0.2f)
			{
				return true;
			}
			if (State.StageTime < 0.25f)
			{
				Check(State, Op->IsCoverWrapping(), TEXT("6: the release runs the wrap"));
				State.Plan = Op->GetCoverWrapPlan();
			}
			if (!CheckWrap(State, Op, TEXT("6 (planned in the pause, released)"), State.R, false))
			{
				return true;
			}
			Op->LeaveCover(TEXT("smoke end"));
			Op->OrderMoveTo(State.P, false);
			Next(State, 16);
			return true;
		}
		case 16:
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
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.CoverWrapSmoke"),
		TEXT("Dev check of the wall-following movement round a cover object (2026-10-20): in cover, a click round the same object -> along its wall round the corners, in cover all the way; two corners the shorter way; low box crouched; open ground leaves; group; pause. Arg 'Shot': rendered stills."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
