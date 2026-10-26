// Dev-only check of the turn-based arena in the Demo 01 lake compound (TurnArenaRules, UTurnBasedCombatSubsystem::FindTurnArena).
// History: game designer report 2026-10-22 (the grid laid INSIDE the base building, the squad stuck outside) -> commit 40782ae made the
// grid need open, level ground everywhere in its middle; game designer decision 2026-10-26: too harsh - the grid round the leader as
// before, buildings (and ladders) subtracted, an 8 x 8 / 8 x 4 open area required.
//   Scripts/smoke_parallel.ps1 -Commands TurnArenaLakeSmoke   (map /Game/Maps/L_Demo01_A_FrozenLake + -LevelJson, Scripts/test_map.json)
//  1. the squad in the yard right outside the generator shed (the shed and the base building within the 21 m grid round the leader):
//     the grid round the leader is valid (no shift);
//  2. the fight (wave 1, its lane enemies parked away) + a hound 8 m off -> turn-based: the grid is the leader's; EVERY walkable cell is
//     open ground (no roof overhead, nothing solid in it, on the navmesh, level with the arena); no cell inside the shed is walkable;
//     every operative stands on a walkable cell on the ground and can move;
//  3. the squad INSIDE the (roofless) shed: the fight starts, everyone is moved out of the shed onto the arena;
//  4. the squad deep inside the base building: no arena -> the Space-hold entry is refused with the "only outside" line.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

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
#include "HAL/IConsoleManager.h"
#include "Interactables/InteractableActor.h"
#include "NavigationSystem.h"
#include "Tactics/GorkyGridManager.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Tutorial/TutorialSubsystem.h"

namespace TurnArenaLakeSmoke
{
	constexpr float StepSeconds = 0.1f;

