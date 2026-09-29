#include "GameFlow/GameFlowStateMachine.h"

FGameFlowStateMachine::FGameFlowStateMachine(const FGameFlowConfig& InConfig)
{
	Reset(InConfig);
}

void FGameFlowStateMachine::Reset(const FGameFlowConfig& InConfig)
{
	Config = InConfig;
	Phase = ECodexGamePhase::Exploration;
	CombatMode = ECodexCombatMode::None;
	WaveIndex = 0;
	PauseCharges = Config.TacticalPauseMaxCharges;
	PauseCooldownRemaining = 0.f;
	PauseTimeRemaining = 0.f;
	PreparationTimeRemaining = 0.f;
	CutsceneTimeRemaining = 0.f;
	TurnBasedUsesThisWave = 0;
	bCombatUnlocked = false;
}

float FGameFlowStateMachine::GetTimeDilation() const
{
	switch (Phase)
	{
	case ECodexGamePhase::WaveCleared:
	case ECodexGamePhase::GameOver:
		return 0.f;
	case ECodexGamePhase::WaveCombat:
		return CombatMode == ECodexCombatMode::TacticalPause ? Config.TacticalPauseTimeDilation : 1.f;
	default:
		return 1.f;
	}
}

EGameFlowResult FGameFlowStateMachine::TriggerCombatZone()
{
	if (Phase != ECodexGamePhase::Exploration)
	{
		return EGameFlowResult::WrongPhase;
	}
	if (bCombatUnlocked)
	{
		return EGameFlowResult::CombatAlreadyUnlocked;
	}
	bCombatUnlocked = true;
	CutsceneTimeRemaining = Config.CutsceneDuration;
	SetState(ECodexGamePhase::Cutscene, ECodexCombatMode::None);
	return EGameFlowResult::Ok;
}

EGameFlowResult FGameFlowStateMachine::FinishCutscene()
{
	if (Phase != ECodexGamePhase::Cutscene)
	{
		return EGameFlowResult::WrongPhase;
	}
	WaveIndex = 1;
	CutsceneTimeRemaining = 0.f;
	PreparationTimeRemaining = Config.PreparationDuration;
	SetState(ECodexGamePhase::Preparation, ECodexCombatMode::None);
	return EGameFlowResult::Ok;
}

EGameFlowResult FGameFlowStateMachine::FinishPreparation()
{
	if (Phase != ECodexGamePhase::Preparation)
	{
		return EGameFlowResult::WrongPhase;
	}
	StartWave();
	return EGameFlowResult::Ok;
}

EGameFlowResult FGameFlowStateMachine::ToggleTacticalPause()
{
	if (Phase != ECodexGamePhase::WaveCombat)
	{
		return EGameFlowResult::NotInWave;
	}
	if (CombatMode == ECodexCombatMode::TacticalPause)
	{
		ReleaseTacticalPause();
		return EGameFlowResult::Ok;
	}
	if (CombatMode != ECodexCombatMode::RealTime)
	{
		return EGameFlowResult::NotInRealTime;
	}
	if (PauseCooldownRemaining > 0.f)
	{
		return EGameFlowResult::PauseOnCooldown;
	}
	if (PauseCharges <= 0)
	{
		return EGameFlowResult::NoPauseCharges;
	}
	--PauseCharges;
	PauseTimeRemaining = Config.TacticalPauseDuration;
	SetState(ECodexGamePhase::WaveCombat, ECodexCombatMode::TacticalPause);
	return EGameFlowResult::Ok;
}

EGameFlowResult FGameFlowStateMachine::RequestEnterTurnBased(bool bEnemiesInRange)
{
	if (Phase != ECodexGamePhase::WaveCombat)
	{
		return EGameFlowResult::NotInWave;
	}
	if (CombatMode != ECodexCombatMode::RealTime)
	{
		return EGameFlowResult::NotInRealTime;
	}
	if (Config.TurnBasedUsesPerWave > 0 && TurnBasedUsesThisWave >= Config.TurnBasedUsesPerWave)
	{
		return EGameFlowResult::TurnBasedLimitReached;
	}
	if (!bEnemiesInRange)
	{
		return EGameFlowResult::NoEnemiesInRange;
	}
	++TurnBasedUsesThisWave;
	SetState(ECodexGamePhase::WaveCombat, ECodexCombatMode::TurnBased);
	return EGameFlowResult::Ok;
}

EGameFlowResult FGameFlowStateMachine::ExitTurnBased()
{
	if (CombatMode != ECodexCombatMode::TurnBased)
	{
		return EGameFlowResult::NotInTurnBased;
	}
	PauseTimeRemaining = Config.PostTurnBasedPauseDuration;
	SetState(ECodexGamePhase::WaveCombat, ECodexCombatMode::TacticalPause);
	return EGameFlowResult::Ok;
}

