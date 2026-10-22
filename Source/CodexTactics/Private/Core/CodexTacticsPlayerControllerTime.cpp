// ACodexTacticsPlayerController: combat time modes - Space tap (tactical pause) / hold (turn-based, back to real time).
// Split out of CodexTacticsPlayerController.cpp verbatim (cleanup 2026-10-15; map: docs/port/REFACTOR_CONTROLLER_2026-10.md).
// Godot reference: Scenes/movements/main.gd (_input and the click / key handlers it dispatches to).

#include "Core/CodexTacticsPlayerController.h"
#include "Core/CodexTacticsPlayerControllerInternal.h"
#include "Combat/HoldSphereActor.h"
#include "Combat/CombatTimeModeRules.h"
#include "Combat/HealthComponent.h"
#include "Combat/EncounterQueries.h"
#include "Core/PlayerCommands.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "UI/GameMessageSubsystem.h"
#include "Characters/OperativeCharacter.h"
#include "Characters/SquadSubsystem.h"
#include "Engine/World.h"
#include "Tactics/TurnBasedCombatSubsystem.h"
#include "Vehicles/VehicleSubsystem.h"

#define LOCTEXT_NAMESPACE "CodexTacticsPlayerController"

void ACodexTacticsPlayerController::SpacePressed()
{
	// Godot: any key skips the pre-combat cutscene.
	if (UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>(); Flow && Flow->GetPhase() == ECodexGamePhase::Cutscene)
	{
		Flow->FinishCutscene();
		return;
	}
	if (IsDialogueOpen())
	{
		DialogueNext(); // Godot: Space advances the dialogue instead of pausing
		return;
	}
	// Demo 01 progressive unlock (user request 2026-10-13): no tactical pause / turn-based before it is taught. A fight
	// (wave / horde) unlocks every tutorial lock, so Space always works there.
	if (!GetActiveTurnBased() && RefuseIfLocked(PlayerCommands::TacticalPause))
	{
		return;
	}
	SpaceInput.Press();
}

void ACodexTacticsPlayerController::SpaceReleased()
{
	SetSquadCeaseFire(false);
	if (HoldSphere)
	{
		HoldSphere->HideSphere();
	}
	if (SpaceInput.Release() == ESpaceInputAction::Tap)
	{
		HandleSpaceTap();
	}
}

void ACodexTacticsPlayerController::HandleSpaceTap()
{
	UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	// FCombatTimeModeRules: a tap toggles real time <-> tactical pause in a wave; in the turn-based fight it does nothing.
	const ECombatTimeModeRequest Request = Flow
		? FCombatTimeModeRules::ResolveSpace(Flow->GetPhase(), Flow->GetCombatMode(), ESpaceInputAction::Tap, Flow->GetConfig())
		: ECombatTimeModeRequest::None;
	if (Request != ECombatTimeModeRequest::EnterTacticalPause && Request != ECombatTimeModeRequest::ResumeRealTime)
	{
		return;
	}
	const bool bWasPaused = Flow->GetCombatMode() == ECodexCombatMode::TacticalPause;
	const EGameFlowResult Result = Flow->ToggleTacticalPause();
	if (Result == EGameFlowResult::Ok)
	{
		ControllerCommands::Report(this, bWasPaused ? PlayerCommands::PauseRelease : PlayerCommands::TacticalPause);
	}
	switch (Result)
	{
	case EGameFlowResult::Ok:
		if (bWasPaused)
		{
			if (UGameMessageSubsystem* Messages = GetWorld()->GetSubsystem<UGameMessageSubsystem>())
			{
				Messages->PostMessage(LOCTEXT("SquadSpeaker", "SQUAD"), LOCTEXT("OrdersAccepted", "▶️ Orders received! Moving out!"));
			}
		}
		else
		{
			PostHeadquarters(FText::Format(LOCTEXT("PausePlanning",
				"⏱️ PLANNING (Charges: {0}/{1}): Give your orders. They are carried out once the pause ends [SPACE]!"),
				Flow->GetPauseCharges(), Flow->GetConfig().TacticalPauseMaxCharges));
		}
		break;
	case EGameFlowResult::PauseOnCooldown:
		PostHeadquarters(FText::Format(LOCTEXT("PauseCooldown", "⏳ Tactical pause is recharging! Remaining: {0}s"),
			FText::AsNumber(Flow->GetPauseCooldownRemaining(), &FNumberFormattingOptions().SetMinimumFractionalDigits(1).SetMaximumFractionalDigits(1))));
		break;
	case EGameFlowResult::NoPauseCharges:
		PostHeadquarters(LOCTEXT("PauseNoCharges", "⏳ All 3 tactical pause charges are used up this wave! Cooldown: 20s."));
		break;
	default:
		// Outside a wave the tap opens the command bar (UI step).
		break;
	}
}

