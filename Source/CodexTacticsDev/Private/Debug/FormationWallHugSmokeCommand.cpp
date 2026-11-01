// Dev-only headless check of the squad hugging a wall in the formation wall layout (behind Codex.Formation.Sectors) - the game designer's
// play-test bug 2026-10-27 (main 804d64c2, the Demo 01 lake compound's yard by the blast door; screenshots hug_wall_ok / hug_wall_gap): "at one
// wall it hugs, at the other it doesn't - and look at the distance between the squad and the wall". At the base building's west wall in the
// open yard the squad crouched along it ("Along the wall (3 operatives), facing out!"); in the passage between the building's east wall and
// the compound's east wall 3.6 m away it bunched up off the wall; everywhere the men stood a hand-span farther off the face than one
// operative in cover.
//   CodexTactics.FormationWallHugSmoke [Shot]
//   powershell -ExecutionPolicy Bypass -File Scripts/smoke_parallel.ps1 -Commands FormationWallHugSmoke [-PreCmds "Codex.Formation.Debug 2"]
//   (map /Game/Maps/L_Demo01_A_FrozenLake + -LevelJson, Scripts/test_map.json)
//   rendered still ("Shot"; -RenderOffscreen): Saved/Screenshots/Formation/wall_hug_fixed.png - the passage hug from straight above.
// The real yard of L_Demo01_A_FrozenLake (the tutorial skipped, checkpoints off, every enemy parked; world coordinates of the user's layout,
// checked by traces at the start): the base building (Base_Wall_* 40 cm thick, 3.2 m high, roofed), the compound's east wall (2.5 m) 3.6 m
// off its east face, and - spawned for the run only, nothing saved - a 1.2 m low wall 2 m off its south face (the narrowest passage a
// squad walks: a man must pass the crouched leader there). Group mode (Solo off, no box selection), the leader walks up, then the player's
// two clicks on the wall (the cover ghost, its confirm) through ACodexTacticsPlayerController::HandleWorldHit. Six scenes - the plain wall
// (the building's west face), the compound passage and the narrow passage, each in exploration and in the real-time wave fight (group cover
// in combat, user decision 2026-10-26):
//   - the click on the wall -> the group cover order is the formation wall layout, all three settle in the wall pose (crouched, facing out);
//   - each man's body centre within the solo cover stand-off (FCoverTraceConfig::SlotOffsetCm, one operative in cover) + MaxExtraGapCm of
//     the clicked face - not a hand-span off it;
//   - nobody on the far side (behind the clicked face, or on / past the passage's other wall), nobody bunched (>= MinSpacingCm apart).
// Godot reference: none (UE-only).

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
#include "Core/SaveGameSubsystem.h"
#include "Debug/SmokeUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Formation/FormationRuntimeRules.h"
#include "Formation/FormationTypes.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/HUD.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Tactics/CoverTraceRules.h"
#include "Tutorial/TutorialSubsystem.h"
#include "UnrealClient.h"

namespace FormationWallHugSmoke
{
	constexpr float StepSeconds = 0.05f;
	/** A member's body centre may stand this much farther off the face than one operative in cover (the solo slot), cm. */
	constexpr float MaxExtraGapCm = 10.f;
	/** Two members closer than this are bunched up (the layout puts them wall.spacing = 1 m apart), cm. */
	constexpr float MinSpacingCm = 70.f;
	/** Everyone in the wall pose this long before the scene is judged; the cap of a scene, s. */
	constexpr float SettleHoldSeconds = 1.5f;
	constexpr float SceneCapSeconds = 25.f;
	/** The spawned low wall of the narrow passage: this far off the building's south face, this thick and high, cm. */
	constexpr float NarrowWidthCm = 200.f;
	constexpr float LowWallThicknessCm = 30.f;
	constexpr float LowWallHeightCm = 120.f;

