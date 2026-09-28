#include "Interactables/InteractionSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "CodexTactics.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Interactables/InteractableActor.h"
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
	Pending = Target;
	if (TryOpenMenu())
	{
		return true;
	}

	const FVector Approach = Target->GetApproachPoint(Leader->GetActorLocation());
	const UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	if (Flow && Flow->GetCombatMode() == ECodexCombatMode::TacticalPause)
	{
		// Godot: in the pause the approach is a planned move (clamped to the pause radius), run on release.
		Squad->PlanMove(Leader, Approach, bSprint, Flow->GetConfig().PauseOrderRadius);
	}
	else
	{
		Leader->OrderMoveTo(Approach, bSprint);
	}
	return true;
}

void UInteractionSubsystem::CancelInteraction()
{
	Pending.Reset();
	CloseMenu();
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

void UInteractionSubsystem::CancelActionMenu()
{
	CloseMenu();
}

void UInteractionSubsystem::CloseMenu()
{
	if (MenuTarget.IsValid())
	{
		MenuTarget.Reset();
		OnActionMenuChanged.Broadcast(false, Menu);
	}
}
