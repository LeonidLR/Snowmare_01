#include "Core/MissionRules.h"

#define LOCTEXT_NAMESPACE "MissionRules"

FText MissionRules::GetStartObjective()
{
	return LOCTEXT("Start", "Исследовать КПП и найти способ открыть гермоворота");
}

bool MissionRules::GetPhaseObjective(ECodexGamePhase Phase, int32 WaveIndex, float PreparationSeconds, bool bAfterCombat, FText& OutObjective)
{
	switch (Phase)
	{
	case ECodexGamePhase::Preparation:
		OutObjective = WaveIndex <= 1
			? LOCTEXT("FirstPreparation", "ПОДГОТОВКА К ОБОРОНЕ: расставьте турели, баррикады и мины ([Space] — приказы)")
			: FText::Format(LOCTEXT("RestPreparation", "ПОДГОТОВКА: {0} сек до волны {1}. Укрепите оборону!"),
				FMath::TruncToInt(PreparationSeconds), WaveIndex);
		return true;
	case ECodexGamePhase::PostCombat:
		OutObjective = LOCTEXT("Victory", "РУБЕЖ ЗАЧИЩЕН: Отряд выжил! Перегруппировка...");
		return true;
	case ECodexGamePhase::Exploration:
		if (bAfterCombat)
		{
			OutObjective = LOCTEXT("Explore", "РУБЕЖ ЗАЧИЩЕН: Исследуйте территорию КПП");
			return true;
		}
		return false;
	default:
		return false;
	}
}

FText MissionRules::GetWaveObjective(int32 WaveIndex, int32 EnemyCount)
{
	return FText::Format(LOCTEXT("Wave", "ОБОРОНА: Отразить волну {0}! Врагов: {1}"), WaveIndex, EnemyCount);
}

FText MissionRules::GetFailureReason(const FText& OperativeName, float ColdLevel)
{
	return ColdLevel >= FrozenDeathColdLevel
		? FText::Format(LOCTEXT("Frozen", "Оперативник {0} погиб от критического переохлаждения!"), OperativeName)
		: FText::Format(LOCTEXT("Wounds", "Оперативник {0} погиб в бою от полученных ранений!"), OperativeName);
}

FText MissionRules::GetFailureRadio(const FText& OperativeName)
{
	return FText::Format(LOCTEXT("Radio", "Внимание! Связь с {0} потеряна. Миссия провалена."), OperativeName);
}

#undef LOCTEXT_NAMESPACE
