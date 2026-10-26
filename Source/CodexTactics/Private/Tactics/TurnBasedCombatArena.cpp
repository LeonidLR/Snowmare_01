// UTurnBasedCombatSubsystem: where the grid of a turn-based fight is laid out and which of its cells are open (TurnArenaRules;
// UE-only). Game designer decision 2026-10-26: the grid round the leader as before 2026-10-22 (commit 40782ae was too harsh), a
// building's footprint (roof / indoor camera zone outline, walled interior, its doors) and ladders subtracted, a connected open area
// of 8 x 8 / 8 x 4 cells required. Godot reference: none (Godot built its grid round the leader wherever he stood:
// Scenes/movements/main.gd _enter_turn_based_combat).

#include "Tactics/TurnBasedCombatSubsystem.h"

#include "Camera/CameraZoneVolume.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/ProximityMineActor.h"
#include "Interactables/TripwireActor.h"
#include "Interactables/TurretActor.h"
#include "Ladders/LadderActor.h"
#include "NavigationSystem.h"
#include "Tactics/GorkyGridManager.h"

#define LOCTEXT_NAMESPACE "TurnBasedArena"

namespace TurnArenaWorld
{
	TAutoConsoleVariable<int32> CVarTurnArenaDebug(TEXT("Codex.TurnArena.Debug"), 0,
		TEXT("1: every turn-based arena search logs its grid cell by cell (. arena, , open but cut off, B building, D doorway, L ladder, # obstacle, ~ not level, N off the navmesh, _ no ground)."));

	/** Roof probe: a building piece this high above the ground (and at least RoofMinHeightCm up) marks the cell as indoors, cm. */
	constexpr float RoofProbeCm = 450.f;
	/** A piece lower than this above the ground is furniture / an obstacle, not a roof (the overlap test decides), cm. */
	constexpr float RoofMinHeightCm = 180.f;
	/** A roof piece narrower than this on either axis (a lamp, a sign) or wider than the max (a level slab) is no building, cm. */
	constexpr float MinRoofSpanCm = 100.f;
	constexpr float MaxRoofSpanCm = 6000.f;
	/** Horizontal wall probes (8 directions) at this height above the ground, this long: a walled interior (roofless shed), cm. */
	constexpr float EnclosureProbeHeightCm = 100.f;
	constexpr float EnclosureRangeCm = 450.f;
	/** Walls on two opposite sides this close = squeezed into an opening (a door), cm. */
	constexpr float JambRangeCm = 110.f;
	/** A building's outline is grown by this (its walls and door frames belong to it), cm. */
	constexpr float FootprintMarginCm = 40.f;
	/** Cells this close to a ladder's rungs / foot / top are the ladder's, cm. */
	constexpr float LadderClearanceCm = 50.f;

	/** Actor tag: never a building for the arena (a tree canopy, a bridge the squad may fight under). */
	const FName OpenTag(TEXT("TurnArenaOpen"));

	bool ClassNameContains(const UObject* Object, const TCHAR* Text)
	{
		return Object && Object->GetClass()->GetName().Contains(Text);
	}

	/** A roof / ceiling piece of a building: a static mesh (graybox cube, building mesh, kit instance) of no pawn, terrain or foliage. */
	bool IsBuildingPiece(const UPrimitiveComponent* Piece)
	{
		const AActor* Owner = Piece ? Piece->GetOwner() : nullptr;
		// By class name: the Landscape / Foliage modules are no dependencies of the game module (as URoofCutawaySubsystem).
		return Piece && Piece->IsA<UStaticMeshComponent>() && !(Owner && (Owner->IsA<APawn>() || Owner->ActorHasTag(OpenTag)))
			&& !ClassNameContains(Owner, TEXT("Landscape")) && !ClassNameContains(Piece, TEXT("Landscape"))
			&& !ClassNameContains(Owner, TEXT("InstancedFoliageActor")) && !ClassNameContains(Piece, TEXT("Foliage"));
	}

