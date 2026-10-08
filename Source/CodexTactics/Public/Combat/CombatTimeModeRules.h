#pragma once

#include "CoreMinimal.h"
#include "Combat/SpaceInput.h"
#include "GameFlow/GameFlowTypes.h"

/** The combat time-mode change a Space tap / hold asks for (FCombatTimeModeRules::ResolveSpace). */
enum class ECombatTimeModeRequest : uint8
{
	None,
	/** Real time -> tactical pause (tap). */
	EnterTacticalPause,
	/** Tactical pause -> real time; the planned orders run now (tap). */
	ResumeRealTime,
	/** Real time / tactical pause -> turn-based fight (hold). */
	EnterTurnBased,
	/** Turn-based fight -> full real time (hold, FGameFlowConfig::bHoldExitsTurnBasedToRealTime). */
	ExitTurnBasedToRealTime,
	/** Turn-based fight -> the free post-turn-based tactical pause (hold, Godot path). */
	ExitTurnBasedToPause
};

/** What a player order (move, stance, attack, ability, item) does in the current combat time mode. */
enum class ECombatOrderDispatch : uint8
{
	/** Runs at once while the world keeps running (exploration, preparation, real-time fight). */
	Execute,
	/** Planned now (preview marker), runs when the tactical pause ends. */
	Queue,
	/** The turn-based grid handles it (AP, cells). */
	TurnBasedGrid,
	/** Refused (only with the 2026-10-05 real-time order lock, Codex.RealTimeOrders 0). */
	Blocked
};

/**
 * RTS combat time modes (user request 2026-10-06, UE-only; supersedes the 2026-10-05 «no orders in real time» lock):
 *  1. Full real time — the default when a fight starts; orders run at once, no Space needed.
 *  2. Real time with tactical pause — a Space TAP toggles the pause (world near-stopped, orders are queued with markers,
 *     the next tap resumes and runs them).
 *  3. Turn-based — HOLDING Space TurnBasedHoldDuration (1.5 s) enters the Gorky 17 grid fight (from real time or the
 *     pause); holding TurnBasedExitHoldDuration (1.5 s) there returns to full real time.
 * A tap resolves on release before the threshold, a hold on reaching it (FSpaceInputTracker): one press is never both.
 * Pure: no world access. Applied by ACodexTacticsPlayerController (Space) and FGameFlowStateMachine (transitions).
 * Godot reference: Scenes/movements/main.gd KEY_SPACE handling (tap = active pause, hold = turn-based enter / exit).
 */
struct CODEXTACTICS_API FCombatTimeModeRules
{
	/** The time-mode change a resolved Space action asks for in Phase / Mode (None outside a wave or for Action None). */
	static ECombatTimeModeRequest ResolveSpace(ECodexGamePhase Phase, ECodexCombatMode Mode, ESpaceInputAction Action,
		const FGameFlowConfig& Config);

	/** Seconds Space must be held in Mode for the hold action (entry vs exit threshold). */
	static float GetHoldSeconds(ECodexCombatMode Mode, const FGameFlowConfig& Config);

	/** Hold progress 0..1 for the HUD bar. */
	static float GetHoldProgress(float HeldSeconds, float HoldSeconds);

	/** How a player order is dispatched in Phase / Mode; bRealTimeOrders = false is the old real-time order lock. */
	static ECombatOrderDispatch GetOrderDispatch(ECodexGamePhase Phase, ECodexCombatMode Mode, bool bRealTimeOrders);

	/** The combat mode Request leads to from Current (Current when the request changes nothing). */
	static ECodexCombatMode GetResultingMode(ECombatTimeModeRequest Request, ECodexCombatMode Current);

	/** HUD mode label in a wave: "REAL TIME" / "TACTICAL PAUSE" / "TURN-BASED"; empty outside a wave. */
	static FString GetModeLabel(ECodexGamePhase Phase, ECodexCombatMode Mode);

	/** HUD caption of the hold bar: what holding Space does now (empty when the hold does nothing). */
	static FString GetHoldCaption(ECodexGamePhase Phase, ECodexCombatMode Mode, const FGameFlowConfig& Config);
};
