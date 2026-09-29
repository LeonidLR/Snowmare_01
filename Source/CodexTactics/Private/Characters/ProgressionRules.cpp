#include "Characters/ProgressionRules.h"
#include "Data/GodotBalanceAsset.h"

int32 ProgressionRules::NextLevelExp(int32 Level)
{
	return Level >= MaxLevel ? 2500 : Level * 250;
}

int32 ProgressionRules::AddExp(int32& Level, int32& CurrentExp, int32 Amount)
{
	if (Level >= MaxLevel)
	{
		return 0;
	}
	CurrentExp += Amount;
	int32 Gained = 0;
	int32 Needed = NextLevelExp(Level);
	while (CurrentExp >= Needed && Level < MaxLevel)
	{
		CurrentExp -= Needed;
		++Level;
		++Gained;
		Needed = NextLevelExp(Level);
	}
	return Gained;
}

float ProgressionRules::StatStep(EProgressStat Stat)
{
	return Stat == EProgressStat::Health ? 5.f : 1.f;
}

float ProgressionRules::StatCap(EProgressStat Stat)
{
	switch (Stat)
	{
	case EProgressStat::Health: return HealthCap;
	case EProgressStat::Luck: return 60.f;
	case EProgressStat::Accuracy: return 100.f;
	default: return 50.f;
	}
}

bool ProgressionRules::CanIncrease(EProgressStat Stat, float Value, int32 UnspentPoints)
{
	return UnspentPoints > 0 && Value + StatStep(Stat) <= StatCap(Stat);
}

bool ProgressionRules::CanDecrease(EProgressStat Stat, float Value, float InitialBase)
{
	return Value - StatStep(Stat) >= InitialBase;
}

int32 ProgressionRules::KillReward(EEnemyArchetype Archetype, const UGodotBalanceAsset* Config)
{
	auto Reward = [Config](const TCHAR* Key, int32 Default)
	{
		return Config ? Config->GetInt(Key, Default) : Default;
	};
	switch (Archetype)
	{
	case EEnemyArchetype::FrostHound: return Reward(TEXT("exp_reward_hound"), 9);
	case EEnemyArchetype::Spitter: return Reward(TEXT("exp_reward_spitter"), 12);
	case EEnemyArchetype::Brute: return Reward(TEXT("exp_reward_brute"), 30);
	case EEnemyArchetype::Frostbitten: return Reward(TEXT("exp_reward_frostbitten"), 7);
	case EEnemyArchetype::Cutter: return 16;
	default: return 9;
	}
}

int32 ProgressionRules::WaveClearReward(const UGodotBalanceAsset* Config)
{
	return Config ? Config->GetInt(TEXT("exp_reward_wave_complete"), 40) : 40;
}

int32 ProgressionRules::FortitudeCutPercent(float Fortitude)
{
	return static_cast<int32>(FMath::Clamp(Fortitude * 1.5f, 0.f, 50.f));
}
