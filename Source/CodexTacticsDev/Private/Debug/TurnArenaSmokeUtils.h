// Dev-only helpers of the turn-based arena smokes (TurnArenaLakeSmoke, TurnArenaYardSmoke): the lake compound's buildings found from the
// world (the generator shed, the base building's indoor camera zone), open spots, squad placement, independent world probes.
#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Camera/CameraZoneVolume.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/BarricadeActor.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/TurretActor.h"
#include "NavigationSystem.h"

namespace TurnArenaSmokeUtils
{
	/** Probes ignore the squad, the enemies, barrels, barricades and turrets (the arena does too). */
	inline FCollisionQueryParams OpenGroundParams(UWorld* World)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TurnArenaSmokeProbe), false);
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
	inline bool GroundAt(UWorld* World, const FVector& XY, float RefZ, const FCollisionQueryParams& Params, float& OutZ)
	{
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, FVector(XY.X, XY.Y, RefZ + 250.f), FVector(XY.X, XY.Y, RefZ - 400.f), ECC_Visibility, Params))
		{
			return false;
		}
		OutZ = static_cast<float>(Hit.ImpactPoint.Z);
		return true;
	}

	/** A static mesh 1.8 .. 4.5 m over the ground at XY (a roof; object types - a cut roof ignores Visibility). */
	inline bool IsRoofed(UWorld* World, const FVector& XY, float GroundZ, const FCollisionQueryParams& Params)
	{
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		TArray<FHitResult> Hits;
		World->LineTraceMultiByObjectType(Hits, FVector(XY.X, XY.Y, GroundZ + 180.f), FVector(XY.X, XY.Y, GroundZ + 450.f), Objects, Params);
		return Hits.ContainsByPredicate([](const FHitResult& Hit) { return Hit.GetComponent() && Hit.GetComponent()->IsA<UStaticMeshComponent>(); });
	}

	/**
	 * The nearest navmesh spot to Point (rings up to MaxRadius) where a standing capsule is not BLOCKED (inside the yard's tutorial
	 * volume every spot "overlaps"). Capsule centre height.
	 */
	inline FVector OpenSpot(UWorld* World, const FVector& Point, const AActor* Ignore, float MaxRadius = 400.f)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(TurnArenaSmokeSpot), false, Ignore);
		for (const float Radius : { 0.f, 80.f, 160.f, 250.f, 400.f })
		{
			if (Radius > MaxRadius)
			{
				break;
			}
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

	/** Puts Member at Spot (capsule centre) facing Yaw, stopped. */
	inline void PlaceAt(AOperativeCharacter* Member, const FVector& Spot, float Yaw)
	{
		Member->StopOperative();
		Member->TeleportTo(FVector(Spot.X, Spot.Y, Spot.Z + 4.f), FRotator(0.f, Yaw, 0.f), false, true);
	}

	/** The compound's generator (the quest's, in its shed). */
	inline AInteractableActor* FindGenerator(UWorld* World)
	{
		for (TActorIterator<AInteractableActor> It(World); It; ++It)
		{
			if (It->ObjectType == EInteractableType::Generator)
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** The generator shed: the union (XY) of the tall static meshes within 3.5 m of the generator (its walls). */
	inline FBox2D FindShedBox(UWorld* World, const AActor* Generator)
	{
		FBox2D Box(ForceInit);
		if (!Generator)
		{
			return Box;
		}
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			FVector Origin, Extent;
			It->GetActorBounds(true, Origin, Extent);
			// Its own walls only: tall, short (not the compound's 40 m perimeter wall behind it), close to the generator.
			if (Extent.Z >= 90.f && FMath::Max(Extent.X, Extent.Y) <= 300.f && FVector::Dist2D(Origin, Generator->GetActorLocation()) < 350.f
				&& It->GetStaticMeshComponent() && It->GetStaticMeshComponent()->IsCollisionEnabled())
			{
				Box += FVector2D(Origin - Extent);
				Box += FVector2D(Origin + Extent);
			}
		}
		return Box;
	}

	/** The building with an indoor (Closed / Shelter) camera zone of at least 6 x 6 m nearest to Near (the lake's PMC base). */
	inline ACameraZoneVolume* FindBaseZone(UWorld* World, const FVector& Near)
	{
		ACameraZoneVolume* Best = nullptr;
		for (TActorIterator<ACameraZoneVolume> It(World); It; ++It)
		{
			const FVector Extent = It->GetZoneHalfExtent();
			if ((It->Environment == ECameraZoneEnvironment::Closed || It->Environment == ECameraZoneEnvironment::Shelter) && Extent.X >= 300.f && Extent.Y >= 300.f
				&& (!Best || FVector::Dist2D(It->GetActorLocation(), Near) < FVector::Dist2D(Best->GetActorLocation(), Near)))
			{
				Best = *It;
			}
		}
		return Best;
	}

	/** Point lies inside the zone's box (XY) grown by Margin. */
	inline bool InsideZone(const ACameraZoneVolume* Zone, const FVector& Point, float Margin)
	{
		const FTransform Transform = Zone->GetZoneTransform();
		const FVector Local = Transform.InverseTransformPosition(FVector(Point.X, Point.Y, Transform.GetLocation().Z));
		const FVector Extent = Zone->GetZoneHalfExtent();
		return FMath::Abs(Local.X) <= Extent.X + Margin && FMath::Abs(Local.Y) <= Extent.Y + Margin;
	}
}

#endif // !UE_BUILD_SHIPPING
