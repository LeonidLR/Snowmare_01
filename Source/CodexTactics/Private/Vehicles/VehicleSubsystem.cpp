#include "Vehicles/VehicleSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "Core/PlayerCommands.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Interactables/InteractionSubsystem.h"
#include "Interactables/RelocationSubsystem.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Tactics/CoverTraceRules.h"
#include "UI/GameMessageSubsystem.h"
#include "Vehicles/VehicleNavigation.h"
#include "Vehicles/VehiclePawn.h"

#define LOCTEXT_NAMESPACE "CodexVehicles"

namespace VehicleSubsystemLocal
{
	bool IsAliveOperative(const AOperativeCharacter* Operative)
	{
		return Operative && Operative->HealthComponent && Operative->HealthComponent->IsAlive();
	}

	void ModeFlags(const UWorld* World, bool& bOutTurnBased, bool& bOutPause)
	{
		const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
		const ECodexCombatMode Mode = Flow ? Flow->GetCombatMode() : ECodexCombatMode::None;
		bOutTurnBased = Mode == ECodexCombatMode::TurnBased;
		bOutPause = Mode == ECodexCombatMode::TacticalPause;
	}
}

bool UVehicleSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UVehicleSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UVehicleSubsystem, STATGROUP_Tickables);
}

void UVehicleSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	StripVehicleAgentFromBounds();
}

// --- Registry ----------------------------------------------------------------------------------------------------

void UVehicleSubsystem::RegisterVehicle(AVehiclePawn* Vehicle)
{
	if (!Vehicle || Vehicles.Contains(Vehicle))
	{
		return;
	}
	Vehicles.Add(Vehicle);
	Vehicle->OnArrived.AddUObject(this, &UVehicleSubsystem::HandleArrived);
	Vehicle->OnDriveBlocked.AddUObject(this, &UVehicleSubsystem::HandleBlocked);
}

void UVehicleSubsystem::UnregisterVehicle(AVehiclePawn* Vehicle)
{
	Vehicles.Remove(Vehicle);
	for (auto It = Mounted.CreateIterator(); It; ++It)
	{
		if (It.Value() == Vehicle)
		{
			It.RemoveCurrent();
		}
	}
}

TArray<AVehiclePawn*> UVehicleSubsystem::GetVehicles() const
{
	TArray<AVehiclePawn*> Result;
	for (const TWeakObjectPtr<AVehiclePawn>& Vehicle : Vehicles)
	{
		if (Vehicle.IsValid())
		{
			Result.Add(Vehicle.Get());
		}
	}
	return Result;
}

bool UVehicleSubsystem::IsMounted(const AActor* Operative) const
{
	return Operative && !Mounted.IsEmpty() && Mounted.Contains(TWeakObjectPtr<const AActor>(Operative));
}

AVehiclePawn* UVehicleSubsystem::GetVehicleOf(const AActor* Operative) const
{
	const TWeakObjectPtr<AVehiclePawn>* Vehicle = Operative ? Mounted.Find(TWeakObjectPtr<const AActor>(Operative)) : nullptr;
	return Vehicle ? Vehicle->Get() : nullptr;
}

void UVehicleSubsystem::CancelBoarding(const AOperativeCharacter* Operative)
{
	Boarding.RemoveAll([Operative](const FPendingBoard& Each) { return Each.Operative.Get() == Operative && Each.ClimbLeft < 0.f; });
}

bool UVehicleSubsystem::IsBoarding(const AOperativeCharacter* Operative) const
{
	return Boarding.ContainsByPredicate([Operative](const FPendingBoard& Each) { return Each.Operative.Get() == Operative; });
}

bool UVehicleSubsystem::IsDisembarking(const AOperativeCharacter* Operative) const
{
	return Exits.ContainsByPredicate([Operative](const FPendingExit& Each) { return Each.Operative.Get() == Operative; });
}

AVehiclePawn* UVehicleSubsystem::FindNearestVehicle(const FVector& Location, float MaxDistanceCm) const
{
	AVehiclePawn* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (AVehiclePawn* Vehicle : GetVehicles())
	{
		const float Distance = FVector::Dist2D(Location, Vehicle->GetHullCentreWorld());
		if ((MaxDistanceCm < 0.f || Distance <= MaxDistanceCm) && Distance < BestDistance)
		{
			Best = Vehicle;
			BestDistance = Distance;
		}
	}
	return Best;
}

// --- Messages ----------------------------------------------------------------------------------------------------

void UVehicleSubsystem::PostLine(const FText& Speaker, const FText& Text) const
{
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(Speaker, Text);
	}
}

