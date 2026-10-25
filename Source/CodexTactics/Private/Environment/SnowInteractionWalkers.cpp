// USnowInteractionSubsystem, the walkers (interactive snow phase 1, docs/systems/environment-weather.md §14): who marks the snow,
// polled PollHz through the registries - USquadSubsystem, UEnemyRegistrySubsystem, UVehicleSubsystem, UTurnBasedCombatSubsystem,
// URelocationSubsystem (phase 3: real-time drag) - never through actor iterators (CodexTactics.Combat.EnemyRegistry.HotPathsUseRegistry scans this file). Read-only: no gameplay state
// is touched.

#include "Environment/SnowInteractionSubsystem.h"

#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/OperativeMovementRules.h"
#include "Characters/SquadSubsystem.h"
#include "CollisionQueryParams.h"
#include "Combat/EnemyRegistrySubsystem.h"
#include "Components/ModelComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Environment/SnowInteractionRules.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/RelocationSubsystem.h"
#include "SnowInteractionStats.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Vehicles/VehiclePawn.h"
#include "Vehicles/VehicleSubsystem.h"

namespace SnowInteractionWalkers
{
	const FName SnowGroundTag(TEXT("SnowGround"));
	const FName LandscapeCollisionClass(TEXT("LandscapeHeightfieldCollisionComponent"));

	/** What a floor component is (no Landscape module dependency: the collision class is matched by name up the hierarchy). */
	ESnowGroundKind Classify(const UPrimitiveComponent* Component, bool& bOutTagged)
	{
		bOutTagged = false;
		if (!Component)
		{
			return ESnowGroundKind::None;
		}
		const AActor* Owner = Component->GetOwner();
		bOutTagged = Component->ComponentHasTag(SnowGroundTag) || (Owner && Owner->ActorHasTag(SnowGroundTag));
		for (const UClass* Class = Component->GetClass(); Class; Class = Class->GetSuperClass())
		{
			if (Class->GetFName() == LandscapeCollisionClass)
			{
				return ESnowGroundKind::Landscape;
			}
		}
		const bool bMeshFloor = Component->IsA<UStaticMeshComponent>() || Component->IsA<UModelComponent>();
		if (bMeshFloor && !Component->IsSimulatingPhysics() && !(Owner && Owner->IsA<APawn>()))
		{
			return ESnowGroundKind::StaticMesh;
		}
		return ESnowGroundKind::Other;
	}
}

USnowInteractionSubsystem::FWalkerTrack& USnowInteractionSubsystem::Track(AActor& Actor, double Now)
{
	FWalkerTrack& Entry = Trackers.FindOrAdd(TObjectKey<AActor>(&Actor));
	if (!Entry.Actor.IsValid())
	{
		Entry.Actor = &Actor;
	}
	Entry.LastSeen = Now;
	return Entry;
}

bool USnowInteractionSubsystem::IsCharacterOnSnow(const ACharacter& Character) const
{
	const UCharacterMovementComponent* Movement = Character.GetCharacterMovement();
	if (!Movement || !Movement->IsMovingOnGround() || !Movement->CurrentFloor.bBlockingHit)
	{
		return false;
	}
	bool bTagged = false;
	const ESnowGroundKind Kind = SnowInteractionWalkers::Classify(Movement->CurrentFloor.HitResult.GetComponent(), bTagged);
	return SnowInteractionRules::IsSnowGround(Kind, bTagged, IsAnyGroundEnabled()) && !IsSheltered(Character.GetActorLocation());
}

bool USnowInteractionSubsystem::IsActorOverSnow(const AActor& Actor) const
{
	const FVector Location = Actor.GetActorLocation();
	if (IsSheltered(Location))
	{
		return false;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	// No CharacterMovement floor (vehicle, dragged object, a falling body): one short trace down.
	FHitResult Hit;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(SnowInteractionGround), false, &Actor);
	if (!World->LineTraceSingleByChannel(Hit, Location + FVector(0., 0., 50.), Location - FVector(0., 0., 400.), ECC_Visibility, Params))
	{
		return false;
	}
	bool bTagged = false;
	const ESnowGroundKind Kind = SnowInteractionWalkers::Classify(Hit.GetComponent(), bTagged);
	return SnowInteractionRules::IsSnowGround(Kind, bTagged, IsAnyGroundEnabled());
}

