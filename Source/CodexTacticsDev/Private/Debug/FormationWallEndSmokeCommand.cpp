// Dev-only headless checks of the squad at a wall's END (behind Codex.Formation.Sectors) - the game designer's bug report 2026-10-26 (rules
// v2.3 in main 7f1406f; docs/design/FORMATION_SECTORS.md "v2.4 - the corner stack ..."):
//   CodexTactics.FormationCornerPeekSmoke      bug A: the squad pressed against a long wall (the wall layout), the leader's move along it to its
//                                               free END -> the corner stack: the member nearest the corner takes the edge and looks round it
//                                               (the corner watch's look), the leader second behind him, the last man looks back along the wall;
//                                               held for 3 s.
//   CodexTactics.FormationWallSwitchSmoke      bug B: from the wall layout a group cover order to another wall of a T junction, on both sides of
//                                               the joint -> every member ends on the leader's side of every wall (0 splits).
//   CodexTactics.FormationSquadPathPreviewSmoke bug C: the squad in the wall layout, the cursor on the ground along the wall / round its end ->
//                                               the squad's dashed guide lines (the leader's path, each member's) exist for the HUD.
//   CodexTactics.FormationLeaderSwapSlideSmoke bug D: the wall layout, ONE leader switch (the others re-form round the new leader), then a move
//                                               along the same wall -> the squad slides (it moved, the wall layout again); then a click 4 m off
//                                               the wall -> a normal move.
//   CodexTactics.FormationSquadPeekSmoke       the squad's corner peek (user bug report 2026-10-26: "solo the wall's corner shows the peek
//                                               silhouette and a click peeks; in formation it is gone"): the squad hugging the long wall, the
//                                               cursor on its END (PlayerTick's hover, UpdateCornerPeekHover) -> the peek ghost of the member
//                                               nearest that end; the ground along the wall keeps the squad's guide lines; the click there
//                                               (HandleWorldHit) -> that member at the corner in the manual corner peek (corner aim, leaning
//                                               out), the leader second, nobody beyond the wall; held 3 s; then the same at the other end.
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands FormationCornerPeekSmoke,FormationWallSwitchSmoke,...
//   rendered stills ("Shot" argument; -RenderOffscreen): Saved/Screenshots/Formation/<scenario>_*.png, a camera straight above the squad.
// A runtime arena on L_MovementTest (SmokeUtils::FindClearArena, nothing saved); the walls are spawned cubes (box collision: the world walls the
// v2.1 rules read). Godot reference: none (UE-only).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/VisionConeComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Core/CodexTacticsPlayerController.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationTuning.h"
#include "GameFramework/HUD.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Tactics/CoverGhostActor.h"
#include "Tactics/CoverTraceRules.h"
#include "UnrealClient.h"

namespace FormationWallEndSmoke
{
	constexpr float StepSeconds = 0.05f;
	/** The long wall: near face this far ahead of the arena origin, its half length along R (the free LEFT end at -HalfLength), cm. */
	constexpr float WallNearF = 400.f;
	constexpr float WallThickness = 40.f;
	constexpr float WallHalfLength = 600.f;
	/** The corner stack is held (checked) this long after the squad settled, s. */
	constexpr float HoldSeconds = 3.f;

	enum class EScenario : uint8
	{
		CornerPeek,
		WallSwitch,
		PathPreview,
		LeaderSwap,
		SquadPeek,
	};

	const TCHAR* ScenarioName(EScenario Scenario)
	{
		switch (Scenario)
		{
		case EScenario::CornerPeek: return TEXT("FormationCornerPeekSmoke");
		case EScenario::WallSwitch: return TEXT("FormationWallSwitchSmoke");
		case EScenario::PathPreview: return TEXT("FormationSquadPathPreviewSmoke");
		case EScenario::SquadPeek: return TEXT("FormationSquadPeekSmoke");
		default: return TEXT("FormationLeaderSwapSlideSmoke");
		}
	}

	const TCHAR* ScenarioShotName(EScenario Scenario)
	{
		switch (Scenario)
		{
		case EScenario::CornerPeek: return TEXT("corner_stack");
		case EScenario::WallSwitch: return TEXT("wall_switch");
		case EScenario::PathPreview: return TEXT("path_preview");
		case EScenario::SquadPeek: return TEXT("squad_peek");
		default: return TEXT("leader_swap");
		}
	}

	struct FState
	{
		EScenario Scenario = EScenario::CornerPeek;
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bShot = false;
		bool bSectors = true;
		FVector P = FVector::ZeroVector;
		FVector F = FVector::ForwardVector;
		FVector R = FVector::RightVector;
		float GroundZ = 0.f;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		TArray<TWeakObjectPtr<AStaticMeshActor>> Walls;
		/** The run's counters / samples (per scenario). */
		int32 Variant = 0;
		float HoldTime = 0.f;
		float HoldGood = 0.f;
		FString HoldWhy;
		float LeaderStartR = 0.f;
		int32 SlidesBefore = 0;
		bool bSawSlide = false;
		float WorstSplitTime = 0.f;
		float SplitTime = 0.f;
		TWeakObjectPtr<ACameraActor> Camera;
		float ShotFreezeTime = -1.f;
		bool bShotTaken = false;
		/** The squad peek: the member the hover showed (the one nearest the end), the peeks the squad counted before the click. */
		TWeakObjectPtr<AOperativeCharacter> Peeker;
		int32 PeeksBefore = 0;
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
		FPlatformMisc::RequestExit(false, ScenarioName(State.Scenario));
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

	/** Yaw of an arena direction (along F / along R). */
	float ArenaYaw(const FState& State, float DirF, float DirR)
	{
		return static_cast<float>((State.F * DirF + State.R * DirR).Rotation().Yaw);
	}