FText UVehicleSubsystem::RefusalText(EVehicleRefusal Refusal, const AOperativeCharacter* Operative) const
{
	const FText Name = Operative ? Operative->DisplayName : LOCTEXT("Operative", "Operative");
	switch (Refusal)
	{
	case EVehicleRefusal::TurnBased:
		return LOCTEXT("RefuseTurnBased", "Not in turn-based combat: the APC sits this fight out for now.");
	case EVehicleRefusal::TacticalPause:
		return LOCTEXT("RefusePause", "Vehicle orders run in real time only: release the pause [SPACE].");
	case EVehicleRefusal::KnockedDown:
		return FText::Format(LOCTEXT("RefuseDown", "{0} is down and can't climb in."), Name);
	case EVehicleRefusal::TooFar:
		return LOCTEXT("RefuseFar", "No vehicle within 30 m.");
	case EVehicleRefusal::NoSeat:
		return FText::Format(LOCTEXT("RefuseSeat", "No seat left for {0}."), Name);
	case EVehicleRefusal::VehicleMoving:
		return LOCTEXT("RefuseMoving", "The APC is moving: stop it first.");
	case EVehicleRefusal::NoDriver:
		return LOCTEXT("RefuseDriver", "Nobody at the wheel.");
	case EVehicleRefusal::NoExit:
		return LOCTEXT("RefuseExit", "All exits are blocked!");
	default:
		return FText::GetEmpty();
	}
}

// --- Orders ------------------------------------------------------------------------------------------------------

EVehicleKeyAction UVehicleSubsystem::HandleBoardKey(bool bSquadWide, AVehiclePawn* Preferred)
{
	USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Leader)
	{
		return EVehicleKeyAction::None;
	}
	TArray<AOperativeCharacter*> Addressed = bSquadWide ? Squad->GetMembers()
		: (Squad->HasMultiSelection() ? Squad->GetSelectedGroup() : TArray<AOperativeCharacter*>{ Leader });
	Addressed.RemoveAll([](const AOperativeCharacter* Each) { return !VehicleSubsystemLocal::IsAliveOperative(Each); });
	const int32 MountedCount = Addressed.FilterByPredicate([this](const AOperativeCharacter* Each) { return IsMounted(Each); }).Num();
	const EVehicleKeyAction Action = VehicleRules::ResolveKeyAction(Addressed.Num(), MountedCount);
	FPlayerCommandContext Context;
	Context.Operative = Leader;
	Context.bSquadWide = bSquadWide;
	if (Action == EVehicleKeyAction::Disembark)
	{
		const AVehiclePawn* Vehicle = GetVehicleOf(Leader) ? GetVehicleOf(Leader) : GetVehicleOf(Addressed[0]);
		if (OrderDisembark(Addressed) > 0)
		{
			Context.Target = const_cast<AVehiclePawn*>(Vehicle);
			Context.Location = Vehicle ? Vehicle->GetActorLocation() : FVector::ZeroVector;
			PlayerCommands::Report(this, PlayerCommands::VehicleDisembark, Context);
		}
	}
	else if (Action == EVehicleKeyAction::Board)
	{
		TArray<AOperativeCharacter*> Outside = Addressed.FilterByPredicate([this](const AOperativeCharacter* Each) { return !IsMounted(Each); });
		// The squad boards the vehicle part of it is already in; else the hovered / nearest one.
		AVehiclePawn* Target = Preferred;
		for (const AOperativeCharacter* Each : Addressed)
		{
			Target = Target ? Target : GetVehicleOf(Each);
		}
		if (OrderBoard(Outside, Target) > 0)
		{
			const AVehiclePawn* Boarded = Boarding.Num() > 0 ? Boarding.Last().Vehicle.Get() : Target;
			Context.Target = const_cast<AVehiclePawn*>(Boarded);
			Context.Location = Boarded ? Boarded->GetActorLocation() : FVector::ZeroVector;
			PlayerCommands::Report(this, PlayerCommands::VehicleBoard, Context);
		}
	}
	return Action;
}

