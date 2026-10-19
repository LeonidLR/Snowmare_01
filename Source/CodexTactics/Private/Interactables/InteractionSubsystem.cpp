#include "Interactables/InteractionSubsystem.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/BarrelActor.h"
#include "Interactables/DeployableActor.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/LootCrateActor.h"
#include "Core/PlayerCommands.h"
#include "Interactables/ProximityMineActor.h"
#include "Interactables/TripwireActor.h"
#include "Interactables/RelocationSubsystem.h"
#include "Interactables/UseOrderRules.h"
#include "Tutorial/TutorialSubsystem.h"
#include "UI/GameMessageSubsystem.h"

bool UInteractionSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UInteractionSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UInteractionSubsystem, STATGROUP_Tickables);
}

bool UInteractionSubsystem::RequestInteraction(AInteractableActor* Target, bool bSprint)
{
	USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Target || !Leader)
	{
		return false;
	}
	CloseMenu();
	ClearDefuser(Pending.Get());
	Pending = Target;
	// Godot: walking up to a mine with a single click marks the leader as the defuser (the mine ignores them). 2026-10-15: the
	// disarm spot is INSIDE a mine's trigger radius now, so a double click (sprint) on a live mine marks him too.
	if (ADeployableActor* Deployable = Cast<ADeployableActor>(Target))
	{
		Deployable->ApproachingDefuser = bSprint && !Target->UsesDisarmApproach() ? nullptr : Leader;
	}
	if (TryOpenMenu())
	{
		return true;
	}

	// A live trap: right up to it (DisarmStandOffCm from the charge / the wire, on his side); anything else: next to its box.
	const FVector Approach = Target->GetInteractionApproachPoint(Leader->GetActorLocation());
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	if (Flow && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause)
	{
		// Godot: in the pause the approach is a planned move (clamped to the pause radius), run on release.
		const FVector Planned = Squad->PlanMove(Leader, Approach, bSprint, Flow->GetPauseOrderRadius());
		if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
		{
			Feedback->SpawnWaypointMarker(Planned);
		}
	}
	else
	{
		Leader->OrderMoveTo(Approach, bSprint);
	}
	return true;
}

void UInteractionSubsystem::CancelInteraction()
{
	ClearDefuser(Pending.Get());
	Pending.Reset();
	CloseMenu();
}

void UInteractionSubsystem::ClearDefuser(AInteractableActor* Target) const
{
	if (ADeployableActor* Deployable = Cast<ADeployableActor>(Target))
	{
		Deployable->ApproachingDefuser.Reset();
	}
}

namespace InteractionCommands
{
	/** Demo 01 command id of a confirmed action menu (defuse / light / use), decided before the action changes the object. */
	FName ForConfirm(const AInteractableActor& Target)
	{
		if (Target.IsA<ATripwireActor>())
		{
			return PlayerCommands::DisarmTripwire;
		}
		if (const AProximityMineActor* Mine = Cast<AProximityMineActor>(&Target))
		{
			return Mine->bPlacedBySquad ? PlayerCommands::UseObject : PlayerCommands::DisarmMine;
		}
		if (Target.bTrapped)
		{
			return PlayerCommands::DisarmTrap;
		}
		if (const ABarrelActor* Barrel = Cast<ABarrelActor>(&Target); Barrel && !Barrel->IsBurning() && !Barrel->IsBurnt())
		{
			return PlayerCommands::LightBarrel;
		}
		return PlayerCommands::UseObject;
	}
}

void UInteractionSubsystem::TrapActionMenu()
{
	AInteractableActor* Target = MenuTarget.Get();
	CloseMenu();
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (UTutorialSubsystem::CheckCommandLocked(this, PlayerCommands::SetTrap))
	{
		return; // not taught yet (Demo 01 progressive unlock)
	}
	if (Target && Leader && Target->TrapWithGrenade(Leader))
	{
		PlayerCommands::ReportBy(this, PlayerCommands::SetTrap, Leader, Target);
	}
}

void UInteractionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	TickUseOrders(DeltaTime);
	if (Pending.IsValid())
	{
		TryOpenMenu();
	}
}