	/** The outline of a building piece: its mesh bounds in its own yaw frame (a hit kit instance on its own). false = not building-sized. */
	bool PieceFootprint(const UPrimitiveComponent& Piece, int32 Item, FTurnArenaFootprint& Out)
	{
		const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(&Piece);
		if (!Mesh || !Mesh->GetStaticMesh())
		{
			return false;
		}
		FTransform Transform = Mesh->GetComponentTransform();
		if (const UInstancedStaticMeshComponent* Instances = Cast<UInstancedStaticMeshComponent>(Mesh); Instances && Item >= 0)
		{
			if (!Instances->GetInstanceTransform(Item, Transform, /*bWorldSpace*/ true))
			{
				return false;
			}
		}
		const FBox Local = Mesh->GetStaticMesh()->GetBoundingBox();
		const float Yaw = static_cast<float>(Transform.Rotator().Yaw);
		FVector2D Min(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
		FVector2D Max(-TNumericLimits<double>::Max(), -TNumericLimits<double>::Max());
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector P((Corner & 1) ? Local.Max.X : Local.Min.X, (Corner & 2) ? Local.Max.Y : Local.Min.Y, (Corner & 4) ? Local.Max.Z : Local.Min.Z);
			const FVector2D Rotated = FVector2D(Transform.TransformPosition(P)).GetRotated(-Yaw);
			Min = FVector2D::Min(Min, Rotated);
			Max = FVector2D::Max(Max, Rotated);
		}
		Out.Centre = ((Min + Max) * 0.5).GetRotated(Yaw);
		Out.HalfExtent = (Max - Min) * 0.5;
		Out.YawDegrees = Yaw;
		const double Short = 2.0 * FMath::Min(Out.HalfExtent.X, Out.HalfExtent.Y);
		const double Long = 2.0 * FMath::Max(Out.HalfExtent.X, Out.HalfExtent.Y);
		return Short >= MinRoofSpanCm && Long <= MaxRoofSpanCm;
	}

	/** One sampled lattice cell and the roof piece over it (if any). */
	struct FRawCell
	{
		FTurnArenaSample Sample;
		TWeakObjectPtr<const UPrimitiveComponent> Roof;
		int32 RoofItem = INDEX_NONE;
	};

	/** Samples one world cell centre (XY) round the reference height RefZ. */
	FRawCell SampleCell(UWorld& World, UNavigationSystemV1* Nav, const FVector2D& XY, float RefZ, float CellSize, const FCollisionQueryParams& Params)
	{
		FRawCell Raw;
		FTurnArenaSample& Sample = Raw.Sample;
		FHitResult Ground;
		if (!World.LineTraceSingleByChannel(Ground, FVector(XY.X, XY.Y, RefZ + 250.f), FVector(XY.X, XY.Y, RefZ - 400.f), ECC_Visibility, Params))
		{
			return Raw;
		}
		Sample.bGround = true;
		Sample.GroundZ = static_cast<float>(Ground.ImpactPoint.Z);
		// Roof: object types, not the Visibility channel - a roof the cutaway has cut ignores Visibility (URoofCutawaySubsystem).
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		TArray<FHitResult> Overhead;
		World.LineTraceMultiByObjectType(Overhead, FVector(XY.X, XY.Y, Sample.GroundZ + 60.f), FVector(XY.X, XY.Y, Sample.GroundZ + RoofProbeCm),
			Objects, Params);
		for (const FHitResult& Hit : Overhead)
		{
			if (Hit.ImpactPoint.Z >= Sample.GroundZ + RoofMinHeightCm && IsBuildingPiece(Hit.GetComponent()))
			{
				Sample.bRoofed = true;
				Raw.Roof = Hit.GetComponent();
				Raw.RoofItem = Hit.Item;
				break;
			}
		}
		// Anything that blocks a walking operative between knee and head height inside the cell (walls, vehicles, fences, crates).
		const FCollisionShape Box = FCollisionShape::MakeBox(FVector(CellSize * 0.35f, CellSize * 0.35f, 60.f));
		Sample.bBlocked = World.OverlapBlockingTestByChannel(FVector(XY.X, XY.Y, Sample.GroundZ + 90.f), FQuat::Identity, ECC_Pawn, Box, Params);
		if (Nav)
		{
			FNavLocation OnNav;
			Sample.bOnNav = Nav->ProjectPointToNavigation(FVector(XY.X, XY.Y, Sample.GroundZ), OnNav, FVector(50.f, 50.f, 120.f))
				&& FMath::Abs(OnNav.Location.Z - Sample.GroundZ) < 60.f;
		}
		if (!Sample.bBlocked && !Sample.bRoofed)
		{
			// Walls round the cell: 8 probes (0 = +X, counter-clockwise); a pair of opposite probes hitting close = a door's jambs.
			const FVector From(XY.X, XY.Y, Sample.GroundZ + EnclosureProbeHeightCm);
			float Distance[8];
			for (int32 Dir = 0; Dir < 8; ++Dir)
			{
				const FVector Way = FVector(1.f, 0.f, 0.f).RotateAngleAxis(Dir * 45.f, FVector::UpVector);
				FHitResult Wall;
				Distance[Dir] = World.LineTraceSingleByChannel(Wall, From, From + Way * EnclosureRangeCm, ECC_Pawn, Params)
					? static_cast<float>(Wall.Distance) : TNumericLimits<float>::Max();
				Sample.EnclosureHits += Distance[Dir] <= EnclosureRangeCm ? 1 : 0;
			}
			for (int32 Dir = 0; Dir < 4; ++Dir)
			{
				Sample.bSqueezed |= Distance[Dir] <= JambRangeCm && Distance[Dir + 4] <= JambRangeCm;
			}
		}
		return Raw;
	}
}