int32 UVehicleSubsystem::OrderBoard(const TArray<AOperativeCharacter*>& Operatives, AVehiclePawn* Vehicle)
{
	if (Operatives.IsEmpty())
	{
		return 0;
	}
	bool bTurnBased = false;
	bool bPause = false;
	VehicleSubsystemLocal::ModeFlags(GetWorld(), bTurnBased, bPause);
	// The vehicle: the given one, else the nearest to the group within the board range.
	FVector Centre = FVector::ZeroVector;
	for (const AOperativeCharacter* Operative : Operatives)
	{
		Centre += Operative->GetActorLocation();
	}
	Centre /= Operatives.Num();
	AVehiclePawn* Target = Vehicle;
	if (!Target)
	{
		Target = FindNearestVehicle(Centre, -1.f);
	}
	if (!Target)
	{
		PostLine(LOCTEXT("HQ", "HQ"), LOCTEXT("NoVehicle", "No vehicle within 30 m."));
		return 0;
	}
	int32 FreeSeats = Target->GetFreeSeats();
	for (const FPendingBoard& Pending : Boarding)
	{
		FreeSeats -= Pending.Vehicle.Get() == Target ? 1 : 0;
	}
	UInteractionSubsystem* Interactions = GetWorld()->GetSubsystem<UInteractionSubsystem>();
	URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>();
	int32 Sent = 0;
	bool bRefusalPosted = false;
	for (AOperativeCharacter* Operative : Operatives)
	{
		if (!Operative || IsBoarding(Operative))
		{
			continue;
		}
		// The nearest door (side doors, then the rear hatch).
		int32 Entry = 0;
		float EntryDistance = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Target->EntryPoints.Num(); ++Index)
		{
			const float Distance = FVector::Dist2D(Operative->GetActorLocation(), Target->GetEntryPointWorld(Index));
			if (Distance < EntryDistance)
			{
				EntryDistance = Distance;
				Entry = Index;
			}
		}
		VehicleRules::FBoardCheck Check;
		Check.bAlive = VehicleSubsystemLocal::IsAliveOperative(Operative);
		Check.bKnockedDown = Operative->IsKnockedDown();
		Check.bMounted = IsMounted(Operative);
		Check.DistanceCm = EntryDistance;
		Check.VehicleSpeedCmS = Target->IsDriving() ? Target->CrewConfig.StoppedSpeedCmS * 2.f : Target->GetSpeed();
		Check.FreeSeats = FreeSeats;
		Check.bTurnBased = bTurnBased;
		Check.bTacticalPause = bPause;
		const EVehicleRefusal Refusal = VehicleRules::CanBoard(Check, Target->CrewConfig);
		if (Refusal != EVehicleRefusal::None)
		{
			if (!bRefusalPosted && Refusal != EVehicleRefusal::AlreadyMounted && Refusal != EVehicleRefusal::Dead)
			{
				PostLine(Operative->DisplayName, RefusalText(Refusal, Operative));
				bRefusalPosted = true;
			}
			continue;
		}
		if (Interactions)
		{
			Interactions->CancelUseOrder(Operative);
		}
		if (Relocation)
		{
			Relocation->CancelActiveTask(Operative);
		}
		FPendingBoard& Pending = Boarding.AddDefaulted_GetRef();
		Pending.Operative = Operative;
		Pending.Vehicle = Target;
		Pending.Entry = Entry;
		if (EntryDistance <= Target->CrewConfig.EntryReachCm)
		{
			Operative->StopOperative();
			Pending.ClimbLeft = Target->CrewConfig.BoardSeconds;
			Target->SetDoorsOpen(true);
		}
		else if (Operative->OrderMoveTo(Target->GetEntryPointWorld(Entry), true) != EOperativeOrderResult::Accepted)
		{
			Boarding.Pop();
			PostLine(Operative->DisplayName, LOCTEXT("NoWayToDoor", "Can't reach the door."));
			continue;
		}
		--FreeSeats;
		++Sent;
	}
	if (Sent > 0)
	{
		const FText VehicleName = Target->DisplayName;
		PostLine(LOCTEXT("Squad", "SQUAD"), Sent == 1
			? FText::Format(LOCTEXT("BoardOne", "{0}: boarding the {1}."), Operatives.Num() == 1 ? Operatives[0]->DisplayName : LOCTEXT("Operative", "Operative"), VehicleName)
			: FText::Format(LOCTEXT("BoardMany", "{0} operatives boarding the {1}."), Sent, VehicleName));
	}
	return Sent;
}

bool UVehicleSubsystem::IsExitFree(const AVehiclePawn& Vehicle, const FVector& Point) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VehicleExit), false, &Vehicle);
	for (const FVehicleCrewMember& Member : Vehicle.GetCrew())
	{
		Params.AddIgnoredActor(Member.Operative.Get());
	}
	return !GetWorld()->OverlapAnyTestByChannel(Point + FVector(0.f, 0.f, 95.f), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(40.f, 88.f), Params);
}

TArray<FVector> UVehicleSubsystem::GatherKnownThreats(const FVector& Around) const
{
	const UTacticalSightSubsystem* Sight = GetWorld()->GetSubsystem<UTacticalSightSubsystem>();
	TArray<FVector> Threats;
	for (TActorIterator<AEnemyCharacter> It(GetWorld()); It; ++It)
	{
		const UHealthComponent* Health = It->GetHealthComponent();
		if (It->IsDying() || (Health && !Health->IsAlive()) || FVector::Dist2D(It->GetActorLocation(), Around) > 5000.f)
		{
			continue;
		}
		// Known = seen by the squad (or no sight model running); a hidden enemy does not steer the exit.
		if (!Sight || !Sight->IsActive() || Sight->IsVisibleToSquad(*It))
		{
			Threats.Add(It->GetActorLocation());
		}
	}
	return Threats;
}