bool UInteractionSubsystem::TryOpenMenu()
{
	AInteractableActor* Target = Pending.Get();
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Target || !Leader)
	{
		Pending.Reset();
		return false;
	}
	if (!Target->IsInInteractionReach(Leader->GetActorLocation(), Leader->IsMoving()))
	{
		return false;
	}
	Pending.Reset();
	if (Target->UsesDisarmApproach())
	{
		// At the disarm spot: he stops and faces the charge / the wire before the menu (2026-10-15).
		Leader->SetFacingPoint(Target->GetDisarmFocusPoint(Leader->GetActorLocation()));
		UE_LOG(LogCodexTactics, Display, TEXT("%s at the disarm spot of %s: %.0f cm from it"), *Leader->DisplayName.ToString(), *Target->GetName(),
			FVector::Dist2D(Leader->GetActorLocation(), Target->GetDisarmFocusPoint(Leader->GetActorLocation())));
	}
	OpenMenuFor(Target, Leader);
	return true;
}

void UInteractionSubsystem::OpenMenuFor(AInteractableActor* Target, AOperativeCharacter* Leader)
{
	if (Target->HandleDirectInteraction(Leader))
	{
		return;
	}
	const FActionMenuRequest Request = Target->BuildActionMenu(Leader);
	if (!Request.bOpenMenu)
	{
		if (!Request.Message.IsEmpty())
		{
			if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
			{
				Messages->PostMessage(Request.MessageSpeaker, Request.Message);
			}
		}
		return;
	}
	MenuTarget = Target;
	Menu = Request.Menu;
	// Godot _open_action_menu: any object that can carry a trap offers "Set trap (N)" / "No grenades".
	Menu.bAllowTrap = Target->CanReceiveTrap();
	Menu.bTrapDisabled = Leader->GrenadesCount <= 0;
	Menu.TrapText = Leader->GrenadesCount > 0
		? FText::Format(NSLOCTEXT("InteractionSubsystem", "Trap", "Set trap ({0})"), Leader->GrenadesCount)
		: NSLOCTEXT("InteractionSubsystem", "NoGrenades", "No grenades");
	UE_LOG(LogCodexTactics, Display, TEXT("Action menu: %s [%s%s]"), *Menu.Title.ToString(), *Menu.ConfirmText.ToString(),
		Menu.bConfirmDisabled ? TEXT(", disabled") : TEXT(""));
	OnActionMenuChanged.Broadcast(true, Menu);
}

void UInteractionSubsystem::ConfirmActionMenu()
{
	AInteractableActor* Target = MenuTarget.Get();
	const bool bDisabled = Menu.bConfirmDisabled;
	CloseMenu();
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (Target && Leader && !bDisabled && UTutorialSubsystem::CheckCommandLocked(this, InteractionCommands::ForConfirm(*Target)))
	{
		return; // defuse / light / use not taught yet (Demo 01 progressive unlock)
	}
	if (Target && Leader && !bDisabled)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Action confirmed on %s by %s"), *Target->GetName(), *Leader->DisplayName.ToString());
		const UGameFlowSubsystem* CommandFlow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
		PlayerCommands::ReportBy(this, InteractionCommands::ForConfirm(*Target), Leader, Target,
			CommandFlow && CommandFlow->GetCombatMode() == ECodexCombatMode::TacticalPause);
		// User decision 2026-10-08: in the fight the barrel menu opens from afar, so "Ignite" is an order - he walks up
		// and lights it (real time), or it is planned and runs on the release (tactical pause).
		const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
		if (Flow && Target->IsA<ABarrelActor>()
			&& UseOrderRules::GetDispatch(Flow->GetPhase(), Flow->GetCombatMode()) != EUseOrderDispatch::Immediate)
		{
			OrderUse(Leader, Target);
			return;
		}
		Target->ExecuteAction(Leader);
	}
}

// --- Use orders in a fight (user decision 2026-10-08) ---

void UInteractionSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UGameFlowSubsystem* Flow = InWorld.GetSubsystem<UGameFlowSubsystem>())
	{
		Flow->OnGameFlowChanged.AddDynamic(this, &UInteractionSubsystem::HandleGameFlowChanged);
		Flow->OnTacticalPauseReleased.AddDynamic(this, &UInteractionSubsystem::HandlePauseReleased);
	}
}

