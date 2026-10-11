#include "Quests/GateProgressRules.h"

#define LOCTEXT_NAMESPACE "GateProgressRules"

namespace
{
	/** The door never reaches "full" before the final wave is cleared, whatever the data says. */
	constexpr float GateMaxCapBeforeFinal = 0.99f;
}

bool GateProgressRules::ParseOpenMode(const FString& Text, EGateOpenMode& OutMode)
{
	const FString Mode = Text.TrimStartAndEnd().ToLower();
	if (Mode == TEXT("generator_waves"))
	{
		OutMode = EGateOpenMode::GeneratorWaves;
		return true;
	}
	if (Mode == TEXT("instant"))
	{
		OutMode = EGateOpenMode::Instant;
		return true;
	}
	return false;
}

FString GateProgressRules::OpenModeToString(EGateOpenMode Mode)
{
	return Mode == EGateOpenMode::Instant ? TEXT("instant") : TEXT("generator_waves");
}

int32 GateProgressRules::ResolveFinalWave(const FGateProgressConfig& Config, int32 TotalWaves)
{
	const int32 Total = FMath::Max(1, TotalWaves);
	return Config.FullOpenAfterWave > 0 ? FMath::Min(Config.FullOpenAfterWave, Total) : Total;
}

bool GateProgressRules::IsFightRunning(ECodexGamePhase Phase, ECodexCombatMode Mode)
{
	return Phase == ECodexGamePhase::WaveCombat && Mode != ECodexCombatMode::TurnBased;
}

bool GateProgressRules::IsFinalWaveCleared(ECodexGamePhase Phase, int32 WaveIndex, int32 FinalWave)
{
	switch (Phase)
	{
	case ECodexGamePhase::WaveCleared:
		return WaveIndex >= FinalWave;
	case ECodexGamePhase::PostCombat:
		return true;
	case ECodexGamePhase::Preparation:
	case ECodexGamePhase::WaveCombat:
		return WaveIndex > FinalWave; // a later wave than the door's one
	default:
		return false;
	}
}

float GateProgressRules::GetProgressCap(const FGateProgressConfig& Config, int32 WaveIndex, int32 FinalWave, bool bFinalWaveCleared)
{
	if (bFinalWaveCleared)
	{
		return 1.f;
	}
	const float MaxBeforeFinal = FMath::Clamp(Config.MaxProgressBeforeFinal, 0.f, GateMaxCapBeforeFinal);
	const float Share = FMath::Clamp(static_cast<float>(WaveIndex) / static_cast<float>(FMath::Max(1, FinalWave)), 0.f, 1.f);
	return MaxBeforeFinal * Share;
}

FGateProgressStep GateProgressRules::Step(const FGateProgressConfig& Config, float Progress, const FGateProgressInputs& Inputs, float DeltaSeconds)
{
	FGateProgressStep Result;
	Result.Progress = FMath::Clamp(Progress, 0.f, 1.f);
	if (!Inputs.bUnlocked)
	{
		Result.Status = EGateProgressStatus::Locked;
		return Result;
	}
	if (Config.Mode == EGateOpenMode::Instant || Result.Progress >= 1.f)
	{
		Result.Progress = 1.f;
		Result.Status = EGateProgressStatus::Opening;
		Result.bOpenFully = true;
		return Result;
	}
	if (!Inputs.bGeneratorWorking)
	{
		Result.Status = EGateProgressStatus::NoPower; // paused, never reversed
		return Result;
	}
	if (Inputs.bFinalWaveCleared)
	{
		Result.Progress = 1.f;
		Result.Status = EGateProgressStatus::Opening;
		Result.bOpenFully = true;
		return Result;
	}
	if (!Inputs.bFightRunning)
	{
		Result.Status = EGateProgressStatus::Idle;
		return Result;
	}
	const float Cap = GetProgressCap(Config, Inputs.WaveIndex, Inputs.FinalWave, false);
	if (Result.Progress >= Cap)
	{
		Result.Status = EGateProgressStatus::HoldingForWave;
		return Result;
	}
	Result.Progress = FMath::Min(Cap, Result.Progress + FMath::Max(0.f, Config.ProgressPerSecond) * FMath::Max(0.f, DeltaSeconds));
	Result.Status = EGateProgressStatus::Advancing;
	return Result;
}

float GateProgressRules::GetCrackOffset(float Progress, float MaxCrackCm)
{
	return FMath::Clamp(Progress, 0.f, 1.f) * FMath::Max(0.f, MaxCrackCm);
}