int32 UVehicleSubsystem::OrderDisembark(const TArray<AOperativeCharacter*>& Operatives)
{
	bool bTurnBased = false;
	bool bPause = false;
	VehicleSubsystemLocal::ModeFlags(GetWorld(), bTurnBased, bPause);
	TMap<AVehiclePawn*, TArray<AOperativeCharacter*>> ByVehicle;
	for (AOperativeCharacter* Operative : Operatives)
	{
		if (AVehiclePawn* Vehicle = GetVehicleOf(Operative); Vehicle && !IsDisembarking(Operative))
		{
			ByVehicle.FindOrAdd(Vehicle).Add(Operative);
		}
	}
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	int32 Count = 0;
	for (TPair<AVehiclePawn*, TArray<AOperativeCharacter*>>& Pair : ByVehicle)
	{
		AVehiclePawn* Vehicle = Pair.Key;
		// Exit candidates: on the infantry navmesh, free of bodies, not taken by someone already climbing out.
		TArray<FVector> Candidates = Vehicle->GetExitCandidatesWorld();
		TArray<bool> Free;
		for (FVector& Candidate : Candidates)
		{
			FNavLocation OnNav;
			bool bOk = Nav && Nav->ProjectPointToNavigation(Candidate, OnNav, FVector(60.f, 60.f, 300.f))
				&& FVector::Dist2D(OnNav.Location, Candidate) <= 70.f && !Vehicle->IsInsideFootprint(OnNav.Location, 30.f);
			if (bOk)
			{
				Candidate = OnNav.Location;
				bOk = IsExitFree(*Vehicle, Candidate) && !Exits.ContainsByPredicate([&Candidate](const FPendingExit& Each)
				{
					return FVector::Dist2D(Each.Point, Candidate) < 90.f;
				});
			}
			Free.Add(bOk);
		}
		const EVehicleRefusal Refusal = VehicleRules::CanDisembark(true, bTurnBased, bPause, Free.Contains(true));
		if (Refusal != EVehicleRefusal::None)
		{
			PostLine(Pair.Value[0]->DisplayName, RefusalText(Refusal, Pair.Value[0]));
			continue;
		}
		const FVector View = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : Vehicle->GetActorLocation();
		const TArray<int32> Ranked = VehicleRules::RankExitPoints(Candidates, Free, Vehicle->GetHullCentreWorld(),
			GatherKnownThreats(Vehicle->GetActorLocation()), View);
		Vehicle->StopDriving(); // a moving vehicle stops first; they climb out once it stands
		int32 Next = 0;
		for (AOperativeCharacter* Operative : Pair.Value)
		{
			if (!Ranked.IsValidIndex(Next))
			{
				PostLine(Operative->DisplayName, LOCTEXT("NoRoom", "No room to get out on this side."));
				continue;
			}
			FPendingExit& Exit = Exits.AddDefaulted_GetRef();
			Exit.Operative = Operative;
			Exit.Vehicle = Vehicle;
			Exit.Point = Candidates[Ranked[Next++]];
			Exit.ClimbLeft = Vehicle->CrewConfig.DisembarkSeconds;
			++Count;
		}
		Vehicle->SetDoorsOpen(true);
	}
	if (Count > 0)
	{
		PostLine(LOCTEXT("Squad", "SQUAD"), Count == 1 ? FText::Format(LOCTEXT("ExitOne", "{0}: getting out."), Operatives[0]->DisplayName)
			: FText::Format(LOCTEXT("ExitMany", "{0} operatives getting out."), Count));
	}
	return Count;
}

bool UVehicleSubsystem::HandleDriveClick(AOperativeCharacter* Leader, const FVector& Goal, bool bRush)
{
	AVehiclePawn* Vehicle = GetVehicleOf(Leader);
	if (!Vehicle)
	{
		return false;
	}
	bool bTurnBased = false;
	bool bPause = false;
	VehicleSubsystemLocal::ModeFlags(GetWorld(), bTurnBased, bPause);
	const EVehicleRefusal Refusal = VehicleRules::CanDrive(Vehicle->HasDriver(), bTurnBased, bPause);
	AOperativeCharacter* Driver = Vehicle->GetOccupant(EVehicleSeat::Driver);
	const FText Speaker = Driver ? Driver->DisplayName : Vehicle->DisplayName;
	if (Refusal != EVehicleRefusal::None)
	{
		PostLine(Speaker, RefusalText(Refusal, Driver));
		return true;
	}
	const EVehicleDriveResult Result = Vehicle->DriveTo(Goal, bRush);
	FGameplayEventContext Event;
	Event.Subject = Driver;
	Event.Target = Vehicle;
	Event.Location = Goal;
	if (Result == EVehicleDriveResult::NoPath || Result == EVehicleDriveResult::NoDriver)
	{
		PostLine(Speaker, LOCTEXT("NoRoad", "Can't get her through there. Dismount? [Y]"));
		GameplayEvents::RaiseWithContext(this, GameplayEvents::VehiclePathBlocked, Event);
		return true;
	}
	if (Result == EVehicleDriveResult::StartedPartial)
	{
		PostLine(Speaker, LOCTEXT("Partial", "She won't fit all the way - as close as she goes."));
		GameplayEvents::RaiseWithContext(this, GameplayEvents::VehiclePathBlocked, Event);
	}
	else
	{
		PostLine(Speaker, Vehicle->IsReversing() ? LOCTEXT("Reverse", "Reversing.")
			: (bRush ? LOCTEXT("Rush", "Full throttle!") : LOCTEXT("Drive", "Moving out.")));
	}
	if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>(); Feedback && Vehicle->GetPathPoints().Num() > 0)
	{
		Feedback->SpawnMovePing(Vehicle->GetPathPoints().Last(), true);
	}
	FPlayerCommandContext Context;
	Context.Operative = Leader;
	Context.Target = Vehicle;
	Context.Location = Goal;
	PlayerCommands::Report(this, PlayerCommands::VehicleDrive, Context);
	Event.Location = Vehicle->GetPathPoints().Num() > 0 ? Vehicle->GetPathPoints().Last() : Goal;
	GameplayEvents::RaiseWithContext(this, GameplayEvents::VehicleDriveStarted, Event);
	return true;
}

