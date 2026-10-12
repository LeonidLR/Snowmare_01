#include "Vehicles/VehicleSubsystem.h"
#include "Characters/EnemyCharacter.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Combat/HealthComponent.h"
#include "Combat/KnockdownComponent.h"
#include "Combat/TacticalSightSubsystem.h"
#include "UI/FloatingTextSubsystem.h"
#include "Core/PlayerCommands.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Interactables/InteractionSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Interactables/RelocationSubsystem.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Tactics/CoverTraceRules.h"
#include "UI/GameMessageSubsystem.h"
#include "Camera/TacticalCameraPawn.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "Engine/GameViewportClient.h"
#include "Styling/CoreStyle.h"
#include "Vehicles/VehicleNavigation.h"
#include "Vehicles/VehiclePawn.h"
#include "Vehicles/VehicleTurretComponent.h"
#include "Widgets/Layout/SBorder.h"

#define LOCTEXT_NAMESPACE "CodexVehicles"

namespace VehicleSubsystemLocal
{
	bool IsAliveOperative(const AOperativeCharacter* Operative)
	{
		return Operative && Operative->HealthComponent && Operative->HealthComponent->IsAlive();
	}

	/** A boarder waiting at the door for the group fade lets his mates walk through him (bBlock false), restored after. */
	void SetBlocksPawns(AOperativeCharacter* Operative, bool bBlock)
	{
		if (UCapsuleComponent* Capsule = Operative ? Operative->GetCapsuleComponent() : nullptr)
		{
			Capsule->SetCollisionResponseToChannel(ECC_Pawn, bBlock ? ECR_Block : ECR_Ignore);
		}
		// The mates' crowd avoidance must not steer round him either (separation kept them metres short of the door).
		const AAIController* Controller = Operative ? Cast<AAIController>(Operative->GetController()) : nullptr;
		if (UCrowdFollowingComponent* Crowd = Controller ? Cast<UCrowdFollowingComponent>(Controller->GetPathFollowingComponent()) : nullptr)
		{
			Crowd->SetCrowdSimulationState(bBlock ? ECrowdSimulationState::Enabled : ECrowdSimulationState::Disabled);
		}
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

void UVehicleSubsystem::Deinitialize()
{
	RemoveFadeOverlay();
	Super::Deinitialize();
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
	Vehicle->OnDisabled.AddUObject(this, &UVehicleSubsystem::HandleDisabled);
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
	Boarding.RemoveAll([Operative](const FPendingBoard& Each)
	{
		const bool bDrop = Each.Operative.Get() == Operative && Each.ClimbLeft < 0.f && !Each.bFadeQueued;
		if (bDrop && Each.bReady)
		{
			VehicleSubsystemLocal::SetBlocksPawns(Each.Operative.Get(), true);
		}
		return bDrop;
	});
	// ... and a repair walk / work (P4).
	Repairs.RemoveAll([Operative](const FPendingRepair& Each) { return Each.Operative.Get() == Operative; });
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
	case EVehicleRefusal::Disabled:
		return LOCTEXT("RefuseDisabled", "She's disabled - the Engineer has to get her running first [Y].");
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
		// P4: the nearest vehicle is disabled - Y sends the Engineer to repair it instead.
		AVehiclePawn* Wreck = Preferred ? Preferred : FindNearestVehicle(Leader->GetActorLocation(), -1.f);
		if (Wreck && Wreck->IsDisabled() && FVector::Dist2D(Leader->GetActorLocation(), Wreck->GetHullCentreWorld()) <= Wreck->CrewConfig.BoardRangeCm)
		{
			if (OrderRepair(Outside, Wreck))
			{
				Context.Target = Wreck;
				Context.Location = Wreck->GetActorLocation();
				PlayerCommands::Report(this, PlayerCommands::VehicleRepair, Context);
			}
			return EVehicleKeyAction::Repair;
		}
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
		float BestScore = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < Target->EntryPoints.Num(); ++Index)
		{
			const float Distance = FVector::Dist2D(Operative->GetActorLocation(), Target->GetEntryPointWorld(Index));
			// Spread a group over the doors (a mate waiting at a door for the fade stands in it).
			const int32 Users = Boarding.FilterByPredicate([Target, Index](const FPendingBoard& Each) { return Each.Vehicle.Get() == Target && Each.Entry == Index; }).Num();
			const float Score = Distance + Users * 400.f;
			if (Score < BestScore)
			{
				BestScore = Score;
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
		Check.bVehicleDisabled = Target->IsDisabled();
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
			if (ShouldFade(Target))
			{
				Pending.bReady = true; // in through the fade (no climbing animation)
				VehicleSubsystemLocal::SetBlocksPawns(Operative, false);
			}
			else
			{
				Pending.ClimbLeft = Target->CrewConfig.BoardSeconds;
			}
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
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	int32 Count = 0;
	for (TPair<AVehiclePawn*, TArray<AOperativeCharacter*>>& Pair : ByVehicle)
	{
		AVehiclePawn* Vehicle = Pair.Key;
		// Exit candidates: on the infantry navmesh, free of bodies, not taken by someone already climbing out.
		TArray<FVector> Candidates;
		TArray<bool> Free;
		GatherExitPoints(*Vehicle, Candidates, Free);
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
	const EVehicleRefusal Refusal = VehicleRules::CanDrive(Vehicle->HasDriver(), bTurnBased, bPause, Vehicle->IsDisabled());
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
	if (Result == EVehicleDriveResult::Disabled)
	{
		PostLine(Speaker, RefusalText(EVehicleRefusal::Disabled, Driver));
		return true;
	}
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
	VehicleSubsystemLocal::SetBlocksPawns(Operative, true); // back to normal for when he gets out (collision is off aboard)
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
	if (Boarding.IsEmpty() && Exits.IsEmpty() && Repairs.IsEmpty() && !Fade.IsActive())
	{
		return;
	}
	UpdateBoarding(DeltaTime);
	UpdateExits(DeltaTime);
	UpdateRepairs(DeltaTime);
	UpdateFade();
	UpdateDoors();
}

// --- Boarding fade -------------------------------------------------------------------------------------------------

bool UVehicleSubsystem::ShouldFade(const AVehiclePawn* Vehicle) const
{
	bool bTurnBased = false;
	bool bPause = false;
	VehicleSubsystemLocal::ModeFlags(GetWorld(), bTurnBased, bPause);
	return Vehicle && Vehicle->FadeConfig.bEnabled && !bTurnBased && !bPause && !UGameplayStatics::IsGamePaused(GetWorld());
}

void UVehicleSubsystem::RequestFade(const AVehiclePawn* Vehicle)
{
	if (!Fade.IsActive())
	{
		FadeConfig = Vehicle ? Vehicle->FadeConfig : FVehicleFadeConfig();
		FadeAge = 0.f;
		++FadeCount;
		UE_LOG(LogCodexTactics, Display, TEXT("[Vehicle] fade to black #%d (%.2f s out, %.2f s hold, %.2f s in)"), FadeCount,
			FadeConfig.FadeOutSeconds, FadeConfig.HoldSeconds, FadeConfig.FadeInSeconds);
		ShowFadeOverlay();
	}
	else if (Fade.Phase == EVehicleFadePhase::FadeIn && !Fade.bQueued)
	{
		++FadeCount; // a second cycle right after this one
	}
	Fade.Request(FadeConfig);
}

float UVehicleSubsystem::GetFadeAlpha() const
{
	return Fade.GetAlpha(FadeConfig);
}

void UVehicleSubsystem::UpdateFade()
{
	if (!Fade.IsActive())
	{
		return;
	}
	// Real time: the fade runs the same in the tactical pause's slow motion and never waits on world time.
	const UWorld* World = GetWorld();
	const float RealDelta = FMath::Clamp(World ? static_cast<float>(World->DeltaRealTimeSeconds) : 0.f, 0.f, 0.1f);
	FadeAge += RealDelta;
	Fade.Advance(RealDelta, FadeConfig);
	if (Fade.IsBlack())
	{
		ExecuteFadeQueue();
	}
	// Watchdog: never stuck black (a hitch, a world without real time, a queue that keeps refilling).
	const float Limit = FadeConfig.FadeOutSeconds + FadeConfig.MaxHoldSeconds + FadeConfig.FadeInSeconds + 1.5f;
	if (FadeAge > Limit * (Fade.bQueued ? 2.f : 1.f))
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[Vehicle] fade watchdog: forced back to clear after %.1f s"), FadeAge);
		ExecuteFadeQueue();
		Fade = FVehicleFadeTimeline();
	}
	if (!Fade.IsActive())
	{
		ExecuteFadeQueue(); // anything that slipped in at the very end is not left waiting
		RemoveFadeOverlay();
	}
}

void UVehicleSubsystem::ExecuteFadeQueue()
{
	bool bMoved = false;
	for (int32 Index = Boarding.Num() - 1; Index >= 0; --Index)
	{
		if (!Boarding[Index].bFadeQueued)
		{
			continue;
		}
		AOperativeCharacter* Operative = Boarding[Index].Operative.Get();
		AVehiclePawn* Vehicle = Boarding[Index].Vehicle.Get();
		Boarding.RemoveAt(Index);
		if (Operative && Vehicle && VehicleSubsystemLocal::IsAliveOperative(Operative) && !IsMounted(Operative) && Vehicle->GetFreeSeats() > 0)
		{
			Mount(Operative, Vehicle, true);
			bMoved = true;
		}
		else
		{
			VehicleSubsystemLocal::SetBlocksPawns(Operative, true);
		}
	}
	for (int32 Index = Exits.Num() - 1; Index >= 0; --Index)
	{
		if (!Exits[Index].bFadeQueued)
		{
			continue;
		}
		AOperativeCharacter* Operative = Exits[Index].Operative.Get();
		const FVector Point = Exits[Index].Point;
		Exits.RemoveAt(Index);
		if (Operative && IsMounted(Operative))
		{
			Unmount(Operative, Point, true);
			bMoved = true;
		}
	}
	if (!bMoved)
	{
		return;
	}
	// The camera jumps with the leader while the screen is black (no visible swoop to the hull / the exit).
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (ATacticalCameraPawn* Camera = Cast<ATacticalCameraPawn>(PC->GetPawn()); Camera && Camera->GetFollowTarget() && !Camera->IsCinematicLocked())
		{
			Camera->SnapFocusOnTarget(Camera->GetFollowTarget());
		}
	}
}

void UVehicleSubsystem::ShowFadeOverlay()
{
	UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	if (!Viewport || FadeOverlay.IsValid())
	{
		return; // headless (-nullrhi): the timeline still runs (smokes read GetFadeAlpha)
	}
	TWeakObjectPtr<UVehicleSubsystem> WeakThis(this);
	FadeOverlay = SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
		.BorderBackgroundColor_Lambda([WeakThis]()
		{
			const UVehicleSubsystem* Self = WeakThis.Get();
			return FSlateColor(FLinearColor(0.f, 0.f, 0.f, Self ? Self->GetFadeAlpha() : 0.f));
		})
		.Visibility(EVisibility::HitTestInvisible); // never blocks the mouse
	Viewport->AddViewportWidgetContent(FadeOverlay.ToSharedRef(), 9000);
}

void UVehicleSubsystem::RemoveFadeOverlay()
{
	if (!FadeOverlay.IsValid())
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		if (UGameViewportClient* Viewport = World->GetGameViewport())
		{
			Viewport->RemoveViewportWidgetContent(FadeOverlay.ToSharedRef());
		}
	}
	FadeOverlay.Reset();
}

// --- Turret orders -------------------------------------------------------------------------------------------------

bool UVehicleSubsystem::HandleTurretTargetClick(AOperativeCharacter* Leader, AActor* Target)
{
	AVehiclePawn* Vehicle = GetVehicleOf(Leader);
	if (!Vehicle)
	{
		return false;
	}
	const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(Target);
	const UHealthComponent* Health = Enemy ? Enemy->GetHealthComponent() : nullptr;
	AOperativeCharacter* Gunner = Vehicle->Turret ? Vehicle->Turret->GetGunner() : nullptr;
	if (!Enemy && Target && Vehicle->Turret)
	{
		// P4: a barrel / mine / trapped object gets one cannon shell (detonated like an operative's targeted shot).
		const EVehicleObjectShot Shot = Vehicle->Turret->SetObjectTarget(Target);
		if (Shot == EVehicleObjectShot::NotATarget)
		{
			return true; // not a target: ignored (no drive on a Ctrl + click)
		}
		const FText Speaker = Gunner ? Gunner->DisplayName : Vehicle->DisplayName;
		switch (Shot)
		{
		case EVehicleObjectShot::None:
			PostLine(Speaker, LOCTEXT("ShellObject", "One shell on it - stand clear!"));
			if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
			{
				Feedback->HighlightTarget(Target);
			}
			break;
		case EVehicleObjectShot::NoGunner:
			PostLine(Speaker, LOCTEXT("NoGunnerObject", "Nobody on the gun: board a second operative."));
			break;
		case EVehicleObjectShot::Disabled:
			PostLine(Speaker, LOCTEXT("DisabledGun", "The turret is dead - no power."));
			break;
		case EVehicleObjectShot::NoShells:
			PostLine(Speaker, LOCTEXT("NoShells", "Out of 30 mm shells."));
			break;
		case EVehicleObjectShot::OutOfRange:
			PostLine(Speaker, LOCTEXT("ObjectFar", "Too far for the cannon."));
			break;
		case EVehicleObjectShot::DeadZone:
			PostLine(Speaker, LOCTEXT("ObjectDeadZone", "Too close - can't depress the gun that far!"));
			break;
		case EVehicleObjectShot::NoLineOfFire:
			PostLine(Speaker, LOCTEXT("ObjectNoLine", "No line of fire from the turret."));
			break;
		default:
			PostLine(Speaker, LOCTEXT("ObjectHeld", "The gun can't fire right now."));
			break;
		}
		FPlayerCommandContext Context;
		Context.Operative = Leader;
		Context.Target = Target;
		Context.Location = Target->GetActorLocation();
		PlayerCommands::Report(this, PlayerCommands::VehicleTurretFire, Context);
		return true;
	}
	if (!Enemy || Enemy->IsDying() || !Health || !Health->IsAlive())
	{
		return true; // not a target: ignored (no drive on a Ctrl + click)
	}
	if (!Gunner)
	{
		PostLine(Vehicle->DisplayName, LOCTEXT("NoGunner", "Nobody on the gun: board a second operative."));
		return true;
	}
	Vehicle->Turret->SetPriorityTarget(Target);
	if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
	{
		Feedback->HighlightTarget(Target);
	}
	if (Vehicle->GetHullDistance2D(Target->GetActorLocation()) < Vehicle->Turret->Config.DeadZoneCm)
	{
		PostLine(Gunner->DisplayName, LOCTEXT("DeadZone", "Too close - can't depress the gun that far!"));
	}
	else
	{
		PostLine(Gunner->DisplayName, LOCTEXT("TurretTarget", "On it - target marked."));
	}
	FPlayerCommandContext Context;
	Context.Operative = Leader;
	Context.Target = Target;
	Context.Location = Target->GetActorLocation();
	PlayerCommands::Report(this, PlayerCommands::VehicleTurretFire, Context);
	return true;
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
			VehicleSubsystemLocal::SetBlocksPawns(Operative, true);
			Boarding.RemoveAt(Index);
			continue;
		}
		Pending.Age += DeltaTime;
		if (!Vehicle->IsStopped() || Operative->IsKnockedDown() || Vehicle->GetFreeSeats() <= 0)
		{
			VehicleSubsystemLocal::SetBlocksPawns(Operative, true);
			PostLine(Operative->DisplayName, LOCTEXT("BoardCancelled", "Boarding cancelled."));
			Boarding.RemoveAt(Index);
			continue;
		}
		if (Pending.bFadeQueued)
		{
			continue; // mounted on black (ExecuteFadeQueue)
		}
		if (Pending.bReady)
		{
			Pending.ReadySeconds += DeltaTime;
			continue; // waits at the door for the group fade (below)
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
				if (ShouldFade(Vehicle))
				{
					Pending.bReady = true;
					VehicleSubsystemLocal::SetBlocksPawns(Operative, false);
				}
				else
				{
					Pending.ClimbLeft = Vehicle->CrewConfig.BoardSeconds;
				}
			}
			else if (Pending.IdleSeconds > 1.f && Pending.Retries < 4 && Pending.Age < 40.f)
			{
				// Stopped short (a mate in the doorway, a path dropped while the navmesh rebuilt): walk on, to the
				// nearest door now (a new player order cancels the boarding: CancelBoarding).
				++Pending.Retries;
				Pending.IdleSeconds = 0.f;
				float Best = TNumericLimits<float>::Max();
				const int32 StalledEntry = Pending.Entry; // (P1 compared against the entry being updated in the loop)
				for (int32 Entry = 0; Entry < Vehicle->EntryPoints.Num(); ++Entry)
				{
					const float EntryDistance = FVector::Dist2D(Operative->GetActorLocation(), Vehicle->GetEntryPointWorld(Entry))
						+ (Entry == StalledEntry && Pending.Retries > 1 ? 600.f : 0.f); // a blocked door: try another
					if (EntryDistance < Best)
					{
						Best = EntryDistance;
						Pending.Entry = Entry;
					}
				}
				const EOperativeOrderResult Retry = Operative->OrderMoveTo(Vehicle->GetEntryPointWorld(Pending.Entry), true);
				UE_LOG(LogCodexTactics, Display, TEXT("[Vehicle] %s stalled %.0f cm from the door: retry %d to entry %d (order %d, at %s)"), *Operative->GetName(),
					Distance, Pending.Retries, Pending.Entry, static_cast<int32>(Retry), *Vehicle->GetActorTransform().InverseTransformPosition(Operative->GetActorLocation()).ToCompactString());
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
	// Group fade: the ones at a door go in together once nobody else walking to the same vehicle is about to arrive (or
	// the first has waited GroupWaitSeconds) - one fade for the whole squad.
	for (AVehiclePawn* Vehicle : GetVehicles())
	{
		float ReadyWait = -1.f;
		TArray<float> Walkers;
		for (const FPendingBoard& Pending : Boarding)
		{
			if (Pending.Vehicle.Get() != Vehicle || Pending.bFadeQueued || !Pending.Operative.IsValid())
			{
				continue;
			}
			if (Pending.bReady)
			{
				ReadyWait = FMath::Max(ReadyWait, Pending.ReadySeconds);
			}
			else if (Pending.ClimbLeft < 0.f)
			{
				const float Distance = FVector::Dist2D(Pending.Operative->GetActorLocation(), Vehicle->GetEntryPointWorld(Pending.Entry));
				Walkers.Add(VehicleCombatRules::EstimateArrivalSeconds(FMath::Max(0.f, Distance - Vehicle->CrewConfig.EntryReachCm),
					Pending.Operative->GetVelocity().Size2D()));
			}
		}
		if (ReadyWait < 0.f)
		{
			continue;
		}
		if (!ShouldFade(Vehicle))
		{
			// The pause / turn-based came in while they waited: straight in, no fade.
			for (FPendingBoard& Pending : Boarding)
			{
				if (Pending.Vehicle.Get() == Vehicle && Pending.bReady && !Pending.bFadeQueued)
				{
					Pending.bFadeQueued = true;
				}
			}
			ExecuteFadeQueue();
			continue;
		}
		if (VehicleCombatRules::ShouldStartGroupFade(ReadyWait, Walkers, Vehicle->FadeConfig))
		{
			for (FPendingBoard& Pending : Boarding)
			{
				if (Pending.Vehicle.Get() == Vehicle && Pending.bReady && !Pending.bFadeQueued)
				{
					Pending.bFadeQueued = true;
				}
			}
			RequestFade(Vehicle);
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
		if (Exit.bFadeQueued)
		{
			continue; // moved out on black (ExecuteFadeQueue)
		}
		if (!Vehicle->IsStopped())
		{
			continue; // braking first
		}
		if (ShouldFade(Vehicle))
		{
			// Everyone of this order is handed over on the same tick (the vehicle stopped for all): one fade.
			Exit.bFadeQueued = true;
			RequestFade(Vehicle);
			continue;
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
		const bool bOpen = Boarding.ContainsByPredicate([Vehicle](const FPendingBoard& Each)
			{
				return Each.Vehicle.Get() == Vehicle && (Each.ClimbLeft >= 0.f || Each.bReady || Each.bFadeQueued);
			})
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
		if (Vehicle->Turret)
		{
			Entry->SetNumberField(TEXT("shells"), Vehicle->Turret->GetShellsLeft());
		}
		Entry->SetNumberField(TEXT("hull_hp"), Vehicle->GetHullHealth());
		Entry->SetBoolField(TEXT("disabled"), Vehicle->IsDisabled());
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
	for (const FPendingBoard& Pending : Boarding)
	{
		VehicleSubsystemLocal::SetBlocksPawns(Pending.Operative.Get(), true);
	}
	Boarding.Reset();
	Exits.Reset();
	Repairs.Reset();
	Fade = FVehicleFadeTimeline();
	RemoveFadeOverlay();
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
		// P4: hull health / disabled (older saves: a whole hull).
		double HullHealth = Vehicle->GetHullMaxHealth();
		bool bDisabled = false;
		Entry->TryGetNumberField(TEXT("hull_hp"), HullHealth);
		Entry->TryGetBoolField(TEXT("disabled"), bDisabled);
		Vehicle->RestoreHull(static_cast<float>(HullHealth), bDisabled);
		double Shells = -1.0;
		if (Vehicle->Turret && Entry->TryGetNumberField(TEXT("shells"), Shells) && Shells >= 0.0)
		{
			Vehicle->Turret->SetShellsLeft(FMath::RoundToInt(Shells));
		}
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

// --- Hull (P4) ---------------------------------------------------------------------------------------------------

void UVehicleSubsystem::GatherExitPoints(const AVehiclePawn& Vehicle, TArray<FVector>& OutCandidates, TArray<bool>& OutFree) const
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	OutCandidates = Vehicle.GetExitCandidatesWorld();
	OutFree.Reset();
	for (FVector& Candidate : OutCandidates)
	{
		FNavLocation OnNav;
		bool bOk = Nav && Nav->ProjectPointToNavigation(Candidate, OnNav, FVector(60.f, 60.f, 300.f))
			&& FVector::Dist2D(OnNav.Location, Candidate) <= 70.f && !Vehicle.IsInsideFootprint(OnNav.Location, 30.f);
		if (bOk)
		{
			Candidate = OnNav.Location;
			bOk = IsExitFree(Vehicle, Candidate) && !Exits.ContainsByPredicate([&Candidate](const FPendingExit& Each)
			{
				return FVector::Dist2D(Each.Point, Candidate) < 90.f;
			});
		}
		OutFree.Add(bOk);
	}
}

void UVehicleSubsystem::HandleDisabled(AVehiclePawn* Vehicle)
{
	if (!Vehicle)
	{
		return;
	}
	Repairs.RemoveAll([Vehicle](const FPendingRepair& Each) { return Each.Vehicle.Get() == Vehicle; });
	const int32 Ejected = EjectCrew(Vehicle);
	FGameplayEventContext Event;
	Event.Target = Vehicle;
	Event.Location = Vehicle->GetActorLocation();
	Event.Kind = FName(*FString::FromInt(Ejected));
	GameplayEvents::RaiseWithContext(this, GameplayEvents::VehicleDisabled, Event);
}

int32 UVehicleSubsystem::EjectCrew(AVehiclePawn* Vehicle)
{
	if (!Vehicle)
	{
		return 0;
	}
	// Boarders still walking up are turned away.
	for (int32 Index = Boarding.Num() - 1; Index >= 0; --Index)
	{
		if (Boarding[Index].Vehicle.Get() == Vehicle)
		{
			VehicleSubsystemLocal::SetBlocksPawns(Boarding[Index].Operative.Get(), true);
			Boarding.RemoveAt(Index);
		}
	}
	TArray<AOperativeCharacter*> Crew;
	for (const FVehicleCrewMember& Member : Vehicle->GetCrew())
	{
		if (AOperativeCharacter* Operative = Member.Operative.Get(); Operative && !IsDisembarking(Operative))
		{
			Crew.Add(Operative);
		}
	}
	if (Crew.IsEmpty())
	{
		return 0;
	}
	Vehicle->StopDriving();
	TArray<FVector> Candidates;
	TArray<bool> Free;
	GatherExitPoints(*Vehicle, Candidates, Free);
	const APlayerController* PC = GetWorld()->GetFirstPlayerController();
	const FVector View = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : Vehicle->GetActorLocation();
	// The safe side first (away from known threats); nobody stays inside a dead hull: the blocked exits after the free ones.
	const TArray<int32> Order = VehicleRules::GetEjectionOrder(Candidates, Free, Vehicle->GetHullCentreWorld(), GatherKnownThreats(Vehicle->GetActorLocation()), View);
	int32 Next = 0;
	for (AOperativeCharacter* Operative : Crew)
	{
		FPendingExit& Exit = Exits.AddDefaulted_GetRef();
		Exit.Operative = Operative;
		Exit.Vehicle = Vehicle;
		Exit.Point = Order.IsValidIndex(Next) ? Candidates[Order[Next]] : Vehicle->GetHullAttackPoint(Operative->GetActorLocation(), 120.f);
		Exit.ClimbLeft = 0.f; // out at once (the fade covers it in real time)
		++Next;
	}
	Vehicle->SetDoorsOpen(true);
	UE_LOG(LogCodexTactics, Display, TEXT("[Vehicle] %s: crew of %d ejected"), *Vehicle->GetName(), Crew.Num());
	return Crew.Num();
}

bool UVehicleSubsystem::WalkToHull(AOperativeCharacter* Worker, const AVehiclePawn& Vehicle) const
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FVector Spot = Vehicle.GetHullAttackPoint(Worker->GetActorLocation(), 90.f);
	FNavLocation OnNav;
	if (Nav && Nav->ProjectPointToNavigation(Spot, OnNav, FVector(150.f, 150.f, 300.f)))
	{
		Spot = OnNav.Location;
	}
	return Worker->OrderMoveTo(Spot, false) == EOperativeOrderResult::Accepted;
}

bool UVehicleSubsystem::OrderRepair(const TArray<AOperativeCharacter*>& Operatives, AVehiclePawn* Vehicle)
{
	if (!Vehicle || !Vehicle->IsDisabled())
	{
		return false;
	}
	bool bTurnBased = false;
	bool bPause = false;
	VehicleSubsystemLocal::ModeFlags(GetWorld(), bTurnBased, bPause);
	const FText Speaker = Operatives.Num() > 0 && Operatives[0] ? Operatives[0]->DisplayName : Vehicle->DisplayName;
	if (bTurnBased || bPause)
	{
		PostLine(Speaker, RefusalText(bTurnBased ? EVehicleRefusal::TurnBased : EVehicleRefusal::TacticalPause, nullptr));
		return false;
	}
	if (const FPendingRepair* Busy = Repairs.FindByPredicate([Vehicle](const FPendingRepair& Each) { return Each.Vehicle.Get() == Vehicle; }))
	{
		PostLine(Busy->Operative.IsValid() ? Busy->Operative->DisplayName : Speaker, LOCTEXT("RepairBusy", "Already on it."));
		return false;
	}
	// The Engineer first (the fastest repairer); anyone else only when the hull allows it.
	AOperativeCharacter* Worker = nullptr;
	float Seconds = 0.f;
	for (AOperativeCharacter* Operative : Operatives)
	{
		const float Time = Operative ? VehicleHullRules::GetRepairSeconds(Operative->SquadRole, Vehicle->HullConfig) : 0.f;
		if (Time > 0.f && VehicleSubsystemLocal::IsAliveOperative(Operative) && !Operative->IsKnockedDown() && !IsMounted(Operative)
			&& (!Worker || Time < Seconds))
		{
			Worker = Operative;
			Seconds = Time;
		}
	}
	if (!Worker)
	{
		PostLine(Speaker, LOCTEXT("RepairNeedsEngineer", "Only the Engineer can get her running - select him and press [Y]."));
		return false;
	}
	if (UInteractionSubsystem* Interactions = GetWorld()->GetSubsystem<UInteractionSubsystem>())
	{
		Interactions->CancelUseOrder(Worker);
	}
	if (URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>())
	{
		Relocation->CancelActiveTask(Worker);
	}
	FPendingRepair& Repair = Repairs.AddDefaulted_GetRef();
	Repair.Operative = Worker;
	Repair.Vehicle = Vehicle;
	Repair.WorkTotal = Seconds;
	if (Vehicle->GetHullDistance2D(Worker->GetActorLocation()) > Vehicle->HullConfig.RepairReachCm && !WalkToHull(Worker, *Vehicle))
	{
		Repairs.Pop();
		PostLine(Worker->DisplayName, LOCTEXT("RepairNoWay", "Can't get to her."));
		return false;
	}
	PostLine(Worker->DisplayName, FText::Format(LOCTEXT("RepairOrder", "On it - I'll get the {0} running ({1} s)."), Vehicle->DisplayName,
		FMath::RoundToInt(Seconds)));
	UE_LOG(LogCodexTactics, Display, TEXT("[Vehicle] %s goes to repair %s (%.1f s)"), *Worker->GetName(), *Vehicle->GetName(), Seconds);
	return true;
}

bool UVehicleSubsystem::IsRepairing(const AOperativeCharacter* Operative) const
{
	return Repairs.ContainsByPredicate([Operative](const FPendingRepair& Each) { return Each.Operative.Get() == Operative; });
}

float UVehicleSubsystem::GetRepairProgress(const AVehiclePawn* Vehicle) const
{
	const FPendingRepair* Repair = Repairs.FindByPredicate([Vehicle](const FPendingRepair& Each) { return Each.Vehicle.Get() == Vehicle && Each.WorkLeft >= 0.f; });
	return Repair ? FMath::Clamp(1.f - Repair->WorkLeft / FMath::Max(0.01f, Repair->WorkTotal), 0.f, 1.f) : 0.f;
}

void UVehicleSubsystem::UpdateRepairs(float DeltaTime)
{
	for (int32 Index = Repairs.Num() - 1; Index >= 0; --Index)
	{
		FPendingRepair& Repair = Repairs[Index];
		AOperativeCharacter* Worker = Repair.Operative.Get();
		AVehiclePawn* Vehicle = Repair.Vehicle.Get();
		if (!Worker || !Vehicle || !Vehicle->IsDisabled() || !VehicleSubsystemLocal::IsAliveOperative(Worker) || IsMounted(Worker))
		{
			Repairs.RemoveAt(Index);
			continue;
		}
		const float Reach = Vehicle->HullConfig.RepairReachCm;
		const float Distance = Vehicle->GetHullDistance2D(Worker->GetActorLocation());
		if (Worker->IsKnockedDown() || (Repair.WorkLeft >= 0.f && Distance > Reach * 2.f))
		{
			PostLine(Worker->DisplayName, LOCTEXT("RepairInterrupted", "Repair interrupted!"));
			Repairs.RemoveAt(Index);
			continue;
		}
		Repair.Age += DeltaTime;
		if (Repair.WorkLeft < 0.f)
		{
			Repair.IdleSeconds = Worker->IsMoving() ? 0.f : Repair.IdleSeconds + DeltaTime;
			if (Distance <= Reach || (Repair.IdleSeconds > 0.5f && Distance <= Reach * 1.6f))
			{
				Worker->StopOperative();
				Worker->SetFacingPoint(Vehicle->GetClosestHullPoint(Worker->GetActorLocation()));
				Worker->SetStance(EOperativeStance::Crouching);
				Repair.WorkLeft = Repair.WorkTotal;
				UFloatingTextSubsystem::SpawnAboveOperative(Worker, TEXT("🔧 REPAIRING..."), FLinearColor(0.2f, 0.9f, 0.4f));
				UE_LOG(LogCodexTactics, Display, TEXT("[Vehicle] %s repairs %s (%.1f s)"), *Worker->GetName(), *Vehicle->GetName(), Repair.WorkTotal);
			}
			else if (Repair.IdleSeconds > 1.f && Repair.Retries < 4 && Repair.Age < 40.f)
			{
				++Repair.Retries;
				Repair.IdleSeconds = 0.f;
				WalkToHull(Worker, *Vehicle);
			}
			else if ((Repair.IdleSeconds > 1.f && Repair.Retries >= 4) || Repair.Age > 40.f)
			{
				PostLine(Worker->DisplayName, LOCTEXT("RepairNoReach", "Can't get to her."));
				Repairs.RemoveAt(Index);
			}
			continue;
		}
		Repair.WorkLeft -= DeltaTime;
		if (Repair.WorkLeft > 0.f)
		{
			continue;
		}
		Repairs.RemoveAt(Index);
		Vehicle->RepairHull();
		PostLine(Worker->DisplayName, FText::Format(LOCTEXT("Repaired", "The {0} is running again ({1}%)."), Vehicle->DisplayName,
			FMath::RoundToInt(Vehicle->GetHullHealth() / Vehicle->GetHullMaxHealth() * 100.f)));
		FGameplayEventContext Event;
		Event.Subject = Worker;
		Event.Target = Vehicle;
		Event.Location = Vehicle->GetActorLocation();
		GameplayEvents::RaiseWithContext(this, GameplayEvents::VehicleRepaired, Event);
	}
}

void UVehicleSubsystem::ApplyBlastToVehicles(UWorld* World, const FVector& Centre, float Radius, TFunctionRef<float(float)> DamageAtHullDistance,
	const AVehiclePawn* TriggeredBy, const FString& Source)
{
	const UVehicleSubsystem* Self = World ? World->GetSubsystem<UVehicleSubsystem>() : nullptr;
	if (!Self)
	{
		return;
	}
	for (AVehiclePawn* Vehicle : Self->GetVehicles())
	{
		const bool bTriggered = Vehicle == TriggeredBy;
		const float HullDistance = Vehicle->GetHullDistance2D(Centre);
		if (!bTriggered && (HullDistance > Radius || FMath::Abs(Centre.Z - Vehicle->GetActorLocation().Z) > 500.f))
		{
			continue;
		}
		const float Raw = bTriggered ? 0.f : DamageAtHullDistance(HullDistance);
		EVehicleDamageSource DamageSource = EVehicleDamageSource::Explosion;
		VehicleHullRules::GetBlastHullDamage(Raw, bTriggered, Vehicle->HullConfig, DamageSource);
		Vehicle->ApplyHullDamage(Raw, DamageSource, Source);
	}
}

AVehiclePawn* UVehicleSubsystem::FindEnemyTargetVehicle(const FVector& From, bool bHordeKnowsSquad, float* OutHullDistanceCm) const
{
	AVehiclePawn* Best = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (const TWeakObjectPtr<AVehiclePawn>& Weak : Vehicles) // every enemy asks every tick: no array copy
	{
		AVehiclePawn* Vehicle = Weak.Get();
		if (!Vehicle)
		{
			continue;
		}
		const float Distance = Vehicle->GetHullDistance2D(From);
		if (VehicleHullRules::IsEnemyTarget(Vehicle->GetCrewCount(), Vehicle->IsDisabled(), bHordeKnowsSquad, Distance, Vehicle->HullConfig)
			&& Distance < BestDistance)
		{
			Best = Vehicle;
			BestDistance = Distance;
		}
	}
	if (OutHullDistanceCm)
	{
		*OutHullDistanceCm = Best ? BestDistance : -1.f;
	}
	return Best;
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

	bool IsBoarding(const AActor* Operative)
	{
		const UWorld* World = Operative ? Operative->GetWorld() : nullptr;
		const UVehicleSubsystem* Vehicles = World ? World->GetSubsystem<UVehicleSubsystem>() : nullptr;
		return Vehicles && Vehicles->IsBoarding(Cast<AOperativeCharacter>(Operative));
	}

	AVehiclePawn* GetVehicle(const AActor* Operative)
	{
		const UWorld* World = Operative ? Operative->GetWorld() : nullptr;
		const UVehicleSubsystem* Vehicles = World ? World->GetSubsystem<UVehicleSubsystem>() : nullptr;
		return Vehicles ? Vehicles->GetVehicleOf(Operative) : nullptr;
	}
}

#undef LOCTEXT_NAMESPACE