void UInteractionSubsystem::PostLine(const FText& Speaker, const FText& Text) const
{
	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		Messages->PostMessage(Speaker, Text);
	}
}

bool UInteractionSubsystem::HasUseOrder(const AOperativeCharacter* Worker) const
{
	auto Matches = [Worker](const FUseOrder& Order) { return Order.Worker.Get() == Worker; };
	return UseOrders.ContainsByPredicate(Matches) || PlannedUseOrders.ContainsByPredicate(Matches);
}

void UInteractionSubsystem::CancelUseOrder(const AOperativeCharacter* Worker)
{
	if (!Worker)
	{
		return;
	}
	auto Matches = [Worker](const FUseOrder& Order) { return Order.Worker.Get() == Worker; };
	if (UseOrders.RemoveAll(Matches) + PlannedUseOrders.RemoveAll(Matches) > 0)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("%s: use order replaced by another order"), *Worker->DisplayName.ToString());
	}
}

void UInteractionSubsystem::OrderUse(AOperativeCharacter* Worker, AInteractableActor* Target)
{
	if (!Worker || !Target)
	{
		return;
	}
	CancelUseOrder(Worker);
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	const EUseOrderDispatch Dispatch = Flow ? UseOrderRules::GetDispatch(Flow->GetPhase(), Flow->GetCombatMode()) : EUseOrderDispatch::Immediate;
	if (Dispatch == EUseOrderDispatch::Immediate)
	{
		UseNow(*Worker, *Target);
		return;
	}
	if (Dispatch == EUseOrderDispatch::Execute)
	{
		StartUse(Worker, Target);
		return;
	}
	// Tactical pause: the plan replaces his planned move / relocation; the release sends him (marker meanwhile).
	FUseOrder& Plan = PlannedUseOrders.AddDefaulted_GetRef();
	Plan.Worker = Worker;
	Plan.Target = Target;
	Plan.Approach = Target->GetApproachPoint(Worker->GetActorLocation());
	if (USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>())
	{
		Squad->ClearPlannedOrder(Worker);
	}
	if (URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>())
	{
		Relocation->ClearPlannedTask(Worker);
	}
	if (UCombatFeedbackSubsystem* Feedback = GetWorld()->GetSubsystem<UCombatFeedbackSubsystem>())
	{
		Feedback->SpawnWaypointMarker(Plan.Approach);
	}
	UE_LOG(LogCodexTactics, Display, TEXT("%s: use of %s planned (pause)"), *Worker->DisplayName.ToString(), *Target->GetName());
	PostLine(Worker->DisplayName, FText::Format(NSLOCTEXT("InteractionSubsystem", "UsePlanned",
		"📋 [PLAN] {0}: {1} - move up and \"{2}\"! [SPACE - execute]"), Worker->DisplayName, Target->DisplayName,
		Target->IsA<ABarrelActor>() ? NSLOCTEXT("InteractionSubsystem", "IgniteVerb", "Ignite") : NSLOCTEXT("InteractionSubsystem", "UseVerb", "Use")));
}

void UInteractionSubsystem::StartUse(AOperativeCharacter* Worker, AInteractableActor* Target)
{
	if (!Worker || !Target)
	{
		return;
	}
	if (Target->GetDistanceTo(Worker->GetActorLocation()) <= Target->InteractionDistance)
	{
		UseNow(*Worker, *Target);
		return;
	}
	FUseOrder& Order = UseOrders.AddDefaulted_GetRef();
	Order.Worker = Worker;
	Order.Target = Target;
	Order.Approach = Target->GetApproachPoint(Worker->GetActorLocation());
	Worker->OrderMoveTo(Order.Approach, false);
	Order.IssuedGoal = Worker->GetLastMoveDestination();
	UE_LOG(LogCodexTactics, Display, TEXT("%s: walking up to use %s"), *Worker->DisplayName.ToString(), *Target->GetName());
	PostLine(Worker->DisplayName, Target->IsA<ABarrelActor>()
		? NSLOCTEXT("InteractionSubsystem", "GoIgnite", "🔥 Moving to light the barrel!")
		: FText::Format(NSLOCTEXT("InteractionSubsystem", "GoUse", "Moving to the object: {0}."), Target->DisplayName));
}

