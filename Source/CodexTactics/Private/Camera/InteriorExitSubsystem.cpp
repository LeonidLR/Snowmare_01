#include "Camera/InteriorExitSubsystem.h"
#include "Camera/BuildingExitMarker.h"
#include "Camera/CameraZoneVolume.h"
#include "Camera/InteriorExitArrowActor.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadFormation.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/HealthComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/InteractionSubsystem.h"
#include "NavigationSystem.h"
#include "UI/GameMessageSubsystem.h"
#include "Vehicles/VehicleSubsystem.h"

#define LOCTEXT_NAMESPACE "InteriorExit"

namespace InteriorExitScan
{
	/** A marker without a zone belongs to the zone whose box, grown by this much, contains it, cm. */
	constexpr float MarkerZoneSlackCm = 200.f;

	bool ProjectNear(UNavigationSystemV1* Nav, const FVector& Point, float Tolerance2D, FVector& Out)
	{
		FNavLocation OnNav;
		if (!Nav || !Nav->ProjectPointToNavigation(Point, OnNav, FVector(Tolerance2D, Tolerance2D, 250.f)))
		{
			return false;
		}
		if (FVector::Dist2D(OnNav.Location, Point) > Tolerance2D)
		{
			return false;
		}
		Out = OnNav.Location;
		return true;
	}

	/** Where the exit walk ends: Distance out of the door on the navmesh (shorter when that spot is off it). */
	FVector FindExitTarget(UNavigationSystemV1* Nav, const FVector& Door, const FVector& Outward, float Distance)
	{
		for (const float Scale : { 1.f, 0.75f, 0.5f, 1.25f })
		{
			FVector OnNav;
			if (ProjectNear(Nav, Door + Outward * Distance * Scale, 60.f, OnNav))
			{
				return OnNav;
			}
		}
		return Door + Outward * Distance;
	}
}

bool UInteriorExitSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UInteriorExitSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UInteriorExitSubsystem, STATGROUP_Tickables);
}

void UInteriorExitSubsystem::Deinitialize()
{
	ClearArrows();
	Super::Deinitialize();
}

ACameraZoneVolume* UInteriorExitSubsystem::GetLeaderZone() const
{
	for (TActorIterator<ACameraZoneVolume> It(GetWorld()); It; ++It)
	{
		if (It->IsZoneActive())
		{
			return *It;
		}
	}
	return nullptr;
}

void UInteriorExitSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	ACameraZoneVolume* Shown = nullptr;
	for (TActorIterator<ACameraZoneVolume> It(World); It; ++It)
	{
		if (It->IsZoneCameraShown())
		{
			Shown = *It;
			break;
		}
	}
	const UGameFlowSubsystem* Flow = World->GetSubsystem<UGameFlowSubsystem>();
	const bool bTurnBased = Flow && Flow->GetCombatMode() == ECodexCombatMode::TurnBased;
	if (!InteriorExitRules::ShouldShowExitArrows(Shown != nullptr, bTurnBased))
	{
		if (ShownZone.IsValid() || !Arrows.IsEmpty())
		{
			ClearArrows();
		}
		return;
	}
	if (ShownZone.Get() != Shown)
	{
		ClearArrows();
		ShowArrows(Shown);
	}
}