void UVehicleSubsystem::HandleArrived(AVehiclePawn* Vehicle)
{
	FGameplayEventContext Event;
	Event.Subject = Vehicle ? Vehicle->GetOccupant(EVehicleSeat::Driver) : nullptr;
	Event.Target = Vehicle;
	Event.Location = Vehicle ? Vehicle->GetActorLocation() : FVector::ZeroVector;
	GameplayEvents::RaiseWithContext(this, GameplayEvents::VehicleArrived, Event);
}

void UVehicleSubsystem::HandleBlocked(AVehiclePawn* Vehicle)
{
	const AOperativeCharacter* Driver = Vehicle ? Vehicle->GetOccupant(EVehicleSeat::Driver) : nullptr;
	PostLine(Driver ? Driver->DisplayName : LOCTEXT("HQ", "HQ"), LOCTEXT("Blocked", "The way is blocked - stopping."));
	FGameplayEventContext Event;
	Event.Subject = const_cast<AOperativeCharacter*>(Driver);
	Event.Target = Vehicle;
	Event.Location = Vehicle ? Vehicle->GetActorLocation() : FVector::ZeroVector;
	GameplayEvents::RaiseWithContext(this, GameplayEvents::VehiclePathBlocked, Event);
}

// --- Mounting ----------------------------------------------------------------------------------------------------

void UVehicleSubsystem::Mount(AOperativeCharacter* Operative, AVehiclePawn* Vehicle, bool bRaiseEvent)
{
	if (!Operative || !Vehicle || IsMounted(Operative))
	{
		return;
	}
	Operative->StopOperative();
	if (Operative->bInCover)
	{
		Operative->LeaveCover(TEXT("Boarding"));
	}
	if (USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		Squad->ClearPlannedOrder(Operative);
	}
	const FMountedOperativeEffects Effects = VehicleRules::GetMountedEffects(true);
	Operative->SetActorHiddenInGame(Effects.bHidden);
	Operative->SetActorEnableCollision(Effects.bCollision);
	if (UCharacterMovementComponent* Movement = Operative->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
		Movement->SetComponentTickEnabled(false);
	}
	Operative->AttachToActor(Vehicle, FAttachmentTransformRules::KeepWorldTransform);
	Operative->SetActorRelativeLocation(Vehicle->CrewAttachOffset);
	Operative->SetActorRelativeRotation(FRotator::ZeroRotator);
	Mounted.Add(Operative, Vehicle);
	Vehicle->AddCrew(Operative);
	Vehicle->SetEngineRunning(true);
	const EVehicleSeat Seat = Vehicle->GetSeatOf(Operative);
	UE_LOG(LogCodexTactics, Display, TEXT("[Vehicle] %s boarded %s as %s"), *Operative->GetName(), *Vehicle->GetName(), *VehicleRules::GetSeatName(Seat));
	if (bRaiseEvent)
	{
		PostLine(Operative->DisplayName, FText::Format(LOCTEXT("Aboard", "Aboard the {0} ({1})."), Vehicle->DisplayName,
			FText::FromString(VehicleRules::GetSeatName(Seat).ToLower())));
		FGameplayEventContext Event;
		Event.Subject = Operative;
		Event.Target = Vehicle;
		Event.Kind = FName(*VehicleRules::GetSeatName(Seat));
		Event.Location = Vehicle->GetActorLocation();
		GameplayEvents::RaiseWithContext(this, GameplayEvents::VehicleBoarded, Event);
	}
}

void UVehicleSubsystem::Unmount(AOperativeCharacter* Operative, const FVector& Location, bool bTakeCover)
{
	AVehiclePawn* Vehicle = GetVehicleOf(Operative);
	if (!Operative)
	{
		return;
	}
	Mounted.Remove(TWeakObjectPtr<const AActor>(Operative));
	if (Vehicle)
	{
		Vehicle->RemoveCrew(Operative);
		if (Vehicle->GetCrewCount() == 0)
		{
			Vehicle->SetEngineRunning(false);
		}
	}
	Operative->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Operative->SetActorEnableCollision(true);
	Operative->SetActorHiddenInGame(false);
	if (UCharacterMovementComponent* Movement = Operative->GetCharacterMovement())
	{
		Movement->SetComponentTickEnabled(true);
		Movement->SetMovementMode(MOVE_Walking);
	}
	const FVector Spot = Location + FVector(0.f, 0.f, Operative->GetSimpleCollisionHalfHeight() + 2.f);
	const FVector Away = Vehicle ? (Spot - Vehicle->GetHullCentreWorld()).GetSafeNormal2D() : Operative->GetActorForwardVector();
	Operative->TeleportTo(Spot, Away.Rotation(), false, true);
	UE_LOG(LogCodexTactics, Display, TEXT("[Vehicle] %s got out of %s at %s"), *Operative->GetName(), *GetNameSafe(Vehicle), *Spot.ToCompactString());
	if (!Vehicle || !bTakeCover)
	{
		return;
	}
	// Straight into cover at the hull: the side he stands at.
	FVector Probe;
	FVector Normal;
	const FVector ToHull = Vehicle->GetHullCentreWorld() - Spot;
	FCoverSlot Slot;
	if (Vehicle->GetCoverProbe(Spot, ToHull, Probe, Normal) && CoverTraceRules::FindCoverSlotAt(GetWorld(), Probe, -Normal, Slot)
		&& Slot.WallActor.Get() == Vehicle)
	{
		Operative->OrderTakeCover(Slot, false);
	}
	FGameplayEventContext Event;
	Event.Subject = Operative;
	Event.Target = Vehicle;
	Event.Location = Spot;
	GameplayEvents::RaiseWithContext(this, GameplayEvents::VehicleDisembarked, Event);
}

