#include "Interactables/InteractionSubsystem.h"
#include "Combat/CombatFeedbackSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/DeployableActor.h"
#include "Interactables/InteractableActor.h"
#include "Interactables/LootCrateActor.h"
#include "Interactables/RelocationSubsystem.h"
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
	// Godot _open_action_menu: any object that can carry a trap offers «Заминировать (N)» / «Нет гранат».
	Menu.bAllowTrap = Target->CanReceiveTrap();
	Menu.bTrapDisabled = Leader->GrenadesCount <= 0;
	Menu.TrapText = Leader->GrenadesCount > 0
		? FText::Format(NSLOCTEXT("InteractionSubsystem", "Trap", "Заминировать ({0})"), Leader->GrenadesCount)
		: NSLOCTEXT("InteractionSubsystem", "NoGrenades", "Нет гранат");
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
		Target->ExecuteAction(Leader);
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
			Messages->PostMessage(Leader ? Leader->DisplayName : NSLOCTEXT("InteractionSubsystem", "Soldier", "Боец"),
				NSLOCTEXT("InteractionSubsystem", "TrappedMove", "⚠️ Объект заминирован растяжкой! Сначала обезвредьте ловушку, иначе перемещение вызовет взрыв!"));
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
			Messages->PostMessage(Leader->DisplayName, FText::Format(NSLOCTEXT("InteractionSubsystem", "LootedOne", "📦 Забрал(а) из ящика: {0}"), Taken));
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
			Messages->PostMessage(Leader->DisplayName, NSLOCTEXT("InteractionSubsystem", "LootedAll", "📦 Забрал(а) ВСЕ припасы из ящика снабжения!"));
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
