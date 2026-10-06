#include "Combat/CombatTimeModeRules.h"

ECombatTimeModeRequest FCombatTimeModeRules::ResolveSpace(ECodexGamePhase Phase, ECodexCombatMode Mode, ESpaceInputAction Action,
	const FGameFlowConfig& Config)
{
	if (Phase != ECodexGamePhase::WaveCombat || Action == ESpaceInputAction::None)
	{
		return ECombatTimeModeRequest::None;
	}
	if (Action == ESpaceInputAction::Tap)
	{
		switch (Mode)
		{
		case ECodexCombatMode::RealTime: return ECombatTimeModeRequest::EnterTacticalPause;
		case ECodexCombatMode::TacticalPause: return ECombatTimeModeRequest::ResumeRealTime;
		default: return ECombatTimeModeRequest::None; // turn-based: a tap does not leave the grid
		}
	}
	switch (Mode)
	{
	case ECodexCombatMode::RealTime:
		return ECombatTimeModeRequest::EnterTurnBased;
	case ECodexCombatMode::TacticalPause:
		return Config.bAllowTurnBasedFromTacticalPause ? ECombatTimeModeRequest::EnterTurnBased : ECombatTimeModeRequest::None;
	case ECodexCombatMode::TurnBased:
		return Config.bHoldExitsTurnBasedToRealTime ? ECombatTimeModeRequest::ExitTurnBasedToRealTime : ECombatTimeModeRequest::ExitTurnBasedToPause;
	default:
		return ECombatTimeModeRequest::None;
	}
}

float FCombatTimeModeRules::GetHoldSeconds(ECodexCombatMode Mode, const FGameFlowConfig& Config)
{
	return FMath::Max(0.1f, Mode == ECodexCombatMode::TurnBased ? Config.TurnBasedExitHoldDuration : Config.TurnBasedHoldDuration);
}

float FCombatTimeModeRules::GetHoldProgress(float HeldSeconds, float HoldSeconds)
{
	return FMath::Clamp(HeldSeconds / FMath::Max(0.01f, HoldSeconds), 0.f, 1.f);
}

ECombatOrderDispatch FCombatTimeModeRules::GetOrderDispatch(ECodexGamePhase Phase, ECodexCombatMode Mode, bool bRealTimeOrders)
{
	if (Phase != ECodexGamePhase::WaveCombat)
	{
		return ECombatOrderDispatch::Execute;
	}
	switch (Mode)
	{
	case ECodexCombatMode::TacticalPause: return ECombatOrderDispatch::Queue;
	case ECodexCombatMode::TurnBased: return ECombatOrderDispatch::TurnBasedGrid;
	case ECodexCombatMode::RealTime: return bRealTimeOrders ? ECombatOrderDispatch::Execute : ECombatOrderDispatch::Blocked;
	default: return ECombatOrderDispatch::Execute;
	}
}

ECodexCombatMode FCombatTimeModeRules::GetResultingMode(ECombatTimeModeRequest Request, ECodexCombatMode Current)
{
	switch (Request)
	{
	case ECombatTimeModeRequest::EnterTacticalPause:
	case ECombatTimeModeRequest::ExitTurnBasedToPause:
		return ECodexCombatMode::TacticalPause;
	case ECombatTimeModeRequest::ResumeRealTime:
	case ECombatTimeModeRequest::ExitTurnBasedToRealTime:
		return ECodexCombatMode::RealTime;
	case ECombatTimeModeRequest::EnterTurnBased:
		return ECodexCombatMode::TurnBased;
	default:
		return Current;
	}
}

FString FCombatTimeModeRules::GetModeLabel(ECodexGamePhase Phase, ECodexCombatMode Mode)
{
	if (Phase != ECodexGamePhase::WaveCombat)
	{
		return FString();
	}
	switch (Mode)
	{
	case ECodexCombatMode::RealTime: return TEXT("РЕАЛЬНОЕ ВРЕМЯ");
	case ECodexCombatMode::TacticalPause: return TEXT("ТАКТИЧЕСКАЯ ПАУЗА");
	case ECodexCombatMode::TurnBased: return TEXT("ПОШАГОВЫЙ БОЙ");
	default: return FString();
	}
}

FString FCombatTimeModeRules::GetHoldCaption(ECodexGamePhase Phase, ECodexCombatMode Mode, const FGameFlowConfig& Config)
{
	switch (ResolveSpace(Phase, Mode, ESpaceInputAction::Hold, Config))
	{
	case ECombatTimeModeRequest::EnterTurnBased: return TEXT("ВХОД В ПОШАГОВЫЙ БОЙ");
	case ECombatTimeModeRequest::ExitTurnBasedToRealTime: return TEXT("ВОЗВРАТ В РЕАЛЬНОЕ ВРЕМЯ");
	case ECombatTimeModeRequest::ExitTurnBasedToPause: return TEXT("ВОЗВРАТ В ТАКТИЧЕСКУЮ ПАУЗУ");
	default: return FString();
	}
}
