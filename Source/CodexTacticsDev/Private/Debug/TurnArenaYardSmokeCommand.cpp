// Dev-only check of the game designer's decision 2026-10-26 (screenshot: the snowy yard of the Demo 01 lake's last compound - barrels,
// the generator shed, the PMC base building; objective "EXIT: take the squad out through the blast door"; turn-based combat was
// refused there): "if an 8 x 8 or 8 x 4 grid fits, turn-based combat can start, excluding buildings, ladders and the rest ... compute
// the building's perimeter and subtract it from the grid, so there are no buildings you could enter during the fight".
//   Scripts/smoke_parallel.ps1 -Commands TurnArenaYardSmoke   (map /Game/Maps/L_Demo01_A_FrozenLake + -LevelJson, Scripts/test_map.json)
//   Image: UnrealEditor-Cmd ... -RenderOffscreen -ExecCmds="CodexTactics.TurnArenaYardSmoke Shot" -> Saved/Screenshots/TurnBased/arena_buildings.png
//  1. the leader in the yard 6 m in front of the base building's door, a mate behind him, a mate INSIDE the building by its door, a hound
//     in the yard and a hound deep inside the building: the grid round the leader is valid (no shift);
//  2. turn-based: the grid is the leader's; every cell inside the building's outline (+ its walls), its door and every roofed cell is
//     closed; no grid unit stands inside (the mate was moved out through the door, the hound deep inside stays out of the fight);
//     ladders' cells are closed; the open area holds 8 x 4 cells;
//  3. a move order into the building is refused (no AP spent, he stays), a move order onto an open cell is carried out.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/CameraZoneVolume.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Core/SaveGameSubsystem.h"
#include "Debug/SmokeUtils.h"
#include "Debug/TurnArenaSmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/InteractableActor.h"
#include "Ladders/LadderActor.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "Tactics/GorkyGridManager.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Tutorial/TutorialSubsystem.h"
#include "UnrealClient.h"

