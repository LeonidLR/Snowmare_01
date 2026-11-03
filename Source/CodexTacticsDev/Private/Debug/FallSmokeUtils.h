#pragma once

// Shared helpers of the fall smokes / shots (FallDropSmoke, FallDropShots; 2026-11-03, docs/systems/stances-and-movement.md "Falls and drops"):
// a test platform spawned at run time for the 9-12 m heavy-wound and the 12 m+ lethal falls (L_Gen_Depot_1 has no roof that high), and a
// walk-off push off its edge. Header-only, inline (unity builds: never copy these into two .cpp files).

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"

namespace FallSmokeUtils
{
	/** The platform's size (cm): a 3 x 3 m slab, 20 cm thick. */
	constexpr float PlatformSizeCm = 300.f;
	constexpr float PlatformThicknessCm = 20.f;

	/**
	 * Open ground near one of Candidates (XY): the floor within 40 cm of GroundZ under it, nothing above it up to MaxHeightCm in a 3.5 m wide
	 * column, and the landing zone 3-6 m out along +X open too.
	 * OutFeet = the floor point; false when none qualifies.
	 */
	inline bool FindOpenGround(UWorld& World, const TArray<FVector2D>& Candidates, float GroundZ, float MaxHeightCm, FVector& OutFeet)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FallSmokeOpenGround), false);
		for (TActorIterator<APawn> It(&World); It; ++It)
		{
			Params.AddIgnoredActor(*It);
		}
		for (const FVector2D& XY : Candidates)
		{
			// Plumb lines (the centre, the slab's corners 1.75 m out and the landing zone up to 6 m out along +X - the push direction) from above
			// the platform down to the ground: each must reach the ground (within 40 cm of GroundZ) without hitting anything else.
			bool bClear = true;
			FVector Feet = FVector::ZeroVector;
			for (const FVector2D& Offset : { FVector2D(0.f, 0.f), FVector2D(175.f, 175.f), FVector2D(-175.f, 175.f), FVector2D(175.f, -175.f), FVector2D(-175.f, -175.f),
				FVector2D(300.f, 0.f), FVector2D(400.f, 120.f), FVector2D(400.f, -120.f), FVector2D(500.f, 0.f), FVector2D(600.f, 120.f), FVector2D(600.f, -120.f) })
			{
				const FVector2D At = XY + Offset;
				FHitResult Floor;
				const bool bHit = World.LineTraceSingleByChannel(Floor, FVector(At.X, At.Y, GroundZ + MaxHeightCm + 300.f), FVector(At.X, At.Y, GroundZ - 300.f), ECC_Pawn, Params);
				if (!bHit || FMath::Abs(Floor.ImpactPoint.Z - GroundZ) > 40.f)
				{
					UE_LOG(LogTemp, Display, TEXT("FallSmokeUtils: (%.0f, %.0f) not open - %s at z %.0f"), At.X, At.Y, bHit ? *GetNameSafe(Floor.GetActor()) : TEXT("nothing"),
						bHit ? Floor.ImpactPoint.Z : 0.f);
					bClear = false;
					break;
				}
				if (Offset.IsZero())
				{
					Feet = Floor.ImpactPoint;
				}
			}
			if (bClear)
			{
				OutFeet = Feet;
				return true;
			}
		}
		return false;
	}

	/** A 3 x 3 m slab whose top lies HeightCm above Feet (BasicShapes cube, blocks pawns). */
	inline AStaticMeshActor* SpawnPlatform(UWorld& World, const FVector& Feet, float HeightCm)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (!Cube)
		{
			return nullptr;
		}
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector Centre = Feet + FVector(0.f, 0.f, HeightCm - PlatformThicknessCm * 0.5f);
		AStaticMeshActor* Platform = World.SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Centre, FRotator::ZeroRotator, Params);
		if (!Platform)
		{
			return nullptr;
		}
		UStaticMeshComponent* Mesh = Platform->GetStaticMeshComponent();
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(Cube);
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		Platform->SetActorScale3D(FVector(PlatformSizeCm / 100.f, PlatformSizeCm / 100.f, PlatformThicknessCm / 100.f));
		return Platform;
	}

	/** Puts Body on the platform's edge (facing out along Dir) and pushes him off it: a walk-off fall ULedgeFallComponent judges on landing. */
	inline void StandOnPlatformEdge(ACharacter& Body, const AStaticMeshActor& Platform, const FVector& Dir)
	{
		const FVector Top = Platform.GetActorLocation() + FVector(0.f, 0.f, PlatformThicknessCm * 0.5f);
		const float HalfHeight = Body.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		Body.TeleportTo(Top + Dir * (PlatformSizeCm * 0.5f - 45.f) + FVector(0.f, 0.f, HalfHeight + 3.f), Dir.Rotation(), false, true);
		if (AAIController* AI = Cast<AAIController>(Body.GetController()))
		{
			AI->StopMovement();
		}
		Body.GetCharacterMovement()->StopMovementImmediately();
	}

	/** Pushes him off the edge along Dir with a small hop (the walk-off flag on; restore it after the landing). Returns the flag he had. */
	inline bool PushOffEdge(ACharacter& Body, const FVector& Dir)
	{
		UCharacterMovementComponent* Movement = Body.GetCharacterMovement();
		const bool bHad = Movement->bCanWalkOffLedges;
		Movement->bCanWalkOffLedges = true;
		// A small hop out (under the ledge fall's 2.25 g: 0.27 s back to the slab's height, 0.95 m out): clear of the slab before he comes down
		// (+20 cm on the fall height); he lands some 4-5 m out (FindOpenGround checks that zone along +X).
		Body.LaunchCharacter(Dir * 350.f + FVector(0.f, 0.f, 300.f), true, true);
		return bHad;
	}

	/** Candidate open spots of L_Gen_Depot_1 (the yard / street areas; checked at run time). */
	inline TArray<FVector2D> DepotOpenSpots()
	{
		return { FVector2D(-2400.f, -6400.f), FVector2D(-2200.f, -4800.f), FVector2D(-900.f, -5000.f), FVector2D(2400.f, -5600.f), FVector2D(3200.f, -4600.f),
			FVector2D(-5200.f, -3200.f), FVector2D(1500.f, 4600.f), FVector2D(-1500.f, -2500.f), FVector2D(1600.f, -6400.f), FVector2D(-5300.f, 1500.f) };
	}
}

#endif // !UE_BUILD_SHIPPING
