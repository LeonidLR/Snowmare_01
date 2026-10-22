// Dev-only check of the game designer's Demo 01 report 2026-10-22 (wave 3 on the lake, blast door 51 %, generator online: the
// turn-based grid was laid INSIDE the base building, the operatives stuck outside / on the roof, nobody could move). The rule
// (2026-09-30, repeated 2026-10-22): a turn-based fight only on flat, open ground - nothing in the middle but barrels, barricades
// and turrets (TurnArenaRules, UTurnBasedCombatSubsystem::FindTurnArena):
//   Scripts/smoke_parallel.ps1 -Commands TurnArenaLakeSmoke   (map /Game/Maps/L_Demo01_A_FrozenLake + -LevelJson, Scripts/test_map.json)
//  1. the squad in the compound's yard right outside the generator shed (a roofed building + the north wall within the 21 m grid round
//     the leader): the grid round the leader is refused (logged why), a valid arena is found nearby;
//  2. the fight (wave 1, its lane enemies parked away) + a hound 8 m off -> turn-based: the grid is the found arena; EVERY walkable
//     cell is open ground (no roof overhead, nothing solid in it, on the navmesh, level with the arena); every operative stands on a
//     walkable cell on the ground (not on a roof) and can move (reachable cells with his AP);
//  3. the squad INSIDE the shed: no arena at all -> the Space-hold entry is refused with the "only in the open" line.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Combat/WaveSubsystem.h"
#include "Containers/Ticker.h"
#include "Core/SaveGameSubsystem.h"
#include "Debug/SmokeUtils.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/TurretActor.h"
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

	FCollisionQueryParams OpenGroundParams(UWorld* World)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TurnArenaLakeProbe), false);
		for (TActorIterator<ACharacter> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		for (TActorIterator<ABarrelActor> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		for (TActorIterator<ABarricadeActor> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		for (TActorIterator<ATurretActor> It(World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		return Params;
	}

	/** Ground under XY near RefZ (a trace from 2.5 m above), or false. */
	bool GroundAt(UWorld* World, const FVector& XY, float RefZ, const FCollisionQueryParams& Params, float& OutZ)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, FVector(XY.X, XY.Y, RefZ + 250.f), FVector(XY.X, XY.Y, RefZ - 400.f), ECC_Visibility, Params))
		{
			return false;
		}
		OutZ = Hit.ImpactPoint.Z;
		return true;
	}

	bool IsRoofed(UWorld* World, const FVector& XY, float GroundZ, const FCollisionQueryParams& Params)
	{
		FHitResult Hit;
		return World->LineTraceSingleByChannel(Hit, FVector(XY.X, XY.Y, GroundZ + 60.f), FVector(XY.X, XY.Y, GroundZ + 450.f), ECC_Visibility, Params);
	}

	/**
	 * The nearest navmesh spot to Point (rings up to 4 m) where a standing capsule is not BLOCKED (SmokeUtils::FreeSpot refuses
	 * touching overlaps too - inside the yard's tutorial volume every spot "overlaps"; LakeQuestSmoke's YardSpot). Centre height.
	 */
	FVector OpenSpot(UWorld* World, const FVector& Point, const AActor* Ignore)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TurnArenaLakeSpot), false, Ignore);
		for (const float Radius : { 0.f, 80.f, 160.f, 250.f, 400.f })
		{
			for (int32 Step = 0; Step < (Radius <= 0.f ? 1 : 12); ++Step)
			{
				const FVector Candidate = Point + FVector(Radius, 0.f, 0.f).RotateAngleAxis(Step * 30.f, FVector::UpVector);
				FNavLocation OnNav;
				if (Nav && Nav->ProjectPointToNavigation(Candidate, OnNav, FVector(40.f, 40.f, 300.f))
					&& !World->OverlapBlockingTestByChannel(OnNav.Location + FVector(0.f, 0.f, 95.f), FQuat::Identity, ECC_Pawn,
						FCollisionShape::MakeCapsule(42.f, 88.f), Params))
				{
					return OnNav.Location + FVector(0.f, 0.f, 92.f);
				}
			}
		}
		return Point + FVector(0.f, 0.f, 92.f);
	}

	/** The squad round Spot (the leader on it), stopped. */
	void PlaceSquad(UWorld* World, USquadSubsystem& Squad, const FVector& Spot, const FVector& Back)
	{
		int32 Slot = 0;
		for (AOperativeCharacter* Member : Squad.GetMembers())
		{
			Member->StopOperative();
			const FVector Offset = Member == Squad.GetLeader() ? FVector::ZeroVector
				: Back * 180.f + FVector::CrossProduct(FVector::UpVector, Back) * (Slot++ == 0 ? -150.f : 150.f);
			const FVector Free = OpenSpot(World, Spot + Offset, Member);
			Member->TeleportTo(FVector(Free.X, Free.Y, Free.Z + 4.f), (-Back).Rotation(), false, true);
		}
	}

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		State.Time += StepSeconds;
		State.StageTime += StepSeconds;
		UWorld* World = WeakWorld.Get();
		if (!World || State.Time > 240.f)
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
			for (TActorIterator<AInteractableActor> It(World); It; ++It)
			{
				if (It->ObjectType == EInteractableType::Generator)
				{
					State.Generator = *It;
				}
			}
			AInteractableActor* Generator = State.Generator.Get();
			Check(State, Generator != nullptr, TEXT("the compound's generator (in its shed)"));
			if (!Generator)
			{
				return Finish(State, false);
			}
			// Out of the shed door (south, -Y): the first open, navigable spot, then 2.5 m further into the yard.
			const FCollisionQueryParams Params = OpenGroundParams(World);
			const float RefZ = Generator->GetActorLocation().Z - 50.f;
			FVector Spot = Generator->GetActorLocation();
			for (float Step = 100.f; Step <= 1500.f; Step += 50.f)
			{
				const FVector Probe = Generator->GetActorLocation() - FVector(0.f, Step, 0.f);
				float Z = 0.f;
				if (GroundAt(World, Probe, RefZ, Params, Z) && !IsRoofed(World, Probe, Z, Params)
					&& Nav->ProjectPointToNavigation(FVector(Probe.X, Probe.Y, Z), OnNav, FVector(40.f, 40.f, 120.f)))
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
			Check(State, Arena.LeaderFault != ETurnArenaFault::None, FString::Printf(TEXT("the grid round the leader (shed + north wall in it) is refused: %s at cell (%d, %d)"),
				TurnArenaRules::FaultName(Arena.LeaderFault), Arena.LeaderFaultCell.X, Arena.LeaderFaultCell.Y));
			Check(State, bFound, FString::Printf(TEXT("a valid arena nearby: offset (%d, %d) cells, %d candidates"), Arena.Offset.X, Arena.Offset.Y, Arena.Tried));
			// The fight: wave 1 on the lake lanes (parked far away), a hound 8 m out in the yard.
			SmokeUtils::StartWaveFight(Flow);
			for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
			{
				SmokeUtils::ParkEnemyFarAway(*It);
				It->SetActorTickEnabled(false);
			}
			State.Hound = Waves->SpawnEnemy(EEnemyArchetype::FrostHound, OpenSpot(World, State.Outside - FVector(0.f, 800.f, 0.f), nullptr)
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
			const EGameFlowResult Result = Flow->RequestEnterTurnBased(true);
			Check(State, Result == EGameFlowResult::Ok && TurnBased->IsActive() && TurnBased->GetEnemyCount() >= 1, FString::Printf(TEXT("turn-based combat entered (%d enemies on the grid; the hound %.1f m from the leader)"),
				TurnBased->GetEnemyCount(), State.Hound.IsValid() ? FVector::Dist2D(State.Hound->GetActorLocation(), Squad->GetLeader()->GetActorLocation()) / 100.f : -1.f));
			if (!TurnBased->IsActive() || !TurnBased->GetGrid())
			{
				return Finish(State, false);
			}
			const FTurnArenaChoice& Arena = TurnBased->GetActiveArena();
			const UGorkyGridManager* Grid = TurnBased->GetGrid();
			Check(State, Arena.bFound && FVector::Dist2D(Grid->GridToWorld(FIntPoint(7, 7)) - FVector(75.f, 75.f, 0.f), Arena.Centre) < 5.f,
				FString::Printf(TEXT("the grid is the found arena (centre %s)"), *Arena.Centre.ToCompactString()));
			const FCollisionQueryParams Params = OpenGroundParams(World);
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			int32 Walkable = 0;
			int32 Roofed = 0;
			int32 Solid = 0;
			int32 OffNav = 0;
			int32 Uneven = 0;
			const FCollisionShape Box = FCollisionShape::MakeBox(FVector(150.f * 0.35f, 150.f * 0.35f, 60.f));
			for (int32 X = 0; X < Grid->GridSize.X; ++X)
			{
				for (int32 Y = 0; Y < Grid->GridSize.Y; ++Y)
				{
					const FIntPoint Cell(X, Y);
					if (!Grid->IsCellWalkable(Cell))
					{
						continue;
					}
					++Walkable;
					const FVector Centre = Grid->GridToWorld(Cell);
					float Z = 0.f;
					if (!GroundAt(World, Centre, Arena.GroundZ, Params, Z))
					{
						++Uneven;
						continue;
					}
					Uneven += FMath::Abs(Z - Arena.GroundZ) > 35.f ? 1 : 0;
					Roofed += IsRoofed(World, Centre, Z, Params) ? 1 : 0;
					Solid += World->OverlapBlockingTestByChannel(FVector(Centre.X, Centre.Y, Z + 90.f), FQuat::Identity, ECC_Pawn, Box, Params) ? 1 : 0;
					FNavLocation OnNav;
					OffNav += Nav && !Nav->ProjectPointToNavigation(FVector(Centre.X, Centre.Y, Z), OnNav, FVector(60.f, 60.f, 120.f)) ? 1 : 0;
				}
			}
			Check(State, Walkable >= 100 && Roofed == 0 && Solid == 0 && OffNav == 0 && Uneven == 0,
				FString::Printf(TEXT("every walkable cell is open, level ground: %d walkable, %d under a roof, %d inside something solid, %d off the navmesh, %d not level"),
					Walkable, Roofed, Solid, OffNav, Uneven));
			for (AOperativeCharacter* Member : Squad->GetMembers())
			{
				const FTurnUnitState* Unit = TurnBased->GetUnitState(Member);
				const FVector Feet = Member->GetActorLocation() - FVector(0.f, 0.f, Member->GetSimpleCollisionHalfHeight());
				const int32 Reach = Unit ? TurnBased->GetGrid()->GetReachableCells(Unit->GridPos, FMath::Max(1, Unit->AP)).Num() : 0;
				Check(State, Unit && FVector::Dist2D(Grid->GridToWorld(Unit->GridPos), Member->GetActorLocation()) < 80.f
					&& FMath::Abs(Feet.Z - Arena.GroundZ) < 60.f && !IsRoofed(World, Member->GetActorLocation(), Feet.Z, Params) && Reach > 4,
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
			// 3. Inside the shed: no arena, the Space-hold entry refused.
			AInteractableActor* Generator = State.Generator.Get();
			const FVector Inside = OpenSpot(World, Generator->GetActorLocation() - FVector(0.f, 150.f, 0.f), Generator) - FVector(0.f, 0.f, 92.f);
			PlaceSquad(World, *Squad, Inside, FVector(1.f, 0.f, 0.f));
			Next(State);
			return true;
		}
		case 4:
		{
			if (State.StageTime < 0.5f)
			{
				return true;
			}
			const FCollisionQueryParams Params = OpenGroundParams(World);
			const AOperativeCharacter* Leader = Squad->GetLeader();
			const float Feet = Leader->GetActorLocation().Z - Leader->GetSimpleCollisionHalfHeight();
			const bool bIndoors = IsRoofed(World, Leader->GetActorLocation(), Feet, Params);
			FTurnArenaChoice Arena;
			const bool bFound = TurnBased->FindTurnArena(Arena);
			Check(State, !bFound && UTurnBasedCombatSubsystem::DescribeArenaFault(Arena.LeaderFault).ToString().Contains(TEXT("Turn-based combat")),
				FString::Printf(TEXT("inside the shed (roof overhead %d): no arena (%s) -> \"%s\""), bIndoors ? 1 : 0, TurnArenaRules::FaultName(Arena.LeaderFault),
					*UTurnBasedCombatSubsystem::DescribeArenaFault(Arena.LeaderFault).ToString()));
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
		TEXT("Dev check (designer report 2026-10-22): the turn-based grid only on open, level ground (never in a building); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
