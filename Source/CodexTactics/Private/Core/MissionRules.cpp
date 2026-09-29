#include "Core/MissionRules.h"

#define LOCTEXT_NAMESPACE "MissionRules"

FText MissionRules::GetStartObjective()
{
	return LOCTEXT("Start", "Исследовать КПП и найти способ открыть гермоворота");
}

FText MissionRules::GetModeObjective(EMissionStartMode Mode)
{
	switch (Mode)
	{
	case EMissionStartMode::Game:
		return GetStartObjective();
	default:
		return FText::GetEmpty();
	}
}

FText MissionRules::GetModeRadio(EMissionStartMode Mode)
{
	switch (Mode)
	{
	case EMissionStartMode::Game:
		return LOCTEXT("GameRadio", "Мы у главных ворот карантинного КПП. Аномальный мороз посреди лета... Нужно запитать ворота и проникнуть внутрь.");
	default:
		return FText::GetEmpty();
	}
}

EMissionStartMode MissionRules::GetAutoStartMode(bool bQuickRestart, EMissionStartMode LastMode, bool bSkipMenu)
{
	if (bQuickRestart && LastMode != EMissionStartMode::None)
	{
		return LastMode;
	}
	return bSkipMenu ? EMissionStartMode::Game : EMissionStartMode::None;
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
			OutObjective = GetAfterVictoryObjective();
			return true;
		}
		return false;
	default:
		return false;
	}
}

FText MissionRules::GetPreparationRadio()
{
	return LOCTEXT("PrepRadio", "Мы во внутреннем дворе КПП! Отряду держать позиции, приступаем к инженерной подготовке рубежа!");
}

FText MissionRules::GetWaveRestRadio(float PreparationSeconds)
{
	return FText::Format(LOCTEXT("RestRadio", "Необходимо подготовиться к следующей волне! У вас {0} секунд на перегруппировку."),
		FMath::TruncToInt(PreparationSeconds));
}

FText MissionRules::GetVictoryRadio()
{
	return LOCTEXT("VictoryRadio", "Отличная работа, бойцы! Рубеж полностью в безопасности. Можете продолжить исследование.");
}

FText MissionRules::GetAfterVictoryObjective()
{
	return LOCTEXT("Explore", "РУБЕЖ ЗАЧИЩЕН: Исследуйте территорию КПП");
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