TArray<FInteriorExit> UInteriorExitSubsystem::FindExits(ACameraZoneVolume* Zone, const FVector& FloorHint) const
{
	TArray<FInteriorExit> Exits;
	UWorld* World = GetWorld();
	if (!Zone || !World)
	{
		return Exits;
	}
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const FTransform ZoneTransform = Zone->GetZoneTransform();
	const FVector HalfExtent = Zone->GetZoneHalfExtent();

	// Placed markers win (the user's own doors).
	for (TActorIterator<ABuildingExitMarker> It(World); It; ++It)
	{
		const FVector Local = ZoneTransform.InverseTransformPosition(It->GetActorLocation());
		const bool bInside = FMath::Abs(Local.X) <= HalfExtent.X + InteriorExitScan::MarkerZoneSlackCm
			&& FMath::Abs(Local.Y) <= HalfExtent.Y + InteriorExitScan::MarkerZoneSlackCm
			&& FMath::Abs(Local.Z) <= HalfExtent.Z + 300.f;
		if (It->Zone ? It->Zone != Zone : !bInside)
		{
			continue;
		}
		FInteriorExit& Exit = Exits.AddDefaulted_GetRef();
		Exit.bFromMarker = true;
		Exit.Outward = It->GetOutwardDirection();
		FVector OnNav;
		Exit.DoorPoint = InteriorExitScan::ProjectNear(Nav, It->GetActorLocation(), 150.f, OnNav) ? OnNav : It->GetActorLocation();
		Exit.ExitTarget = InteriorExitScan::FindExitTarget(Nav, Exit.DoorPoint, Exit.Outward, It->ExitDistanceCm);
	}
	if (!Exits.IsEmpty() || !Nav)
	{
		return Exits;
	}

	// Automatic: where the navmesh crosses the zone's box.
	const InteriorExitRules::FExitScanConfig& Config = ScanConfig;
	for (int32 Edge = 0; Edge < InteriorExitRules::EdgeCount; ++Edge)
	{
		TArray<FVector2D> Samples;
		FVector2D LocalNormal;
		InteriorExitRules::BuildEdgeSamples(FVector2D(HalfExtent.X, HalfExtent.Y), Edge, Config, Samples, LocalNormal);
		const FVector Outward = ZoneTransform.TransformVectorNoScale(FVector(LocalNormal, 0.f)).GetSafeNormal2D();
		TArray<bool> Crossable;
		TArray<FVector> EdgePoints;
		for (const FVector2D& Sample : Samples)
		{
			FVector Point = ZoneTransform.TransformPosition(FVector(Sample, 0.f));
			Point.Z = FloorHint.Z;
			EdgePoints.Add(Point);
			FVector Inside;
			FVector Outside;
			FVector Hit;
			const bool bOpen = InteriorExitScan::ProjectNear(Nav, Point - Outward * Config.InsideDepthCm, Config.ProjectToleranceCm, Inside)
				&& InteriorExitScan::ProjectNear(Nav, Point + Outward * Config.OutsideDepthCm, Config.ProjectToleranceCm, Outside)
				&& !Nav->NavigationRaycast(World, Inside, Outside, Hit);
			Crossable.Add(bOpen);
		}
		for (const InteriorExitRules::FDoorRun& Run : InteriorExitRules::FindDoorRuns(Crossable, Config.SampleSpacingCm, Config.MinDoorWidthCm))
		{
			FInteriorExit& Exit = Exits.AddDefaulted_GetRef();
			Exit.Outward = Outward;
			Exit.WidthCm = Run.WidthCm;
			const FVector Centre = (EdgePoints[Run.First] + EdgePoints[Run.Last]) * 0.5f;
			FVector OnNav;
			Exit.DoorPoint = InteriorExitScan::ProjectNear(Nav, Centre, 100.f, OnNav) ? OnNav : Centre;
			Exit.ExitTarget = InteriorExitScan::FindExitTarget(Nav, Exit.DoorPoint, Outward, Config.ExitDistanceCm);
		}
	}
	return Exits;
}

void UInteriorExitSubsystem::ShowArrows(ACameraZoneVolume* Zone)
{
	ShownZone = Zone;
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	const AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	const FVector FloorHint = Leader ? Leader->GetActorLocation() : Zone->GetActorLocation();
	const TArray<FInteriorExit> Exits = FindExits(Zone, FloorHint);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const FInteriorExit& Exit : Exits)
	{
		// Just inside the opening, so the interior camera sees it.
		const FVector At = Exit.bFromMarker ? Exit.DoorPoint : Exit.DoorPoint - Exit.Outward * 30.f;
		if (AInteriorExitArrowActor* Arrow = GetWorld()->SpawnActor<AInteriorExitArrowActor>(At, Exit.Outward.Rotation(), Params))
		{
			Arrow->Setup(Zone, At, Exit.Outward, Exit.ExitTarget);
			Arrows.Add(Arrow);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[InteriorExit] %s: door at (%.0f, %.0f) %s, %.0f cm wide, out (%.2f, %.2f) -> (%.0f, %.0f)"),
			*Zone->GetName(), Exit.DoorPoint.X, Exit.DoorPoint.Y, Exit.bFromMarker ? TEXT("marker") : TEXT("auto"), Exit.WidthCm,
			Exit.Outward.X, Exit.Outward.Y, Exit.ExitTarget.X, Exit.ExitTarget.Y);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[InteriorExit] %s: interior camera on, %d EXIT arrow(s)"), *Zone->GetName(), Exits.Num());
}