bool UVehicleSubsystem::MountImmediately(AOperativeCharacter* Operative, AVehiclePawn* Vehicle)
{
	if (!Operative || !Vehicle || IsMounted(Operative) || Vehicle->GetFreeSeats() <= 0)
	{
		return false;
	}
	Boarding.RemoveAll([Operative](const FPendingBoard& Each) { return Each.Operative.Get() == Operative; });
	Mount(Operative, Vehicle, false);
	return true;
}

void UVehicleSubsystem::DismountImmediately(AOperativeCharacter* Operative, const FVector& Location)
{
	Exits.RemoveAll([Operative](const FPendingExit& Each) { return Each.Operative.Get() == Operative; });
	if (IsMounted(Operative))
	{
		Unmount(Operative, Location, false);
	}
}

// --- Tick --------------------------------------------------------------------------------------------------------

void UVehicleSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (Boarding.IsEmpty() && Exits.IsEmpty())
	{
		return;
	}
	UpdateBoarding(DeltaTime);
	UpdateExits(DeltaTime);
	UpdateDoors();
}

void UVehicleSubsystem::UpdateBoarding(float DeltaTime)
{
	for (int32 Index = Boarding.Num() - 1; Index >= 0; --Index)
	{
		FPendingBoard& Pending = Boarding[Index];
		AOperativeCharacter* Operative = Pending.Operative.Get();
		AVehiclePawn* Vehicle = Pending.Vehicle.Get();
		if (!Operative || !Vehicle || !VehicleSubsystemLocal::IsAliveOperative(Operative) || IsMounted(Operative))
		{
			Boarding.RemoveAt(Index);
			continue;
		}
		Pending.Age += DeltaTime;
		if (!Vehicle->IsStopped() || Operative->IsKnockedDown() || Vehicle->GetFreeSeats() <= 0)
		{
			PostLine(Operative->DisplayName, LOCTEXT("BoardCancelled", "Boarding cancelled."));
			Boarding.RemoveAt(Index);
			continue;
		}
		if (Pending.ClimbLeft < 0.f)
		{
			const FVector Door = Vehicle->GetEntryPointWorld(Pending.Entry);
			const float Distance = FVector::Dist2D(Operative->GetActorLocation(), Door);
			Pending.IdleSeconds = Operative->IsMoving() ? 0.f : Pending.IdleSeconds + DeltaTime;
			if (Distance <= Vehicle->CrewConfig.EntryReachCm
				|| (Pending.IdleSeconds > 0.3f && Distance <= Vehicle->CrewConfig.EntryReachCm * 2.f))
			{
				Operative->StopOperative();
				Operative->SetFacingPoint(Vehicle->GetHullCentreWorld());
				Pending.ClimbLeft = Vehicle->CrewConfig.BoardSeconds;
			}
			else if (Pending.IdleSeconds > 1.f && Pending.Retries < 4 && Pending.Age < 40.f)
			{
				// Stopped short (a mate in the doorway, a path dropped while the navmesh rebuilt): walk on, to the
				// nearest door now (a new player order cancels the boarding: CancelBoarding).
				++Pending.Retries;
				Pending.IdleSeconds = 0.f;
				float Best = TNumericLimits<float>::Max();
				for (int32 Entry = 0; Entry < Vehicle->EntryPoints.Num(); ++Entry)
				{
					const float EntryDistance = FVector::Dist2D(Operative->GetActorLocation(), Vehicle->GetEntryPointWorld(Entry))
						+ (Entry == Pending.Entry && Pending.Retries > 1 ? 300.f : 0.f); // a blocked door: try another
					if (EntryDistance < Best)
					{
						Best = EntryDistance;
						Pending.Entry = Entry;
					}
				}
				Operative->OrderMoveTo(Vehicle->GetEntryPointWorld(Pending.Entry), true);
			}
			else if ((Pending.IdleSeconds > 1.f && Pending.Retries >= 4) || Pending.Age > 40.f)
			{
				UE_LOG(LogCodexTactics, Display, TEXT("[Vehicle] %s gave up boarding %.0f cm from the door"), *Operative->GetName(), Distance);
				PostLine(Operative->DisplayName, LOCTEXT("BoardStopped", "Didn't make it to the door."));
				Boarding.RemoveAt(Index);
			}
			continue;
		}
		Pending.ClimbLeft -= DeltaTime;
		if (Pending.ClimbLeft <= 0.f)
		{
			Boarding.RemoveAt(Index);
			Mount(Operative, Vehicle, true);
		}
	}
}

