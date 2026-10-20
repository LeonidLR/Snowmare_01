// ACodexTacticsPlayerController: stance keys - selected operative(s), whole squad (Alt), action bar cycle.
// Split out of CodexTacticsPlayerController.cpp verbatim (cleanup 2026-10-15; map: docs/port/REFACTOR_CONTROLLER_2026-10.md).
// Godot reference: Scenes/movements/main.gd (_input and the click / key handlers it dispatches to).

#include "Core/CodexTacticsPlayerController.h"
#include "Core/CodexTacticsPlayerControllerInternal.h"
#include "UI/FloatingTextSubsystem.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "CodexTacticsPlayerController"

void ACodexTacticsPlayerController::SetEntireSquadStance(EOperativeStance Stance)
{
	USquadSubsystem* Squad = GetSquad();
	UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>();
	if (!Squad || RefuseIfLocked(ControllerCommands::ForStance(Stance, true)))
	{
		return;
	}
	// Rev 5e item 6 (user request 2026-10-14; Godot _set_entire_squad_stance refused while the squad moved): each member goes
	// prone his own way - a walking / running one dives on the move, a slow one stops first, one in cover / carrying stays up.
	const TCHAR* Name = Stance == EOperativeStance::Prone ? TEXT("PRONE") : (Stance == EOperativeStance::Crouching ? TEXT("CROUCHED") : TEXT("STANDING"));
	for (AOperativeCharacter* Member : Squad->GetMembers())
	{
		if (Stance == EOperativeStance::Prone)
		{
			if (Member->DropProne() == EProneEntry::Refused)
			{
				continue;
			}
		}
		else
		{
			Member->SetStance(Stance);
		}
		Member->ClearStanceFromSquad(); // the player's order (Alt + Z / C / V): kept when the leader sprints
		UFloatingTextSubsystem::SpawnAboveOperative(Member, FString::Printf(TEXT("👥 SQUAD: %s"), Name), FLinearColor(0.3f, 0.95f, 1.f));
	}
	if (Messages)
	{
		Messages->PostMessage(LOCTEXT("SquadSpeaker", "SQUAD"), FText::FromString(FString::Printf(TEXT("📢 [SQUAD ORDER]: All operatives go %s!"), Name)));
	}
	ControllerCommands::Report(this, ControllerCommands::ForStance(Stance, true), nullptr, false, true);
}

void ACodexTacticsPlayerController::ApplyStance(EOperativeStance Stance)
{
	if (RefuseIfLocked(ControllerCommands::ForStance(Stance, false)))
	{
		return;
	}
	if (UTurnBasedCombatSubsystem* TurnBased = GetActiveTurnBased())
	{
		if (Stance == EOperativeStance::Standing && TurnBased->IsActiveUnitKnockedDown())
		{
			TurnBased->GetUpActiveUnit(); // knocked down: [Z] = «GET UP» (2 AP; not enough AP -> a feed line, he stays down)
			return;
		}
		TurnBased->SetActiveUnitStance(Stance); // 1 AP, active operative only
		ControllerCommands::Report(this, ControllerCommands::ForStance(Stance, false), nullptr, false, false, TurnBased->GetActiveUnit());
		return;
	}
	USquadSubsystem* Squad = GetSquad();
	if (!Squad)
	{
		return;
	}
	if (IsSquadWideModifierDown())
	{
		SetEntireSquadStance(Stance); // Godot Alt + Z / C / V
		return;
	}
	AOperativeCharacter* Leader = Squad->GetLeader();
	if (!Leader)
	{
		return;
	}

	// Rev 5e item 6 (user request 2026-10-14; was the Godot check "cannot go prone while moving"): walking / running he dives
	// prone on the move, slower he stops first (AOperativeCharacter::DropProne); only in cover / carrying / vaulting is it refused.
	if (Stance == EOperativeStance::Prone && Leader->DropProne() == EProneEntry::Refused)
	{
		if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
		{
			Messages->PostMessage(Leader->DisplayName,
				LOCTEXT("CannotProneWhileMoving", "⚠️ Cannot go prone while moving! Come to a full stop first."));
		}
		return;
	}

	// Deviation (user decision 2026-10-01; Godot _sync_squad_stances made the others follow): only the selected
	// operative changes his stance — the whole squad only with Alt + Z / C / V (SetEntireSquadStance).
	if (Stance != EOperativeStance::Prone)
	{
		Leader->SetStance(Stance);
	}
	Leader->ClearStanceFromSquad();
	ControllerCommands::Report(this, ControllerCommands::ForStance(Stance, false));

	if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
	{
		FText StanceName;
		switch (Stance)
		{
		case EOperativeStance::Standing: StanceName = LOCTEXT("StanceStanding", "STANDING"); break;
		case EOperativeStance::Crouching: StanceName = LOCTEXT("StanceCrouching", "CROUCHED"); break;
		case EOperativeStance::Prone: StanceName = LOCTEXT("StanceProne", "PRONE"); break;
		default: break;
		}

		Messages->PostMessage(Leader->DisplayName,
			FText::Format(LOCTEXT("SoloStanceFmt", "{0} stance: {1}"), Leader->DisplayName, StanceName));
	}
}

void ACodexTacticsPlayerController::CycleLeaderStance()
{
	USquadSubsystem* Squad = GetSquad();
	AOperativeCharacter* Leader = Squad ? Squad->GetLeader() : nullptr;
	if (!Leader || IsDialogueOpen() || BlockRealTimeOrder())
	{
		return;
	}
	EOperativeStance Next = static_cast<EOperativeStance>((static_cast<int32>(Leader->GetStance()) + 1) % 3);
	if (RefuseIfLocked(ControllerCommands::ForStance(Next, false)))
	{
		return;
	}
	// Rev 5e item 6: prone on the move dives / stops first (DropProne); refused (cover, carrying) -> skipped to standing (Godot).
	if (Next == EOperativeStance::Prone && Leader->DropProne() == EProneEntry::Refused)
	{
		Next = EOperativeStance::Standing;
	}
	// Godot _cycle_leader_stance, without the squad sync (user decision 2026-10-01): the selected operative only.
	if (Next != EOperativeStance::Prone)
	{
		Leader->SetStance(Next);
	}
	Leader->ClearStanceFromSquad();
	ControllerCommands::Report(this, ControllerCommands::ForStance(Next, false));
}

#undef LOCTEXT_NAMESPACE