bool USnowInteractionSubsystem::IsSnowGroundHit(const FHitResult& Hit) const
{
	// A floor, not a wall / a crate's side (the ground overlay reads the RT by XY: a dent on a wall would show on the floor below it).
	if (!Hit.bBlockingHit || Hit.ImpactNormal.Z < 0.7)
	{
		return false;
	}
	bool bTagged = false;
	const ESnowGroundKind Kind = SnowInteractionWalkers::Classify(Hit.GetComponent(), bTagged);
	return SnowInteractionRules::IsSnowGround(Kind, bTagged, IsAnyGroundEnabled()) && !IsSheltered(Hit.ImpactPoint);
}

void USnowInteractionSubsystem::PollWalkers(double Now)
{
	SCOPE_CYCLE_COUNTER(STAT_SnowInteraction_Poll);
	++Metrics.Polls;
	Metrics.LastWalkers = 0;
	PollOperatives(Now);
	PollEnemies(Now);
	PollVehicles(Now);
	PollDragged(Now);
	SET_DWORD_STAT(STAT_SnowInteraction_Walkers, Metrics.LastWalkers);
	if (Now >= NextPrune)
	{
		PruneTrackers(Now);
		NextPrune = Now + 2.;
	}
}

void USnowInteractionSubsystem::PollOperatives(double Now)
{
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	if (!Squad)
	{
		return;
	}
	for (AOperativeCharacter* Operative : Squad->GetMembers())
	{
		if (!IsValid(Operative) || Operative->IsHidden())
		{
			continue;
		}
		++Metrics.LastWalkers;
		FWalkerTrack& Entry = Track(*Operative, Now);
		const FVector Location = Operative->GetActorLocation();
		const FVector2D Position(Location.X, Location.Y);
		const bool bDown = Operative->IsKnockedDown();
		const bool bOnSnow = IsCharacterOnSnow(*Operative) || (bDown && !Entry.bWasDown && IsActorOverSnow(*Operative));
		if (bDown && !Entry.bWasDown && bOnSnow)
		{
			AddImprint(Stamps.OperativeImprint, *Operative); // the body hits the snow once, on the rising edge
		}
		Entry.bWasDown = bDown;
		if (bDown || !bOnSnow)
		{
			Entry.Stride.bValid = false;
			Entry.Trail.bValid = false;
			continue;
		}
		const bool bCrawl = Operative->GetStance() == EOperativeStance::Prone || Operative->IsProneDiving();
		if (bCrawl)
		{
			if (!Entry.bWasCrawling)
			{
				Entry.Trail.bValid = false;
			}
			SnowInteractionRules::AdvanceTrail(Entry.Trail, Position, Stamps.OperativeCrawl, Tuning.MaxJumpCm, PendingStamps);
			Entry.Stride.bValid = false;
		}
		else
		{
			SnowInteractionRules::AdvanceStride(Entry.Stride, Position, Stamps.OperativeBoot, Tuning.MaxJumpCm, PendingStamps);
			Entry.Trail.bValid = false;
		}
		Entry.bWasCrawling = bCrawl;
	}
}

void USnowInteractionSubsystem::PollEnemies(double Now)
{
	for (AEnemyCharacter* Enemy : UEnemyRegistrySubsystem::GetEnemyCharacters(GetWorld()))
	{
		if (!IsValid(Enemy) || Enemy->IsDying() || Enemy->IsHidden())
		{
			continue;
		}
		const FSnowStampSpec* Spec = nullptr;
		const SnowInteractionRules::EEnemyTrack Kind = SnowInteractionRules::GetEnemyTrack(Enemy->GetArchetype(), Stamps, Spec);
		if (Kind == SnowInteractionRules::EEnemyTrack::None || !Spec)
		{
			continue;
		}
		++Metrics.LastWalkers;
		FWalkerTrack& Entry = Track(*Enemy, Now);
		const FVector Location = Enemy->GetActorLocation();
		const FVector2D Position(Location.X, Location.Y);
		const bool bDown = Enemy->IsKnockedDown();
		const bool bOnSnow = IsCharacterOnSnow(*Enemy) || (bDown && !Entry.bWasDown && IsActorOverSnow(*Enemy));
		if (bDown && !Entry.bWasDown && bOnSnow)
		{
			AddImprint(SnowInteractionRules::GetEnemyImprint(Enemy->GetArchetype(), Stamps), *Enemy);
		}
		Entry.bWasDown = bDown;
		if (bDown || !bOnSnow)
		{
			Entry.Stride.bValid = false;
			Entry.Trail.bValid = false;
			continue;
		}
		if (Kind == SnowInteractionRules::EEnemyTrack::Trail)
		{
			SnowInteractionRules::AdvanceTrail(Entry.Trail, Position, *Spec, Tuning.MaxJumpCm, PendingStamps);
		}
		else
		{
			SnowInteractionRules::AdvanceStride(Entry.Stride, Position, *Spec, Tuning.MaxJumpCm, PendingStamps);
		}
	}
}

