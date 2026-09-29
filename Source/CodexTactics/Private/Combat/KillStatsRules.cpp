#include "Combat/KillStatsRules.h"

namespace
{
	FMemberKillStats* FindMember(FSquadKillStats& Stats, const FString& Key)
	{
		if (Key == TEXT("Командир"))
		{
			return &Stats.Commander;
		}
		if (Key == TEXT("Инженер"))
		{
			return &Stats.Engineer;
		}
		if (Key == TEXT("Медик-сапёр"))
		{
			return &Stats.Medic;
		}
		return nullptr;
	}
}

void KillStatsRules::RegisterKill(FSquadKillStats& Stats, EEnemyArchetype Type, const FString& Source, const FString& LeaderName)
{
	FString MemberKey;
	bool bTurret = false;
	bool bMine = false;
	if (Source == TEXT("Турель") || Source == TEXT("Командир"))
	{
		MemberKey = TEXT("Командир");
		bTurret = Source == TEXT("Турель");
	}
	else if (Source == TEXT("Инженер"))
	{
		MemberKey = TEXT("Инженер");
	}
	else if (Source == TEXT("Мина") || Source == TEXT("Медик-сапёр") || Source == TEXT("Медик"))
	{
		MemberKey = TEXT("Медик-сапёр");
		bMine = Source == TEXT("Мина");
	}
	else
	{
		MemberKey = LeaderName.IsEmpty() ? FString(TEXT("Командир")) : LeaderName;
	}

	FMemberKillStats* Member = FindMember(Stats, MemberKey);
	if (!Member)
	{
		return;
	}
	switch (Type)
	{
	case EEnemyArchetype::FrostHound: ++Member->Hound; break;
	case EEnemyArchetype::Spitter: ++Member->Spitter; break;
	case EEnemyArchetype::Brute: ++Member->Brute; break;
	default: break;
	}
	++Member->Total;
	if (bTurret)
	{
		// Godot: only the commander's entry has TURRET_KILLS (a turret always counts for him).
		++Member->TurretKills;
		++Stats.TurretKills;
	}
	if (bMine)
	{
		++Member->MineKills;
		++Stats.MineKills;
	}
	if (Source == TEXT("Баррикада"))
	{
		++Stats.BarricadeKills;
	}
}

FString KillStatsRules::Format(const FSquadKillStats& Stats)
{
	const FMemberKillStats& C = Stats.Commander;
	const FMemberKillStats& E = Stats.Engineer;
	const FMemberKillStats& M = Stats.Medic;
	FString Text = FString::Printf(TEXT("📊 ИТОГОВАЯ СТАТИСТИКА БОЯ (ВСЕГО УНИЧТОЖЕНО: %d):\n"), Stats.GetTotal());
	Text += FString::Printf(TEXT("🎖️ Командир: 🐺 %d | 🏹 %d | ❄️ %d  (💥 Турель: %d) ➔ %d убийств\n"), C.Hound, C.Spitter, C.Brute, C.TurretKills, C.Total);
	Text += FString::Printf(TEXT("🛠️ Инженер: 🐺 %d | 🏹 %d | ❄️ %d ➔ %d убийств\n"), E.Hound, E.Spitter, E.Brute, E.Total);
	Text += FString::Printf(TEXT("🩺 Медик-сапёр: 🐺 %d | 🏹 %d | ❄️ %d  (💣 Мины: %d) ➔ %d убийств"), M.Hound, M.Spitter, M.Brute, M.MineKills, M.Total);
	return Text;
}