void UVehicleSubsystem::UpdateExits(float DeltaTime)
{
	for (int32 Index = Exits.Num() - 1; Index >= 0; --Index)
	{
		FPendingExit& Exit = Exits[Index];
		AOperativeCharacter* Operative = Exit.Operative.Get();
		AVehiclePawn* Vehicle = Exit.Vehicle.Get();
		if (!Operative || !Vehicle || !IsMounted(Operative))
		{
			Exits.RemoveAt(Index);
			continue;
		}
		if (!Vehicle->IsStopped())
		{
			continue; // braking first
		}
		Exit.ClimbLeft -= DeltaTime;
		if (Exit.ClimbLeft <= 0.f)
		{
			const FVector Point = Exit.Point;
			Exits.RemoveAt(Index);
			Unmount(Operative, Point, true);
		}
	}
}

void UVehicleSubsystem::UpdateDoors()
{
	for (AVehiclePawn* Vehicle : GetVehicles())
	{
		const bool bOpen = Boarding.ContainsByPredicate([Vehicle](const FPendingBoard& Each) { return Each.Vehicle.Get() == Vehicle && Each.ClimbLeft >= 0.f; })
			|| Exits.ContainsByPredicate([Vehicle](const FPendingExit& Each) { return Each.Vehicle.Get() == Vehicle; });
		Vehicle->SetDoorsOpen(bOpen);
	}
}

// --- Navigation --------------------------------------------------------------------------------------------------

int32 UVehicleSubsystem::GetVehicleAgentIndex() const
{
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Nav)
	{
		return INDEX_NONE;
	}
	const TArray<FNavDataConfig>& Agents = Nav->GetSupportedAgents();
	for (int32 Index = 0; Index < Agents.Num(); ++Index)
	{
		if (Agents[Index].Name == VehicleNavAgents::VehicleAgentName)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void UVehicleSubsystem::StripVehicleAgentFromBounds()
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const int32 VehicleIndex = GetVehicleAgentIndex();
	if (!Nav || VehicleIndex == INDEX_NONE)
	{
		return;
	}
	for (TActorIterator<ANavMeshBoundsVolume> It(GetWorld()); It; ++It)
	{
		ANavMeshBoundsVolume* Volume = *It;
		// Opt-in: tagged volumes and vehicle-only volumes keep the vehicle navmesh.
		if (Volume->ActorHasTag(VehicleNavAgents::VehicleNavTag) || !Volume->SupportedAgents.Contains(VehicleIndex)
			|| !Volume->SupportedAgents.Contains(VehicleNavAgents::InfantryIndex))
		{
			continue;
		}
		Volume->SupportedAgents.PackedBits &= ~(1u << VehicleIndex);
		Nav->OnNavigationBoundsUpdated(Volume);
		UE_LOG(LogCodexTactics, Verbose, TEXT("[Vehicle] %s: no vehicle navmesh (no '%s' tag)"), *Volume->GetName(), *VehicleNavAgents::VehicleNavTag.ToString());
	}
}

void UVehicleSubsystem::EnableVehicleNavigation(ANavMeshBoundsVolume* Volume)
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const int32 VehicleIndex = GetVehicleAgentIndex();
	if (!Volume || !Nav || VehicleIndex == INDEX_NONE)
	{
		return;
	}
	Volume->Tags.AddUnique(VehicleNavAgents::VehicleNavTag);
	Volume->SupportedAgents.Set(VehicleIndex);
	Nav->OnNavigationBoundsUpdated(Volume);
}

// --- Save / load -------------------------------------------------------------------------------------------------

TArray<TSharedPtr<FJsonValue>> UVehicleSubsystem::CaptureSave() const
{
	TArray<TSharedPtr<FJsonValue>> Entries;
	for (const AVehiclePawn* Vehicle : GetVehicles())
	{
		const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Vehicle->GetName());
		Entry->SetStringField(TEXT("class"), Vehicle->GetClass()->GetPathName());
		const FVector Location = Vehicle->GetActorLocation();
		Entry->SetNumberField(TEXT("x"), Location.X);
		Entry->SetNumberField(TEXT("y"), Location.Y);
		Entry->SetNumberField(TEXT("z"), Location.Z);
		Entry->SetNumberField(TEXT("yaw"), Vehicle->GetHeadingYaw());
		TArray<TSharedPtr<FJsonValue>> CrewList;
		for (const FVehicleCrewMember& Member : Vehicle->GetCrew())
		{
			if (const AOperativeCharacter* Operative = Member.Operative.Get())
			{
				const TSharedRef<FJsonObject> Seat = MakeShared<FJsonObject>();
				Seat->SetStringField(TEXT("character_name"), Operative->DisplayName.ToString());
				Seat->SetStringField(TEXT("role"), UEnum::GetValueAsString(Operative->SquadRole));
				Seat->SetStringField(TEXT("seat"), VehicleRules::GetSeatName(Member.Seat));
				CrewList.Add(MakeShared<FJsonValueObject>(Seat));
			}
		}
		Entry->SetArrayField(TEXT("crew"), CrewList);
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	}
	return Entries;
}