void USnowInteractionSubsystem::PollVehicles(double Now)
{
	const UVehicleSubsystem* Vehicles = GetWorld()->GetSubsystem<UVehicleSubsystem>();
	if (!Vehicles)
	{
		return;
	}
	for (AVehiclePawn* Vehicle : Vehicles->GetVehicles())
	{
		if (!IsValid(Vehicle) || Vehicle->IsHidden())
		{
			continue;
		}
		++Metrics.LastWalkers;
		FWalkerTrack& Entry = Track(*Vehicle, Now);
		const FVector Location = Vehicle->GetActorLocation();
		const FVector Side = Vehicle->GetActorRightVector() * Tuning.RutHalfSpacingCm;
		const FVector2D Left(Location.X - Side.X, Location.Y - Side.Y);
		const FVector2D Right(Location.X + Side.X, Location.Y + Side.Y);
		// A parked APC costs nothing: the ground is only checked while its wheels turn or it has moved a rut segment.
		const bool bMoving = FMath::Abs(Vehicle->GetWheelSpinRate()) > UE_KINDA_SMALL_NUMBER || !Entry.Trail.bValid
			|| FVector2D::Distance(Entry.Trail.Last, Left) >= Stamps.VehicleRut.SpacingCm;
		if (!bMoving)
		{
			continue;
		}
		if (!IsActorOverSnow(*Vehicle))
		{
			Entry.Trail.bValid = false;
			Entry.TrailB.bValid = false;
			continue;
		}
		SnowInteractionRules::AdvanceTrail(Entry.Trail, Left, Stamps.VehicleRut, Tuning.MaxJumpCm, PendingStamps);
		SnowInteractionRules::AdvanceTrail(Entry.TrailB, Right, Stamps.VehicleRut, Tuning.MaxJumpCm, PendingStamps);
	}
}

void USnowInteractionSubsystem::PollDragged(double Now)
{
	if (const UTurnBasedCombatSubsystem* TurnBased = GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>())
	{
		if (AActor* Object = TurnBased->GetRelocatingObject())
		{
			// The object moves AFTER the relocation order cleared it: keep watching it for a while.
			Track(*Object, Now).DragUntil = Now + Tuning.DragTrackSeconds;
		}
	}
	// Phase 3: the objects a real-time relocation pushes right now (the same drag trail; watched DragTrackSeconds after the push too).
	MovingScratch.Reset();
	if (const URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>())
	{
		Relocation->GetMovingObjects(MovingScratch);
	}
	for (const AInteractableActor* Moving : MovingScratch)
	{
		if (IsValid(Moving))
		{
			// Read-only: the tracker keeps a weak pointer and only reads the location.
			Track(*const_cast<AInteractableActor*>(Moving), Now).DragUntil = Now + Tuning.DragTrackSeconds;
		}
	}
	Metrics.LastMovingObjects = MovingScratch.Num();
	MovingScratch.Reset();
	for (TPair<TObjectKey<AActor>, FWalkerTrack>& Pair : Trackers)
	{
		FWalkerTrack& Entry = Pair.Value;
		AActor* Object = Entry.DragUntil > Now ? Entry.Actor.Get() : nullptr;
		if (!Object)
		{
			continue;
		}
		Entry.LastSeen = Now;
		const FVector Location = Object->GetActorLocation();
		const FVector2D Position(Location.X, Location.Y);
		if (Entry.Trail.bValid && FVector2D::Distance(Entry.Trail.Last, Position) < Stamps.DragTrail.SpacingCm)
		{
			continue; // not moved: no trace
		}
		if (!IsActorOverSnow(*Object))
		{
			Entry.Trail.Last = Position;
			Entry.Trail.bValid = true;
			continue;
		}
		Metrics.DragSegments += SnowInteractionRules::AdvanceTrail(Entry.Trail, Position, Stamps.DragTrail, Tuning.MaxDragCm, PendingStamps);
	}
}

void USnowInteractionSubsystem::PruneTrackers(double Now)
{
	for (auto It = Trackers.CreateIterator(); It; ++It)
	{
		const FWalkerTrack& Entry = It.Value();
		if (!Entry.Actor.IsValid() || (Now - Entry.LastSeen > 5. && Entry.DragUntil <= Now))
		{
			It.RemoveCurrent();
		}
	}
}