	/** One scene's wall: a point on the clicked face (world XY), its outward normal, the passage's width (0 = an open face). */
	struct FSceneWall
	{
		const TCHAR* Name = TEXT("");
		FVector2D Face = FVector2D::ZeroVector;
		FVector2D Normal = FVector2D(1.0, 0.0);
		float PassageWidthCm = 0.f;
		/** The leader's start and his walk's goal; the others start in a file behind him. */
		FVector2D Start = FVector2D::ZeroVector;
		FVector2D WalkTo = FVector2D::ZeroVector;
	};

	/**
	 * The lake yard (L_Demo01_A_FrozenLake as laid out by the user, 2026-10-27 dump): Base_Wall_W2 x 30900..30940 (y -2410..-1590), Base_Wall_E
	 * x 31900..31940 (y -3390..-1590), Base_Wall_S y -3410..-3370 (x 30920..31920); Compound_Wall_E1 / E2 inner face x 32300 (an opening
	 * y -2590..-2390), the blast door at (32320, -1300).
	 */
	const FSceneWall& Wall(int32 Kind)
	{
		static const FSceneWall Walls[] = {
			{ TEXT("plain wall (the building's west face)"), FVector2D(30900.0, -2000.0), FVector2D(-1.0, 0.0), 0.f, FVector2D(30250.0, -2050.0), FVector2D(30550.0, -2050.0) },
			{ TEXT("compound passage (east face, 3.6 m)"), FVector2D(31940.0, -2150.0), FVector2D(1.0, 0.0), 360.f, FVector2D(32120.0, -1350.0), FVector2D(32120.0, -1850.0) },
			{ TEXT("narrow passage (south face, 2 m)"), FVector2D(31500.0, -3410.0), FVector2D(0.0, -1.0), NarrowWidthCm, FVector2D(30450.0, -3510.0), FVector2D(31050.0, -3510.0) },
		};
		return Walls[FMath::Clamp(Kind, 0, 2)];
	}

	constexpr int32 WallKinds = 3;
	constexpr int32 SceneCount = 2 * WallKinds;

	bool IsFightScene(int32 Scene)
	{
		return Scene >= WallKinds;
	}

	FString SceneName(int32 Scene)
	{
		return FString::Printf(TEXT("%s, %s"), Wall(Scene % WallKinds).Name, IsFightScene(Scene) ? TEXT("real-time fight") : TEXT("exploration"));
	}

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		int32 Scene = 0;
		bool bShot = false;
		bool bShotTaken = false;
		float GroundZ = 0.f;
		TArray<TWeakObjectPtr<AOperativeCharacter>> Members;
		TWeakObjectPtr<AStaticMeshActor> LowWall;
		TWeakObjectPtr<ACameraActor> Camera;
		float StableSince = -1.f;
		/** The worst gap seen over all scenes (body centre off the face minus the solo stand-off), cm. */
		float WorstGap = -1000.f;
		TArray<FString> Table;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(FState& State, bool bComplete)
	{
		for (const FString& Row : State.Table)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke table: %s"), *Row);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		if (State.LowWall.IsValid())
		{
			State.LowWall->Destroy();
		}
		FPlatformMisc::RequestExit(false, TEXT("FormationWallHugSmoke"));
		return false;
	}

	void Next(FState& State, int32 Stage)
	{
		State.Stage = Stage;
		State.StageTime = 0.f;
		State.StableSince = -1.f;
	}

	AOperativeCharacter* Member(const FState& State, int32 Index)
	{
		return State.Members.IsValidIndex(Index) ? State.Members[Index].Get() : nullptr;
	}

	FVector At(const FState& State, const FVector2D& XY, float Up = 0.f)
	{
		return FVector(XY.X, XY.Y, State.GroundZ + Up);
	}

