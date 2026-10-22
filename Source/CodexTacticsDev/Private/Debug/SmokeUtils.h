#pragma once

// Shared helpers of the dev smoke commands (not shipped).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Combat/EnemySpawnPoint.h"
#include "Combat/HordeIntroSubsystem.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/GateActor.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMeshActor.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"

namespace SmokeUtils
{
	/**
	 * Transform of the L_MovementTest layout, from the checkpoint gate (else the "Floor" actor), created by
	 * create_movement_test_map.py. The user may move / rotate the whole layout in the editor; smokes express their
	 * points in the original design coordinates and map them through this transform. Identity when there is no Floor.
	 */
	inline FTransform LayoutTransform(const UWorld* World)
	{
		// Anchor on the checkpoint gate (design (0, -2000), yaw 90): the floor may be resized for play-testing
		// (e.g. stretched to a far PlayerStart), which moves its centre but not the gate.
		if (World)
		{
			for (TActorIterator<AGateActor> It(const_cast<UWorld*>(World)); It; ++It)
			{
				if (It->bSetDressing)
				{
					continue; // 2026-10-22 b: the open lake door behind the Frozen Factory start is no layout anchor
				}
				const float Yaw = It->GetActorRotation().Yaw - 90.f;
				const FRotator LayoutRotation(0.f, Yaw, 0.f);
				const FVector Gate = It->GetActorLocation();
				const FVector Origin = FVector(Gate.X, Gate.Y, 0.f) - LayoutRotation.RotateVector(FVector(0.f, -2000.f, 0.f));
				return FTransform(LayoutRotation, Origin);
			}
		}
		if (World)
		{
			for (TActorIterator<AStaticMeshActor> It(const_cast<UWorld*>(World)); It; ++It)
			{
#if WITH_EDITOR
				const bool bFloor = It->GetActorLabel() == TEXT("Floor");
#else
				const bool bFloor = It->GetName().StartsWith(TEXT("Floor"));
#endif
				if (bFloor)
				{
					const FVector Location = It->GetActorLocation();
					return FTransform(FRotator(0.f, It->GetActorRotation().Yaw, 0.f), FVector(Location.X, Location.Y, 0.f));
				}
			}
		}
		return FTransform::Identity;
	}

