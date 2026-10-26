#include "Core/CodexTacticsPlayerController.h"

// The formation radial menu on B (stage 7 of the formation system, user decision 2026-10-26: G is the grenade, so the menu moved to
// B). Tap B = the solo / squad toggle as before (on the release); hold B (FormationMenuHoldSeconds, real time) = the radial menu at
// the cursor (pulled in to stay on screen), the cursor moves its highlight, the release picks (FormationMenuRules::Release ->
// USquadSubsystem::SetFormationMenuChoice, the same path as the dev console Codex.Formation.Menu); Esc / RMB cancel it. Only in
// exploration / the tactical pause, squad mode, >= 2 members; otherwise a hold is a plain tap. Godot reference: none (UE-only;
// docs/design/FORMATION_SECTORS.md "Selection", docs/design/FORMATION_CONTRACT.md 9).

#include "Blueprint/UserWidget.h"
#include "CodexTactics.h"
#include "Core/CodexTacticsPlayerControllerInternal.h"
#include "Characters/SquadSubsystem.h"
#include "Engine/World.h"
#include "Formation/FormationMenuRules.h"
#include "Tutorial/TutorialSubsystem.h"
#include "UI/FormationMenuInputRules.h"
#include "UI/FormationMenuWidget.h"

#define LOCTEXT_NAMESPACE "CodexTacticsPlayerController"

void ACodexTacticsPlayerController::SoloModePressed()
{
	FormationMenuKey.Press();
}

void ACodexTacticsPlayerController::SoloModeReleased()
{
	switch (FormationMenuKey.Release())
	{
	case EFormationMenuKeyAction::Toggle:
		ToggleSoloMode();
		break;
	case EFormationMenuKeyAction::Pick:
		ReleaseFormationMenu();
		break;
	default:
		break;
	}
}

int32 ACodexTacticsPlayerController::GetFormationMenuMemberCount() const
{
	const USquadSubsystem* Squad = GetSquad();
	return Squad ? Squad->GetMembers().Num() : 0;
}

bool ACodexTacticsPlayerController::CanOpenFormationMenu() const
{
	const USquadSubsystem* Squad = GetSquad();
	if (!Squad || Squad->IsSoloMode() || GetActiveTurnBased() || IsRealTimeOrderLocked() || IsDialogueOpen())
	{
		return false;
	}
	// Every item needs the same member count (FormationMenuRules::MinMembers): one operative has no formation.
	if (!FormationMenuRules::IsEnabled(EFormationMenuItem::Line, GetFormationMenuMemberCount()))
	{
		return false;
	}
	// A tutorial lock on Squad.Formation (or Squad.*) leaves B a plain tap - asked silently, the lock hint belongs to the tap.
	const UTutorialSubsystem* Tutorial = GetWorld() ? GetWorld()->GetSubsystem<UTutorialSubsystem>() : nullptr;
	return !Tutorial || !Tutorial->IsCommandLocked(PlayerCommands::FormationMenu);
}

void ACodexTacticsPlayerController::TickFormationMenu(float RealDeltaSeconds)
{
	const bool bAvailable = CanOpenFormationMenu();
	if (FormationMenuState.bOpen && !bAvailable)
	{
		CancelFormationMenu(); // solo / turn-based / a dialogue / members lost while open: closes, the choice stays
	}
	if (FormationMenuKey.Tick(RealDeltaSeconds, FormationMenuHoldSeconds, bAvailable) == EFormationMenuKeyAction::Open)
	{
		OpenFormationMenu();
	}
	if (FormationMenuState.bOpen)
	{
		UpdateFormationMenu();
	}
}