	FCollisionQueryParams IgnoreSquad(const FState& State)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FormationWallHugSmoke), false);
		for (const TWeakObjectPtr<AOperativeCharacter>& Each : State.Members)
		{
			Params.AddIgnoredActor(Each.Get());
		}
		return Params;
	}

	/** The clicked face as the cursor sees it: a trace from 60 cm out onto the face, 1.2 m up. */
	bool TraceFace(UWorld* World, const FState& State, const FSceneWall& Scene, FHitResult& OutHit)
	{
		const FVector Out = At(State, Scene.Face + Scene.Normal * 60.0, 120.f);
		return World->LineTraceSingleByChannel(OutHit, Out, Out - FVector(Scene.Normal, 0.0) * 120.f, ECC_Visibility, IgnoreSquad(State))
			&& FVector2D::DotProduct(FVector2D(OutHit.ImpactNormal).GetSafeNormal(), Scene.Normal) > 0.9;
	}

	/** Everyone out of cover / the wall pose, standing; the leader at Start, the others in a file behind him (Solo while placed). */
	void ResetSquad(UWorld* World, USquadSubsystem* Squad, FState& State, const FSceneWall& Scene)
	{
		Squad->EnterSoloMode();
		const FVector2D Back = (Scene.Start - Scene.WalkTo).GetSafeNormal();
		const float Yaw = static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(-Back.Y, -Back.X)));
		for (int32 Index = 0; Index < State.Members.Num(); ++Index)
		{
			AOperativeCharacter* Each = Member(State, Index);
			if (!Each)
			{
				continue;
			}
			Each->LeaveCover(TEXT("smoke reset"));
			Each->SetFormationWallPose(false);
			Each->ClearPendingCover();
			Each->StopOperative();
			Each->SetStance(EOperativeStance::Standing);
			const FVector Spot = SmokeUtils::FreeSpot(World, At(State, Scene.Start + Back * (150.0 * Index), 92.f), Each);
			Each->TeleportTo(Spot + FVector(0.f, 0.f, 4.f), FRotator(0.f, Yaw, 0.f), false, true);
		}
		Squad->SetLeader(Member(State, 0));
	}

	void ParkEnemies(UWorld* World)
	{
		for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
		{
			if (It->IsActorTickEnabled())
			{
				SmokeUtils::ParkEnemyFarAway(*It);
				It->SetActorTickEnabled(false);
			}
		}
	}

	/** The narrow passage's low wall along the building's south face (runtime only; Base_Wall_S x 30920..31920). */
	void SpawnLowWall(UWorld* World, FState& State)
	{
		const FSceneWall& Scene = Wall(2);
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		const FVector2D Centre(31420.0, Scene.Face.Y + Scene.Normal.Y * (NarrowWidthCm + 0.5 * LowWallThicknessCm));
		const FTransform Transform(FRotator::ZeroRotator, At(State, Centre, 0.5f * LowWallHeightCm), FVector(10.f, LowWallThicknessCm / 100.f, LowWallHeightCm / 100.f));
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
		State.LowWall = Block;
	}

	/** The still from straight above: bCapture false puts the camera there (no motion blur), true - a second later - takes the picture. */
	void TopDownShot(UWorld* World, FState& State, const FSceneWall& Scene, bool bCapture)
	{
		if (bCapture)
		{
			const FString Path = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("Formation") / TEXT("wall_hug_fixed.png");
			FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ false, /*bAddFilenameSuffix*/ false);
			UE_LOG(LogCodexTactics, Display, TEXT("FormationWallHug: screenshot %s"), *Path);
			State.bShotTaken = true;
			return;
		}
		const FVector2D Centre = Scene.Face + Scene.Normal * (0.5 * Scene.PassageWidthCm);
		const FVector Eye = At(State, Centre, 1400.f);
		const FRotator Down(-90.f, FMath::RadiansToDegrees(FMath::Atan2(Scene.Normal.X, -Scene.Normal.Y)), 0.f);
		if (!State.Camera.IsValid())
		{
			State.Camera = World->SpawnActor<ACameraActor>(Eye, Down);
			if (ACameraActor* Created = State.Camera.Get())
			{
				Created->GetCameraComponent()->SetFieldOfView(45.f);
				Created->GetCameraComponent()->bConstrainAspectRatio = false;
				Created->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount = true;
				Created->GetCameraComponent()->PostProcessSettings.MotionBlurAmount = 0.f;
			}
		}
		if (ACameraActor* Cam = State.Camera.Get())
		{
			Cam->SetActorLocationAndRotation(Eye, Down);
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

	/** The judged scene: the wall layout, the gaps off the face, nobody beyond it / past the passage's other wall, nobody bunched. */
	void JudgeScene(FState& State, USquadSubsystem* Squad, int32 SceneIndex)
	{
		const FSceneWall& Scene = Wall(SceneIndex % WallKinds);
		const FString Name = SceneName(SceneIndex);
		const float Solo = FCoverTraceConfig().SlotOffsetCm;
		bool bAllPose = true;
		float Worst = -1000.f;
		float Nearest = 1000.f;
		bool bFarSide = false;
		FString Line;
		TArray<FVector2D> Places;
		const FVector2D Along(-Scene.Normal.Y, Scene.Normal.X);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const AOperativeCharacter* Each = Member(State, Index);
			if (!Each)
			{
				bAllPose = false;
				continue;
			}
			const FVector2D Here(Each->GetActorLocation());
			const float Off = static_cast<float>(FVector2D::DotProduct(Here - Scene.Face, Scene.Normal));
			bAllPose &= Each->IsInFormationWallPose() && Each->GetStance() == EOperativeStance::Crouching;
			Worst = FMath::Max(Worst, Off - Solo);
			Nearest = FMath::Min(Nearest, Off);
			bFarSide |= Off <= 0.f || (Scene.PassageWidthCm > 0.f && Off >= Scene.PassageWidthCm);
			Places.Add(Here);
			Line += FString::Printf(TEXT(" [%s: %.0f cm off the face (gap %+.0f), %.0f along, %s%s]"), *Each->DisplayName.ToString(), Off, Off - Solo,
				FVector2D::DotProduct(Here - Scene.Face, Along), Each->IsInFormationWallPose() ? TEXT("wall pose") : TEXT("NOT in the wall pose"),
				Each->GetStance() == EOperativeStance::Crouching ? TEXT(", crouched") : TEXT(""));
		}
		float Closest = 10000.f;
		for (int32 A = 0; A < Places.Num(); ++A)
		{
			for (int32 B = A + 1; B < Places.Num(); ++B)
			{
				Closest = FMath::Min(Closest, static_cast<float>(FVector2D::Distance(Places[A], Places[B])));
			}
		}
		State.WorstGap = FMath::Max(State.WorstGap, Worst);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke info: %s:%s; closest pair %.0f cm, solo stand-off %.0f cm"), *Name, *Line, Closest, Solo);
		const bool bLayout = Squad->HasFormationWallOrder() && bAllPose;
		Check(State, bLayout, FString::Printf(TEXT("%s: the click on the wall -> all three in the formation wall layout, crouched facing out (after %.1f s)"), *Name,
			State.StageTime));
		Check(State, Worst <= MaxExtraGapCm && Nearest >= 25.f,
			FString::Printf(TEXT("%s: each man tight to the face - body centre <= the solo cover stand-off %.0f + %.0f cm (worst %.0f cm off, nearest %.0f)"), *Name, Solo,
				MaxExtraGapCm, Worst + Solo, Nearest));
		Check(State, !bFarSide, FString::Printf(TEXT("%s: nobody on the far side (behind the face%s)"), *Name,
			Scene.PassageWidthCm > 0.f ? TEXT(" or at the passage's other wall") : TEXT("")));
		Check(State, Closest >= MinSpacingCm, FString::Printf(TEXT("%s: not bunched up - the closest pair %.0f cm apart (>= %.0f)"), *Name, Closest, MinSpacingCm));
		State.Table.Add(FString::Printf(TEXT("| %s | wall layout %s | worst %.0f cm off the face (gap %+.0f vs solo %.0f) | closest pair %.0f cm | far side %s |"), *Name,
			bLayout ? TEXT("yes") : TEXT("NO"), Worst + Solo, Worst, Solo, Closest, bFarSide ? TEXT("YES") : TEXT("no")));
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 420.f)
		{
			Check(State, false, FString::Printf(TEXT("timeout (stage %d, scene %d)"), State.Stage, State.Scene));
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		ACodexTacticsPlayerController* PC = Cast<ACodexTacticsPlayerController>(UGameplayStatics::GetPlayerController(World, 0));
		AOperativeCharacter* Op = Member(State, 0);
		const FSceneWall& Scene = Wall(State.Scene % WallKinds);
		if (State.Stage > 0)
		{
			ParkEnemies(World); // the yard's patrols and the fight's waves never disturb the squad
		}
		switch (State.Stage)
		{
		case 0:
		{
			if (State.StageTime < 4.f || !Squad || !PC || !Flow || Squad->GetMembers().Num() < 3)
			{
				return State.Time < 120.f || Finish(State, false);
			}
			if (!FormationRuntime::IsEnabled())
			{
				Check(State, false, TEXT("Codex.Formation.Sectors is on (the formation wall layout)"));
				return Finish(State, false);
			}
			if (UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>())
			{
				Tutorial->SkipTutorial();
			}
			if (USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>())
			{
				Saves->bCheckpointsEnabled = false;
			}
			TArray<AOperativeCharacter*> Roster = Squad->GetMembers();
			Roster.Sort([](const AOperativeCharacter& X, const AOperativeCharacter& Y) { return X.SquadIndex < Y.SquadIndex; });
			for (int32 Index = 0; Index < 3; ++Index)
			{
				State.Members.Add(Roster[Index]);
			}
			// The yard's ground and its walls where the user laid them (the scene table): a trace onto each clicked face.
			FHitResult Ground;
			const FVector Probe(30500.f, -2050.f, 500.f);
			if (!World->LineTraceSingleByChannel(Ground, Probe, Probe - FVector(0.f, 0.f, 1000.f), ECC_Visibility, IgnoreSquad(State)))
			{
				Check(State, false, TEXT("the lake yard's ground (is this L_Demo01_A_FrozenLake?)"));
				return Finish(State, false);
			}
			State.GroundZ = static_cast<float>(Ground.ImpactPoint.Z);
			bool bWalls = true;
			for (int32 Kind = 0; Kind < 2; ++Kind)
			{
				FHitResult Face;
				bWalls &= TraceFace(World, State, Wall(Kind), Face) && FVector2D::Distance(FVector2D(Face.ImpactPoint), Wall(Kind).Face) < 15.0;
			}
			FHitResult Far;
			const FSceneWall& Passage = Wall(1);
			const FVector FarFrom = At(State, Passage.Face + Passage.Normal * 100.0, 120.f);
			bWalls &= World->LineTraceSingleByChannel(Far, FarFrom, FarFrom + FVector(Passage.Normal, 0.0) * 400.f, ECC_Visibility, IgnoreSquad(State))
				&& FMath::Abs(FVector2D::DotProduct(FVector2D(Far.ImpactPoint) - Passage.Face, Passage.Normal) - Passage.PassageWidthCm) < 15.0;
			Check(State, bWalls, FString::Printf(TEXT("the yard as laid out: the base building's west / east faces, the compound wall 3.6 m off the east face (ground z %.0f)"),
				State.GroundZ));
			if (!bWalls)
			{
				return Finish(State, false);
			}
			SpawnLowWall(World, State);
			Next(State, 1);
			return true;
		}
		case 1:
		{
			// A scene begins: the squad (Solo while placed) at its start.
			if (State.StageTime < 2.5f)
			{
				return true; // the navmesh rebuilds round the low wall / the last scene's squad settles
			}
			ResetSquad(World, Squad, State, Scene);
			Next(State, 2);
			return true;
		}
		case 2:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			Squad->ExitSoloMode(false, 0.f);
			Op->OrderMoveTo(At(State, Scene.WalkTo), false);
			Next(State, 3);
			return true;
		}
		case 3:
		{
			// The leader stopped, the formation came up behind him; then the player's two clicks on the wall (the ghost, its confirm).
			if ((Op->IsMoving() || State.StageTime < 4.f) && State.StageTime < 15.f)
			{
				return true;
			}
			const TArray<AOperativeCharacter*> Joining = Squad->GetGroupCoverMembers(Op);
			Check(State, !Squad->IsSoloMode() && !Squad->HasMultiSelection() && Squad->IsGroupCoverFormation() && Joining.Num() == 2,
				FString::Printf(TEXT("%s: group mode (%s), the two followers go along (%d)"), *SceneName(State.Scene),
					Flow->GetPhase() == ECodexGamePhase::WaveCombat ? TEXT("the wave fight in real time") : TEXT("exploration"), Joining.Num()));
			FHitResult Click;
			if (!TraceFace(World, State, Scene, Click))
			{
				Check(State, false, FString::Printf(TEXT("%s: the cursor on the wall"), *SceneName(State.Scene)));
				return Finish(State, false);
			}
			PC->HandleWorldHit(Click);
			Next(State, 4);
			return true;
		}
		case 4:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			FHitResult Click;
			TraceFace(World, State, Scene, Click);
			PC->HandleWorldHit(Click);
			Check(State, Squad->HasFormationWallOrder(), FString::Printf(TEXT("%s: the clicks on the wall -> the group cover order as the formation wall layout"),
				*SceneName(State.Scene)));
			Next(State, 5);
			return true;
		}
		case 5:
		{
			bool bAll = true;
			for (int32 Index = 0; Index < 3; ++Index)
			{
				const AOperativeCharacter* Each = Member(State, Index);
				bAll &= Each && Each->IsInFormationWallPose() && Each->GetVelocity().Size2D() < 5.f;
			}
			if (!bAll)
			{
				State.StableSince = -1.f;
				if (State.StageTime < SceneCapSeconds)
				{
					return true;
				}
			}
			else if (State.StableSince < 0.f)
			{
				State.StableSince = State.StageTime;
			}
			if (bAll && State.StageTime - State.StableSince < SettleHoldSeconds)
			{
				return true;
			}
			JudgeScene(State, Squad, State.Scene);
			if (State.bShot && State.Scene == 1 && !State.bShotTaken)
			{
				TopDownShot(World, State, Scene, false);
				Next(State, 6);
				return true;
			}
			Next(State, 7);
			return true;
		}
		case 6:
		{
			if (State.StageTime >= 1.f && !State.bShotTaken)
			{
				TopDownShot(World, State, Scene, true); // the camera cut's motion has settled
			}
			if (State.StageTime < 2.f)
			{
				return true; // the screenshot is written
			}
			if (APlayerController* Player = UGameplayStatics::GetPlayerController(World, 0))
			{
				Player->SetViewTarget(Player->GetPawn());
			}
			Next(State, 7);
			return true;
		}
		case 7:
		{
			// The next scene; the fight starts once before the fight scenes (it then goes on in real time).
			++State.Scene;
			if (State.Scene >= SceneCount)
			{
				Check(State, State.WorstGap <= MaxExtraGapCm, FString::Printf(TEXT("over all scenes: the worst gap off the face %+.0f cm vs the solo cover stand-off"),
					State.WorstGap));
				return Finish(State, true);
			}
			if (IsFightScene(State.Scene) && Flow->GetPhase() != ECodexGamePhase::WaveCombat)
			{
				Squad->EnterSoloMode();
				SmokeUtils::StartWaveFight(Flow);
				ParkEnemies(World);
			}
			if (Flow->GetCombatMode() == ECodexCombatMode::TacticalPause)
			{
				Flow->ToggleTacticalPause();
			}
			Check(State, !IsFightScene(State.Scene) || (Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetCombatMode() == ECodexCombatMode::RealTime),
				FString::Printf(TEXT("%s: the phase is right (%s)"), *SceneName(State.Scene),
					Flow->GetPhase() == ECodexGamePhase::WaveCombat ? TEXT("the wave fight") : TEXT("exploration")));
			Next(State, 1);
			return true;
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
		TWeakObjectPtr<UWorld> WeakWorld(World);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.FormationWallHugSmoke"),
		TEXT("Dev check on the lake yard (the squad hugging a wall, bug 2026-10-27): the group cover click at a plain wall, in the compound passage and in a 2 m passage, exploration and the real-time fight - all in the wall layout, tight to the face like one operative in cover, nobody beyond it; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