	/** A box wall: centre (F, R), size along F / along R, 3 m high. */
	void SpawnWall(UWorld* World, FState& State, float CentreF, float CentreR, float SizeF, float SizeR)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		const FTransform Transform(State.F.Rotation(), Local(State, CentreF, CentreR, 150.f), FVector(SizeF / 100.f, SizeR / 100.f, 3.f));
		AStaticMeshActor* Block = Cube ? World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn) : nullptr;
		if (Block)
		{
			Block->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Block->GetStaticMeshComponent()->SetStaticMesh(Cube);
			Block->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			Block->GetStaticMeshComponent()->SetCanEverAffectNavigation(true);
			Block->FinishSpawning(Transform);
			State.Walls.Add(Block);
		}
	}

	/** Everyone crouched in the wall layout's pose on his slot, turned to his look (FormationSlideSmoke's test). */
	bool InWallLayout(const USquadSubsystem* Squad, const FState& State, FString* OutWhy = nullptr)
	{
		bool bAll = Squad->HasFormationWallOrder() && Squad->GetFormationKind() == EFormationKind::Wall && !Squad->IsFormationSliding();
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			const AOperativeCharacter* Each = Weak.Get();
			FVector Slot;
			float Look = 0.f;
			const bool bTarget = Each && Squad->GetFormationTarget(Each, Slot, Look);
			const float Off = bTarget ? static_cast<float>(FVector::Dist2D(Slot, Each->GetActorLocation())) : -1.f;
			const bool bOk = bTarget && Each->IsInFormationWallPose() && Off <= 60.f && Each->GetVelocity().Size2D() < 5.f;
			bAll &= bOk;
			if (OutWhy && Each)
			{
				*OutWhy += FString::Printf(TEXT(" [%s at F %.0f R %.0f, %.0f cm off, pose %d, look %.0f]"), *Each->DisplayName.ToString(), AlongF(State, Each->GetActorLocation()),
					AlongR(State, Each->GetActorLocation()), Off, Each->IsInFormationWallPose() ? 1 : 0, Each->GetSightFacing().Rotation().Yaw);
			}
		}
		return bAll;
	}

	FString DescribeSquad(const USquadSubsystem* Squad, const FState& State)
	{
		FString Line;
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			if (const AOperativeCharacter* Each = Weak.Get())
			{
				EFormationRole Role = EFormationRole::Commander;
				const bool bRole = Squad->GetFormationRole(Each, Role);
				Line += FString::Printf(TEXT(" [%s%s %s: F %.0f R %.0f, look %.0f, v %.0f, moving %d, own %d, stance %d%s, pose %d, hold %.2f, serial %u]"),
					*Each->DisplayName.ToString(), Each == Squad->GetLeader() ? TEXT(" (leader)") : TEXT(""), bRole ? FormationNames::ToString(Role) : TEXT("-"),
					AlongF(State, Each->GetActorLocation()), AlongR(State, Each->GetActorLocation()), Each->GetSightFacing().Rotation().Yaw, Each->GetVelocity().Size2D(),
					Each->IsMoving() ? 1 : 0, Each->HasOwnMoveOrder() ? 1 : 0, static_cast<int32>(Each->GetStance()), Each->IsStanceFromSquad() ? TEXT(" squad") : TEXT(""),
					Each->IsInFormationWallPose() ? 1 : 0, Each->GetMoveHoldSeconds(), Each->GetMoveOrderSerial());
			}
		}
		return Line + FString::Printf(TEXT(" formation %s%s%s"), FormationNames::ToString(Squad->GetFormationKind()), Squad->HasFormationWallOrder() ? TEXT(", wall order") : TEXT(""),
			Squad->IsFormationSliding() ? TEXT(", sliding") : TEXT(""));
	}

	/** A wall body between the leader and a member (a trace at chest height, pawns ignored): the squad is split. */
	bool IsCutFromLeader(UWorld* World, const FState& State, const AOperativeCharacter* Leader, const AOperativeCharacter* Other)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FormationWallEndSmokeCut), false);
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			Params.AddIgnoredActor(Weak.Get());
		}
		const FVector Up(0.f, 0.f, 40.f);
		FHitResult Hit;
		return World->LineTraceSingleByChannel(Hit, Leader->GetActorLocation() + Up, Other->GetActorLocation() + Up, ECC_Visibility, Params);
	}

	void PlaceCamera(UWorld* World, FState& State, const FVector& Eye, const FVector& LookAt)
	{
		if (!State.Camera.IsValid())
		{
			State.Camera = World->SpawnActor<ACameraActor>(Eye, (LookAt - Eye).Rotation());
			if (ACameraActor* Created = State.Camera.Get())
			{
				Created->GetCameraComponent()->SetFieldOfView(60.f);
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

	/**
	 * A top-down still of the squad (Shot runs only): the game slowed to a standstill, the camera above the squad's centre. Returns true while
	 * the still is being taken (the caller waits).
	 */
	bool TakeShot(UWorld* World, FState& State, const TCHAR* Suffix)
	{
		if (!State.bShot || State.bShotTaken)
		{
			return false;
		}
		if (State.ShotFreezeTime < 0.f)
		{
			UGameplayStatics::SetGlobalTimeDilation(World, 0.01f);
			FVector Centre = FVector::ZeroVector;
			int32 Count = 0;
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
			{
				if (const AOperativeCharacter* Each = Weak.Get())
				{
					Centre += Each->GetActorLocation();
					++Count;
				}
			}
			Centre /= FMath::Max(1, Count);
			PlaceCamera(World, State, Centre + FVector(0.f, 0.f, 1300.f) - State.F * 150.f, Centre);
			State.ShotFreezeTime = State.StageTime;
			return true;
		}
		if (State.StageTime - State.ShotFreezeTime < 2.f)
		{
			return true;
		}
		const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Formation") / FString::Printf(TEXT("%s_%s.png"), ScenarioShotName(State.Scenario), Suffix);
		FScreenshotRequest::RequestScreenshot(Path, false, false);
		UE_LOG(LogCodexTactics, Display, TEXT("%s: screenshot %s"), ScenarioName(State.Scenario), *Path);
		State.bShotTaken = true;
		State.ShotFreezeTime = State.StageTime;
		return true;
	}

	/** After TakeShot: the game runs again (call once the still was requested, a second later). */
	bool ReleaseShot(UWorld* World, FState& State)
	{
		if (!State.bShot || !State.bShotTaken || State.ShotFreezeTime < 0.f)
		{
			return false;
		}
		if (State.StageTime - State.ShotFreezeTime < 1.f)
		{
			return true;
		}
		UGameplayStatics::SetGlobalTimeDilation(World, 1.f);
		if (APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0))
		{
			PC->SetViewTarget(PC->GetPawn());
		}
		State.ShotFreezeTime = -1.f;
		return false;
	}

	/** The arena, the squad of three in group mode (the Commander leading) standing 3 m in front of the long wall's middle. */
	bool SetUp(UWorld* World, FState& State, USquadSubsystem* Squad, ACodexTacticsPlayerController* PC, float StartR)
	{
		State.bSectors = FormationRuntime::IsEnabled();
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
			FBox2D(FVector2D(-450.f, -1050.f), FVector2D(950.f, 900.f)), Ignore);
		if (!Arena.bFound)
		{
			Check(State, false, FString::Printf(TEXT("an empty, flat, navigable 14 x 19.5 m spot for the smoke's own set-up (%d tried)"), Arena.Tried));
			return false;
		}
		State.P = Arena.Origin;
		State.F = Arena.Forward;
		State.R = Arena.Right;
		State.GroundZ = Arena.Origin.Z;
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke: arena at %s, Codex.Formation.Sectors %d"), *Arena.Origin.ToCompactString(), State.bSectors ? 1 : 0);
		Squad->ExitSoloMode(false, 0.f);
		Squad->SetLeader(Roster[0]);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			AOperativeCharacter* Each = Roster[Index];
			Each->StopOperative();
			Each->SetStance(EOperativeStance::Standing);
			Each->TeleportTo(Local(State, Index == 0 ? 100.f : -150.f, StartR + (Index == 0 ? 0.f : (Index == 1 ? -150.f : 150.f)), Each->GetSimpleCollisionHalfHeight() + 5.f),
				State.F.Rotation(), false, true);
		}
		Squad->ResetFormationState(State.F);
		// The long wall (R -600..600, near face at F 400): its LEFT end is free.
		SpawnWall(World, State, WallNearF + 0.5f * WallThickness, 0.f, WallThickness, 2.f * WallHalfLength);
		if (State.Scenario == EScenario::WallSwitch)
		{
			// A T junction beyond the right end: a cross wall (along F, F -250..1050) the long wall's RIGHT end meets (R 600..640).
			SpawnWall(World, State, 400.f, WallHalfLength + 0.5f * WallThickness, 1300.f, WallThickness);
		}
		if (State.bShot)
		{
			UVisionConeComponent::SetPlayerSquadCones(true);
		}
		return true;
	}

	/** The group cover order at the long wall's near face, R along it. */
	bool HugTheWall(UWorld* World, FState& State, USquadSubsystem* Squad, ACodexTacticsPlayerController* PC, float AtR)
	{
		FCoverSlot Slot;
		const bool bSlot = CoverTraceRules::FindCoverSlotAt(World, Local(State, WallNearF, AtR, 90.f), State.F, Slot);
		Check(State, bSlot, FString::Printf(TEXT("set-up: a cover slot at R %.0f of the long wall"), AtR));
		AOperativeCharacter* Leader = Squad->GetLeader();
		const bool bOrdered = bSlot && Leader && PC->OrderGroupCover(Leader, Slot, false, 1200.f, false);
		Check(State, bOrdered && Squad->HasFormationWallOrder(), TEXT("the group cover order in group mode runs as the formation wall layout"));
		return bOrdered;
	}

	// ---------------------------------------------------------------------------------------------------------------------------------
	// Bug A: the move to the wall's free end = the corner stack.

	/** One frame of the corner stack check: the point man at the edge looking round it, the leader second, the last man looking back. */
	bool CornerStackFrame(const USquadSubsystem* Squad, const FState& State, FString& OutWhy)
	{
		TArray<const AOperativeCharacter*> Order;
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			if (const AOperativeCharacter* Each = Weak.Get())
			{
				Order.Add(Each);
			}
		}
		if (Order.Num() != 3)
		{
			OutWhy = TEXT("a member is missing");
			return false;
		}
		// From the corner (the LEFT end, R = -600) back along the wall.
		Order.Sort([&State](const AOperativeCharacter& A, const AOperativeCharacter& B) { return AlongR(State, A.GetActorLocation()) < AlongR(State, B.GetActorLocation()); });
		const AOperativeCharacter* Point = Order[0];
		const AOperativeCharacter* Second = Order[1];
		const AOperativeCharacter* Rear = Order[2];
		const float PointR = AlongR(State, Point->GetActorLocation());
		const float PointF = AlongF(State, Point->GetActorLocation());
		// Along the wall to its end (yaw of -R); "round the corner" = turned from it towards the wall's far side (+F).
		const float AlongYaw = ArenaYaw(State, 0.f, -1.f);
		const float PointLook = static_cast<float>(Point->GetSightFacing().Rotation().Yaw);
		const float TowardsFar = FMath::FindDeltaAngleDegrees(AlongYaw, PointLook) * (FVector::DotProduct(FRotator(0.f, AlongYaw + 90.f, 0.f).Vector(), State.F) > 0.f ? 1.f : -1.f);
		const float BackYaw = ArenaYaw(State, 0.f, 1.f);
		const float RearOff = FMath::Abs(FMath::FindDeltaAngleDegrees(BackYaw, static_cast<float>(Rear->GetSightFacing().Rotation().Yaw)));
		const bool bEdge = PointR >= -WallHalfLength - 10.f && PointR <= -WallHalfLength + 110.f && PointF < WallNearF && PointF > WallNearF - 160.f;
		const bool bPeek = TowardsFar >= 10.f && TowardsFar <= 100.f;
		const bool bLeaderSecond = Second == Squad->GetLeader();
		const bool bRearBack = RearOff <= 70.f;
		OutWhy = FString::Printf(TEXT("point %s at R %.0f F %.0f (edge %d), look %.0f deg round the corner (peek %d); second %s (leader %d); rear %s looks %.0f deg off back (%d)"),
			*Point->DisplayName.ToString(), PointR, PointF, bEdge ? 1 : 0, TowardsFar, bPeek ? 1 : 0, *Second->DisplayName.ToString(), bLeaderSecond ? 1 : 0,
			*Rear->DisplayName.ToString(), RearOff, bRearBack ? 1 : 0);
		return bEdge && bPeek && bLeaderSecond && bRearBack;
	}

	bool StepCornerPeek(UWorld* World, FState& State, USquadSubsystem* Squad, ACodexTacticsPlayerController* PC)
	{
		AOperativeCharacter* Op = Squad->GetLeader();
		switch (State.Stage)
		{
		case 1:
		{
			if (State.StageTime < 3.f)
			{
				return true; // the navmesh rebuilds round the wall
			}
			if (!HugTheWall(World, State, Squad, PC, 100.f))
			{
				return Finish(State, false);
			}
			Next(State, 2);
			return true;
		}
		case 2:
		{
			FString Why;
			const bool bLayout = InWallLayout(Squad, State, &Why);
			if ((!bLayout || State.StageTime < 1.f) && State.StageTime < 25.f)
			{
				return true;
			}
			Check(State, bLayout, FString::Printf(TEXT("start: the wall layout, crouched facing out [%s ]"), *Why));
			// The player's click at the wall's free end: 20 cm past it, 90 cm off the face (the ghost at the edge).
			State.LeaderStartR = AlongR(State, Op->GetActorLocation());
			State.SlidesBefore = Squad->GetFormationSlideCount();
			const FVector Destination = Local(State, WallNearF - 90.f, -WallHalfLength - 20.f, Op->GetSimpleCollisionHalfHeight());
			Op->OrderMoveTo(Destination, false);
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: the leader's move to the wall's free end (from R %.0f):%s"), State.LeaderStartR, *DescribeSquad(Squad, State));
			Next(State, 3);
			return true;
		}
		case 3:
		{
			// The move; then everyone settles (crouched in his place) - or 30 s.
			State.bSawSlide |= Squad->IsFormationSliding();
			FString Why;
			const bool bSettled = InWallLayout(Squad, State, &Why) && State.StageTime > 1.f;
			if (FMath::IsNearlyEqual(FMath::Fmod(State.StageTime, 1.f), 0.f, StepSeconds * 0.5f))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: t %.1f:%s"), State.StageTime, *DescribeSquad(Squad, State));
			}
			if (!bSettled && State.StageTime < 30.f)
			{
				return true;
			}
			Check(State, bSettled, FString::Printf(TEXT("arrived: everyone settled at the wall's end [%s ]"), *Why));
			Check(State, State.bSawSlide && Squad->GetFormationSlideCount() > State.SlidesBefore, TEXT("the move along the wall to its end is the wall slide (the order kept)"));
			Next(State, 4);
			return true;
		}
		case 4:
		{
			if (TakeShot(World, State, TEXT("held")))
			{
				return true;
			}
			if (ReleaseShot(World, State))
			{
				return true;
			}
			// The stack held for HoldSeconds: checked every frame.
			FString Why;
			const bool bGood = CornerStackFrame(Squad, State, Why);
			State.HoldTime += StepSeconds;
			State.HoldGood += bGood ? StepSeconds : 0.f;
			if (!bGood && State.HoldWhy.IsEmpty())
			{
				State.HoldWhy = Why;
			}
			if (State.HoldTime < HoldSeconds)
			{
				return true;
			}
			Check(State, State.HoldGood >= 0.9f * State.HoldTime, FString::Printf(TEXT("the corner stack held %.1f of %.1f s: %s%s"), State.HoldGood, State.HoldTime, *Why,
				State.HoldWhy.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (first miss: %s)"), *State.HoldWhy)));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: held:%s"), *DescribeSquad(Squad, State));
			// Back along the wall: the stack slides back (the next order ends it), the wall layout again.
			State.SlidesBefore = Squad->GetFormationSlideCount();
			State.bSawSlide = false;
			Op = Squad->GetLeader();
			Op->OrderMoveTo(Local(State, WallNearF - 90.f, AlongR(State, Op->GetActorLocation()) + 400.f, Op->GetSimpleCollisionHalfHeight()), false);
			Next(State, 5);
			return true;
		}
		case 5:
		{
			State.bSawSlide |= Squad->IsFormationSliding();
			FString Why;
			const bool bSettled = InWallLayout(Squad, State, &Why) && State.StageTime > 1.f && !Squad->IsFormationSliding();
			if (!bSettled && State.StageTime < 30.f)
			{
				return true;
			}
			Check(State, bSettled && State.bSawSlide, FString::Printf(TEXT("from the stack 4 m back along the wall: a slide, then the wall layout [%s ]"), *Why));
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	// ---------------------------------------------------------------------------------------------------------------------------------
	// Bug B: a group cover order to the cross wall of a T junction never splits the squad.

	/** Variant v: the cover slot on the cross wall's LEFT face (towards the squad's side, R 600), F along it. */
	float SwitchVariantF(int32 Variant)
	{
		// Behind the long wall right at the joint, behind it a step from the joint, in front of it at the joint.
		static const float Fs[] = { 480.f, 560.f, 340.f, 300.f };
		return Fs[FMath::Clamp(Variant, 0, 3)];
	}

	bool StepWallSwitch(UWorld* World, FState& State, USquadSubsystem* Squad, ACodexTacticsPlayerController* PC)
	{
		constexpr int32 Variants = 4;
		switch (State.Stage)
		{
		case 1:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			if (!HugTheWall(World, State, Squad, PC, 250.f))
			{
				return Finish(State, false);
			}
			Next(State, 2);
			return true;
		}
		case 2:
		{
			FString Why;
			const bool bLayout = InWallLayout(Squad, State, &Why);
			if ((!bLayout || State.StageTime < 1.f) && State.StageTime < 25.f)
			{
				return true;
			}
			// The first hug is checked; a re-hug after a variant is the smoke's own set-up (logged only).
			if (State.Variant == 0)
			{
				Check(State, bLayout, FString::Printf(TEXT("variant %d start: the wall layout at the long wall [%s ]"), State.Variant, *Why));
			}
			else
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: variant %d start (%s): [%s ]"), State.Variant, bLayout ? TEXT("settled") : TEXT("not settled"), *Why);
			}
			// The cover click on the cross wall's face that looks at the long wall's right end (normal -R).
			const float AtF = SwitchVariantF(State.Variant);
			FCoverSlot Slot;
			const bool bSlot = CoverTraceRules::FindCoverSlotAt(World, Local(State, AtF, WallHalfLength, 90.f), State.R, Slot);
			AOperativeCharacter* Leader = Squad->GetLeader();
			const bool bOrdered = bSlot && PC->OrderGroupCover(Leader, Slot, false, 1200.f, false);
			Check(State, bOrdered, FString::Printf(TEXT("variant %d: the group cover order at the cross wall (F %.0f, slot at F %.0f R %.0f)"), State.Variant, AtF,
				bSlot ? AlongF(State, Slot.WorldLocation) : 0.f, bSlot ? AlongR(State, Slot.WorldLocation) : 0.f));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: variant %d ordered:%s"), State.Variant, *DescribeSquad(Squad, State));
			State.SplitTime = 0.f;
			State.WorstSplitTime = 0.f;
			Next(State, 3);
			return true;
		}
		case 3:
		{
			// The walk there: a member cut from the leader by a wall for long counts as split (crossing round a corner is a moment).
			const AOperativeCharacter* Leader = Squad->GetLeader();
			bool bCut = false;
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
			{
				if (const AOperativeCharacter* Each = Weak.Get(); Each && Leader && Each != Leader && IsCutFromLeader(World, State, Leader, Each))
				{
					bCut = true;
				}
			}
			State.SplitTime = bCut ? State.SplitTime + StepSeconds : 0.f;
			State.WorstSplitTime = FMath::Max(State.WorstSplitTime, State.SplitTime);
			if (FMath::IsNearlyEqual(FMath::Fmod(State.StageTime, 1.f), 0.f, StepSeconds * 0.5f))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: t %.1f%s:%s"), State.StageTime, bCut ? TEXT(" CUT") : TEXT(""), *DescribeSquad(Squad, State));
			}
			FString Why;
			const bool bSettled = InWallLayout(Squad, State, &Why) && State.StageTime > 1.f;
			if (!bSettled && State.StageTime < 30.f)
			{
				return true;
			}
			if (TakeShot(World, State, *FString::Printf(TEXT("v%d"), State.Variant)))
			{
				return true;
			}
			if (ReleaseShot(World, State))
			{
				return true;
			}
			State.bShotTaken = false;
			int32 Cut = 0;
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
			{
				if (const AOperativeCharacter* Each = Weak.Get(); Each && Leader && Each != Leader && IsCutFromLeader(World, State, Leader, Each))
				{
					++Cut;
				}
			}
			Check(State, bSettled, FString::Printf(TEXT("variant %d: everyone settled at the cross wall [%s ]"), State.Variant, *Why));
			Check(State, Cut == 0, FString::Printf(TEXT("variant %d: every member on the leader's side of every wall (%d cut off)"), State.Variant, Cut));
			Check(State, State.WorstSplitTime <= 1.5f, FString::Printf(TEXT("variant %d: nobody cut from the leader by a wall on the way for long (worst %.1f s)"), State.Variant,
				State.WorstSplitTime));
			if (++State.Variant >= Variants)
			{
				return Finish(State, true);
			}
			// Back to the long wall for the next variant.
			Squad->GetLeader()->OrderMoveTo(Local(State, 100.f, 250.f, Squad->GetLeader()->GetSimpleCollisionHalfHeight()), false);
			Next(State, 4);
			return true;
		}
		case 4:
		{
			if (State.StageTime < 6.f || (Squad->GetLeader()->IsMoving() && State.StageTime < 20.f))
			{
				return true;
			}
			if (!HugTheWall(World, State, Squad, PC, 250.f))
			{
				return Finish(State, false);
			}
			Next(State, 2);
			return true;
		}
		default:
			return Finish(State, false);
		}
	}

	// ---------------------------------------------------------------------------------------------------------------------------------
	// Bug C: the squad's guide lines along / round the wall.

	bool StepPathPreview(UWorld* World, FState& State, USquadSubsystem* Squad, ACodexTacticsPlayerController* PC)
	{
		switch (State.Stage)
		{
		case 1:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			if (!HugTheWall(World, State, Squad, PC, 100.f))
			{
				return Finish(State, false);
			}
			Next(State, 2);
			return true;
		}
		case 2:
		{
			FString Why;
			const bool bLayout = InWallLayout(Squad, State, &Why);
			if ((!bLayout || State.StageTime < 1.f) && State.StageTime < 25.f)
			{
				return true;
			}
			Check(State, bLayout, FString::Printf(TEXT("start: the wall layout [%s ]"), *Why));
			// Hover 1: the ground 4 m along the wall (a slide): every member's line along the wall.
			FHitResult Along;
			Along.bBlockingHit = true;
			Along.ImpactPoint = Local(State, WallNearF - 80.f, -300.f, 0.f);
			Along.Location = Along.ImpactPoint;
			Along.ImpactNormal = FVector::UpVector;
			Along.Normal = FVector::UpVector;
			const bool bAlong = PC->UpdateSquadMoveHover(&Along);
			TArray<TArray<FVector>> Paths;
			PC->GetSquadMovePreviewPaths(Paths);
			int32 Lines = 0;
			bool bAllAlong = true;
			for (const TArray<FVector>& Path : Paths)
			{
				Lines += Path.Num() >= 2 ? 1 : 0;
				for (const FVector& Point : Path)
				{
					bAllAlong &= AlongF(State, Point) < WallNearF; // never through or behind the wall
				}
			}
			Check(State, bAlong && Lines == 3 && bAllAlong, FString::Printf(TEXT("along the wall: the slide's guide line for every member (%d lines of %d, shown %d, in front of the wall %d)"),
				Lines, Paths.Num(), bAlong ? 1 : 0, bAllAlong ? 1 : 0));
			// Hover 2: round the free end (the far face side, 1 m behind the wall): the leader's path wraps round the end.
			FHitResult Round = Along;
			Round.ImpactPoint = Local(State, WallNearF + WallThickness + 100.f, -WallHalfLength + 150.f, 0.f);
			Round.Location = Round.ImpactPoint;
			const bool bRound = PC->UpdateSquadMoveHover(&Round);
			PC->GetSquadMovePreviewPaths(Paths);
			bool bWraps = false;
			bool bThrough = false;
			for (const TArray<FVector>& Path : Paths)
			{
				for (int32 Index = 0; Index < Path.Num(); ++Index)
				{
					bWraps |= AlongR(State, Path[Index]) < -WallHalfLength; // past the wall's end
					if (Index > 0)
					{
						// A leg straight through the wall body: both ends at the wall's span, one in front, one behind.
						const FVector& A = Path[Index - 1];
						const FVector& B = Path[Index];
						bThrough |= AlongR(State, A) > -WallHalfLength && AlongR(State, B) > -WallHalfLength && AlongF(State, A) < WallNearF && AlongF(State, B) > WallNearF + WallThickness;
					}
				}
			}
			Check(State, bRound && Paths.Num() >= 1 && bWraps && !bThrough, FString::Printf(TEXT("round the end: the guide line wraps round the wall's end (%d lines, shown %d, past the end %d, through the wall %d)"),
				Paths.Num(), bRound ? 1 : 0, bWraps ? 1 : 0, bThrough ? 1 : 0));
			// Hover 3: the open ground 6 m out: no squad guide line (a plain move).
			FHitResult Open = Along;
			Open.ImpactPoint = Local(State, -350.f, 100.f, 0.f);
			Open.Location = Open.ImpactPoint;
			const bool bOpen = PC->UpdateSquadMoveHover(&Open);
			Check(State, !bOpen, TEXT("the open ground away from the wall: no wall guide line"));
			PC->UpdateSquadMoveHover(nullptr);
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	// ---------------------------------------------------------------------------------------------------------------------------------
	// Bug D: a leader switch at the wall, then a move along it - the player's own clicks (ACodexTacticsPlayerController::HandleWorldHit).

	/** A click on the long wall's near face at R (the cover preview, then its confirm). */
	FHitResult WallClick(const FState& State, float AtR)
	{
		AStaticMeshActor* Wall = State.Walls.IsValidIndex(0) ? State.Walls[0].Get() : nullptr;
		FHitResult Hit(Wall, Wall ? Wall->GetStaticMeshComponent() : nullptr, Local(State, WallNearF, AtR, 120.f), -State.F);
		Hit.bBlockingHit = true;
		return Hit;
	}

	/** A click on the ground. */
	FHitResult GroundClick(const FState& State, float AtF, float AtR)
	{
		FHitResult Hit(nullptr, nullptr, Local(State, AtF, AtR, 0.f), FVector::UpVector);
		Hit.bBlockingHit = true;
		return Hit;
	}

	/** Part p of the leader-switch run: who is clicked to lead (the Medic-Sapper once; then the Commander and the Engineer), the slide click. */
	struct FSwapPart
	{
		TArray<int32> Leaders;
		float SlideAlongCm = 0.f;
	};

	const FSwapPart& SwapPart(int32 Part)
	{
		static const FSwapPart Parts[] = { { { 2 }, -450.f }, { { 0, 1 }, 350.f } };
		return Parts[FMath::Clamp(Part, 0, 1)];
	}

	bool StepLeaderSwap(UWorld* World, FState& State, USquadSubsystem* Squad, ACodexTacticsPlayerController* PC)
	{
		switch (State.Stage)
		{
		case 1:
		{
			if (State.StageTime < 3.f)
			{
				return true;
			}
			// The player's two clicks on the wall: the cover ghost, then its confirm (the group cover order = the wall layout).
			PC->HandleWorldHit(WallClick(State, 100.f));
			Next(State, 10);
			return true;
		}
		case 10:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			PC->HandleWorldHit(WallClick(State, 100.f));
			Check(State, Squad->HasFormationWallOrder(), TEXT("the clicks on the wall: the group cover order as the formation wall layout"));
			Next(State, 2);
			return true;
		}
		case 2:
		{
			FString Why;
			const bool bLayout = InWallLayout(Squad, State, &Why);
			if ((!bLayout || State.StageTime < 1.f) && State.StageTime < 25.f)
			{
				return true;
			}
			Check(State, bLayout, FString::Printf(TEXT("part %d start: the wall layout, crouched [%s ]"), State.Variant, *Why));
			State.HoldTime = 0.f;
			Next(State, 3);
			return true;
		}
		case 3:
		{
			// The leader switches: a click on each member in turn, 1.5 s apart.
			const FSwapPart& Part = SwapPart(State.Variant);
			const int32 Done = FMath::FloorToInt32(State.HoldTime);
			if (Done < Part.Leaders.Num())
			{
				if (State.StageTime >= 1.5f * static_cast<float>(Done + 1))
				{
					AOperativeCharacter* Picked = Member(State, Part.Leaders[Done]);
					PC->HandleWorldHit(FHitResult(Picked, nullptr, Picked->GetActorLocation(), FVector::UpVector));
					State.HoldTime += 1.f;
					State.SlidesBefore = static_cast<int32>(Picked->GetMoveOrderSerial()); // the switch must give him no order (no phantom move)
					UE_LOG(LogCodexTactics, Display, TEXT("Smoke: part %d: the click on %s - leader %s:%s"), State.Variant, *Picked->DisplayName.ToString(),
						*Squad->GetLeader()->DisplayName.ToString(), *DescribeSquad(Squad, State));
				}
				return true;
			}
			// The others re-form round the new leader at the wall (K in the middle), crouched again: the hug goes on.
			FString Hug;
			const bool bHug = InWallLayout(Squad, State, &Hug);
			if (FMath::IsNearlyEqual(FMath::Fmod(State.StageTime, 1.f), 0.f, StepSeconds * 0.5f))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: t %.1f:%s"), State.StageTime, *DescribeSquad(Squad, State));
			}
			if ((!bHug || State.StageTime < 1.5f * Part.Leaders.Num() + 1.f) && State.StageTime < 25.f)
			{
				return true;
			}
			Check(State, Squad->GetFormationKind() == EFormationKind::Wall && Squad->GetLeader() == Member(State, Part.Leaders.Last()),
				FString::Printf(TEXT("part %d: after the switch the squad stays at the wall (formation %s, leader %s)"), State.Variant,
					FormationNames::ToString(Squad->GetFormationKind()), *Squad->GetLeader()->DisplayName.ToString()));
			Check(State, static_cast<int32>(Squad->GetLeader()->GetMoveOrderSerial()) == State.SlidesBefore,
				FString::Printf(TEXT("part %d: the switch gave the new leader no order of his own (serial %u, at the switch %d)"), State.Variant,
					Squad->GetLeader()->GetMoveOrderSerial(), State.SlidesBefore));
			Check(State, bHug && Squad->HasFormationWallOrder(), FString::Printf(TEXT("part %d: the hug goes on with the new leader - the wall order, everyone crouched on his place [%s ]"),
				State.Variant, *Hug));
			{
				// K in the middle of the three along the wall.
				TArray<float> Rs;
				for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
				{
					Rs.Add(AlongR(State, Weak->GetActorLocation()));
				}
				Rs.Sort();
				const float LeaderR = AlongR(State, Squad->GetLeader()->GetActorLocation());
				Check(State, Rs.Num() == 3 && FMath::IsNearlyEqual(LeaderR, Rs[1], 1.f), FString::Printf(TEXT("part %d: the new leader in the middle of the line (R %.0f; %.0f / %.0f / %.0f)"),
					State.Variant, LeaderR, Rs.IsValidIndex(0) ? Rs[0] : 0.f, Rs.IsValidIndex(1) ? Rs[1] : 0.f, Rs.IsValidIndex(2) ? Rs[2] : 0.f));
			}
			// The ground click along the same wall, 1 m off its face (the slide).
			AOperativeCharacter* Op = Squad->GetLeader();
			State.LeaderStartR = AlongR(State, Op->GetActorLocation());
			State.SlidesBefore = Squad->GetFormationSlideCount();
			State.bSawSlide = false;
			PC->HandleWorldHit(GroundClick(State, WallNearF - 100.f, State.LeaderStartR + Part.SlideAlongCm));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: part %d: the click %.0f cm along the wall:%s"), State.Variant, Part.SlideAlongCm, *DescribeSquad(Squad, State));
			Next(State, 4);
			return true;
		}
		case 4:
		{
			const FSwapPart& Part = SwapPart(State.Variant);
			State.bSawSlide |= Squad->IsFormationSliding();
			AOperativeCharacter* Op = Squad->GetLeader();
			if (FMath::IsNearlyEqual(FMath::Fmod(State.StageTime, State.StageTime < 4.f ? 0.25f : 1.f), 0.f, StepSeconds * 0.5f))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: t %.1f:%s"), State.StageTime, *DescribeSquad(Squad, State));
			}
			FString Why;
			const bool bSettled = State.StageTime > 2.f && !Op->IsMoving() && InWallLayout(Squad, State, &Why);
			if (!bSettled && State.StageTime < 25.f)
			{
				return true;
			}
			const float Moved = AlongR(State, Op->GetActorLocation()) - State.LeaderStartR;
			Check(State, Moved * FMath::Sign(Part.SlideAlongCm) >= FMath::Abs(Part.SlideAlongCm) - 100.f,
				FString::Printf(TEXT("part %d: after the leader switch the move along the wall runs: the leader moved %.0f cm (ordered %.0f)"), State.Variant, Moved,
					Part.SlideAlongCm));
			Check(State, State.bSawSlide && Squad->GetFormationSlideCount() > State.SlidesBefore, FString::Printf(TEXT("part %d: ... as the wall slide"), State.Variant));
			Check(State, bSettled, FString::Printf(TEXT("part %d: ... and the wall layout again at its end, crouched [%s ]"), State.Variant, *Why));
			if (State.Variant == 0)
			{
				++State.Variant;
				Next(State, 2);
				return true;
			}
			// Then a click 4 m off the wall: no slide, a normal move.
			State.LeaderStartR = AlongR(State, Op->GetActorLocation());
			State.SlidesBefore = Squad->GetFormationSlideCount();
			PC->HandleWorldHit(GroundClick(State, WallNearF - 450.f, State.LeaderStartR - 200.f));
			Next(State, 5);
			return true;
		}
		case 5:
		{
			AOperativeCharacter* Op = Squad->GetLeader();
			const float Off = WallNearF - AlongF(State, Op->GetActorLocation());
			if ((Off < 400.f || Op->IsMoving()) && State.StageTime < 20.f)
			{
				return true;
			}
			Check(State, Off >= 330.f && Squad->GetFormationSlideCount() == State.SlidesBefore && !Squad->HasFormationWallOrder(),
				FString::Printf(TEXT("a click 4 m off the wall: a normal move (the leader %.0f cm off the wall, slides %d -> %d, wall order %d)"), Off, State.SlidesBefore,
					Squad->GetFormationSlideCount(), Squad->HasFormationWallOrder() ? 1 : 0));
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	// ---------------------------------------------------------------------------------------------------------------------------------
	// The squad's corner peek (user bug report 2026-10-26): the hover ghost and the click, at both ends of the long wall.

	/** The end the variant peeks round: R of the wall's end (variant 0: the LEFT end, -R; 1: the right end). */
	float PeekEndR(const FState& State)
	{
		return State.Variant == 0 ? -WallHalfLength : WallHalfLength;
	}

	/** One frame of the squad peek: the peeker at the end leaning out round it, the leader second from the end, nobody beyond the wall. */
	bool SquadPeekFrame(const USquadSubsystem* Squad, const FState& State, FString& OutWhy)
	{
		const AOperativeCharacter* Peeker = State.Peeker.Get();
		TArray<const AOperativeCharacter*> Order;
		bool bNearSide = true;
		for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
		{
			if (const AOperativeCharacter* Each = Weak.Get())
			{
				Order.Add(Each);
				bNearSide &= AlongF(State, Each->GetActorLocation()) < WallNearF;
			}
		}
		if (!Peeker || Order.Num() != 3)
		{
			OutWhy = TEXT("a member is missing");
			return false;
		}
		const float EndR = PeekEndR(State);
		Order.Sort([&State, EndR](const AOperativeCharacter& A, const AOperativeCharacter& B)
		{
			return FMath::Abs(AlongR(State, A.GetActorLocation()) - EndR) < FMath::Abs(AlongR(State, B.GetActorLocation()) - EndR);
		});
		const float PeekerToEnd = FMath::Abs(AlongR(State, Peeker->GetActorLocation()) - EndR);
		const bool bPose = Peeker->bInCover && Peeker->bAtCoverCorner && Peeker->IsCornerAimActive() && Peeker->IsManualCornerPeek() && Peeker->bIsCornerLeaning;
		const bool bFirst = Order[0] == Peeker && PeekerToEnd <= 130.f;
		const bool bLeaderSecond = Order[1] == Squad->GetLeader();
		OutWhy = FString::Printf(TEXT("peeker %s %.0f cm from the end (first %d), in cover %d, at the corner %d, corner aim %d, manual peek %d, leaning %d (peeking %d); second %s (leader %d); all on the near side %d"),
			*Peeker->DisplayName.ToString(), PeekerToEnd, bFirst ? 1 : 0, Peeker->bInCover ? 1 : 0, Peeker->bAtCoverCorner ? 1 : 0, Peeker->IsCornerAimActive() ? 1 : 0,
			Peeker->IsManualCornerPeek() ? 1 : 0, Peeker->bIsCornerLeaning ? 1 : 0, Peeker->IsPeekingRoundCorner() ? 1 : 0, *Order[1]->DisplayName.ToString(),
			bLeaderSecond ? 1 : 0, bNearSide ? 1 : 0);
		return bPose && bFirst && bLeaderSecond && bNearSide;
	}

	bool StepSquadPeek(UWorld* World, FState& State, USquadSubsystem* Squad, ACodexTacticsPlayerController* PC)
	{
		switch (State.Stage)
		{
		case 1:
		{
			if (State.StageTime < 3.f)
			{
				return true; // the navmesh rebuilds round the wall
			}
			// The player's two clicks on the wall: the cover ghost, then its confirm (the group cover order = the formation wall layout).
			PC->HandleWorldHit(WallClick(State, 100.f));
			Next(State, 10);
			return true;
		}
		case 10:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			PC->HandleWorldHit(WallClick(State, 100.f));
			Check(State, Squad->HasFormationWallOrder(), TEXT("the clicks on the wall: the group cover order as the formation wall layout"));
			Next(State, 2);
			return true;
		}
		case 2:
		{
			FString Why;
			const bool bLayout = InWallLayout(Squad, State, &Why) || (State.Variant > 0 && Squad->GetFormationKind() == EFormationKind::Wall && State.StageTime > 1.f);
			if ((!bLayout || State.StageTime < 1.f) && State.StageTime < 25.f)
			{
				return true;
			}
			if (State.Variant == 0)
			{
				Check(State, bLayout, FString::Printf(TEXT("start: the wall layout, crouched facing out, nobody in a cover slot [%s ]"), *Why));
			}
			AOperativeCharacter* Leader = Squad->GetLeader();
			const float EndR = PeekEndR(State);
			const float Inward = State.Variant == 0 ? 1.f : -1.f;
			// The member nearest that end (where they stand now).
			AOperativeCharacter* Nearest = nullptr;
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
			{
				if (AOperativeCharacter* Each = Weak.Get(); Each && (!Nearest || FMath::Abs(AlongR(State, Each->GetActorLocation()) - EndR) < FMath::Abs(AlongR(State, Nearest->GetActorLocation()) - EndR)))
				{
					Nearest = Each;
				}
			}
			// Hover 1: the wall's end (its near face 20 cm in from the end) - the peek ghost of the member nearest it, the crosshair.
			const FHitResult EndHit = WallClick(State, EndR + Inward * 20.f);
			const bool bGhost = PC->UpdateCornerPeekHover(Leader, &EndHit);
			const ACoverGhostActor* Ghost = PC->GetCornerPeekGhost();
			const AOperativeCharacter* GhostOf = Ghost ? Ghost->GetOperative() : nullptr;
			Check(State, !Leader->bInCover, TEXT("the squad hugs the wall in the formation layout: the leader is in no cover slot (the single peek cannot apply)"));
			Check(State, bGhost && PC->IsCornerPeekPreviewShown() && Ghost && Ghost->IsCornerPeekPose() && GhostOf == Nearest && PC->CurrentMouseCursor == EMouseCursor::Crosshairs,
				FString::Printf(TEXT("end %d: hovering the wall's end shows the peek ghost (shown %d, corner-peek pose %d) of the member nearest it (%s; ghost of %s), the crosshair %d"),
					State.Variant, bGhost ? 1 : 0, Ghost && Ghost->IsCornerPeekPose() ? 1 : 0, Nearest ? *Nearest->DisplayName.ToString() : TEXT("-"),
					GhostOf ? *GhostOf->DisplayName.ToString() : TEXT("-"), PC->CurrentMouseCursor == EMouseCursor::Crosshairs ? 1 : 0));
			if (Ghost)
			{
				Check(State, FMath::Abs(AlongR(State, Ghost->GetSlot().WorldLocation) - EndR) <= 110.f && AlongF(State, Ghost->GetSlot().WorldLocation) < WallNearF,
					FString::Printf(TEXT("end %d: the ghost stands at the corner on the squad's side (R %.0f, F %.0f)"), State.Variant,
						AlongR(State, Ghost->GetSlot().WorldLocation), AlongF(State, Ghost->GetSlot().WorldLocation)));
			}
			// Hover 2: the ground along the wall 2.5 m from the end - no ghost; the squad's guide lines stay (the slide / the corner stack).
			const FHitResult Along = GroundClick(State, WallNearF - 100.f, EndR + Inward * 250.f);
			const bool bAlongGhost = PC->UpdateCornerPeekHover(Leader, &Along);
			const bool bLines = PC->UpdateSquadMoveHover(&Along);
			Check(State, !bAlongGhost && !PC->IsCornerPeekPreviewShown() && bLines,
				FString::Printf(TEXT("end %d: the ground along the wall: no peek ghost (%d), the squad's guide lines (%d)"), State.Variant, bAlongGhost ? 1 : 0, bLines ? 1 : 0));
			PC->UpdateSquadMoveHover(nullptr);
			// Hover 3: the wall's middle - no ghost (a click there is the cover order).
			const FHitResult Middle = WallClick(State, State.Variant == 0 ? 300.f : -300.f);
			Check(State, !PC->UpdateCornerPeekHover(Leader, &Middle) && !PC->IsCornerPeekPreviewShown(), FString::Printf(TEXT("end %d: the wall's middle: no peek ghost"), State.Variant));
			// Back on the end (the still shows the ghost), then the click.
			PC->UpdateCornerPeekHover(Leader, &EndHit);
			State.Peeker = Nearest;
			Next(State, 3);
			return true;
		}
		case 3:
		{
			if (State.Variant == 0)
			{
				PC->bFreezeCornerPeekHoverForTesting = true;
				if (TakeShot(World, State, TEXT("ghost")))
				{
					return true;
				}
				if (ReleaseShot(World, State))
				{
					return true;
				}
				PC->bFreezeCornerPeekHoverForTesting = false;
				State.bShotTaken = false; // the next still: the peek
			}
			State.PeeksBefore = Squad->GetFormationPeekCount();
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: end %d: the click on the wall's end:%s"), State.Variant, *DescribeSquad(Squad, State));
			PC->HandleWorldHit(WallClick(State, PeekEndR(State) + (State.Variant == 0 ? 20.f : -20.f)));
			Check(State, Squad->GetFormationPeeker() == State.Peeker.Get() && !PC->HasCoverPreview(),
				FString::Printf(TEXT("end %d: the click on the end orders the squad's corner peek for %s (peeker %s, no cover ghost %d)"), State.Variant,
					State.Peeker.IsValid() ? *State.Peeker->DisplayName.ToString() : TEXT("-"),
					Squad->GetFormationPeeker() ? *Squad->GetFormationPeeker()->DisplayName.ToString() : TEXT("-"), PC->HasCoverPreview() ? 0 : 1));
			State.HoldTime = 0.f;
			State.HoldGood = 0.f;
			State.HoldWhy.Reset();
			Next(State, 4);
			return true;
		}
		case 4:
		{
			FString Why;
			const bool bGood = SquadPeekFrame(Squad, State, Why);
			if (FMath::IsNearlyEqual(FMath::Fmod(State.StageTime, 1.f), 0.f, StepSeconds * 0.5f))
			{
				UE_LOG(LogCodexTactics, Display, TEXT("Smoke: t %.1f: %s;%s"), State.StageTime, *Why, *DescribeSquad(Squad, State));
			}
			// Everyone settled (the peeker leaning out, the others at their stack places).
			bool bStill = true;
			for (const TWeakObjectPtr<AOperativeCharacter>& Weak : State.Members)
			{
				bStill &= Weak.IsValid() && Weak->GetVelocity().Size2D() < 5.f;
			}
			if (!(bGood && bStill && State.StageTime > 1.f) && State.StageTime < 45.f)
			{
				return true;
			}
			Check(State, bGood, FString::Printf(TEXT("end %d: the nearest member peeks round the corner, the leader second, nobody beyond the wall (%.1f s): %s"), State.Variant,
				State.StageTime, *Why));
			Check(State, Squad->GetFormationPeekCount() == State.PeeksBefore + 1, FString::Printf(TEXT("end %d: one squad peek order given (%d -> %d)"), State.Variant,
				State.PeeksBefore, Squad->GetFormationPeekCount()));
			Next(State, 5);
			return true;
		}
		case 5:
		{
			if (State.Variant == 0)
			{
				if (TakeShot(World, State, TEXT("held")))
				{
					return true;
				}
				if (ReleaseShot(World, State))
				{
					return true;
				}
			}
			FString Why;
			const bool bGood = SquadPeekFrame(Squad, State, Why);
			State.HoldTime += StepSeconds;
			State.HoldGood += bGood ? StepSeconds : 0.f;
			if (!bGood && State.HoldWhy.IsEmpty())
			{
				State.HoldWhy = Why;
			}
			if (State.HoldTime < HoldSeconds)
			{
				return true;
			}
			Check(State, State.HoldGood >= 0.9f * State.HoldTime, FString::Printf(TEXT("end %d: the peek held %.1f of %.1f s: %s%s"), State.Variant, State.HoldGood, State.HoldTime,
				*Why, State.HoldWhy.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (first miss: %s)"), *State.HoldWhy)));
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke: end %d held:%s"), State.Variant, *DescribeSquad(Squad, State));
			if (State.Variant == 0)
			{
				++State.Variant;
				Next(State, 2);
				return true;
			}
			// The leader's next order (a click 8 m off the wall: beyond SquadOrderRules::CoverLeaveSlotDistanceCm of the corner) ends the peek: the
			// peeker follows out of his corner (the followers-in-cover rule).
			AOperativeCharacter* Op = Squad->GetLeader();
			PC->HandleWorldHit(GroundClick(State, WallNearF - 800.f, AlongR(State, Op->GetActorLocation())));
			Next(State, 6);
			return true;
		}
		case 6:
		{
			const AOperativeCharacter* Peeker = State.Peeker.Get();
			const bool bOut = !Squad->GetFormationPeeker() && Peeker && !Peeker->bInCover;
			if (!bOut && State.StageTime < 15.f)
			{
				return true;
			}
			Check(State, bOut, FString::Printf(TEXT("the leader's next order ends the squad peek: the peeker leaves his corner and follows (peek %d, in cover %d)"),
				Squad->GetFormationPeeker() ? 1 : 0, Peeker && Peeker->bInCover ? 1 : 0));
			return Finish(State, true);
		}
		default:
			return Finish(State, false);
		}
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 260.f)
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
			if (!SetUp(World, State, Squad, PC, State.Scenario == EScenario::WallSwitch ? 250.f : 100.f))
			{
				return Finish(State, false);
			}
			if (!State.bSectors)
			{
				Check(State, false, TEXT("Codex.Formation.Sectors is 1 (these checks are the formation's)"));
				return Finish(State, false);
			}
			Next(State, 1);
			return true;
		}
		if (!Squad || !PC || !Squad->GetLeader())
		{
			return Finish(State, false);
		}
		switch (State.Scenario)
		{
		case EScenario::CornerPeek: return StepCornerPeek(World, State, Squad, PC);
		case EScenario::WallSwitch: return StepWallSwitch(World, State, Squad, PC);
		case EScenario::PathPreview: return StepPathPreview(World, State, Squad, PC);
		case EScenario::SquadPeek: return StepSquadPeek(World, State, Squad, PC);
		default: return StepLeaderSwap(World, State, Squad, PC);
		}
	}

	void Run(EScenario Scenario, const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TSharedRef<FState> State = MakeShared<FState>();
		State->Scenario = Scenario;
		State->bShot = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("Shot"), ESearchCase::IgnoreCase); });
		if (IConsoleVariable* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.Formation.Debug")); Debug && Debug->GetInt() == 0)
		{
			Debug->Set(2, ECVF_SetByCode); // the formation's decisions in the log (the smoke's evidence)
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs CornerPeekCommand(
		TEXT("CodexTactics.FormationCornerPeekSmoke"),
		TEXT("Dev check (bug A 2026-10-26): a move along the wall to its free end = the corner stack (the point man peeks, the leader second, the rear man back); PASS/FAIL, exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Run(EScenario::CornerPeek, Args, World); }));

	static FAutoConsoleCommandWithWorldAndArgs WallSwitchCommand(
		TEXT("CodexTactics.FormationWallSwitchSmoke"),
		TEXT("Dev check (bug B 2026-10-26): a group cover order to the cross wall of a T junction never splits the squad; PASS/FAIL, exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Run(EScenario::WallSwitch, Args, World); }));

	static FAutoConsoleCommandWithWorldAndArgs PathPreviewCommand(
		TEXT("CodexTactics.FormationSquadPathPreviewSmoke"),
		TEXT("Dev check (bug C 2026-10-26): the squad at a wall shows its dashed guide lines along / round the wall; PASS/FAIL, exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Run(EScenario::PathPreview, Args, World); }));

	static FAutoConsoleCommandWithWorldAndArgs SquadPeekCommand(
		TEXT("CodexTactics.FormationSquadPeekSmoke"),
		TEXT("Dev check (user bug report 2026-10-26): the squad hugging a wall - the hover on its end shows the peek ghost of the nearest member, the click makes him peek round the corner (the leader second), at both ends; PASS/FAIL, exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Run(EScenario::SquadPeek, Args, World); }));

	static FAutoConsoleCommandWithWorldAndArgs LeaderSwapCommand(
		TEXT("CodexTactics.FormationLeaderSwapSlideSmoke"),
		TEXT("Dev check (bug D 2026-10-26): one leader switch at the wall, then a move along it slides; a click 4 m off it is a normal move; PASS/FAIL, exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World) { Run(EScenario::LeaderSwap, Args, World); }));
}

#endif // !UE_BUILD_SHIPPING