void ACodexTacticsPlayerController::OpenFormationMenu()
{
	USquadSubsystem* Squad = GetSquad();
	if (!Squad)
	{
		FormationMenuKey.Close();
		return;
	}
	if (!FormationMenuWidget)
	{
		const TSubclassOf<UFormationMenuWidget> WidgetClass = FormationMenuWidgetClass ? FormationMenuWidgetClass : TSubclassOf<UFormationMenuWidget>(UFormationMenuWidget::StaticClass());
		FormationMenuWidget = CreateWidget<UFormationMenuWidget>(this, WidgetClass);
		if (!FormationMenuWidget)
		{
			FormationMenuKey.Close();
			return;
		}
		FormationMenuWidget->SetVisibility(ESlateVisibility::Collapsed);
		FormationMenuWidget->AddToViewport(30); // over the HUD widgets, under the CommonUI menus
	}
	FormationMenuState.Choice = Squad->GetFormationMenuChoice(); // the squad holds the choice (the console sets it too)
	FormationMenuRules::Open(FormationMenuState);
	// At the cursor, pulled in so the ring and its labels stay on screen (the cursor follows a pulled centre, so Auto is under it);
	// a virtual cursor (smokes / shots) opens at the view centre.
	const FVector2D View = FormationMenuWidget->GetViewSize();
	FVector2D Cursor = View * 0.5;
	const bool bRealCursor = !FormationMenuCursorOffset.IsSet() && FormationMenuWidget->GetCursorPosition(Cursor);
	const FVector2D Centre = FormationMenuInput::ClampCentre(Cursor, View, FormationMenuWidget->GetHalfExtent(), 16.f);
	if (bRealCursor && !Centre.Equals(Cursor, 1.0))
	{
		FormationMenuWidget->WarpCursorTo(Centre);
	}
	FormationMenuWidget->ShowMenu(Centre);
	UE_LOG(LogCodexTactics, Display, TEXT("[FormationMenu] open at (%.0f, %.0f) of %.0f x %.0f, choice %s"), Centre.X, Centre.Y, View.X, View.Y,
		FormationMenuState.Choice.IsSet() ? FormationMenuRules::ToString(FormationMenuState.Choice.GetValue()) : TEXT("auto"));
	UpdateFormationMenu();
}

void ACodexTacticsPlayerController::UpdateFormationMenu()
{
	if (!FormationMenuWidget || !FormationMenuState.bOpen)
	{
		return;
	}
	FVector2D Offset = FVector2D::ZeroVector;
	if (FormationMenuCursorOffset.IsSet())
	{
		Offset = FormationMenuCursorOffset.GetValue();
	}
	else if (FVector2D Cursor; FormationMenuWidget->GetCursorPosition(Cursor))
	{
		Offset = Cursor - FormationMenuWidget->GetCentre();
	}
	FormationMenuRules::Move(FormationMenuState, static_cast<float>(Offset.X), static_cast<float>(Offset.Y), FormationMenuWidget->DeadZone);
	const USquadSubsystem* Squad = GetSquad();
	FormationMenuWidget->SetMenuView(FormationMenuState.Highlighted, Squad ? Squad->GetFormationMenuChoice() : FormationMenuState.Choice,
		GetFormationMenuMemberCount());
}

void ACodexTacticsPlayerController::ReleaseFormationMenu()
{
	UpdateFormationMenu(); // the cursor where the key came up
	USquadSubsystem* Squad = GetSquad();
	const int32 Members = GetFormationMenuMemberCount();
	LastFormationMenuEffect = FormationMenuRules::Release(FormationMenuState, Members);
	if (FormationMenuWidget)
	{
		FormationMenuWidget->HideMenu();
	}
	if (!Squad)
	{
		return;
	}
	if (LastFormationMenuEffect == EFormationMenuEffect::Picked || LastFormationMenuEffect == EFormationMenuEffect::Auto)
	{
		Squad->SetFormationMenuChoice(FormationMenuState.Choice);
		ControllerCommands::Report(this, PlayerCommands::FormationMenu);
		UE_LOG(LogCodexTactics, Display, TEXT("[FormationMenu] picked %s"), *FormationMenuInput::Label(FormationMenuState.Choice, Members).ToString());
	}
	else if (LastFormationMenuEffect == EFormationMenuEffect::Disabled)
	{
		PostHeadquarters(LOCTEXT("FormationMenuRefused", "A formation needs at least 2 operatives."));
	}
}

bool ACodexTacticsPlayerController::CancelFormationMenu()
{
	if (!FormationMenuState.bOpen)
	{
		return false;
	}
	LastFormationMenuEffect = FormationMenuRules::Cancel(FormationMenuState);
	FormationMenuKey.Close();
	if (FormationMenuWidget)
	{
		FormationMenuWidget->HideMenu();
	}
	UE_LOG(LogCodexTactics, Display, TEXT("[FormationMenu] cancelled"));
	return true;
}

#undef LOCTEXT_NAMESPACE