void UInteriorExitSubsystem::ClearArrows()
{
	for (const TWeakObjectPtr<AInteriorExitArrowActor>& Arrow : Arrows)
	{
		if (Arrow.IsValid())
		{
			Arrow->Destroy();
		}
	}
	Arrows.Reset();
	ShownZone.Reset();
}

TArray<AInteriorExitArrowActor*> UInteriorExitSubsystem::GetArrows() const
{
	TArray<AInteriorExitArrowActor*> Result;
	for (const TWeakObjectPtr<AInteriorExitArrowActor>& Arrow : Arrows)
	{
		if (Arrow.IsValid())
		{
			Result.Add(Arrow.Get());
		}
	}
	return Result;
}

void UInteriorExitSubsystem::SetHoveredArrow(AInteriorExitArrowActor* Hovered)
{
	for (AInteriorExitArrowActor* Arrow : GetArrows())
	{
		Arrow->SetHovered(Arrow == Hovered);
	}
}

int32 UInteriorExitSubsystem::OrderExit(AInteriorExitArrowActor* Arrow)
{
	UWorld* World = GetWorld();
	USquadSubsystem* Squad = World ? World->GetSubsystem<USquadSubsystem>() : nullptr;
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Arrow || !Leader)
	{
		return 0;
	}
	const InteriorExitRules::EExitOrderScope Scope = InteriorExitRules::GetExitOrderScope(Squad->IsSoloMode());
	TArray<AOperativeCharacter*> Group = Scope == InteriorExitRules::EExitOrderScope::Selected ? Squad->GetSelectedGroup() : Squad->GetMembers();
	Group.RemoveAll([](const AOperativeCharacter* Member)
	{
		return !Member || !Member->HealthComponent || !Member->HealthComponent->IsAlive() || VehicleQueries::IsMounted(Member);
	});
	if (Group.Contains(Leader))
	{
		Group.Remove(Leader);
		Group.Insert(Leader, 0);
	}
	if (Group.IsEmpty())
	{
		return 0;
	}
	FVector Average = FVector::ZeroVector;
	for (const AOperativeCharacter* Member : Group)
	{
		Average += Member->GetActorLocation() / Group.Num();
	}
	const TArray<FVector> Targets = SquadFormation::ComputeGroupTargets(Arrow->GetExitTarget(), Average, Arrow->GetOutward(), Group.Num());
	UInteractionSubsystem* UseOrders = World->GetSubsystem<UInteractionSubsystem>();
	UVehicleSubsystem* Vehicles = World->GetSubsystem<UVehicleSubsystem>();
	for (int32 Index = 0; Index < Group.Num(); ++Index)
	{
		if (UseOrders)
		{
			UseOrders->CancelUseOrder(Group[Index]);
		}
		if (Vehicles)
		{
			Vehicles->CancelBoarding(Group[Index]);
		}
		Group[Index]->OrderMoveTo(Targets.IsValidIndex(Index) ? Targets[Index] : Arrow->GetExitTarget(), false);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[InteriorExit] %d operative(s) walk out to (%.0f, %.0f) (%s)"), Group.Num(),
		Arrow->GetExitTarget().X, Arrow->GetExitTarget().Y, Scope == InteriorExitRules::EExitOrderScope::Selected ? TEXT("solo: selected") : TEXT("squad"));
	if (UGameMessageSubsystem* Messages = World->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(LOCTEXT("Speaker", "SQUAD"), Group.Num() == 1
			? FText::Format(LOCTEXT("ExitOne", "🚪 {0} heads out through the door."), Group[0]->DisplayName)
			: FText::Format(LOCTEXT("ExitGroup", "🚪 Squad ({0}) heading out through the door."), Group.Num()));
	}
	return Group.Num();
}

bool UInteriorExitSubsystem::ToggleInteriorView(bool& bOutOutside)
{
	ACameraZoneVolume* Zone = GetLeaderZone();
	if (!Zone || !Zone->TargetCamera)
	{
		bOutOutside = false;
		return false;
	}
	bOutOutside = Zone->TogglePlayerOutsideView();
	return true;
}

#undef LOCTEXT_NAMESPACE