FText UTurnBasedCombatSubsystem::DescribeArenaFault(ETurnArenaFault Fault)
{
	switch (Fault)
	{
	case ETurnArenaFault::TooSmall:
		return LOCTEXT("ArenaTooSmall", "⚠️ Turn-based combat needs room: at least 8 x 4 cells (12 x 6 m) of open, level ground round the squad - buildings, ladders and walls don't count.");
	case ETurnArenaFault::InsideBuilding:
		return LOCTEXT("ArenaInsideBuilding", "⚠️ Turn-based combat only outside: lead the squad out of the building first!");
	case ETurnArenaFault::SquadOutside:
		return LOCTEXT("ArenaSquad", "⚠️ Turn-based combat: gather the squad first!");
	default:
		return LOCTEXT("ArenaNotFlat", "⚠️ Turn-based combat can only start on level ground - not on high ground or in a pit!");
	}
}

ETurnArenaCell UTurnBasedCombatSubsystem::GetArenaCellClass(const FIntPoint& Cell) const
{
	const FTurnArenaVerdict& Verdict = ActiveArena.Verdict;
	return Verdict.Cells > 0 && Verdict.CellClass.Num() == Verdict.Cells * Verdict.Cells ? Verdict.ClassOf(Cell) : ETurnArenaCell::Walkable;
}

FIntPoint UTurnBasedCombatSubsystem::FindArenaCellFor(const FIntPoint& Cell, const FVector& Location, bool bArenaOnly) const
{
	if (!Grid)
	{
		return Cell;
	}
	const FTurnArenaVerdict& Verdict = ActiveArena.Verdict;
	if (Verdict.Cells != Grid->GridSize.X || Verdict.CellClass.Num() != Verdict.Cells * Verdict.Cells)
	{
		return Grid->FindNearestFreeCell(Cell); // no sampled arena: as before
	}
	const FIntPoint Raw(FMath::FloorToInt((Location.X - Grid->OriginWorld.X) / Grid->CellSize), FMath::FloorToInt((Location.Y - Grid->OriginWorld.Y) / Grid->CellSize));
	auto IsIndoors = [](ETurnArenaCell Class) { return Class == ETurnArenaCell::Building || Class == ETurnArenaCell::Doorway; };
	if (!Verdict.IsInside(Raw) && IsIndoors(Verdict.ClassOf(Cell)))
	{
		return FIntPoint(-1, -1); // in a building that reaches past the grid's edge: out of the fight
	}
	const bool bOnArena = bArenaOnly && Verdict.ArenaCells > 0;
	const FIntPoint Snap = TurnArenaRules::SnapToArena(Raw, Verdict.Cells, FTurnArenaConfig().MaxSnapCells,
		[this, &Verdict, bOnArena](const FIntPoint& C) { return Grid->IsCellWalkable(C) && !Grid->IsCellOccupied(C) && (!bOnArena || Verdict.IsArena(C)); },
		[&Verdict](const FIntPoint& C)
		{
			const ETurnArenaCell Class = Verdict.ClassOf(C);
			return Class == ETurnArenaCell::Building || Class == ETurnArenaCell::Doorway || Class == ETurnArenaCell::Walkable;
		});
	if (Snap.X >= 0)
	{
		return Snap;
	}
	if (Verdict.IsInside(Raw) && IsIndoors(Verdict.ClassOf(Raw)))
	{
		return FIntPoint(-1, -1); // deep inside a building
	}
	return Grid->FindNearestFreeCell(Cell);
}

