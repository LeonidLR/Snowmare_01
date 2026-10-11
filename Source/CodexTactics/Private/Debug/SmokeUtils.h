#pragma once

// Shared helpers of the dev smoke commands (not shipped).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Interactables/GateActor.h"
#include "Engine/StaticMeshActor.h"
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
	 * Releases the free tactical pause a sudden horde wave opens with (user decision 2026-10-11; it also ends the camera
	 * flyover): the fight runs in real time, the pause charges untouched. No-op outside that pause.
	 */
	inline void ReleaseHordeIntroPause(UGameFlowSubsystem* Flow)
	{
		if (Flow && Flow->IsFreePause())
		{
			Flow->ToggleTacticalPause();
		}
	}

	/**
	 * Starts the wave fight the way the game does and lands in WaveCombat / RealTime: the sudden horde start (the wave
	 * spawns at once into the free tactical pause, which is released here), or with the old prepared start
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