void UVehicleSubsystem::PrepareForLoad()
{
	Boarding.Reset();
	Exits.Reset();
	for (AVehiclePawn* Vehicle : GetVehicles())
	{
		Vehicle->StopDriving();
		const TArray<FVector> Candidates = Vehicle->GetExitCandidatesWorld();
		int32 Next = 0;
		TArray<AOperativeCharacter*> Crew;
		for (const FVehicleCrewMember& Member : Vehicle->GetCrew())
		{
			Crew.Add(Member.Operative.Get());
		}
		for (AOperativeCharacter* Operative : Crew)
		{
			// Out at once beside the hull; the save then puts him where he was.
			DismountImmediately(Operative, Candidates.IsValidIndex(Next) ? Candidates[Next++] : Vehicle->GetActorLocation() + FVector(0.f, 0.f, 0.f));
		}
	}
	Mounted.Reset();
}

void UVehicleSubsystem::RestoreSave(const TArray<TSharedPtr<FJsonValue>>& Entries)
{
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	for (const TSharedPtr<FJsonValue>& Value : Entries)
	{
		const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!Entry.IsValid())
		{
			continue;
		}
		const FString Name = Entry->GetStringField(TEXT("name"));
		AVehiclePawn* Vehicle = nullptr;
		for (AVehiclePawn* Each : GetVehicles())
		{
			Vehicle = Each->GetName() == Name ? Each : Vehicle;
		}
		const FVector Location(Entry->GetNumberField(TEXT("x")), Entry->GetNumberField(TEXT("y")), Entry->GetNumberField(TEXT("z")));
		const float Yaw = static_cast<float>(Entry->GetNumberField(TEXT("yaw")));
		if (!Vehicle)
		{
			// Spawned at runtime in the saved session: spawn it again from its class.
			UClass* Class = LoadClass<AVehiclePawn>(nullptr, *Entry->GetStringField(TEXT("class")));
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Vehicle = Class ? GetWorld()->SpawnActor<AVehiclePawn>(Class, Location, FRotator(0.f, Yaw, 0.f), Params) : nullptr;
		}
		if (!Vehicle)
		{
			continue;
		}
		Vehicle->RestoreTransform(Location, Yaw);
		const TArray<TSharedPtr<FJsonValue>>* CrewList = nullptr;
		if (!Squad || !Entry->TryGetArrayField(TEXT("crew"), CrewList))
		{
			continue;
		}
		TArray<TPair<AOperativeCharacter*, EVehicleSeat>> Seats;
		for (const TSharedPtr<FJsonValue>& SeatValue : *CrewList)
		{
			const TSharedPtr<FJsonObject> Seat = SeatValue.IsValid() ? SeatValue->AsObject() : nullptr;
			if (!Seat.IsValid())
			{
				continue;
			}
			const FString CharacterName = Seat->GetStringField(TEXT("character_name"));
			const FString Role = Seat->GetStringField(TEXT("role"));
			AOperativeCharacter* Operative = nullptr;
			for (AOperativeCharacter* Candidate : Squad->GetMembers())
			{
				if (Candidate->DisplayName.ToString() == CharacterName || (!Operative && UEnum::GetValueAsString(Candidate->SquadRole) == Role))
				{
					Operative = Candidate;
				}
			}
			if (Operative && VehicleSubsystemLocal::IsAliveOperative(Operative) && MountImmediately(Operative, Vehicle))
			{
				Seats.Emplace(Operative, VehicleRules::ParseSeatName(Seat->GetStringField(TEXT("seat"))));
			}
		}
		for (const TPair<AOperativeCharacter*, EVehicleSeat>& Seat : Seats)
		{
			Vehicle->SetCrewSeat(Seat.Key, Seat.Value);
		}
	}
}

// --- Queries -----------------------------------------------------------------------------------------------------

namespace VehicleQueries
{
	bool IsMounted(const AActor* Operative)
	{
		const UWorld* World = Operative ? Operative->GetWorld() : nullptr;
		const UVehicleSubsystem* Vehicles = World ? World->GetSubsystem<UVehicleSubsystem>() : nullptr;
		return Vehicles && Vehicles->IsMounted(Operative);
	}

	AVehiclePawn* GetVehicle(const AActor* Operative)
	{
		const UWorld* World = Operative ? Operative->GetWorld() : nullptr;
		const UVehicleSubsystem* Vehicles = World ? World->GetSubsystem<UVehicleSubsystem>() : nullptr;
		return Vehicles ? Vehicles->GetVehicleOf(Operative) : nullptr;
	}
}

#undef LOCTEXT_NAMESPACE