	/**
	 * Desired if a straight, fully pathed walk from From reaches it on the navmesh; else the same distance turned by
	 * +-30 / 60 / 90 ... degrees, the first clear one (the user re-lays L_MovementTest — user decision 2026-10-04: the
	 * map is the standard, the smokes adapt). Desired when nothing is clear.
	 */
	inline FVector ClearPoint(UWorld* World, const FVector& From, const FVector& Desired)
	{
		UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		if (!Nav)
		{
			return Desired;
		}
		const FVector Offset = FVector(Desired.X - From.X, Desired.Y - From.Y, 0.f);
		for (const float Turn : { 0.f, 30.f, -30.f, 60.f, -60.f, 90.f, -90.f, 120.f, -120.f, 150.f, -150.f, 180.f })
		{
			const FVector Candidate = From + Offset.RotateAngleAxis(Turn, FVector::UpVector) + FVector(0.f, 0.f, Desired.Z - From.Z);
			FNavLocation OnNav;
			if (!Nav->ProjectPointToNavigation(Candidate, OnNav, FVector(80.f, 80.f, 300.f)))
			{
				continue;
			}
			FVector HitLocation;
			if (Nav->NavigationRaycast(World, From, OnNav.Location, HitLocation))
			{
				continue; // blocked on the way
			}
			const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, From, OnNav.Location);
			if (Path && Path->IsValid() && !Path->IsPartial())
			{
				return FVector(OnNav.Location.X, OnNav.Location.Y, Desired.Z);
			}
		}
		return Desired;
	}

	/**
	 * The nearest spot to Point (rings up to 6 m) on the navmesh where a 45 / 90 cm capsule is free of geometry and pawns
	 * (a teleport into a prop the user placed leaves the unit stuck). Centre height = ground + 92 cm. Point when none.
	 */
	inline FVector FreeSpot(UWorld* World, const FVector& Point, const AActor* Ignore = nullptr)
	{
		UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		if (!World)
		{
			return Point;
		}
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SmokeFreeSpot), false, Ignore);
		for (const float Radius : { 0.f, 100.f, 200.f, 300.f, 450.f, 600.f })
		{
			const int32 Samples = Radius <= 0.f ? 1 : 12;
			for (int32 Step = 0; Step < Samples; ++Step)
			{
				const FVector Candidate = Point + FVector(Radius, 0.f, 0.f).RotateAngleAxis(Step * 30.f, FVector::UpVector);
				FNavLocation OnNav;
				if (Nav && !Nav->ProjectPointToNavigation(Candidate, OnNav, FVector(60.f, 60.f, 300.f)))
				{
					continue;
				}
				const FVector Centre = (Nav ? OnNav.Location : Candidate) + FVector(0.f, 0.f, 92.f);
				if (!World->OverlapAnyTestByChannel(Centre, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(45.f, 90.f), Params))
				{
					return Centre;
				}
			}
		}
		return Point;
	}

	/**
	 * Takes a live enemy out of the picture without ending its wave: frozen (CustomTimeDilation 0) and parked 1 km to the
	 * side and 50 m down. Moving it only down is not enough: many game queries are 2D (panic repulsion, nearest-enemy
	 * distance, vision cones, threat checks), and L_MovementTest's sudden horde (55) spawns right around the squad, so an
	 * enemy "under the floor" still pushes / scares from where it stood (PanicSmoke 2026-10-15).
	 */
	inline void ParkEnemyFarAway(AActor* Enemy)
	{
		if (!Enemy)
		{
			return;
		}
		Enemy->CustomTimeDilation = 0.f;
		Enemy->SetActorLocation(Enemy->GetActorLocation() + FVector(100000.f, 0.f, -5000.f), false, nullptr, ETeleportType::TeleportPhysics);
	}

	/** A smoke's own empty test ground (FindClearArena): Origin on the navmesh, the frame (Forward, Right) the rectangle was checked in. */
	struct FClearArena
	{
		bool bFound = false;
		FVector Origin = FVector::ZeroVector;
		FVector Forward = FVector::ForwardVector;
		FVector Right = FVector::RightVector;
		/** Candidates tried / rejected by geometry / by the navmesh (logged by the caller). */
		int32 Tried = 0;
		int32 Blocked = 0;
		int32 OffNav = 0;
	};

	/**
	 * Is the rectangle Rect (X along Forward, Y along Right, cm from Origin) empty, flat and fully walkable? Empty: nothing that
	 * blocks pawns or sight, or overlaps pawns (a trigger), stands in it from 0.4 to 3.4 m above Origin (actors in Ignore and
	 * navmesh bounds volumes do not count). Walkable: every 2.5 m sample is on the navmesh within 0.6 m of Origin's height and
	 * its four corners are reached by a near-straight navmesh path. Sets bBlocked when the geometry test failed.
	 */
	inline bool IsArenaClear(UWorld* World, const FVector& Origin, const FVector& Forward, const FBox2D& Rect, const TArray<const AActor*>& Ignore, bool& bBlocked)
	{
		bBlocked = false;
		UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		if (!Nav)
		{
			return false;
		}
		const FVector F = Forward.GetSafeNormal2D();
		const FVector R = FVector::CrossProduct(FVector::UpVector, F);
		const FVector2D Mid = Rect.GetCenter();
		const FVector2D Half = Rect.GetExtent();
		const FVector BoxCentre = Origin + F * Mid.X + R * Mid.Y + FVector(0.f, 0.f, 40.f + 150.f);
		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SmokeArena), false);
		for (const AActor* Each : Ignore)
		{
			Params.AddIgnoredActor(Each);
		}
		World->OverlapMultiByObjectType(Overlaps, BoxCentre, F.ToOrientationQuat(), FCollisionObjectQueryParams(FCollisionObjectQueryParams::InitType::AllObjects),
			FCollisionShape::MakeBox(FVector(Half.X, Half.Y, 150.f)), Params);
		for (const FOverlapResult& Overlap : Overlaps)
		{
			const UPrimitiveComponent* Component = Overlap.GetComponent();
			const AActor* Owner = Overlap.GetActor();
			if (!Component || (Owner && Owner->IsA<ANavMeshBoundsVolume>()))
			{
				continue;
			}
			if (Component->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Ignore || Component->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block)
			{
				bBlocked = true;
				return false;
			}
		}
		for (float Along = Rect.Min.X; Along <= Rect.Max.X + 1.f; Along += 250.f)
		{
			for (float Side = Rect.Min.Y; Side <= Rect.Max.Y + 1.f; Side += 250.f)
			{
				FNavLocation OnNav;
				const FVector Sample = Origin + F * FMath::Min(Along, Rect.Max.X) + R * FMath::Min(Side, Rect.Max.Y);
				if (!Nav->ProjectPointToNavigation(Sample, OnNav, FVector(40.f, 40.f, 150.f)) || FMath::Abs(OnNav.Location.Z - Origin.Z) > 60.f)
				{
					return false;
				}
			}
		}
		for (const FVector2D& Corner : { Rect.Min, Rect.Max, FVector2D(Rect.Min.X, Rect.Max.Y), FVector2D(Rect.Max.X, Rect.Min.Y) })
		{
			const FVector2D Inset = Corner + (Mid - Corner).GetSafeNormal() * 100.f;
			const FVector Target = Origin + F * Inset.X + R * Inset.Y;
			const UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, Origin, Target);
			if (!Path || !Path->IsValid() || Path->IsPartial() || Path->GetPathLength() > FVector::Dist2D(Origin, Target) * 1.15f + 150.f)
			{
				return false;
			}
		}
		return true;
	}

	/**
	 * An empty spot of the map for a smoke's own set-up (walls, enemies, the squad, all spawned at runtime), so a designer moving
	 * things on the map cannot break it (the user's L_PatrolTest edits 2026-10-18: an enemy searching a stone saw the squad past
	 * a prop). Rect = the rectangle the smoke uses (X along Forward, Y along Right, cm from the origin; pass a margin in it).
	 * Candidate origins on a GridCm grid over the navmesh bounds, the nearest to Hint first, each tried facing HintForward and
	 * turned 90 / 180 / 270 deg (IsArenaClear). bFound false when the map has no such spot.
	 */
	inline FClearArena FindClearArena(UWorld* World, const FVector& Hint, const FVector& HintForward, const FBox2D& Rect, const TArray<const AActor*>& Ignore,
		float GridCm = 500.f)
	{
		FClearArena Arena;
		UNavigationSystemV1* Nav = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
		if (!Nav)
		{
			return Arena;
		}
		FBox Bounds(ForceInit);
		for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
		{
			Bounds += It->GetComponentsBoundingBox(true);
		}
		if (!Bounds.IsValid)
		{
			return Arena;
		}
		TArray<FVector> Candidates;
		for (double X = Bounds.Min.X + GridCm * 0.5; X < Bounds.Max.X; X += GridCm)
		{
			for (double Y = Bounds.Min.Y + GridCm * 0.5; Y < Bounds.Max.Y; Y += GridCm)
			{
				Candidates.Add(FVector(X, Y, Hint.Z));
			}
		}
		Candidates.Sort([&Hint](const FVector& A, const FVector& B) { return FVector::DistSquared2D(A, Hint) < FVector::DistSquared2D(B, Hint); });
		const FVector Facing = HintForward.GetSafeNormal2D().IsNearlyZero() ? FVector::ForwardVector : HintForward.GetSafeNormal2D();
		for (const FVector& Candidate : Candidates)
		{
			FNavLocation OnNav;
			if (!Nav->ProjectPointToNavigation(Candidate, OnNav, FVector(GridCm * 0.4f, GridCm * 0.4f, Bounds.GetSize().Z + 1000.f)))
			{
				continue;
			}
			for (const float Turn : { 0.f, 90.f, 180.f, 270.f })
			{
				const FVector F = Facing.RotateAngleAxis(Turn, FVector::UpVector);
				bool bBlocked = false;
				++Arena.Tried;
				if (IsArenaClear(World, OnNav.Location, F, Rect, Ignore, bBlocked))
				{
					Arena.bFound = true;
					Arena.Origin = OnNav.Location;
					Arena.Forward = F;
					Arena.Right = FVector::CrossProduct(FVector::UpVector, F);
					return Arena;
				}
				Arena.Blocked += bBlocked ? 1 : 0;
				Arena.OffNav += bBlocked ? 0 : 1;
			}
		}
		return Arena;
	}

	/** Design-coordinate point of L_MovementTest (layout at the origin, yaw 0) -> world point of the current layout. */
	inline FVector LevelPoint(const UWorld* World, const FVector& DesignPoint)
	{
		return LayoutTransform(World).TransformPosition(DesignPoint);
	}

	/**
	 * Puts the squad at the test start of L_MovementTest (design origin, facing design +X, commander ahead of the other two), the
	 * layout the checks were written for (Scripts/Editor/create_movement_test_map.py). The level's PlayerStart may be
	 * moved for play-testing; smokes that rely on distances to map features call this first.
	 */
	/**
	 * Smokes that check preparation-phase behaviour (preparation set-up, banners, prep dialogue, ...) use the old
	 * prepared start (cutscene -> preparation countdown -> wave): still the start of a level wave with "sudden_start":
	 * false. Sets Codex.HordeIntro.SuddenStart 0 for this process (each smoke runs in its own); false restores -1.
	 */
	inline void UsePreparedWaveStart(bool bPrepared = true)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("Codex.HordeIntro.SuddenStart")))
		{
			Variable->Set(bPrepared ? 0 : -1, ECVF_SetByConsole);
		}
	}

	/**
	 * Ends the horde intro of a sudden wave deterministically (user decisions 2026-10-11): no trigger, no flyover, no
	 * free pause later — the fight runs in real time, the pause charges untouched (a free pause already open is
	 * released). No-op outside a sudden wave.
	 */
	inline void ReleaseHordeIntroPause(UGameFlowSubsystem* Flow)
	{
		if (!Flow)
		{
			return;
		}
		if (UHordeIntroSubsystem* Intro = Flow->GetWorld() ? Flow->GetWorld()->GetSubsystem<UHordeIntroSubsystem>() : nullptr)
		{
			Intro->DismissIntro();
		}
		Flow->CancelHordeIntro();
		if (Flow->IsFreePause())
		{
			Flow->ToggleTacticalPause();
		}
	}

	/**
	 * Starts the wave fight the way the game does and lands in WaveCombat / RealTime: the sudden horde start (the wave
	 * spawns at once; its horde intro is dismissed here, so no free pause / flyover interrupts the smoke later), or with
	 * the old prepared start
	 * (Codex.HordeIntro.SuddenStart 0, a "sudden_start": false wave) cutscene -> preparation -> wave.
	 */
	inline void StartWaveFight(UGameFlowSubsystem* Flow)
	{
		if (!Flow)
		{
			return;
		}
		Flow->TriggerCombatZone();
		Flow->FinishCutscene();
		Flow->FinishPreparation();
		ReleaseHordeIntroPause(Flow);
	}

	/**
	 * L_MovementTest's wave spawn points sit around the test start (a horde would appear on the squad): puts the squad
	 * DistanceCm from their centre (on the side of the test start), facing them, so the horde has a run-up.
	 */
	inline void PlaceSquadAwayFromWaveSpawns(UWorld* World, float DistanceCm)
	{
		USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		if (!Squad || !Squad->GetLeader())
		{
			return;
		}
		FVector Centre = FVector::ZeroVector;
		int32 Points = 0;
		for (TActorIterator<AEnemySpawnPoint> It(World); It; ++It)
		{
			if (!It->bIsDynamic)
			{
				Centre += It->GetActorLocation();
				++Points;
			}
		}
		if (Points == 0)
		{
			return;
		}
		Centre /= Points;
		FVector Away = (Squad->GetLeader()->GetActorLocation() - Centre).GetSafeNormal2D();
		Away = Away.IsNearlyZero() ? FVector(1.f, 0.f, 0.f) : Away;
		const FVector Delta = Centre + Away * DistanceCm - Squad->GetLeader()->GetActorLocation();
		for (AOperativeCharacter* Each : Squad->GetMembers())
		{
			Each->StopOperative();
			const FVector Spot = FreeSpot(World, FVector(Each->GetActorLocation().X + Delta.X, Each->GetActorLocation().Y + Delta.Y,
				Each->GetActorLocation().Z), Each);
			Each->TeleportTo(Spot, (-Away).Rotation(), false, true);
		}
	}

	inline void PlaceSquadAtTestStart(UWorld* World)
	{
		USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
		if (!Squad)
		{
			return;
		}
		TArray<AOperativeCharacter*> Members = Squad->GetMembers();
		Members.Sort([](const AOperativeCharacter& A, const AOperativeCharacter& B) { return A.SquadIndex < B.SquadIndex; });
		const FTransform Layout = LayoutTransform(World);
		const FVector Offsets[] = { FVector(0.f, 0.f, 0.f), FVector(-280.f, -260.f, 0.f), FVector(-280.f, 260.f, 0.f) };
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			AOperativeCharacter* Member = Members[Index];
			Member->StopOperative();
			const FVector Offset = Offsets[FMath::Min(Index, 2)];
			Member->TeleportTo(Layout.TransformPosition(FVector(Offset.X, Offset.Y, 100.f)), Layout.Rotator(), false, true);
		}
		Squad->SetLeaderByIndex(0);
	}
}

#endif // !UE_BUILD_SHIPPING