namespace TurnArenaYardSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		bool bShot = false;
		TWeakObjectPtr<ACameraZoneVolume> Base;
		FVector Spot = FVector::ZeroVector;
		FVector Outward = FVector::ZeroVector;
		FVector Door = FVector::ZeroVector;
		TWeakObjectPtr<AOperativeCharacter> Indoors;
		FVector IndoorSpot = FVector::ZeroVector;
		FVector DeepSpot = FVector::ZeroVector;
		TWeakObjectPtr<AEnemyCharacter> YardHound;
		TWeakObjectPtr<AEnemyCharacter> DeepHound;
		FIntPoint FromCell = FIntPoint(-1, -1);
		FIntPoint OpenTarget = FIntPoint(-1, -1);
		int32 APBefore = 0;
		TWeakObjectPtr<ACameraActor> ShotCamera;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("TurnArenaYardSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	/** A hound that survives the squad's fire until the grid is laid. */
	AEnemyCharacter* SpawnHound(UWaveSubsystem& Waves, const FVector& At)
	{
		AEnemyCharacter* Hound = Waves.SpawnEnemy(EEnemyArchetype::FrostHound, At + FVector(0.f, 0.f, 20.f), FRotator::ZeroRotator);
		if (Hound && Hound->GetHealthComponent())
		{
			Hound->GetHealthComponent()->SetMaxHealth(100000.f, false);
			Hound->GetHealthComponent()->Heal(100000.f);
		}
		return Hound;
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 300.f)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("Smoke stopped in stage %d"), State.Stage);
			return World ? Finish(State, false) : false;
		}
		USquadSubsystem* Squad = World->GetSubsystem<USquadSubsystem>();
		UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
		UTurnBasedCombatSubsystem* TurnBased = World->GetSubsystem<UTurnBasedCombatSubsystem>();
		UWaveSubsystem* Waves = World->GetSubsystem<UWaveSubsystem>();
		switch (State.Stage)
		{
		case 0:
		{
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			FNavLocation OnNav;
			if (!Squad || !Flow || !TurnBased || !Waves || Squad->GetMembers().Num() < 3 || !Squad->GetLeader()
				|| !(Nav && Nav->ProjectPointToNavigation(Squad->GetLeader()->GetActorLocation(), OnNav, FVector(60.f, 60.f, 200.f))) || State.StageTime < 4.f)
			{
				return State.Time < 120.f || Finish(State, false);
			}
			if (USaveGameSubsystem* Saves = World->GetSubsystem<USaveGameSubsystem>())
			{
				Saves->bCheckpointsEnabled = false;
			}
			if (IConsoleVariable* Debug = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.TurnArena.Debug")))
			{
				Debug->Set(1);
			}
			if (UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>())
			{
				Tutorial->SkipTutorial();
			}
			const AInteractableActor* Generator = TurnArenaSmokeUtils::FindGenerator(World);
			ACameraZoneVolume* Base = Generator ? TurnArenaSmokeUtils::FindBaseZone(World, Generator->GetActorLocation()) : nullptr;
			State.Base = Base;
			Check(State, Base != nullptr, TEXT("the yard's base building (its indoor camera zone next to the generator)"));
			if (!Base)
			{
				return Finish(State, false);
			}
			// The door side first (layout: the -X face), else any face with open ground 6 m out.
			const FTransform Zone = Base->GetZoneTransform();
			const FVector Extent = Base->GetZoneHalfExtent();
			const FCollisionQueryParams Params = TurnArenaSmokeUtils::OpenGroundParams(World);
			const float RefZ = static_cast<float>(Zone.GetLocation().Z - Extent.Z);
			bool bFound = false;
			for (const FVector& Axis : { FVector(-1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), FVector(0.f, -1.f, 0.f), FVector(1.f, 0.f, 0.f) })
			{
				const FVector Out = Zone.TransformVectorNoScale(Axis);
				const float Half = static_cast<float>(FMath::Abs(Axis.X) * Extent.X + FMath::Abs(Axis.Y) * Extent.Y);
				const FVector Probe = Zone.GetLocation() + Out * (Half + 600.f);
				float Z = 0.f;
				if (TurnArenaSmokeUtils::GroundAt(World, Probe, RefZ, Params, Z) && !TurnArenaSmokeUtils::IsRoofed(World, Probe, Z, Params)
					&& Nav->ProjectPointToNavigation(FVector(Probe.X, Probe.Y, Z), OnNav, FVector(60.f, 60.f, 150.f)))
				{
					State.Outward = Out;
					State.Door = Zone.GetLocation() + Out * (Half + 40.f);
					State.Spot = TurnArenaSmokeUtils::OpenSpot(World, FVector(Probe.X, Probe.Y, Z), nullptr);
					bFound = true;
					break;
				}
			}
			Check(State, bFound, FString::Printf(TEXT("open yard 6 m in front of the building (outward %s, door side %s)"), *State.Outward.ToCompactString(),
				*State.Door.ToCompactString()));
			if (!bFound)
			{
				return Finish(State, false);
			}
			// The leader on the spot, a mate behind him, a mate INSIDE by the door.
			AOperativeCharacter* Leader = Squad->GetLeader();
			const float Yaw = static_cast<float>((-State.Outward).Rotation().Yaw);
			TurnArenaSmokeUtils::PlaceAt(Leader, State.Spot, Yaw);
			int32 Slot = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				if (Member == Leader)
				{
					continue;
				}
				if (Slot++ == 0)
				{
					TurnArenaSmokeUtils::PlaceAt(Member, TurnArenaSmokeUtils::OpenSpot(World, State.Spot + State.Outward * 200.f, Member), Yaw);
				}
				else
				{
					// 1.6 m in from the wall line at the door: inside the building, by its door.
					const FVector Inside = Zone.GetLocation() + State.Outward * (FVector::DotProduct(State.Door - Zone.GetLocation(), State.Outward) - 160.f);
					State.IndoorSpot = TurnArenaSmokeUtils::OpenSpot(World, FVector(Inside.X, Inside.Y, State.Spot.Z - 92.f), Member, 80.f);
					TurnArenaSmokeUtils::PlaceAt(Member, State.IndoorSpot, Yaw);
					State.Indoors = Member;
				}
			}
			Next(State);
			return true;
		}
		case 1:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			ACameraZoneVolume* Base = State.Base.Get();
			FTurnArenaChoice Arena;
			const bool bFound = TurnBased->FindTurnArena(Arena);
			Check(State, bFound && Arena.Offset == FIntPoint::ZeroValue && Arena.LeaderFault == ETurnArenaFault::None,
				FString::Printf(TEXT("the yard by the building: the grid round the leader is valid (%s, offset (%d, %d), arena %d cells, rect %dx%d, %d building outlines)"),
					TurnArenaRules::FaultName(Arena.LeaderFault), Arena.Offset.X, Arena.Offset.Y, Arena.Verdict.ArenaCells, Arena.Verdict.RectSize.X,
					Arena.Verdict.RectSize.Y, Arena.Footprints.Num()));
			SmokeUtils::StartWaveFight(Flow);
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
				It->SetActorTickEnabled(false);
			}
			State.YardHound = SpawnHound(*Waves, TurnArenaSmokeUtils::OpenSpot(World, State.Spot + State.Outward * 700.f, nullptr) - FVector(0.f, 0.f, 72.f));
			State.DeepHound = SpawnHound(*Waves, Base->GetZoneTransform().GetLocation() - FVector(0.f, 0.f, Base->GetZoneHalfExtent().Z - 20.f));
			State.DeepSpot = State.DeepHound.IsValid() ? State.DeepHound->GetActorLocation() : FVector::ZeroVector;
			Check(State, State.YardHound.IsValid() && State.DeepHound.IsValid() && Flow->GetCombatMode() == ECodexCombatMode::RealTime,
				TEXT("the fight runs in real time: a hound in the yard, a hound deep inside the building"));
			Next(State);
			return true;
		}
		case 2:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			// The mate inside again (the squad's follow may have walked him to the door meanwhile), the fight at once.
			AOperativeCharacter* Indoors = State.Indoors.Get();
			if (Indoors)
			{
				TurnArenaSmokeUtils::PlaceAt(Indoors, State.IndoorSpot, static_cast<float>((-State.Outward).Rotation().Yaw));
			}
			Check(State, Indoors && State.Base.IsValid() && TurnArenaSmokeUtils::InsideZone(State.Base.Get(), Indoors->GetActorLocation(), 0.f),
				FString::Printf(TEXT("%s stands INSIDE the building by its door (%s) when the fight goes turn-based"), Indoors ? *Indoors->DisplayName.ToString() : TEXT("-"),
					Indoors ? *Indoors->GetActorLocation().ToCompactString() : TEXT("-")));
			// The hound deep inside: a live hound in a real-time fight runs at the squad at once (since the horde warning no longer
			// pauses the fight, 2026-10-26 gameflow) - 0.5 s later it may stand in the door. Log where it got to, then put it back deep
			// inside and stop it: the check is about where it stands when the fight goes turn-based.
			if (AEnemyCharacter* Deep = State.DeepHound.Get())
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[TurnArenaYard] the deep hound moved %.0f cm in %.1f s of real-time fight (%s -> %s, speed %.0f cm/s, inside the building: %d)"),
					FVector::Dist2D(Deep->GetActorLocation(), State.DeepSpot), State.StageTime, *State.DeepSpot.ToCompactString(),
					*Deep->GetActorLocation().ToCompactString(), Deep->GetVelocity().Size2D(),
					State.Base.IsValid() && TurnArenaSmokeUtils::InsideZone(State.Base.Get(), Deep->GetActorLocation(), 0.f) ? 1 : 0);
				if (AController* Controller = Deep->GetController())
				{
					Controller->StopMovement();
				}
				Deep->SetActorLocation(State.DeepSpot, false, nullptr, ETeleportType::TeleportPhysics);
			}
			Check(State, State.DeepHound.IsValid() && State.Base.IsValid() && TurnArenaSmokeUtils::InsideZone(State.Base.Get(), State.DeepHound->GetActorLocation(), -200.f),
				TEXT("the second hound stands deep inside the building (2 m+ from its walls) when the fight goes turn-based"));
			const FVector LeaderAt = Squad->GetLeader()->GetActorLocation();
			const EGameFlowResult Result = Flow->RequestEnterTurnBased(true);
			Check(State, Result == EGameFlowResult::Ok && TurnBased->IsActive(), TEXT("turn-based combat entered in the yard"));
			const UGorkyGridManager* Grid = TurnBased->GetGrid();
			ACameraZoneVolume* Base = State.Base.Get();
			if (!TurnBased->IsActive() || !Grid || !Base)
			{
				return Finish(State, false);
			}
			const FTurnArenaChoice& Arena = TurnBased->GetActiveArena();
			Check(State, Arena.bFound && FVector::Dist2D(Grid->GridToWorld(FIntPoint(7, 7)) - FVector(75.f, 75.f, 0.f), LeaderAt) < 5.f,
				TEXT("the grid is laid round the leader (as before 2026-10-22)"));
			const FCollisionQueryParams Params = TurnArenaSmokeUtils::OpenGroundParams(World);
			int32 InOutline = 0;
			int32 InOutlineOpen = 0;
			int32 DoorCells = 0;
			int32 DoorOpen = 0;
			int32 RoofedOpen = 0;
			int32 Closed = 0;
			for (int32 X = 0; X < Grid->GridSize.X; ++X)
			{
				for (int32 Y = 0; Y < Grid->GridSize.Y; ++Y)
				{
					const FIntPoint Cell(X, Y);
					const FVector Centre = Grid->GridToWorld(Cell);
					const bool bOpen = Grid->IsCellWalkable(Cell);
					Closed += Grid->IsCellClosed(Cell) ? 1 : 0;
					if (TurnArenaSmokeUtils::InsideZone(Base, Centre, 40.f))
					{
						++InOutline;
						InOutlineOpen += bOpen ? 1 : 0;
					}
					if (FVector::Dist2D(Centre, State.Door) < 80.f || Cell == Grid->WorldToGrid(State.Door))
					{
						++DoorCells;
						DoorOpen += Grid->IsCellClosed(Cell) ? 0 : 1;
					}
					float Z = 0.f;
					if (bOpen && TurnArenaSmokeUtils::GroundAt(World, Centre, Arena.GroundZ, Params, Z) && TurnArenaSmokeUtils::IsRoofed(World, Centre, Z, Params))
					{
						++RoofedOpen;
					}
				}
			}
			Check(State, InOutline >= 6 && InOutlineOpen == 0, FString::Printf(TEXT("every cell inside the building's outline is closed (%d cells, %d open)"), InOutline, InOutlineOpen));
			Check(State, DoorCells >= 1 && DoorOpen == 0, FString::Printf(TEXT("its door is closed (%d cells by the door, %d open)"), DoorCells, DoorOpen));
			Check(State, RoofedOpen == 0, FString::Printf(TEXT("no open cell under a roof (%d)"), RoofedOpen));
			Check(State, Arena.Verdict.RectSize.X * Arena.Verdict.RectSize.Y >= 32 && Arena.Verdict.ArenaCells >= 32,
				FString::Printf(TEXT("the open area holds %dx%d (arena %d cells, %d closed)"), Arena.Verdict.RectSize.X, Arena.Verdict.RectSize.Y,
					Arena.Verdict.ArenaCells, Closed));
			// Nobody on the grid inside a building.
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				const FTurnUnitState* Unit = TurnBased->GetUnitState(Member);
				Check(State, Unit && TurnBased->GetArenaCellClass(Unit->GridPos) == ETurnArenaCell::Walkable && !TurnArenaSmokeUtils::InsideZone(Base, Member->GetActorLocation(), 0.f),
					FString::Printf(TEXT("%s%s on open cell (%d, %d), outside the building"), *Member->DisplayName.ToString(),
						Member == State.Indoors.Get() ? TEXT(" (was inside, moved out)") : TEXT(""), Unit ? Unit->GridPos.X : -1, Unit ? Unit->GridPos.Y : -1));
			}
			const FTurnUnitState* Yard = TurnBased->GetUnitState(State.YardHound.Get());
			Check(State, Yard && TurnBased->GetArenaCellClass(Yard->GridPos) == ETurnArenaCell::Walkable, TEXT("the yard hound is on the grid on an open cell"));
			Check(State, State.DeepHound.IsValid() && !TurnBased->GetUnitState(State.DeepHound.Get()),
				TEXT("the hound deep inside the building is no grid unit (stasis, out of the fight)"));
			// Ladders: their cells closed.
			for (TActorIterator<ALadderActor> It(World); It; ++It)
			{
				const FVector Foot = It->GetBottomLinkWorld();
				const FVector Local = Foot - Grid->OriginWorld;
				if (Local.X >= 0.f && Local.Y >= 0.f && Local.X < Grid->GridSize.X * Grid->CellSize && Local.Y < Grid->GridSize.Y * Grid->CellSize)
				{
					Check(State, Grid->IsCellClosed(Grid->WorldToGrid(Foot)), FString::Printf(TEXT("ladder %s: its foot cell is closed"), *It->GetName()));
				}
			}
			// 3. A move order into the building: the leader is the active unit.
			AOperativeCharacter* Leader = Squad->GetLeader();
			TurnBased->SelectUnit(Leader);
			const FTurnUnitState* LeaderUnit = TurnBased->GetUnitState(Leader);
			FIntPoint Target(-1, -1);
			int32 Best = TNumericLimits<int32>::Max();
			for (int32 X = 0; X < Grid->GridSize.X && LeaderUnit; ++X)
			{
				for (int32 Y = 0; Y < Grid->GridSize.Y; ++Y)
				{
					const FIntPoint Cell(X, Y);
					const int32 Distance = (Cell - LeaderUnit->GridPos).SizeSquared();
					if (Grid->IsCellClosed(Cell) && TurnArenaSmokeUtils::InsideZone(Base, Grid->GridToWorld(Cell), 0.f) && Distance < Best)
					{
						Best = Distance;
						Target = Cell;
					}
				}
			}
			Check(State, LeaderUnit && TurnBased->GetActiveUnit() == Leader && Target.X >= 0,
				FString::Printf(TEXT("the leader is active, a cell inside the building %d cells away: (%d, %d)"), FMath::RoundToInt(FMath::Sqrt(static_cast<float>(Best))), Target.X, Target.Y));
			if (!LeaderUnit || Target.X < 0)
			{
				return Finish(State, false);
			}
			State.FromCell = LeaderUnit->GridPos;
			State.APBefore = LeaderUnit->AP;
			TurnBased->HandleWorldClick(Grid->GridToWorld(Target) + FVector(0.f, 0.f, 5.f), nullptr, false);
			Check(State, !TurnBased->MoveActiveUnitTo(Target), TEXT("a move order into the building is refused"));
			Next(State);
			return true;
		}
		case 3:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			const FTurnUnitState* Unit = TurnBased->GetUnitState(Squad->GetLeader());
			Check(State, Unit && Unit->GridPos == State.FromCell && Unit->AP == State.APBefore && !TurnBased->IsBusy(),
				FString::Printf(TEXT("after the click into the building he stays on (%d, %d) with %d AP"), Unit ? Unit->GridPos.X : -1, Unit ? Unit->GridPos.Y : -1, Unit ? Unit->AP : -1));
			// The control: an open cell 2 steps away is a valid order.
			const UGorkyGridManager* Grid = TurnBased->GetGrid();
			for (const TPair<FIntPoint, int32>& Entry : Grid->GetReachableCells(State.FromCell, 2))
			{
				if (Entry.Value == 2 && Grid->IsCellWalkable(Entry.Key) && !Grid->IsCellOccupied(Entry.Key)
					&& TurnBased->GetArenaCellClass(Entry.Key) == ETurnArenaCell::Walkable)
				{
					State.OpenTarget = Entry.Key;
					break;
				}
			}
			Check(State, State.OpenTarget.X >= 0 && TurnBased->MoveActiveUnitTo(State.OpenTarget),
				FString::Printf(TEXT("a move order onto open cell (%d, %d) is carried out"), State.OpenTarget.X, State.OpenTarget.Y));
			Next(State);
			return true;
		}
		case 4:
		{
			const FTurnUnitState* Unit = TurnBased->GetUnitState(Squad->GetLeader());
			if ((TurnBased->IsBusy() || !Unit || Unit->GridPos != State.OpenTarget) && State.StageTime < 8.f)
			{
				return true;
			}
			Check(State, Unit && Unit->GridPos == State.OpenTarget, TEXT("... he walked onto it"));
			if (!State.bShot)
			{
				return Finish(State, true);
			}
			// The image: the grid with the subtracted building, a 3/4 view from the yard side.
			APlayerController* PC = World->GetFirstPlayerController();
			const UGorkyGridManager* Grid = TurnBased->GetGrid();
			const FVector Centre = Grid->GridToWorld(FIntPoint(7, 7)) - FVector(75.f, 75.f, 0.f);
			ACameraActor* Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity);
			if (PC && Camera)
			{
				const FVector Eye = Centre + State.Outward * 1500.f + FVector(0.f, 0.f, 2300.f) + FVector::CrossProduct(FVector::UpVector, State.Outward) * 500.f;
				Camera->SetActorLocationAndRotation(Eye, (Centre - State.Outward * 200.f - Eye).Rotation());
				Camera->GetCameraComponent()->SetFieldOfView(70.f);
				Camera->GetCameraComponent()->bConstrainAspectRatio = false;
				PC->SetViewTarget(Camera);
				State.ShotCamera = Camera;
			}
			Next(State);
			return true;
		}
		case 5:
		{
			if (State.StageTime < 2.f)
			{
				return true;
			}
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Screenshots/TurnBased/arena_buildings.png"));
			FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ false, /*bAddFilenameSuffix*/ false);
			UE_LOG(LogCodexTactics, Display, TEXT("[TurnArenaYard] screenshot -> %s"), *Path);
			Next(State);
			return true;
		}
		case 6:
			return State.StageTime < 2.f || Finish(State, true);
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
		State->bShot = Args.ContainsByPredicate([](const FString& Arg) { return Arg.Equals(TEXT("Shot"), ESearchCase::IgnoreCase); }) && FApp::CanEverRender();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.TurnArenaYardSmoke"),
		TEXT("Dev check (game designer 2026-10-26): turn-based combat starts in the lake compound's yard, the base building's outline and door subtracted, nobody inside, a move into it refused; [Shot] saves Saved/Screenshots/TurnBased/arena_buildings.png. Logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