EGameFlowResult FGameFlowStateMachine::NotifyWaveCleared()
{
	if (Phase != ECodexGamePhase::WaveCombat)
	{
		return EGameFlowResult::WrongPhase;
	}
	PauseTimeRemaining = 0.f;
	SetState(ECodexGamePhase::WaveCleared, ECodexCombatMode::None);
	return EGameFlowResult::Ok;
}

EGameFlowResult FGameFlowStateMachine::AdvanceAfterWave()
{
	if (Phase != ECodexGamePhase::WaveCleared)
	{
		return EGameFlowResult::WrongPhase;
	}
	if (WaveIndex >= Config.TotalWaves)
	{
		SetState(ECodexGamePhase::PostCombat, ECodexCombatMode::None);
		return EGameFlowResult::Ok;
	}
	++WaveIndex;
	PauseCharges = Config.TacticalPauseMaxCharges;
	PauseCooldownRemaining = 0.f;
	PreparationTimeRemaining = Config.WaveRestDuration;
	SetState(ECodexGamePhase::Preparation, ECodexCombatMode::None);
	return EGameFlowResult::Ok;
}

EGameFlowResult FGameFlowStateMachine::FinishPostCombat()
{
	if (Phase != ECodexGamePhase::PostCombat)
	{
		return EGameFlowResult::WrongPhase;
	}
	bCombatUnlocked = false;
	SetState(ECodexGamePhase::Exploration, ECodexCombatMode::None);
	return EGameFlowResult::Ok;
}

void FGameFlowStateMachine::RestoreForLoad(bool bInCombatUnlocked, bool bInCombatPhase, int32 InWaveIndex)
{
	const FGameFlowConfig Saved = Config;
	Reset(Saved);
	bCombatUnlocked = bInCombatUnlocked;
	if (bInCombatUnlocked && bInCombatPhase)
	{
		WaveIndex = FMath::Max(1, InWaveIndex);
		PreparationTimeRemaining = WaveIndex > 1 ? Config.WaveRestDuration : Config.PreparationDuration;
		SetState(ECodexGamePhase::Preparation, ECodexCombatMode::None);
	}
	else
	{
		WaveIndex = FMath::Max(0, InWaveIndex);
		SetState(ECodexGamePhase::Exploration, ECodexCombatMode::None);
	}
}

void FGameFlowStateMachine::TriggerGameOver()
{
	PauseTimeRemaining = 0.f;
	PreparationTimeRemaining = 0.f;
	SetState(ECodexGamePhase::GameOver, ECodexCombatMode::None);
}

void FGameFlowStateMachine::Tick(float RealDeltaSeconds)
{
	if (PauseCooldownRemaining > 0.f)
	{
		PauseCooldownRemaining -= RealDeltaSeconds;
		if (PauseCooldownRemaining <= 0.f)
		{
			PauseCooldownRemaining = 0.f;
			PauseCharges = Config.TacticalPauseMaxCharges;
		}
	}

	if (Phase == ECodexGamePhase::WaveCombat && CombatMode == ECodexCombatMode::TacticalPause)
	{
		PauseTimeRemaining -= RealDeltaSeconds;
		if (PauseTimeRemaining <= 0.f)
		{
			ReleaseTacticalPause();
		}
	}
	else if (Phase == ECodexGamePhase::Cutscene)
	{
		CutsceneTimeRemaining -= RealDeltaSeconds;
		if (CutsceneTimeRemaining <= 0.f)
		{
			FinishCutscene();
		}
	}
	else if (Phase == ECodexGamePhase::Preparation)
	{
		PreparationTimeRemaining -= RealDeltaSeconds;
		if (PreparationTimeRemaining <= 0.f)
		{
			StartWave();
		}
	}
}

void FGameFlowStateMachine::SetState(ECodexGamePhase NewPhase, ECodexCombatMode NewMode)
{
	if (Phase == NewPhase && CombatMode == NewMode)
	{
		return;
	}
	Phase = NewPhase;
	CombatMode = NewMode;
	OnStateChanged.Broadcast(Phase, CombatMode);
}

void FGameFlowStateMachine::StartWave()
{
	PreparationTimeRemaining = 0.f;
	PauseCharges = Config.TacticalPauseMaxCharges;
	PauseCooldownRemaining = 0.f;
	TurnBasedUsesThisWave = 0;
	SetState(ECodexGamePhase::WaveCombat, ECodexCombatMode::RealTime);
}

void FGameFlowStateMachine::ReleaseTacticalPause()
{
	PauseTimeRemaining = 0.f;
	if (PauseCharges <= 0 && PauseCooldownRemaining <= 0.f)
	{
		PauseCooldownRemaining = Config.TacticalPauseCooldown;
	}
	SetState(ECodexGamePhase::WaveCombat, ECodexCombatMode::RealTime);
	OnTacticalPauseReleased.Broadcast();
}