void ACodexTacticsPlayerController::HandleSpaceHold()
{
	UGameFlowSubsystem* Flow = GetWorld()->GetSubsystem<UGameFlowSubsystem>();
	USquadSubsystem* Squad = GetSquad();
	if (!Flow || !Squad || !Squad->GetLeader())
	{
		return;
	}
	const ECombatTimeModeRequest Request = FCombatTimeModeRules::ResolveSpace(Flow->GetPhase(), Flow->GetCombatMode(),
		ESpaceInputAction::Hold, Flow->GetConfig());
	if (Request == ECombatTimeModeRequest::ExitTurnBasedToRealTime)
	{
		// User request 2026-10-06: the hold leaves the grid straight into full real time.
		if (Flow->ExitTurnBasedToRealTime() == EGameFlowResult::Ok)
		{
			ControllerCommands::Report(this, PlayerCommands::TurnBasedExit);
			PostHeadquarters(LOCTEXT("TurnBasedExitRealTime",
				"▶️ Turn-based combat over. Real time: orders execute at once. [SPACE] - pause, hold - turn-based."));
		}
		return;
	}
	if (Request == ECombatTimeModeRequest::ExitTurnBasedToPause)
	{
		if (Flow->ExitTurnBased() == EGameFlowResult::Ok)
		{
			ControllerCommands::Report(this, PlayerCommands::TurnBasedExit);
			PostHeadquarters(LOCTEXT("TurnBasedExit",
				"🛡️ Turn-based combat over. Tactical pause (20s) on to prepare the squad [SPACE]."));
		}
		return;
	}
	if (Request != ECombatTimeModeRequest::EnterTurnBased)
	{
		return;
	}
	// APC P1: the grid fight has no vehicle seats yet (P3) - everyone out first.
	if (const UVehicleSubsystem* Vehicles = GetWorld()->GetSubsystem<UVehicleSubsystem>(); Vehicles && Vehicles->HasMountedOperatives())
	{
		PostHeadquarters(LOCTEXT("TurnBasedMounted", "⚠️ Get out of the APC first [Y]: it can't fight turn-based yet."));
		return;
	}

	// User decision 2026-09-30 + game designer 2026-10-22: only on flat, OPEN ground holding the squad (no platform, pit, building,
	// walls in the middle - barrels, barricades and turrets are fine): UTurnBasedCombatSubsystem::FindTurnArena searches such a grid
	// round the squad; none -> refused with the reason (never a grid inside a building with the squad stuck outside).
	if (Flow->GetCombatMode() != ECodexCombatMode::TurnBased)
	{
		const UTurnBasedCombatSubsystem* TurnBased = GetWorld()->GetSubsystem<UTurnBasedCombatSubsystem>();
		FTurnArenaChoice Arena;
		if (TurnBased && !TurnBased->FindTurnArena(Arena))
		{
			PostHeadquarters(UTurnBasedCombatSubsystem::DescribeArenaFault(Arena.LeaderFault));
			return;
		}
	}
	const bool bEnemiesNear = CombatQueries::HasEnemiesWithin(GetWorld(), Squad->GetLeader()->GetActorLocation(),
		Flow->GetConfig().TurnBasedEncounterRadius);
	const EGameFlowResult Result = Flow->RequestEnterTurnBased(bEnemiesNear);
	if (Result == EGameFlowResult::Ok)
	{
		ControllerCommands::Report(this, PlayerCommands::TurnBasedEnter);
	}
	if (Result == EGameFlowResult::NoEnemiesInRange)
	{
		PostHeadquarters(LOCTEXT("TurnBasedNoEnemies", "⚠️ No live enemies within 15m for turn-based combat!"));
	}
	else if (Result == EGameFlowResult::TurnBasedLimitReached)
	{
		PostHeadquarters(FText::Format(LOCTEXT("TurnBasedLimit", "⚠️ Turn-based combat limit for this wave reached ({0}/{0})!"),
			Flow->GetConfig().TurnBasedUsesPerWave));
	}
}

#undef LOCTEXT_NAMESPACE