void UInteractionSubsystem::UseNow(AOperativeCharacter& Worker, AInteractableActor& Target) const
{
	Worker.StopOperative();
	const FVector Facing = (Target.GetActorLocation() - Worker.GetActorLocation()).GetSafeNormal2D();
	if (!Facing.IsNearlyZero())
	{
		Worker.SetActorRotation(FRotator(0.f, Facing.Rotation().Yaw, 0.f));
	}
	UE_LOG(LogCodexTactics, Display, TEXT("%s uses %s"), *Worker.DisplayName.ToString(), *Target.GetName());
	Target.ExecuteAction(&Worker);
}

void UInteractionSubsystem::TickUseOrders(float DeltaTime)
{
	// Indexed walk from the back: using an object (a lit barrel, a trap) posts lines and may change the flow.
	for (int32 Index = UseOrders.Num() - 1; Index >= 0; --Index)
	{
		if (!UseOrders.IsValidIndex(Index))
		{
			continue;
		}
		FUseOrder& Order = UseOrders[Index];
		AOperativeCharacter* Worker = Order.Worker.Get();
		AInteractableActor* Target = Order.Target.Get();
		if (!Worker || !Target)
		{
			UseOrders.RemoveAt(Index);
			continue;
		}
		Order.Elapsed += DeltaTime;
		Order.RetryTime -= DeltaTime;
		const FVector Goal = Worker->GetLastMoveDestination();
		const float Drift = FMath::Min(FVector::Dist2D(Goal, Order.Approach), FVector::Dist2D(Goal, Order.IssuedGoal));
		switch (UseOrderRules::GetStep(Target->GetDistanceTo(Worker->GetActorLocation()), Target->InteractionDistance, Drift, Order.Elapsed))
		{
		case EUseOrderStep::Use:
			UseOrders.RemoveAt(Index);
			UseNow(*Worker, *Target);
			break;
		case EUseOrderStep::Cancelled:
			UE_LOG(LogCodexTactics, Display, TEXT("%s: use of %s cancelled (another move order)"), *Worker->DisplayName.ToString(), *Target->GetName());
			UseOrders.RemoveAt(Index);
			break;
		case EUseOrderStep::TimedOut:
			UseOrders.RemoveAt(Index);
			PostLine(Worker->DisplayName, FText::Format(NSLOCTEXT("InteractionSubsystem", "UseTimedOut",
				"❌ Can't reach the object ({0}) - order cancelled."), Target->DisplayName));
			break;
		default:
			if (!Worker->IsMoving() && Order.RetryTime <= 0.f)
			{
				// Stalled (a stance clip, a bump): send him again.
				Order.RetryTime = 1.f;
				Order.Approach = Target->GetApproachPoint(Worker->GetActorLocation());
				Worker->OrderMoveTo(Order.Approach, false);
				Order.IssuedGoal = Worker->GetLastMoveDestination();
			}
			break;
		}
	}
}

void UInteractionSubsystem::HandlePauseReleased()
{
	TArray<FUseOrder> Plans = MoveTemp(PlannedUseOrders);
	PlannedUseOrders.Reset();
	for (const FUseOrder& Plan : Plans)
	{
		StartUse(Plan.Worker.Get(), Plan.Target.Get());
	}
}

void UInteractionSubsystem::HandleGameFlowChanged(ECodexGamePhase Phase, ECodexCombatMode CombatMode)
{
	// Turn-based combat / the end of the wave drops the use orders of the fight (the grid has its own rules).
	if (Phase != ECodexGamePhase::WaveCombat || CombatMode == ECodexCombatMode::TurnBased)
	{
		UseOrders.Reset();
		PlannedUseOrders.Reset();
	}
}

void UInteractionSubsystem::RelocateActionMenu()
{
	AInteractableActor* Target = MenuTarget.Get();
	CloseMenu();
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	URelocationSubsystem* Relocation = GetWorld()->GetSubsystem<URelocationSubsystem>();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (Target && Target->bTrapped)
	{
		// Godot _on_relocate_confirmed: moving a trapped object would set the wire off.
		if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(Leader ? Leader->DisplayName : NSLOCTEXT("InteractionSubsystem", "Soldier", "Operative"),
				NSLOCTEXT("InteractionSubsystem", "TrappedMove", "⚠️ The object is rigged with a tripwire! Defuse the trap first, or moving it will set it off!"));
		}
		return;
	}
	if (Target && Relocation)
	{
		Relocation->StartRelocate(Target, Leader);
	}
}