	struct FState
	{
		float Time = 0.f;
		float StageTime = 0.f;
		int32 Stage = 0;
		int32 Failures = 0;
		TWeakObjectPtr<AInteractableActor> Generator;
		FBox2D Shed = FBox2D(ForceInit);
		FVector Outside = FVector::ZeroVector;
		TWeakObjectPtr<AEnemyCharacter> Hound;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	bool Finish(const FState& State, bool bComplete)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), State.Failures == 0 && bComplete ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("TurnArenaLakeSmoke"));
		return false;
	}

	void Next(FState& State)
	{
		++State.Stage;
		State.StageTime = 0.f;
	}

	/** The squad round Spot (the leader on it), stopped. */
	void PlaceSquad(UWorld* World, USquadSubsystem& Squad, const FVector& Spot, const FVector& Back, float MaxRadius = 400.f)
	{
		int32 Slot = 0;
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			const FVector Offset = Member == Squad.GetLeader() ? FVector::ZeroVector
				: Back * 180.f + FVector::CrossProduct(FVector::UpVector, Back) * (Slot++ == 0 ? -150.f : 150.f);
			TurnArenaSmokeUtils::PlaceAt(Member, TurnArenaSmokeUtils::OpenSpot(World, Spot + Offset, Member, MaxRadius), (-Back).Rotation().Yaw);
		}
	}

	/** Every operative's grid unit: on a walkable cell, outside the shed. */
	void CheckSquadOutsideShed(FState& State, UTurnBasedCombatSubsystem& TurnBased, USquadSubsystem& Squad, const TCHAR* When)
	{
		const UGorkyGridManager* Grid = TurnBased.GetGrid();
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			const FTurnUnitState* Unit = TurnBased.GetUnitState(Member);
			const FVector2D At(Member->GetActorLocation());
			const bool bInShed = State.Shed.bIsValid && State.Shed.ExpandBy(-20.f).IsInside(At);
			Check(State, Unit && Grid && TurnBased.GetArenaCellClass(Unit->GridPos) == ETurnArenaCell::Walkable && !bInShed,
				FString::Printf(TEXT("%s: %s on cell (%d, %d) (%s), %s the shed"), When, *Member->DisplayName.ToString(), Unit ? Unit->GridPos.X : -1,
					Unit ? Unit->GridPos.Y : -1, Unit ? TEXT("a grid unit") : TEXT("no unit"), bInShed ? TEXT("INSIDE") : TEXT("outside")));
		}
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
				Debug->Set(1); // the cell map of every search in the log
			}
			if (UTutorialSubsystem* Tutorial = World->GetSubsystem<UTutorialSubsystem>())
			{
				Tutorial->SkipTutorial();
			}
			AInteractableActor* Generator = TurnArenaSmokeUtils::FindGenerator(World);
			State.Generator = Generator;
			Check(State, Generator != nullptr, TEXT("the compound's generator (in its shed)"));
			if (!Generator)
			{
				return Finish(State, false);
			}
			State.Shed = TurnArenaSmokeUtils::FindShedBox(World, Generator);
			Check(State, State.Shed.bIsValid && State.Shed.GetSize().X > 200.f && State.Shed.GetSize().Y > 200.f,
				FString::Printf(TEXT("the shed's walls: %s"), *State.Shed.ToString()));
			// Out of the shed door (south, -Y): the first open, navigable spot, then 2.5 m further into the yard.
			const FCollisionQueryParams Params = TurnArenaSmokeUtils::OpenGroundParams(World);
			const float RefZ = static_cast<float>(Generator->GetActorLocation().Z) - 50.f;
			FVector Spot = Generator->GetActorLocation();
			for (float Step = 100.f; Step <= 1500.f; Step += 50.f)
			{
				const FVector Probe = Generator->GetActorLocation() - FVector(0.f, Step, 0.f);
				float Z = 0.f;
				if (TurnArenaSmokeUtils::GroundAt(World, Probe, RefZ, Params, Z) && !TurnArenaSmokeUtils::IsRoofed(World, Probe, Z, Params)
					&& Nav->ProjectPointToNavigation(FVector(Probe.X, Probe.Y, Z), OnNav, FVector(40.f, 40.f, 120.f)) && !State.Shed.IsInside(FVector2D(Probe)))
				{
					Spot = FVector(Probe.X, Probe.Y - 250.f, Z);
					break;
				}
			}
			State.Outside = Spot;
			UE_LOG(LogCodexTactics, Display, TEXT("[TurnArenaLake] generator %s, the yard spot outside the shed %s (%.1f m south)"),
				*Generator->GetActorLocation().ToCompactString(), *Spot.ToCompactString(), (Generator->GetActorLocation().Y - Spot.Y) / 100.f);
			PlaceSquad(World, *Squad, Spot, FVector(0.f, -1.f, 0.f)); // the leader by the shed, the mates behind him in the yard
			Next(State);
			return true;
		}
		case 1:
		{
			if (State.StageTime < 1.f)
			{
				return true;
			}
			FTurnArenaChoice Arena;
			const bool bFound = TurnBased->FindTurnArena(Arena);
			Check(State, bFound && Arena.LeaderFault == ETurnArenaFault::None && Arena.Offset == FIntPoint::ZeroValue,
				FString::Printf(TEXT("the grid round the leader by the shed is valid as it is (as before 2026-10-22): %s, offset (%d, %d), arena %d cells, %d building outlines"),
					TurnArenaRules::FaultName(Arena.LeaderFault), Arena.Offset.X, Arena.Offset.Y, Arena.Verdict.ArenaCells, Arena.Footprints.Num()));
			// The fight: wave 1 on the lake lanes (parked far away), a hound 8 m out in the yard.
			SmokeUtils::StartWaveFight(Flow);
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
				It->SetActorTickEnabled(false);
			}
			State.Hound = Waves->SpawnEnemy(EEnemyArchetype::FrostHound, TurnArenaSmokeUtils::OpenSpot(World, State.Outside - FVector(0.f, 800.f, 0.f), nullptr)
				+ FVector(0.f, 0.f, 20.f), FRotator(0.f, 90.f, 0.f));
			if (AEnemyCharacter* Hound = State.Hound.Get(); Hound && Hound->GetHealthComponent())
			{
				Hound->GetHealthComponent()->SetMaxHealth(100000.f, false); // the squad's fire must not end the fight before the grid
				Hound->GetHealthComponent()->Heal(100000.f);
			}
			Check(State, State.Hound.IsValid() && Flow->GetPhase() == ECodexGamePhase::WaveCombat && Flow->GetCombatMode() == ECodexCombatMode::RealTime,
				TEXT("the fight runs in real time, a hound 8 m out in the yard"));
			Next(State);
			return true;
		}
		case 2:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			const FVector LeaderAt = Squad->GetLeader()->GetActorLocation();
			const EGameFlowResult Result = Flow->RequestEnterTurnBased(true);
			Check(State, Result == EGameFlowResult::Ok && TurnBased->IsActive() && TurnBased->GetEnemyCount() >= 1, FString::Printf(TEXT("turn-based combat entered (%d enemies on the grid; the hound %.1f m from the leader)"),
				TurnBased->GetEnemyCount(), State.Hound.IsValid() ? FVector::Dist2D(State.Hound->GetActorLocation(), LeaderAt) / 100.f : -1.f));
			if (!TurnBased->IsActive() || !TurnBased->GetGrid())
			{
				return Finish(State, false);
			}
			const FTurnArenaChoice& Arena = TurnBased->GetActiveArena();
			const UGorkyGridManager* Grid = TurnBased->GetGrid();
			Check(State, Arena.bFound && FVector::Dist2D(Grid->GridToWorld(FIntPoint(7, 7)) - FVector(75.f, 75.f, 0.f), LeaderAt) < 5.f,
				FString::Printf(TEXT("the grid is laid round the leader (centre %s)"), *Arena.Centre.ToCompactString()));
			const FCollisionQueryParams Params = TurnArenaSmokeUtils::OpenGroundParams(World);
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			int32 Walkable = 0;
			int32 Roofed = 0;
			int32 Solid = 0;
			int32 OffNav = 0;
			int32 Uneven = 0;
			int32 ShedCells = 0;
			int32 ShedOpen = 0;
			const FCollisionShape Box = FCollisionShape::MakeBox(FVector(150.f * 0.35f, 150.f * 0.35f, 60.f));
			for (int32 X = 0; X < Grid->GridSize.X; ++X)
			{
				for (int32 Y = 0; Y < Grid->GridSize.Y; ++Y)
				{
					const FIntPoint Cell(X, Y);
					const FVector Centre = Grid->GridToWorld(Cell);
					if (State.Shed.bIsValid && State.Shed.ExpandBy(-30.f).IsInside(FVector2D(Centre)))
					{
						++ShedCells;
						ShedOpen += Grid->IsCellWalkable(Cell) ? 1 : 0;
					}
					if (!Grid->IsCellWalkable(Cell))
					{
						continue;
					}
					++Walkable;
					float Z = 0.f;
					if (!TurnArenaSmokeUtils::GroundAt(World, Centre, Arena.GroundZ, Params, Z))
					{
						++Uneven;
						continue;
					}
					Uneven += FMath::Abs(Z - Arena.GroundZ) > 105.f ? 1 : 0;
					Roofed += TurnArenaSmokeUtils::IsRoofed(World, Centre, Z, Params) ? 1 : 0;
					Solid += World->OverlapBlockingTestByChannel(FVector(Centre.X, Centre.Y, Z + 90.f), FQuat::Identity, ECC_Pawn, Box, Params) ? 1 : 0;
					FNavLocation OnNav;
					OffNav += Nav && !Nav->ProjectPointToNavigation(FVector(Centre.X, Centre.Y, Z), OnNav, FVector(60.f, 60.f, 120.f)) ? 1 : 0;
				}
			}
			Check(State, Walkable >= 32 && Roofed == 0 && Solid == 0 && OffNav == 0 && Uneven == 0,
				FString::Printf(TEXT("every walkable cell is open, level ground: %d walkable, %d under a roof, %d inside something solid, %d off the navmesh, %d not level"),
					Walkable, Roofed, Solid, OffNav, Uneven));
			Check(State, ShedOpen == 0, FString::Printf(TEXT("no cell inside the shed is walkable (%d of %d)"), ShedOpen, ShedCells));
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				const FTurnUnitState* Unit = TurnBased->GetUnitState(Member);
				const FVector Feet = Member->GetActorLocation() - FVector(0.f, 0.f, Member->GetSimpleCollisionHalfHeight());
				const int32 Reach = Unit ? TurnBased->GetGrid()->GetReachableCells(Unit->GridPos, FMath::Max(1, Unit->AP)).Num() : 0;
				Check(State, Unit && FVector::Dist2D(Grid->GridToWorld(Unit->GridPos), Member->GetActorLocation()) < 80.f
					&& FMath::Abs(Feet.Z - Arena.GroundZ) < 60.f && !TurnArenaSmokeUtils::IsRoofed(World, Member->GetActorLocation(), Feet.Z, Params) && Reach > 4,
					FString::Printf(TEXT("%s on cell (%d, %d) on the ground (feet %.0f cm off the arena), %d cells reachable with %d AP"),
						*Member->DisplayName.ToString(), Unit ? Unit->GridPos.X : -1, Unit ? Unit->GridPos.Y : -1, Feet.Z - Arena.GroundZ, Reach, Unit ? Unit->AP : 0));
			}
			Next(State);
			return true;
		}
		case 3:
		{
			Flow->ExitTurnBasedToRealTime();
			if (State.StageTime < 1.f)
			{
				return true;
			}
			// 3. Inside the (roofless) shed: the fight starts, everyone is moved out onto the arena.
			const FVector2D ShedCentre = State.Shed.bIsValid ? State.Shed.GetCenter() : FVector2D(State.Generator->GetActorLocation());
			const FVector Inside(ShedCentre.X - 100.f, ShedCentre.Y - 60.f, State.Outside.Z);
			int32 Slot = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				const FVector Spot = TurnArenaSmokeUtils::OpenSpot(World, Inside + FVector(Slot++ * 90.f, 0.f, 0.f), Member, 80.f);
				TurnArenaSmokeUtils::PlaceAt(Member, Spot, 0.f);
			}
			Next(State);
			return true;
		}
		case 4:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			int32 InShed = 0;
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				InShed += State.Shed.bIsValid && State.Shed.IsInside(FVector2D(Member->GetActorLocation())) ? 1 : 0;
			}
			FTurnArenaChoice Arena;
			const bool bFound = TurnBased->FindTurnArena(Arena);
			Check(State, InShed >= 1 && bFound, FString::Printf(TEXT("%d operatives inside the shed: the fight may start (%s, arena %d cells)"), InShed,
				TurnArenaRules::FaultName(Arena.LeaderFault), Arena.Verdict.ArenaCells));
			const EGameFlowResult Result = Flow->RequestEnterTurnBased(true);
			Check(State, Result == EGameFlowResult::Ok && TurnBased->IsActive(), TEXT("turn-based combat entered from the shed"));
			if (TurnBased->IsActive())
			{
				CheckSquadOutsideShed(State, *TurnBased, *Squad, TEXT("moved out of the shed"));
			}
			Next(State);
			return true;
		}
		case 5:
		{
			Flow->ExitTurnBasedToRealTime();
			if (State.StageTime < 1.f)
			{
				return true;
			}
			// 4. Deep inside the base building: refused.
			ACameraZoneVolume* Base = TurnArenaSmokeUtils::FindBaseZone(World, State.Generator->GetActorLocation());
			Check(State, Base != nullptr, TEXT("the base building's indoor camera zone"));
			if (!Base)
			{
				return Finish(State, false);
			}
			PlaceSquad(World, *Squad, Base->GetActorLocation() - FVector(0.f, 0.f, Base->GetZoneHalfExtent().Z), FVector(0.f, 1.f, 0.f), 160.f);
			Next(State);
			return true;
		}
		case 6:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			FTurnArenaChoice Arena;
			const bool bFound = TurnBased->FindTurnArena(Arena);
			const FString Line = UTurnBasedCombatSubsystem::DescribeArenaFault(Arena.LeaderFault).ToString();
			Check(State, !bFound && Arena.LeaderFault == ETurnArenaFault::InsideBuilding && Line.Contains(TEXT("outside")),
				FString::Printf(TEXT("deep inside the base building (leader %s): no arena (%s) -> \"%s\""), *Squad->GetLeader()->GetActorLocation().ToCompactString(),
					TurnArenaRules::FaultName(Arena.LeaderFault), *Line));
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
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.TurnArenaLakeSmoke"),
		TEXT("Dev check (game designer 2026-10-26): the turn-based grid round the leader in the lake compound, the shed / base subtracted, the squad moved out of the shed, refused deep in the base; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