bool UTurnBasedCombatSubsystem::FindTurnArena(FTurnArenaChoice& OutChoice) const
{
	OutChoice = FTurnArenaChoice();
	UWorld* World = GetWorld();
	const USquadSubsystem* SquadSystem = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
	const AOperativeCharacter* Leader = SquadSystem ? SquadSystem->GetLeader() : nullptr;
	if (!Leader)
	{
		return false;
	}
	const FTurnArenaConfig Config;
	// The allowed props and everybody on the battlefield (with what they carry) never count as obstacles or roofs.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TurnArenaSample), false);
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
		TArray<AActor*> Attached;
		It->GetAttachedActors(Attached, true, true);
		Params.AddIgnoredActors(Attached);
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
	for (TActorIterator<AProximityMineActor> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	for (TActorIterator<ATripwireActor> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}
	for (TActorIterator<ALadderActor> It(World); It; ++It)
	{
		Params.AddIgnoredActor(*It); // its own cells are subtracted below; its rungs are no wall / roof
	}

	const FVector Base = Leader->GetActorLocation();
	const float RefZ = static_cast<float>(Base.Z) - Leader->GetSimpleCollisionHalfHeight();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	{
		// No navmesh data round the leader (not built yet, a test map without one): the navmesh is no criterion.
		FNavLocation OnNav;
		if (Nav && !Nav->ProjectPointToNavigation(FVector(Base.X, Base.Y, RefZ), OnNav, FVector(150.f, 150.f, 250.f)))
		{
			Nav = nullptr;
		}
	}
	const int32 Half = Config.Cells / 2;
	const int32 Reach = Half + Config.SearchRadiusCells;
	// Lattice cell L (relative to the leader) covers Base + L * CellSize .. + CellSize; a grid offset O covers L = O - Half .. O + Half - 1.
	auto CentreOf = [&Base, &Config](const FIntPoint& L)
	{
		return FVector2D(Base.X + (L.X + 0.5f) * Config.CellSize, Base.Y + (L.Y + 0.5f) * Config.CellSize);
	};
	auto LatticeOf = [&Base, &Config](const FVector& Point)
	{
		return FIntPoint(FMath::FloorToInt((Point.X - Base.X) / Config.CellSize), FMath::FloorToInt((Point.Y - Base.Y) / Config.CellSize));
	};

	// 1. Every lattice cell any candidate grid may use.
	TMap<FIntPoint, TurnArenaWorld::FRawCell> Lattice;
	Lattice.Reserve(4 * Reach * Reach);
	for (int32 X = -Reach; X < Reach; ++X)
	{
		for (int32 Y = -Reach; Y < Reach; ++Y)
		{
			Lattice.Add(FIntPoint(X, Y), TurnArenaWorld::SampleCell(*World, Nav, CentreOf(FIntPoint(X, Y)), RefZ, Config.CellSize, Params));
		}
	}

	// 2. Building outlines: every roof piece seen overhead, the indoor camera zones (Closed always; Shelter when it has a roof or walls).
	TSet<TPair<const UPrimitiveComponent*, int32>> Roofs;
	for (const TPair<FIntPoint, TurnArenaWorld::FRawCell>& Entry : Lattice)
	{
		const UPrimitiveComponent* Roof = Entry.Value.Roof.Get();
		FTurnArenaFootprint Footprint;
		if (Roof && !Roofs.Contains(TPair<const UPrimitiveComponent*, int32>(Roof, Entry.Value.RoofItem)))
		{
			Roofs.Add(TPair<const UPrimitiveComponent*, int32>(Roof, Entry.Value.RoofItem));
			if (TurnArenaWorld::PieceFootprint(*Roof, Entry.Value.RoofItem, Footprint))
			{
				OutChoice.Footprints.Add(Footprint);
			}
		}
	}
	const float SearchRadius = (Reach + 2) * Config.CellSize * UE_SQRT_2;
	for (TActorIterator<ACameraZoneVolume> It(World); It; ++It)
	{
		if (It->Environment != ECameraZoneEnvironment::Closed && It->Environment != ECameraZoneEnvironment::Shelter)
		{
			continue;
		}
		const FTransform Zone = It->GetZoneTransform();
		const FVector Extent = It->GetZoneHalfExtent();
		const FVector Centre = Zone.GetLocation();
		if (FVector::Dist2D(Centre, Base) > SearchRadius + Extent.Size2D() || RefZ + 90.f < Centre.Z - Extent.Z - 100.f || RefZ + 90.f > Centre.Z + Extent.Z + 100.f)
		{
			continue;
		}
		FTurnArenaFootprint Footprint;
		Footprint.Centre = FVector2D(Centre);
		Footprint.HalfExtent = FVector2D(Extent);
		Footprint.YawDegrees = static_cast<float>(Zone.Rotator().Yaw);
		bool bIndoors = It->Environment == ECameraZoneEnvironment::Closed;
		for (const TPair<FIntPoint, TurnArenaWorld::FRawCell>& Entry : Lattice)
		{
			const FTurnArenaSample& Sample = Entry.Value.Sample;
			if (!bIndoors && (Sample.bRoofed || Sample.EnclosureHits >= Config.EnclosedMinHits)
				&& TurnArenaRules::IsInsideFootprint(Footprint, CentreOf(Entry.Key), 0.f))
			{
				bIndoors = true;
			}
		}
		if (bIndoors)
		{
			OutChoice.Footprints.Add(Footprint);
		}
	}

	// 3. Ladders: the rungs, the foot, the top (LADDERS.md: ladders are forbidden in turn-based combat).
	TSet<FIntPoint> LadderCells;
	for (TActorIterator<ALadderActor> It(World); It; ++It)
	{
		if (FVector::Dist2D(It->GetActorLocation(), Base) > SearchRadius)
		{
			continue;
		}
		const FVector Points[] = { It->GetActorLocation(), It->GetBottomLinkWorld(), It->GetTopLinkWorld(), It->GetTopExitWorld(),
			It->GetRungWorldLocation(FMath::Max(0, It->GetRungCount() / 2)) };
		for (const FVector& Point : Points)
		{
			LadderCells.Append(TurnArenaRules::CellsNearPoint(FVector2D(Point), FVector2D(Base), Config.CellSize, TurnArenaWorld::LadderClearanceCm));
		}
	}
	for (TPair<FIntPoint, TurnArenaWorld::FRawCell>& Entry : Lattice)
	{
		FTurnArenaSample& Sample = Entry.Value.Sample;
		Sample.bLadder = LadderCells.Contains(Entry.Key);
		const FVector2D Centre = CentreOf(Entry.Key);
		Sample.bInFootprint = OutChoice.Footprints.ContainsByPredicate([&Centre](const FTurnArenaFootprint& Footprint)
		{
			return TurnArenaRules::IsInsideFootprint(Footprint, Centre, TurnArenaWorld::FootprintMarginCm);
		});
	}

	// 4. Candidate grids: the leader's own first (as before 2026-10-22); shifted ones only when it fails.
	TArray<FIntPoint> UnitLattice;
	TArray<float> UnitFeet;
	TArray<const AOperativeCharacter*> Members = { Leader };
	for (const AOperativeCharacter* Member : SquadSystem->GetMembers())
	{
		Members.AddUnique(Member);
	}
	for (const AOperativeCharacter* Member : Members)
	{
		// Climbers are not on the grid (LADDERS.md 7.6): they join once off the ladder.
		if (Member && Member->HealthComponent && Member->HealthComponent->IsAlive() && !Member->IsClimbingLadder())
		{
			UnitLattice.Add(LatticeOf(Member->GetActorLocation()));
			UnitFeet.Add(static_cast<float>(Member->GetActorLocation().Z) - Member->GetSimpleCollisionHalfHeight());
		}
	}
	TArray<FIntPoint> EnemyLattice;
	for (TActorIterator<AEnemyCharacter> It(World); It; ++It)
	{
		if (!It->IsDying() && It->GetHealthComponent() && It->GetHealthComponent()->IsAlive()
			&& FVector::Dist2D(It->GetActorLocation(), Base) < Config.Cells * Config.CellSize)
		{
			EnemyLattice.Add(LatticeOf(It->GetActorLocation()));
		}
	}
	auto EvaluateAt = [&](const FIntPoint& Offset)
	{
		const FIntPoint Origin = Offset - FIntPoint(Half, Half); // lattice of grid cell (0, 0)
		TArray<FTurnArenaUnit> Units;
		for (int32 Index = 0; Index < UnitLattice.Num(); ++Index)
		{
			FTurnArenaUnit& Unit = Units.AddDefaulted_GetRef();
			Unit.Cell = UnitLattice[Index] - Origin;
			Unit.FeetZ = UnitFeet[Index];
		}
		return TurnArenaRules::Evaluate([&Lattice, &Origin](const FIntPoint& Cell)
		{
			const TurnArenaWorld::FRawCell* Raw = Lattice.Find(Cell + Origin);
			return Raw ? Raw->Sample : FTurnArenaSample();
		}, Units, Config);
	};
	const FTurnArenaVerdict AtLeader = EvaluateAt(FIntPoint::ZeroValue);
	OutChoice.LeaderFault = AtLeader.Fault;
	OutChoice.LeaderFaultCell = AtLeader.Cell;
	OutChoice.Tried = 1;
	OutChoice.Verdict = AtLeader;
	OutChoice.GroundZ = AtLeader.bHasGround ? AtLeader.GroundZ : RefZ;
	OutChoice.Centre = FVector(Base.X, Base.Y, OutChoice.GroundZ);
	OutChoice.bFound = AtLeader.IsValid();
	if (!OutChoice.bFound)
	{
		float BestScore = TNumericLimits<float>::Max();
		for (const FIntPoint& Offset : TurnArenaRules::CandidateOffsets(Config))
		{
			if (Offset == FIntPoint::ZeroValue)
			{
				continue;
			}
			++OutChoice.Tried;
			FTurnArenaVerdict Verdict = EvaluateAt(Offset);
			if (!Verdict.IsValid())
			{
				continue;
			}
			int32 EnemiesInside = 0;
			for (const FIntPoint& Enemy : EnemyLattice)
			{
				EnemiesInside += Verdict.IsInside(Enemy - (Offset - FIntPoint(Half, Half))) ? 1 : 0;
			}
			const float Score = TurnArenaRules::CandidateScore(Offset, EnemiesInside, Config);
			if (Score < BestScore)
			{
				BestScore = Score;
				OutChoice.bFound = true;
				OutChoice.Offset = Offset;
				OutChoice.GroundZ = Verdict.GroundZ;
				OutChoice.Centre = FVector(Base.X + Offset.X * Config.CellSize, Base.Y + Offset.Y * Config.CellSize, Verdict.GroundZ);
				OutChoice.Verdict = MoveTemp(Verdict);
			}
		}
	}

	const FTurnArenaVerdict& Chosen = OutChoice.Verdict;
	int32 Subtracted = 0;
	for (const ETurnArenaCell Class : Chosen.CellClass)
	{
		Subtracted += TurnArenaRules::IsSubtracted(Class) && Class != ETurnArenaCell::NoGround ? 1 : 0;
	}
	if (TurnArenaWorld::CVarTurnArenaDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[TurnArena] grid offset (%d, %d) (rows = X, columns = Y), median ground %.0f:"), OutChoice.Offset.X,
			OutChoice.Offset.Y, Chosen.GroundZ);
		for (int32 X = 0; X < Chosen.Cells; ++X)
		{
			FString Row;
			for (int32 Y = 0; Y < Chosen.Cells; ++Y)
			{
				const ETurnArenaCell Class = Chosen.ClassOf(FIntPoint(X, Y));
				Row.AppendChar(Class == ETurnArenaCell::Walkable && !Chosen.IsArena(FIntPoint(X, Y)) ? TEXT(',') : TurnArenaRules::CellChar(Class));
			}
			UE_LOG(LogCodexTactics, Display, TEXT("[TurnArena]   %2d %s"), X, *Row);
		}
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[TurnArena] %s: %d candidates, %d cells sampled, %d building outlines, %d ladder cells; round the leader: %s at (%d, %d); grid offset (%d, %d), ground %.0f, arena %d cells (rect %dx%d at (%d, %d)), %d cells subtracted"),
		OutChoice.bFound ? TEXT("arena found") : TEXT("NO arena"), OutChoice.Tried, Lattice.Num(), OutChoice.Footprints.Num(), LadderCells.Num(),
		TurnArenaRules::FaultName(OutChoice.LeaderFault), OutChoice.LeaderFaultCell.X, OutChoice.LeaderFaultCell.Y, OutChoice.Offset.X, OutChoice.Offset.Y,
		OutChoice.GroundZ, Chosen.ArenaCells, Chosen.RectSize.X, Chosen.RectSize.Y, Chosen.RectOrigin.X, Chosen.RectOrigin.Y, Subtracted);
	return OutChoice.bFound;
}

#undef LOCTEXT_NAMESPACE