void UInteractionSubsystem::OpenLootDialog(ALootCrateActor* Crate)
{
	if (!Crate || UTutorialSubsystem::CheckCommandLocked(this, PlayerCommands::LootCrate))
	{
		return; // the loot lesson comes in the hut (Demo 01 progressive unlock)
	}
	CloseMenu();
	LootCrate = Crate;
	OnLootDialogChanged.Broadcast(true, Crate);
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	PlayerCommands::ReportBy(this, PlayerCommands::LootCrate, Squad ? Squad->GetLeader() : nullptr, Crate);
}

void UInteractionSubsystem::CloseLootDialog()
{
	if (ALootCrateActor* Previous = LootCrate.Get(); Previous && Previous != Crate)
	{
		Previous->HandleDialogClosed();
	}
	ALootCrateActor* Crate = LootCrate.Get();
	Crate->MarkOpen(); // normally ALootCrateActor::FinishOpening (the lid is up); a dev shortcut opens it directly
	LootCrate.Reset();
	OnLootDialogChanged.Broadcast(false, Crate);
}

void UInteractionSubsystem::LootItem(ELootItem Item)
{
	ALootCrateActor* Crate = LootCrate.Get();
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (Crate)
	{
		Crate->HandleDialogClosed(); // a running search still finishes: the items move at its grab
	}
	if (!Crate || !Leader)
	{
		return;
	}
	// Rev 2026-10-19 (game designer: cause -> effect): the search clip first, the stack moves at its grab (HandleSearchFinished).
	if (Crate->RequestSearch(Leader, Item, false))
	{
		PlayerCommands::ReportBy(this, PlayerCommands::LootTake, Leader, Crate);
	}
	OnLootDialogChanged.Broadcast(true, Crate); // "Searching..." (buttons off)
}

void UInteractionSubsystem::LootAll()
{
	ALootCrateActor* Crate = LootCrate.Get();
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Crate || !Leader)
	{
		return;
	}
	if (Crate->RequestSearch(Leader, ELootItem::Medkit, true))
	{
		PlayerCommands::ReportBy(this, PlayerCommands::LootTake, Leader, Crate);
		OnLootDialogChanged.Broadcast(true, Crate); // "Searching..." until the grab closes it
	}
	else if (!Crate->IsSearching())
	{
		CloseLootDialog(); // nothing (left) to take
	}
}

void UInteractionSubsystem::HandleSearchFinished(ALootCrateActor* Crate, AOperativeCharacter* Collector, bool bAll, const FText& Taken)
{
	UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	if (Collector && Messages)
	{
		if (bAll)
		{
			Messages->PostMessage(Collector->DisplayName, NSLOCTEXT("InteractionSubsystem", "LootedAll", "📦 Took ALL supplies from the supply crate!"));
		}
		else if (!Taken.IsEmpty())
		{
			Messages->PostMessage(Collector->DisplayName, FText::Format(NSLOCTEXT("InteractionSubsystem", "LootedOne", "📦 Took from the crate: {0}"), Taken));
		}
	}
	if (!Crate || LootCrate.Get() != Crate)
	{
		return; // the window was closed meanwhile
	}
	if (bAll || !Collector)
	{
		CloseLootDialog();
		return;
	}
}

void UInteractionSubsystem::CancelActionMenu()
{
	CloseMenu();
}

void UInteractionSubsystem::CloseMenu()
	OnLootDialogChanged.Broadcast(true, Crate); // refresh the list, buttons back on
{
	if (MenuTarget.IsValid())
	{
		ClearDefuser(MenuTarget.Get());
		MenuTarget.Reset();
		OnActionMenuChanged.Broadcast(false, Menu);
	}
}

void UInteractionSubsystem::OpenMenuNow(AInteractableActor* Target)
{
	const USquadSubsystem* Squad = GetWorld() ? GetWorld()->GetSubsystem<USquadSubsystem>() : nullptr;
	if (AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr; Leader && Target)
	{
		CancelInteraction();
		OpenMenuFor(Target, Leader);
	}
}
