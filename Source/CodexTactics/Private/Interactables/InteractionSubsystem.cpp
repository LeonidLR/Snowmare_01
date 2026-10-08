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
#include "Interactables/RelocationSubsystem.h"
#include "Interactables/UseOrderRules.h"
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
	// Godot: walking up to a mine with a single click marks the leader as the defuser (the mine ignores them).
	if (ADeployableActor* Deployable = Cast<ADeployableActor>(Target))
	{
		Deployable->ApproachingDefuser = bSprint ? nullptr : Leader;
	}
	if (TryOpenMenu())
	{
		return true;
	}

	const FVector Approach = Target->GetApproachPoint(Leader->GetActorLocation());
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	if (Flow && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause)
	{
		// Godot: in the pause the approach is a planned move (clamped to the pause radius), run on release.
		const FVector Planned = Squad->PlanMove(Leader, Approach, bSprint, Flow->GetConfig().PauseOrderRadius);
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

void UInteractionSubsystem::TrapActionMenu()
{
	AInteractableActor* Target = MenuTarget.Get();
	CloseMenu();
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (Target && Leader)
	{
		Target->TrapWithGrenade(Leader);
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
	if (Target->GetDistanceTo(Leader->GetActorLocation()) > Target->InteractionDistance)
	{
		return false;
	}
	Pending.Reset();
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
	if (Target && Leader && !bDisabled)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("Action confirmed on %s by %s"), *Target->GetName(), *Leader->DisplayName.ToString());
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
	if (!Crate)
	{
		return;
	}
	CloseMenu();
	LootCrate = Crate;
	OnLootDialogChanged.Broadcast(true, Crate);
}

void UInteractionSubsystem::CloseLootDialog()
{
	ALootCrateActor* Crate = LootCrate.Get();
	LootCrate.Reset();
	OnLootDialogChanged.Broadcast(false, Crate);
}

void UInteractionSubsystem::LootItem(ELootItem Item)
{
	ALootCrateActor* Crate = LootCrate.Get();
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Crate || !Leader)
	{
		return;
	}
	const FText Taken = Crate->TakeItem(Item, Leader);
	if (!Taken.IsEmpty())
	{
		if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(Leader->DisplayName, FText::Format(NSLOCTEXT("InteractionSubsystem", "LootedOne", "📦 Took from the crate: {0}"), Taken));
		}
	}
	OnLootDialogChanged.Broadcast(true, Crate); // refresh the list
}

void UInteractionSubsystem::LootAll()
{
	ALootCrateActor* Crate = LootCrate.Get();
	const USquadSubsystem* Squad = GetWorld()->GetSubsystem<USquadSubsystem>();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (Crate && Leader)
	{
		Crate->TakeAll(Leader);
		if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(Leader->DisplayName, NSLOCTEXT("InteractionSubsystem", "LootedAll", "📦 Took ALL supplies from the supply crate!"));
		}
	}
	CloseLootDialog();
}

void UInteractionSubsystem::CancelActionMenu()
{
	CloseMenu();
}

void UInteractionSubsystem::CloseMenu()
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