bool GateProgressRules::ShouldShowStatus(EGateProgressStatus Status)
{
	return Status != EGateProgressStatus::Locked && Status != EGateProgressStatus::Open;
}

FString GateProgressRules::GetStatusText(EGateProgressStatus Status, float Progress)
{
	const int32 Percent = FMath::FloorToInt(FMath::Clamp(Progress, 0.f, 1.f) * 100.f + KINDA_SMALL_NUMBER);
	switch (Status)
	{
	case EGateProgressStatus::Locked:
		return TEXT("BLAST DOOR LOCKED — NO POWER TO THE TERMINAL");
	case EGateProgressStatus::Idle:
		return FString::Printf(TEXT("BLAST DOOR %d%% — GENERATOR ONLINE"), Percent);
	case EGateProgressStatus::Advancing:
		return FString::Printf(TEXT("BLAST DOOR %d%% — OPENING SLOWLY"), Percent);
	case EGateProgressStatus::HoldingForWave:
		return FString::Printf(TEXT("BLAST DOOR %d%% — GRID OVERLOADED, HOLD THE WAVE"), Percent);
	case EGateProgressStatus::NoPower:
		return FString::Printf(TEXT("BLAST DOOR %d%% — GENERATOR OFFLINE"), Percent);
	case EGateProgressStatus::Opening:
		return TEXT("BLAST DOOR OPENING — GET INSIDE");
	case EGateProgressStatus::Open:
	default:
		return TEXT("BLAST DOOR OPEN");
	}
}

bool GateProgressRules::GetRadioLine(EGateProgressStatus Previous, EGateProgressStatus Next, bool bFinalWaveCleared, FText& OutSpeaker, FText& OutText)
{
	if (Previous == Next || Next == EGateProgressStatus::Locked || Next == EGateProgressStatus::Open)
	{
		return false;
	}
	OutSpeaker = LOCTEXT("HQ", "HQ");
	if (Previous == EGateProgressStatus::Locked)
	{
		if (Next == EGateProgressStatus::Opening)
		{
			return false; // opened outright (instant mode, «Start Battle»): no fight to warn about
		}
		OutText = LOCTEXT("Unlocked", "Generator is humming, but the power grid is overloaded! The blast door servos will only crawl while the generator runs. Hold the line until the last wave is down and the voltage stabilizes.");
		return true;
	}
	switch (Next)
	{
	case EGateProgressStatus::NoPower:
		OutText = bFinalWaveCleared
			? LOCTEXT("FinalNoPower", "That was the last wave, but the blast door has no power! Get the generator running to open it.")
			: LOCTEXT("NoPower", "The generator is down and the blast door has stopped! Engineer, get it running again!");
		return true;
	case EGateProgressStatus::Opening:
		OutText = LOCTEXT("Opening", "Voltage has stabilized - the blast door is opening! Get the squad inside!");
		return true;
	case EGateProgressStatus::Advancing:
	case EGateProgressStatus::Idle:
	case EGateProgressStatus::HoldingForWave:
		if (Previous == EGateProgressStatus::NoPower)
		{
			OutText = LOCTEXT("PowerBack", "Power is back - the blast door servos are moving again.");
			return true;
		}
		if (Next == EGateProgressStatus::HoldingForWave && Previous == EGateProgressStatus::Advancing)
		{
			OutText = LOCTEXT("Holding", "The grid can't push the blast door any further until this wave is beaten back. Hold on!");
			return true;
		}
		return false;
	default:
		return false;
	}
}

FText GateProgressRules::GetHoldObjective()
{
	return LOCTEXT("HoldObjective", "Hold the line until the blast door opens (keep the generator running)");
}

void GateProgressRules::AdjustQuestLine(EInteractableType Type, bool bGatePowered, bool bGateOpen, FQuestInteractionResult& InOutResult)
{
	if (bGateOpen)
	{
		return;
	}
	if (Type == EInteractableType::GateTerminal && InOutResult.Event == EQuestEvent::GateOpened)
	{
		InOutResult.Text = LOCTEXT("TerminalPaced", "Applying voltage to the servo drives... The locks click, but the leaves barely move - the grid is overloaded!");
	}
	else if (Type == EInteractableType::GateTerminal && bGatePowered)
	{
		InOutResult.Text = LOCTEXT("TerminalPacedDone", "Gate power is on, but the grid is overloaded. The servos only crawl while the generator runs.");
	}
	else if (Type == EInteractableType::Gate && bGatePowered)
	{
		InOutResult.Text = LOCTEXT("GatePaced", "The blast door is still creeping open on generator power. Hold the line until it is fully open!");
	}
}

#undef LOCTEXT_NAMESPACE
